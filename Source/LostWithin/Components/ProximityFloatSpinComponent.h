// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Interaction/InteractableInterface.h"
#include "ProximityFloatSpinComponent.generated.h"

class APickupItemActor;
class UAnimationAsset;
class UAudioComponent;
class UExponentialHeightFogComponent;
class ULightComponent;
class UMaterialInstanceDynamic;
class UPointLightComponent;
class UPostProcessComponent;
class USceneComponent;
class USkeletalMesh;
class USkeletalMeshComponent;
class USkyAtmosphereComponent;
class USkyLightComponent;
class USoundAttenuation;
class USoundBase;
class USphereComponent;
class USpotLightComponent;
class UStaticMeshComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnProximityFloatStateChanged, bool, bIsActive);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCatInteracted, int32, NewInteractionCount);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDirectedPathWaypointReached, int32, WaypointIndex);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnDirectedPathCompleted);

/**
 *  Controls when the cat automatically begins moving along PathWaypoints.
 */
UENUM(BlueprintType)
enum class EDirectedPathTriggerMode : uint8
{
	/** Only moves when started explicitly via StartDirectedPath / StartDirectedPathWithWaypoints / MoveToDirectedLocation */
	Manual UMETA(DisplayName = "Manual (Blueprint / C++ Call)"),
	/** Starts following PathWaypoints immediately at BeginPlay */
	OnBeginPlay UMETA(DisplayName = "On BeginPlay"),
	/** Starts following PathWaypoints on the player's first [LMB] interaction */
	OnFirstInteract UMETA(DisplayName = "On First Interact"),
	/** Starts following PathWaypoints when InteractionCount drops the item */
	OnItemDropped UMETA(DisplayName = "On Item Dropped"),
	/** Starts following PathWaypoints when the float/spin sequence triggers */
	OnSpinTriggered UMETA(DisplayName = "On Spin Triggered"),
	/** Starts following PathWaypoints once when the float/spin sequence reaches DirectedPathSpinWindowIndex */
	OnSpinWindow UMETA(DisplayName = "On Spin Window Start")
};

/**
 *  Inclusive elapsed-time window (seconds after trigger) during which spinning is active.
 */
USTRUCT(BlueprintType)
struct FSpinTimeWindow
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin", meta = (ClampMin = "0.0", Units = "s"))
	float StartTime = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin", meta = (ClampMin = "0.0", Units = "s"))
	float EndTime = 0.0f;

	/** Multiplier applied to SpinSpeed during this window */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin", meta = (ClampMin = "0.0"))
	float SpinSpeedMultiplier = 1.0f;
};

/**
 *  Smoothly floats and spins the owning actor midair when the player pawn approaches within ActivationRadius,
 *  drives a morph target based on an outer vicinity check (default 1.0 when far, 0.0 when closer) and spin state,
 *  performs a pre-spin jump, floats strictly while spinning, and plays 3D spatialized audio attached to the owner.
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class LOSTWITHIN_API UProximityFloatSpinComponent : public UActorComponent, public IInteractableInterface
{
	GENERATED_BODY()

public:
	UProximityFloatSpinComponent();

	/** Inner distance from the cat to the player's collision at which the jump, timed spin, and 3D audio trigger */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Proximity", meta = (ClampMin = "0.0", Units = "cm"))
	float ActivationRadius = 120.0f;

	/** Distance from the cat to the player's collision at which floating deactivates if bStayActiveOnceTriggered is false */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Proximity", meta = (ClampMin = "0.0", Units = "cm"))
	float DeactivationRadius = 160.0f;

	/** If true, once triggered the actor continues floating even if the player leaves */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Proximity")
	bool bStayActiveOnceTriggered = true;

	/** Target height above the resting Z position when active */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Motion", meta = (Units = "cm"))
	float FloatHeight = 85.0f;

	/** Vertical sine-wave bobbing amplitude while floating */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Motion", meta = (ClampMin = "0.0", Units = "cm"))
	float BobAmplitude = 12.0f;

	/** Vertical bobbing frequency in cycles per second */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Motion", meta = (ClampMin = "0.0", Units = "Hz"))
	float BobFrequency = 0.8f;

	/** Yaw rotation speed when spinning is active */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Motion", meta = (Units = "deg/s"))
	float SpinSpeed = 1440.0f;

	/** Interpolation speed for rising into the floating state */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Motion", meta = (ClampMin = "0.1"))
	float BlendSpeed = 4.0f;

	/** Downward gravity acceleration applied immediately when spinning stops so the actor drops rapidly to ground */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Motion", meta = (ClampMin = "1.0", Units = "cm/s^2"))
	float FallGravity = 980.0f;

	/** If true, performs an upward hop before spinning and returns to the ground */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Jump")
	bool bJumpBeforeSpin = true;

	/** Peak vertical offset above the ground during the pre-spin jump */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Jump", meta = (ClampMin = "0.0", Units = "cm", EditCondition = "bJumpBeforeSpin"))
	float JumpHeight = 70.0f;

	/** Duration of the pre-spin jump arc (seconds; matches 0.27s pre-spin beat by default) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Jump", meta = (ClampMin = "0.01", Units = "s", EditCondition = "bJumpBeforeSpin"))
	float JumpDuration = 0.27f;

	/** If true, jumps right before every SpinTimeWindows entry instead of only on initial trigger */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Jump", meta = (EditCondition = "bJumpBeforeSpin"))
	bool bJumpBeforeEverySpinWindow = false;

	/** If true, spinning only occurs during SpinTimeWindows (measured in seconds since trigger) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Timing")
	bool bUseSpinTimeWindows = true;

	/** Time ranges (seconds after trigger) during which aggressive spinning is active */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Timing", meta = (EditCondition = "bUseSpinTimeWindows"))
	TArray<FSpinTimeWindow> SpinTimeWindows;

	/** If true, resets Z (Yaw) rotation back to its initial resting orientation whenever spinning stops */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Timing")
	bool bResetRotationWhenSpinStops = true;

	/** Drive the skeletal mesh morph target directly (0 = idle, 1 = spinning) instead of playing a looping AnimSequence */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Morph Animation")
	bool bDriveSpinMorphTarget = true;

	/** Morph target name on the skeletal mesh (oiiaioooooiai_cat uses oiiaioooooiai_cat_mesh_0_0_MorphTarget) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Morph Animation", meta = (EditCondition = "bDriveSpinMorphTarget"))
	FName SpinMorphTargetName = TEXT("oiiaioooooiai_cat_mesh_0_0_MorphTarget");

	/** Morph target weight when inactive (player is inside MorphVicinityRadius and not spinning) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Morph Animation", meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "bDriveSpinMorphTarget"))
	float InactiveMorphWeight = 0.0f;

	/** Morph target weight when active (default/far from player, or while spinning) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Morph Animation", meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "bDriveSpinMorphTarget"))
	float ActiveMorphWeight = 1.0f;

	/** Blend speed for morph target transitions (0 = instant snap on beat) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Morph Animation", meta = (ClampMin = "0.0", EditCondition = "bDriveSpinMorphTarget"))
	float MorphBlendSpeed = 8.0f;

	/** If true, uses an outer vicinity check where far = ActiveMorphWeight (default) and close = InactiveMorphWeight */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Morph Animation", meta = (EditCondition = "bDriveSpinMorphTarget"))
	bool bUseMorphVicinityCheck = true;

	/** Outer vicinity distance from the cat to the player's collision at which the morph switches from active (1.0) to inactive (0.0) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Morph Animation", meta = (ClampMin = "0.0", Units = "cm", EditCondition = "bDriveSpinMorphTarget && bUseMorphVicinityCheck"))
	float MorphVicinityRadius = 180.0f;

	/** Outer vicinity distance from the cat to the player's collision at which the morph switches back to active (1.0) when the player moves away */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Morph Animation", meta = (ClampMin = "0.0", Units = "cm", EditCondition = "bDriveSpinMorphTarget && bUseMorphVicinityCheck"))
	float MorphVicinityDeactivationRadius = 220.0f;

	/** Vertical Z offset applied to the skeletal mesh in the active morph state (far from player); inactive morph inside vicinity uses 0 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Morph Animation", meta = (Units = "cm", EditCondition = "bDriveSpinMorphTarget && bUseMorphVicinityCheck"))
	float ActiveMorphMeshZOffset = -15.0f;

	/** Blend speed for lowering/raising the skeletal mesh between active and inactive morph states */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Morph Animation", meta = (ClampMin = "0.1", EditCondition = "bDriveSpinMorphTarget && bUseMorphVicinityCheck"))
	float MorphHeightBlendSpeed = 8.0f;

	/** If true, forces ActiveMorphWeight during active spin windows even while inside MorphVicinityRadius */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Morph Animation", meta = (EditCondition = "bDriveSpinMorphTarget"))
	bool bActiveMorphWhileSpinning = true;

	/** Skeletal mesh displayed by this actor (e.g., oiiaioooooiai_cat) if the owner has no SkeletalMeshComponent */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Visuals")
	TObjectPtr<USkeletalMesh> StandbySkeletalMesh = nullptr;

	/** Optional animation used only when bDriveSpinMorphTarget is false */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Visuals", meta = (EditCondition = "!bDriveSpinMorphTarget"))
	TObjectPtr<UAnimationAsset> StandbyAnimation = nullptr;

	/** Whether StandbyAnimation loops while idle (when bDriveSpinMorphTarget is false) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Visuals", meta = (EditCondition = "!bDriveSpinMorphTarget"))
	bool bLoopStandbyAnimation = true;

	/** If true, stops StandbyAnimation while spinning (when bDriveSpinMorphTarget is false) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Visuals", meta = (EditCondition = "!bDriveSpinMorphTarget"))
	bool bStopAnimationWhileSpinning = true;

	/** Relative offset applied when spawning the runtime skeletal mesh component */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Visuals")
	FTransform StandbyMeshOffset = FTransform::Identity;

	/** Hides any attached child mesh actors on the owner at BeginPlay (useful for hiding leftover Maxwell child actors) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Visuals")
	bool bHideAttachedChildActors = true;

	/** 3D music asset to play attached to this actor when triggered */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Audio")
	TObjectPtr<USoundBase> TriggerMusic = nullptr;

	/** Optional attenuation asset for 3D music; if unset, uses MusicInnerRadius and MusicFalloffDistance */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Audio")
	TObjectPtr<USoundAttenuation> MusicAttenuation = nullptr;

	/** Full-volume sphere radius around the actor when MusicAttenuation is unset */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Audio", meta = (ClampMin = "0.0", Units = "cm", EditCondition = "MusicAttenuation == nullptr"))
	float MusicInnerRadius = 250.0f;

	/** Distance over which 3D music attenuates to silence when MusicAttenuation is unset */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Audio", meta = (ClampMin = "1.0", Units = "cm", EditCondition = "MusicAttenuation == nullptr"))
	float MusicFalloffDistance = 1800.0f;

	/** If true, replays TriggerMusic from the start when it finishes and resets the spin timeline */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Audio")
	bool bLoopMusic = false;

	/** Volume multiplier for TriggerMusic */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Audio", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float MusicVolume = 1.0f;

	/** Optional fade-in duration when TriggerMusic starts (0.0 for immediate start) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Audio", meta = (ClampMin = "0.0", Units = "s"))
	float MusicFadeInDuration = 0.0f;

	/** If bStayActiveOnceTriggered is false, whether to fade out music when player leaves */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Audio")
	bool bStopMusicOnDeactivate = true;

	/** Automatically switch Static components on the owner and attached child actors to Movable at BeginPlay */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Setup")
	bool bAutoSetHierarchyMovable = true;

	/** Automatically disable any existing URotatingMovementComponent on the owner at BeginPlay */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Setup")
	bool bDisableExistingRotatingMovement = true;

	/** Draw debug spheres in PIE to visualize ActivationRadius and DeactivationRadius */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Debug")
	bool bDrawDebugRadius = false;

	/** Draw debug waypoint markers and path lines in PIE */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Debug")
	bool bDrawDebugPath = false;

	/** Enable directed waypoint movement along PathWaypoints */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Path")
	bool bEnableDirectedPath = false;

	/** Controls when the cat automatically begins moving along PathWaypoints */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Path")
	EDirectedPathTriggerMode DirectedPathTriggerMode = EDirectedPathTriggerMode::OnSpinWindow;

	/** Zero-based SpinTimeWindows entry whose StartTime triggers the directed path once (2 = 3rd spin start at 6.06s by default) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Path", meta = (ClampMin = "0", EditCondition = "DirectedPathTriggerMode == EDirectedPathTriggerMode::OnSpinWindow"))
	int32 DirectedPathSpinWindowIndex = 2;

	/** Ordered 3D waypoints relative to the cat's initial unscaled actor transform (editable with viewport gizmos; moves once and stops at the final waypoint) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Path", meta = (MakeEditWidget = true))
	TArray<FVector> PathWaypoints;

	/** Linear movement speed along directed path segments */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Path", meta = (ClampMin = "0.0", Units = "cm/s"))
	float PathMoveSpeed = 150.0f;

	/** Distance threshold to consider the active waypoint reached */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Path", meta = (ClampMin = "0.1", Units = "cm"))
	float WaypointAcceptanceRadius = 5.0f;

	/** Smoothly turn BaseRotation.Yaw to face the current path movement direction */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Path")
	bool bFacePathDirection = true;

	/** Angular interpolation speed when turning BaseRotation.Yaw toward the path direction */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Path", meta = (ClampMin = "0.1", EditCondition = "bFacePathDirection"))
	float PathTurnSpeed = 6.0f;

	/** Optional yaw offset added when facing the movement vector (e.g., +/-90 deg if the cat mesh forward differs from actor +X) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Path", meta = (Units = "deg", EditCondition = "bFacePathDirection"))
	float PathFacingYawOffset = 0.0f;

	/** If true, pauses advancing along PathWaypoints while the cat is actively spinning/hovering */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Path")
	bool bPausePathWhileSpinning = false;

	/** If true, performs a downward line trace to keep BaseLocation.Z aligned with ground geometry along the path */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Path")
	bool bSnapPathToGround = false;

	/** Vertical half-extent above and below the current path point used when tracing for ground geometry */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Path", meta = (ClampMin = "10.0", Units = "cm", EditCondition = "bSnapPathToGround"))
	float GroundTraceHalfHeight = 250.0f;

	/** Height offset applied above the traced ground impact point when bSnapPathToGround is enabled */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Path", meta = (Units = "cm", EditCondition = "bSnapPathToGround"))
	float GroundHeightOffset = 0.0f;

	/** Collision channel traced downward when bSnapPathToGround is enabled (defaults to WorldStatic to ignore pawns/pickups) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Path", meta = (EditCondition = "bSnapPathToGround"))
	TEnumAsByte<ECollisionChannel> GroundTraceChannel = ECC_WorldStatic;

	/** If true, spinning triggers after SpinTriggerInteractionThreshold [LMB] interactions instead of near-distance proximity */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Interaction")
	bool bTriggerSpinByInteractionCount = true;

	/** Number of [LMB] interactions required to trigger the spin sequence (replaces inner near-distance trigger) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Interaction", meta = (ClampMin = "1", EditCondition = "bTriggerSpinByInteractionCount"))
	int32 SpinTriggerInteractionThreshold = 20;

	/** Number of [LMB] interactions required before the cat drops the Fairy Tale Kitty Key pickup item */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Interaction", meta = (ClampMin = "1"))
	int32 ItemDropInteractionThreshold = 5;

	/** Fractional vertical compression at the peak of an [LMB] petting squeeze (0.22 = squishes to 78% Z height) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Interaction", meta = (ClampMin = "0.0", ClampMax = "0.75"))
	float InteractionSqueezeScaleZ = 0.22f;

	/** Fractional X/Y expansion at the peak of an [LMB] petting squeeze for squash-and-stretch volume preservation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Interaction", meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float InteractionSqueezeHorizontalExpand = 0.08f;

	/** Duration of each [LMB] petting squeeze pulse */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Interaction", meta = (ClampMin = "0.01", Units = "s"))
	float InteractionSqueezeDuration = 0.28f;

	/** If true, smoothly darkens sky atmosphere, horizon fog, and world lights starting on SkyDarkenStartWindowIndex */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Sky & Lighting")
	bool bDarkenSkyOnSpinWindow = true;

	/** 0-based index into SpinTimeWindows where smooth sky darkening begins (1 = 2nd spin window: 3.12s - 5.10s) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Sky & Lighting", meta = (ClampMin = "0", EditCondition = "bDarkenSkyOnSpinWindow"))
	int32 SkyDarkenStartWindowIndex = 1;

	/** Target SkyAtmosphere luminance multiplier when fully darkened (0.015 = deep night sky) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Sky & Lighting", meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "bDarkenSkyOnSpinWindow"))
	float SkyDarkenTargetAtmosphereMultiplier = 0.015f;

	/** Target ExponentialHeightFog inscattering multiplier when fully darkened */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Sky & Lighting", meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "bDarkenSkyOnSpinWindow"))
	float SkyDarkenTargetFogMultiplier = 0.03f;

	/** Target Directional/Sky light intensity multiplier when fully darkened */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Sky & Lighting", meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "bDarkenSkyOnSpinWindow"))
	float SkyDarkenTargetWorldLightMultiplier = 0.06f;

	/** Smooth interpolation speed when restoring daylight after the floatspin sequence completes */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Sky & Lighting", meta = (ClampMin = "0.1", EditCondition = "bDarkenSkyOnSpinWindow"))
	float SkyRestoreSpeed = 1.5f;

	/** If true, transforms the surrounding environment into a disco during the configured spin window */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Disco")
	bool bEnableDiscoEnvironmentOnSpinWindow = true;

	/** 0-based index into SpinTimeWindows that activates disco lights & post-processing (2 = 3rd spin window: 6.06s - 43.84s) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Disco", meta = (ClampMin = "0", EditCondition = "bEnableDiscoEnvironmentOnSpinWindow"))
	int32 DiscoSpinWindowIndex = 2;

	/** Number of sweeping colored spotlights spawned above the cat during disco mode */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Disco", meta = (ClampMin = "1", ClampMax = "12", EditCondition = "bEnableDiscoEnvironmentOnSpinWindow"))
	int32 DiscoSpotlightCount = 6;

	/** Rotation speed of the sweeping disco spotlight rig */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Disco", meta = (Units = "deg/s", EditCondition = "bEnableDiscoEnvironmentOnSpinWindow"))
	float DiscoOrbitSpeed = 240.0f;

	/** HSV hue cycles per second across the disco lights */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Disco", meta = (ClampMin = "0.0", EditCondition = "bEnableDiscoEnvironmentOnSpinWindow"))
	float DiscoColorCycleSpeed = 1.35f;

	/** Beat pulse frequency in cycles per second for disco lights and post-processing */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Disco", meta = (ClampMin = "0.0", Units = "Hz", EditCondition = "bEnableDiscoEnvironmentOnSpinWindow"))
	float DiscoPulseFrequency = 7.5f;

	/** Base intensity (candelas) of each disco spotlight (312.5 = 1.25% of original 25000 cd) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Disco", meta = (ClampMin = "0.0", EditCondition = "bEnableDiscoEnvironmentOnSpinWindow"))
	float DiscoSpotlightIntensity = 312.5f;

	/** Vertical offset relative to the cat's floating center at which the disco rig is positioned (0 = centered on the cat) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Disco", meta = (Units = "cm", EditCondition = "bEnableDiscoEnvironmentOnSpinWindow"))
	float DiscoRigHeightAboveFloat = 0.0f;

	/** Intensity fraction of DiscoSpotlightIntensity used by the overhead central fill light */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Disco", meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "bEnableDiscoEnvironmentOnSpinWindow"))
	float DiscoFillIntensityMultiplier = 0.15f;

	/** Attenuation radius for disco spotlights and central fill light */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Disco", meta = (ClampMin = "100.0", Units = "cm", EditCondition = "bEnableDiscoEnvironmentOnSpinWindow"))
	float DiscoLightAttenuationRadius = 1600.0f;

	/** Radius of the query-only trace sphere added to the owner for [LMB] interaction targeting when no mesh collision exists */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Interaction", meta = (ClampMin = "10.0", Units = "cm"))
	float InteractionTraceRadius = 55.0f;

	/** Sound assets played at random on [LMB] interaction; auto-populated from InteractionSoundSearchPaths if empty */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Interaction")
	TArray<TObjectPtr<USoundBase>> InteractionSounds;

	/** Content paths searched at BeginPlay to gather cat SFX assets (e.g. MetaSound/Cat/*) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Interaction")
	TArray<FName> InteractionSoundSearchPaths;

	/** Volume multiplier for cat meow SFX played on interaction */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Interaction", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float InteractionSoundVolume = 0.25f;

	/** Actor class spawned when InteractionCount reaches ItemDropInteractionThreshold */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Float & Spin|Interaction")
	TSubclassOf<APickupItemActor> DropItemClass;

	/** Broadcast whenever proximity activation state changes */
	UPROPERTY(BlueprintAssignable, Category = "Float & Spin|Events")
	FOnProximityFloatStateChanged OnProximityFloatStateChanged;

	/** Broadcast whenever the cat is interacted with via [LMB] */
	UPROPERTY(BlueprintAssignable, Category = "Float & Spin|Events")
	FOnCatInteracted OnCatInteracted;

	/** Broadcast whenever a directed path waypoint index is reached */
	UPROPERTY(BlueprintAssignable, Category = "Float & Spin|Events")
	FOnDirectedPathWaypointReached OnDirectedPathWaypointReached;

	/** Broadcast once the cat reaches the final directed path waypoint and stops */
	UPROPERTY(BlueprintAssignable, Category = "Float & Spin|Events")
	FOnDirectedPathCompleted OnDirectedPathCompleted;

	/** Starts (or restarts) moving along the configured PathWaypoints */
	UFUNCTION(BlueprintCallable, Category = "Float & Spin|Path")
	void StartDirectedPath(bool bRestartFromBeginning = true);

	/** Starts moving once through an explicit sequence of world-space waypoints */
	UFUNCTION(BlueprintCallable, Category = "Float & Spin|Path")
	void StartDirectedPathWithWaypoints(const TArray<FVector>& WorldWaypoints);

	/** Moves directly from current BaseLocation to a single world-space destination and stops */
	UFUNCTION(BlueprintCallable, Category = "Float & Spin|Path")
	void MoveToDirectedLocation(const FVector& WorldDestination);

	/** Temporarily pauses directed path movement while retaining current waypoint progress */
	UFUNCTION(BlueprintCallable, Category = "Float & Spin|Path")
	void PauseDirectedPath();

	/** Resumes paused directed path movement */
	UFUNCTION(BlueprintCallable, Category = "Float & Spin|Path")
	void ResumeDirectedPath();

	/** Stops directed path movement and optionally resets progress back to waypoint 0 */
	UFUNCTION(BlueprintCallable, Category = "Float & Spin|Path")
	void StopDirectedPath(bool bResetWaypointIndex = true);

	/** Returns true while actively moving along directed path waypoints */
	UFUNCTION(BlueprintPure, Category = "Float & Spin|Path")
	bool IsFollowingDirectedPath() const { return bIsFollowingPath && !bPathPaused && !bHasCompletedPath; }

	/** Returns true once the final directed path waypoint has been reached */
	UFUNCTION(BlueprintPure, Category = "Float & Spin|Path")
	bool HasCompletedDirectedPath() const { return bHasCompletedPath; }

	/** Returns the 0-based index of the active target waypoint */
	UFUNCTION(BlueprintPure, Category = "Float & Spin|Path")
	int32 GetCurrentWaypointIndex() const { return CurrentWaypointIndex; }

	/** Returns the unscaled world transform anchored at Owner's location and rotation for resolving local PathWaypoints */
	static FTransform GetDirectedPathAnchorTransform(const AActor* Owner);

	/** Returns true if currently active */
	UFUNCTION(BlueprintPure, Category = "Float & Spin")
	bool IsFloatSpinActive() const { return bIsActive; }

	/** True while the float/spin sequence runs on an enabled, not-despawned cat; any spin window or gap counts. */
	UFUNCTION(BlueprintPure, Category = "Float & Spin")
	bool IsFloatSpinThreatening() const { return bIsActive && IsComponentTickEnabled() && !bHasDespawnedCat; }

	/** True only while the given 0-based spin window is actually running. */
	UFUNCTION(BlueprintPure, Category = "Float & Spin")
	bool IsSpinWindowActive(int32 WindowIndex) const;

	/** Returns true while the cat's floatspin music sequence is actively playing */
	UFUNCTION(BlueprintPure, Category = "Float & Spin|Audio")
	bool IsSpinMusicActive() const;

	/** Returns the number of times the player has interacted with the cat via [LMB] */
	UFUNCTION(BlueprintPure, Category = "Float & Spin|Interaction")
	int32 GetInteractionCount() const { return InteractionCount; }

	//~ Begin IInteractableInterface
	virtual void OnInteract_Implementation(AActor* Interactor) override;
	virtual FText GetInteractionPrompt_Implementation() const override;
	virtual bool CanInteract_Implementation(AActor* Interactor) const override;
	//~ End IInteractableInterface

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	/** Ensures the owner and all recursively attached child actors use Movable mobility */
	void EnsureHierarchyMovable(AActor* Owner) const;

	/** Deactivates any URotatingMovementComponent on the owner so it does not spin unconditionally */
	void DisableConflictingRotatingMovement(AActor* Owner) const;

	/** Initializes or spawns the skeletal mesh component and hides leftover attached child actors if configured */
	void InitializeVisualMeshes(AActor* Owner);

	/** Switches between the idle animation and static spinning pose when bDriveSpinMorphTarget is false */
	void UpdateSpinAnimationState(bool bIsSpinning);

	/** Drives SpinMorphTargetName based on outer vicinity state and active spin state */
	void UpdateSpinMorphTarget(bool bIsSpinning, float DeltaTime);

	/** Smoothly lowers/raises the skeletal mesh Z offset and applies floor-anchored squeeze compensation */
	void UpdateVicinityMeshHeight(bool bIsSpinning, float JumpOffset, float SqueezeFraction, float DeltaTime);

	/** Computes squared distance from the cat's visual center to the closest point on PlayerPawn's collision */
	float ComputePlayerDistanceSq(const APawn* PlayerPawn) const;

	/** Evaluates outer vicinity distance to player pawn for morph switching */
	void UpdateMorphVicinityState(const APawn* PlayerPawn);

	/** Evaluates inner trigger distance to player pawn and updates bIsActive / audio trigger */
	void UpdateProximityState(AActor* Owner, const APawn* PlayerPawn);

	/** Activates floating, spinning, and TriggerMusic */
	void TriggerSpinSequence(AActor* Owner);

	/** Loads all USoundBase assets found in InteractionSoundSearchPaths when InteractionSounds is empty */
	void LoadInteractionSounds();

	/** Plays a random 3D sound from InteractionSounds at the owner's location */
	void PlayRandomInteractionSound(AActor* Owner) const;

	/** Triggers a brief vertical squash animation in response to [LMB] petting */
	void TriggerInteractionSqueeze();

	/** Advances or clears the [LMB] petting squash animation and returns current fractional squeeze [0..1] */
	float UpdateInteractionSqueeze(AActor* Owner, float DeltaTime);

	/** Applies the white-cyan glow effect based on unstable matter intensity */
	void UpdateCatGlow(float Intensity, float ElapsedTime);

	/** Immediately restores base mesh/actor scale and clears any active squeeze animation */
	void ResetInteractionSqueeze(AActor* Owner);

	/** Updates smooth sky/fog/world light darkening from the 2nd spin window through sequence completion */
	void UpdateSkyDarkening(float DeltaTime);

	/** Caches original sky atmosphere, fog, and non-static directional/sky lights before modifying them */
	void CacheWorldSkyAndLights();

	/** Applies interpolated sky darkness alpha [0..1] across all cached world atmosphere/light components */
	void ApplyWorldSkyDarkness(float Alpha);

	/** Restores all cached sky atmosphere, fog, and world lights to their exact original values */
	void RestoreWorldSkyAndLights();

	/** Spawns or cleans up overhead disco spotlights and post-process effects */
	void SetDiscoEnvironmentActive(AActor* Owner, bool bActivate);

	/** Animates sweeping multi-color overhead spotlights and post-process beat pulse during the disco spin window */
	void UpdateDiscoEnvironment(AActor* Owner, bool bShouldBeActive, float DeltaTime);

	/** Resets timed shockwave, color mode, and unstable matter state */
	void ResetTimelineEffects();

	/** Evaluates Oiiai_Psytrance timeframe triggers (shockwaves, environment/disco color shifts) */
	void EvaluateTimelineKeyframes(AActor* Owner, bool bDiscoShouldRun);

	/** Returns the disco spotlight intensity multiplier relative to the 5% base for ElapsedTriggeredTime */
	float ComputeDiscoBrightnessMultiplier(float TimeSeconds) const;

	/** Returns normalized unstable matter movement intensity [0..1] for ElapsedTriggeredTime */
	float ComputeUnstableMatterIntensity(float TimeSeconds, bool bIsSpinning) const;

	/** Computes per-frame unstable matter position, rotation, and scale warp offsets */
	void ComputeUnstableMatterOffsets(float Intensity, float TimeSeconds, FVector& OutPositionOffset, FRotator& OutRotationOffset, FVector& OutScaleMultiplier) const;

	/** Advances the 42.89s pop-out elastic scale rebound and returns its 3D scale multiplier */
	FVector UpdatePopOutScale(float DeltaTime);

	/** Pre-creates hidden shockwave ring segments and radial flash light */
	void InitializeShockwaveVisuals(AActor* Owner);

	/** Triggers a cat-centered neon shockwave ring & flash light burst */
	void TriggerCatShockwave(AActor* Owner, const FLinearColor& WaveColor, float RadiusScale = 1.0f);

	/** Advances active shockwave ring expansion and outputs normalized post-process impact alpha [0..1] */
	void UpdateCatShockwave(float DeltaTime, float& OutPostProcessPulseAlpha);

	/** Hides active shockwave components without destroying the reusable pool */
	void HideCatShockwaveVisuals();

	/** Destroys pooled shockwave components and material references */
	void DestroyCatShockwaveVisuals();

	/** Spawns the placeholder pickup item in front of the cat toward Interactor */
	void SpawnDroppedItem(AActor* Owner, AActor* Interactor);

	/** Computes parabolic pre-spin jump Z offset for the given elapsed trigger time */
	float ComputeJumpOffset(float TimeSeconds) const;

	/** Evaluates whether TimeSeconds is inside a spin window and outputs its speed multiplier and window index */
	float GetActiveSpinSpeedMultiplier(float TimeSeconds, bool& bOutIsSpinning, int32& OutWindowIndex) const;

	/** Evaluates whether spinning should currently run given bIsActive, SpinTimeWindows, and despawn/cutoff state */
	bool EvaluateShouldSpin(int32& OutActiveWindowIndex, float& OutActiveSpinMultiplier) const;

	/** Returns true if TimeSeconds falls inside any configured SpinTimeWindows entry */
	bool IsWithinSpinWindow(float TimeSeconds) const;

	/** Spawns and starts 3D spatialized music attached to the owner's root component */
	void StartTriggerMusic(AActor* Owner);

	/** Stops and cleans up any active 3D music component */
	void StopTriggerMusic();

	/** Loops TriggerMusic and resets the spin timeline if bLoopMusic is enabled */
	UFUNCTION()
	void HandleMusicFinished();

	/** Applies interpolated height, sine bobbing, timed yaw spin, and morph/animation state to the owner */
	void ApplyFloatAndSpinTransform(AActor* Owner, float DeltaTime);

	/** Converts local PathWaypoints relative to Owner's initial unscaled transform into RuntimeWorldWaypoints */
	void ResolveConfiguredWaypointsToWorld(const AActor* Owner);

	/** Starts configured directed path automatically if TriggerMode matches EventMode and path hasn't run yet */
	void TryTriggerDirectedPath(EDirectedPathTriggerMode EventMode);

	/** Advances BaseLocation (and optionally BaseRotation.Yaw) along RuntimeWorldWaypoints before ApplyFloatAndSpinTransform */
	void UpdateDirectedPathMovement(AActor* Owner, float DeltaTime);

	/** Traces downward to align InOutWorldLocation.Z with ground geometry when bSnapPathToGround is enabled */
	bool SnapLocationToGround(const AActor* Owner, FVector& InOutWorldLocation) const;

	/** Draws PIE debug visualization for RuntimeWorldWaypoints when bDrawDebugPath is true */
	void DrawDirectedPathDebug() const;

	FVector BaseLocation = FVector::ZeroVector;
	FRotator BaseRotation = FRotator::ZeroRotator;
	FVector BaseActorScale3D = FVector::OneVector;
	FVector BaseMeshRelativeLocation = FVector::ZeroVector;
	FVector BaseMeshRelativeScale3D = FVector::OneVector;
	float MeshPivotHeightAboveBottom = 0.0f;
	float ActorPivotHeightAboveBottom = 0.0f;

	bool bIsActive = false;
	bool bHasTriggered = false;
	bool bIsInMorphVicinity = false;
	bool bSettledAtRest = true;
	bool bWasSpinningLastTick = false;
	bool bOwnsRuntimeSkeletalMesh = false;
	float CurrentHoverHeight = 0.0f;
	float FallVelocity = 0.0f;
	float CurrentVicinityZOffset = 0.0f;
	float CurrentMorphWeight = 0.0f;
	float BobPhase = 0.0f;
	float ElapsedTriggeredTime = 0.0f;
	float CurrentSpinOffsetYaw = 0.0f;

	TArray<FVector> RuntimeWorldWaypoints;
	FTransform CachedPathAnchorTransform = FTransform::Identity;
	bool bHasCachedPathAnchorTransform = false;
	bool bUsingRuntimeOverrideWaypoints = false;
	int32 CurrentWaypointIndex = 0;
	bool bIsFollowingPath = false;
	bool bPathPaused = false;
	bool bHasCompletedPath = false;
	bool bHasAutoTriggeredDirectedPath = false;

	int32 InteractionCount = 0;
	bool bHasDroppedItem = false;
	bool bIsInteractionSqueezing = false;
	float InteractionSqueezeElapsed = 0.0f;
	float CurrentSqueezeFraction = 0.0f;

	enum class ECachedAtmosphereTargetType : uint8
	{
		DirectionalLight,
		SkyLight,
		SkyAtmosphere,
		ExponentialHeightFog
	};

	struct FCachedWorldSkyAndLightState
	{
		ECachedAtmosphereTargetType TargetType = ECachedAtmosphereTargetType::DirectionalLight;
		TWeakObjectPtr<ULightComponent> LightComponent;
		TWeakObjectPtr<USkyLightComponent> SkyLightComponent;
		TWeakObjectPtr<USkyAtmosphereComponent> SkyAtmosphereComponent;
		TWeakObjectPtr<UExponentialHeightFogComponent> ExponentialFogComponent;
		float OriginalIntensity = 1.0f;
		FLinearColor OriginalPrimaryColor = FLinearColor::White;
		FLinearColor OriginalSecondaryColor = FLinearColor::White;
	};

	TArray<FCachedWorldSkyAndLightState> CachedWorldSkyAndLights;
	bool bWorldSkyAndLightsCached = false;
	float CurrentSkyDarknessAlpha = 0.0f;
	bool bIsDiscoActive = false;
	float DiscoElapsedTime = 0.0f;
	float DiscoOrbitYaw = 0.0f;

	int32 NextShockwaveStageIndex = 0;
	int32 CurrentDiscoColorMode = 0;
	bool bTriggeredPopOutEffect = false;
	bool bIsPopOutActive = false;
	bool bHasDespawnedCat = false;
	float PopOutElapsed = 0.0f;
	float PopOutDuration = 0.15f;
	float LastShockwavePostProcessAlpha = 0.0f;

	bool bShockwaveVisualActive = false;
	float ShockwaveElapsed = 0.0f;
	float ShockwaveDuration = 0.90f;
	float ShockwaveMaxRadius = 2200.0f;
	float ActiveShockwaveRadiusScale = 1.0f;
	FVector ShockwaveOrigin = FVector::ZeroVector;
	FLinearColor ActiveShockwaveColor = FLinearColor::White;

	float UnstableMatterIntensity = 0.0f;
	FVector UnstableScaleMultiplier = FVector::OneVector;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> ActiveAudioComponent = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> RuntimeSkeletalMeshComponent = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<USphereComponent> InteractionTraceSphere = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> DiscoRigPivot = nullptr;

	UPROPERTY(Transient)
	TArray<TObjectPtr<USpotLightComponent>> DiscoSpotlights;

	UPROPERTY(Transient)
	TObjectPtr<UPointLightComponent> DiscoFillPointLight = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UPostProcessComponent> DiscoPostProcessComponent = nullptr;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> ShockwaveRingSegments;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> ShockwaveRingMaterial = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UPointLightComponent> ShockwaveFlashPointLight = nullptr;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> CatMaterialInstances;
};
