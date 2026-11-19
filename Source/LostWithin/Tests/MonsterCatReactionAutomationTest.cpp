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
#include "NavigationSystem.h"
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
			USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/NPC/Lunger/base_basic_pbr.base_basic_pbr"));
			Result.Actor->GetMesh()->SetSkeletalMesh(Mesh);
			Result.Actor->GetMesh()->SetRelativeLocation(FVector(0.0f, 0.0f, -Result.Actor->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()));
		}
		Result.AI = NewObject<UMonsterAIComponent>(Result.Actor, TEXT("CatReactionAI"));
		Result.AI->RegisterComponent();
		Result.Actor->FinishSpawning(Transform);
		return Result;
	}

	UProximityFloatSpinComponent* SpawnCat(UWorld* World, const FVector& Location, bool bActive, bool bAdvanceToThirdSpin = true)
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
		Root->SetWorldTransform(Transform);
		Root->RegisterComponent();
		UProximityFloatSpinComponent* Cat = NewObject<UProximityFloatSpinComponent>(Actor, TEXT("FloatSpin"));
		Cat->bAutoSetHierarchyMovable = false;
		Cat->bDarkenSkyOnSpinWindow = false;
		Cat->bEnableDiscoEnvironmentOnSpinWindow = false;
		Cat->bUseMorphVicinityCheck = false;
		Cat->bEnableDirectedPath = false;
		Cat->bJumpBeforeSpin = false;
		Cat->bLoopMusic = false;
		Cat->bRepelMonstersInSanctuary = false;
		Cat->FloatHeight = 0.0f;
		Cat->BobAmplitude = 0.0f;
		// One pet starts the sequence here; the pet-and-spin ritual has its own test.
		Cat->bRequirePetSpinSequence = false;
		Cat->SpinTriggerInteractionThreshold = 1;
		Cat->ItemDropInteractionThreshold = 100;
		Cat->InteractionSoundSearchPaths.Reset();
		Cat->SpinTimeWindows = { {0.25f, 1.0f, 1.0f}, {3.0f, 4.0f, 1.0f}, {6.0f, 8.0f, 1.0f} };
		Cat->RegisterComponent();
		Actor->FinishSpawning(Transform);
		if (bActive)
		{
			IInteractableInterface::Execute_OnInteract(Cat, Actor);
			if (bAdvanceToThirdSpin)
			{
				Cat->TickComponent(6.0f, LEVELTICK_All, nullptr);
			}
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

	void StepPhysics(UWorld* World)
	{
		++GFrameCounter;
		World->Tick(LEVELTICK_All, 1.0f / 60.0f);
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
	// Isolate flee selection; throwing is covered by the capsule-radius and priority tests.
	Monster.AI->CatRagdollRadius = 0.0f;
	TestEqual(TEXT("The fixture movement component owns the monster"),
		Monster.Actor->GetCharacterMovement()->GetPawnOwner(), static_cast<APawn*>(Monster.Actor));
	UProximityFloatSpinComponent* Dormant = SpawnCat(Environment.World, FVector(50.0f, 0.0f, 0.0f), false);
	UProximityFloatSpinComponent* Active = SpawnCat(Environment.World, FVector(700.0f, 0.0f, 0.0f), true);
	if (!TestNotNull(TEXT("Dormant cat exists"), Dormant) || !TestNotNull(TEXT("Active cat exists"), Active))
	{
		return false;
	}
	TestTrue(TEXT("Advancing the cat to spin #3 preserves its requested spawn position"),
		Active->GetOwner()->GetActorLocation().Equals(FVector(700.0f, 0.0f, 0.0f), 0.1f));
	TickMonster(Monster);
	TestFalse(TEXT("Inactive nearby cat and active distant cat are ignored"), Monster.AI->bCatIsNear);
	TestTrue(TEXT("Normal AI is allowed without a threat"), UMonsterAIComponent::CanRunNormalAI(Monster.Actor));

	// This isolated world deliberately has no navigation data, exercising the movement fallback.
	TestNull(TEXT("The fallback fixture has no navigation system"), UNavigationSystemV1::GetCurrent(Environment.World));
	AddExpectedError(TEXT("has no escape navigation path"), EAutomationExpectedErrorFlags::Contains, 1);
	Active->GetOwner()->SetActorLocation(FVector(500.0f, 0.0f, 0.0f));
	TickMonster(Monster);
	TestTrue(TEXT("The 500 cm boundary triggers fear"), Monster.AI->bCatIsNear && Monster.AI->bIsFleeing);
	TestEqual(TEXT("The inactive closer cat cannot mask an active cat"), Monster.AI->GetThreateningCat(), Active->GetOwner());
	TestFalse(TEXT("Normal Blueprint movement is suspended"), UMonsterAIComponent::CanRunNormalAI(Monster.Actor));
	const FVector FirstFleeInput = Monster.Actor->ConsumeMovementInputVector();
	TestTrue(TEXT("Missing navigation still gives input away from the first cat"), FirstFleeInput.X < 0.0f);

	UProximityFloatSpinComponent* Nearer = SpawnCat(Environment.World, FVector(-200.0f, 0.0f, 0.0f), true);
	if (!TestNotNull(TEXT("Second active cat exists"), Nearer))
	{
		return false;
	}
	TickMonster(Monster);
	TestEqual(TEXT("Nearest active cat determines the threat"), Monster.AI->GetThreateningCat(), Nearer->GetOwner());
	const FVector NearestFleeInput = Monster.Actor->GetPendingMovementInputVector();
	TestTrue(FString::Printf(TEXT("Flee input points away from the nearest cat (%s)"), *NearestFleeInput.ToString()),
		NearestFleeInput.X > 0.0f);
	UProximityFloatSpinComponent* EarlierSpin = SpawnCat(Environment.World, FVector(40.0f, 0.0f, 0.0f), true, false);
	if (!TestNotNull(TEXT("Earlier-spin cat exists"), EarlierSpin))
	{
		return false;
	}
	EarlierSpin->TickComponent(0.25f, LEVELTICK_All, nullptr);
	TickMonster(Monster);
	TestEqual(TEXT("A closer cat in its first spin window now threatens too"), Monster.AI->GetThreateningCat(), EarlierSpin->GetOwner());
	TestTrue(TEXT("Any active float-spin keeps the monster fleeing"), Monster.AI->bIsFleeing);
	EarlierSpin->GetOwner()->Destroy();
	Active->GetOwner()->SetActorLocation(FVector(700.0f, 0.0f, 0.0f));
	Nearer->TickComponent(2.25f, LEVELTICK_All, nullptr);
	TickMonster(Monster);
	TestTrue(TEXT("The sequence can remain active after its third window"), Nearer->IsFloatSpinActive());
	TestTrue(TEXT("Fear continues while the float-spin sequence is still active"), Monster.AI->bCatIsNear && Monster.AI->bIsFleeing);
	TestEqual(TEXT("The remaining nearby cat is the threat"), Monster.AI->GetThreateningCat(), Nearer->GetOwner());
	TestFalse(TEXT("Normal AI stays suspended while the sequence runs"), UMonsterAIComponent::CanRunNormalAI(Monster.Actor));
	Nearer->GetOwner()->Destroy();
	Active->GetOwner()->Destroy();
	TickMonster(Monster);
	TestFalse(TEXT("Destroyed active cats clear the threat"), Monster.AI->bCatIsNear || Monster.AI->bIsFleeing);
	TestTrue(TEXT("Normal movement resumes"), UMonsterAIComponent::CanRunNormalAI(Monster.Actor));
	TestFalse(TEXT("Invalid actor cannot run normal AI"), UMonsterAIComponent::CanRunNormalAI(nullptr));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLostWithinMonsterFloatSpinGateTest,
	"LostWithin.AI.MonsterCatReaction.FloatSpinGate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FLostWithinMonsterFloatSpinGateTest::RunTest(const FString& Parameters)
{
	FScopedMonsterTestWorld Environment;
	if (!TestNotNull(TEXT("World exists"), Environment.World))
	{
		return false;
	}
	const FMonsterSetup Monster = SpawnMonster(Environment.World, FVector(0.0f, 0.0f, 100.0f), false);
	// Flee-only fixture: inside the 500 cm fear range, with throwing isolated away.
	UProximityFloatSpinComponent* Cat = SpawnCat(Environment.World, FVector(400.0f, 0.0f, 100.0f), false);
	if (!TestNotNull(TEXT("Monster exists"), Monster.Actor) || !TestNotNull(TEXT("AI exists"), Monster.AI)
		|| !TestNotNull(TEXT("Cat exists"), Cat))
	{
		return false;
	}
	Monster.AI->CatRagdollRadius = 0.0f;
	// The fixture has no controller; each fear episode reports that once.
	AddExpectedError(TEXT("needs an AIController to flee"), EAutomationExpectedErrorFlags::Contains, 0);
	auto ExpectFear = [&](bool bExpected, const TCHAR* Context)
	{
		TickMonster(Monster);
		TestEqual(Context, Monster.AI->bCatIsNear && Monster.AI->bIsFleeing, bExpected);
		TestEqual(TEXT("Normal AI is suspended exactly while afraid"), UMonsterAIComponent::CanRunNormalAI(Monster.Actor), !bExpected);
	};
	ExpectFear(false, TEXT("An idle cat in fear range is ignored"));
	IInteractableInterface::Execute_OnInteract(Cat, Cat->GetOwner());
	TestTrue(TEXT("Interaction starts the float-spin sequence"), Cat->IsFloatSpinActive());
	ExpectFear(true, TEXT("Activation before the first spin already frightens"));
	Cat->TickComponent(0.25f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("First window is identified"), Cat->IsSpinWindowActive(0));
	ExpectFear(true, TEXT("The first spin frightens"));
	Cat->TickComponent(1.25f, LEVELTICK_All, nullptr);
	ExpectFear(true, TEXT("The gap after the first spin frightens"));
	Cat->TickComponent(1.5f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("Second window is identified"), Cat->IsSpinWindowActive(1));
	ExpectFear(true, TEXT("The second spin frightens"));
	Cat->TickComponent(3.0f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("Third window is identified"), Cat->IsSpinWindowActive(2));
	ExpectFear(true, TEXT("The third spin frightens"));
	Cat->TickComponent(2.25f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("The sequence can remain active after its last window"), Cat->IsFloatSpinActive());
	ExpectFear(true, TEXT("An active sequence after its last window frightens"));
	Cat->bUseSpinTimeWindows = false;
	ExpectFear(true, TEXT("Unwindowed float-spin frightens"));
	Cat->bUseSpinTimeWindows = true;
	Cat->SetComponentTickEnabled(false);
	ExpectFear(false, TEXT("A disabled float/spin component does not threaten the monster"));
	Cat->SetComponentTickEnabled(true);
	ExpectFear(true, TEXT("Re-enabling the running sequence frightens again"));
	Cat->GetOwner()->SetActorLocation(FVector(501.0f, 0.0f, 100.0f));
	ExpectFear(false, TEXT("An active cat beyond 500 cm is ignored"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLostWithinMonsterCapsuleRadiusTest,
	"LostWithin.AI.MonsterCatReaction.CapsuleThrowRadius",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FLostWithinMonsterCapsuleRadiusTest::RunTest(const FString& Parameters)
{
	FScopedMonsterTestWorld Environment;
	if (!TestNotNull(TEXT("World exists"), Environment.World))
	{
		return false;
	}
	for (int32 Case = 0; Case < 4; ++Case)
	{
		const FMonsterSetup Monster = SpawnMonster(Environment.World, FVector(Case * 2000.0f, 0.0f, 300.0f), true);
		if (!TestNotNull(TEXT("Capsule-test monster exists"), Monster.Actor) || !TestNotNull(TEXT("Capsule-test AI exists"), Monster.AI))
		{
			return false;
		}
		TestEqual(TEXT("The default throw radius is 3 m"), Monster.AI->CatRagdollRadius, 300.0f);
		// Isolate throwing and verify it is not clipped to the center-based flee range.
		Monster.AI->CatFleeRadius = 0.0f;
		UCapsuleComponent* Capsule = Monster.Actor->GetCapsuleComponent();
		if (Case == 2)
		{
			Capsule->SetWorldScale3D(FVector(1.5f));
			Capsule->SetWorldRotation(FRotator(0.0f, 0.0f, 90.0f));
		}
		const FVector Center = Capsule->GetComponentLocation();
		const FVector Direction = Case == 0 ? FVector::ForwardVector : Capsule->GetUpVector();
		const float BodyExtent = Case == 0 ? Capsule->GetScaledCapsuleRadius() : Capsule->GetScaledCapsuleHalfHeight();
		UProximityFloatSpinComponent* Cat = SpawnCat(Environment.World, Center + Direction * (BodyExtent + 301.0f), true);
		if (!TestNotNull(TEXT("Capsule-test cat exists"), Cat))
		{
			return false;
		}
		TickMonster(Monster);
		TestFalse(TEXT("A cat beyond 3 m from the capsule is ignored"), Monster.AI->bIsRagdolled || Monster.AI->bCatIsNear);
		const float SurfaceOffset = Case == 2 ? 299.0f : 300.0f;
		FVector ThrowPoint = Center + Direction * (BodyExtent + SurfaceOffset);
		if (Case == 3)
		{
			Monster.AI->CatRagdollRadius = 0.0f;
			ThrowPoint = Center + Direction * (BodyExtent * 0.5f);
		}
		Cat->GetOwner()->SetActorLocation(ThrowPoint);
		if (Case == 1)
		{
			TestTrue(TEXT("Hovering cat centers are farther apart than the throw radius"), FVector::Dist(Center, ThrowPoint) > 300.0);
		}
		TickMonster(Monster);
		TestTrue(TEXT("Surface boundary, hover, scaled/rotated and inside-capsule cases throw"), Monster.AI->bIsRagdolled);
		TestEqual(TEXT("Throw chooses the qualifying cat"), Monster.AI->GetThreateningCat(), Cat->GetOwner());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLostWithinMonsterThrowPriorityTest,
	"LostWithin.AI.MonsterCatReaction.ThrowPriority",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FLostWithinMonsterThrowPriorityTest::RunTest(const FString& Parameters)
{
	FScopedMonsterTestWorld Environment;
	if (!TestNotNull(TEXT("World exists"), Environment.World))
	{
		return false;
	}
	const FMonsterSetup Monster = SpawnMonster(Environment.World, FVector(0.0f, 0.0f, 300.0f), true);
	if (!TestNotNull(TEXT("Priority-test monster exists"), Monster.Actor) || !TestNotNull(TEXT("Priority-test AI exists"), Monster.AI))
	{
		return false;
	}
	const FVector Center = Monster.Actor->GetActorLocation();
	// Flee-only: 350 cm by center but over 300 cm from the capsule surface. The hovering cat is farther by center,
	// yet within 300 cm of the capsule top.
	UProximityFloatSpinComponent* FleeOnly = SpawnCat(Environment.World, Center + FVector(350.0f, 0.0f, 0.0f), true);
	UProximityFloatSpinComponent* Hovering = SpawnCat(Environment.World,
		Center + FVector(0.0f, 0.0f, Monster.Actor->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 290.0f), true);
	if (!TestNotNull(TEXT("Flee-only cat exists"), FleeOnly) || !TestNotNull(TEXT("Hovering throw cat exists"), Hovering))
	{
		return false;
	}
	TickMonster(Monster);
	TestTrue(TEXT("A capsule-range throw preempts a closer center-distance flee threat"), Monster.AI->bIsRagdolled);
	TestEqual(TEXT("The hovering throw candidate wins"), Monster.AI->GetThreateningCat(), Hovering->GetOwner());

	const FMonsterSetup SecondMonster = SpawnMonster(Environment.World, FVector(3000.0f, 0.0f, 300.0f), true);
	if (!TestNotNull(TEXT("Second priority-test monster exists"), SecondMonster.Actor) || !TestNotNull(TEXT("Second priority-test AI exists"), SecondMonster.AI))
	{
		return false;
	}
	const FVector SecondCenter = SecondMonster.Actor->GetActorLocation();
	UProximityFloatSpinComponent* Horizontal = SpawnCat(Environment.World, SecondCenter + FVector(90.0f, 0.0f, 0.0f), true);
	UProximityFloatSpinComponent* ClosestBody = SpawnCat(Environment.World,
		SecondCenter + FVector(0.0f, 0.0f, SecondMonster.Actor->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 20.0f), true);
	if (!TestNotNull(TEXT("Horizontal throw candidate exists"), Horizontal) || !TestNotNull(TEXT("Closer capsule-distance cat exists"), ClosestBody))
	{
		return false;
	}
	TickMonster(SecondMonster);
	TestTrue(TEXT("A second monster is thrown"), SecondMonster.AI->bIsRagdolled);
	TestEqual(TEXT("Throw candidates use capsule distance, not center distance"), SecondMonster.AI->GetThreateningCat(), ClosestBody->GetOwner());
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
	UProximityFloatSpinComponent* Cat = SpawnCat(Environment.World, FVector(-Capsule->GetScaledCapsuleRadius() - 300.0f, 0.0f, 100.0f), true);
	if (!TestNotNull(TEXT("Cat exists"), Cat) || !TestNotNull(TEXT("Floor exists"), SpawnCollisionBox(Environment.World, FVector(0.0f, 0.0f, -20.0f), FVector(5000.0f, 5000.0f, 20.0f))))
	{
		return false;
	}
	TickMonster(Monster);
	if (!TestTrue(TEXT("300 cm capsule-surface boundary enters ragdoll"), Monster.AI->bIsRagdolled))
	{
		return false;
	}
	TestFalse(TEXT("No navigation state is set after entering ragdoll"), Monster.AI->bIsFleeing);
	TestEqual(TEXT("Movement is disabled"), Monster.Actor->GetCharacterMovement()->MovementMode.GetValue(), MOVE_None);
	TestEqual(TEXT("Capsule is disabled"), Capsule->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
	StepPhysics(Environment.World);
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
	StepPhysics(Environment.World);
	TestTrue(TEXT("Coincident cat still gives a finite outward throw"), Mesh->GetPhysicsLinearVelocity(TEXT("Hips")).X > 1000.0f);
	Monster.Actor->Destroy();
	AdvanceRecovery(Environment.World, 4.0f); // EndPlay must cancel recovery callbacks.
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLostWithinMonsterPanicSpeedTest,
	"LostWithin.AI.MonsterCatReaction.PanicSpeed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FLostWithinMonsterPanicSpeedTest::RunTest(const FString& Parameters)
{
	FScopedMonsterTestWorld Environment;
	if (!TestNotNull(TEXT("World exists"), Environment.World))
	{
		return false;
	}
	const FMonsterSetup Monster = SpawnMonster(Environment.World, FVector::ZeroVector, false);
	if (!TestNotNull(TEXT("Monster exists"), Monster.Actor) || !TestNotNull(TEXT("AI exists"), Monster.AI))
	{
		return false;
	}
	UCharacterMovementComponent* Movement = Monster.Actor->GetCharacterMovement();
	USkeletalMeshComponent* Mesh = Monster.Actor->GetMesh();
	TestEqual(TEXT("The default panic multiplier is 1.5"), Monster.AI->PanicSpeedMultiplier, 1.5f);
	Monster.AI->CatRagdollRadius = 0.0f;
	Monster.AI->PanicGaitSpeed = 100.0f;
	Movement->MaxWalkSpeed = 60.0f;
	Mesh->GlobalAnimRateScale = 1.0f;
	AddExpectedError(TEXT("needs an AIController to flee"), EAutomationExpectedErrorFlags::Contains, 0);

	UProximityFloatSpinComponent* Cat = SpawnCat(Environment.World, FVector(400.0f, 0.0f, 0.0f), true);
	if (!TestNotNull(TEXT("Cat exists"), Cat))
	{
		return false;
	}
	TickMonster(Monster);
	TestTrue(TEXT("The monster flees the float-spinning cat"), Monster.AI->bIsFleeing);
	TestEqual(TEXT("Panic speed is 1.5x the panic gait speed"), Movement->MaxWalkSpeed, 150.0f);
	TestEqual(TEXT("The animation rate rises by the same factor"), Mesh->GlobalAnimRateScale, 1.5f);
	TickMonster(Monster);
	TestEqual(TEXT("Panic speed does not compound across ticks"), Movement->MaxWalkSpeed, 150.0f);
	TestEqual(TEXT("Panic animation rate does not compound across ticks"), Mesh->GlobalAnimRateScale, 1.5f);

	Cat->GetOwner()->Destroy();
	TickMonster(Monster);
	TestFalse(TEXT("Fleeing ends without a cat"), Monster.AI->bIsFleeing);
	TestEqual(TEXT("The normal speed is restored"), Movement->MaxWalkSpeed, 60.0f);
	TestEqual(TEXT("The normal animation rate is restored"), Mesh->GlobalAnimRateScale, 1.0f);

	Monster.AI->PanicGaitSpeed = 0.0f;
	UProximityFloatSpinComponent* Second = SpawnCat(Environment.World, FVector(-400.0f, 0.0f, 0.0f), true);
	if (!TestNotNull(TEXT("Second cat exists"), Second))
	{
		return false;
	}
	TickMonster(Monster);
	TestEqual(TEXT("Without a panic gait speed the current speed is scaled"), Movement->MaxWalkSpeed, 90.0f);
	Second->GetOwner()->Destroy();
	TickMonster(Monster);
	TestEqual(TEXT("The scaled speed is restored too"), Movement->MaxWalkSpeed, 60.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLostWithinMonsterRecoveryFallbackTest,
	"LostWithin.AI.MonsterCatReaction.RecoveryFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FLostWithinMonsterRecoveryFallbackTest::RunTest(const FString& Parameters)
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
	if (!TestNotNull(TEXT("Monster physics asset loaded"), Mesh->GetPhysicsAsset())
		|| !TestNotNull(TEXT("Floor exists"), SpawnCollisionBox(Environment.World, FVector(0.0f, 0.0f, -20.0f), FVector(300.0f, 300.0f, 20.0f))))
	{
		return false;
	}
	// A roaming monster walks; recovery must restore exactly that mode.
	Monster.Actor->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	const FVector ThrowOrigin = Monster.Actor->GetActorLocation();
	UProximityFloatSpinComponent* Cat = SpawnCat(Environment.World, FVector(-Capsule->GetScaledCapsuleRadius() - 200.0f, 0.0f, 100.0f), true);
	if (!TestNotNull(TEXT("Cat exists"), Cat))
	{
		return false;
	}
	TickMonster(Monster);
	if (!TestTrue(TEXT("The cat throws the monster"), Monster.AI->bIsRagdolled))
	{
		return false;
	}
	Cat->GetOwner()->Destroy();
	// Land the body far past the floor's edge, where no standing floor exists.
	Mesh->SetAllPhysicsLinearVelocity(FVector::ZeroVector);
	Mesh->SetWorldLocation(Mesh->GetComponentLocation() + FVector(3000.0f, 0.0f, 30.0f) - Mesh->GetSocketLocation(TEXT("Hips")),
		false, nullptr, ETeleportType::TeleportPhysics);
	AdvanceRecovery(Environment.World, 3.1f);
	TestTrue(TEXT("Without a floor the normal recovery keeps retrying"), Monster.AI->bIsRagdolled);
	for (int32 Step = 0; Step < 6; ++Step)
	{
		AdvanceRecovery(Environment.World, 0.26f);
	}
	TestTrue(TEXT("Retries continue until the fallback delay"), Monster.AI->bIsRagdolled);
	for (int32 Step = 0; Step < 4; ++Step)
	{
		AdvanceRecovery(Environment.World, 0.26f);
	}
	TestFalse(TEXT("The monster stands within recovery time + 2 s even with no landing floor"), Monster.AI->bIsRagdolled);
	TestTrue(TEXT("Without navigation it returns to where the throw started"), Monster.Actor->GetActorLocation().Equals(ThrowOrigin, 1.0));
	TestTrue(TEXT("The mesh is reattached to the capsule"), Mesh->GetAttachParent() == Capsule);
	TestEqual(TEXT("Walking movement is restored"), Monster.Actor->GetCharacterMovement()->MovementMode.GetValue(), MOVE_Walking);
	TickMonster(Monster);
	TestTrue(TEXT("Normal AI resumes after the fallback recovery"), UMonsterAIComponent::CanRunNormalAI(Monster.Actor));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
