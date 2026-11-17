// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "AI/MonsterAIComponent.h"
#include "AIController.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/ProximityFloatSpinComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "Interaction/InteractableInterface.h"
#include "Misc/AutomationTest.h"
#include "TimerManager.h"

namespace
{
	struct FScopedMonsterTestWorld
	{
		UWorld* World = nullptr;

		FScopedMonsterTestWorld()
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

		~FScopedMonsterTestWorld()
		{
			if (World)
			{
				World->DestroyWorld(false);
				if (GEngine)
				{
					GEngine->DestroyWorldContext(World);
				}
			}
			CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
		}
	};

	struct FMonsterSetup
	{
		ACharacter* Actor = nullptr;
		UMonsterAIComponent* AI = nullptr;
	};

	FMonsterSetup SpawnMonster(UWorld* World, const FVector& Location, bool bWithPhysics)
	{
		FMonsterSetup Result;
		const FTransform Transform(Location);
		Result.Actor = World->SpawnActorDeferred<ACharacter>(ACharacter::StaticClass(), Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Result.Actor)
		{
			return Result;
		}
		Result.Actor->AutoPossessAI = EAutoPossessAI::Disabled;
		if (bWithPhysics)
		{
			USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/NPC/Monster/base_basic_pbr.base_basic_pbr"));
			Result.Actor->GetMesh()->SetSkeletalMesh(Mesh);
			Result.Actor->GetMesh()->SetRelativeLocation(FVector(0.0f, 0.0f, -Result.Actor->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()));
		}
		Result.AI = NewObject<UMonsterAIComponent>(Result.Actor, TEXT("CatReactionAI"));
		Result.AI->RegisterComponent();
		Result.Actor->FinishSpawning(Transform);
		return Result;
	}

	UProximityFloatSpinComponent* SpawnCat(UWorld* World, const FVector& Location, bool bActive)
	{
		const FTransform Transform(Location);
		AActor* Actor = World->SpawnActorDeferred<AActor>(AActor::StaticClass(), Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Actor)
		{
			return nullptr;
		}
		USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("Root"));
		Root->SetMobility(EComponentMobility::Movable);
		Actor->SetRootComponent(Root);
		Root->RegisterComponent();
		UProximityFloatSpinComponent* Cat = NewObject<UProximityFloatSpinComponent>(Actor, TEXT("FloatSpin"));
		Cat->bAutoSetHierarchyMovable = false;
		Cat->bDarkenSkyOnSpinWindow = false;
		Cat->bEnableDiscoEnvironmentOnSpinWindow = false;
		Cat->bUseMorphVicinityCheck = false;
		Cat->bEnableDirectedPath = false;
		Cat->bJumpBeforeSpin = false;
		Cat->FloatHeight = 0.0f;
		Cat->BobAmplitude = 0.0f;
		Cat->SpinTriggerInteractionThreshold = 1;
		Cat->ItemDropInteractionThreshold = 100;
		Cat->InteractionSoundSearchPaths.Reset();
		Cat->SpinTimeWindows = { {0.25f, 1.0f, 1.0f}, {3.0f, 4.0f, 1.0f} };
		Cat->RegisterComponent();
		Actor->FinishSpawning(Transform);
		if (bActive)
		{
			IInteractableInterface::Execute_OnInteract(Cat, Actor);
		}
		return Cat;
	}

	void TickMonster(const FMonsterSetup& Monster)
	{
		++GFrameCounter;
		Monster.Actor->GetWorld()->GetTimerManager().Tick(0.0f);
		// Dispatch through the public actor-component API, as the engine tick does.
		static_cast<UActorComponent*>(Monster.AI)->TickComponent(0.02f, LEVELTICK_All, nullptr);
	}

	void AdvanceRecovery(UWorld* World, float DeltaTime)
	{
		++GFrameCounter;
		World->GetTimerManager().Tick(DeltaTime);
	}

	UBoxComponent* SpawnCollisionBox(UWorld* World, const FVector& Location, const FVector& Extent)
	{
		AActor* Actor = World->SpawnActor<AActor>();
		if (!Actor)
		{
			return nullptr;
		}
		UBoxComponent* Box = NewObject<UBoxComponent>(Actor, TEXT("CollisionBox"));
		Actor->SetRootComponent(Box);
		Box->SetBoxExtent(Extent);
		Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Box->SetCollisionObjectType(ECC_WorldStatic);
		Box->SetCollisionResponseToAllChannels(ECR_Block);
		Box->RegisterComponent();
		Box->SetWorldLocation(Location);
		return Box;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLostWithinMonsterCatSelectionTest,
	"LostWithin.AI.MonsterCatReaction.Selection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FLostWithinMonsterCatSelectionTest::RunTest(const FString& Parameters)
{
	FScopedMonsterTestWorld Environment;
	if (!TestNotNull(TEXT("World exists"), Environment.World))
	{
		return false;
	}
	const FMonsterSetup Monster = SpawnMonster(Environment.World, FVector::ZeroVector, false);
	if (!TestNotNull(TEXT("Monster exists"), Monster.Actor) || !TestNotNull(TEXT("AI component exists"), Monster.AI))
	{
		return false;
	}
	AAIController* Controller = Environment.World->SpawnActor<AAIController>();
	if (!TestNotNull(TEXT("AIController exists"), Controller))
	{
		return false;
	}
	Controller->Possess(Monster.Actor);
	UProximityFloatSpinComponent* Dormant = SpawnCat(Environment.World, FVector(50.0f, 0.0f, 0.0f), false);
	UProximityFloatSpinComponent* Active = SpawnCat(Environment.World, FVector(700.0f, 0.0f, 0.0f), true);
	if (!TestNotNull(TEXT("Dormant cat exists"), Dormant) || !TestNotNull(TEXT("Active cat exists"), Active))
	{
		return false;
	}
	TickMonster(Monster);
	TestFalse(TEXT("Inactive nearby cat and active distant cat are ignored"), Monster.AI->bCatIsNear);
	TestTrue(TEXT("Normal AI is allowed without a threat"), UMonsterAIComponent::CanRunNormalAI(Monster.Actor));

	// This isolated world deliberately has no navigation data, exercising the movement fallback.
	AddExpectedError(TEXT("has no escape navigation path"), EAutomationExpectedErrorFlags::Contains, 1);
	Active->GetOwner()->SetActorLocation(FVector(500.0f, 0.0f, 0.0f));
	TickMonster(Monster);
	TestTrue(TEXT("The 500 cm boundary triggers fear"), Monster.AI->bCatIsNear && Monster.AI->bIsFleeing);
	TestEqual(TEXT("The inactive closer cat cannot mask an active cat"), Monster.AI->GetThreateningCat(), Active->GetOwner());
	TestFalse(TEXT("Normal Blueprint movement is suspended"), UMonsterAIComponent::CanRunNormalAI(Monster.Actor));
	Monster.Actor->ConsumeMovementInputVector();

	UProximityFloatSpinComponent* Nearer = SpawnCat(Environment.World, FVector(-200.0f, 0.0f, 0.0f), true);
	if (!TestNotNull(TEXT("Second active cat exists"), Nearer))
	{
		return false;
	}
	TickMonster(Monster);
	TestEqual(TEXT("Nearest active cat determines the threat"), Monster.AI->GetThreateningCat(), Nearer->GetOwner());
	TestTrue(TEXT("Flee input points away from the nearest cat"), Monster.Actor->GetPendingMovementInputVector().X > 0.0f);
	Nearer->TickComponent(2.0f, LEVELTICK_All, nullptr);
	TickMonster(Monster);
	TestTrue(TEXT("Fear remains in the gap between spin windows"), Monster.AI->bCatIsNear && Nearer->IsFloatSpinActive());
	Nearer->GetOwner()->Destroy();
	Active->GetOwner()->Destroy();
	TickMonster(Monster);
	TestFalse(TEXT("Destroyed active cats clear the threat"), Monster.AI->bCatIsNear || Monster.AI->bIsFleeing);
	TestTrue(TEXT("Normal movement resumes"), UMonsterAIComponent::CanRunNormalAI(Monster.Actor));
	TestFalse(TEXT("Invalid actor cannot run normal AI"), UMonsterAIComponent::CanRunNormalAI(nullptr));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLostWithinMonsterCatRagdollTest,
	"LostWithin.AI.MonsterCatReaction.ThrowAndRecovery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FLostWithinMonsterCatRagdollTest::RunTest(const FString& Parameters)
{
	FScopedMonsterTestWorld Environment;
	if (!TestNotNull(TEXT("World exists"), Environment.World))
	{
		return false;
	}
	const FMonsterSetup Monster = SpawnMonster(Environment.World, FVector(0.0f, 0.0f, 100.0f), true);
	if (!TestNotNull(TEXT("Monster exists"), Monster.Actor) || !TestNotNull(TEXT("AI exists"), Monster.AI))
	{
		return false;
	}
	USkeletalMeshComponent* Mesh = Monster.Actor->GetMesh();
	UCapsuleComponent* Capsule = Monster.Actor->GetCapsuleComponent();
	if (!TestNotNull(TEXT("Monster mesh asset loaded"), Mesh->GetSkeletalMeshAsset()) || !TestNotNull(TEXT("Monster physics asset loaded"), Mesh->GetPhysicsAsset()))
	{
		return false;
	}
	Mesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	Mesh->SetEnableGravity(false);
	Capsule->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Monster.Actor->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
	const FTransform OriginalMeshTransform = Mesh->GetRelativeTransform();
	const FName OriginalProfile = Mesh->GetCollisionProfileName();
	UProximityFloatSpinComponent* Cat = SpawnCat(Environment.World, FVector(-100.0f, 0.0f, 100.0f), true);
	if (!TestNotNull(TEXT("Cat exists"), Cat) || !TestNotNull(TEXT("Floor exists"), SpawnCollisionBox(Environment.World, FVector(0.0f, 0.0f, -20.0f), FVector(5000.0f, 5000.0f, 20.0f))))
	{
		return false;
	}
	TickMonster(Monster);
	if (!TestTrue(TEXT("100 cm boundary enters ragdoll"), Monster.AI->bIsRagdolled))
	{
		return false;
	}
	TestFalse(TEXT("No navigation state is set after entering ragdoll"), Monster.AI->bIsFleeing);
	TestEqual(TEXT("Movement is disabled"), Monster.Actor->GetCharacterMovement()->MovementMode.GetValue(), MOVE_None);
	TestEqual(TEXT("Capsule is disabled"), Capsule->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
	const FVector LaunchVelocity = Mesh->GetPhysicsLinearVelocity(TEXT("Hips"));
	TestTrue(TEXT("Throw has outward velocity"), LaunchVelocity.X > 1000.0f);
	TestTrue(TEXT("Throw has upward velocity"), LaunchVelocity.Z > 500.0f);
	TickMonster(Monster);
	TestTrue(TEXT("A second tick cannot apply another impulse"), Mesh->GetPhysicsLinearVelocity(TEXT("Hips")).Equals(LaunchVelocity, 0.1f));
	TestFalse(TEXT("Ragdoll suspends normal AI"), UMonsterAIComponent::CanRunNormalAI(Monster.Actor));

	Cat->GetOwner()->Destroy();
	Mesh->SetAllPhysicsLinearVelocity(FVector::ZeroVector);
	const FVector LandedHip(600.0f, 0.0f, 30.0f);
	Mesh->SetWorldLocation(Mesh->GetComponentLocation() + LandedHip - Mesh->GetSocketLocation(TEXT("Hips")), false, nullptr, ETeleportType::TeleportPhysics);
	const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	UBoxComponent* Ceiling = SpawnCollisionBox(Environment.World,
		FVector(600.0f, 0.0f, HalfHeight * 2.0f - 10.0f), FVector(150.0f, 150.0f, 5.0f));
	if (!TestNotNull(TEXT("Low ceiling exists"), Ceiling))
	{
		return false;
	}
	AdvanceRecovery(Environment.World, 3.1f);
	TestTrue(TEXT("Blocked standing position defers recovery"), Monster.AI->bIsRagdolled);
	Ceiling->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	AdvanceRecovery(Environment.World, 0.3f);
	TestFalse(TEXT("Recovery retries after the obstruction clears"), Monster.AI->bIsRagdolled);
	TestTrue(TEXT("Capsule recovers by the landed body instead of the original location"), FMath::IsNearlyEqual(Monster.Actor->GetActorLocation().X, 600.0, 1.0));
	TestTrue(TEXT("Mesh relative transform is restored"), Mesh->GetRelativeTransform().Equals(OriginalMeshTransform));
	TestTrue(TEXT("Mesh is reattached to the capsule"), Mesh->GetAttachParent() == Capsule);
	TestEqual(TEXT("Mesh profile is restored"), Mesh->GetCollisionProfileName(), OriginalProfile);
	TestEqual(TEXT("Custom mesh response is restored"), Mesh->GetCollisionResponseToChannel(ECC_Visibility), ECR_Block);
	TestFalse(TEXT("Original gravity setting is restored"), Mesh->IsGravityEnabled());
	TestEqual(TEXT("Original capsule collision is restored"), Capsule->GetCollisionEnabled(), ECollisionEnabled::QueryOnly);
	TestEqual(TEXT("Original movement mode is restored"), Monster.Actor->GetCharacterMovement()->MovementMode.GetValue(), MOVE_Flying);
	TickMonster(Monster);
	TestTrue(TEXT("Normal AI resumes after recovery and cat destruction"), UMonsterAIComponent::CanRunNormalAI(Monster.Actor));

	UProximityFloatSpinComponent* OverlappingCat = SpawnCat(Environment.World, Monster.Actor->GetActorLocation(), true);
	if (!TestNotNull(TEXT("Overlapping cat exists"), OverlappingCat))
	{
		return false;
	}
	TickMonster(Monster);
	TestTrue(TEXT("Monster can be thrown again after recovery"), Monster.AI->bIsRagdolled);
	TestTrue(TEXT("Coincident cat still gives a finite outward throw"), Mesh->GetPhysicsLinearVelocity(TEXT("Hips")).X > 1000.0f);
	Monster.Actor->Destroy();
	AdvanceRecovery(Environment.World, 4.0f); // EndPlay must cancel recovery callbacks.
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
