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

	/** Speed while heading to a noise. 0 uses the cat-reaction PanicGaitSpeed (the fastest gait clip's speed). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearing|Investigate", meta = (ClampMin = "0.0", Units = "cm/s"))
	float InvestigateSpeed = 0.0f;

	/** Used when neither InvestigateSpeed nor PanicGaitSpeed is set: current MaxWalkSpeed times this. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearing|Investigate", meta = (ClampMin = "1.0", ClampMax = "4.0"))
	float FallbackSpeedMultiplier = 1.5f;

	/** Distance from the noise (beyond the capsule) that counts as arrived. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearing|Investigate", meta = (ClampMin = "10.0", Units = "cm"))
	float AcceptanceRadius = 90.0f;

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

	/** Ends any investigation and restores the walk speed. */
	UFUNCTION(BlueprintCallable, Category = "Hearing")
	void StopInvestigating();

	/** True when Actor has a hearing component that is investigating. */
	static bool IsActorInvestigating(const AActor* Actor);

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

	EInvestigationState State = EInvestigationState::Idle;
	FVector InvestigationLocation = FVector::ZeroVector;
	float StateElapsed = 0.0f;
	float SearchBaseYaw = 0.0f;
	bool bUseDirectMovement = false;
	bool bMoveRequested = false;
	bool bSpeedApplied = false;
	float SavedMaxWalkSpeed = 0.0f;
};
