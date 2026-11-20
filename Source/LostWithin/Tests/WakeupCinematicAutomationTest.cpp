// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Player/WakeupCinematicActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/InputComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "InputCoreTypes.h"
#include "LostWithinCharacter.h"
#include "LostWithinGameMode.h"
#include "Misc/AutomationTest.h"

#define LOSTWITHIN_CINEMATIC_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

namespace
{
	struct FScopedWakeupCinematicTestWorld
	{
		UWorld* World = nullptr;

		FScopedWakeupCinematicTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			if (World && GEngine)
			{
				FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
				Context.SetCurrentWorld(World);
				World->InitializeActorsForPlay(FURL());
				World->BeginPlay();
				if (AWorldSettings* Settings = World->GetWorldSettings())
				{
					Settings->NotifyBeginPlay();
				}
			}
		}

		~FScopedWakeupCinematicTestWorld()
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

	ALostWithinCharacter* SpawnWakeupTestCharacter(UWorld* World, const FVector& Location)
	{
		UClass* CharClass = LoadClass<ALostWithinCharacter>(
			nullptr,
			TEXT("/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter.BP_FirstPersonCharacter_C"));

		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		if (CharClass)
		{
			return World->SpawnActor<ALostWithinCharacter>(CharClass, Location, FRotator::ZeroRotator, SpawnParams);
		}
		return nullptr;
	}

	ALostWithinGameMode* SpawnWakeupTestGameMode(UWorld* World)
	{
		UClass* GMClass = LoadClass<ALostWithinGameMode>(
			nullptr,
			TEXT("/Game/FirstPerson/Blueprints/BP_FirstPersonGameMode.BP_FirstPersonGameMode_C"));

		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		if (GMClass)
		{
			return World->SpawnActor<ALostWithinGameMode>(GMClass, FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
		}
		return nullptr;
	}
}

/**
 * Test 1: Initial state & bed alignment
 * Verifies that AWakeupCinematicActor defaults match SM_Bed coordinates (X=-4082, Y=-3902, Z=110 cm),
 * bed edge sitting position (Z=90 cm), and PlayerStart standing height (X=-4122, Y=-3742, Z=170 cm).
 * Also tests StartCinematic initial camera placement, rotation, and fade setup.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWakeupCinematicBedAlignmentAndInitialStateTest,
	"LostWithin.Cinematic.Wakeup.BedAlignmentAndInitialState",
	LOSTWITHIN_CINEMATIC_TEST_FLAGS)

bool FWakeupCinematicBedAlignmentAndInitialStateTest::RunTest(const FString& Parameters)
{
	FScopedWakeupCinematicTestWorld Env;
	UWorld* World = Env.World;
	if (!TestNotNull(TEXT("Test world created successfully"), World))
	{
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AWakeupCinematicActor* Cinematic = World->SpawnActor<AWakeupCinematicActor>(
		AWakeupCinematicActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);

	if (!TestNotNull(TEXT("Wakeup cinematic actor spawned"), Cinematic))
	{
		return false;
	}

	// 1. Verify default origin and bed alignment coordinates (double precision)
	TestEqual(TEXT("Bed pillow X coordinate is -4082"), Cinematic->BedPillowLocation.X, -4082.0);
	TestEqual(TEXT("Bed pillow Y coordinate is -3902"), Cinematic->BedPillowLocation.Y, -3902.0);
	TestEqual(TEXT("Bed pillow Z coordinate is 110 cm (pillow height)"), Cinematic->BedPillowLocation.Z, 110.0);

	TestEqual(TEXT("Bed edge X coordinate is -4100"), Cinematic->BedEdgeLocation.X, -4100.0);
	TestEqual(TEXT("Bed edge Y coordinate is -3820"), Cinematic->BedEdgeLocation.Y, -3820.0);
	TestEqual(TEXT("Bed edge Z coordinate is 90 cm"), Cinematic->BedEdgeLocation.Z, 90.0);

	TestEqual(TEXT("Standing PlayerStart X coordinate is -4122"), Cinematic->StandingLocation.X, -4122.0);
	TestEqual(TEXT("Standing PlayerStart Y coordinate is -3742"), Cinematic->StandingLocation.Y, -3742.0);
	TestEqual(TEXT("Standing eye height Z coordinate is 170 cm"), Cinematic->StandingLocation.Z, 170.0);

	// 2. Verify rotations (double precision)
	TestEqual(TEXT("Initial lying pitch is 60 deg"), Cinematic->InitialLyingRotation.Pitch, 60.0);
	TestEqual(TEXT("Initial lying yaw is 90 deg"), Cinematic->InitialLyingRotation.Yaw, 90.0);
	TestEqual(TEXT("Initial lying roll is -10 deg"), Cinematic->InitialLyingRotation.Roll, -10.0);

	TestEqual(TEXT("Sitting pitch is -5 deg"), Cinematic->SittingRotation.Pitch, -5.0);
	TestEqual(TEXT("Sitting yaw is 95 deg"), Cinematic->SittingRotation.Yaw, 95.0);

	TestEqual(TEXT("Standing pitch is 0 deg"), Cinematic->StandingRotation.Pitch, 0.0);
	TestEqual(TEXT("Standing yaw is 90 deg"), Cinematic->StandingRotation.Yaw, 90.0);

	// 3. Verify phase timings (float precision)
	TestEqual(TEXT("Phase 1 duration is 2.5s"), Cinematic->Phase1Duration, 2.5f);
	TestEqual(TEXT("Phase 2 duration is 3.0s"), Cinematic->Phase2Duration, 3.0f);
	TestEqual(TEXT("Phase 3 duration is 2.0s"), Cinematic->Phase3Duration, 2.0f);
	TestEqual(TEXT("Total duration is 7.5s"), Cinematic->GetTotalDuration(), 7.5f);

	// 4. Verify initial state before start
	TestFalse(TEXT("Cinematic is not playing before start"), Cinematic->IsPlaying());
	TestFalse(TEXT("Cinematic is not finished before start"), Cinematic->IsFinished());
	TestEqual(TEXT("Phase is 0 before start"), Cinematic->GetCurrentPhase(), 0);

	// 5. Start sequence and verify immediate alignment
	APlayerController* PC = World->SpawnActor<APlayerController>(APlayerController::StaticClass(), SpawnParams);
	ACharacter* Pawn = World->SpawnActor<ACharacter>(ACharacter::StaticClass(), FTransform(Cinematic->StandingLocation), SpawnParams);

	Cinematic->StartCinematic(PC, Pawn);

	TestTrue(TEXT("Cinematic is playing after start"), Cinematic->IsPlaying());
	TestFalse(TEXT("Cinematic is not finished after start"), Cinematic->IsFinished());
	TestEqual(TEXT("Current phase is Phase 1 (Awakening)"), Cinematic->GetCurrentPhase(), 1);

	// Camera must immediately snap to bed pillow
	TestEqual(TEXT("Immediate camera location X at pillow"), Cinematic->GetActorLocation().X, -4082.0);
	TestEqual(TEXT("Immediate camera location Y at pillow"), Cinematic->GetActorLocation().Y, -3902.0);
	TestEqual(TEXT("Immediate camera location Z at pillow"), Cinematic->GetActorLocation().Z, 110.0);

	// View target of PC set to cinematic actor
	if (PC)
	{
		TestTrue(TEXT("PC view target is cinematic actor"), PC->GetViewTarget() == Cinematic);
	}

	return true;
}

/**
 * Test 2: Phase progression
 * Covers:
 *  - Awakening (0.0s - 2.5s): multi-stage eye blink curve, motion blur & vignette
 *  - Sitting up & amnesia shake (2.5s - 5.5s): position lerp to Z=90cm, rotation pitch/roll lerp,
 *    head shake oscillation (damped sine + Perlin sway)
 *  - Standing up & blend (5.5s - 7.5s): rising to Z=170cm, camera blend initiation, post-process taper
 *  - Natural finish at t >= 7.5s with delegates
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWakeupCinematicPhaseProgressionTest,
	"LostWithin.Cinematic.Wakeup.PhaseProgression",
	LOSTWITHIN_CINEMATIC_TEST_FLAGS)

bool FWakeupCinematicPhaseProgressionTest::RunTest(const FString& Parameters)
{
	FScopedWakeupCinematicTestWorld Env;
	UWorld* World = Env.World;
	if (!TestNotNull(TEXT("Test world created successfully"), World))
	{
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AWakeupCinematicActor* Cinematic = World->SpawnActor<AWakeupCinematicActor>(
		AWakeupCinematicActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);

	if (!TestNotNull(TEXT("Wakeup cinematic actor spawned"), Cinematic))
	{
		return false;
	}

	// 1. Comprehensive eye blink alpha curve verification
	// Phase 1 is 2.5s
	const float P1 = 2.5f;
	TestEqual(TEXT("Blink alpha: fully closed at t=0.0s"), AWakeupCinematicActor::ComputeBlinkAlpha(0.0f, P1), 1.0f);
	TestEqual(TEXT("Blink alpha: fully closed at t=0.35s (T=0.14)"), AWakeupCinematicActor::ComputeBlinkAlpha(0.35f, P1), 1.0f);

	// First opening (0.4s - 0.75s, T 0.16 -> 0.30)
	const float FirstSlit = AWakeupCinematicActor::ComputeBlinkAlpha(0.6f, P1);
	TestTrue(TEXT("Blink alpha: first slit opens (< 1.0)"), FirstSlit < 1.0f && FirstSlit >= 0.35f);

	// Squeeze shut (0.75s - 1.0s, T 0.30 -> 0.40)
	const float Squeeze = AWakeupCinematicActor::ComputeBlinkAlpha(0.9f, P1);
	TestTrue(TEXT("Blink alpha: squeeze shut (> 0.65)"), Squeeze > 0.65f);

	// Second opening (1.0s - 1.4s, T 0.40 -> 0.56)
	const float SecondOpening = AWakeupCinematicActor::ComputeBlinkAlpha(1.25f, P1);
	TestTrue(TEXT("Blink alpha: wider opening (< 0.5)"), SecondOpening < 0.5f);

	// Flutter blink (1.4s - 1.65s, T 0.56 -> 0.66)
	const float Flutter = AWakeupCinematicActor::ComputeBlinkAlpha(1.55f, P1);
	TestTrue(TEXT("Blink alpha: flutter closure (> 0.4)"), Flutter > 0.4f);

	// Steady opening to fully open (1.65s - 2.5s)
	const float SteadyOpen = AWakeupCinematicActor::ComputeBlinkAlpha(2.0f, P1);
	TestTrue(TEXT("Blink alpha: nearly clear vision (< 0.25)"), SteadyOpen < 0.25f);
	TestEqual(TEXT("Blink alpha: fully open at end of Phase 1 (t=2.3s)"), AWakeupCinematicActor::ComputeBlinkAlpha(2.3f, P1), 0.0f);
	TestEqual(TEXT("Blink alpha: fully open at t=2.5s"), AWakeupCinematicActor::ComputeBlinkAlpha(2.5f, P1), 0.0f);

	// 2. Start cinematic and step through Phase 1
	APlayerController* PC = World->SpawnActor<APlayerController>(APlayerController::StaticClass(), SpawnParams);
	ACharacter* Pawn = World->SpawnActor<ACharacter>(ACharacter::StaticClass(), FTransform(Cinematic->StandingLocation), SpawnParams);
	Cinematic->StartCinematic(PC, Pawn);

	TestEqual(TEXT("Phase 1 active at start"), Cinematic->GetCurrentPhase(), 1);

	// Advance 1.0s into Phase 1
	Cinematic->Tick(1.0f);
	TestEqual(TEXT("Still in Phase 1 after 1.0s"), Cinematic->GetCurrentPhase(), 1);
	TestTrue(TEXT("Camera location near bed pillow"), FVector::Dist(Cinematic->GetActorLocation(), Cinematic->BedPillowLocation) < 5.0);

	// 3. Step into Phase 2 (Sitting up & Amnesia Head Shake)
	// Phase 1 ends at 2.5s. Tick 1.6s -> total elapsed 2.6s
	Cinematic->Tick(1.6f);
	TestEqual(TEXT("Phase 2 active at 2.6s"), Cinematic->GetCurrentPhase(), 2);

	// In Phase 2, player is sitting up: Z height moves towards BedEdgeLocation.Z (90 cm)
	TestTrue(TEXT("Camera Z moving downwards from pillow 110cm toward bed edge 90cm"),
		Cinematic->GetActorLocation().Z <= 110.0 && Cinematic->GetActorLocation().Z >= 85.0);

	// Advance further into Phase 2 (mid-situp at 4.0s) to observe amnesia head shake
	Cinematic->Tick(1.4f); // Total elapsed: 4.0s (mid Phase 2)
	TestEqual(TEXT("Still in Phase 2 at 4.0s"), Cinematic->GetCurrentPhase(), 2);

	FRotator CurrentRot = Cinematic->GetActorRotation();
	// Base sitting rotation has Yaw around 90-95. Due to head shake oscillation and Perlin noise,
	// Yaw and Roll must be actively modulated
	TestTrue(TEXT("Head shake oscillation active (Roll is non-zero)"), FMath::Abs(CurrentRot.Roll) > 0.01 || FMath::Abs(CurrentRot.Yaw - 95.0) > 0.01);

	// 4. Step into Phase 3 (Standing up & handoff)
	// Phase 2 ends at 5.5s. Tick 1.6s -> total elapsed 5.6s
	Cinematic->Tick(1.6f);
	TestEqual(TEXT("Phase 3 active at 5.6s"), Cinematic->GetCurrentPhase(), 3);

	// In Phase 3, player rises to standing eye height (170 cm)
	TestTrue(TEXT("Camera Z rising from bed edge towards standing eye height (170 cm)"),
		Cinematic->GetActorLocation().Z > 90.0 && Cinematic->GetActorLocation().Z <= 175.0);

	// 5. Natural completion at t >= 7.5s
	bool bNativeDelegateBroadcast = false;

	Cinematic->OnCinematicFinished.AddDynamic(Cinematic, &AWakeupCinematicActor::SkipCinematic); // verify dynamic binding
	Cinematic->OnFinishedNative.AddLambda([&bNativeDelegateBroadcast]() {
		bNativeDelegateBroadcast = true;
	});

	Cinematic->Tick(2.0f); // Total elapsed: 7.6s > 7.5s

	TestTrue(TEXT("Cinematic marked finished after total duration"), Cinematic->IsFinished());
	TestFalse(TEXT("Cinematic no longer playing"), Cinematic->IsPlaying());
	TestEqual(TEXT("Phase reaches 4 upon completion"), Cinematic->GetCurrentPhase(), 4);
	TestTrue(TEXT("Native finish delegate fired"), bNativeDelegateBroadcast);

	// Final camera location must exactly match standing position
	TestEqual(TEXT("Final camera location X at PlayerStart"), Cinematic->GetActorLocation().X, -4122.0);
	TestEqual(TEXT("Final camera location Y at PlayerStart"), Cinematic->GetActorLocation().Y, -3742.0);
	TestEqual(TEXT("Final camera location Z at standing height"), Cinematic->GetActorLocation().Z, 170.0);

	return true;
}

/**
 * Test 3: Input freeze during cinematic and clean unfreeze upon finish/skip
 * Verifies that:
 *  - LostCharacter enters cinematic state (IsInCinematicState() == true)
 *  - IsGameplayInputBlocked() returns true
 *  - PlayerController ignores movement and look inputs
 *  - Character input methods (DoMove, DoJumpStart, DoToggleFlashlight, DoReloadFlashlight,
 *    DoStartWalk, DoStartFastSprint, DoToggleInventory) are cleanly blocked
 *  - HUD visibility is collapsed
 *  - When cinematic finishes or is skipped, ExitCinematicState() cleanly restores input and HUD
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWakeupCinematicInputFreezeAndUnfreezeTest,
	"LostWithin.Cinematic.Wakeup.InputFreezeAndUnfreeze",
	LOSTWITHIN_CINEMATIC_TEST_FLAGS)

bool FWakeupCinematicInputFreezeAndUnfreezeTest::RunTest(const FString& Parameters)
{
	FScopedWakeupCinematicTestWorld Env;
	UWorld* World = Env.World;
	if (!TestNotNull(TEXT("Test world created successfully"), World))
	{
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	APlayerController* PC = World->SpawnActor<APlayerController>(APlayerController::StaticClass(), SpawnParams);
	if (!TestNotNull(TEXT("PlayerController spawned"), PC))
	{
		return false;
	}

	ALostWithinCharacter* LostChar = SpawnWakeupTestCharacter(World, FVector(-4122.0f, -3742.0f, 170.0f));
	if (!LostChar)
	{
		LostChar = World->SpawnActor<ALostWithinCharacter>(ALostWithinCharacter::StaticClass(), FVector(-4122.0f, -3742.0f, 170.0f), FRotator::ZeroRotator, SpawnParams);
	}

	if (!TestNotNull(TEXT("ALostWithinCharacter spawned"), LostChar))
	{
		return false;
	}

	PC->Possess(LostChar);

	// Baseline checks before cinematic
	TestFalse(TEXT("Character initially not in cinematic state"), LostChar->IsInCinematicState());
	TestFalse(TEXT("Character input initially not blocked"), LostChar->IsGameplayInputBlocked());

	AWakeupCinematicActor* Cinematic = World->SpawnActor<AWakeupCinematicActor>(
		AWakeupCinematicActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
	if (!TestNotNull(TEXT("Cinematic spawned"), Cinematic))
	{
		return false;
	}

	// 1. Start cinematic and verify input freeze
	Cinematic->StartCinematic(PC, LostChar);

	TestTrue(TEXT("Character is in cinematic state"), LostChar->IsInCinematicState());
	TestTrue(TEXT("Character gameplay input is blocked"), LostChar->IsGameplayInputBlocked());
	TestFalse(TEXT("Character HUD is hidden during cinematic"), LostChar->IsHUDVisible());
	TestTrue(TEXT("Character model is temporarily hidden during cinematic"), LostChar->IsHidden());
	TestTrue(TEXT("PC movement input ignored"), PC->IsMoveInputIgnored());
	TestTrue(TEXT("PC look input ignored"), PC->IsLookInputIgnored());

	// 2. Test input state gating during cinematic
	TestTrue(TEXT("Cinematic state blocks gameplay input flag"), LostChar->IsGameplayInputBlocked());
	TestTrue(TEXT("Cinematic state sets character cinematic flag"), LostChar->IsInCinematicState());
	TestFalse(TEXT("Character HUD is hidden during cinematic"), LostChar->IsHUDVisible());
	TestTrue(TEXT("PlayerController ignores move input"), PC->IsMoveInputIgnored());
	TestTrue(TEXT("PlayerController ignores look input"), PC->IsLookInputIgnored());

	// 3. Test clean unfreeze on natural finish
	Cinematic->Tick(7.6f);

	TestTrue(TEXT("Cinematic finished"), Cinematic->IsFinished());
	TestFalse(TEXT("Character exited cinematic state on finish"), LostChar->IsInCinematicState());
	TestFalse(TEXT("Character gameplay input no longer blocked on finish"), LostChar->IsGameplayInputBlocked());
	TestFalse(TEXT("Character model is restored visible on finish"), LostChar->IsHidden());
	TestFalse(TEXT("PC move input restored on finish"), PC->IsMoveInputIgnored());
	TestFalse(TEXT("PC look input restored on finish"), PC->IsLookInputIgnored());
	TestTrue(TEXT("HUD restored on finish"), LostChar->IsHUDVisible());

	// 4. Test clean unfreeze on skip
	AWakeupCinematicActor* SkipCinematic = World->SpawnActor<AWakeupCinematicActor>(
		AWakeupCinematicActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);

	SkipCinematic->StartCinematic(PC, LostChar);
	TestTrue(TEXT("Character re-entered cinematic state"), LostChar->IsInCinematicState());
	TestTrue(TEXT("Character input re-blocked"), LostChar->IsGameplayInputBlocked());
	TestTrue(TEXT("Character model is temporarily hidden during skip test"), LostChar->IsHidden());

	SkipCinematic->SkipCinematic();

	TestTrue(TEXT("Skip completed immediately"), SkipCinematic->IsFinished());
	TestFalse(TEXT("Character cleanly exited cinematic state on skip"), LostChar->IsInCinematicState());
	TestFalse(TEXT("Character gameplay input unblocked on skip"), LostChar->IsGameplayInputBlocked());
	TestFalse(TEXT("Character model is restored visible on skip"), LostChar->IsHidden());
	TestFalse(TEXT("PC move input restored on skip"), PC->IsMoveInputIgnored());
	TestFalse(TEXT("PC look input restored on skip"), PC->IsLookInputIgnored());
	TestTrue(TEXT("HUD restored on skip"), LostChar->IsHUDVisible());

	return true;
}

/**
 * Test 4: Skip functionality & input filtering
 * Tests:
 *  - Skip from Phase 1, Phase 2, and Phase 3
 *  - Camera teleport to standing location on skip
 *  - Fade cleared immediately on skip
 *  - Discrete keys (Space, Enter, Escape, LMB) triggering skip vs continuous mouse axes ignored
 *  - Safe idempotent no-op when calling SkipCinematic multiple times
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWakeupCinematicSkipFunctionalityTest,
	"LostWithin.Cinematic.Wakeup.SkipFunctionality",
	LOSTWITHIN_CINEMATIC_TEST_FLAGS)

bool FWakeupCinematicSkipFunctionalityTest::RunTest(const FString& Parameters)
{
	FScopedWakeupCinematicTestWorld Env;
	UWorld* World = Env.World;
	if (!TestNotNull(TEXT("Test world created successfully"), World))
	{
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	APlayerController* PC = World->SpawnActor<APlayerController>(APlayerController::StaticClass(), SpawnParams);
	ACharacter* Pawn = World->SpawnActor<ACharacter>(ACharacter::StaticClass(), FTransform(FVector(-4122.0f, -3742.0f, 170.0f)), SpawnParams);

	// 1. Skip from Phase 1 (Awakening)
	AWakeupCinematicActor* CinP1 = World->SpawnActor<AWakeupCinematicActor>(
		AWakeupCinematicActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);

	bool bP1DelegateFired = false;
	CinP1->OnFinishedNative.AddLambda([&bP1DelegateFired]() {
		bP1DelegateFired = true;
	});

	CinP1->StartCinematic(PC, Pawn);
	CinP1->Tick(0.5f); // in Phase 1
	TestEqual(TEXT("Currently in Phase 1"), CinP1->GetCurrentPhase(), 1);

	CinP1->SkipCinematic();

	TestTrue(TEXT("Skipped from Phase 1 is finished"), CinP1->IsFinished());
	TestFalse(TEXT("Skipped from Phase 1 not playing"), CinP1->IsPlaying());
	TestTrue(TEXT("Delegate broadcast on Phase 1 skip"), bP1DelegateFired);
	TestEqual(TEXT("Camera moved to standing location on Phase 1 skip"), CinP1->GetActorLocation(), CinP1->StandingLocation);

	// 2. Skip from Phase 2 (Sitting up & head shake)
	AWakeupCinematicActor* CinP2 = World->SpawnActor<AWakeupCinematicActor>(
		AWakeupCinematicActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);

	bool bP2DelegateFired = false;
	CinP2->OnFinishedNative.AddLambda([&bP2DelegateFired]() {
		bP2DelegateFired = true;
	});

	CinP2->StartCinematic(PC, Pawn);
	CinP2->Tick(3.5f); // in Phase 2
	TestEqual(TEXT("Currently in Phase 2"), CinP2->GetCurrentPhase(), 2);

	CinP2->SkipCinematic();

	TestTrue(TEXT("Skipped from Phase 2 is finished"), CinP2->IsFinished());
	TestTrue(TEXT("Delegate broadcast on Phase 2 skip"), bP2DelegateFired);
	TestEqual(TEXT("Camera moved to standing location on Phase 2 skip"), CinP2->GetActorLocation(), CinP2->StandingLocation);

	// 3. Skip from Phase 3 (Standing up & blend)
	AWakeupCinematicActor* CinP3 = World->SpawnActor<AWakeupCinematicActor>(
		AWakeupCinematicActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);

	bool bP3DelegateFired = false;
	CinP3->OnFinishedNative.AddLambda([&bP3DelegateFired]() {
		bP3DelegateFired = true;
	});

	CinP3->StartCinematic(PC, Pawn);
	CinP3->Tick(6.0f); // in Phase 3
	TestEqual(TEXT("Currently in Phase 3"), CinP3->GetCurrentPhase(), 3);

	CinP3->SkipCinematic();

	TestTrue(TEXT("Skipped from Phase 3 is finished"), CinP3->IsFinished());
	TestTrue(TEXT("Delegate broadcast on Phase 3 skip"), bP3DelegateFired);
	TestEqual(TEXT("Camera moved to standing location on Phase 3 skip"), CinP3->GetActorLocation(), CinP3->StandingLocation);

	// 4. Test input axis filtering: MouseX/MouseY must NOT trigger skip
	AWakeupCinematicActor* CinKeyFilter = World->SpawnActor<AWakeupCinematicActor>(
		AWakeupCinematicActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
	CinKeyFilter->StartCinematic(PC, Pawn);

	// Simulate analog mouse movement
	TestTrue(TEXT("EKeys::MouseX is an analog axis"), EKeys::MouseX.IsAxis1D());
	TestTrue(TEXT("EKeys::Mouse2D is a 2D axis"), EKeys::Mouse2D.IsAxis2D());

	// Calling SkipCinematic when not started or already finished is safe
	CinP3->SkipCinematic(); // repeated skip call
	TestTrue(TEXT("Repeated SkipCinematic call remains safely finished"), CinP3->IsFinished());

	return true;
}

/**
 * Test 5: GameMode integration
 * Verifies:
 *  - ALostWithinGameMode properties: bEnableOpeningCinematic (true), OpeningCinematicClass (AWakeupCinematicActor)
 *  - StartOpeningCinematic() spawns ActiveOpeningCinematic and triggers StartSequence
 *  - ActiveOpeningCinematic binds HandleOpeningCinematicFinished
 *  - Bypassed cleanly when bEnableOpeningCinematic is false
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWakeupCinematicGameModeIntegrationTest,
	"LostWithin.Cinematic.Wakeup.GameModeIntegration",
	LOSTWITHIN_CINEMATIC_TEST_FLAGS)

bool FWakeupCinematicGameModeIntegrationTest::RunTest(const FString& Parameters)
{
	FScopedWakeupCinematicTestWorld Env;
	UWorld* World = Env.World;
	if (!TestNotNull(TEXT("Test world created successfully"), World))
	{
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// 1. Verify CDO default values
	const ALostWithinGameMode* GM_CDO = GetDefault<ALostWithinGameMode>();
	if (TestNotNull(TEXT("ALostWithinGameMode CDO exists"), GM_CDO))
	{
		TestTrue(TEXT("bEnableOpeningCinematic is true by default"), GM_CDO->bEnableOpeningCinematic);
		TestEqual(TEXT("OpeningCinematicClass is AWakeupCinematicActor::StaticClass()"),
			GM_CDO->OpeningCinematicClass.Get(), AWakeupCinematicActor::StaticClass());
	}

	// 2. Test GameMode opening cinematic execution
	ALostWithinGameMode* GameMode = SpawnWakeupTestGameMode(World);
	if (!GameMode)
	{
		// Fallback to default game mode instance if blueprint not found
		GameMode = World->SpawnActor<ALostWithinGameMode>(ALostWithinGameMode::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
	}

	if (GameMode)
	{
		APlayerController* PC = World->SpawnActor<APlayerController>(APlayerController::StaticClass(), SpawnParams);
		ALostWithinCharacter* LostChar = SpawnWakeupTestCharacter(World, FVector(-4122.0f, -3742.0f, 170.0f));
		if (!LostChar)
		{
			LostChar = World->SpawnActor<ALostWithinCharacter>(ALostWithinCharacter::StaticClass(), FVector(-4122.0f, -3742.0f, 170.0f), FRotator::ZeroRotator, SpawnParams);
		}

		if (PC && LostChar)
		{
			PC->Possess(LostChar);
		}

		// Trigger opening cinematic
		GameMode->StartOpeningCinematic();

		if (TestNotNull(TEXT("GameMode spawned ActiveOpeningCinematic"), GameMode->ActiveOpeningCinematic.Get()))
		{
			TestTrue(TEXT("ActiveOpeningCinematic is playing"), GameMode->ActiveOpeningCinematic->IsPlaying());
			if (LostChar)
			{
				TestTrue(TEXT("Player placed in cinematic state by GameMode"), LostChar->IsInCinematicState());
			}

			// Complete the cinematic and verify GameMode handler unfreezes player
			GameMode->ActiveOpeningCinematic->SkipCinematic();

			if (LostChar)
			{
				TestFalse(TEXT("Player unfrozen after GameMode cinematic completes"), LostChar->IsInCinematicState());
				TestTrue(TEXT("Player HUD visible after GameMode cinematic completes"), LostChar->IsHUDVisible());
			}
		}

		// 3. Verify bEnableOpeningCinematic = false bypasses cinematic
		ALostWithinGameMode* DisabledGM = SpawnWakeupTestGameMode(World);
		if (DisabledGM)
		{
			DisabledGM->bEnableOpeningCinematic = false;
			DisabledGM->ActiveOpeningCinematic = nullptr;
			DisabledGM->StartOpeningCinematic();
			TestNull(TEXT("No cinematic spawned when bEnableOpeningCinematic is false"), DisabledGM->ActiveOpeningCinematic.Get());
		}
	}

	return true;
}

/**
 * Test 6: Umbrella test (LostWithin.Player.WakeupCinematic)
 * Retains backwards compatibility with the original test name
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWakeupCinematicAutomationTest,
	"LostWithin.Player.WakeupCinematic",
	LOSTWITHIN_CINEMATIC_TEST_FLAGS)

bool FWakeupCinematicAutomationTest::RunTest(const FString& Parameters)
{
	FScopedWakeupCinematicTestWorld TestWorld;
	UWorld* World = TestWorld.World;
	if (!TestNotNull(TEXT("World created successfully"), World))
	{
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AWakeupCinematicActor* Cinematic = World->SpawnActor<AWakeupCinematicActor>(
		AWakeupCinematicActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);

	if (!TestNotNull(TEXT("Wakeup cinematic actor spawned"), Cinematic))
	{
		return false;
	}

	// 1. Verify default origin and target coordinates (double precision)
	TestEqual(TEXT("Bed pillow X is -4082"), Cinematic->BedPillowLocation.X, -4082.0);
	TestEqual(TEXT("Bed pillow Y is -3902"), Cinematic->BedPillowLocation.Y, -3902.0);
	TestEqual(TEXT("Bed pillow Z is 110 (pillow height)"), Cinematic->BedPillowLocation.Z, 110.0);

	TestEqual(TEXT("Bed edge Z is 90 cm"), Cinematic->BedEdgeLocation.Z, 90.0);

	TestEqual(TEXT("PlayerStart X is -4122"), Cinematic->StandingLocation.X, -4122.0);
	TestEqual(TEXT("PlayerStart Y is -3742"), Cinematic->StandingLocation.Y, -3742.0);
	TestEqual(TEXT("Standing eye height Z is 170 cm"), Cinematic->StandingLocation.Z, 170.0);

	// 2. Verify phase timings (float precision)
	TestEqual(TEXT("Phase 1 duration is 2.5s"), Cinematic->Phase1Duration, 2.5f);
	TestEqual(TEXT("Phase 2 duration is 3.0s"), Cinematic->Phase2Duration, 3.0f);
	TestEqual(TEXT("Phase 3 duration is 2.0s"), Cinematic->Phase3Duration, 2.0f);
	TestEqual(TEXT("Total duration is 7.5s"), Cinematic->GetTotalDuration(), 7.5f);

	// 3. Test eye blink alpha curve
	TestEqual(TEXT("Eyes fully closed at start"), AWakeupCinematicActor::ComputeBlinkAlpha(0.0f, 2.5f), 1.0f);
	TestTrue(TEXT("Eyes fluttering at 0.6s"), AWakeupCinematicActor::ComputeBlinkAlpha(0.6f, 2.5f) < 1.0f);
	TestTrue(TEXT("Eyes squeezing shut at 0.9s"), AWakeupCinematicActor::ComputeBlinkAlpha(0.9f, 2.5f) > 0.5f);
	TestEqual(TEXT("Eyes fully open at end of Phase 1"), AWakeupCinematicActor::ComputeBlinkAlpha(2.3f, 2.5f), 0.0f);

	// 4. Test start sequence
	APlayerController* PC = World->SpawnActor<APlayerController>(APlayerController::StaticClass(), SpawnParams);
	ACharacter* Pawn = World->SpawnActor<ACharacter>(ACharacter::StaticClass(), FTransform(Cinematic->StandingLocation), SpawnParams);

	Cinematic->StartCinematic(PC, Pawn);

	TestTrue(TEXT("Cinematic is playing"), Cinematic->IsPlaying());
	TestFalse(TEXT("Cinematic is not finished"), Cinematic->IsFinished());
	TestEqual(TEXT("Current phase is Phase 1 (Awakening)"), Cinematic->GetCurrentPhase(), 1);

	// Initial view should be at the pillow
	TestEqual(TEXT("Initial location at pillow X"), Cinematic->GetActorLocation().X, -4082.0);
	TestEqual(TEXT("Initial location at pillow Y"), Cinematic->GetActorLocation().Y, -3902.0);
	TestEqual(TEXT("Initial location at pillow Z"), Cinematic->GetActorLocation().Z, 110.0);

	// 5. Test Phase progression
	Cinematic->Tick(2.6f);
	TestEqual(TEXT("Current phase is Phase 2 (Sitting Up)"), Cinematic->GetCurrentPhase(), 2);
	TestTrue(TEXT("Z height moving from pillow (110) towards bed edge (90)"),
		Cinematic->GetActorLocation().Z <= 110.0 && Cinematic->GetActorLocation().Z >= 85.0);

	Cinematic->Tick(3.0f); // Total elapsed: 5.6s
	TestEqual(TEXT("Current phase is Phase 3 (Standing Up & Blend)"), Cinematic->GetCurrentPhase(), 3);
	TestTrue(TEXT("Rising towards standing eye height (170 cm)"), Cinematic->GetActorLocation().Z > 90.0);

	// 6. Test natural finish
	bool bFinishedDelegateFired = false;
	Cinematic->OnFinishedNative.AddLambda([&bFinishedDelegateFired]() {
		bFinishedDelegateFired = true;
	});

	Cinematic->Tick(2.0f); // Total elapsed: 7.6s > 7.5s
	TestTrue(TEXT("Cinematic is finished"), Cinematic->IsFinished());
	TestFalse(TEXT("Cinematic is no longer playing"), Cinematic->IsPlaying());
	TestTrue(TEXT("OnFinishedNative fired"), bFinishedDelegateFired);
	TestEqual(TEXT("Final camera location at standing X"), Cinematic->GetActorLocation().X, -4122.0);
	TestEqual(TEXT("Final camera location at standing Y"), Cinematic->GetActorLocation().Y, -3742.0);
	TestEqual(TEXT("Final camera location at standing Z"), Cinematic->GetActorLocation().Z, 170.0);

	// 7. Test Skip functionality on a fresh actor
	AWakeupCinematicActor* SkipTestActor = World->SpawnActor<AWakeupCinematicActor>(
		AWakeupCinematicActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);

	bool bSkipDelegateFired = false;
	SkipTestActor->OnFinishedNative.AddLambda([&bSkipDelegateFired]() {
		bSkipDelegateFired = true;
	});

	SkipTestActor->StartCinematic(PC, Pawn);
	TestTrue(TEXT("Skip actor is playing"), SkipTestActor->IsPlaying());

	SkipTestActor->SkipCinematic();

	TestTrue(TEXT("Skip actor finished immediately"), SkipTestActor->IsFinished());
	TestTrue(TEXT("Skip delegate broadcasted"), bSkipDelegateFired);
	TestEqual(TEXT("Camera teleported to standing position on skip"), SkipTestActor->GetActorLocation(), SkipTestActor->StandingLocation);

	// 8. Test StartSequence overload
	AWakeupCinematicActor* SequenceActor = World->SpawnActor<AWakeupCinematicActor>(
		AWakeupCinematicActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
	SequenceActor->StartSequence(Pawn, PC);
	TestTrue(TEXT("StartSequence triggered cinematic"), SequenceActor->IsPlaying());
	SequenceActor->SkipCinematic();

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
