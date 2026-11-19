#include "Animation/LostWithinMetaHumanAnimInstance.h"

#include "Components/SkeletalMeshComponent.h"
#include "LostWithinCharacter.h"
#include "Retargeter/IKRetargeter.h"

ULostWithinMetaHumanAnimInstance::ULostWithinMetaHumanAnimInstance() : Proxy(this) {}

void ULostWithinMetaHumanAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();
	const ALostWithinCharacter* Character = Cast<ALostWithinCharacter>(GetOwningActor());
	if (Character && GetSkelMeshComponent() == Character->GetPresentationMesh())
	{
		Retargeter = LoadObject<UIKRetargeter>(nullptr, RetargeterPath);
	}
}

FAnimInstanceProxy* ULostWithinMetaHumanAnimInstance::CreateAnimInstanceProxy() { return &Proxy; }
void ULostWithinMetaHumanAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) {}

void FLostWithinMetaHumanAnimProxy::Initialize(UAnimInstance* Instance)
{
	const ALostWithinCharacter* Character = Cast<ALostWithinCharacter>(Instance->GetOwningActor());
	bRetarget = Character && Instance->GetSkelMeshComponent() == Character->GetPresentationMesh();
	if (bRetarget)
	{
		// UAnimInstance::InitializeAnimation initializes the proxy before NativeInitializeAnimation, so the retargeter
		// must be loaded here; a null asset leaves the visible body in its reference (T/A) pose.
		ULostWithinMetaHumanAnimInstance* MetaHuman = CastChecked<ULostWithinMetaHumanAnimInstance>(Instance);
		if (!MetaHuman->Retargeter)
		{
			MetaHuman->Retargeter = LoadObject<UIKRetargeter>(nullptr, ULostWithinMetaHumanAnimInstance::RetargeterPath);
		}
		Retarget.SourceMeshComponent = Character->GetMesh();
		Retarget.IKRetargeterAsset = MetaHuman->Retargeter;
	}
	else
	{
		Copy.SourceMeshComponent = Character ? Character->GetPresentationMesh() : nullptr;
	}
	FAnimInstanceProxy::Initialize(Instance);
}

void FLostWithinMetaHumanAnimProxy::PreUpdate(UAnimInstance* Instance, float DeltaSeconds)
{
	FAnimInstanceProxy::PreUpdate(Instance, DeltaSeconds);
	if (bRetarget) { Retarget.PreUpdate(Instance); }
	else { Copy.PreUpdate(Instance); }
}
