// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "AI/MonsterAIComponent.h"
#include "AI/MonsterLightAttractionComponent.h"
#include "AI/MonsterLightAverseComponent.h"
#include "AIController.h"
#include "Components/CapsuleComponent.h"
#include "Components/SpotLightComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "Interaction/InteractableInterface.h"
#include "Interaction/PickupItemActor.h"
#include "LostWithinCharacter.h"
#include "Misc/AutomationTest.h"

namespace
{
	struct FScopedGhoulTestWorld
	{
		UWorld* World = nullptr;

		FScopedGhoulTestWorld()
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

		~FScopedGhoulTestWorld()
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

	struct FGhoulSetup
	{
		ACharacter* Actor = nullptr;
		UMonsterAIComponent* AI = nullptr;
		UMonsterLightAverseComponent* LightAverse = nullptr;
	};

	FGhoulSetup SpawnGhoul(UWorld* World, const FVector& Location)
	{
		FGhoulSetup Result;
		const FTransform Transform(Location);
		Result.Actor = World->SpawnActorDeferred<ACharacter>(ACharacter::StaticClass(), Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Result.Actor)
		{
			return Result;
		}
		Result.Actor->AutoPossessAI = EAutoPossessAI::Disabled;

		Result.AI = NewObject<UMonsterAIComponent>(Result.Actor, TEXT("CatReactionAI"));
		Result.AI->RegisterComponent();

		Result.LightAverse = NewObject<UMonsterLightAverseComponent>(Result.Actor, TEXT("LightAverse"));
		Result.LightAverse->bRequireLineOfSight = false; // Disable raycasts in synthetic test environment
		Result.LightAverse->RegisterComponent();

		Result.Actor->FinishSpawning(Transform);
		return Result;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGhoulLightAverseAutomationTest,
	"LostWithin.AI.GhoulLightAversion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FGhoulLightAverseAutomationTest::RunTest(const FString& Parameters)
{
	FScopedGhoulTestWorld Env;
	if (!TestNotNull(TEXT("World created"), Env.World))
	{
		return false;
	}

	FGhoulSetup Ghoul = SpawnGhoul(Env.World, FVector(500.0f, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("Ghoul actor spawned"), Ghoul.Actor)
		|| !TestNotNull(TEXT("LightAverse component exists"), Ghoul.LightAverse)
		|| !TestNotNull(TEXT("MonsterAI component exists"), Ghoul.AI))
	{
		return false;
	}

	// 1. Initial State: Unlit / Darkness
	TestEqual(TEXT("Initial state is DarkStalking"), Ghoul.LightAverse->GetLightState(), EGhoulLightState::DarkStalking);
	TestTrue(TEXT("Ghoul starts stalking in darkness"), Ghoul.LightAverse->IsStalkingInDarkness());
	TestFalse(TEXT("Ghoul does not start lit"), Ghoul.LightAverse->IsLitByFlashlight());
	TestFalse(TEXT("Ghoul does not start shielding face"), Ghoul.LightAverse->IsShieldingFace());
	TestFalse(TEXT("Ghoul does not start repelled"), Ghoul.LightAverse->IsRepelled());
	TestTrue(TEXT("CanRunNormalAI is allowed when unlit"), UMonsterAIComponent::CanRunNormalAI(Ghoul.Actor));
	TestFalse(TEXT("IsActorLightRepelled is false initially"), UMonsterLightAverseComponent::IsActorLightRepelled(Ghoul.Actor));

	// 2. Aim a real held flashlight down the cone toward Ghoul.
	UClass* PlayerClass = LoadClass<ALostWithinCharacter>(
		nullptr, TEXT("/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter.BP_FirstPersonCharacter_C"));
	if (!TestNotNull(TEXT("FirstPerson player Blueprint"), PlayerClass))
	{
		return false;
	}
	APlayerController* Controller = Env.World->SpawnActor<APlayerController>();
	ALostWithinCharacter* Player = Env.World->SpawnActor<ALostWithinCharacter>(
		PlayerClass, FVector::ZeroVector, FRotator::ZeroRotator);
	APickupItemActor* Flashlight = Env.World->SpawnActor<APickupItemActor>(
		APickupItemActor::StaticClass(), FVector(150.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("Player controller"), Controller) || !TestNotNull(TEXT("Player character"), Player)
		|| !TestNotNull(TEXT("Flashlight pickup"), Flashlight))
	{
		return false;
	}
	Controller->Possess(Player);
	Flashlight->ConfigureAsFlashlight();
	IInteractableInterface::Execute_OnInteract(Flashlight, Player);
	UFunction* ToggleFlashlight = Player->FindFunction(TEXT("DoToggleFlashlight"));
	USpotLightComponent* SpotLight = Player->GetFlashlightSpotLight();
	if (!TestNotNull(TEXT("Flashlight toggle"), ToggleFlashlight) || !TestNotNull(TEXT("Player spotlight"), SpotLight))
	{
		return false;
	}
	Player->ProcessEvent(ToggleFlashlight, nullptr);
	SpotLight->SetWorldLocationAndRotation(FVector(0.0f, 0.0f, 50.0f), FRotator::ZeroRotator);
	if (!TestTrue(TEXT("Held flashlight switched on"), Player->IsFlashlightOn()))
	{
		return false;
	}
	Ghoul.LightAverse->TickComponent(0.01f, LEVELTICK_All, nullptr);

	// Instant reaction: the first lit tick already retreats (no flinch pause).
	TestEqual(TEXT("First lit tick retreats at once"), Ghoul.LightAverse->GetLightState(), EGhoulLightState::Retreating);
	TestTrue(TEXT("IsRetreating on the first lit tick"), Ghoul.LightAverse->IsRetreating());
	TestTrue(TEXT("Ghoul is marked repelled"), Ghoul.LightAverse->IsRepelled());
	TestTrue(TEXT("Ghoul shields its face"), Ghoul.LightAverse->IsShieldingFace());
	TestFalse(TEXT("Ghoul suspends dark stalking"), Ghoul.LightAverse->IsStalkingInDarkness());
	TestFalse(TEXT("Lit Ghoul is not chasing"), Ghoul.LightAverse->IsChasingPlayer());
	TestTrue(TEXT("Current light intensity is recorded"), Ghoul.LightAverse->GetCurrentLightIntensity() > 0.0f);

	// 3. Coordination with CanRunNormalAI
	TestFalse(TEXT("CanRunNormalAI is suspended while repelled by light"), UMonsterAIComponent::CanRunNormalAI(Ghoul.Actor));
	TestTrue(TEXT("IsActorLightRepelled returns true while repelled"), UMonsterLightAverseComponent::IsActorLightRepelled(Ghoul.Actor));

	// 4. Verify Retreat Direction
	const FVector RetreatDir = Ghoul.LightAverse->CurrentRetreatDirection;
	TestTrue(TEXT("Retreat direction is normalized"), RetreatDir.IsNormalized());
	TestTrue(TEXT("Retreat direction moves away from light (positive X)"), RetreatDir.X > 0.0f);

	// 5. Beam leaves the Ghoul: in the same tick it stops retreating and chases the nearby player.
	SpotLight->SetWorldRotation(FRotator(0.0f, 180.0f, 0.0f));
	Ghoul.LightAverse->TickComponent(0.01f, LEVELTICK_All, nullptr);
	TestEqual(TEXT("Beam away returns to DarkStalking at once"), Ghoul.LightAverse->GetLightState(), EGhoulLightState::DarkStalking);
	TestFalse(TEXT("Not repelled once the beam leaves"), Ghoul.LightAverse->IsRepelled());
	TestFalse(TEXT("Face shield drops once the beam leaves"), Ghoul.LightAverse->IsShieldingFace());
	TestTrue(TEXT("Chasing the player in the same tick"), Ghoul.LightAverse->IsChasingPlayer());
	TestFalse(TEXT("Roam loop waits while chasing (fastest gait)"), UMonsterAIComponent::CanRunNormalAI(Ghoul.Actor));
	TestTrue(TEXT("IsActorDarkStalkingPlayer while chasing"), UMonsterLightAverseComponent::IsActorDarkStalkingPlayer(Ghoul.Actor));

	// Beam back on: retreats again in the same tick.
	SpotLight->SetWorldRotation(FRotator::ZeroRotator);
	Ghoul.LightAverse->TickComponent(0.01f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("Reappearing light repels at once"), Ghoul.LightAverse->IsRetreating());
	TestFalse(TEXT("Reappearing light ends the chase"), Ghoul.LightAverse->IsChasingPlayer());

	// 6. Flashlight off: chases in the same tick; beyond the detection radius it hands back to the roam loop.
	Player->ProcessEvent(ToggleFlashlight, nullptr);
	Ghoul.LightAverse->TickComponent(0.01f, LEVELTICK_All, nullptr);
	TestEqual(TEXT("Flashlight off returns to DarkStalking"), Ghoul.LightAverse->GetLightState(), EGhoulLightState::DarkStalking);
	TestFalse(TEXT("No longer shielding face"), Ghoul.LightAverse->IsShieldingFace());
	TestFalse(TEXT("No longer repelled"), Ghoul.LightAverse->IsRepelled());
	TestFalse(TEXT("No longer retreating"), Ghoul.LightAverse->IsRetreating());
	TestTrue(TEXT("Resumed aggressive dark stalking"), Ghoul.LightAverse->IsStalkingInDarkness());
	TestTrue(TEXT("Chases at once when the flashlight goes off"), Ghoul.LightAverse->IsChasingPlayer());

	Ghoul.Actor->SetActorLocation(FVector(Ghoul.LightAverse->DarkStalkDetectionRadius + 1000.0f, 0.0f, 0.0f));
	Ghoul.LightAverse->TickComponent(0.01f, LEVELTICK_All, nullptr);
	TestFalse(TEXT("Out of range: no chase"), Ghoul.LightAverse->IsChasingPlayer());
	TestTrue(TEXT("CanRunNormalAI resumes for roaming out of range"), UMonsterAIComponent::CanRunNormalAI(Ghoul.Actor));
	Ghoul.Actor->SetActorLocation(FVector(500.0f, 0.0f, 0.0f));

	// 7. Cat Panic Priority Test: Cat reaction must override light aversion
	Player->ProcessEvent(ToggleFlashlight, nullptr);
	SpotLight->SetWorldLocationAndRotation(FVector(0.0f, 0.0f, 50.0f), FRotator::ZeroRotator);
	Ghoul.LightAverse->TickComponent(0.1f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("Repelled before cat panic"), Ghoul.LightAverse->IsRepelled());

	Ghoul.AI->bIsFleeing = true;
	Ghoul.LightAverse->TickComponent(0.1f, LEVELTICK_All, nullptr);
	TestFalse(TEXT("Cat reaction overrides light repulsion"), Ghoul.LightAverse->IsRepelled());
	TestFalse(TEXT("Normal AI remains suspended due to cat flee"), UMonsterAIComponent::CanRunNormalAI(Ghoul.Actor));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBoneDogLightAttractionAutomationTest,
	"LostWithin.AI.BoneDogLightAttraction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBoneDogLightAttractionAutomationTest::RunTest(const FString& Parameters)
{
	FScopedGhoulTestWorld Env;
	if (!TestNotNull(TEXT("World created"), Env.World))
	{
		return false;
	}

	const FTransform DogTransform(FVector(500.0f, 0.0f, 0.0f));
	ACharacter* Dog = Env.World->SpawnActorDeferred<ACharacter>(ACharacter::StaticClass(), DogTransform, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!TestNotNull(TEXT("Dog spawned"), Dog))
	{
		return false;
	}
	Dog->AutoPossessAI = EAutoPossessAI::Disabled;
	UMonsterAIComponent* AI = NewObject<UMonsterAIComponent>(Dog, TEXT("CatReactionAI"));
	AI->PanicGaitSpeed = 420.0f;
	AI->RegisterComponent();
	UMonsterLightAttractionComponent* Attraction = NewObject<UMonsterLightAttractionComponent>(Dog, TEXT("LightAttraction"));
	Attraction->RegisterComponent();
	Dog->FinishSpawning(DogTransform);
	UCharacterMovementComponent* Move = Dog->GetCharacterMovement();
	Move->MaxWalkSpeed = 90.0f;

	UClass* PlayerClass = LoadClass<ALostWithinCharacter>(
		nullptr, TEXT("/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter.BP_FirstPersonCharacter_C"));
	if (!TestNotNull(TEXT("FirstPerson player Blueprint"), PlayerClass))
	{
		return false;
	}
	APlayerController* Controller = Env.World->SpawnActor<APlayerController>();
	ALostWithinCharacter* Player = Env.World->SpawnActor<ALostWithinCharacter>(PlayerClass, FVector(0.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	APickupItemActor* Flashlight = Env.World->SpawnActor<APickupItemActor>(
		APickupItemActor::StaticClass(), FVector(0.0f, 300.0f, 0.0f), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("Player controller"), Controller) || !TestNotNull(TEXT("Player character"), Player)
		|| !TestNotNull(TEXT("Flashlight pickup"), Flashlight))
	{
		return false;
	}
	Controller->Possess(Player);
	Flashlight->ConfigureAsFlashlight();
	IInteractableInterface::Execute_OnInteract(Flashlight, Player);
	UFunction* ToggleFlashlight = Player->FindFunction(TEXT("DoToggleFlashlight"));
	USpotLightComponent* SpotLight = Player->GetFlashlightSpotLight();
	if (!TestNotNull(TEXT("Flashlight toggle"), ToggleFlashlight) || !TestNotNull(TEXT("Player spotlight"), SpotLight))
	{
		return false;
	}

	// Flashlight off: the dog ignores the player and keeps roaming.
	Attraction->TickComponent(0.01f, LEVELTICK_All, nullptr);
	TestFalse(TEXT("Not attracted while the flashlight is off"), Attraction->IsAttracted());
	TestTrue(TEXT("Roam loop runs while the flashlight is off"), UMonsterAIComponent::CanRunNormalAI(Dog));

	// On, beam on the dog: attracted in the same tick, running at its fastest gait.
	Player->ProcessEvent(ToggleFlashlight, nullptr);
	SpotLight->SetWorldLocationAndRotation(FVector(0.0f, 0.0f, 50.0f), FRotator::ZeroRotator);
	if (!TestTrue(TEXT("Held flashlight switched on"), Player->IsFlashlightOn()))
	{
		return false;
	}
	Attraction->TickComponent(0.01f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("Attracted on the first lit tick"), Attraction->IsAttracted());
	TestTrue(TEXT("IsActorAttractedToLight"), UMonsterLightAttractionComponent::IsActorAttractedToLight(Dog));
	TestFalse(TEXT("Roam loop waits while attracted"), UMonsterAIComponent::CanRunNormalAI(Dog));
	TestEqual(TEXT("Runs at the panic gait speed"), Move->MaxWalkSpeed, 420.0f);

	// Off: released in the same tick and its walk speed restored.
	Player->ProcessEvent(ToggleFlashlight, nullptr);
	Attraction->TickComponent(0.01f, LEVELTICK_All, nullptr);
	TestFalse(TEXT("Released on the first tick after switching off"), Attraction->IsAttracted());
	TestTrue(TEXT("Roam loop resumes after release"), UMonsterAIComponent::CanRunNormalAI(Dog));
	TestEqual(TEXT("Walk speed restored"), Move->MaxWalkSpeed, 90.0f);

	// On but pointing away: still drawn nearby, not from beyond AttractionRadius outside the beam.
	Player->ProcessEvent(ToggleFlashlight, nullptr);
	SpotLight->SetWorldLocationAndRotation(FVector(0.0f, 0.0f, 50.0f), FRotator(0.0f, 180.0f, 0.0f));
	Attraction->TickComponent(0.01f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("Nearby lit flashlight draws the dog"), Attraction->IsAttracted());
	Dog->SetActorLocation(FVector(Attraction->AttractionRadius + 500.0f, 0.0f, 0.0f));
	Attraction->TickComponent(0.01f, LEVELTICK_All, nullptr);
	TestFalse(TEXT("Far and outside the beam: not drawn"), Attraction->IsAttracted());

	// Behind a wall: the beam does not draw it.
	Dog->SetActorLocation(FVector(500.0f, 0.0f, 0.0f));
	SpotLight->SetWorldRotation(FRotator::ZeroRotator);
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	AStaticMeshActor* Wall = Env.World->SpawnActor<AStaticMeshActor>(FVector(250.0f, 0.0f, 50.0f), FRotator::ZeroRotator);
	if (TestNotNull(TEXT("Cube mesh"), Cube) && TestNotNull(TEXT("Wall actor"), Wall))
	{
		Wall->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
		Wall->GetStaticMeshComponent()->SetStaticMesh(Cube);
		Wall->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
		Wall->SetActorScale3D(FVector(0.2f, 6.0f, 6.0f));
		Attraction->TickComponent(0.01f, LEVELTICK_All, nullptr);
		TestFalse(TEXT("Wall between light and dog: not drawn"), Attraction->IsAttracted());
		Wall->Destroy();
	}

	// Cat reaction keeps priority.
	Attraction->TickComponent(0.01f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("Drawn again with a clear beam"), Attraction->IsAttracted());
	AI->bIsFleeing = true;
	Attraction->TickComponent(0.01f, LEVELTICK_All, nullptr);
	TestFalse(TEXT("Cat panic overrides the light"), Attraction->IsAttracted());
	AI->bIsFleeing = false;
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
