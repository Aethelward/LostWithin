// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraActor.h"
#include "WakeupCinematicActor.generated.h"

class APawn;
class APlayerController;
class UAudioComponent;
class USoundBase;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnWakeupCinematicFinished);

/**
 * Wakeup cinematic camera director for LostWithin.
 * Manages the opening sequence when the player wakes up disoriented in the maze bed:
 *  - Origin: Starts positioned at SM_Bed (approx X=-4082, Y=-3902, Z=110 cm for pillow height), lying down facing up/angled.
 *  - Phase 1 (0.0s - 2.5s) Awakening: Eye blinks (camera fade / post-process vignette), heavy motion blur, disorientation breathing/groan.
 *  - Phase 2 (2.5s - 5.5s) Sitting up & Amnesia Head Shake: Interpolates pitch and roll as the player sits up to bed edge (~90 cm Z).
 *    Adds amnesia lightheaded head-shake oscillation (swaying yaw and roll with Perlin noise / damping sine).
 *  - Phase 3 (5.5s - 7.5s) Standing Up & Smooth Blend: Rises to standing eye height (~170 cm) moving to PlayerStart (X=-4122, Y=-3742),
 *    smoothly blending camera back to the player pawn and broadcasting OnCinematicFinished.
 *  - Support skip (e.g. AnyKey / Space) to immediately complete the blend.
 */
UCLASS()
class LOSTWITHIN_API AWakeupCinematicActor : public ACameraActor
{
	GENERATED_BODY()

public:

	AWakeupCinematicActor();

	/** Bed pillow position where the player starts lying down (SM_Bed pillow) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wakeup Cinematic|Transform", meta = (Units = "cm"))
	FVector BedPillowLocation = FVector(-4082.0f, -3902.0f, 110.0f);

	/** Bed edge position where the player sits up */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wakeup Cinematic|Transform", meta = (Units = "cm"))
	FVector BedEdgeLocation = FVector(-4100.0f, -3820.0f, 90.0f);

	/** Standing eye position near PlayerStart */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wakeup Cinematic|Transform", meta = (Units = "cm"))
	FVector StandingLocation = FVector(-4122.0f, -3742.0f, 170.0f);

	/** Initial rotation lying down on the pillow, facing up toward ceiling/angled */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wakeup Cinematic|Transform")
	FRotator InitialLyingRotation = FRotator(60.0f, 90.0f, -10.0f);

	/** Rotation when sitting up at the edge of the bed looking forward */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wakeup Cinematic|Transform")
	FRotator SittingRotation = FRotator(-5.0f, 95.0f, 0.0f);

	/** Final standing rotation facing the room */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wakeup Cinematic|Transform")
	FRotator StandingRotation = FRotator(0.0f, 90.0f, 0.0f);

	/** Phase 1: Awakening (eye blinks, heavy motion blur, disorientation breathing/groan) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wakeup Cinematic|Timing", meta = (ClampMin = "0.1", Units = "s"))
	float Phase1Duration = 2.5f;

	/** Phase 2: Sitting up & amnesia head shake oscillation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wakeup Cinematic|Timing", meta = (ClampMin = "0.1", Units = "s"))
	float Phase2Duration = 3.0f;

	/** Phase 3: Standing up & blending camera back to pawn */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wakeup Cinematic|Timing", meta = (ClampMin = "0.1", Units = "s"))
	float Phase3Duration = 2.0f;

	/** Max motion blur amount during Phase 1 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wakeup Cinematic|Visuals", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MaxMotionBlurAmount = 1.0f;

	/** Max motion blur maximum percentage during Phase 1 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wakeup Cinematic|Visuals", meta = (ClampMin = "0.0", Units = "%"))
	float MaxMotionBlurMax = 100.0f;

	/** Heavy vignette intensity while waking up */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wakeup Cinematic|Visuals", meta = (ClampMin = "0.0"))
	float MaxVignetteIntensity = 1.8f;

	/** Base FOV for the camera */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wakeup Cinematic|Visuals", meta = (ClampMin = "30.0", ClampMax = "120.0", Units = "deg"))
	float BaseFOV = 90.0f;

	/** Frequency of the rhythmic head-shake oscillation (Hz) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wakeup Cinematic|Head Shake", meta = (ClampMin = "0.5", ClampMax = "10.0"))
	float HeadShakeFrequency = 3.5f;

	/** Yaw amplitude of the rhythmic amnesia head shake */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wakeup Cinematic|Head Shake", meta = (ClampMin = "0.0", ClampMax = "30.0", Units = "deg"))
	float HeadShakeYawAmplitude = 6.0f;

	/** Roll amplitude of the head shake oscillation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wakeup Cinematic|Head Shake", meta = (ClampMin = "0.0", ClampMax = "30.0", Units = "deg"))
	float HeadShakeRollAmplitude = 4.0f;

	/** Perlin noise yaw sway amplitude for organic lightheadedness */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wakeup Cinematic|Head Shake", meta = (ClampMin = "0.0", ClampMax = "20.0", Units = "deg"))
	float NoiseYawAmplitude = 3.0f;

	/** Perlin noise roll sway amplitude for organic lightheadedness */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wakeup Cinematic|Head Shake", meta = (ClampMin = "0.0", ClampMax = "20.0", Units = "deg"))
	float NoiseRollAmplitude = 2.5f;

	/** Perlin noise pitch sway amplitude for organic lightheadedness */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wakeup Cinematic|Head Shake", meta = (ClampMin = "0.0", ClampMax = "20.0", Units = "deg"))
	float NoisePitchAmplitude = 1.5f;

	/** Ambient bed sound (AMB_MazeBed_01) played during the cinematic sequence */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wakeup Cinematic|Audio")
	TObjectPtr<USoundBase> BedAmbienceSound;

	/** Heartbeat / pulse sound played during the awakening sequence */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wakeup Cinematic|Audio")
	TObjectPtr<USoundBase> HeartbeatSound;

	/** Disorientation groan sound played during awakening (optional) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wakeup Cinematic|Audio")
	TObjectPtr<USoundBase> GroanSound;

	/** Disorientation heavy breathing sound played during awakening (optional) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wakeup Cinematic|Audio")
	TObjectPtr<USoundBase> BreathingSound;

	/** Active bed ambience audio component */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Wakeup Cinematic|Audio")
	TObjectPtr<UAudioComponent> BedAmbienceAudioComponent = nullptr;

	/** Whether skipping with AnyKey/Space is enabled */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wakeup Cinematic|Input")
	bool bAllowSkip = true;

	/** Whether to automatically start on BeginPlay with the first local player */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wakeup Cinematic")
	bool bAutoStart = false;

	/** Broadcast when the cinematic finishes or is skipped */
	UPROPERTY(BlueprintAssignable, Category = "Wakeup Cinematic")
	FOnWakeupCinematicFinished OnCinematicFinished;

	/** Native delegate for C++ listeners */
	FSimpleMulticastDelegate OnFinishedNative;

	/** Begins the wakeup sequence with the given player controller and optional target pawn */
	UFUNCTION(BlueprintCallable, Category = "Wakeup Cinematic")
	void StartCinematic(APlayerController* InPlayerController, APawn* InPlayerPawn = nullptr);

	/** Begins the wakeup sequence with the given pawn (compatible with GameMode callers) */
	UFUNCTION(BlueprintCallable, Category = "Wakeup Cinematic")
	void StartSequence(APawn* InPlayerPawn, APlayerController* InPlayerController = nullptr);

	/** Skips the cinematic immediately and completes the blend to the player pawn */
	UFUNCTION(BlueprintCallable, Category = "Wakeup Cinematic")
	void SkipCinematic();

	UFUNCTION(BlueprintPure, Category = "Wakeup Cinematic")
	bool IsPlaying() const { return bStarted && !bFinished; }

	UFUNCTION(BlueprintPure, Category = "Wakeup Cinematic")
	bool IsFinished() const { return bFinished; }

	/** Total length of the three phases */
	UFUNCTION(BlueprintPure, Category = "Wakeup Cinematic")
	float GetTotalDuration() const { return Phase1Duration + Phase2Duration + Phase3Duration; }

	/** Current phase (0 = Not Started, 1 = Awakening, 2 = Sitting Up & Head Shake, 3 = Standing Up & Blend, 4 = Finished) */
	UFUNCTION(BlueprintPure, Category = "Wakeup Cinematic")
	int32 GetCurrentPhase() const;

	UFUNCTION(BlueprintPure, Category = "Wakeup Cinematic")
	float GetElapsedTime() const { return Elapsed; }

	/** Computes eye closure alpha: 1.0 = fully closed (black), 0.0 = fully open */
	static float ComputeBlinkAlpha(float CurrentTime, float Duration);

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

protected:

	void UpdatePhase1(float DeltaSeconds);
	void UpdatePhase2(float DeltaSeconds);
	void UpdatePhase3(float DeltaSeconds);
	void FinishCinematic();
	void HandleAnyKey(FKey Key);
	void ApplyView(const FVector& Location, const FRotator& Rotation, float FieldOfView);

private:

	TWeakObjectPtr<APlayerController> PlayerController;
	TWeakObjectPtr<APawn> PlayerPawn;

	float Elapsed = 0.0f;
	bool bStarted = false;
	bool bFinished = false;
	bool bBlendStarted = false;
};
