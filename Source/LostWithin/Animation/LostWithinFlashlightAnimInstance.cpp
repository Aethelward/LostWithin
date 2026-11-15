// Copyright Epic Games, Inc. All Rights Reserved.

#include "Animation/LostWithinFlashlightAnimInstance.h"
#include "Animation/AnimSequence.h"
#include "BonePose.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"

const TCHAR* ULostWithinFlashlightAnimInstance::FlashlightIdleAnimPath =
	TEXT("/Game/Characters/Animations/SurvivalSet/Mannequin/Flashlight/Idle/RightHand/Type01/A_Flashlight_Idle_01_R.A_Flashlight_Idle_01_R");

const TCHAR* ULostWithinFlashlightAnimInstance::ItemPickupAnimPath =
	TEXT("/Game/Characters/Animations/ItemPickupSet/Mannequin/A_ItemPickup_fromIdle_RH_100cm.A_ItemPickup_fromIdle_RH_100cm");

namespace
{
	void SetupSingleBoneLayeredBlend(
		FAnimNode_LayeredBoneBlend& BlendNode,
		FAnimNode_Base* BaseNode,
		FAnimNode_Base* OverlayNode,
		const FName& BranchBoneName)
	{
		BlendNode.BasePose.SetLinkNode(BaseNode);
		BlendNode.BlendMode = ELayeredBoneBlendMode::BranchFilter;
		BlendNode.bMeshSpaceRotationBlend = true;
		BlendNode.bRootSpaceRotationBlend = false;
		BlendNode.bMeshSpaceScaleBlend = false;
		BlendNode.CurveBlendOption = ECurveBlendOption::Override;

		BlendNode.BlendPoses.SetNum(1);
		BlendNode.BlendPoses[0].SetLinkNode(OverlayNode);

		BlendNode.BlendWeights.SetNum(1);
		BlendNode.BlendWeights[0] = 0.0f;

		BlendNode.LayerSetup.SetNum(1);
		BlendNode.LayerSetup[0].BranchFilters.SetNum(1);
		BlendNode.LayerSetup[0].BranchFilters[0].BoneName = BranchBoneName;
		BlendNode.LayerSetup[0].BranchFilters[0].BlendDepth = 0;
		BlendNode.InvalidatePerBoneBlendWeights();
	}

	/** Maps Actor-space rotation delta into SkeletalMesh Component Space */
	FQuat ActorRotationToComponentSpace(const FQuat& ComponentToActorQuat, const FQuat& ActorSpaceQuat)
	{
		return (ComponentToActorQuat.Inverse() * ActorSpaceQuat * ComponentToActorQuat).GetNormalized();
	}

	/** Distributes TotalPitchDeg across a weighted spine/neck/head bone chain around each bone's CS pivot */
	void BuildArticulatedSpineTransforms(
		FCSPose<FCompactPose>& CSPose,
		TConstArrayView<TPair<FCompactPoseBoneIndex, float>> Chain,
		float TotalPitchDeg,
		const FQuat& ComponentToActorQuat,
		TArray<FBoneTransform>& OutTransforms)
	{
		TArray<FCompactPoseBoneIndex, TInlineAllocator<6>> ValidIndices;
		TArray<float, TInlineAllocator<6>> Weights;
		TArray<FTransform, TInlineAllocator<6>> ArticulatedCS;

		for (const TPair<FCompactPoseBoneIndex, float>& Entry : Chain)
		{
			if (Entry.Key.IsValid())
			{
				ValidIndices.Add(Entry.Key);
				Weights.Add(Entry.Value);
				ArticulatedCS.Add(CSPose.GetComponentSpaceTransform(Entry.Key));
			}
		}

		for (int32 StepIdx = 0; StepIdx < ValidIndices.Num(); ++StepIdx)
		{
			const FQuat StepCS = ActorRotationToComponentSpace(
				ComponentToActorQuat,
				FRotator(TotalPitchDeg * Weights[StepIdx], 0.0f, 0.0f).Quaternion());
			const FVector Pivot = ArticulatedCS[StepIdx].GetLocation();

			for (int32 ChildIdx = StepIdx; ChildIdx < ValidIndices.Num(); ++ChildIdx)
			{
				ArticulatedCS[ChildIdx].SetLocation(Pivot + StepCS.RotateVector(ArticulatedCS[ChildIdx].GetLocation() - Pivot));
				ArticulatedCS[ChildIdx].SetRotation((StepCS * ArticulatedCS[ChildIdx].GetRotation()).GetNormalized());
			}
		}

		OutTransforms.Reset(ValidIndices.Num());
		for (int32 Idx = 0; Idx < ValidIndices.Num(); ++Idx)
		{
			OutTransforms.Emplace(ValidIndices[Idx], ArticulatedCS[Idx]);
		}
		OutTransforms.Sort([](const FBoneTransform& A, const FBoneTransform& B)
		{
			return A.BoneIndex < B.BoneIndex;
		});
	}
}

void FLostWithinFlashlightRigBoneCache::Rebuild(const FBoneContainer& RequiredBones)
{
	auto ResolveBone = [&RequiredBones](const TCHAR* Name) -> FCompactPoseBoneIndex
	{
		const int32 PoseIdx = RequiredBones.GetPoseBoneIndexForBoneName(Name);
		return (PoseIdx != INDEX_NONE)
			? RequiredBones.MakeCompactPoseIndex(FMeshPoseBoneIndex(PoseIdx))
			: FCompactPoseBoneIndex(INDEX_NONE);
	};

	Spine03 = ResolveBone(TEXT("spine_03"));
	Spine04 = ResolveBone(TEXT("spine_04"));
	Spine05 = ResolveBone(TEXT("spine_05"));
	Neck01 = ResolveBone(TEXT("neck_01"));
	Head = ResolveBone(TEXT("head"));

	bValid = Spine03.IsValid() && Spine05.IsValid() && Head.IsValid();
}

void FLostWithinFlashlightRootNode::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	FAnimNode_Base::Initialize_AnyThread(Context);
	if (OwnerProxy)
	{
		OwnerProxy->PickupBlend.Initialize_AnyThread(Context);
	}
}

void FLostWithinFlashlightRootNode::CacheBones_AnyThread(const FAnimationCacheBonesContext& Context)
{
	if (OwnerProxy)
	{
		OwnerProxy->PickupBlend.CacheBones_AnyThread(Context);
		OwnerProxy->RigBones.Rebuild(OwnerProxy->GetRequiredBones());
	}
}

void FLostWithinFlashlightRootNode::Update_AnyThread(const FAnimationUpdateContext& Context)
{
	if (OwnerProxy)
	{
		OwnerProxy->PickupBlend.Update_AnyThread(Context);
	}
}

void FLostWithinFlashlightRootNode::Evaluate_AnyThread(FPoseContext& Output)
{
	if (!OwnerProxy)
	{
		Output.ResetToRefPose();
		return;
	}

	OwnerProxy->PickupBlend.Evaluate_AnyThread(Output);
	OwnerProxy->ApplyAimPitchPostProcess(Output);
}

void FLostWithinFlashlightAnimProxy::ConfigureGraphNodes(const ULostWithinFlashlightAnimInstance* OwnerAnim)
{
	const FName HoldBone = OwnerAnim ? OwnerAnim->FlashlightHoldBranchBone : FName(TEXT("clavicle_r"));
	const FName PickupBone = OwnerAnim ? OwnerAnim->PickupBranchBone : FName(TEXT("spine_03"));

	FlashlightIdlePlayer.SetSequence(OwnerAnim ? OwnerAnim->FlashlightIdleAnim.Get() : nullptr);
	FlashlightIdlePlayer.SetLoopAnimation(true);
	FlashlightIdlePlayer.SetPlayRate(1.0f);
	FlashlightIdlePlayer.SetStartPosition(0.0f);
	FlashlightIdlePlayer.SetIgnoreForRelevancyTest(true);

	PickupPlayer.SetSequence(OwnerAnim ? OwnerAnim->ItemPickupAnim.Get() : nullptr);
	PickupPlayer.SetLoopAnimation(false);
	PickupPlayer.SetPlayRate(OwnerAnim ? OwnerAnim->PickupPlayRate : 1.45f);
	PickupPlayer.SetStartPosition(0.0f);
	PickupPlayer.SetIgnoreForRelevancyTest(true);

	SetupSingleBoneLayeredBlend(FlashlightHoldBlend, &BaseLocomotionNode, &FlashlightIdlePlayer, HoldBone);
	SetupSingleBoneLayeredBlend(PickupBlend, &FlashlightHoldBlend, &PickupPlayer, PickupBone);

	bGraphConfigured = true;
}

void FLostWithinFlashlightAnimProxy::Initialize(UAnimInstance* InAnimInstance)
{
	RootAnimNode.OwnerProxy = this;

	ULostWithinFlashlightAnimInstance* OwnerAnim = Cast<ULostWithinFlashlightAnimInstance>(InAnimInstance);
	if (OwnerAnim)
	{
		OwnerAnim->EnsureApprovedAnimationsLoaded();
	}

	ConfigureGraphNodes(OwnerAnim);

	Super::Initialize(InAnimInstance);
}

FAnimNode_Base* FLostWithinFlashlightAnimProxy::GetCustomRootNode()
{
	return &RootAnimNode;
}

void FLostWithinFlashlightAnimProxy::SetBaseAnimClass(const UAnimInstance* InOwningAnimInstance, TSubclassOf<UAnimInstance> InBaseAnimClass)
{
	if (!InOwningAnimInstance || !InBaseAnimClass)
	{
		return;
	}

	BaseLocomotionNode.SetAnimClass(InBaseAnimClass, InOwningAnimInstance);
}

void FLostWithinFlashlightAnimProxy::PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds)
{
	Super::PreUpdate(InAnimInstance, DeltaSeconds);

	ULostWithinFlashlightAnimInstance* OwnerAnim = Cast<ULostWithinFlashlightAnimInstance>(InAnimInstance);
	if (!OwnerAnim)
	{
		return;
	}

	if (!bGraphConfigured)
	{
		ConfigureGraphNodes(OwnerAnim);
	}

	if (OwnerAnim->BaseAnimClass && BaseLocomotionNode.InstanceClass != OwnerAnim->BaseAnimClass)
	{
		BaseLocomotionNode.SetAnimClass(OwnerAnim->BaseAnimClass, OwnerAnim);
	}

	if (OwnerAnim->FlashlightIdleAnim && FlashlightIdlePlayer.GetSequence() != OwnerAnim->FlashlightIdleAnim)
	{
		FlashlightIdlePlayer.SetSequence(OwnerAnim->FlashlightIdleAnim);
	}

	AimPitch = OwnerAnim->GetAimPitch();

	if (const USkeletalMeshComponent* SkelComp = OwnerAnim->GetSkelMeshComponent())
	{
		if (const AActor* OwnerActor = SkelComp->GetOwner())
		{
			ComponentToActorQuat = (OwnerActor->GetActorQuat().Inverse() * SkelComp->GetComponentQuat()).GetNormalized();
		}
	}

	const float TargetHoldWeight = OwnerAnim->IsHoldingFlashlight() ? 1.0f : 0.0f;
	HoldBlendWeight = FMath::FInterpTo(HoldBlendWeight, TargetHoldWeight, DeltaSeconds, OwnerAnim->HoldBlendInterpSpeed);
	if (FlashlightHoldBlend.BlendWeights.IsValidIndex(0))
	{
		FlashlightHoldBlend.BlendWeights[0] = HoldBlendWeight;
	}

	if (OwnerAnim->ConsumePickupRequest() && OwnerAnim->ItemPickupAnim)
	{
		const float EffectivePlayRate = FMath::Max(0.25f, OwnerAnim->PickupPlayRate);
		PickupPlayer.SetSequence(OwnerAnim->ItemPickupAnim);
		PickupPlayer.SetLoopAnimation(false);
		PickupPlayer.SetPlayRate(EffectivePlayRate);
		PickupPlayer.SetStartPosition(0.0f);
		PickupPlayer.SetAccumulatedTime(0.0f);

		bIsPlayingPickup = true;
		PickupElapsed = 0.0f;
		PickupBlendWeight = 0.0f;
	}

	if (bIsPlayingPickup && OwnerAnim->ItemPickupAnim)
	{
		const float EffectivePlayRate = FMath::Max(0.25f, OwnerAnim->PickupPlayRate);
		const float RawLength = FMath::Max(0.1f, OwnerAnim->ItemPickupAnim->GetPlayLength());
		const float TotalDuration = RawLength / EffectivePlayRate;

		PickupElapsed += DeltaSeconds;

		if (PickupElapsed >= TotalDuration)
		{
			bIsPlayingPickup = false;
			PickupBlendWeight = 0.0f;
		}
		else
		{
			const float InAlpha = (OwnerAnim->PickupBlendInDuration > KINDA_SMALL_NUMBER)
				? FMath::SmoothStep(0.0f, 1.0f, FMath::Clamp(PickupElapsed / OwnerAnim->PickupBlendInDuration, 0.0f, 1.0f))
				: 1.0f;

			const float RemainingTime = TotalDuration - PickupElapsed;
			const float OutAlpha = (OwnerAnim->PickupBlendOutDuration > KINDA_SMALL_NUMBER)
				? FMath::SmoothStep(0.0f, 1.0f, FMath::Clamp(RemainingTime / OwnerAnim->PickupBlendOutDuration, 0.0f, 1.0f))
				: 1.0f;

			PickupBlendWeight = FMath::Min(InAlpha, OutAlpha);
		}
	}
	else
	{
		PickupBlendWeight = 0.0f;
	}

	if (PickupBlend.BlendWeights.IsValidIndex(0))
	{
		PickupBlend.BlendWeights[0] = PickupBlendWeight;
	}
}

void FLostWithinFlashlightAnimProxy::ApplyAimPitchPostProcess(FPoseContext& InOutPose)
{
	ApplyUpperBodyAimPitch(InOutPose, AimPitch);
}

void FLostWithinFlashlightAnimProxy::ApplyUpperBodyAimPitch(FPoseContext& InOutPose, float PitchDeg) const
{
	const float ClampedPitch = FMath::Clamp(PitchDeg, -85.0f, 85.0f);
	if (!RigBones.bValid || FMath::Abs(ClampedPitch) <= 0.1f)
	{
		return;
	}

	FCSPose<FCompactPose> CSPose;
	CSPose.InitPose(InOutPose.Pose);

	const TPair<FCompactPoseBoneIndex, float> SpineHeadChain[] =
	{
		{ RigBones.Spine03, 0.20f },
		{ RigBones.Spine04, 0.22f },
		{ RigBones.Spine05, 0.25f },
		{ RigBones.Neck01,  0.13f },
		{ RigBones.Head,    0.20f }
	};

	TArray<FBoneTransform> BoneTransforms;
	BuildArticulatedSpineTransforms(CSPose, SpineHeadChain, ClampedPitch, ComponentToActorQuat, BoneTransforms);
	if (BoneTransforms.Num() > 0)
	{
		CSPose.LocalBlendCSBoneTransforms(BoneTransforms, 1.0f);
		FCSPose<FCompactPose>::ConvertComponentPosesToLocalPosesSafe(CSPose, InOutPose.Pose);
		InOutPose.Pose.NormalizeRotations();
	}
}

ULostWithinFlashlightAnimInstance::ULostWithinFlashlightAnimInstance()
{
}

void ULostWithinFlashlightAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();
	EnsureApprovedAnimationsLoaded();
}

FAnimInstanceProxy* ULostWithinFlashlightAnimInstance::CreateAnimInstanceProxy()
{
	return new FLostWithinFlashlightAnimProxy(this);
}

void ULostWithinFlashlightAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy)
{
	delete InProxy;
}

void ULostWithinFlashlightAnimInstance::EnsureApprovedAnimationsLoaded()
{
	if (!FlashlightIdleAnim)
	{
		FlashlightIdleAnim = LoadObject<UAnimSequence>(nullptr, FlashlightIdleAnimPath);
	}

	if (!ItemPickupAnim)
	{
		ItemPickupAnim = LoadObject<UAnimSequence>(nullptr, ItemPickupAnimPath);
	}
}

void ULostWithinFlashlightAnimInstance::SetBaseAnimClass(TSubclassOf<UAnimInstance> InBaseAnimClass)
{
	BaseAnimClass = InBaseAnimClass;

	if (BaseAnimClass)
	{
		FLostWithinFlashlightAnimProxy& Proxy = GetProxyOnGameThread<FLostWithinFlashlightAnimProxy>();
		Proxy.SetBaseAnimClass(this, BaseAnimClass);
		RecalcRequiredBones();
	}
}

void ULostWithinFlashlightAnimInstance::SetHasFlashlight(bool bInHasFlashlight)
{
	bHasFlashlight = bInHasFlashlight;
}

void ULostWithinFlashlightAnimInstance::SetAimPitch(float InAimPitch)
{
	AimPitch = FMath::Clamp(InAimPitch, -89.0f, 89.0f);
}

void ULostWithinFlashlightAnimInstance::PlayPickupAnimation()
{
	EnsureApprovedAnimationsLoaded();
	if (ItemPickupAnim)
	{
		bPendingPickupTrigger = true;
	}
}

bool ULostWithinFlashlightAnimInstance::ConsumePickupRequest()
{
	const bool bWasPending = bPendingPickupTrigger;
	bPendingPickupTrigger = false;
	return bWasPending;
}

