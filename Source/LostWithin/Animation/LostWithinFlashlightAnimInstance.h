// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNode_LinkedAnimGraph.h"
#include "Animation/AnimNode_SequencePlayer.h"
#include "AnimNodes/AnimNode_LayeredBoneBlend.h"
#include "BoneContainer.h"
#include "LostWithinFlashlightAnimInstance.generated.h"

class UAnimSequence;
struct FLostWithinFlashlightAnimProxy;

/** Cached compact-pose bone indices for 3rd-person upper-body/neck/head pitch articulation */
struct FLostWithinFlashlightRigBoneCache
{
	FCompactPoseBoneIndex Spine03{ INDEX_NONE };
	FCompactPoseBoneIndex Spine04{ INDEX_NONE };
	FCompactPoseBoneIndex Spine05{ INDEX_NONE };
	FCompactPoseBoneIndex Neck01{ INDEX_NONE };
	FCompactPoseBoneIndex Head{ INDEX_NONE };

	bool bValid = false;

	void Rebuild(const FBoneContainer& RequiredBones);
};

/** Custom AnimGraph root node wrapping PickupBlend and applying 3rd-person upper-body/head pitch post-processing */
struct FLostWithinFlashlightRootNode : public FAnimNode_Base
{
	FLostWithinFlashlightAnimProxy* OwnerProxy = nullptr;

	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
	virtual void CacheBones_AnyThread(const FAnimationCacheBonesContext& Context) override;
	virtual void Update_AnyThread(const FAnimationUpdateContext& Context) override;
	virtual void Evaluate_AnyThread(FPoseContext& Output) override;
};

/**
 *  Native animation proxy that evaluates the base unarmed locomotion AnimBP as a linked graph,
 *  layers only the two approved animations on top:
 *  - A_Flashlight_Idle_01_R on the right shoulder/arm (clavicle_r)
 *  - A_ItemPickup_fromIdle_RH_100cm on the upper torso/arm (spine_03) without blocking locomotion
 *  and articulates upper-body/neck/head pitch smoothly in 3rd-person view.
 */
USTRUCT()
struct LOSTWITHIN_API FLostWithinFlashlightAnimProxy : public FAnimInstanceProxy
{
	GENERATED_BODY()

	friend struct FLostWithinFlashlightRootNode;

public:
	FLostWithinFlashlightAnimProxy()
	{
		RootAnimNode.OwnerProxy = this;
	}
	explicit FLostWithinFlashlightAnimProxy(UAnimInstance* InAnimInstance)
		: FAnimInstanceProxy(InAnimInstance)
	{
		RootAnimNode.OwnerProxy = this;
	}

	virtual void Initialize(UAnimInstance* InAnimInstance) override;
	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
	virtual FAnimNode_Base* GetCustomRootNode() override;

	/** Attaches or swaps the underlying locomotion AnimBlueprint class (e.g. ABP_Unarmed_C) */
	void SetBaseAnimClass(const UAnimInstance* InOwningAnimInstance, TSubclassOf<UAnimInstance> InBaseAnimClass);

private:
	/** Configures pose links and branch filters once prior to root initialization */
	void ConfigureGraphNodes(const class ULostWithinFlashlightAnimInstance* OwnerAnim);

	/** Applies 3rd-person upper-body/neck/head aim pitch post-processing */
	void ApplyAimPitchPostProcess(FPoseContext& InOutPose);

	/** Distributes AimPitch across spine_03..05, neck_01, and head on the 3P mesh */
	void ApplyUpperBodyAimPitch(FPoseContext& InOutPose, float PitchDeg) const;

	FLostWithinFlashlightRootNode RootAnimNode;
	FAnimNode_LinkedAnimGraph BaseLocomotionNode;
	FAnimNode_SequencePlayer_Standalone FlashlightIdlePlayer;
	FAnimNode_LayeredBoneBlend FlashlightHoldBlend;
	FAnimNode_SequencePlayer_Standalone PickupPlayer;
	FAnimNode_LayeredBoneBlend PickupBlend;

	FLostWithinFlashlightRigBoneCache RigBones;
	FQuat ComponentToActorQuat = FRotator(0.0f, -90.0f, 0.0f).Quaternion();

	float HoldBlendWeight = 0.0f;
	float PickupBlendWeight = 0.0f;
	float PickupElapsed = 0.0f;
	float AimPitch = 0.0f;
	bool bIsPlayingPickup = false;
	bool bGraphConfigured = false;
};

/**
 *  Native UAnimInstance wrapper applied to the 3rd-person player mesh so smooth flashlight hold,
 *  item pickup overlays, and body/face pitch articulation work together.
 */
UCLASS(Transient)
class LOSTWITHIN_API ULostWithinFlashlightAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

	friend struct FLostWithinFlashlightAnimProxy;

public:
	ULostWithinFlashlightAnimInstance();

	/** Asset path for the right-hand flashlight idle loop */
	static const TCHAR* FlashlightIdleAnimPath;

	/** Asset path for the right-hand item pickup sequence */
	static const TCHAR* ItemPickupAnimPath;

	/** Assigns the base locomotion AnimBlueprint class (e.g. ABP_Unarmed_C) and initializes it */
	UFUNCTION(BlueprintCallable, Category = "Animation|Flashlight")
	void SetBaseAnimClass(TSubclassOf<UAnimInstance> InBaseAnimClass);

	/** Controls whether the right-arm A_Flashlight_Idle_01_R hold pose should blend in */
	UFUNCTION(BlueprintCallable, Category = "Animation|Flashlight")
	void SetHasFlashlight(bool bInHasFlashlight);

	/** Updates clamped POV pitch (degrees, positive = looking up, negative = looking down) */
	UFUNCTION(BlueprintCallable, Category = "Animation|Aim")
	void SetAimPitch(float InAimPitch);

	/** Plays A_ItemPickup_fromIdle_RH_100cm on the upper body / right arm overlay */
	UFUNCTION(BlueprintCallable, Category = "Animation|Flashlight")
	void PlayPickupAnimation();

	bool IsHoldingFlashlight() const { return bHasFlashlight; }
	float GetAimPitch() const { return AimPitch; }

	/** Interpolation speed for blending in/out A_Flashlight_Idle_01_R */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Flashlight", meta = (ClampMin = "0.1"))
	float HoldBlendInterpSpeed = 10.0f;

	/** Playback speed multiplier for A_ItemPickup_fromIdle_RH_100cm (2.0s raw -> ~1.38s at 1.45x) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Pickup", meta = (ClampMin = "0.25", ClampMax = "4.0"))
	float PickupPlayRate = 1.45f;

	/** Fade-in duration for the pickup upper-body blend */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Pickup", meta = (ClampMin = "0.01", Units = "s"))
	float PickupBlendInDuration = 0.12f;

	/** Fade-out duration for the pickup upper-body blend */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Pickup", meta = (ClampMin = "0.01", Units = "s"))
	float PickupBlendOutDuration = 0.25f;

	/** Bone filter root used when blending A_Flashlight_Idle_01_R */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Flashlight")
	FName FlashlightHoldBranchBone = TEXT("clavicle_r");

	/** Bone filter root used when blending A_ItemPickup_fromIdle_RH_100cm */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Pickup")
	FName PickupBranchBone = TEXT("spine_03");

protected:
	virtual void NativeInitializeAnimation() override;
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;

private:
	/** Ensures only the two approved AnimSequence assets are loaded */
	void EnsureApprovedAnimationsLoaded();

	/** Returns true once when a new pickup trigger is queued */
	bool ConsumePickupRequest();

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> FlashlightIdleAnim = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> ItemPickupAnim = nullptr;

	UPROPERTY(Transient)
	TSubclassOf<UAnimInstance> BaseAnimClass = nullptr;

	bool bHasFlashlight = false;
	bool bPendingPickupTrigger = false;
	float AimPitch = 0.0f;
};

