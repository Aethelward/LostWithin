// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "AI/MonsterAIComponent.h"
#include "AI/MonsterHearingComponent.h"
#include "AI/MonsterLightAttractionComponent.h"
#include "AI/MonsterLightAverseComponent.h"
#include "AI/MonsterScentComponent.h"
#include "AI/MonsterSenseProfile.h"
#include "AI/MonsterSightFreezeComponent.h"
#include "AI/MonsterVibrationComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "Gameplay/LostWithinLightSubsystem.h"
#include "Gameplay/LostWithinNoiseSubsystem.h"
#include "Gameplay/LostWithinScentSubsystem.h"
#include "LostWithinCharacter.h"
#include "Misc/AutomationTest.h"

#define LOSTWITHIN_SENSES_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

namespace MonsterSensesTestHelpers
{
	struct FScopedSensesTestWorld
	{
		UWorld* World = nullptr;

		FScopedSensesTestWorld()
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

		~FScopedSensesTestWorld()
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

	/** A monster stand-in: a plain character with MonsterAI (cat reactions) plus the components AddComponents adds. */
	ACharacter* SpawnSenseMonster(UWorld* World, const FVector& Location, TFunctionRef<void(ACharacter*)> AddComponents)
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
		// No floor here: start grounded like a maze NPC.
		Monster->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		return Monster;
	}

	/** A wall-torch stand-in named like the maze torches. */
	UPointLightComponent* SpawnSenseTorch(UWorld* World, const FVector& Location)
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

	/** The real player Blueprint, possessed by the world's first player controller. */
	ALostWithinCharacter* SpawnSensePlayer(UWorld* World, const FVector& Location, APlayerController*& OutController)
	{
		UClass* PlayerClass = LoadClass<ALostWithinCharacter>(
			nullptr, TEXT("/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter.BP_FirstPersonCharacter_C"));
		OutController = World->SpawnActor<APlayerController>();
		ALostWithinCharacter* Player = PlayerClass
			? World->SpawnActor<ALostWithinCharacter>(PlayerClass, Location, FRotator::ZeroRotator) : nullptr;
		if (OutController && Player)
		{
			OutController->Possess(Player);
		}
		return Player;
	}

	void TickSenseFor(UActorComponent* Component, float Seconds, float Step = 0.05f)
	{
		for (float Elapsed = 0.0f; Elapsed < Seconds - KINDA_SMALL_NUMBER; Elapsed += Step)
		{
			Component->TickComponent(Step, LEVELTICK_All, nullptr);
		}
	}

	FLostWithinNoiseEvent SenseFootstep(const FVector& Location, float Radius, AActor* Maker)
	{
		FLostWithinNoiseEvent Noise;
		Noise.Location = Location;
		Noise.Radius = Radius;
		Noise.Kind = ELostWithinNoiseKind::Footstep;
		Noise.NoiseMaker = Maker;
		return Noise;
	}
}

// -----------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLostWithinSenseProfileTest, "LostWithin.AI.Senses.Profile", LOSTWITHIN_SENSES_TEST_FLAGS)

bool FLostWithinSenseProfileTest::RunTest(const FString& Parameters)
{
	using namespace MonsterSensesTestHelpers;

	// 1. The placed creatures map to one sense each, by class.
	struct FExpected { const TCHAR* Path; EMonsterSense Sense; };
	const FExpected Expected[] = {
		{ TEXT("/Game/NPC/Doll/BP_Doll.BP_Doll_C"), EMonsterSense::Sight },
		{ TEXT("/Game/NPC/Ghoul/BP_Ghoul.BP_Ghoul_C"), EMonsterSense::Hearing },
		{ TEXT("/Game/NPC/BoneDog/BP_BoneDog.BP_BoneDog_C"), EMonsterSense::Smell },
		{ TEXT("/Game/NPC/Lunger/Lunger.Lunger_C"), EMonsterSense::Touch },
	};
	for (const FExpected& Entry : Expected)
	{
		UClass* Class = LoadClass<ACharacter>(nullptr, Entry.Path);
		if (TestNotNull(FString::Printf(TEXT("Class %s"), Entry.Path), Class))
		{
			TestEqual(FString::Printf(TEXT("Sense of %s"), Entry.Path),
				UMonsterSenseProfile::GetSenseForActor(Class->GetDefaultObject<AActor>()), Entry.Sense);
		}
	}
	TestEqual(TEXT("A plain character is no monster"),
		UMonsterSenseProfile::GetSenseForActor(GetDefault<ACharacter>()), EMonsterSense::None);

	FScopedSensesTestWorld Env;
	if (!TestNotNull(TEXT("World"), Env.World))
	{
		return false;
	}
	auto WithHearing = [](ACharacter* Monster)
	{
		NewObject<UMonsterHearingComponent>(Monster, TEXT("Hearing"))->RegisterComponent();
	};
	const FLostWithinNoiseEvent Step = SenseFootstep(FVector(300.0f, 0.0f, 0.0f), 900.0f, nullptr);

	// 2. BoneDog: deaf, gains near-sight and a nose; applying twice adds nothing.
	ACharacter* Dog = SpawnSenseMonster(Env.World, FVector::ZeroVector, WithHearing);
	UMonsterSenseProfile::ApplySenseToActor(Dog, EMonsterSense::Smell);
	UMonsterSenseProfile::ApplySenseToActor(Dog, EMonsterSense::Smell);
	UMonsterHearingComponent* DogEars = Dog->FindComponentByClass<UMonsterHearingComponent>();
	TestFalse(TEXT("BoneDog hearing deactivated"), DogEars->IsActive());
	TestFalse(TEXT("BoneDog hears no footstep"), DogEars->HearNoise(Step));
	TArray<UMonsterScentComponent*> Noses;
	Dog->GetComponents(Noses);
	TArray<UMonsterLightAttractionComponent*> Eyes;
	Dog->GetComponents(Eyes);
	TestEqual(TEXT("One nose"), Noses.Num(), 1);
	TestEqual(TEXT("One near-sight"), Eyes.Num(), 1);
	if (Eyes.Num() == 1)
	{
		TestEqual(TEXT("Near-sight reaches 400 cm"), Eyes[0]->AttractionRadius, 400.0f);
		TestFalse(TEXT("Near-sight needs no flashlight"), Eyes[0]->bRequireFlashlight);
	}

	// 3. Doll: deaf.
	ACharacter* Doll = SpawnSenseMonster(Env.World, FVector(0.0f, 2000.0f, 0.0f), WithHearing);
	UMonsterSenseProfile::ApplySenseToActor(Doll, EMonsterSense::Sight);
	TestFalse(TEXT("Doll hears no footstep"), Doll->FindComponentByClass<UMonsterHearingComponent>()->HearNoise(
		SenseFootstep(FVector(300.0f, 2000.0f, 0.0f), 900.0f, nullptr)));

	// 4. Ghoul: sharper, exact ears.
	ACharacter* Ghoul = SpawnSenseMonster(Env.World, FVector(0.0f, 4000.0f, 0.0f), WithHearing);
	UMonsterHearingComponent* GhoulEars = Ghoul->FindComponentByClass<UMonsterHearingComponent>();
	GhoulEars->PinpointErrorInDark = 500.0f;
	UMonsterSenseProfile::ApplySenseToActor(Ghoul, EMonsterSense::Hearing);
	TestEqual(TEXT("Ghoul range x1.5"), GhoulEars->HearingRangeMultiplier, 1.5f);
	TestEqual(TEXT("Ghoul exact in light"), GhoulEars->PinpointErrorInLight, 0.0f);
	TestEqual(TEXT("Ghoul exact in the dark"), GhoulEars->PinpointErrorInDark, 0.0f);
	TestTrue(TEXT("Ghoul hears a walk 5.5 m away (4 m x 1.5)"), GhoulEars->HearNoise(
		SenseFootstep(FVector(550.0f, 4000.0f, 0.0f), 400.0f, nullptr)));

	// 5. ApplyForWorld skips plain characters (no creature class name).
	TestEqual(TEXT("Plain test characters are not profiled"), UMonsterSenseProfile::ApplyForWorld(Env.World), 0);
	return true;
}

// -----------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLostWithinScentTrailTest, "LostWithin.AI.Senses.ScentTrail", LOSTWITHIN_SENSES_TEST_FLAGS)

bool FLostWithinScentTrailTest::RunTest(const FString& Parameters)
{
	using namespace MonsterSensesTestHelpers;
	FScopedSensesTestWorld Env;
	ULostWithinScentSubsystem* Scent = ULostWithinScentSubsystem::Get(Env.World);
	ULostWithinLightSubsystem* Light = ULostWithinLightSubsystem::Get(Env.World);
	if (!TestNotNull(TEXT("Scent subsystem"), Scent) || !TestNotNull(TEXT("Light subsystem"), Light))
	{
		return false;
	}
	Scent->bSamplePlayer = false;

	// 1. Order: ids grow along the trail; the next node follows the current one.
	const int32 A = Scent->AddNode(FVector(0.0f, 0.0f, 0.0f), 1.0f, 45.0f);
	const int32 B = Scent->AddNode(FVector(100.0f, 0.0f, 0.0f), 1.5f, 45.0f);
	const int32 C = Scent->AddNode(FVector(200.0f, 0.0f, 0.0f), 2.0f, 60.0f);
	TestTrue(TEXT("Ids grow"), A > 0 && B > A && C > B);
	const FLostWithinScentNode* Next = Scent->FindNextNode(A);
	TestTrue(TEXT("Next after A is B"), Next && Next->Id == B);
	const FLostWithinScentNode* Freshest = Scent->FindFreshestNodeWithin(FVector::ZeroVector, 1200.0f);
	TestTrue(TEXT("Freshest within the nose is C"), Freshest && Freshest->Id == C);
	Freshest = Scent->FindFreshestNodeWithin(FVector::ZeroVector, 1200.0f, C);
	TestNull(TEXT("Nothing fresher than C"), Freshest);

	// 2. Fade: linear over the lifetime; walk nodes expire at 45 s, sprint nodes last to 60 s.
	Scent->Tick(22.5f);
	if (const FLostWithinScentNode* Node = Scent->FindNode(A))
	{
		TestEqual(TEXT("Half faded at half its lifetime"), Scent->GetEffectiveStrength(*Node), 0.5f, 0.01f);
	}
	Scent->Tick(23.0f);
	TestNull(TEXT("Walk node gone after 45 s"), Scent->FindNode(A));
	TestNotNull(TEXT("Sprint node still there at 45.5 s"), Scent->FindNode(C));
	Scent->Tick(15.0f);
	TestNull(TEXT("Sprint node gone after 60 s"), Scent->FindNode(C));

	// 3. Torch smoke: no node near a lit torch; an existing node there is removed once the torch is lit.
	UPointLightComponent* Torch = SpawnSenseTorch(Env.World, FVector(1000.0f, 0.0f, 100.0f));
	if (TestNotNull(TEXT("Torch"), Torch))
	{
		Light->RefreshTorches();
		TestEqual(TEXT("No node within 400 cm of a lit torch"), Scent->AddNode(FVector(1100.0f, 0.0f, 0.0f), 1.0f, 45.0f), 0);
		TestTrue(TEXT("Masked point"), Scent->IsScentMasked(FVector(1100.0f, 0.0f, 0.0f)));
		Torch->SetVisibility(false);
		const int32 Dark = Scent->AddNode(FVector(1100.0f, 0.0f, 0.0f), 1.0f, 45.0f);
		TestTrue(TEXT("A snuffed torch masks nothing"), Dark > 0);
		Torch->SetVisibility(true);
		Scent->Tick(0.6f);
		TestNull(TEXT("Relit torch removes the node"), Scent->FindNode(Dark));
	}

	// 4. Scented item: strength 3, fading over 20 s; an ignored item is skipped.
	AStaticMeshActor* Item = Env.World->SpawnActor<AStaticMeshActor>(FVector(-500.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	if (TestNotNull(TEXT("Item"), Item))
	{
		Scent->AddItemScent(Item);
		TestEqual(TEXT("Item scent starts at 3"), Scent->GetItemScentStrength(Item), 3.0f, 0.01f);
		float Strength = 0.0f;
		TSet<TWeakObjectPtr<AActor>> Ignored;
		TestTrue(TEXT("Strongest item found"), Scent->FindStrongestItemWithin(FVector::ZeroVector, 1200.0f, Ignored, Strength) == Item);
		Ignored.Add(Item);
		TestNull(TEXT("Ignored item skipped"), Scent->FindStrongestItemWithin(FVector::ZeroVector, 1200.0f, Ignored, Strength));
		Scent->Tick(20.5f);
		TestFalse(TEXT("Item scent gone after 20 s"), Scent->HasItemScent(Item));
	}

	// 5. Standing still pools a strength-3 node every 5 s.
	Scent->ClearScent();
	APlayerController* Controller = nullptr;
	ALostWithinCharacter* Player = SpawnSensePlayer(Env.World, FVector(-3000.0f, 0.0f, 0.0f), Controller);
	if (TestNotNull(TEXT("Player"), Player))
	{
		Scent->bSamplePlayer = true;
		for (int32 Step = 0; Step < 53; ++Step)
		{
			Scent->Tick(0.1f);
		}
		bool bPool = false;
		for (const FLostWithinScentNode& Node : Scent->GetNodes())
		{
			bPool |= FMath::IsNearlyEqual(Node.Strength, 3.0f);
		}
		TestTrue(TEXT("A strength-3 pool after 5 s standing still"), bPool);
		TestTrue(TEXT("Standing still lays no extra step nodes"), Scent->GetNodes().Num() <= 2);
	}
	return true;
}

// -----------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLostWithinBoneDogNoseTest, "LostWithin.AI.Senses.BoneDogNose", LOSTWITHIN_SENSES_TEST_FLAGS)

bool FLostWithinBoneDogNoseTest::RunTest(const FString& Parameters)
{
	using namespace MonsterSensesTestHelpers;
	FScopedSensesTestWorld Env;
	ULostWithinScentSubsystem* Scent = ULostWithinScentSubsystem::Get(Env.World);
	if (!TestNotNull(TEXT("Scent subsystem"), Scent))
	{
		return false;
	}
	Scent->bSamplePlayer = false;
	UMonsterScentComponent* Nose = nullptr;
	ACharacter* Dog = SpawnSenseMonster(Env.World, FVector::ZeroVector, [&Nose](ACharacter* Monster)
	{
		Nose = NewObject<UMonsterScentComponent>(Monster, TEXT("ScentSense"));
		Nose->RegisterComponent();
	});
	if (!TestNotNull(TEXT("Dog"), Dog) || !TestNotNull(TEXT("Nose"), Nose))
	{
		return false;
	}
	UMonsterAIComponent* AI = Dog->FindComponentByClass<UMonsterAIComponent>();

	// 1. Nothing to smell: idle, the roam loop runs.
	Nose->Think();
	TestEqual(TEXT("Idle without scent"), Nose->GetScentState(), EMonsterScentState::Idle);
	TestTrue(TEXT("Roam loop runs"), UMonsterAIComponent::CanRunNormalAI(Dog));

	// 2. Trail along +X: with a short nose it picks up the nearest (first) node and follows the later ones in order.
	int32 Ids[5];
	for (int32 Index = 0; Index < 5; ++Index)
	{
		Ids[Index] = Scent->AddNode(FVector(100.0f * (Index + 1), 0.0f, 0.0f), 1.0f, 45.0f);
	}
	Nose->NoseRange = 150.0f;
	Nose->Think();
	TestEqual(TEXT("Trailing"), Nose->GetScentState(), EMonsterScentState::Trailing);
	TestEqual(TEXT("Starts at the node in range"), Nose->GetCurrentNodeId(), Ids[0]);
	TestFalse(TEXT("Roam loop waits while trailing"), UMonsterAIComponent::CanRunNormalAI(Dog));
	for (int32 Index = 0; Index < 4; ++Index)
	{
		Dog->SetActorLocation(FVector(100.0f * (Index + 1), 0.0f, Dog->GetActorLocation().Z));
		Nose->Think();
		TestEqual(FString::Printf(TEXT("Reached node %d: heads for the next"), Index + 1), Nose->GetCurrentNodeId(), Ids[Index + 1]);
	}

	// 3. A gap wider than 400 cm breaks the trail: it casts, then gives up after 6 s.
	const int32 Far = Scent->AddNode(FVector(1500.0f, 0.0f, 0.0f), 1.0f, 45.0f);
	Dog->SetActorLocation(FVector(500.0f, 0.0f, Dog->GetActorLocation().Z));
	Nose->Think();
	TestEqual(TEXT("Gap: casting"), Nose->GetScentState(), EMonsterScentState::Casting);
	TickSenseFor(Nose, 6.3f);
	TestEqual(TEXT("Cast ends: idle again"), Nose->GetScentState(), EMonsterScentState::Idle);
	Nose->NoseRange = 1200.0f;
	Nose->Think();
	TestEqual(TEXT("The lost trail stays lost"), Nose->GetScentState(), EMonsterScentState::Idle);
	TestTrue(TEXT("Far node still exists"), Scent->FindNode(Far) != nullptr);

	// 4. Decoys: an unscented item does nothing; a scented one wins over the trail, once.
	AStaticMeshActor* Plain = Env.World->SpawnActor<AStaticMeshActor>(FVector(700.0f, 300.0f, 0.0f), FRotator::ZeroRotator);
	Nose->Think();
	TestEqual(TEXT("An unscented item never draws it"), Nose->GetScentState(), EMonsterScentState::Idle);
	AStaticMeshActor* Carried = Env.World->SpawnActor<AStaticMeshActor>(FVector(800.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	if (TestNotNull(TEXT("Plain item"), Plain) && TestNotNull(TEXT("Carried item"), Carried))
	{
		Scent->AddNode(FVector(600.0f, 0.0f, 0.0f), 1.0f, 45.0f);
		Scent->AddItemScent(Carried);
		Nose->Think();
		TestEqual(TEXT("Scented item draws it"), Nose->GetScentState(), EMonsterScentState::Decoy);
		TestTrue(TEXT("Heads for the item"), Nose->GetDecoyTarget() == Carried);
		Dog->SetActorLocation(FVector(800.0f, 0.0f, Dog->GetActorLocation().Z));
		Nose->Think();
		TestTrue(TEXT("Reached: ignores that item now"), Nose->IsIgnoringItem(Carried));
		TestNotEqual(TEXT("No longer heading for it"), Nose->GetScentState(), EMonsterScentState::Decoy);
		Nose->Think();
		TestNotEqual(TEXT("A spent decoy does not draw it again"), Nose->GetScentState(), EMonsterScentState::Decoy);
	}

	// 5. Air scent: a player within 600 cm is smelled directly; near-sight sees them only within 400 cm.
	Nose->StopTracking();
	UMonsterLightAttractionComponent* Eyes = NewObject<UMonsterLightAttractionComponent>(Dog, TEXT("LightAttraction"));
	Eyes->RegisterComponent();
	APlayerController* Controller = nullptr;
	ALostWithinCharacter* Player = SpawnSensePlayer(Env.World, Dog->GetActorLocation() + FVector(500.0f, 0.0f, 0.0f), Controller);
	if (TestNotNull(TEXT("Player"), Player))
	{
		TestFalse(TEXT("Flashlight off"), Player->IsFlashlightOn());
		TestFalse(TEXT("Not seen at 5 m"), Eyes->IsDrawnBy(Player));
		Nose->Think();
		TestEqual(TEXT("Smelled directly within 6 m"), Nose->GetScentState(), EMonsterScentState::AirLock);
		Player->SetActorLocation(Dog->GetActorLocation() + FVector(250.0f, 0.0f, 0.0f));
		TestTrue(TEXT("Seen at 2.5 m without any light"), Eyes->IsDrawnBy(Player));
		Player->SetActorLocation(Dog->GetActorLocation() + FVector(2000.0f, 0.0f, 0.0f));
		Nose->Think();
		TestNotEqual(TEXT("Out of direct smell: lets go of the air lock"), Nose->GetScentState(), EMonsterScentState::AirLock);
	}

	// 6. The cat wins over the nose.
	Scent->AddNode(Dog->GetActorLocation() + FVector(50.0f, 0.0f, 0.0f), 1.0f, 45.0f);
	AI->bCatIsNear = true;
	Nose->Think();
	TestEqual(TEXT("Cat near: the nose lets go"), Nose->GetScentState(), EMonsterScentState::Idle);
	AI->bCatIsNear = false;
	return true;
}

// -----------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLostWithinGhoulEarsTest, "LostWithin.AI.Senses.GhoulHearingChase", LOSTWITHIN_SENSES_TEST_FLAGS)

bool FLostWithinGhoulEarsTest::RunTest(const FString& Parameters)
{
	using namespace MonsterSensesTestHelpers;
	FScopedSensesTestWorld Env;
	if (!TestNotNull(TEXT("World"), Env.World))
	{
		return false;
	}
	UMonsterHearingComponent* Ears = nullptr;
	UMonsterLightAverseComponent* Averse = nullptr;
	ACharacter* Ghoul = SpawnSenseMonster(Env.World, FVector::ZeroVector, [&Ears, &Averse](ACharacter* Monster)
	{
		Ears = NewObject<UMonsterHearingComponent>(Monster, TEXT("Hearing"));
		Ears->RegisterComponent();
		Averse = NewObject<UMonsterLightAverseComponent>(Monster, TEXT("LightAverse"));
		Averse->bRequireLineOfSight = false;
		Averse->RegisterComponent();
	});
	APlayerController* Controller = nullptr;
	ALostWithinCharacter* Player = SpawnSensePlayer(Env.World, FVector(600.0f, 0.0f, 0.0f), Controller);
	if (!TestNotNull(TEXT("Ghoul"), Ghoul) || !TestNotNull(TEXT("Player"), Player))
	{
		return false;
	}
	UMonsterSenseProfile::ApplySenseToActor(Ghoul, EMonsterSense::Hearing);
	TestTrue(TEXT("Hunts by ear by default"), UMonsterLightAverseComponent::IsActorHuntingByEar(Ghoul));

	// 1. A silent player in the dark, 6 m away, is never found.
	TickSenseFor(Averse, 1.0f);
	TestFalse(TEXT("Silent player: no chase"), Averse->IsChasingPlayer());

	// 2. A footstep: the chase heads for where it was heard; the hearing component does not walk there itself.
	const FVector Heard = Player->GetActorLocation();
	TestTrue(TEXT("Footstep heard"), Ears->HearNoise(SenseFootstep(Heard, 900.0f, Player)));
	TestFalse(TEXT("The ear chase takes the footstep, not an investigation"), Ears->IsInvestigating());
	TickSenseFor(Averse, 0.05f);
	TestTrue(TEXT("Chasing the heard point"), Averse->IsChasingPlayer());
	TestTrue(TEXT("Target is the exact footstep"), Averse->GetHeardChaseTarget().Equals(Heard, 0.5f));
	TestFalse(TEXT("Roam loop waits"), UMonsterAIComponent::CanRunNormalAI(Ghoul));
	Player->SetActorLocation(FVector(600.0f, 800.0f, 0.0f));
	TickSenseFor(Averse, 0.1f);
	TestTrue(TEXT("Never follows the silent player's live position"), Averse->GetHeardChaseTarget().Equals(Heard, 0.5f));

	// 3. Speed steps every 3 s of the chase: 360, then 420.
	TestEqual(TEXT("First speed step"), Averse->GetCurrentDarkStalkSpeed(), 360.0f);
	TickSenseFor(Averse, 3.0f);
	TestEqual(TEXT("Second speed step after 3 s"), Averse->GetCurrentDarkStalkSpeed(), 420.0f);

	// 4. Six seconds without a sound: it gives up.
	TickSenseFor(Averse, 3.1f);
	TestFalse(TEXT("Gave up after 6 s of silence"), Averse->IsChasingPlayer());
	TestTrue(TEXT("Roam loop runs again"), UMonsterAIComponent::CanRunNormalAI(Ghoul));

	// 5. An item impact is still a decoy it walks to.
	FLostWithinNoiseEvent Impact = SenseFootstep(FVector(-500.0f, 0.0f, 0.0f), 3000.0f, nullptr);
	Impact.Kind = ELostWithinNoiseKind::ItemImpact;
	TestTrue(TEXT("Item impact heard"), Ears->HearNoise(Impact));
	TestTrue(TEXT("Investigates the impact"), Ears->IsInvestigating());
	Ears->StopInvestigating();
	return true;
}

// -----------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLostWithinGhoulTorchSnuffTest, "LostWithin.AI.Senses.GhoulTorchSnuff", LOSTWITHIN_SENSES_TEST_FLAGS)

bool FLostWithinGhoulTorchSnuffTest::RunTest(const FString& Parameters)
{
	using namespace MonsterSensesTestHelpers;
	FScopedSensesTestWorld Env;
	ULostWithinLightSubsystem* Light = ULostWithinLightSubsystem::Get(Env.World);
	if (!TestNotNull(TEXT("Light subsystem"), Light))
	{
		return false;
	}
	UMonsterHearingComponent* Ears = nullptr;
	UMonsterLightAverseComponent* Averse = nullptr;
	ACharacter* Ghoul = SpawnSenseMonster(Env.World, FVector(60.0f, 0.0f, 0.0f), [&Ears, &Averse](ACharacter* Monster)
	{
		Ears = NewObject<UMonsterHearingComponent>(Monster, TEXT("Hearing"));
		Ears->RegisterComponent();
		Averse = NewObject<UMonsterLightAverseComponent>(Monster, TEXT("LightAverse"));
		Averse->bRequireLineOfSight = false;
		Averse->bOrbitTorchEdge = false; // keep it at the same rim point for the hold timer
		Averse->RegisterComponent();
	});
	UPointLightComponent* Torch = SpawnSenseTorch(Env.World, FVector(500.0f, 0.0f, 100.0f));
	APlayerController* Controller = nullptr;
	ALostWithinCharacter* Player = SpawnSensePlayer(Env.World, FVector(700.0f, 0.0f, 0.0f), Controller);
	if (!TestNotNull(TEXT("Ghoul"), Ghoul) || !TestNotNull(TEXT("Torch"), Torch) || !TestNotNull(TEXT("Player"), Player))
	{
		return false;
	}
	Light->RefreshTorches();
	UMonsterSenseProfile::ApplySenseToActor(Ghoul, EMonsterSense::Hearing);

	// The player walks inside the torch's light; the Ghoul hears every step and holds at the dark edge.
	for (int32 Second = 0; Second < 3; ++Second)
	{
		Ears->HearNoise(SenseFootstep(Player->GetActorLocation(), 900.0f, Player));
		TickSenseFor(Averse, 1.0f);
		TestTrue(FString::Printf(TEXT("Holding at the torch edge (%d s)"), Second + 1), Averse->IsHoldingAtTorchEdge());
		TestTrue(TEXT("Torch still lit before 4 s"), Torch->IsVisible());
	}
	Ears->HearNoise(SenseFootstep(Player->GetActorLocation(), 900.0f, Player));
	TickSenseFor(Averse, 1.1f);
	TestFalse(TEXT("Snuffed after 4 s at the edge"), Torch->IsVisible());
	TestTrue(TEXT("Marked snuffed"), Light->IsTorchSnuffed(Torch));
	TestFalse(TEXT("Dark now: no longer holding"), Averse->IsHoldingAtTorchEdge());
	TestFalse(TEXT("A snuffed torch cannot be snuffed again"), Light->SnuffTorch(Torch, 20.0f));

	Light->RelightAllSnuffedTorches();
	TestTrue(TEXT("Relit"), Torch->IsVisible());
	TestFalse(TEXT("No longer snuffed"), Light->IsTorchSnuffed(Torch));
	return true;
}

// -----------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLostWithinDollSightTest, "LostWithin.AI.Senses.DollSight", LOSTWITHIN_SENSES_TEST_FLAGS)

bool FLostWithinDollSightTest::RunTest(const FString& Parameters)
{
	using namespace MonsterSensesTestHelpers;
	FScopedSensesTestWorld Env;
	ULostWithinLightSubsystem* Light = ULostWithinLightSubsystem::Get(Env.World);
	if (!TestNotNull(TEXT("Light subsystem"), Light))
	{
		return false;
	}
	// The viewer's camera has not moved: it sits at the origin looking down +X. The Doll stands behind it.
	APlayerController* Viewer = nullptr;
	ALostWithinCharacter* Player = SpawnSensePlayer(Env.World, FVector(-600.0f, 100.0f, 0.0f), Viewer);
	UMonsterSightFreezeComponent* Sight = nullptr;
	ACharacter* Doll = SpawnSenseMonster(Env.World, FVector(-900.0f, 0.0f, 0.0f), [&Sight](ACharacter* Monster)
	{
		Sight = NewObject<UMonsterSightFreezeComponent>(Monster, TEXT("SightFreezeAI"));
		Sight->bRequireLineOfSight = false;
		Sight->MimicPlayerRange = 0.0f;
		Sight->RegisterComponent();
	});
	UPointLightComponent* Torch = SpawnSenseTorch(Env.World, FVector(-750.0f, 0.0f, 150.0f));
	if (!TestNotNull(TEXT("Player"), Player) || !TestNotNull(TEXT("Doll"), Doll) || !TestNotNull(TEXT("Torch"), Torch))
	{
		return false;
	}
	Torch->SetVisibility(false);
	Light->RefreshTorches();

	// 1. No light on it: dormant, held in place, the roam loop waits too.
	TickSenseFor(Sight, 0.05f);
	TestTrue(TEXT("Dormant in the dark"), Sight->IsDormant());
	TestTrue(TEXT("Held while dormant"), Sight->IsFrozen());
	TestFalse(TEXT("Not rushing"), Sight->IsRushing());
	TestFalse(TEXT("Roam loop waits while dormant"), UMonsterAIComponent::CanRunNormalAI(Doll));

	// 2. Torch light on it and on the player, unwatched: wakes in the same tick, sees the player, rushes.
	Torch->SetVisibility(true);
	TickSenseFor(Sight, 0.05f);
	TestFalse(TEXT("Awake in light"), Sight->IsDormant());
	TestTrue(TEXT("Sees the lit player"), Sight->SeesPlayer());
	TestTrue(TEXT("Rushes the unwatched player"), Sight->IsRushing());
	TestFalse(TEXT("Roam loop waits while rushing"), UMonsterAIComponent::CanRunNormalAI(Doll));
	TestEqual(TEXT("Rush speed"), Doll->GetCharacterMovement()->MaxWalkSpeed, Sight->RushSpeed);

	// 3. Lit players only: the same player 16 m away in the dark (within its 20 m sight) is invisible to it.
	TestTrue(TEXT("Sees the player standing in the torch light"), Sight->CanSeePlayer(Player, /*bBeamOnDoll=*/false));
	Player->SetActorLocation(FVector(-2500.0f, 0.0f, 0.0f));
	TestFalse(TEXT("An unlit player is invisible to it"), Sight->CanSeePlayer(Player, /*bBeamOnDoll=*/false));
	Player->SetActorLocation(FVector(-600.0f, 100.0f, 0.0f));

	// 4. Darkness again: dormant, forgets, stops.
	Torch->SetVisibility(false);
	TickSenseFor(Sight, 0.05f);
	TestTrue(TEXT("Dormant again"), Sight->IsDormant());
	TestFalse(TEXT("Forgot the player"), Sight->HasSeenPlayer());
	TestFalse(TEXT("Rush ended"), Sight->IsRushing());

	// 5. Watched in light: no longer frozen (2026-11-20); it keeps rushing a player it sees. The old rule still works.
	Doll->SetActorLocation(FVector(500.0f, 0.0f, 0.0f));
	Torch->SetWorldLocation(FVector(500.0f, 0.0f, 300.0f));
	// A second torch keeps the player lit where they stand, so the Doll (in front of the camera) sees them.
	UPointLightComponent* PlayerTorch = SpawnSenseTorch(Env.World, FVector(-600.0f, 100.0f, 150.0f));
	Light->RefreshTorches();
	Torch->SetVisibility(true);
	TickSenseFor(Sight, 0.05f);
	TestFalse(TEXT("Lit: awake"), Sight->IsDormant());
	TestTrue(TEXT("Watched in light"), Sight->IsObserved());
	TestFalse(TEXT("Watching does not freeze it"), Sight->IsFrozen());
	TestNotNull(TEXT("Player torch"), PlayerTorch);
	TestTrue(TEXT("Watched, it still rushes the lit player"), Sight->IsRushing());
	Sight->bFreezeWhenWatched = true;
	TickSenseFor(Sight, 0.05f);
	TestTrue(TEXT("bFreezeWhenWatched: watched and lit freezes"), Sight->IsFrozen());
	Sight->bFreezeWhenWatched = false;

	// 6. The cat wins: no hold while it reacts.
	UMonsterAIComponent* AI = Doll->FindComponentByClass<UMonsterAIComponent>();
	AI->bCatIsNear = true;
	TickSenseFor(Sight, 0.05f);
	TestFalse(TEXT("Cat near: not held"), Sight->IsFrozen());
	AI->bCatIsNear = false;
	return true;
}

// -----------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLostWithinLungerLungeTest, "LostWithin.AI.Senses.LungerLunge", LOSTWITHIN_SENSES_TEST_FLAGS)

bool FLostWithinLungerLungeTest::RunTest(const FString& Parameters)
{
	using namespace MonsterSensesTestHelpers;
	FScopedSensesTestWorld Env;
	if (!TestNotNull(TEXT("World"), Env.World))
	{
		return false;
	}
	UMonsterVibrationComponent* Touch = nullptr;
	ACharacter* Lunger = SpawnSenseMonster(Env.World, FVector::ZeroVector, [&Touch](ACharacter* Monster)
	{
		Touch = NewObject<UMonsterVibrationComponent>(Monster, TEXT("VibrationSense"));
		Touch->RegisterComponent();
	});
	if (!TestNotNull(TEXT("Lunger"), Lunger) || !TestNotNull(TEXT("Touch"), Touch))
	{
		return false;
	}
	const FVector Near(500.0f, 0.0f, 0.0f);

	// 1. Far tremor (beyond 750 cm): it walks there as before.
	TestTrue(TEXT("Far tremor sensed"), Touch->SenseFloorVibration(FVector(1000.0f, 0.0f, 0.0f), Touch->SprintVibrationRadius));
	TestEqual(TEXT("Far: investigates"), Touch->State, EMonsterVibrationState::Investigating);
	Touch->StopInvestigating();

	// 2. Near tremor then stillness: coils and releases without lunging.
	TestTrue(TEXT("Near tremor sensed"), Touch->SenseFloorVibration(Near, Touch->SprintVibrationRadius));
	TestEqual(TEXT("Near: coils"), Touch->State, EMonsterVibrationState::Coiling);
	TestFalse(TEXT("Roam loop waits while coiling"), UMonsterAIComponent::CanRunNormalAI(Lunger));
	TestTrue(TEXT("Coiling counts as hunting"), UMonsterSenseProfile::IsActorHunting(Lunger));
	TickSenseFor(Touch, 0.75f);
	TestEqual(TEXT("Nothing moved: coil released into a search"), Touch->State, EMonsterVibrationState::Searching);
	Touch->StopInvestigating();

	// 3. Near tremor, a fresh one during the coil: a straight lunge toward the last point, then a jam.
	Touch->SenseFloorVibration(Near, Touch->SprintVibrationRadius);
	TickSenseFor(Touch, 0.2f);
	Touch->SenseFloorVibration(Near, Touch->SprintVibrationRadius);
	TickSenseFor(Touch, 0.55f);
	TestEqual(TEXT("Lunging"), Touch->State, EMonsterVibrationState::Lunging);
	TestTrue(TEXT("Locked along +X"), Touch->GetLockedLungeDirection().Equals(FVector(1.0f, 0.0f, 0.0f), 0.01f));
	TestEqual(TEXT("Lunge speed"), Lunger->GetCharacterMovement()->MaxWalkSpeed, Touch->LungeSpeed);
	TestFalse(TEXT("A tremor during the lunge is ignored"), Touch->SenseFloorVibration(FVector(0.0f, 500.0f, 0.0f), Touch->SprintVibrationRadius));
	TickSenseFor(Touch, 0.75f);
	TestEqual(TEXT("Jammed after the lunge"), Touch->State, EMonsterVibrationState::Jammed);
	TestFalse(TEXT("Jammed: senses nothing"), Touch->SenseFloorVibration(Near, Touch->SprintVibrationRadius));
	TickSenseFor(Touch, 3.1f);
	TestEqual(TEXT("Recovered after the jam"), Touch->State, EMonsterVibrationState::Idle);

	// 4. The cat wins over a lunge.
	Touch->SenseFloorVibration(Near, Touch->SprintVibrationRadius);
	TickSenseFor(Touch, 0.2f);
	Touch->SenseFloorVibration(Near, Touch->SprintVibrationRadius);
	TickSenseFor(Touch, 0.55f);
	UMonsterAIComponent* AI = Lunger->FindComponentByClass<UMonsterAIComponent>();
	AI->bCatIsNear = true;
	TickSenseFor(Touch, 0.05f);
	TestEqual(TEXT("Cat near: the lunge stops"), Touch->State, EMonsterVibrationState::Idle);
	AI->bCatIsNear = false;
	return true;
}

#endif
