// Copyright Epic Games, Inc. All Rights Reserved.

#include "Player/WakeupCinematicActor.h"

#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/AudioComponent.h"
#include "Components/InputComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "LostWithin.h"
#include "LostWithinCharacter.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

AWakeupCinematicActor::AWakeupCinematicActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	SetActorEnableCollision(false);

	if (UCameraComponent* Camera = GetCameraComponent())
	{
		Camera->bConstrainAspectRatio = false;
		Camera->SetFieldOfView(BaseFOV);

		Camera->PostProcessSettings.bOverride_MotionBlurAmount = 1;
		Camera->PostProcessSettings.MotionBlurAmount = MaxMotionBlurAmount;
		Camera->PostProcessSettings.bOverride_MotionBlurMax = 1;
		Camera->PostProcessSettings.MotionBlurMax = MaxMotionBlurMax;
		Camera->PostProcessSettings.bOverride_VignetteIntensity = 1;
		Camera->PostProcessSettings.VignetteIntensity = MaxVignetteIntensity;
	}

	// Try default groan / breathing sounds if present in the project
	static ConstructorHelpers::FObjectFinderOptional<USoundBase> DoorSighFinder(
		TEXT("/Game/Level/MAZE/Objects/ScaryDoor/SFX/SC_Door_Ominous_sigh.SC_Door_Ominous_sigh"));
	if (DoorSighFinder.Succeeded())
	{
		GroanSound = DoorSighFinder.Get();
	}

	static ConstructorHelpers::FObjectFinderOptional<USoundBase> BedAmbienceFinder(
		TEXT("/Game/Audio/Ambience/AMB_MazeBed_01.AMB_MazeBed_01"));
	if (BedAmbienceFinder.Succeeded())
	{
		BedAmbienceSound = BedAmbienceFinder.Get();
		BreathingSound = BedAmbienceFinder.Get();
	}

	static ConstructorHelpers::FObjectFinderOptional<USoundBase> HeartbeatFinder(
		TEXT("/Game/Audio/SFX/Player/Death/SFX_DeathImpact_01.SFX_DeathImpact_01"));
	if (HeartbeatFinder.Succeeded())
	{
		HeartbeatSound = HeartbeatFinder.Get();
	}
}

void AWakeupCinematicActor::BeginPlay()
{
	Super::BeginPlay();

	if (bAutoStart && !bStarted)
	{
		if (const UWorld* World = GetWorld())
		{
			APlayerController* PC = World->GetFirstPlayerController();
			if (PC)
			{
				StartCinematic(PC, PC->GetPawn());
			}
		}
	}
}

void AWakeupCinematicActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (BedAmbienceAudioComponent)
	{
		BedAmbienceAudioComponent->Stop();
		BedAmbienceAudioComponent = nullptr;
	}

	if (PlayerController.IsValid() && bStarted && !bFinished)
	{
		DisableInput(PlayerController.Get());
	}

	Super::EndPlay(EndPlayReason);
}

float AWakeupCinematicActor::ComputeBlinkAlpha(float CurrentTime, float Duration)
{
	if (Duration <= UE_KINDA_SMALL_NUMBER)
	{
		return 0.0f;
	}

	const float T = FMath::Clamp(CurrentTime / Duration, 0.0f, 1.0f);

	// 0.00 - 0.16 (0.0s - 0.4s): Eyes tightly shut in darkness
	if (T < 0.16f)
	{
		return 1.0f;
	}

	// 0.16 - 0.30 (0.4s - 0.75s): First slit of light (1.0 -> 0.35)
	if (T < 0.30f)
	{
		const float SubT = (T - 0.16f) / (0.30f - 0.16f);
		return FMath::InterpEaseInOut(1.0f, 0.35f, SubT, 2.0f);
	}

	// 0.30 - 0.40 (0.75s - 1.0s): Squeeze shut against bright light (0.35 -> 0.90)
	if (T < 0.40f)
	{
		const float SubT = (T - 0.30f) / (0.40f - 0.30f);
		return FMath::InterpEaseInOut(0.35f, 0.90f, SubT, 2.0f);
	}

	// 0.40 - 0.56 (1.0s - 1.4s): Second wider opening (0.90 -> 0.15)
	if (T < 0.56f)
	{
		const float SubT = (T - 0.40f) / (0.56f - 0.40f);
		return FMath::InterpEaseInOut(0.90f, 0.15f, SubT, 2.0f);
	}

	// 0.56 - 0.66 (1.4s - 1.65s): Quick flutter blink (0.15 -> 0.70)
	if (T < 0.66f)
	{
		const float SubT = (T - 0.56f) / (0.66f - 0.56f);
		return FMath::InterpEaseInOut(0.15f, 0.70f, SubT, 2.0f);
	}

	// 0.66 - 0.84 (1.65s - 2.1s): Final steady opening (0.70 -> 0.0)
	if (T < 0.84f)
	{
		const float SubT = (T - 0.66f) / (0.84f - 0.66f);
		return FMath::InterpEaseInOut(0.70f, 0.0f, SubT, 2.0f);
	}

	// 0.84 - 1.00 (2.1s - 2.5s): Eyes fully open, clear vision
	return 0.0f;
}

int32 AWakeupCinematicActor::GetCurrentPhase() const
{
	if (!bStarted)
	{
		return 0;
	}
	if (Elapsed < Phase1Duration)
	{
		return 1;
	}
	if (Elapsed < Phase1Duration + Phase2Duration)
	{
		return 2;
	}
	if (Elapsed < GetTotalDuration())
	{
		return 3;
	}
	return 4;
}

void AWakeupCinematicActor::StartSequence(APawn* InPlayerPawn, APlayerController* InPlayerController)
{
	if (!InPlayerController && InPlayerPawn)
	{
		InPlayerController = Cast<APlayerController>(InPlayerPawn->GetController());
	}
	StartCinematic(InPlayerController, InPlayerPawn);
}

void AWakeupCinematicActor::StartCinematic(APlayerController* InPlayerController, APawn* InPlayerPawn)
{
	if (bStarted && !bFinished)
	{
		return;
	}

	PlayerController = InPlayerController;
	APawn* EffectivePawn = InPlayerPawn;
	if (!EffectivePawn && InPlayerController)
	{
		EffectivePawn = InPlayerController->GetPawn();
	}
	PlayerPawn = EffectivePawn;

	Elapsed = 0.0f;
	bStarted = true;
	bFinished = false;
	bBlendStarted = false;

	// Align camera immediately to SM_Bed pillow position and lying down rotation
	ApplyView(BedPillowLocation, InitialLyingRotation, BaseFOV);

	if (InPlayerController)
	{
		// Cut view target to this camera director immediately
		InPlayerController->SetViewTargetWithBlend(this, 0.0f);

		// Start in full darkness for the initial eye closure
		if (InPlayerController->PlayerCameraManager)
		{
			InPlayerController->PlayerCameraManager->SetManualCameraFade(1.0f, FLinearColor::Black, /*bFadeAudio=*/false);
		}

		// Enable input to capture skip commands (Space, AnyKey, Enter, Escape, LMB)
		if (bAllowSkip)
		{
			EnableInput(InPlayerController);
			if (InputComponent)
			{
				InputComponent->BindKey(EKeys::SpaceBar, IE_Pressed, this, &AWakeupCinematicActor::SkipCinematic);
				InputComponent->BindKey(EKeys::Enter, IE_Pressed, this, &AWakeupCinematicActor::SkipCinematic);
				InputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &AWakeupCinematicActor::SkipCinematic);
				InputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &AWakeupCinematicActor::SkipCinematic);
				InputComponent->BindKey(EKeys::AnyKey, IE_Pressed, this, &AWakeupCinematicActor::HandleAnyKey);
			}
		}
	}

	// Freeze player movement and hide HUD during the cinematic
	if (APawn* Pawn = PlayerPawn.Get())
	{
		if (ALostWithinCharacter* LostCharacter = Cast<ALostWithinCharacter>(Pawn))
		{
			LostCharacter->EnterCinematicState();
		}
		else if (ACharacter* Character = Cast<ACharacter>(Pawn))
		{
			if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
			{
				Movement->StopMovementImmediately();
			}
		}
	}

	// Play ambient bed audio and disorientation audio
	if (BedAmbienceSound)
	{
		BedAmbienceAudioComponent = UGameplayStatics::SpawnSound2D(this, BedAmbienceSound, 1.0f);
		UE_LOG(LogLostWithin, Log, TEXT("Wakeup cinematic: playing bed ambience %s"), *BedAmbienceSound->GetName());
	}
	if (HeartbeatSound)
	{
		UGameplayStatics::PlaySound2D(this, HeartbeatSound, 0.85f, 0.85f);
		UE_LOG(LogLostWithin, Log, TEXT("Wakeup cinematic: playing heartbeat sound %s"), *HeartbeatSound->GetName());
	}
	if (GroanSound)
	{
		UGameplayStatics::PlaySound2D(this, GroanSound, 1.0f, 1.0f, 0.0f, nullptr, nullptr, /*bIsUISound=*/false);
		UE_LOG(LogLostWithin, Log, TEXT("Wakeup cinematic: playing groan sound %s"), *GroanSound->GetName());
	}
	if (BreathingSound && BreathingSound != BedAmbienceSound)
	{
		UGameplayStatics::PlaySound2D(this, BreathingSound, 1.0f, 1.0f, 0.0f, nullptr, nullptr, /*bIsUISound=*/false);
		UE_LOG(LogLostWithin, Log, TEXT("Wakeup cinematic: playing breathing sound %s"), *BreathingSound->GetName());
	}

	SetActorTickEnabled(true);
	UE_LOG(LogLostWithin, Log, TEXT("Wakeup cinematic started at SM_Bed (%f, %f, %f)."),
		BedPillowLocation.X, BedPillowLocation.Y, BedPillowLocation.Z);
}

void AWakeupCinematicActor::ApplyView(const FVector& Location, const FRotator& Rotation, float FieldOfView)
{
	SetActorLocationAndRotation(Location, Rotation);
	if (UCameraComponent* Camera = GetCameraComponent())
	{
		Camera->SetFieldOfView(FieldOfView);
	}
}

void AWakeupCinematicActor::HandleAnyKey(FKey Key)
{
	// Ignore continuous analog mouse and touch axes so normal mouse motion does not cancel the cinematic.
	// Only discrete key, mouse button, and gamepad presses trigger skip.
	if (!Key.IsAxis1D() && !Key.IsAxis2D() && !Key.IsAxis3D())
	{
		SkipCinematic();
	}
}

void AWakeupCinematicActor::SkipCinematic()
{
	if (bFinished || !bStarted)
	{
		return;
	}

	UE_LOG(LogLostWithin, Log, TEXT("Wakeup cinematic skipped by user."));

	if (PlayerController.IsValid())
	{
		if (APlayerCameraManager* CameraManager = PlayerController->PlayerCameraManager)
		{
			CameraManager->StopCameraFade();
			CameraManager->SetManualCameraFade(0.0f, FLinearColor::Black, /*bFadeAudio=*/false);
		}

		APawn* PawnToRestore = PlayerPawn.Get();
		if (!PawnToRestore && PlayerController.IsValid())
		{
			PawnToRestore = PlayerController->GetPawn();
		}
		if (PawnToRestore)
		{
			PlayerController->SetViewTarget(PawnToRestore);
			if (ALostWithinCharacter* LostCharacter = Cast<ALostWithinCharacter>(PawnToRestore))
			{
				LostCharacter->ExitCinematicState();
			}
		}

		DisableInput(PlayerController.Get());
	}

	if (BedAmbienceAudioComponent)
	{
		BedAmbienceAudioComponent->FadeOut(0.5f, 0.0f);
		BedAmbienceAudioComponent = nullptr;
	}

	// Reset camera post-process overrides
	if (UCameraComponent* Camera = GetCameraComponent())
	{
		Camera->PostProcessSettings.bOverride_MotionBlurAmount = 0;
		Camera->PostProcessSettings.bOverride_MotionBlurMax = 0;
		Camera->PostProcessSettings.bOverride_VignetteIntensity = 0;
		Camera->SetFieldOfView(BaseFOV);
	}

	// Teleport camera to final standing position
	ApplyView(StandingLocation, StandingRotation, BaseFOV);

	bFinished = true;
	SetActorTickEnabled(false);

	OnCinematicFinished.Broadcast();
	OnFinishedNative.Broadcast();
}

void AWakeupCinematicActor::FinishCinematic()
{
	if (bFinished)
	{
		return;
	}

	bFinished = true;
	SetActorTickEnabled(false);

	if (PlayerController.IsValid())
	{
		if (APlayerCameraManager* CameraManager = PlayerController->PlayerCameraManager)
		{
			CameraManager->StopCameraFade();
			CameraManager->SetManualCameraFade(0.0f, FLinearColor::Black, /*bFadeAudio=*/false);
		}

		APawn* TargetPawn = PlayerPawn.Get();
		if (!TargetPawn && PlayerController.IsValid())
		{
			TargetPawn = PlayerController->GetPawn();
		}
		if (TargetPawn)
		{
			if (PlayerController->GetViewTarget() != TargetPawn)
			{
				PlayerController->SetViewTarget(TargetPawn);
			}
			if (ALostWithinCharacter* LostCharacter = Cast<ALostWithinCharacter>(TargetPawn))
			{
				LostCharacter->ExitCinematicState();
			}
			else if (ACharacter* Character = Cast<ACharacter>(TargetPawn))
			{
				Character->SetActorHiddenInGame(false);
			}
		}

		DisableInput(PlayerController.Get());
	}

	if (BedAmbienceAudioComponent)
	{
		BedAmbienceAudioComponent->FadeOut(0.5f, 0.0f);
		BedAmbienceAudioComponent = nullptr;
	}

	// Reset camera post-process overrides
	if (UCameraComponent* Camera = GetCameraComponent())
	{
		Camera->PostProcessSettings.bOverride_MotionBlurAmount = 0;
		Camera->PostProcessSettings.bOverride_MotionBlurMax = 0;
		Camera->PostProcessSettings.bOverride_VignetteIntensity = 0;
		Camera->SetFieldOfView(BaseFOV);
	}

	ApplyView(StandingLocation, StandingRotation, BaseFOV);

	UE_LOG(LogLostWithin, Log, TEXT("Wakeup cinematic finished successfully."));

	OnCinematicFinished.Broadcast();
	OnFinishedNative.Broadcast();
}

void AWakeupCinematicActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bStarted || bFinished)
	{
		return;
	}

	Elapsed += DeltaSeconds;

	const float Phase2Start = Phase1Duration;
	const float Phase3Start = Phase1Duration + Phase2Duration;
	const float TotalDuration = GetTotalDuration();

	if (Elapsed < Phase2Start)
	{
		UpdatePhase1(DeltaSeconds);
		return;
	}

	if (Elapsed < Phase3Start)
	{
		UpdatePhase2(DeltaSeconds);
		return;
	}

	if (Elapsed < TotalDuration)
	{
		UpdatePhase3(DeltaSeconds);
		return;
	}

	FinishCinematic();
}

void AWakeupCinematicActor::UpdatePhase1(float DeltaSeconds)
{
	const float BlinkAlpha = ComputeBlinkAlpha(Elapsed, Phase1Duration);

	// Camera fade: 1.0 = pitch black screen, 0.0 = clear vision
	if (PlayerController.IsValid() && PlayerController->PlayerCameraManager)
	{
		PlayerController->PlayerCameraManager->SetManualCameraFade(BlinkAlpha, FLinearColor::Black, /*bFadeAudio=*/false);
	}

	// Post-process settings: heavy motion blur and vignette
	if (UCameraComponent* Camera = GetCameraComponent())
	{
		Camera->PostProcessSettings.bOverride_MotionBlurAmount = 1;
		Camera->PostProcessSettings.bOverride_MotionBlurMax = 1;
		Camera->PostProcessSettings.MotionBlurAmount = MaxMotionBlurAmount;
		Camera->PostProcessSettings.MotionBlurMax = MaxMotionBlurMax;

		Camera->PostProcessSettings.bOverride_VignetteIntensity = 1;
		const float AwakeningProgress = FMath::Clamp(Elapsed / Phase1Duration, 0.0f, 1.0f);
		const float TunnelAlpha = FMath::Max(BlinkAlpha, 1.0f - AwakeningProgress);
		Camera->PostProcessSettings.VignetteIntensity = FMath::Lerp(0.3f, MaxVignetteIntensity, TunnelAlpha);
	}

	// Subtle chest heave and breathing cycle while lying on the pillow
	const float BreathCycle = Elapsed * 2.5f;
	const float BreathZ = FMath::Sin(BreathCycle) * 0.8f;
	const float BreathPitch = FMath::Sin(BreathCycle) * 0.5f;

	FRotator CurrentRotation = InitialLyingRotation;
	CurrentRotation.Pitch += BreathPitch;

	ApplyView(BedPillowLocation + FVector(0.0f, 0.0f, BreathZ), CurrentRotation, BaseFOV);
}

void AWakeupCinematicActor::UpdatePhase2(float DeltaSeconds)
{
	const float Phase2Time = Elapsed - Phase1Duration;
	const float Phase2Alpha = FMath::Clamp(Phase2Time / FMath::Max(Phase2Duration, UE_KINDA_SMALL_NUMBER), 0.0f, 1.0f);
	const float SitAlpha = FMath::InterpEaseInOut(0.0f, 1.0f, Phase2Alpha, 2.0f);

	// Interpolate position from pillow (Z=110 cm) to bed edge (Z=90 cm)
	const FVector BaseLocation = FMath::Lerp(BedPillowLocation, BedEdgeLocation, SitAlpha);

	// Interpolate pitch and roll as the player sits up to bed edge
	const FRotator BaseRotation = FMath::Lerp(InitialLyingRotation, SittingRotation, SitAlpha);

	// Amnesia lightheaded head-shake oscillation:
	// Envelope peaks at mid-situp and eases smoothly
	const float Envelope = FMath::Sin(Phase2Alpha * UE_PI);

	// 1. Damped sine wave for rhythmic head shaking (trying to shake head clear)
	const float DampedSine = FMath::Sin(Phase2Time * HeadShakeFrequency * 2.0f * UE_PI) * Envelope;

	// 2. Perlin noise sway for erratic, uncoordinated lightheadedness
	const float SwayYaw = FMath::PerlinNoise1D(Elapsed * 2.3f) * NoiseYawAmplitude * Envelope;
	const float SwayRoll = FMath::PerlinNoise1D(Elapsed * 2.9f + 14.3f) * NoiseRollAmplitude * Envelope;
	const float SwayPitch = FMath::PerlinNoise1D(Elapsed * 1.8f + 27.5f) * NoisePitchAmplitude * Envelope;

	FRotator FinalRotation = BaseRotation;
	FinalRotation.Yaw += (DampedSine * HeadShakeYawAmplitude) + SwayYaw;
	FinalRotation.Roll += (DampedSine * HeadShakeRollAmplitude) + SwayRoll;
	FinalRotation.Pitch += SwayPitch;

	// Subtle lightheaded dizziness FOV pulse
	const float DizzinessPulse = FMath::Sin(Phase2Time * 4.0f) * 1.5f * Envelope;

	// Ensure camera fade is clear
	if (PlayerController.IsValid() && PlayerController->PlayerCameraManager)
	{
		PlayerController->PlayerCameraManager->SetManualCameraFade(0.0f, FLinearColor::Black, /*bFadeAudio=*/false);
	}

	// Motion blur remains strong during the head shake, then starts tapering
	if (UCameraComponent* Camera = GetCameraComponent())
	{
		Camera->PostProcessSettings.bOverride_MotionBlurAmount = 1;
		Camera->PostProcessSettings.bOverride_MotionBlurMax = 1;
		const float BlurDecay = 1.0f - (Phase2Alpha * 0.35f);
		Camera->PostProcessSettings.MotionBlurAmount = MaxMotionBlurAmount * BlurDecay;
		Camera->PostProcessSettings.MotionBlurMax = MaxMotionBlurMax * BlurDecay;

		Camera->PostProcessSettings.bOverride_VignetteIntensity = 1;
		Camera->PostProcessSettings.VignetteIntensity = 0.3f + 0.6f * Envelope;
	}

	ApplyView(BaseLocation, FinalRotation, BaseFOV + DizzinessPulse);
}

void AWakeupCinematicActor::UpdatePhase3(float DeltaSeconds)
{
	const float Phase3Time = Elapsed - Phase1Duration - Phase2Duration;
	const float Phase3Alpha = FMath::Clamp(Phase3Time / FMath::Max(Phase3Duration, UE_KINDA_SMALL_NUMBER), 0.0f, 1.0f);
	const float StandAlpha = FMath::InterpEaseInOut(0.0f, 1.0f, Phase3Alpha, 2.0f);

	// Trigger smooth blend back to player pawn at the beginning of Phase 3
	if (!bBlendStarted)
	{
		bBlendStarted = true;
		APawn* TargetPawn = PlayerPawn.Get();
		if (!TargetPawn && PlayerController.IsValid())
		{
			TargetPawn = PlayerController->GetPawn();
		}
		if (PlayerController.IsValid() && TargetPawn)
		{
			PlayerController->SetViewTargetWithBlend(TargetPawn, Phase3Duration, EViewTargetBlendFunction::VTBlend_Cubic, 2.0f);
			UE_LOG(LogLostWithin, Log, TEXT("Wakeup cinematic: starting camera blend to pawn %s over %f s"),
				*TargetPawn->GetName(), Phase3Duration);
		}
	}

	// Rises to standing eye height (~170 cm) moving to PlayerStart (X=-4122, Y=-3742)
	const FVector CurrentLocation = FMath::Lerp(BedEdgeLocation, StandingLocation, StandAlpha);
	const FRotator CurrentRotation = FMath::Lerp(SittingRotation, StandingRotation, StandAlpha);

	// Taper post-process motion blur and vignette to zero as standing completes
	if (UCameraComponent* Camera = GetCameraComponent())
	{
		const float Remaining = 1.0f - Phase3Alpha;
		Camera->PostProcessSettings.MotionBlurAmount = MaxMotionBlurAmount * 0.35f * Remaining;
		Camera->PostProcessSettings.MotionBlurMax = MaxMotionBlurMax * 0.35f * Remaining;
		Camera->PostProcessSettings.VignetteIntensity = 0.3f * Remaining;
	}

	ApplyView(CurrentLocation, CurrentRotation, BaseFOV);
}
