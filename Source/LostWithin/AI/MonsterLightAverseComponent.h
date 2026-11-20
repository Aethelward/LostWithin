// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "MonsterLightAverseComponent.generated.h"

class ACharacter;
class AAIController;
class ALostWithinCharacter;
class USpotLightComponent;
class UMonsterAIComponent;
class UMonsterHearingComponent;
class UPointLightComponent;
class USoundAttenuation;
class USoundBase;

UENUM(BlueprintType)
enum class EGhoulLightState : uint8
{
	/** In darkness: stalks aggressively toward player or roams in shadows */
	DarkStalking UMETA(DisplayName="Dark Stalking"),
	/** Shone on by flashlight: startled recoil and face shield */
	Startled UMETA(DisplayName="Startled"),
	/** Actively retreating away from flashlight cone into shadows */
	Retreating UMETA(DisplayName="Retreating"),
	/** Flashlight beam left: recovering before resuming aggressive dark stalking */
	Recovering UMETA(DisplayName="Recovering")
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnGhoulLightRepelled, float, LightIntensity);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnGhoulLightRecovered);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnGhoulDarkStalkChanged, bool, bIsStalking);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnGhoulTorchSnuffed, FVector, TorchLocation);

/**
 * Manages light-averse / shadow stalker behavior for BP_Ghoul.
 * When the player's flashlight cone shines directly on the Ghoul, it gets startled, shields its face,
 * and retreats away from the beam into darkness, suspending normal AI roaming.
 * In unlit areas, it stalks aggressively with increased speed.
 */
UCLASS(ClassGroup=(AI), meta=(BlueprintSpawnableComponent))
class LOSTWITHIN_API UMonsterLightAverseComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMonsterLightAverseComponent();

	/** Minimum effective light intensity (candelas) required to trigger repulsion */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Light Aversion|Detection", meta=(ClampMin="0.0"))
	float LightToleranceThreshold = 0.5f;

	/** Vertical offset from actor location when tracing light intersection */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Light Aversion|Detection", meta=(Units="cm"))
	float DetectionTraceOffsetZ = 45.0f;

	/** Bone or socket on skeletal mesh to test for light illumination (e.g. head) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Light Aversion|Detection")
	FName HeadSocketOrBoneName = TEXT("head");

	/** If true, checks line of sight raycast between flashlight and monster */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Light Aversion|Detection")
	bool bRequireLineOfSight = true;

	/** Collision channel used for line-of-sight raycasts */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Light Aversion|Detection")
	TEnumAsByte<ECollisionChannel> LightTraceChannel = ECC_Visibility;

	/** Flinch before the retreat starts; 0 (default) retreats in the same frame the beam hits */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Light Aversion|Repulsion", meta=(ClampMin="0.0", Units="s"))
	float StartledDuration = 0.0f;

	/** Distance to retreat away from the flashlight cone */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Light Aversion|Repulsion", meta=(ClampMin="100.0", Units="cm"))
	float RetreatDistance = 750.0f;

	/** Speed multiplier applied to base walk speed when retreating from light */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Light Aversion|Repulsion", meta=(ClampMin="1.0", ClampMax="3.0"))
	float RetreatSpeedMultiplier = 1.5f;

	/** Explicit retreat speed (cm/s); if > 0, overrides RetreatSpeedMultiplier */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Light Aversion|Repulsion", meta=(ClampMin="0.0", Units="cm/s"))
	float RetreatGaitSpeed = 450.0f;

	/** Pause in darkness after the beam leaves before the chase resumes; 0 (default) resumes in the same frame */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Light Aversion|Repulsion", meta=(ClampMin="0.0", Units="s"))
	float RecoveryDuration = 0.0f;

	/** Wall torch light (ULostWithinLightSubsystem) repels the Ghoul like the flashlight: it retreats out of it */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Light Aversion|Torches")
	bool bTorchLightRepels = true;

	/** While chasing, the Ghoul stops when its path's next stretch this far ahead would be torch-lit */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Light Aversion|Torches", meta=(ClampMin="0.0", Units="cm"))
	float TorchEdgeLookAhead = 90.0f;

	/** Whether aggressive dark stalking is enabled in unlit areas */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Light Aversion|Dark Stalking")
	bool bEnableAggressiveDarkStalking = true;

	/** Speed multiplier while stalking in darkness */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Light Aversion|Dark Stalking", meta=(ClampMin="1.0", ClampMax="3.0"))
	float DarkStalkSpeedMultiplier = 1.35f;

	/** Distance at which Ghoul detects and stalks player in darkness */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Light Aversion|Dark Stalking", meta=(ClampMin="100.0", Units="cm"))
	float DarkStalkDetectionRadius = 1500.0f;

	/** Explicit dark stalk speed (cm/s); if > 0, overrides DarkStalkSpeedMultiplier */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Light Aversion|Dark Stalking", meta=(ClampMin="0.0", Units="cm/s"))
	float DarkStalkApproachSpeed = 360.0f;

	/** The Ghoul is blind: the dark chase starts only from a heard player footstep and heads for where it was heard.
	 *  False restores the older chase of any player within DarkStalkDetectionRadius. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Light Aversion|Dark Stalking")
	bool bDarkStalkRequiresHearing = true;

	/** With bDarkStalkRequiresHearing: the chase ends after this long without hearing the player again. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Light Aversion|Dark Stalking", meta=(ClampMin="0.0", Units="s"))
	float DarkStalkHearingMemory = 6.0f;

	/** With bDarkStalkRequiresHearing: chase speeds, one step up every DarkStalkSpeedStepInterval of the chase.
	 *  Empty uses DarkStalkApproachSpeed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Light Aversion|Dark Stalking")
	TArray<float> DarkStalkSpeedSteps = { 360.0f, 420.0f, 480.0f };

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Light Aversion|Dark Stalking", meta=(ClampMin="0.1", Units="s"))
	float DarkStalkSpeedStepInterval = 3.0f;

	/** While held at a torch's edge, the Ghoul circles along the dark rim of its light toward its target. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Light Aversion|Torches")
	bool bOrbitTorchEdge = true;

	/** Held at the same torch's edge this long, the Ghoul puts that torch out. 0 never snuffs. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Light Aversion|Torches", meta=(ClampMin="0.0", Units="s"))
	float TorchSnuffHoldTime = 4.0f;

	/** How long a snuffed torch stays dark before it relights. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Light Aversion|Torches", meta=(ClampMin="0.0", Units="s"))
	float TorchSnuffDuration = 20.0f;

	/** Cold-draft inhale at the torch, PreSnuffLeadTime before it goes out. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Light Aversion|Audio")
	TObjectPtr<USoundBase> PreSnuffSound = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Light Aversion|Audio", meta=(ClampMin="0.0", Units="s"))
	float PreSnuffLeadTime = 1.5f;

	/** The torch going out. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Light Aversion|Audio")
	TObjectPtr<USoundBase> SnuffSound = nullptr;

	/** Head-snap hiss toward a player footstep it just heard (at most once per DarkStalkHearingMemory). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Light Aversion|Audio")
	TObjectPtr<USoundBase> HeardYouSound = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Light Aversion|Audio")
	TObjectPtr<USoundAttenuation> SoundAttenuation = nullptr;

	/** Current light aversion state */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Light Aversion|State")
	EGhoulLightState CurrentLightState = EGhoulLightState::DarkStalking;

	/** True when the flashlight cone is currently illuminating the Ghoul */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Light Aversion|State")
	bool bIsIlluminatedByFlashlight = false;

	/** True while a wall torch lights the Ghoul (repels it like the beam) */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Light Aversion|State")
	bool bIsTorchLit = false;

	/** True while the dark chase waits at the edge of torch light instead of stepping into it */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Light Aversion|State")
	bool bIsHoldingAtTorchEdge = false;

	/** True when shielding face from light (used by Animation Blueprint) */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Light Aversion|State")
	bool bIsShieldingFace = false;

	/** True while repelled by light (startled or retreating) */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Light Aversion|State")
	bool bIsRepelled = false;

	/** True while actively moving away from the light cone */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Light Aversion|State")
	bool bIsRetreating = false;

	/** True when in unlit areas and stalking aggressively */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Light Aversion|State")
	bool bIsStalkingInDarkness = true;

	/** Current computed effective light intensity reaching the Ghoul */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Light Aversion|State")
	float CurrentLightIntensity = 0.0f;

	/** Current retreat vector away from the light cone */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Light Aversion|State")
	FVector CurrentRetreatDirection = FVector::ZeroVector;

	/** Broadcast when flashlight repels the Ghoul */
	UPROPERTY(BlueprintAssignable, Category="Light Aversion|Events")
	FOnGhoulLightRepelled OnLightRepelled;

	/** Broadcast when Ghoul recovers in the dark */
	UPROPERTY(BlueprintAssignable, Category="Light Aversion|Events")
	FOnGhoulLightRecovered OnLightRecovered;

	/** Broadcast when entering or leaving dark stalking */
	UPROPERTY(BlueprintAssignable, Category="Light Aversion|Events")
	FOnGhoulDarkStalkChanged OnDarkStalkChanged;

	/** Broadcast when the Ghoul puts a wall torch out */
	UPROPERTY(BlueprintAssignable, Category="Light Aversion|Events")
	FOnGhoulTorchSnuffed OnTorchSnuffed;

	UFUNCTION(BlueprintPure, Category="Monster AI|Light Averse")
	bool IsLitByFlashlight() const { return bIsIlluminatedByFlashlight; }

	UFUNCTION(BlueprintPure, Category="Monster AI|Light Averse")
	bool IsShieldingFace() const { return bIsShieldingFace; }

	UFUNCTION(BlueprintPure, Category="Monster AI|Light Averse")
	bool IsRepelled() const { return bIsRepelled; }

	UFUNCTION(BlueprintPure, Category="Monster AI|Light Averse")
	bool IsRetreating() const { return bIsRetreating; }

	UFUNCTION(BlueprintPure, Category="Monster AI|Light Averse")
	bool IsStalkingInDarkness() const { return bIsStalkingInDarkness; }

	UFUNCTION(BlueprintPure, Category="Monster AI|Light Averse")
	float GetCurrentLightIntensity() const { return CurrentLightIntensity; }

	UFUNCTION(BlueprintPure, Category="Monster AI|Light Averse")
	EGhoulLightState GetLightState() const { return CurrentLightState; }

	UFUNCTION(BlueprintPure, Category="Monster AI|Light Averse")
	bool IsTorchLit() const { return bIsTorchLit; }

	UFUNCTION(BlueprintPure, Category="Monster AI|Light Averse")
	bool IsHoldingAtTorchEdge() const { return bIsHoldingAtTorchEdge; }

	/** True when a wall torch lights any of the Ghoul's sample points; OutTorchLocation receives that torch light */
	bool EvaluateTorchExposure(FVector& OutTorchLocation) const;

	/** True when the stretch ahead toward Target (TorchEdgeLookAhead along it) is torch-lit */
	bool WouldStepIntoTorchLight(const FVector& Target) const;

	/** True when Actor has a light averse component that is repelled by light */
	UFUNCTION(BlueprintPure, Category="Monster AI|Light Averse")
	static bool IsActorLightRepelled(const AActor* Actor);

	/** True while the dark chase is running toward the player (the Blueprint roam loop waits meanwhile) */
	UFUNCTION(BlueprintPure, Category="Monster AI|Light Averse")
	bool IsChasingPlayer() const { return bChasingPlayer; }

	/** True when Actor has a light averse component that is chasing the player in darkness */
	static bool IsActorDarkStalkingPlayer(const AActor* Actor);

	/** True when Actor's dark chase is driven by its hearing (the hearing component hands player footsteps to it) */
	static bool IsActorHuntingByEar(const AActor* Actor);

	/** Where the hearing-driven chase is heading (the last heard player footstep) */
	FVector GetHeardChaseTarget() const { return HeardTarget; }

	/** Seconds since the Ghoul last heard the player (large when never) */
	float GetSecondsSinceHeardPlayer() const { return SecondsSinceHeard; }

	/** Seconds held at the current torch's edge */
	float GetTorchEdgeHoldSeconds() const { return EdgeHoldSeconds; }

	/** Dark chase speed right now (after its steps) */
	float GetCurrentDarkStalkSpeed() const;

	/** Calculates flashlight cone intersection, distance attenuation, and intensity from player */
	UFUNCTION(BlueprintCallable, Category="Monster AI|Light Averse")
	bool EvaluateFlashlightExposure(const ALostWithinCharacter* PlayerChar, float& OutIntensity, FVector& OutBeamDirection);

	/** Manually triggers light repulsion reaction */
	UFUNCTION(BlueprintCallable, Category="Monster AI|Light Averse")
	void TriggerRepulsion(const FVector& LightOrigin, const FVector& LightForward, float Intensity);

	/** Clears light repulsion and returns to dark state */
	UFUNCTION(BlueprintCallable, Category="Monster AI|Light Averse")
	void StopRepulsion();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void RegisterComponentTickFunctions(bool bRegister) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	ALostWithinCharacter* FindPlayerCharacter() const;
	UMonsterAIComponent* GetMonsterAI() const;
	bool IsReactingToCat() const;
	FVector ComputeRetreatDirection(const FVector& LightPos, const FVector& LightFwd) const;

	void UpdateExposureAndRepulsion(float DeltaTime);
	void UpdateRetreatMovement(float DeltaTime);
	void UpdateDarkStalking(float DeltaTime, ALostWithinCharacter* PlayerChar);

	void ApplyRetreatSpeed();
	void ApplyDarkStalkSpeed();
	/** Updates the heard target and the time since the last heard player footstep */
	void UpdateHearingMemory(float DeltaTime);
	/** Holding at a torch's edge: counts toward the snuff and circles the rim */
	void UpdateTorchEdgeHold(float DeltaTime, const FVector& ChaseTarget);
	void PlaySoundAt(USoundBase* Sound, const FVector& Location) const;
	void RestoreWalkSpeed();
	/** Cancels the current path move (chase or retreat) so a light change takes effect in the same frame */
	void StopOwnerMove() const;
	void EndChase();
	void EndRepulsionNow();

	float StateElapsed = 0.0f;
	float RepathTimer = 0.0f;
	bool bSpeedApplied = false;
	float SavedMaxWalkSpeed = 0.0f;
	bool bDirectMovementFallback = false;
	bool bChasingPlayer = false;

	int32 LastHeardCount = 0;
	bool bHasHeardTarget = false;
	FVector HeardTarget = FVector::ZeroVector;
	FVector LastRequestedTarget = FVector::ZeroVector;
	float SecondsSinceHeard = 1.0e6f;
	float HeardChaseSeconds = 0.0f;
	float EdgeHoldSeconds = 0.0f;
	bool bPreSnuffPlayed = false;
	TWeakObjectPtr<UPointLightComponent> EdgeTorch;
};
