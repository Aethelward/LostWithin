// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Gameplay/LostWithinNoiseSubsystem.h"
#include "MonsterVibrationComponent.generated.h"

class ACharacter;
class UMonsterAIComponent;
class UAnimMontage;
class UAnimSequenceBase;
class USoundAttenuation;
class USoundBase;

/** What kind of tremor or displacement triggered the vibration sensor. */
UENUM(BlueprintType)
enum class EMonsterVibrationSenseKind : uint8
{
	None,
	SprintTremor,       /** Heavy floor vibration caused by a sprinting character. */
	LandingTremor,      /** Heavy floor vibration caused by landing from a jump or fall. */
	ProximityTurbulence /** Close-range air or body displacement from a moving character. */
};

/** Investigation phase for a monster guided by vibration/turbulence. */
UENUM(BlueprintType)
enum class EMonsterVibrationState : uint8
{
	Idle,          /** Roaming or waiting; not responding to a tremor. */
	Investigating, /** Navigating toward the sensed tremor or turbulence location. */
	Searching,     /** Scanning the sensed area before resuming normal roaming. */
	Coiling,       /** Planted and cranking its casing open before a lunge; stillness releases it. */
	Lunging,       /** Launched along a locked straight line; it cannot steer. */
	Jammed         /** Slammed or whiffed: latches jammed, senses nothing until it re-compresses. */
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnMonsterVibrationSensed, FVector, SensedLocation, EMonsterVibrationSenseKind, Kind);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMonsterVibrationStateChanged, EMonsterVibrationState, NewState);

/**
 *  Implements blind and deaf sensing mechanics for BP_Monster:
 *  - Blind: Cannot see the player; ignores flashlight beams and visual sight cones.
 *  - Deaf to airborne sounds: Has no MonsterHearingComponent and ignores airborne noises (e.g. dropped items).
 *  - Floor vibrations: Senses ground tremors from sprinting and jumping/landing within range when touching the floor.
 *  - Air turbulence: Senses close-range body displacement if a nearby character moves (even sneaking/crouching),
 *    allowing the player to survive by freezing completely still when the monster is near.
 */
UCLASS(ClassGroup=(AI), meta=(BlueprintSpawnableComponent))
class LOSTWITHIN_API UMonsterVibrationComponent : public UActorComponent
{
	GENERATED_BODY()

public:

	UMonsterVibrationComponent();

	/** If true, this monster ignores visual perception and player sight cones. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Senses|Vibration")
	bool bBlind = true;

	/** If true, airborne noises (like dropped items or distant objects) are ignored; only floor tremors and turbulence are sensed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Senses|Vibration")
	bool bDeafToAirborne = true;

	/** If true, sets PlayerDetectionRadius on owning MonsterAIComponent to 0 on BeginPlay to enforce complete visual blindness. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Senses|Vibration")
	bool bEnforceBlindnessOnAI = true;

	/** Maximum distance to detect floor tremors from sprinting. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Senses|Vibration", meta = (ClampMin = "0.0", Units = "cm"))
	float SprintVibrationRadius = 1200.0f;

	/** Maximum distance to detect floor tremors from landing after a jump or fall. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Senses|Vibration", meta = (ClampMin = "0.0", Units = "cm"))
	float LandingVibrationRadius = 1400.0f;

	/** Minimum horizontal speed (cm/s) for a moving character to generate sprint floor tremors. Above the 500 cm/s run, so
	 *  only a sprint shakes the floor. A footstep noise counts as a tremor when its radius reaches SprintVibrationRadius. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Senses|Vibration", meta = (ClampMin = "0.0", Units = "cm/s"))
	float SprintSpeedThreshold = 600.0f;

	/** If true, floor tremors require both the vibration source and the monster to be on or very near the floor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Senses|Vibration")
	bool bRequireFloorContact = true;

	/** Maximum vertical (Z) distance between monster and vibration source for floor tremors to conduct. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Senses|Vibration", meta = (ClampMin = "0.0", Units = "cm"))
	float MaxFloorZDifference = 250.0f;

	/** Radius within which any character movement displaces air and causes body turbulence. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Senses|Turbulence", meta = (ClampMin = "0.0", Units = "cm"))
	float TurbulenceRadius = 300.0f;

	/** Minimum speed in close proximity that generates detectable air turbulence. A player standing still generates 0 turbulence. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Senses|Turbulence", meta = (ClampMin = "0.0", Units = "cm/s"))
	float TurbulenceSpeedThreshold = 15.0f;

	/** Movement speed when heading toward a vibration. 0 uses the cat panic speed or FallbackSpeedMultiplier * MaxWalkSpeed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Senses|Investigation", meta = (ClampMin = "0.0", Units = "cm/s"))
	float InvestigateSpeed = 0.0f;

	/** Multiplier for normal walk speed during vibration investigation if InvestigateSpeed is 0. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Senses|Investigation", meta = (ClampMin = "1.0", ClampMax = "4.0"))
	float FallbackSpeedMultiplier = 1.5f;

	/** Distance from the monster's centre to the vibration source that counts as arrived (walks onto the source). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Senses|Investigation", meta = (ClampMin = "0.0", Units = "cm"))
	float AcceptanceRadius = 25.0f;

	/** Duration (seconds) spent searching and scanning the area at the vibration location. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Senses|Investigation", meta = (ClampMin = "0.0", Units = "s"))
	float SearchDuration = 4.0f;

	/** Half-angle of the scanning sweep when searching at the arrival spot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Senses|Investigation", meta = (ClampMin = "0.0", ClampMax = "180.0", Units = "deg"))
	float SearchSweepAngle = 45.0f;

	/** Max time before giving up navigation and searching where standing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Senses|Investigation", meta = (ClampMin = "1.0", Units = "s"))
	float GiveUpTime = 15.0f;

	/** A sense this close sets off the coil and lunge instead of a walk. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Senses|Lunge")
	bool bEnableLunge = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Senses|Lunge", meta = (ClampMin = "0.0", Units = "cm"))
	float LungeTriggerRange = 750.0f;

	/** The coil lasts a random time in this range (seconds). With no new sense during it, the coil is released. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Senses|Lunge")
	FVector2D CoilDurationRange = FVector2D(0.55, 0.7);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Senses|Lunge", meta = (ClampMin = "0.0", Units = "cm/s"))
	float LungeSpeed = 1000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Senses|Lunge", meta = (ClampMin = "0.05", Units = "s"))
	float LungeDuration = 0.7f;

	/** The lunge counts as slammed into a wall when its speed drops below this fraction of LungeSpeed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Senses|Lunge", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float WallSlamSpeedRatio = 0.4f;

	/** After every lunge (slam or whiff) it is jammed for a random time in this range (seconds). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Senses|Lunge")
	FVector2D JamDurationRange = FVector2D(2.5, 3.0);

	/** One clip for the whole attack, played through LungeAnimSlot: its crouch plays over the coil, its leap over the
	 *  lunge and its stumble over the jam, each part time-stretched to its state. Default: the Lunger's 000038 lunge
	 *  (_InPlace copy: the code moves the capsule). Empty plays nothing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Senses|Lunge|Animation")
	TSoftObjectPtr<UAnimSequenceBase> LungeAnimation;

	/** Animation Blueprint slot the attack clip plays in (ABP_Monster: DefaultSlot before the output pose). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Senses|Lunge|Animation")
	FName LungeAnimSlot = TEXT("DefaultSlot");

	/** Clip time where the crouch ends and the leap starts (000038: the hips are lowest at 0.46 s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Senses|Lunge|Animation", meta = (ClampMin = "0.0", Units = "s"))
	float LungeAnimLeapStart = 0.46f;

	/** Clip time where the leap lands and the stumble starts (000038: the feet touch down at 1.67 s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Senses|Lunge|Animation", meta = (ClampMin = "0.0", Units = "s"))
	float LungeAnimLeapEnd = 1.67f;

	/** Steel ratchet as the coil starts (the 0.6 s telegraph). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Senses|Lunge|Audio")
	TObjectPtr<USoundBase> CoilSound = nullptr;

	/** Concrete clang and re-latch clicks when the lunge hits a wall. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Senses|Lunge|Audio")
	TObjectPtr<USoundBase> SlamSound = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Senses|Lunge|Audio")
	TObjectPtr<USoundAttenuation> SoundAttenuation = nullptr;

	/** Fired when a floor tremor or air turbulence is sensed. */
	UPROPERTY(BlueprintAssignable, Category = "Senses|Vibration")
	FOnMonsterVibrationSensed OnVibrationSensed;

	/** Fired when investigation state changes. */
	UPROPERTY(BlueprintAssignable, Category = "Senses|Vibration")
	FOnMonsterVibrationStateChanged OnStateChanged;

	/** Current investigation state. */
	UPROPERTY(BlueprintReadOnly, Category = "Senses|Vibration")
	EMonsterVibrationState State = EMonsterVibrationState::Idle;

	/** Kind of tremor or turbulence currently being investigated. */
	UPROPERTY(BlueprintReadOnly, Category = "Senses|Vibration")
	EMonsterVibrationSenseKind LastSenseKind = EMonsterVibrationSenseKind::None;

	/** Location of the sensed vibration. */
	UPROPERTY(BlueprintReadOnly, Category = "Senses|Vibration")
	FVector InvestigationLocation = FVector::ZeroVector;

	/** True if the monster is currently investigating or searching. */
	UFUNCTION(BlueprintPure, Category = "Senses|Vibration")
	bool IsInvestigating() const { return State != EMonsterVibrationState::Idle; }

	/** Straight-line direction locked for the current lunge. */
	FVector GetLockedLungeDirection() const { return LockedDirection; }

	/** True when the last lunge ended against a wall (not a whiff). */
	bool DidLastLungeSlam() const { return bLastLungeSlammed; }

	/** True if the monster has arrived and is searching. */
	UFUNCTION(BlueprintPure, Category = "Senses|Vibration")
	bool IsSearching() const { return State == EMonsterVibrationState::Searching; }

	/** Evaluates whether a floor vibration at the given location can be sensed. */
	UFUNCTION(BlueprintPure, Category = "Senses|Vibration")
	bool CanSenseFloorVibration(const FVector& Location, float Radius, const AActor* InstigatorActor = nullptr) const;

	/** Evaluates whether close-range air turbulence from the given actor can be sensed. */
	UFUNCTION(BlueprintPure, Category = "Senses|Vibration")
	bool CanSenseTurbulence(const AActor* PotentialActor) const;

	/** Directly trigger floor vibration sensing (e.g. from sprint footstep or landing). */
	UFUNCTION(BlueprintCallable, Category = "Senses|Vibration")
	bool SenseFloorVibration(const FVector& Location, float Radius, AActor* InstigatorActor = nullptr);

	/** Directly trigger air/body turbulence sensing. */
	UFUNCTION(BlueprintCallable, Category = "Senses|Vibration")
	bool SenseTurbulence(const FVector& Location, AActor* InstigatorActor = nullptr);

	/** Stops any current investigation and restores walk speed. */
	UFUNCTION(BlueprintCallable, Category = "Senses|Vibration")
	void StopInvestigating();

	/** Helper to check whether an actor has an active vibration investigation. */
	UFUNCTION(BlueprintPure, Category = "Senses|Vibration")
	static bool IsActorInvestigating(const AActor* Actor);

	/** Helper to find the vibration component on an actor. */
	UFUNCTION(BlueprintPure, Category = "Senses|Vibration")
	static UMonsterVibrationComponent* FindVibrationComponent(const AActor* Actor);

	/** Static helper to broadcast a vibration tremor to all vibration monsters in the world. */
	UFUNCTION(BlueprintCallable, Category = "Senses|Vibration", meta = (WorldContext = "WorldContextObject"))
	static int32 ReportVibration(const UObject* WorldContextObject, FVector Location, float Radius,
		EMonsterVibrationSenseKind Kind, AActor* Instigator = nullptr);

	/** Handler for noise events broadcast by ULostWithinNoiseSubsystem. */
	void HandleNoiseBroadcast(const FLostWithinNoiseEvent& Noise);

protected:

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void RegisterComponentTickFunctions(bool bRegister) override;

private:

	/** Starts the coil when the sense is close enough, else walks to it; shared by both senses. */
	void ReactToSense();
	void BeginCoil();
	void UpdateCoiling(float DeltaTime);
	void BeginLunge();
	void UpdateLunging(float DeltaTime);
	void BeginJam(bool bSlammed);
	void UpdateJammed(float DeltaTime);
	void RestoreLungeMovement();
	void PlaySoundAt(USoundBase* Sound) const;
	/** Starts the attack clip at its crouch, stretched over Duration. */
	void StartLungeAnim(float Duration);
	/** Jumps the attack clip to ClipTime and plays from there so that ClipEnd arrives after Duration. */
	void SetLungeAnimPhase(float ClipTime, float ClipEnd, float Duration);
	void StopLungeAnim(float BlendOut);

	UMonsterAIComponent* GetMonsterAI() const;
	bool IsReactingToCat() const;
	void CheckPlayerProximityAndMovement();
	void ApplyInvestigateSpeed();
	void RestoreWalkSpeed();
	float ResolveInvestigateSpeed(const ACharacter* Character) const;
	void IssueMove();
	void StopOwnerMovement() const;
	bool HasArrived() const;
	void BeginSearch();
	void UpdateMoving(float DeltaTime);
	void UpdateSearching(float DeltaTime);
	void Finish(bool bRestoreSpeed);
	void SetState(EMonsterVibrationState NewState);

	FDelegateHandle NoiseBroadcastHandle;
	float StateElapsed = 0.0f;
	float SavedMaxWalkSpeed = 0.0f;
	float SearchBaseYaw = 0.0f;
	bool bSpeedApplied = false;
	bool bUseDirectMovement = false;
	bool bMoveRequested = false;

	/** Tracking for character landing tremors via character movement state. */
	bool bLastPlayerWasFalling = false;

	float PhaseDuration = 0.0f;
	bool bFreshSenseDuringCoil = false;
	FVector LockedDirection = FVector::ZeroVector;
	bool bLungeMovementApplied = false;
	float LungeSavedMaxWalkSpeed = 0.0f;
	float LungeSavedMaxAcceleration = 0.0f;
	bool bLastLungeSlammed = false;
	TWeakObjectPtr<UAnimMontage> LungeMontage;
};
