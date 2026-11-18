// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "AI/MonsterSightFreezeComponent.h"
#include "AI/MonsterAIComponent.h"
#include "AIController.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "LostWithinCharacter.h"
#include "Misc/AutomationTest.h"

namespace
{
	struct FScopedDollTestWorld
	{
		UWorld* World = nullptr;

		FScopedDollTestWorld()
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

		~FScopedDollTestWorld()
		{
			if (World)
			{
				World->EndPlay(EEndPlayReason::Quit);
				World->DestroyWorld(false);
				if (GEngine)
				{
					GEngine->DestroyWorldContext(World);
				}
				World = nullptr;
			}
			CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
		}
	};

	struct FDollSetup
	{
		ACharacter* Actor = nullptr;
		UMonsterSightFreezeComponent* SightFreeze = nullptr;
		UMonsterAIComponent* CatAI = nullptr;
	};

	FDollSetup SpawnTestDoll(UWorld* World, const FVector& Location)
	{
		FDollSetup Result;
		const FTransform Transform(Location);
		Result.Actor = World->SpawnActorDeferred<ACharacter>(ACharacter::StaticClass(), Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Result.Actor)
		{
			return Result;
		}

		Result.Actor->AutoPossessAI = EAutoPossessAI::Disabled;

		Result.CatAI = NewObject<UMonsterAIComponent>(Result.Actor, TEXT("CatReactionAI"));
		Result.CatAI->RegisterComponent();

		Result.SightFreeze = NewObject<UMonsterSightFreezeComponent>(Result.Actor, TEXT("SightFreezeAI"));
		Result.SightFreeze->bRequireLineOfSight = false; // Synthetic test environment without static geometry
		Result.SightFreeze->RegisterComponent();

		Result.Actor->FinishSpawning(Transform);
		return Result;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDollSightFreezeBasicTest,
	"LostWithin.AI.DollSightFreeze.Basic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDollSightFreezeBasicTest::RunTest(const FString& Parameters)
{
	FScopedDollTestWorld Env;
	if (!TestNotNull(TEXT("World created"), Env.World))
	{
		return false;
	}

	FDollSetup Doll = SpawnTestDoll(Env.World, FVector(500.0f, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("Doll actor spawned"), Doll.Actor) ||
		!TestNotNull(TEXT("SightFreeze component exists"), Doll.SightFreeze))
	{
		return false;
	}

	// 1. Initial State
	TestFalse(TEXT("Initially not frozen"), Doll.SightFreeze->IsFrozen());
	TestFalse(TEXT("Initially not observed"), Doll.SightFreeze->IsObserved());
	TestTrue(TEXT("Initially can move"), UMonsterSightFreezeComponent::CanActorMove(Doll.Actor));
	TestFalse(TEXT("Initially IsActorSightFrozen is false"), UMonsterSightFreezeComponent::IsActorSightFrozen(Doll.Actor));

	// 2. Freeze Application
	Doll.SightFreeze->SetFrozen(true);
	TestTrue(TEXT("IsFrozen is true after SetFrozen(true)"), Doll.SightFreeze->IsFrozen());
	TestFalse(TEXT("CanActorMove is false when frozen"), UMonsterSightFreezeComponent::CanActorMove(Doll.Actor));
	TestTrue(TEXT("IsActorSightFrozen is true when frozen"), UMonsterSightFreezeComponent::IsActorSightFrozen(Doll.Actor));

	UCharacterMovementComponent* MoveComp = Doll.Actor->GetCharacterMovement();
	if (TestNotNull(TEXT("Movement component exists"), MoveComp))
	{
		TestEqual(TEXT("Movement mode is MOVE_None while frozen"), MoveComp->MovementMode.GetValue(), MOVE_None);
	}

	if (USkeletalMeshComponent* Mesh = Doll.Actor->GetMesh())
	{
		TestTrue(TEXT("Animation is paused while frozen"), Mesh->bPauseAnims);
	}

	// 3. Unfreeze Application
	Doll.SightFreeze->SetFrozen(false);
	TestFalse(TEXT("IsFrozen is false after SetFrozen(false)"), Doll.SightFreeze->IsFrozen());
	TestTrue(TEXT("CanActorMove is true after unfreezing"), UMonsterSightFreezeComponent::CanActorMove(Doll.Actor));

	if (MoveComp)
	{
		TestEqual(TEXT("Movement mode is MOVE_Walking after unfreezing"), MoveComp->MovementMode.GetValue(), MOVE_Walking);
		TestEqual(TEXT("Walk speed is set to StalkWalkSpeed"), MoveComp->MaxWalkSpeed, Doll.SightFreeze->StalkWalkSpeed);
	}

	if (USkeletalMeshComponent* Mesh = Doll.Actor->GetMesh())
	{
		TestFalse(TEXT("Animation is unpaused after unfreezing"), Mesh->bPauseAnims);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDollFrustumAndMathTest,
	"LostWithin.AI.DollSightFreeze.FrustumMath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDollFrustumAndMathTest::RunTest(const FString& Parameters)
{
	FScopedDollTestWorld Env;
	if (!TestNotNull(TEXT("World created"), Env.World))
	{
		return false;
	}

	FDollSetup Doll = SpawnTestDoll(Env.World, FVector(0.0f, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("SightFreeze component exists"), Doll.SightFreeze))
	{
		return false;
	}

	const FVector CamLoc(0.0f, 0.0f, 100.0f);
	const FVector CamFwd(1.0f, 0.0f, 0.0f); // Looking along +X
	const float HalfFOV = 45.0f; // 90 degree total FOV

	// Directly ahead (+X) -> inside frustum
	const FVector Ahead(500.0f, 0.0f, 100.0f);
	TestTrue(TEXT("Point directly ahead is in frustum"),
		Doll.SightFreeze->IsPointInPlayerFrustum(Ahead, CamLoc, CamFwd, HalfFOV));

	// 30 degrees to the right -> inside 45-degree half FOV
	const FVector Angle30(500.0f, 500.0f * FMath::Tan(FMath::DegreesToRadians(30.0f)), 100.0f);
	TestTrue(TEXT("Point at 30 degrees is in frustum"),
		Doll.SightFreeze->IsPointInPlayerFrustum(Angle30, CamLoc, CamFwd, HalfFOV));

	// 60 degrees to the right -> outside 45-degree half FOV
	const FVector Angle60(500.0f, 500.0f * FMath::Tan(FMath::DegreesToRadians(60.0f)), 100.0f);
	TestFalse(TEXT("Point at 60 degrees is outside frustum"),
		Doll.SightFreeze->IsPointInPlayerFrustum(Angle60, CamLoc, CamFwd, HalfFOV));

	// Behind camera (-X) -> outside frustum
	const FVector Behind(-300.0f, 0.0f, 100.0f);
	TestFalse(TEXT("Point behind camera is outside frustum"),
		Doll.SightFreeze->IsPointInPlayerFrustum(Behind, CamLoc, CamFwd, HalfFOV));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDollCatPriorityAndMenuDistractionTest,
	"LostWithin.AI.DollSightFreeze.CatAndMenuPriority",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDollCatPriorityAndMenuDistractionTest::RunTest(const FString& Parameters)
{
	FScopedDollTestWorld Env;
	if (!TestNotNull(TEXT("World created"), Env.World))
	{
		return false;
	}

	FDollSetup Doll = SpawnTestDoll(Env.World, FVector(500.0f, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("Doll actor spawned"), Doll.Actor) ||
		!TestNotNull(TEXT("SightFreeze component exists"), Doll.SightFreeze) ||
		!TestNotNull(TEXT("CatAI component exists"), Doll.CatAI))
	{
		return false;
	}

	// 1. Menu / distraction check with null actor
	TestFalse(TEXT("Null player is not in menu"), Doll.SightFreeze->IsPlayerInMenuOrPadlock(nullptr));

	// 2. Distraction with plain actor without menus
	TestFalse(TEXT("Fresh test character is not in menu"), Doll.SightFreeze->IsPlayerInMenuOrPadlock(Doll.Actor));

	// 3. Cat Priority: when CatAI is ragdolled or fleeing, sight freeze yields
	Doll.SightFreeze->SetFrozen(true);
	TestTrue(TEXT("Doll initially frozen"), Doll.SightFreeze->IsFrozen());

	Doll.CatAI->bIsRagdolled = true;
	// Ticking the component should release the sight freeze in favor of the cat ragdoll
	Doll.SightFreeze->TickComponent(0.1f, LEVELTICK_All, nullptr);
	TestFalse(TEXT("Cat ragdoll takes priority over sight freeze"), Doll.SightFreeze->IsFrozen());

	Doll.CatAI->bIsRagdolled = false;

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
