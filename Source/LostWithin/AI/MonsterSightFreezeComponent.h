// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "MonsterSightFreezeComponent.generated.h"

class ACharacter;
class AAIController;
class ALostWithinCharacter;
class USpotLightComponent;
class UMonsterAIComponent;
class USoundAttenuation;
class USoundBase;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMonsterSightFreezeChanged, bool, bIsFrozen);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMonsterObservedChanged, bool, bIsObserved);

/**
 * Manages line-of-sight freezing / weeping stalker mechanics for BP_Doll.
 *
 * When the player looks directly at the Doll (within the camera frustum with unblocked
 * line-of-sight, especially when illuminated by the flashlight), the Doll completely freezes
 * in place: character movement is stopped immediately and animations are frozen.
 *
 * When the player looks away, turns around, or enters menus / the padlock dial inspection,
 * the Doll resumes creeping forward toward the player at its stalking pace.
 *
 * Sight is the Doll's only sense (it is deaf). With bDormantInDarkness it lies dormant while no light touches it,
 * and with bRequireSightOfPlayer it only rushes at a player it has seen lit (or the place it last saw them).
 * Holding the lit beam on it builds interference that blacks the flashlight out briefly, letting it advance.
 */
UCLASS(ClassGroup=(AI), meta=(BlueprintSpawnableComponent))
class LOSTWITHIN_API UMonsterSightFreezeComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMonsterSightFreezeComponent();

	// -------------------------------------------------------------------------
	// Detection & Frustum Settings
	// -------------------------------------------------------------------------

	/** Maximum distance (cm) at which the player can observe and freeze the Doll */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sight Freeze|Detection", meta=(ClampMin="100.0", Units="cm"))
	float MaxSightDistance = 3500.0f;

	/** Fallback horizontal half-angle (degrees) if camera FOV cannot be retrieved */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sight Freeze|Detection", meta=(ClampMin="10.0", ClampMax="90.0", Units="deg"))
	float FrustumHalfAngle = 50.0f;

	/** If true, dynamically uses the player camera's actual FOV angle */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sight Freeze|Detection")
	bool bUsePlayerCameraFOV = true;

	/** Extra margin added to camera half-FOV to prevent popping at peripheral screen edges */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sight Freeze|Detection", meta=(ClampMin="0.0", ClampMax="30.0", Units="deg"))
	float FOVMarginDegrees = 5.0f;

	/** If true, raycasts between camera and Doll must not be occluded by static/world geometry */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sight Freeze|Detection")
	bool bRequireLineOfSight = true;

	/** Collision channel used for line-of-sight occlusion raycasts */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sight Freeze|Detection")
	TEnumAsByte<ECollisionChannel> SightTraceChannel = ECC_Visibility;

	/** Bone or socket on the skeletal mesh to sample for head visibility */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sight Freeze|Detection")
	FName HeadSocketOrBoneName = TEXT("head");

	/** Vertical offset from actor location for chest/torso visibility sample point */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sight Freeze|Detection", meta=(Units="cm"))
	float DetectionTraceOffsetZ = 40.0f;

	// -------------------------------------------------------------------------
	// Illumination Settings
	// -------------------------------------------------------------------------

	/**
	 * If true, targets beyond UnilluminatedMaxDistance require flashlight illumination to freeze.
	 * Inside UnilluminatedMaxDistance, the player can discern the Doll in dim/ambient darkness.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sight Freeze|Illumination")
	bool bRequireIlluminationAtDistance = true;

	/** Maximum distance in unilluminated ambient darkness where the Doll can still be frozen */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sight Freeze|Illumination", meta=(ClampMin="50.0", Units="cm"))
	float UnilluminatedMaxDistance = 800.0f;

	/** Maximum distance when illuminated by the player's flashlight */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sight Freeze|Illumination", meta=(ClampMin="100.0", Units="cm"))
	float IlluminatedMaxDistance = 3500.0f;

	/**
	 * If true (default), the Doll freezes only while the player watches it AND a watched point of it is lit, by the
	 * flashlight beam or (with bTorchLightCounts) a wall torch. Watched in the dark, or lit but unwatched, it moves.
	 * False restores the distance-based watched-to-freeze rules below.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sight Freeze|Illumination")
	bool bFreezeOnlyWhenLitAndWatched = true;

	/** Wall torch light (ULostWithinLightSubsystem) counts as light for the Doll */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sight Freeze|Illumination")
	bool bTorchLightCounts = true;

	/** If true, the Doll ONLY freezes when illuminated directly by the flashlight beam */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sight Freeze|Illumination")
	bool bRequireFlashlightAlways = false;

	// -------------------------------------------------------------------------
	// Player State & Distraction
	// -------------------------------------------------------------------------

	/**
	 * If true, when the player enters inventory, reads notes, or interacts with the padlock,
	 * the player is considered distracted and NOT observing the Doll, allowing it to creep forward.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sight Freeze|Player State")
	bool bIgnoreSightWhenInMenus = true;

	// -------------------------------------------------------------------------
	// Stalking & Creeping Settings
	// -------------------------------------------------------------------------

	/** If true, automatically creeps / stalks toward the player when unobserved */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sight Freeze|Stalking")
	bool bAutoStalkWhenUnobserved = true;

	/** Walk speed (cm/s) applied while creeping unobserved toward the player */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sight Freeze|Stalking", meta=(ClampMin="10.0", Units="cm/s"))
	float StalkWalkSpeed = 220.0f;

	/** Centre distance (cm) to the player that ends the stalk move; small so the Doll walks into the player (body contact) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sight Freeze|Stalking", meta=(ClampMin="0.0", Units="cm"))
	float StalkAcceptanceRadius = 5.0f;

	/** Interval (seconds) between recalculating pathfinding toward the player */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sight Freeze|Stalking", meta=(ClampMin="0.05", Units="s"))
	float StalkRepathInterval = 0.25f;

	// -------------------------------------------------------------------------
	// Sight: dormancy, seeing the player, gaze flicker
	// -------------------------------------------------------------------------

	/** While no light (beam or a lit torch) touches the Doll it lies dormant: no movement, animation paused */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sight Freeze|Sight")
	bool bDormantInDarkness = true;

	/** The Doll moves only toward a player it has seen lit (or where it last saw them); false stalks the player anywhere */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sight Freeze|Sight")
	bool bRequireSightOfPlayer = true;

	/** Being watched in light freezes the Doll (the older rule). False (default, 2026-11-20): watching does not stop it;
	 *  only darkness does. The gaze flicker exists to break this freeze, so it applies only when this is true. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sight Freeze|Sight")
	bool bFreezeWhenWatched = false;

	/** How far the awake Doll sees a lit player (clear line needed) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sight Freeze|Sight", meta=(ClampMin="0.0", Units="cm"))
	float DollSightRange = 2000.0f;

	/** Speed of the rush toward a player it has seen, with bRequireSightOfPlayer */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sight Freeze|Sight", meta=(ClampMin="10.0", Units="cm/s"))
	float RushSpeed = 450.0f;

	/** Holding the lit beam on the watched Doll this long blacks the flashlight out. 0 disables it */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sight Freeze|Sight", meta=(ClampMin="0.0", Units="s"))
	float GazeFlickerTime = 4.0f;

	/** Length of the blackout; the Doll is free to move during it */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sight Freeze|Sight", meta=(ClampMin="0.0", Units="s"))
	float GazeFlickerBlackout = 0.4f;

	/** Phantom lure sounds while dormant; empty uses the Doll's own footstep sounds */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sight Freeze|Audio")
	TArray<TObjectPtr<USoundBase>> MimicSounds;

	/** Seconds between phantom lures (random within the range) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sight Freeze|Audio")
	FVector2D MimicIntervalRange = FVector2D(12.0, 25.0);

	/** Lures play only while the player is this close to the dormant Doll. 0 disables them */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sight Freeze|Audio", meta=(ClampMin="0.0", Units="cm"))
	float MimicPlayerRange = 1500.0f;

	/** Lures sound this far from the player, to their left, right or behind */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sight Freeze|Audio")
	FVector2D MimicOffsetRange = FVector2D(400.0, 800.0);

	/** Porcelain click when the rushing, unwatched Doll first comes this close to the player */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sight Freeze|Audio")
	TObjectPtr<USoundBase> RearClickSound = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sight Freeze|Audio", meta=(ClampMin="0.0", Units="cm"))
	float RearClickDistance = 350.0f;

	/** Rising ballast buzz, started GazeBuzzLeadTime before the flashlight blacks out */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sight Freeze|Audio")
	TObjectPtr<USoundBase> GazeBuzzSound = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sight Freeze|Audio", meta=(ClampMin="0.0", Units="s"))
	float GazeBuzzLeadTime = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sight Freeze|Audio")
	TObjectPtr<USoundAttenuation> SoundAttenuation = nullptr;

	/** If true, cat reaction fear / ragdoll takes priority over sight freeze */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sight Freeze|Priorities")
	bool bCatReactionHasPriority = true;

	// -------------------------------------------------------------------------
	// State (Read-Only)
	// -------------------------------------------------------------------------

	/** True when the Doll is frozen in place by player line-of-sight */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Sight Freeze|State")
	bool bIsFrozen = false;

	/** True when the player currently has line-of-sight to the Doll */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Sight Freeze|State")
	bool bIsObserved = false;

	/** True when a watched point of the Doll is lit (flashlight beam, or a torch with bTorchLightCounts) */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Sight Freeze|State")
	bool bIsIlluminated = false;

	/** True when the player is distracted in menus, reading a note, or using the padlock */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Sight Freeze|State")
	bool bIsPlayerDistracted = false;

	/** True while no light touches the Doll and it lies dormant */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Sight Freeze|State")
	bool bIsDormant = false;

	/** True when any light (beam or a lit torch) touches the Doll, watched or not */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Sight Freeze|State")
	bool bIsDollLit = false;

	/** True while the awake Doll sees the lit player */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Sight Freeze|State")
	bool bSeesPlayer = false;

	// -------------------------------------------------------------------------
	// Delegates & Events
	// -------------------------------------------------------------------------

	/** Fired when the freeze state changes */
	UPROPERTY(BlueprintAssignable, Category="Sight Freeze|Events")
	FOnMonsterSightFreezeChanged OnSightFreezeChanged;

	/** Fired when observed state changes */
	UPROPERTY(BlueprintAssignable, Category="Sight Freeze|Events")
	FOnMonsterObservedChanged OnObservedChanged;

	// -------------------------------------------------------------------------
	// Getters & Public Methods
	// -------------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category="Monster AI|Sight Freeze")
	bool IsFrozen() const { return bIsFrozen; }

	UFUNCTION(BlueprintPure, Category="Monster AI|Sight Freeze")
	bool IsObserved() const { return bIsObserved; }

	UFUNCTION(BlueprintPure, Category="Monster AI|Sight Freeze")
	bool IsIlluminated() const { return bIsIlluminated; }

	UFUNCTION(BlueprintPure, Category="Monster AI|Sight Freeze")
	bool IsPlayerDistracted() const { return bIsPlayerDistracted; }

	UFUNCTION(BlueprintPure, Category="Monster AI|Sight Freeze")
	bool IsDormant() const { return bIsDormant; }

	UFUNCTION(BlueprintPure, Category="Monster AI|Sight Freeze")
	bool SeesPlayer() const { return bSeesPlayer; }

	/** True while the awake Doll moves toward a player it has seen (or where it last saw them) */
	UFUNCTION(BlueprintPure, Category="Monster AI|Sight Freeze")
	bool IsRushing() const { return bRushing; }

	/** Where it last saw the player (valid once HasSeenPlayer) */
	FVector GetLastSeenPlayerLocation() const { return LastSeenPlayerLocation; }
	bool HasSeenPlayer() const { return bHasSeenPlayer; }

	/** Seconds the lit beam has been held on the watched Doll */
	float GetGazeLockSeconds() const { return GazeLockSeconds; }

	/** True during the flashlight blackout the gaze flicker caused */
	bool IsInGazeFlicker() const { return FlickerRemaining > 0.0f; }

	/** True when any light touches the Doll now: the beam (with a clear line) or a lit torch */
	bool EvaluateDollLit(const ALostWithinCharacter* PlayerChar, bool& bOutBeamLit) const;

	/** True when the awake Doll sees the player: within range, a clear line and the player lit */
	bool CanSeePlayer(const ALostWithinCharacter* PlayerChar, bool bBeamOnDoll) const;

	/** True while Actor's Doll is rushing toward the player it saw */
	static bool IsActorRushingAtPlayer(const AActor* Actor);

	/** Explicitly sets the freeze state, updating movement and animation */
	UFUNCTION(BlueprintCallable, Category="Monster AI|Sight Freeze")
	void SetFrozen(bool bFreeze);

	/** Resumes stalking movement towards the player immediately */
	UFUNCTION(BlueprintCallable, Category="Monster AI|Sight Freeze")
	void ResumeStalking();

	/** Static helper to check if an actor is frozen by sight */
	UFUNCTION(BlueprintPure, Category="Monster AI|Sight Freeze")
	static bool IsActorSightFrozen(const AActor* Actor);

	/** Static helper for Blueprint movement guards; returns false if actor is sight-frozen */
	UFUNCTION(BlueprintPure, Category="Monster AI|Sight Freeze")
	static bool CanActorMove(const AActor* Actor);

	/** Evaluates whether the given player controller has clear line-of-sight to the Doll */
	UFUNCTION(BlueprintCallable, Category="Monster AI|Sight Freeze")
	bool EvaluatePlayerObservation(const APlayerController* PC, bool& OutObserved, bool& OutIlluminated);

	/** Tests whether a world point is within the camera's view frustum */
	bool IsPointInPlayerFrustum(const FVector& TargetPoint, const FVector& CameraLoc, const FVector& CameraFwd, float HalfFOVDegrees) const;

	/** Tests whether there is unoccluded line of sight between two points */
	bool HasLineOfSightToPoint(const FVector& FromLocation, const FVector& TargetPoint, const AActor* IgnoredActor1 = nullptr, const AActor* IgnoredActor2 = nullptr) const;

	/** Tests whether the target point is within the player's flashlight cone and range */
	bool IsPointIlluminatedByFlashlight(const ALostWithinCharacter* PlayerChar, const FVector& TargetPoint) const;

	/** Checks whether the player is currently in a menu, reading a note, or picking the padlock */
	bool IsPlayerInMenuOrPadlock(const AActor* PlayerActor) const;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void RegisterComponentTickFunctions(bool bRegister) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	ALostWithinCharacter* FindPlayerCharacter() const;
	APlayerController* FindPlayerController() const;
	UMonsterAIComponent* GetMonsterAI() const;
	bool IsReactingToCat() const;

	void UpdateSightFreeze(float DeltaTime);
	void UpdateStalkingMovement(float DeltaTime);

	void ApplyFrozenState();
	void ApplyUnfrozenState();
	void UpdateMimic(float DeltaTime, const ALostWithinCharacter* PlayerChar);
	void PlaySoundAt(USoundBase* Sound, const FVector& Location) const;

	float RepathTimer = 0.0f;
	float CachedMaxWalkSpeed = 0.0f;
	bool bSavedInitialSpeed = false;

	bool bRushing = false;
	bool bHasSeenPlayer = false;
	FVector LastSeenPlayerLocation = FVector::ZeroVector;
	float GazeLockSeconds = 0.0f;
	bool bGazeBuzzPlayed = false;
	float FlickerRemaining = 0.0f;
	float MimicTimer = -1.0f;
	bool bRearClickPlayed = false;
};
