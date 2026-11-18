// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "AI/MonsterAIComponent.h"
#include "AI/MonsterLightAverseComponent.h"
#include "AIController.h"
#include "Components/CapsuleComponent.h"
#include "Components/SpotLightComponent.h"
#include "Engine/Engine.h"
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

	TestEqual(TEXT("State transitioned to Startled"), Ghoul.LightAverse->GetLightState(), EGhoulLightState::Startled);
	TestTrue(TEXT("Ghoul is marked repelled"), Ghoul.LightAverse->IsRepelled());
	TestTrue(TEXT("Ghoul shields its face"), Ghoul.LightAverse->IsShieldingFace());
	TestFalse(TEXT("Ghoul suspends dark stalking"), Ghoul.LightAverse->IsStalkingInDarkness());
	TestTrue(TEXT("Current light intensity is recorded"), Ghoul.LightAverse->GetCurrentLightIntensity() > 0.0f);

	// 3. Coordination with CanRunNormalAI
	TestFalse(TEXT("CanRunNormalAI is suspended while repelled by light"), UMonsterAIComponent::CanRunNormalAI(Ghoul.Actor));
	TestTrue(TEXT("IsActorLightRepelled returns true while repelled"), UMonsterLightAverseComponent::IsActorLightRepelled(Ghoul.Actor));

	// 4. Verify Retreat Direction
	const FVector RetreatDir = Ghoul.LightAverse->CurrentRetreatDirection;
	TestTrue(TEXT("Retreat direction is normalized"), RetreatDir.IsNormalized());
	TestTrue(TEXT("Retreat direction moves away from light (positive X)"), RetreatDir.X > 0.0f);

	// 5. Simulate Startled -> Retreating transition
	Ghoul.LightAverse->TickComponent(0.5f, LEVELTICK_All, nullptr);
	TestEqual(TEXT("State transitioned to Retreating after startled duration"), Ghoul.LightAverse->GetLightState(), EGhoulLightState::Retreating);
	TestTrue(TEXT("IsRetreating is true"), Ghoul.LightAverse->IsRetreating());
	TestTrue(TEXT("Still shielding face while retreating"), Ghoul.LightAverse->IsShieldingFace());
	TestFalse(TEXT("CanRunNormalAI remains false while retreating"), UMonsterAIComponent::CanRunNormalAI(Ghoul.Actor));

	// 6. Move the beam off Ghoul and back before it finishes recovering.
	SpotLight->SetWorldRotation(FRotator(0.0f, 180.0f, 0.0f));
	Ghoul.LightAverse->TickComponent(0.1f, LEVELTICK_All, nullptr);
	TestEqual(TEXT("Turning away starts recovery"), Ghoul.LightAverse->GetLightState(), EGhoulLightState::Recovering);
	SpotLight->SetWorldRotation(FRotator::ZeroRotator);
	Ghoul.LightAverse->TickComponent(0.1f, LEVELTICK_All, nullptr);
	TestEqual(TEXT("Reappearing light startles Ghoul again"), Ghoul.LightAverse->GetLightState(), EGhoulLightState::Startled);
	Ghoul.LightAverse->TickComponent(0.5f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("Ghoul retreats again"), Ghoul.LightAverse->IsRetreating());

	Player->ProcessEvent(ToggleFlashlight, nullptr);
	Ghoul.LightAverse->TickComponent(0.1f, LEVELTICK_All, nullptr);
	Ghoul.LightAverse->TickComponent(Ghoul.LightAverse->RecoveryDuration + 0.1f, LEVELTICK_All, nullptr);
	TestEqual(TEXT("State returned to DarkStalking"), Ghoul.LightAverse->GetLightState(), EGhoulLightState::DarkStalking);
	TestFalse(TEXT("No longer shielding face"), Ghoul.LightAverse->IsShieldingFace());
	TestFalse(TEXT("No longer repelled"), Ghoul.LightAverse->IsRepelled());
	TestFalse(TEXT("No longer retreating"), Ghoul.LightAverse->IsRetreating());
	TestTrue(TEXT("Resumed aggressive dark stalking"), Ghoul.LightAverse->IsStalkingInDarkness());
	TestTrue(TEXT("CanRunNormalAI resumes when back in darkness"), UMonsterAIComponent::CanRunNormalAI(Ghoul.Actor));

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

#endif // WITH_DEV_AUTOMATION_TESTS
