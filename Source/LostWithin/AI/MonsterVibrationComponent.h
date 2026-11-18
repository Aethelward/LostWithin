// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Gameplay/LostWithinNoiseSubsystem.h"
#include "MonsterVibrationComponent.generated.h"

class ACharacter;
class UMonsterAIComponent;

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
	Searching      /** Scanning the sensed area before resuming normal roaming. */
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

	/** Minimum horizontal speed (cm/s) for a moving character to generate sprint floor tremors. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Senses|Vibration", meta = (ClampMin = "0.0", Units = "cm/s"))
	float SprintSpeedThreshold = 450.0f;

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

	/** Acceptance radius from the target location that counts as arrived. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Senses|Investigation", meta = (ClampMin = "10.0", Units = "cm"))
	float AcceptanceRadius = 100.0f;

	/** Duration (seconds) spent searching and scanning the area at the vibration location. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Senses|Investigation", meta = (ClampMin = "0.0", Units = "s"))
	float SearchDuration = 4.0f;

	/** Half-angle of the scanning sweep when searching at the arrival spot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Senses|Investigation", meta = (ClampMin = "0.0", ClampMax = "180.0", Units = "deg"))
	float SearchSweepAngle = 45.0f;

	/** Max time before giving up navigation and searching where standing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Senses|Investigation", meta = (ClampMin = "1.0", Units = "s"))
	float GiveUpTime = 15.0f;

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

private:

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
};
