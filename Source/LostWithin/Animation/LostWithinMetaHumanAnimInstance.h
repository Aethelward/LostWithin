#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "AnimNodes/AnimNode_CopyPoseFromMesh.h"
#include "AnimNodes/AnimNode_RetargetPoseFromMesh.h"
#include "LostWithinMetaHumanAnimInstance.generated.h"

class UIKRetargeter;

/** Body retargets the final Manny pose; face/clothing copy the resulting visible body pose. */
USTRUCT()
struct FLostWithinMetaHumanAnimProxy : public FAnimInstanceProxy
{
	GENERATED_BODY()
	FLostWithinMetaHumanAnimProxy() = default;
	explicit FLostWithinMetaHumanAnimProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance) {}
	virtual void Initialize(UAnimInstance* Instance) override;
	virtual void PreUpdate(UAnimInstance* Instance, float DeltaSeconds) override;
	virtual FAnimNode_Base* GetCustomRootNode() override { return bRetarget ? static_cast<FAnimNode_Base*>(&Retarget) : static_cast<FAnimNode_Base*>(&Copy); }
	UPROPERTY(Transient) FAnimNode_RetargetPoseFromMesh Retarget;
	UPROPERTY(Transient) FAnimNode_CopyPoseFromMesh Copy;
	bool bRetarget = false;
};

UCLASS(Transient)
class LOSTWITHIN_API ULostWithinMetaHumanAnimInstance : public UAnimInstance
{
	GENERATED_BODY()
	friend struct FLostWithinMetaHumanAnimProxy;
public:
	ULostWithinMetaHumanAnimInstance();
	static constexpr const TCHAR* RetargeterPath = TEXT("/Game/Gameplay/Subject09/RTG_Manny_Evann.RTG_Manny_Evann");
	/** True once the visible body's retarget node has its retargeter (false means it would hold the reference pose). */
	bool HasRetargeterForBody() const { return Proxy.bRetarget && Proxy.Retarget.IKRetargeterAsset != nullptr; }
protected:
	virtual void NativeInitializeAnimation() override;
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* Proxy) override;
private:
	UPROPERTY(Transient) TObjectPtr<UIKRetargeter> Retargeter;
	/** Reflected ownership keeps the retarget processor and copy-pose node references alive across GC. */
	UPROPERTY(Transient) FLostWithinMetaHumanAnimProxy Proxy;
};
