// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "AI/MonsterAIComponent.h"
#include "AI/MonsterHearingComponent.h"
#include "AI/MonsterLightAverseComponent.h"
#include "AI/MonsterSightFreezeComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "Gameplay/LostWithinLightSubsystem.h"
#include "Gameplay/LostWithinNoiseSubsystem.h"
#include "LostWithinCharacter.h"
#include "Misc/AutomationTest.h"

#define LOSTWITHIN_TORCH_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

namespace TorchLightTestHelpers
{
	struct FScopedTorchTestWorld
	{
		UWorld* World = nullptr;

		FScopedTorchTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			if (World && GEngine)
			{
				GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
				World->InitializeActorsForPlay(FURL());
				World->BeginPlay();
				if (AWorldSettings* Settings = World->GetWorldSettings())
				{
					Settings->NotifyBeginPlay();
				}
			}
		}

		~FScopedTorchTestWorld()
		{
			if (World)
			{
				World->EndPlay(EEndPlayReason::Quit);
				World->DestroyWorld(false);
				if (GEngine)
				{
					GEngine->DestroyWorldContext(World);
				}
			}
			CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
		}
	};

	/** A wall-torch stand-in: an actor with a point light named like the maze torches (900 cm, 3500). */
	UPointLightComponent* SpawnTorch(UWorld* World, const FVector& Location)
	{
		AStaticMeshActor* Torch = World->SpawnActor<AStaticMeshActor>(Location, FRotator::ZeroRotator);
		if (!Torch)
		{
			return nullptr;
		}
		Torch->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
		UPointLightComponent* Light = NewObject<UPointLightComponent>(Torch, TEXT("TorchPointLight"));
		Light->SetMobility(EComponentMobility::Movable);
		Light->SetupAttachment(Torch->GetRootComponent());
		Light->AttenuationRadius = 900.0f;
		Light->Intensity = 3500.0f;
		Light->RegisterComponent();
		Light->SetWorldLocation(Location);
		return Light;
	}

	AStaticMeshActor* SpawnWall(UWorld* World, const FVector& Location, const FVector& Scale)
	{
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		AStaticMeshActor* Wall = Cube ? World->SpawnActor<AStaticMeshActor>(Location, FRotator::ZeroRotator) : nullptr;
		if (Wall)
		{
			Wall->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
			Wall->GetStaticMeshComponent()->SetStaticMesh(Cube);
			Wall->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
			Wall->SetActorScale3D(Scale);
		}
		return Wall;
	}

	ACharacter* SpawnMonster(UWorld* World, const FVector& Location, TFunctionRef<void(ACharacter*)> AddComponents)
	{
		const FTransform Transform(Location);
		ACharacter* Monster = World->SpawnActorDeferred<ACharacter>(ACharacter::StaticClass(), Transform, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Monster)
		{
			return nullptr;
		}
		Monster->AutoPossessAI = EAutoPossessAI::Disabled;
		UMonsterAIComponent* CatAI = NewObject<UMonsterAIComponent>(Monster, TEXT("CatReactionAI"));
		CatAI->RegisterComponent();
		AddComponents(Monster);
		Monster->FinishSpawning(Transform);
		return Monster;
	}

	void Tick(UActorComponent* Component)
	{
		Component->TickComponent(0.01f, LEVELTICK_All, nullptr);
	}
}

// -----------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLostWithinTorchLightQueryTest, "LostWithin.AI.TorchLight.Query",
	LOSTWITHIN_TORCH_TEST_FLAGS)

bool FLostWithinTorchLightQueryTest::RunTest(const FString& Parameters)
{
	using namespace TorchLightTestHelpers;
	FScopedTorchTestWorld Env;
	ULostWithinLightSubsystem* Light = ULostWithinLightSubsystem::Get(Env.World);
	if (!TestNotNull(TEXT("World"), Env.World) || !TestNotNull(TEXT("Light subsystem"), Light))
	{
		return false;
	}
	TestEqual(TEXT("Default torch radius is the 400 cm bright pool"), Light->TorchLitRadius, 400.0f);

	UPointLightComponent* Torch = SpawnTorch(Env.World, FVector(0.0f, 0.0f, 200.0f));
	if (!TestNotNull(TEXT("Torch"), Torch))
	{
		return false;
	}
	Light->RefreshTorches();
	TestEqual(TEXT("One torch light collected"), Light->GetNumTorchLights(), 1);

	FVector LightAt = FVector::ZeroVector;
	TestTrue(TEXT("300 cm from the torch is lit"), Light->IsPointTorchLit(FVector(300.0f, 0.0f, 200.0f), nullptr, nullptr, &LightAt));
	TestTrue(TEXT("Reports the lighting torch"), LightAt.Equals(FVector(0.0f, 0.0f, 200.0f), 1.0f));
	TestFalse(TEXT("500 cm (beyond the 400 cm pool, inside the 900 cm range) is dark"),
		Light->IsPointTorchLit(FVector(500.0f, 0.0f, 200.0f)));

	Light->TorchLitRadius = 600.0f;
	TestTrue(TEXT("A larger radius setting lights 500 cm"), Light->IsPointTorchLit(FVector(500.0f, 0.0f, 200.0f)));
	Light->TorchLitRadius = 400.0f;

	AStaticMeshActor* Wall = SpawnWall(Env.World, FVector(150.0f, 0.0f, 200.0f), FVector(0.2f, 4.0f, 4.0f));
	if (TestNotNull(TEXT("Wall"), Wall))
	{
		TestFalse(TEXT("A wall between torch and point keeps it dark"), Light->IsPointTorchLit(FVector(300.0f, 0.0f, 200.0f)));
		TestTrue(TEXT("The other side of the torch is still lit"), Light->IsPointTorchLit(FVector(-300.0f, 0.0f, 200.0f)));
		Wall->Destroy();
	}

	Torch->SetVisibility(false);
	TestFalse(TEXT("A hidden torch lights nothing"), Light->IsPointTorchLit(FVector(300.0f, 0.0f, 200.0f)));
	return true;
}

// -----------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLostWithinTorchLightGhoulTest, "LostWithin.AI.TorchLight.Ghoul",
	LOSTWITHIN_TORCH_TEST_FLAGS)

bool FLostWithinTorchLightGhoulTest::RunTest(const FString& Parameters)
{
	using namespace TorchLightTestHelpers;
	FScopedTorchTestWorld Env;
	ULostWithinLightSubsystem* Light = ULostWithinLightSubsystem::Get(Env.World);
	if (!TestNotNull(TEXT("World"), Env.World) || !TestNotNull(TEXT("Light subsystem"), Light))
	{
		return false;
	}

	UMonsterLightAverseComponent* Averse = nullptr;
	ACharacter* Ghoul = SpawnMonster(Env.World, FVector(300.0f, 0.0f, 0.0f), [&Averse](ACharacter* Monster)
	{
		Averse = NewObject<UMonsterLightAverseComponent>(Monster, TEXT("LightAverse"));
		// Pins the distance-based dark chase and the plain edge hold; the hearing chase, orbit and snuff are tested
		// in LostWithin.AI.Senses.
		Averse->bDarkStalkRequiresHearing = false;
		Averse->bOrbitTorchEdge = false;
		Averse->TorchSnuffHoldTime = 0.0f;
		Averse->RegisterComponent();
	});
	UPointLightComponent* Torch = SpawnTorch(Env.World, FVector(500.0f, 0.0f, 100.0f));
	if (!TestNotNull(TEXT("Ghoul"), Ghoul) || !TestNotNull(TEXT("LightAverse"), Averse) || !TestNotNull(TEXT("Torch"), Torch))
	{
		return false;
	}
	Light->RefreshTorches();

	// 1. Inside a torch's light: repelled in the same tick, straight away from the torch.
	Tick(Averse);
	TestTrue(TEXT("Torch light repels the Ghoul"), Averse->IsRepelled());
	TestTrue(TEXT("Marked torch-lit"), Averse->IsTorchLit());
	TestFalse(TEXT("Not reported as flashlight-lit"), Averse->IsLitByFlashlight());
	TestEqual(TEXT("Retreats at once"), Averse->GetLightState(), EGhoulLightState::Retreating);
	TestTrue(TEXT("Shields its face"), Averse->IsShieldingFace());
	TestTrue(TEXT("Retreats away from the torch (-X)"), Averse->CurrentRetreatDirection.X < -0.9f);
	TestFalse(TEXT("Roam loop waits while repelled"), UMonsterAIComponent::CanRunNormalAI(Ghoul));

	// 2. Out of the light: the retreat ends in the same tick.
	Ghoul->SetActorLocation(FVector(-300.0f, 0.0f, 0.0f));
	Tick(Averse);
	TestFalse(TEXT("Dark again: no longer repelled"), Averse->IsRepelled());
	TestFalse(TEXT("No longer torch-lit"), Averse->IsTorchLit());

	// 3. The edge: a step toward the torch would enter its light; away from it would not.
	Ghoul->SetActorLocation(FVector(30.0f, 0.0f, 0.0f));
	Tick(Averse);
	TestFalse(TEXT("Standing just outside the pool is dark"), Averse->IsRepelled());
	TestTrue(TEXT("Stepping toward the torch would enter its light"), Averse->WouldStepIntoTorchLight(FVector(500.0f, 0.0f, 0.0f)));
	TestFalse(TEXT("Stepping away stays dark"), Averse->WouldStepIntoTorchLight(FVector(-500.0f, 0.0f, 0.0f)));

	// 4. Dark chase toward a player standing in the light: holds at the edge instead of walking in.
	UClass* PlayerClass = LoadClass<ALostWithinCharacter>(
		nullptr, TEXT("/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter.BP_FirstPersonCharacter_C"));
	APlayerController* Controller = Env.World->SpawnActor<APlayerController>();
	ALostWithinCharacter* Player = PlayerClass
		? Env.World->SpawnActor<ALostWithinCharacter>(PlayerClass, FVector(700.0f, 0.0f, 0.0f), FRotator::ZeroRotator) : nullptr;
	if (TestNotNull(TEXT("Player controller"), Controller) && TestNotNull(TEXT("Player"), Player))
	{
		Controller->Possess(Player);
		Tick(Averse);
		TestTrue(TEXT("Dark chase toward the player"), Averse->IsChasingPlayer());
		TestTrue(TEXT("Holds at the torch's edge"), Averse->IsHoldingAtTorchEdge());
		TestFalse(TEXT("Roam loop still waits while holding"), UMonsterAIComponent::CanRunNormalAI(Ghoul));

		Torch->SetVisibility(false);
		Tick(Averse);
		TestTrue(TEXT("Torch out: still chasing"), Averse->IsChasingPlayer());
		TestFalse(TEXT("Torch out: no longer holding"), Averse->IsHoldingAtTorchEdge());
		Torch->SetVisibility(true);
	}

	// 5. Torch repulsion off: the light is ignored.
	Averse->bTorchLightRepels = false;
	Ghoul->SetActorLocation(FVector(300.0f, 0.0f, 0.0f));
	Tick(Averse);
	TestFalse(TEXT("bTorchLightRepels false ignores torches"), Averse->IsRepelled());
	return true;
}

// -----------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLostWithinTorchLightDollTest, "LostWithin.AI.TorchLight.DollLitAndWatched",
	LOSTWITHIN_TORCH_TEST_FLAGS)

bool FLostWithinTorchLightDollTest::RunTest(const FString& Parameters)
{
	using namespace TorchLightTestHelpers;
	FScopedTorchTestWorld Env;
	ULostWithinLightSubsystem* Light = ULostWithinLightSubsystem::Get(Env.World);
	if (!TestNotNull(TEXT("World"), Env.World) || !TestNotNull(TEXT("Light subsystem"), Light))
	{
		return false;
	}

	// The viewer: a player controller at the origin looking down +X (its camera has not moved yet).
	APlayerController* Viewer = Env.World->SpawnActor<APlayerController>();
	UMonsterSightFreezeComponent* Freeze = nullptr;
	ACharacter* Doll = SpawnMonster(Env.World, FVector(500.0f, 0.0f, 0.0f), [&Freeze](ACharacter* Monster)
	{
		Freeze = NewObject<UMonsterSightFreezeComponent>(Monster, TEXT("SightFreezeAI"));
		Freeze->bRequireLineOfSight = false; // no level geometry here
		// Pins the older always-stalking Doll; dormancy, lit sight and the gaze flicker are tested in LostWithin.AI.Senses.
		Freeze->bDormantInDarkness = false;
		Freeze->bFreezeWhenWatched = true;
		Freeze->bRequireSightOfPlayer = false;
		Freeze->GazeFlickerTime = 0.0f;
		Freeze->MimicPlayerRange = 0.0f;
		Freeze->RegisterComponent();
	});
	UPointLightComponent* Torch = SpawnTorch(Env.World, FVector(500.0f, 0.0f, 300.0f));
	if (!TestNotNull(TEXT("Viewer"), Viewer) || !TestNotNull(TEXT("Doll"), Doll) || !TestNotNull(TEXT("SightFreeze"), Freeze)
		|| !TestNotNull(TEXT("Torch"), Torch))
	{
		return false;
	}
	TestTrue(TEXT("Lit-and-watched is the default rule"), Freeze->bFreezeOnlyWhenLitAndWatched);
	Light->RefreshTorches();

	// 1. Watched in torch light: frozen.
	Tick(Freeze);
	TestTrue(TEXT("Watched and torch-lit: frozen"), Freeze->IsFrozen());
	TestTrue(TEXT("Reported lit"), Freeze->IsIlluminated());

	// 2. Watched in the dark: it keeps coming.
	Torch->SetVisibility(false);
	Tick(Freeze);
	TestFalse(TEXT("Watched in the dark: not frozen"), Freeze->IsFrozen());
	TestFalse(TEXT("Not lit"), Freeze->IsIlluminated());
	TestTrue(TEXT("It may move"), UMonsterSightFreezeComponent::CanActorMove(Doll));

	// 3. Lit but behind the viewer: not watched, so it moves.
	Torch->SetVisibility(true);
	Doll->SetActorLocation(FVector(-500.0f, 0.0f, 0.0f));
	Torch->SetWorldLocation(FVector(-500.0f, 0.0f, 300.0f));
	Tick(Freeze);
	TestFalse(TEXT("Lit but unwatched: not frozen"), Freeze->IsFrozen());

	// 4. Back in view and lit: frozen again; torch light switched off for the Doll: moves.
	Doll->SetActorLocation(FVector(500.0f, 0.0f, 0.0f));
	Torch->SetWorldLocation(FVector(500.0f, 0.0f, 300.0f));
	Tick(Freeze);
	TestTrue(TEXT("Watched and lit again: frozen"), Freeze->IsFrozen());
	Freeze->bTorchLightCounts = false;
	Tick(Freeze);
	TestFalse(TEXT("bTorchLightCounts false: torch light does not freeze"), Freeze->IsFrozen());
	Freeze->bTorchLightCounts = true;

	// 5. The previous rule (flag off): watched within 8 m freezes even in the dark.
	Torch->SetVisibility(false);
	Freeze->bFreezeOnlyWhenLitAndWatched = false;
	Tick(Freeze);
	TestTrue(TEXT("Old rule: watched in the dark at 5 m freezes"), Freeze->IsFrozen());
	return true;
}

// -----------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLostWithinTorchLightBoneDogHearingTest, "LostWithin.AI.TorchLight.BoneDogPinpoint",
	LOSTWITHIN_TORCH_TEST_FLAGS)

bool FLostWithinTorchLightBoneDogHearingTest::RunTest(const FString& Parameters)
{
	using namespace TorchLightTestHelpers;
	FScopedTorchTestWorld Env;
	ULostWithinLightSubsystem* Light = ULostWithinLightSubsystem::Get(Env.World);
	if (!TestNotNull(TEXT("World"), Env.World) || !TestNotNull(TEXT("Light subsystem"), Light))
	{
		return false;
	}
	UMonsterHearingComponent* Hearing = nullptr;
	ACharacter* Dog = SpawnMonster(Env.World, FVector::ZeroVector, [&Hearing](ACharacter* Monster)
	{
		Hearing = NewObject<UMonsterHearingComponent>(Monster, TEXT("Hearing"));
		Hearing->RegisterComponent();
	});
	if (!TestNotNull(TEXT("Dog"), Dog) || !TestNotNull(TEXT("Hearing"), Hearing))
	{
		return false;
	}
	TestEqual(TEXT("Default: exact source in light"), Hearing->PinpointErrorInLight, 0.0f);
	TestEqual(TEXT("Default: exact source in the dark"), Hearing->PinpointErrorInDark, 0.0f);

	FLostWithinNoiseEvent Noise;
	Noise.Location = FVector(600.0f, 0.0f, 0.0f);
	Noise.Radius = 2000.0f;
	Noise.Kind = ELostWithinNoiseKind::Footstep;

	// Zero error (every other monster): the exact source.
	if (TestTrue(TEXT("Footstep heard"), Hearing->HearNoise(Noise)))
	{
		TestTrue(TEXT("Zero error investigates the exact source"), Hearing->GetInvestigationLocation().Equals(Noise.Location, 0.5f));
	}

	// BoneDog values; check many draws in the dark, then in torch light.
	Hearing->PinpointErrorInLight = 200.0f;
	Hearing->PinpointErrorInDark = 500.0f;
	auto CheckDraws = [&](const TCHAR* Label, float Error, bool bExpectLit)
	{
		float MinMiss = TNumericLimits<float>::Max();
		float MaxMiss = 0.0f;
		for (int32 Draw = 0; Draw < 40; ++Draw)
		{
			if (!Hearing->HearNoise(Noise))
			{
				AddError(FString::Printf(TEXT("%s: footstep %d not heard"), Label, Draw));
				return;
			}
			const float Miss = FVector::Dist2D(Hearing->GetInvestigationLocation(), Noise.Location);
			MinMiss = FMath::Min(MinMiss, Miss);
			MaxMiss = FMath::Max(MaxMiss, Miss);
		}
		TestEqual(FString::Printf(TEXT("%s: error used"), Label), Hearing->GetLastPinpointError(), Error);
		TestEqual(FString::Printf(TEXT("%s: lit state"), Label), Hearing->WasLitAtLastNoise(), bExpectLit);
		TestTrue(FString::Printf(TEXT("%s: never on the source (min miss %.0f >= %.0f)"), Label, MinMiss, 0.4f * Error - 1.0f),
			MinMiss >= 0.4f * Error - 1.0f);
		TestTrue(FString::Printf(TEXT("%s: within the error (max miss %.0f <= %.0f)"), Label, MaxMiss, Error + 1.0f),
			MaxMiss <= Error + 1.0f);
	};
	CheckDraws(TEXT("Dark"), 500.0f, false);

	SpawnTorch(Env.World, FVector(0.0f, 0.0f, 250.0f));
	Light->RefreshTorches();
	CheckDraws(TEXT("Torch-lit"), 200.0f, true);
	Hearing->StopInvestigating();
	return true;
}

#undef LOSTWITHIN_TORCH_TEST_FLAGS

#endif // WITH_DEV_AUTOMATION_TESTS
