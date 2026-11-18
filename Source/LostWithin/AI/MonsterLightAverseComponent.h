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

	/** Duration of startled flinch before movement begins */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Light Aversion|Repulsion", meta=(ClampMin="0.0", Units="s"))
	float StartledDuration = 0.4f;

	/** Distance to retreat away from the flashlight cone */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Light Aversion|Repulsion", meta=(ClampMin="100.0", Units="cm"))
	float RetreatDistance = 750.0f;

	/** Speed multiplier applied to base walk speed when retreating from light */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Light Aversion|Repulsion", meta=(ClampMin="1.0", ClampMax="3.0"))
	float RetreatSpeedMultiplier = 1.5f;

	/** Explicit retreat speed (cm/s); if > 0, overrides RetreatSpeedMultiplier */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Light Aversion|Repulsion", meta=(ClampMin="0.0", Units="cm/s"))
	float RetreatGaitSpeed = 450.0f;

	/** Time spent recovering in darkness after beam leaves before returning to stalking */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Light Aversion|Repulsion", meta=(ClampMin="0.1", Units="s"))
	float RecoveryDuration = 1.5f;

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

	/** Current light aversion state */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Light Aversion|State")
	EGhoulLightState CurrentLightState = EGhoulLightState::DarkStalking;

	/** True when the flashlight cone is currently illuminating the Ghoul */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Light Aversion|State")
	bool bIsIlluminatedByFlashlight = false;

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

	/** True when Actor has a light averse component that is repelled by light */
	UFUNCTION(BlueprintPure, Category="Monster AI|Light Averse")
	static bool IsActorLightRepelled(const AActor* Actor);

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
	void RestoreWalkSpeed();

	float StateElapsed = 0.0f;
	float RepathTimer = 0.0f;
	bool bSpeedApplied = false;
	float SavedMaxWalkSpeed = 0.0f;
	bool bDirectMovementFallback = false;
};
