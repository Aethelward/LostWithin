// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Gameplay/LostWithinNoiseSubsystem.h"
#include "MonsterHearingComponent.generated.h"

class ACharacter;
class UMonsterAIComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnMonsterHeardNoise, FVector, NoiseLocation, ELostWithinNoiseKind, Kind);

/**
 *  Lets a roaming monster hear player noises and investigate them: it hurries to the noise at its fastest gait,
 *  searches there, then hands control back to its Blueprint roam loop. While investigating,
 *  UMonsterAIComponent::CanRunNormalAI reports false, so guarded Blueprint moves wait and the Animation Blueprint
 *  plays the panic (fastest) gait. A cat reaction always wins over an investigation.
 */
UCLASS(ClassGroup=(AI), meta=(BlueprintSpawnableComponent))
class LOSTWITHIN_API UMonsterHearingComponent : public UActorComponent
{
	GENERATED_BODY()

public:

	UMonsterHearingComponent();

	/** Scales every noise radius for this monster (2 = hears twice as far). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearing", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float HearingRangeMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearing")
	bool bHearFootsteps = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearing")
	bool bHearItemImpacts = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearing")
	bool bHearOtherNoises = true;

	/** Minimum noise radius of a footstep required to alert this monster (e.g. filters out silent/crouch steps if set > 0). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearing", meta = (ClampMin = "0.0", Units = "cm"))
	float MinFootstepNoiseRadius = 0.0f;

	/** If true, item impact distractions (thrown/dropped items) take precedence and resist being cancelled by routine footsteps. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearing|Distraction")
	bool bPrioritizeItemDistractions = true;

	/** Distance ratio threshold for a footstep to interrupt an active item distraction (0.5 = footstep must be at least twice as close to the monster). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearing|Distraction", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DistractionInterruptDistanceRatio = 0.5f;

	/** Loudness radius threshold required for a footstep to qualify as an urgent sprint that can interrupt an item distraction. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearing|Distraction", meta = (ClampMin = "0.0", Units = "cm"))
	float SprintInterruptNoiseRadius = 1200.0f;

	/** Time spent thoroughly searching around an item distraction before resuming roaming. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearing|Distraction", meta = (ClampMin = "0.0", Units = "s"))
	float ItemDistractionSearchDuration = 5.0f;

	/** Speed while heading to a noise. 0 uses the cat-reaction PanicGaitSpeed (the fastest gait clip's speed). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearing|Investigate", meta = (ClampMin = "0.0", Units = "cm/s"))
	float InvestigateSpeed = 0.0f;

	/** Used when neither InvestigateSpeed nor PanicGaitSpeed is set: current MaxWalkSpeed times this. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearing|Investigate", meta = (ClampMin = "1.0", ClampMax = "4.0"))
	float FallbackSpeedMultiplier = 1.5f;

	/** Distance from the monster's centre to the noise that counts as arrived. Small, so it walks onto the source and
	 *  physically touches a player still standing there (it used to stop ~90 cm plus its capsule in front). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearing|Investigate", meta = (ClampMin = "0.0", Units = "cm"))
	float AcceptanceRadius = 25.0f;

	/** The monster cannot pinpoint a sound: it heads for a point off the true source, 40-100% of this away (cm), while
	 *  it stands in light (a torch or the flashlight). 0 (default) walks to the exact source. BoneDog: 200. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearing|Pinpoint", meta = (ClampMin = "0.0", Units = "cm"))
	float PinpointErrorInLight = 0.0f;

	/** As PinpointErrorInLight, while the monster stands in darkness. BoneDog: 500. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearing|Pinpoint", meta = (ClampMin = "0.0", Units = "cm"))
	float PinpointErrorInDark = 0.0f;

	/** Time spent looking around at the noise before roaming again. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearing|Investigate", meta = (ClampMin = "0.0", Units = "s"))
	float SearchDuration = 5.0f;

	/** Half-angle of the look-around sweep while searching. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearing|Investigate", meta = (ClampMin = "0.0", ClampMax = "180.0", Units = "deg"))
	float SearchSweepAngle = 60.0f;

	/** Gives up walking to a noise after this long and searches where it stands. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearing|Investigate", meta = (ClampMin = "1.0", Units = "s"))
	float GiveUpTime = 20.0f;

	/** Fired when a noise is accepted (new investigation or a redirect). */
	UPROPERTY(BlueprintAssignable, Category = "Hearing")
	FOnMonsterHeardNoise OnNoiseHeard;

	/** True when Noise is a kind this monster listens to and its radius reaches the owner. */
	UFUNCTION(BlueprintPure, Category = "Hearing")
	bool CanHear(const FLostWithinNoiseEvent& Noise) const;

	/** Starts or redirects an investigation; false when the noise is out of range, filtered or the cat has priority. */
	UFUNCTION(BlueprintCallable, Category = "Hearing")
	bool HearNoise(const FLostWithinNoiseEvent& Noise);

	/** True while moving to a noise or searching at it. */
	UFUNCTION(BlueprintPure, Category = "Hearing")
	bool IsInvestigating() const { return State != EInvestigationState::Idle; }

	UFUNCTION(BlueprintPure, Category = "Hearing")
	bool IsSearching() const { return State == EInvestigationState::Searching; }

	UFUNCTION(BlueprintPure, Category = "Hearing")
	FVector GetInvestigationLocation() const { return InvestigationLocation; }

	/** The pinpoint error used for the last accepted noise (0 = exact), and whether the monster was lit then. */
	float GetLastPinpointError() const { return LastPinpointError; }
	bool WasLitAtLastNoise() const { return bLitAtLastNoise; }

	UFUNCTION(BlueprintPure, Category = "Hearing")
	ELostWithinNoiseKind GetCurrentNoiseKind() const { return CurrentNoiseKind; }

	UFUNCTION(BlueprintPure, Category = "Hearing")
	bool IsInvestigatingDistraction() const { return IsInvestigating() && CurrentNoiseKind == ELostWithinNoiseKind::ItemImpact; }

	/** Ends any investigation and restores the walk speed. */
	UFUNCTION(BlueprintCallable, Category = "Hearing")
	void StopInvestigating();

	/** True when Actor has a hearing component that is investigating. */
	static bool IsActorInvestigating(const AActor* Actor);

	/** How many player footsteps this monster has heard (a counter; it changes on each new one). */
	int32 GetHeardPlayerCount() const { return HeardPlayerCount; }

	/** Where the last heard player footstep was made. */
	FVector GetLastHeardPlayerLocation() const { return LastHeardPlayerLocation; }

	/** True once any player footstep was heard. */
	bool HasHeardPlayer() const { return HeardPlayerCount > 0; }

protected:

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:

	enum class EInvestigationState : uint8
	{
		Idle,
		Moving,
		Searching
	};

	UMonsterAIComponent* GetMonsterAI() const;
	bool IsReactingToCat() const;
	float ResolveInvestigateSpeed(const ACharacter* Character) const;
	void ApplyInvestigateSpeed();
	void RestoreWalkSpeed();
	void IssueMove();
	void StopOwnerMovement() const;
	bool HasArrived() const;
	void BeginSearch();
	void UpdateMoving(float DeltaTime);
	void UpdateSearching(float DeltaTime);
	void Finish(bool bRestoreSpeed);

	int32 HeardPlayerCount = 0;
	FVector LastHeardPlayerLocation = FVector::ZeroVector;

	EInvestigationState State = EInvestigationState::Idle;
	ELostWithinNoiseKind CurrentNoiseKind = ELostWithinNoiseKind::Other;
	FVector InvestigationLocation = FVector::ZeroVector;
	float LastPinpointError = 0.0f;
	bool bLitAtLastNoise = false;

	/** Where to investigate a noise heard at Source: Source itself, or a point off it by the pinpoint error. */
	FVector ResolveInvestigationTarget(const FVector& Source);
	float StateElapsed = 0.0f;
	float SearchBaseYaw = 0.0f;
	bool bUseDirectMovement = false;
	bool bMoveRequested = false;
	bool bSpeedApplied = false;
	float SavedMaxWalkSpeed = 0.0f;
};
