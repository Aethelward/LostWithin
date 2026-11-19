// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/BoxComponent.h"
#include "Components/ProximityFloatSpinComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GameFramework/WorldSettings.h"
#include "Interaction/InteractableInterface.h"
#include "Interaction/PickupItemActor.h"
#include "Misc/AutomationTest.h"

namespace
{
	struct FScopedPathTestWorld
	{
		UWorld* World = nullptr;

		FScopedPathTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			if (World && GEngine)
			{
				FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
				Context.SetCurrentWorld(World);
				World->InitializeActorsForPlay(FURL());
				World->BeginPlay();
				if (AWorldSettings* WS = World->GetWorldSettings())
				{
					WS->NotifyBeginPlay();
				}
			}
		}

		~FScopedPathTestWorld()
		{
			if (World)
			{
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

	struct FCatTestSetup
	{
		AActor* Actor = nullptr;
		UProximityFloatSpinComponent* SpinComp = nullptr;
	};

	FCatTestSetup SpawnTestCat(
		UWorld* World,
		const FTransform& SpawnTransform,
		TFunctionRef<void(UProximityFloatSpinComponent*)> ConfigureFn)
	{
		FCatTestSetup Result;
		if (!World)
		{
			return Result;
		}

		AActor* CatActor = World->SpawnActorDeferred<AActor>(
			AActor::StaticClass(),
			SpawnTransform,
			nullptr,
			nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!CatActor)
		{
			return Result;
		}

		USceneComponent* Root = NewObject<USceneComponent>(CatActor, TEXT("TestRoot"));
		Root->SetMobility(EComponentMobility::Movable);
		CatActor->SetRootComponent(Root);
		Root->RegisterComponent();
		Root->SetWorldTransform(SpawnTransform);

		UProximityFloatSpinComponent* SpinComp = NewObject<UProximityFloatSpinComponent>(CatActor, TEXT("FloatSpinComp"));
		SpinComp->bAutoSetHierarchyMovable = false;
		SpinComp->bDarkenSkyOnSpinWindow = false;
		SpinComp->bEnableDiscoEnvironmentOnSpinWindow = false;
		SpinComp->bUseMorphVicinityCheck = false;
		SpinComp->InteractionSoundSearchPaths.Reset();
		// These path tests start the sequence by pet count; the pet-and-spin ritual has its own test.
		SpinComp->bRequirePetSpinSequence = false;
		ConfigureFn(SpinComp);
		SpinComp->RegisterComponent();

		CatActor->FinishSpawning(SpawnTransform);

		Result.Actor = CatActor;
		Result.SpinComp = SpinComp;
		return Result;
	}

	void StepActorAndComponent(const FCatTestSetup& Cat, float Dt, int32 Steps)
	{
		for (int32 Step = 0; Step < Steps; ++Step)
		{
			++GFrameCounter;
			if (Cat.Actor)
			{
				Cat.Actor->Tick(Dt);
			}
			if (Cat.SpinComp)
			{
				Cat.SpinComp->TickComponent(Dt, LEVELTICK_All, nullptr);
			}
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLostWithinProximityFloatSpinPathAutomationTest,
	"LostWithin.Components.ProximityFloatSpinPath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FLostWithinProximityFloatSpinPathAutomationTest::RunTest(const FString& Parameters)
{
	FScopedPathTestWorld TestEnv;
	if (!TestNotNull(TEXT("Test world created"), TestEnv.World))
	{
		return false;
	}

	// 1. Local-to-world conversion (rotated actor + non-unit scale), Move-Once completion, and Yaw facing
	{
		const FTransform InitialTransform(FRotator(0.0f, 90.0f, 0.0f), FVector(100.0f, 200.0f, 0.0f), FVector(2.0f, 2.0f, 2.0f));
		const FCatTestSetup Cat = SpawnTestCat(TestEnv.World, InitialTransform, [](UProximityFloatSpinComponent* Comp)
		{
			Comp->bEnableDirectedPath = true;
			Comp->DirectedPathTriggerMode = EDirectedPathTriggerMode::OnBeginPlay;
			Comp->PathMoveSpeed = 200.0f;
			Comp->WaypointAcceptanceRadius = 5.0f;
			Comp->bFacePathDirection = true;
			Comp->PathTurnSpeed = 25.0f;
			// Local +X 100 cm at actor Yaw=90 maps to World (100, 300, 0); local (100, 100, 50) maps to World (0, 300, 50)
			Comp->PathWaypoints = {
				FVector(100.0f, 0.0f, 0.0f),
				FVector(100.0f, 100.0f, 50.0f)
			};
		});

		if (!TestNotNull(TEXT("BeginPlay path cat spawned"), Cat.Actor) || !TestNotNull(TEXT("SpinComp created"), Cat.SpinComp))
		{
			return false;
		}

		TestTrue(TEXT("OnBeginPlay starts path automatically"), Cat.SpinComp->IsFollowingDirectedPath());
		TestEqual(TEXT("Initial target waypoint index is 0"), Cat.SpinComp->GetCurrentWaypointIndex(), 0);

		// Step enough time to move 100cm + ~112cm at 200cm/s (~1.1s), including rest frames afterwards
		StepActorAndComponent(Cat, 0.02f, 80);

		TestFalse(TEXT("Path stops after completing Move-Once waypoints"), Cat.SpinComp->IsFollowingDirectedPath());
		TestTrue(TEXT("HasCompletedDirectedPath returns true at end"), Cat.SpinComp->HasCompletedDirectedPath());
		TestEqual(TEXT("Waypoint index advanced past last element"), Cat.SpinComp->GetCurrentWaypointIndex(), 2);

		const FVector FinalLoc = Cat.Actor->GetActorLocation();
		TestTrue(
			FString::Printf(TEXT("Cat arrived at expected rotated unscaled world waypoint (0, 300, 50), got %s"), *FinalLoc.ToString()),
			FinalLoc.Equals(FVector(0.0f, 300.0f, 50.0f), 1.0f));

		const float FinalYaw = FRotator::NormalizeAxis(Cat.Actor->GetActorRotation().Yaw);
		TestTrue(
			FString::Printf(TEXT("Cat turned to face second segment (-X -> Yaw ~ +/-180 deg), got %.1f"), FinalYaw),
			FMath::Abs(FinalYaw) > 160.0f);

		// Verify Move-Once remains stopped on subsequent ticks
		StepActorAndComponent(Cat, 0.02f, 20);
		TestTrue(TEXT("Cat remains stationary after path completion"), Cat.Actor->GetActorLocation().Equals(FVector(0.0f, 300.0f, 50.0f), 0.1f));

		Cat.Actor->Destroy();
	}

	// 2. Manual mode + Pause/Resume/Stop + StartDirectedPathWithWaypoints + MoveToDirectedLocation
	{
		const FTransform SpawnTx(FRotator::ZeroRotator, FVector(0.0f, 0.0f, 0.0f));
		const FCatTestSetup Cat = SpawnTestCat(TestEnv.World, SpawnTx, [](UProximityFloatSpinComponent* Comp)
		{
			Comp->bEnableDirectedPath = false;
			Comp->DirectedPathTriggerMode = EDirectedPathTriggerMode::Manual;
			Comp->PathMoveSpeed = 100.0f;
			Comp->PathWaypoints = { FVector(200.0f, 0.0f, 0.0f) };
		});

		StepActorAndComponent(Cat, 0.02f, 10);
		TestFalse(TEXT("Manual cat does not move before StartDirectedPath"), Cat.SpinComp->IsFollowingDirectedPath());
		TestTrue(TEXT("Manual cat stays at origin"), Cat.Actor->GetActorLocation().Equals(FVector::ZeroVector, 0.1f));

		Cat.SpinComp->StartDirectedPath(true);
		TestTrue(TEXT("StartDirectedPath begins movement even in Manual mode"), Cat.SpinComp->IsFollowingDirectedPath());
		StepActorAndComponent(Cat, 0.05f, 10); // 0.5s -> ~50cm
		const float MidX = Cat.Actor->GetActorLocation().X;
		TestTrue(FString::Printf(TEXT("Cat moved ~50cm along +X, got %.1f"), MidX), FMath::IsNearlyEqual(MidX, 50.0f, 3.0f));

		Cat.SpinComp->PauseDirectedPath();
		TestFalse(TEXT("PauseDirectedPath pauses movement"), Cat.SpinComp->IsFollowingDirectedPath());
		StepActorAndComponent(Cat, 0.05f, 10);
		TestTrue(TEXT("Cat does not advance while paused"), FMath::IsNearlyEqual(Cat.Actor->GetActorLocation().X, MidX, 0.1f));

		Cat.SpinComp->ResumeDirectedPath();
		TestTrue(TEXT("ResumeDirectedPath resumes movement"), Cat.SpinComp->IsFollowingDirectedPath());
		StepActorAndComponent(Cat, 0.05f, 40); // finishes remaining 150cm -> arrives at 200cm
		TestTrue(TEXT("Resumed path finishes at destination (200, 0, 0)"), Cat.Actor->GetActorLocation().Equals(FVector(200.0f, 0.0f, 0.0f), 1.0f));

		// Imperative MoveToDirectedLocation overrides to a new world coordinate
		Cat.SpinComp->MoveToDirectedLocation(FVector(200.0f, 150.0f, 0.0f));
		TestTrue(TEXT("MoveToDirectedLocation activates movement"), Cat.SpinComp->IsFollowingDirectedPath());
		StepActorAndComponent(Cat, 0.05f, 40);
		TestTrue(TEXT("MoveToDirectedLocation reaches (200, 150, 0)"), Cat.Actor->GetActorLocation().Equals(FVector(200.0f, 150.0f, 0.0f), 1.0f));

		Cat.Actor->Destroy();
	}

	// 3. OnFirstInteract trigger mode
	{
		const FCatTestSetup Cat = SpawnTestCat(TestEnv.World, FTransform(FVector(0.0f, 0.0f, 0.0f)), [](UProximityFloatSpinComponent* Comp)
		{
			Comp->bEnableDirectedPath = true;
			Comp->DirectedPathTriggerMode = EDirectedPathTriggerMode::OnFirstInteract;
			Comp->ItemDropInteractionThreshold = 5;
			Comp->SpinTriggerInteractionThreshold = 20;
			Comp->PathMoveSpeed = 100.0f;
			Comp->PathWaypoints = { FVector(100.0f, 0.0f, 0.0f) };
		});

		StepActorAndComponent(Cat, 0.02f, 5);
		TestFalse(TEXT("OnFirstInteract cat waits before interaction"), Cat.SpinComp->IsFollowingDirectedPath());

		IInteractableInterface::Execute_OnInteract(Cat.SpinComp, Cat.Actor);
		TestTrue(TEXT("OnFirstInteract starts path immediately after 1st interact"), Cat.SpinComp->IsFollowingDirectedPath());
		StepActorAndComponent(Cat, 0.05f, 25);
		TestTrue(TEXT("OnFirstInteract completes target waypoint"), Cat.Actor->GetActorLocation().Equals(FVector(100.0f, 0.0f, 0.0f), 1.0f));

		Cat.Actor->Destroy();
	}

	// 4. OnItemDropped trigger mode
	{
		const FCatTestSetup Cat = SpawnTestCat(TestEnv.World, FTransform(FVector(0.0f, 0.0f, 0.0f)), [](UProximityFloatSpinComponent* Comp)
		{
			Comp->bEnableDirectedPath = true;
			Comp->DirectedPathTriggerMode = EDirectedPathTriggerMode::OnItemDropped;
			Comp->ItemDropInteractionThreshold = 3;
			Comp->SpinTriggerInteractionThreshold = 20;
			Comp->PathMoveSpeed = 120.0f;
			Comp->PathWaypoints = { FVector(120.0f, 0.0f, 0.0f) };
		});

		IInteractableInterface::Execute_OnInteract(Cat.SpinComp, Cat.Actor);
		IInteractableInterface::Execute_OnInteract(Cat.SpinComp, Cat.Actor);
		TestFalse(TEXT("OnItemDropped waits until ItemDropInteractionThreshold (3)"), Cat.SpinComp->IsFollowingDirectedPath());

		IInteractableInterface::Execute_OnInteract(Cat.SpinComp, Cat.Actor);
		TestTrue(TEXT("OnItemDropped starts path on 3rd interact"), Cat.SpinComp->IsFollowingDirectedPath());
		StepActorAndComponent(Cat, 0.05f, 25);
		TestTrue(TEXT("OnItemDropped completes target waypoint"), Cat.Actor->GetActorLocation().Equals(FVector(120.0f, 0.0f, 0.0f), 1.0f));

		for (TActorIterator<APickupItemActor> It(TestEnv.World); It; ++It)
		{
			if (IsValid(*It))
			{
				(*It)->Destroy();
			}
		}
		Cat.Actor->Destroy();
	}

	// 5. OnSpinTriggered + active Float/Spin composition (moving BaseLocation while hovering/spinning)
	{
		const FCatTestSetup Cat = SpawnTestCat(TestEnv.World, FTransform(FVector(0.0f, 0.0f, 0.0f)), [](UProximityFloatSpinComponent* Comp)
		{
			Comp->bEnableDirectedPath = true;
			Comp->DirectedPathTriggerMode = EDirectedPathTriggerMode::OnSpinTriggered;
			Comp->SpinTriggerInteractionThreshold = 1;
			Comp->bJumpBeforeSpin = false;
			Comp->bUseSpinTimeWindows = false;
			Comp->FloatHeight = 80.0f;
			Comp->BobAmplitude = 0.0f;
			Comp->SpinSpeed = 360.0f;
			Comp->bPausePathWhileSpinning = false;
			Comp->PathMoveSpeed = 100.0f;
			Comp->bFacePathDirection = false;
			Comp->PathWaypoints = { FVector(200.0f, 0.0f, 0.0f) };
		});

		TestFalse(TEXT("OnSpinTriggered waits until spin begins"), Cat.SpinComp->IsFollowingDirectedPath());
		IInteractableInterface::Execute_OnInteract(Cat.SpinComp, Cat.Actor);
		TestTrue(TEXT("Cat entered spin sequence"), Cat.SpinComp->IsFloatSpinActive());
		TestTrue(TEXT("OnSpinTriggered started path"), Cat.SpinComp->IsFollowingDirectedPath());

		StepActorAndComponent(Cat, 0.02f, 50); // 1.0s -> ~100cm X, floating toward 80cm Z, spinning yaw
		const FVector MidPos = Cat.Actor->GetActorLocation();
		TestTrue(FString::Printf(TEXT("Cat moved along X (~100cm) while floating/spinning, got %.1f"), MidPos.X),
			FMath::IsNearlyEqual(MidPos.X, 100.0f, 3.0f));
		TestTrue(FString::Printf(TEXT("Cat Z hovers upward above BaseLocation while moving, got %.1f"), MidPos.Z),
			MidPos.Z > 40.0f);

		Cat.Actor->Destroy();
	}

	// 6. Ground-trace snapping aligns BaseLocation.Z to floor surface and finishes horizontally
	{
		AActor* FloorActor = TestEnv.World->SpawnActor<AActor>(AActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
		UBoxComponent* FloorBox = NewObject<UBoxComponent>(FloorActor, TEXT("FloorBox"));
		FloorBox->SetBoxExtent(FVector(400.0f, 400.0f, 10.0f)); // Top surface at Z = 40.0f once placed at Z = 30.0f
		FloorBox->SetCollisionObjectType(ECC_WorldStatic);
		FloorBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		FloorBox->SetCollisionResponseToAllChannels(ECR_Block);
		FloorActor->SetRootComponent(FloorBox);
		FloorBox->RegisterComponent();
		FloorBox->SetWorldLocation(FVector(150.0f, 0.0f, 30.0f));

		const FCatTestSetup Cat = SpawnTestCat(TestEnv.World, FTransform(FVector(0.0f, 0.0f, 40.0f)), [](UProximityFloatSpinComponent* Comp)
		{
			Comp->bEnableDirectedPath = true;
			Comp->DirectedPathTriggerMode = EDirectedPathTriggerMode::OnBeginPlay;
			Comp->bSnapPathToGround = true;
			Comp->GroundTraceChannel = ECC_WorldStatic;
			Comp->GroundTraceHalfHeight = 200.0f;
			Comp->GroundHeightOffset = 0.0f;
			Comp->PathMoveSpeed = 150.0f;
			Comp->WaypointAcceptanceRadius = 5.0f;
			// Intentionally offset Z by -35cm below floor to verify horizontal completion + Z snapping
			Comp->PathWaypoints = { FVector(150.0f, 0.0f, -35.0f) };
		});

		StepActorAndComponent(Cat, 0.02f, 60); // 1.2s -> 150cm at 150cm/s
		TestTrue(TEXT("Ground-snapped path finishes horizontally despite waypoint Z mismatch"), Cat.SpinComp->HasCompletedDirectedPath());
		const FVector GroundFinal = Cat.Actor->GetActorLocation();
		TestTrue(
			FString::Printf(TEXT("Ground-snapped cat landed at (150, 0, 40), got %s"), *GroundFinal.ToString()),
			GroundFinal.Equals(FVector(150.0f, 0.0f, 40.0f), 1.5f));

		Cat.Actor->Destroy();
		FloorActor->Destroy();
	}

	// 7. bPausePathWhileSpinning pauses path progression during active spin windows
	{
		const FCatTestSetup Cat = SpawnTestCat(TestEnv.World, FTransform(FVector(0.0f, 0.0f, 0.0f)), [](UProximityFloatSpinComponent* Comp)
		{
			Comp->bEnableDirectedPath = true;
			Comp->DirectedPathTriggerMode = EDirectedPathTriggerMode::OnBeginPlay;
			Comp->bPausePathWhileSpinning = true;
			Comp->bJumpBeforeSpin = false;
			Comp->bUseSpinTimeWindows = false;
			Comp->SpinTriggerInteractionThreshold = 1;
			Comp->FloatHeight = 60.0f;
			Comp->BobAmplitude = 0.0f;
			Comp->PathMoveSpeed = 100.0f;
			Comp->bFacePathDirection = false;
			Comp->PathWaypoints = { FVector(300.0f, 0.0f, 0.0f) };
		});

		StepActorAndComponent(Cat, 0.02f, 25); // 0.5s -> ~50cm X
		const float BeforeSpinX = Cat.Actor->GetActorLocation().X;
		TestTrue(TEXT("Cat moved along path before spin started"), FMath::IsNearlyEqual(BeforeSpinX, 50.0f, 3.0f));

		IInteractableInterface::Execute_OnInteract(Cat.SpinComp, Cat.Actor);
		TestTrue(TEXT("Cat entered spin state"), Cat.SpinComp->IsFloatSpinActive());
		StepActorAndComponent(Cat, 0.02f, 25); // 0.5s while spinning -> X should hold while Z rises
		const FVector DuringSpinPos = Cat.Actor->GetActorLocation();
		TestTrue(
			FString::Printf(TEXT("bPausePathWhileSpinning keeps X at %.1f during spin (got %.1f)"), BeforeSpinX, DuringSpinPos.X),
			FMath::IsNearlyEqual(DuringSpinPos.X, BeforeSpinX, 0.5f));
		TestTrue(TEXT("Cat still hovers upward while path is paused for spin"), DuringSpinPos.Z > 20.0f);

		Cat.Actor->Destroy();
	}

	// 8. OnSpinWindow (default trigger mode) triggers once at 3rd spin window start (6.06s) and does not retrigger
	{
		const FCatTestSetup Cat = SpawnTestCat(TestEnv.World, FTransform(FVector(0.0f, 0.0f, 0.0f)), [this](UProximityFloatSpinComponent* Comp)
		{
			TestEqual(TEXT("Default DirectedPathTriggerMode is OnSpinWindow"), Comp->DirectedPathTriggerMode, EDirectedPathTriggerMode::OnSpinWindow);
			TestEqual(TEXT("Default DirectedPathSpinWindowIndex is 2 (3rd spin window)"), Comp->DirectedPathSpinWindowIndex, 2);

			Comp->bEnableDirectedPath = true;
			Comp->SpinTriggerInteractionThreshold = 1;
			Comp->bJumpBeforeSpin = false;
			Comp->FloatHeight = 60.0f;
			Comp->BobAmplitude = 0.0f;
			Comp->bPausePathWhileSpinning = false;
			Comp->bFacePathDirection = false;
			Comp->PathMoveSpeed = 120.0f;
			Comp->WaypointAcceptanceRadius = 5.0f;
			Comp->PathWaypoints = { FVector(120.0f, 0.0f, 0.0f) };
		});

		if (!TestNotNull(TEXT("OnSpinWindow default cat spawned"), Cat.Actor) || !TestNotNull(TEXT("OnSpinWindow SpinComp created"), Cat.SpinComp))
		{
			return false;
		}

		TestFalse(TEXT("OnSpinWindow does not start before interaction"), Cat.SpinComp->IsFollowingDirectedPath());

		IInteractableInterface::Execute_OnInteract(Cat.SpinComp, Cat.Actor);
		TestTrue(TEXT("Cat entered float/spin sequence"), Cat.SpinComp->IsFloatSpinActive());
		TestFalse(TEXT("OnSpinWindow waits past initial trigger before 3rd spin"), Cat.SpinComp->IsFollowingDirectedPath());

		// Advance 6.00s across 1st spin window (0.27-1.77s) and 2nd spin window (3.12-5.10s), stopping before 6.06s
		StepActorAndComponent(Cat, 0.02f, 300);
		TestFalse(TEXT("OnSpinWindow does not trigger during 1st or 2nd spin window up to 6.00s"), Cat.SpinComp->IsFollowingDirectedPath());
		TestTrue(
			FString::Printf(TEXT("Cat stays at origin XY prior to 3rd spin window, got %s"), *Cat.Actor->GetActorLocation().ToString()),
			FMath::IsNearlyZero(Cat.Actor->GetActorLocation().X, 0.5f) && FMath::IsNearlyZero(Cat.Actor->GetActorLocation().Y, 0.5f));

		// Cross 6.06s (to 6.10s) -> 3rd spin window starts and triggers the directed path
		StepActorAndComponent(Cat, 0.02f, 5);
		TestTrue(TEXT("OnSpinWindow triggers directed path once ElapsedTriggeredTime reaches 6.06s"), Cat.SpinComp->IsFollowingDirectedPath());

		// Advance 1.30s so the 120cm waypoint completes at 120cm/s
		StepActorAndComponent(Cat, 0.02f, 65);
		TestFalse(TEXT("OnSpinWindow stops once waypoint is completed"), Cat.SpinComp->IsFollowingDirectedPath());
		TestTrue(TEXT("OnSpinWindow marks directed path completed"), Cat.SpinComp->HasCompletedDirectedPath());
		TestTrue(
			FString::Printf(TEXT("Cat reached X=120 waypoint after 3rd spin start, got %.1f"), Cat.Actor->GetActorLocation().X),
			FMath::IsNearlyEqual(Cat.Actor->GetActorLocation().X, 120.0f, 1.5f));

		// Reset completion flag while still in the 3rd spin window to verify automatic triggering fires strictly once
		Cat.SpinComp->StopDirectedPath(/*bResetWaypointIndex=*/true);
		StepActorAndComponent(Cat, 0.02f, 10);
		TestFalse(TEXT("OnSpinWindow does not auto-retrigger after firing once"), Cat.SpinComp->IsFollowingDirectedPath());
		TestTrue(
			FString::Printf(TEXT("Cat stays at X=120 without retriggering, got %.1f"), Cat.Actor->GetActorLocation().X),
			FMath::IsNearlyEqual(Cat.Actor->GetActorLocation().X, 120.0f, 0.5f));

		Cat.Actor->Destroy();
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

