// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MonsterScentComponent.generated.h"

class ACharacter;
class ALostWithinCharacter;
class USoundAttenuation;
class USoundBase;
class UMonsterAIComponent;

UENUM(BlueprintType)
enum class EMonsterScentState : uint8
{
	/** No scent in range: the Blueprint roam loop runs. */
	Idle,
	/** Following the player's trail from older to fresher nodes. */
	Trailing,
	/** The trail broke (torch smoke, a gap, its end): sniffing around for it. */
	Casting,
	/** Heading for a dropped item that carries the player's scent. */
	Decoy,
	/** Smells the player directly (close by path) and runs at it. */
	AirLock
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMonsterScentStateChanged, EMonsterScentState, NewState);

/**
 *  The BoneDog's nose. It follows the player's scent trail (ULostWithinScentSubsystem) along the route the player
 *  walked, goes for dropped items that carry the player's scent, loses the trail where torch smoke or a gap breaks
 *  it, and runs straight at the player once it smells them close by path. It never hears; its eyes reach only a few
 *  metres (UMonsterLightAttractionComponent, which takes over while it sees the player). While tracking,
 *  UMonsterAIComponent::CanRunNormalAI is false, so the roam loop waits; a cat reaction always wins.
 */
UCLASS(ClassGroup=(AI), meta=(BlueprintSpawnableComponent))
class LOSTWITHIN_API UMonsterScentComponent : public UActorComponent
{
	GENERATED_BODY()

public:

	UMonsterScentComponent();

	/** Picks up any trail node or scented item this close to its nose. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scent", meta = (ClampMin = "0.0", Units = "cm"))
	float NoseRange = 1200.0f;

	/** Smells the player directly within this path distance and runs at them. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scent", meta = (ClampMin = "0.0", Units = "cm"))
	float AirScentRange = 600.0f;

	/** Walking speed along the trail, to a decoy and while casting. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scent", meta = (ClampMin = "0.0", Units = "cm/s"))
	float TrailSpeed = 300.0f;

	/** Speed while it smells the player directly; 0 uses the cat-reaction PanicGaitSpeed (its fastest gait). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scent", meta = (ClampMin = "0.0", Units = "cm/s"))
	float AirLockSpeed = 0.0f;

	/** A node this close counts as reached. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scent|Trail", meta = (ClampMin = "10.0", Units = "cm"))
	float NodeReachRadius = 90.0f;

	/** Consecutive trail nodes farther apart than this break the trail (torch smoke, a gap). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scent|Trail", meta = (ClampMin = "0.0", Units = "cm"))
	float TrailGap = 400.0f;

	/** While casting it only picks up nodes this close, at up to CastPoints spots around where the trail broke. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scent|Cast", meta = (ClampMin = "0.0", Units = "cm"))
	float CastRadius = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scent|Cast", meta = (ClampMin = "0.0", Units = "s"))
	float CastDuration = 6.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scent|Cast", meta = (ClampMin = "1", ClampMax = "8"))
	int32 CastPoints = 3;

	/** Seconds between decisions; movement continues between them. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scent", meta = (ClampMin = "0.0", Units = "s"))
	float ThinkInterval = 0.2f;

	/** Played while trailing: slow when the scent under its nose is weak, fast when fresh. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scent|Audio")
	TArray<TObjectPtr<USoundBase>> SniffSounds;

	/** Played once when it smells the player directly. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scent|Audio")
	TObjectPtr<USoundBase> AirLockSound = nullptr;

	/** Played when it loses the trail or a decoy comes up empty. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scent|Audio")
	TObjectPtr<USoundBase> LostTrailSound = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scent|Audio")
	TObjectPtr<USoundAttenuation> SoundAttenuation = nullptr;

	UPROPERTY(BlueprintAssignable, Category = "Scent")
	FOnMonsterScentStateChanged OnScentStateChanged;

	UFUNCTION(BlueprintPure, Category = "Scent")
	EMonsterScentState GetScentState() const { return State; }

	/** True while it follows any scent (trail, cast, decoy or the player). */
	UFUNCTION(BlueprintPure, Category = "Scent")
	bool IsTracking() const { return State != EMonsterScentState::Idle; }

	/** Trail node it is heading for (0 when none). */
	int32 GetCurrentNodeId() const { return CurrentNodeId; }

	/** The decoy item it is heading for. */
	AActor* GetDecoyTarget() const { return DecoyTarget.Get(); }

	/** True when it has already checked Item and ignores it now. */
	bool IsIgnoringItem(const AActor* Item) const;

	/** The point it is moving to now. */
	FVector GetMoveTarget() const { return MoveTarget; }

	/** Runs one decision now (tests). */
	void Think();

	/** Drops every scent target and lets the roam loop run again. */
	UFUNCTION(BlueprintCallable, Category = "Scent")
	void StopTracking();

	/** Lets go before a cat reaction caches the movement settings: restores the walk speed, keeps the flee move. */
	void ReleaseForCatReaction();

	/** True when Actor's nose is tracking any scent. */
	static bool IsActorTrackingScent(const AActor* Actor);

protected:

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:

	UMonsterAIComponent* GetMonsterAI() const;
	bool IsReactingToCat() const;
	bool SmellsPlayerDirectly(const ALostWithinCharacter* Player) const;
	void SetState(EMonsterScentState NewState);
	void StartTrail(int32 NodeId);
	void StartCast();
	void LoseTrail();
	void UpdateTrail();
	void UpdateCast();
	void UpdateDecoy();
	void MoveTo(const FVector& Target, bool bForce = false);
	void ChaseActor(AActor* Target);
	void ApplySpeed(float Speed);
	void RestoreSpeed();
	void StopOwnerMovement() const;
	void PlaySniff(float DeltaTime);
	void PlayOneShot(USoundBase* Sound) const;

	EMonsterScentState State = EMonsterScentState::Idle;
	int32 CurrentNodeId = 0;
	/** Nodes up to this id are spent (a trail it lost). */
	int32 IgnoreNodesUpToId = 0;
	TWeakObjectPtr<AActor> DecoyTarget;
	TSet<TWeakObjectPtr<AActor>> IgnoredItems;

	FVector MoveTarget = FVector::ZeroVector;
	bool bHasMoveTarget = false;
	bool bUseDirectMovement = false;
	float SinceMoveRequest = 0.0f;

	FVector CastOrigin = FVector::ZeroVector;
	int32 CastIndex = 0;
	float CastElapsed = 0.0f;

	float ThinkTimer = 0.0f;
	float SniffTimer = 0.0f;
	bool bSpeedApplied = false;
	float SavedMaxWalkSpeed = 0.0f;
};
