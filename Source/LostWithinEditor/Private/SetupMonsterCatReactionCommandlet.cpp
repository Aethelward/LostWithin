// Copyright Epic Games, Inc. All Rights Reserved.

#include "SetupMonsterCatReactionCommandlet.h"

#include "AI/MonsterAIComponent.h"
#include "AIController.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/SkeletalMeshComponent.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Character.h"
#include "HAL/FileManager.h"
#include "K2Node_CallFunction.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_Self.h"
#include "K2Node_VariableSet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "KismetCompiler.h"
#include "LostWithin.h"
#include "MonsterMovementSetup.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/PhysicsConstraintTemplate.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace
{
	constexpr const TCHAR* GeometryVersionKey = TEXT("LostWithin.MonsterRagdollGeometryVersion");

	// Bones that get bodies in a generated ragdoll; Hips and Head become spheres, the others capsules.
	const TCHAR* const HumanRagdollBones[] = {
		TEXT("Hips"), TEXT("Spine"), TEXT("Spine1"), TEXT("Spine2"), TEXT("Neck"), TEXT("Head"),
		TEXT("LeftShoulder"), TEXT("RightShoulder"), TEXT("LeftUpLeg"), TEXT("LeftLeg"),
		TEXT("LeftFoot"), TEXT("RightUpLeg"), TEXT("RightLeg"), TEXT("RightFoot")
	};
	const TCHAR* const CanineRagdollBones[] = {
		TEXT("Hips"), TEXT("Spine1"), TEXT("Spine3"), TEXT("Neck"), TEXT("Head"),
		TEXT("LeftArm"), TEXT("LeftForeArm"), TEXT("LeftFrontFoot"), TEXT("RightArm"), TEXT("RightForeArm"), TEXT("RightFrontFoot"),
		TEXT("LeftUpLeg"), TEXT("LeftLeg"), TEXT("LeftHindFoot"), TEXT("RightUpLeg"), TEXT("RightLeg"), TEXT("RightHindFoot")
	};

	struct FReactionTarget
	{
		const TCHAR* Label;
		const TCHAR* BlueprintPath;
		/** Generated articulated ragdoll package, or nullptr to keep the mesh's own physics asset. */
		const TCHAR* RagdollPackage;
		TArrayView<const TCHAR* const> RagdollBones;
		const TCHAR* RecoveryBone;
		/** Natural ground speed (cm/s) of the fastest gait clip, which plays faster while panicking. */
		float PanicGaitSpeed;
		/** Navigation is built once, sized for Lunger's capsule; the other monsters roam the same mesh. */
		bool bRepairNavigation;
	};

	const FReactionTarget ReactionTargets[] = {
		{ TEXT("Lunger"), TEXT("/Game/NPC/Lunger/Lunger.Lunger"), TEXT("/Game/NPC/Lunger/PA_MonsterCatRagdoll"),
			MakeArrayView(HumanRagdollBones), TEXT("Hips"), 62.6f, true },
		{ TEXT("BP_BoneDog"), TEXT("/Game/NPC/BoneDog/BP_BoneDog.BP_BoneDog"), TEXT("/Game/NPC/BoneDog/PA_BoneDogCatRagdoll"),
			MakeArrayView(CanineRagdollBones), TEXT("Hips"), 350.0f, false },
		{ TEXT("BP_Doll"), TEXT("/Game/NPC/Doll/BP_Doll.BP_Doll"), TEXT("/Game/NPC/Doll/PA_DollCatRagdoll"),
			MakeArrayView(HumanRagdollBones), TEXT("Hips"), 157.3f, false },
		// The Ghoul's imported physics asset is already articulated (16 bodies, mannequin rig).
		{ TEXT("BP_Ghoul"), TEXT("/Game/NPC/Ghoul/BP_Ghoul.BP_Ghoul"), nullptr, {}, TEXT("pelvis"), 350.0f, false },
	};

	bool ConfigureTarget(UWorld* World, const FReactionTarget& Target);

	bool SaveAsset(UObject* Asset)
	{
		const FString Filename = FPackageName::LongPackageNameToFilename(Asset->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
		if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true))
		{
			UE_LOG(LogLostWithin, Error, TEXT("Cannot create the content folder for %s."), *Asset->GetPathName());
			return false;
		}
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		if (!UPackage::SavePackage(Asset->GetOutermost(), Asset, *Filename, SaveArgs))
		{
			UE_LOG(LogLostWithin, Error, TEXT("Could not save %s. Check source-control/file permissions."), *Asset->GetPathName());
			return false;
		}
		return true;
	}

	struct FGeneratedRagdollShape
	{
		bool bCapsule = false;
		FKSphylElem Capsule;
		FKSphereElem Sphere;
	};

	FGeneratedRagdollShape MakeRagdollShape(const FReferenceSkeleton& Skeleton, const TArray<FTransform>& Bones,
		int32 Index, float MeshHeight, bool bNormalizeBoneScale)
	{
		const FName Bone = Skeleton.GetBoneName(Index);
		const float Scale = bNormalizeBoneScale
			? FMath::Max(UE_SMALL_NUMBER, static_cast<float>(Bones[Index].GetScale3D().GetAbsMax())) : 1.0f;
		const float LocalHeight = MeshHeight / Scale;
		FVector ChildOffset = FVector::ZeroVector;
		for (int32 Child = Index + 1; Child < Skeleton.GetNum(); ++Child)
		{
			if (Skeleton.GetParentIndex(Child) == Index)
			{
				ChildOffset = Bones[Index].InverseTransformPosition(Bones[Child].GetLocation());
				break;
			}
		}
		FGeneratedRagdollShape Result;
		const float Length = static_cast<float>(ChildOffset.Size());
		Result.bCapsule = Bone != TEXT("Hips") && Bone != TEXT("Head") && Length > LocalHeight * 0.015f;
		if (Result.bCapsule)
		{
			Result.Capsule.Center = ChildOffset * 0.5f;
			Result.Capsule.Rotation = FRotationMatrix::MakeFromZ(ChildOffset).Rotator();
			Result.Capsule.Radius = FMath::Clamp(Length * 0.22f, LocalHeight * 0.015f, LocalHeight * 0.055f);
			Result.Capsule.Length = FMath::Max(0.1f / Scale, Length - 2.0f * Result.Capsule.Radius);
		}
		else
		{
			Result.Sphere.Radius = LocalHeight * (Bone == TEXT("Hips") ? 0.075f : Bone == TEXT("Head") ? 0.065f : 0.03f);
		}
		return Result;
	}

	bool MatchesLegacyShape(const USkeletalBodySetup* Body, const FGeneratedRagdollShape& Shape)
	{
		if (Body->GetName() != TEXT("Body_") + Body->BoneName.ToString() || Body->AggGeom.GetElementCount() != 1)
		{
			return false;
		}
		if (Shape.bCapsule && Body->AggGeom.SphylElems.Num() == 1)
		{
			const FKSphylElem& Existing = Body->AggGeom.SphylElems[0];
			return Existing.Center.Equals(Shape.Capsule.Center, 0.001)
				&& Existing.Rotation.Equals(Shape.Capsule.Rotation, 0.001)
				&& FMath::IsNearlyEqual(Existing.Radius, Shape.Capsule.Radius, 0.001f)
				&& FMath::IsNearlyEqual(Existing.Length, Shape.Capsule.Length, 0.001f);
		}
		return !Shape.bCapsule && Body->AggGeom.SphereElems.Num() == 1
			&& Body->AggGeom.SphereElems[0].Center.IsNearlyZero(0.001)
			&& FMath::IsNearlyEqual(Body->AggGeom.SphereElems[0].Radius, Shape.Sphere.Radius, 0.001f);
	}

	void ApplyRagdollShape(USkeletalBodySetup* Body, const FGeneratedRagdollShape& Shape)
	{
		Body->InvalidatePhysicsData();
		Body->AggGeom.EmptyElements();
		if (Shape.bCapsule)
		{
			Body->AggGeom.SphylElems.Add(Shape.Capsule);
		}
		else
		{
			Body->AggGeom.SphereElems.Add(Shape.Sphere);
		}
		Body->DefaultInstance.bUseCCD = true;
		Body->CreatePhysicsMeshes();
	}

	// Shapes use bone-local dimensions; imported bone scale is applied by the physics component.
	UPhysicsAsset* GetOrCreateRagdollAsset(USkeletalMesh* Mesh, const FReactionTarget& Target, bool& bOutNeedsSave)
	{
		bOutNeedsSave = false;
		const FString RagdollPackageName = Target.RagdollPackage;
		const FString RagdollAssetName = FPackageName::GetShortName(RagdollPackageName);
		const FString RagdollObjectPathString = RagdollPackageName + TEXT(".") + RagdollAssetName;
		const TCHAR* RagdollObjectPath = *RagdollObjectPathString;
		const FReferenceSkeleton& Skeleton = Mesh->GetRefSkeleton();
		if (Skeleton.FindBoneIndex(TEXT("Hips")) == INDEX_NONE || Skeleton.FindBoneIndex(TEXT("Head")) == INDEX_NONE)
		{
			UE_LOG(LogLostWithin, Error, TEXT("The %s mesh must have Hips and Head bones for a generated ragdoll."), Target.Label);
			return nullptr;
		}
		TArray<FTransform> BoneTransforms;
		BoneTransforms.SetNum(Skeleton.GetNum());
		for (int32 Index = 0; Index < Skeleton.GetNum(); ++Index)
		{
			const int32 Parent = Skeleton.GetParentIndex(Index);
			BoneTransforms[Index] = Parent == INDEX_NONE ? Skeleton.GetRefBonePose()[Index]
				: Skeleton.GetRefBonePose()[Index] * BoneTransforms[Parent];
		}
		const float Height = FMath::Max(1.0f, static_cast<float>(Mesh->GetBounds().BoxExtent.Z * 2.0));
		UObject* Existing = FindObject<UObject>(nullptr, RagdollObjectPath);
		const bool bExistsOnDisk = FPackageName::DoesPackageExist(RagdollPackageName);
		if (!Existing && bExistsOnDisk)
		{
			Existing = LoadObject<UObject>(nullptr, RagdollObjectPath);
		}
		if (Existing || bExistsOnDisk)
		{
			UPhysicsAsset* Asset = Cast<UPhysicsAsset>(Existing);
			if (!Asset || Asset->SkeletalBodySetups.Num() < 6 || Asset->ConstraintSetup.Num() < Asset->SkeletalBodySetups.Num() - 1
				|| Asset->FindBodyIndex(TEXT("Hips")) == INDEX_NONE)
			{
				UE_LOG(LogLostWithin, Error, TEXT("%s already exists but is not an articulated monster ragdoll; leaving it unchanged."), RagdollObjectPath);
				return nullptr;
			}
			bOutNeedsSave = !bExistsOnDisk;
			FMetaData& Metadata = Asset->GetOutermost()->GetMetaData();
			if (Metadata.GetValue(Asset, GeometryVersionKey) != TEXT("2"))
			{
				bool bUntunedLegacyGeometry = true;
				for (const USkeletalBodySetup* Body : Asset->SkeletalBodySetups)
				{
					const int32 Index = Body ? Skeleton.FindBoneIndex(Body->BoneName) : INDEX_NONE;
					if (Index == INDEX_NONE || !MatchesLegacyShape(Body, MakeRagdollShape(Skeleton, BoneTransforms, Index, Height, false)))
					{
						bUntunedLegacyGeometry = false;
						break;
					}
				}
				if (bUntunedLegacyGeometry)
				{
					Asset->Modify();
					for (USkeletalBodySetup* Body : Asset->SkeletalBodySetups)
					{
						Body->Modify();
						ApplyRagdollShape(Body, MakeRagdollShape(Skeleton, BoneTransforms,
							Skeleton.FindBoneIndex(Body->BoneName), Height, true));
					}
					Metadata.SetValue(Asset, GeometryVersionKey, TEXT("2"));
					Asset->UpdateBoundsBodiesArray();
					Asset->MarkPackageDirty();
					bOutNeedsSave = true;
					UE_LOG(LogLostWithin, Display, TEXT("Corrected legacy generated ragdoll geometry for imported bone scale (mesh height %.2f cm, Hips reference scale %.2f); body settings and joints retained."),
						Height, BoneTransforms[Skeleton.FindBoneIndex(TEXT("Hips"))].GetScale3D().GetAbsMax());
				}
			}
			return Asset;
		}

		UPackage* Package = CreatePackage(*RagdollPackageName);
		UPhysicsAsset* Asset = NewObject<UPhysicsAsset>(Package, FName(*RagdollAssetName), RF_Public | RF_Standalone | RF_Transactional);
		TSet<FName> PhysicalBones;
		for (const TCHAR* Bone : Target.RagdollBones)
		{
			PhysicalBones.Add(FName(Bone));
		}
		for (int32 Index = 0; Index < Skeleton.GetNum(); ++Index)
		{
			const FName Bone = Skeleton.GetBoneName(Index);
			if (!PhysicalBones.Contains(Bone))
			{
				continue;
			}
			USkeletalBodySetup* Body = NewObject<USkeletalBodySetup>(Asset, FName(*(TEXT("Body_") + Bone.ToString())), RF_Transactional);
			Body->BoneName = Bone;
			Body->PhysicsType = PhysType_Default;
			Body->CollisionTraceFlag = CTF_UseSimpleAsComplex;
			Body->DefaultInstance.SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			Body->DefaultInstance.LinearDamping = 0.1f;
			Body->DefaultInstance.AngularDamping = 0.5f;

			ApplyRagdollShape(Body, MakeRagdollShape(Skeleton, BoneTransforms, Index, Height, true));
			Asset->SkeletalBodySetups.Add(Body);
		}
		Asset->UpdateBodySetupIndexMap();
		for (int32 BodyIndex = 0; BodyIndex < Asset->SkeletalBodySetups.Num(); ++BodyIndex)
		{
			const FName Bone = Asset->SkeletalBodySetups[BodyIndex]->BoneName;
			const int32 BoneIndex = Skeleton.FindBoneIndex(Bone);
			int32 ParentBoneIndex = Skeleton.GetParentIndex(BoneIndex);
			while (ParentBoneIndex != INDEX_NONE && Asset->FindBodyIndex(Skeleton.GetBoneName(ParentBoneIndex)) == INDEX_NONE)
			{
				ParentBoneIndex = Skeleton.GetParentIndex(ParentBoneIndex);
			}
			if (ParentBoneIndex != INDEX_NONE)
			{
				UPhysicsConstraintTemplate* Constraint = NewObject<UPhysicsConstraintTemplate>(Asset, FName(*(TEXT("Joint_") + Bone.ToString())), RF_Transactional);
				FConstraintInstance& Joint = Constraint->DefaultInstance;
				Joint.JointName = Bone;
				Joint.ConstraintBone1 = Bone;
				Joint.ConstraintBone2 = Skeleton.GetBoneName(ParentBoneIndex);
				Joint.SetLinearXLimit(LCM_Locked, 0.0f);
				Joint.SetLinearYLimit(LCM_Locked, 0.0f);
				Joint.SetLinearZLimit(LCM_Locked, 0.0f);
				Joint.SetAngularSwing1Limit(ACM_Limited, 45.0f);
				Joint.SetAngularSwing2Limit(ACM_Limited, 45.0f);
				Joint.SetAngularTwistLimit(ACM_Limited, 30.0f);
				Joint.SetDisableCollision(true);
				Joint.SetRefFrame(EConstraintFrame::Frame1, FTransform::Identity);
				FTransform ParentFrame = BoneTransforms[BoneIndex].GetRelativeTransform(BoneTransforms[ParentBoneIndex]);
				ParentFrame.SetScale3D(FVector::OneVector);
				Joint.SetRefFrame(EConstraintFrame::Frame2, ParentFrame);
				Asset->ConstraintSetup.Add(Constraint);
			}
			// World collision stays enabled; suppress self-collision of overlapping fitted shapes.
			for (int32 Other = 0; Other < BodyIndex; ++Other)
			{
				Asset->DisableCollision(BodyIndex, Other);
			}
		}
		Asset->UpdateBoundsBodiesArray();
		Asset->PreviewSkeletalMesh = Mesh;
		Package->GetMetaData().SetValue(Asset, GeometryVersionKey, TEXT("2"));
		Asset->MarkPackageDirty();
		FAssetRegistryModule::AssetCreated(Asset);
		bOutNeedsSave = true;
		return Asset;
	}

	bool IsMovementNode(UEdGraphNode* Node)
	{
		if (const UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node))
		{
			const FName Name = Call->FunctionReference.GetMemberName();
			return Name == TEXT("SimpleMoveToLocation") || Name == TEXT("SimpleMoveToActor")
				|| Name == TEXT("MoveToLocation") || Name == TEXT("MoveToActor") || Name == TEXT("StopMovement")
				|| Name == TEXT("K2_SetActorLocation") || Name == TEXT("K2_SetActorLocationAndRotation");
		}
		const UK2Node_VariableSet* Set = Cast<UK2Node_VariableSet>(Node);
		return Set && Set->VariableReference.GetMemberName() == TEXT("MaxWalkSpeed");
	}

	void ResetReactionTemplate(UMonsterAIComponent* Component)
	{
		Component->Modify();
		Component->bCatIsNear = false;
		Component->bPlayerIsNear = false;
		Component->bIsFleeing = false;
		Component->bIsRagdolled = false;
		Component->PrimaryComponentTick.bCanEverTick = true;
		Component->PrimaryComponentTick.bStartWithTickEnabled = true;
		Component->SetAutoActivate(true);
	}

	void ConfigureReactionTemplate(UMonsterAIComponent* Component, const FReactionTarget& Target, const UPhysicsAsset* Ragdoll)
	{
		Component->Modify();
		// Fill only an unset panic gait speed so later per-Blueprint tuning survives repeated setup.
		if (Component->PanicGaitSpeed <= 0.0f)
		{
			Component->PanicGaitSpeed = Target.PanicGaitSpeed;
		}
		if (Ragdoll->FindBodyIndex(Component->RagdollRecoveryBone) == INDEX_NONE)
		{
			Component->RagdollRecoveryBone = Target.RecoveryBone;
		}
	}

	bool IsMovementGuarded(const UEdGraphPin* Input)
	{
		for (const UEdGraphPin* Link : Input->LinkedTo)
		{
			bool bGuardedLink = false;
			UK2Node_IfThenElse* Branch = Cast<UK2Node_IfThenElse>(Link->GetOwningNode());
			if (Branch && Link == Branch->GetThenPin())
			{
				for (const UEdGraphPin* ConditionLink : Branch->GetConditionPin()->LinkedTo)
				{
					const UK2Node_CallFunction* Query = Cast<UK2Node_CallFunction>(ConditionLink->GetOwningNode());
					if (Query && Query->FunctionReference.GetMemberName() == GET_FUNCTION_NAME_CHECKED(UMonsterAIComponent, CanRunNormalAI))
					{
						bGuardedLink = true;
						break;
					}
				}
			}
			if (!bGuardedLink)
			{
				return false;
			}
		}
		return true;
	}

	bool GuardMovementNode(UEdGraph* Graph, UEdGraphNode* Node)
	{
		UEdGraphPin* Input = Node->FindPin(UEdGraphSchema_K2::PN_Execute);
		UEdGraphPin* Output = Node->FindPin(UEdGraphSchema_K2::PN_Then);
		if (!Input || !Output)
		{
			UE_LOG(LogLostWithin, Error, TEXT("Cannot guard movement node %s: expected execute/then pins."), *Node->GetName());
			return false;
		}
		if (Input->LinkedTo.IsEmpty() || IsMovementGuarded(Input))
		{
			return true;
		}
		const UEdGraphSchema_K2* Schema = CastChecked<UEdGraphSchema_K2>(Graph->GetSchema());
		Graph->Modify();
		Node->Modify();
		const TArray<UEdGraphPin*> PreviousInputs = Input->LinkedTo;
		const TArray<UEdGraphPin*> NextInputs = Output->LinkedTo;

		FGraphNodeCreator<UK2Node_IfThenElse> BranchCreator(*Graph);
		UK2Node_IfThenElse* Branch = BranchCreator.CreateNode();
		Branch->NodePosX = Node->NodePosX - 320;
		Branch->NodePosY = Node->NodePosY;
		Branch->NodeComment = TEXT("Active cat reaction takes priority; skip only this movement command.");
		Branch->bCommentBubbleVisible = true;
		BranchCreator.Finalize();

		FGraphNodeCreator<UK2Node_CallFunction> QueryCreator(*Graph);
		UK2Node_CallFunction* Query = QueryCreator.CreateNode();
		Query->SetFromFunction(UMonsterAIComponent::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UMonsterAIComponent, CanRunNormalAI)));
		Query->NodePosX = Branch->NodePosX - 250;
		Query->NodePosY = Branch->NodePosY + 100;
		QueryCreator.Finalize();

		FGraphNodeCreator<UK2Node_Self> SelfCreator(*Graph);
		UK2Node_Self* Self = SelfCreator.CreateNode();
		Self->NodePosX = Query->NodePosX - 160;
		Self->NodePosY = Query->NodePosY;
		SelfCreator.Finalize();

		bool bConnected = Schema->TryCreateConnection(Self->FindPinChecked(UEdGraphSchema_K2::PN_Self), Query->FindPinChecked(TEXT("Actor")))
			&& Schema->TryCreateConnection(Query->GetReturnValuePin(), Branch->GetConditionPin());
		Input->BreakAllPinLinks();
		bConnected &= Schema->TryCreateConnection(Branch->GetThenPin(), Input);
		for (UEdGraphPin* Previous : PreviousInputs)
		{
			bConnected &= Schema->TryCreateConnection(Previous, Branch->GetExecPin());
		}
		for (UEdGraphPin* Next : NextInputs)
		{
			// A skipped movement still continues its original Delay/roam scheduling chain.
			bConnected &= Schema->TryCreateConnection(Branch->GetElsePin(), Next);
		}
		if (!bConnected)
		{
			// Leave the original movement chain intact if any schema connection fails.
			for (UEdGraphNode* Added : TArray<UEdGraphNode*>{Branch, Query, Self})
			{
				for (UEdGraphPin* Pin : Added->Pins)
				{
					Pin->BreakAllPinLinks();
				}
				Graph->RemoveNode(Added);
			}
			for (UEdGraphPin* Previous : PreviousInputs)
			{
				Previous->MakeLinkTo(Input);
			}
			for (UEdGraphPin* Next : NextInputs)
			{
				Output->MakeLinkTo(Next);
			}
			UE_LOG(LogLostWithin, Error, TEXT("Could not connect the cat priority guard for %s; Blueprint will not be saved."), *Node->GetName());
		}
		return bConnected;
	}
}

USetupMonsterCatReactionCommandlet::USetupMonsterCatReactionCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 USetupMonsterCatReactionCommandlet::Main(const FString& Params)
{
	return ConfigureMonster() ? 0 : 1;
}

bool USetupMonsterCatReactionCommandlet::ConfigureMonster()
{
	UWorld* World = LostWithinMonsterSetup::GetSetupWorld();
	if (!World)
	{
		return false;
	}
	// Configure every monster even if one fails, so a single bad asset does not block the others.
	bool bAllConfigured = true;
	for (const FReactionTarget& Target : ReactionTargets)
	{
		bAllConfigured &= ConfigureTarget(World, Target);
	}
	if (bAllConfigured)
	{
		UE_LOG(LogLostWithin, Display, TEXT("Cat reaction configured for %d monsters: any active float-spin, 5 m fear, 3 m capsule-surface throw, 1.5x panic gait and animation, 3 s recovery with a 2 s stand-up fallback."),
			UE_ARRAY_COUNT(ReactionTargets));
	}
	return bAllConfigured;
}

namespace
{
bool ConfigureTarget(UWorld* World, const FReactionTarget& Target)
{
	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, Target.BlueprintPath);
	ACharacter* Character = Blueprint && Blueprint->GeneratedClass ? Cast<ACharacter>(Blueprint->GeneratedClass->GetDefaultObject()) : nullptr;
	if (!Character || !Character->GetMesh() || !Character->GetMesh()->GetSkeletalMeshAsset() || !Blueprint->SimpleConstructionScript)
	{
		UE_LOG(LogLostWithin, Error, TEXT("%s must be a Character Blueprint with a skeletal mesh."), Target.Label);
		return false;
	}
	bool bNeedsPhysicsSave = false;
	UPhysicsAsset* Ragdoll = Target.RagdollPackage
		? GetOrCreateRagdollAsset(Character->GetMesh()->GetSkeletalMeshAsset(), Target, bNeedsPhysicsSave)
		: Character->GetMesh()->GetPhysicsAsset();
	if (!Ragdoll || Ragdoll->SkeletalBodySetups.IsEmpty())
	{
		UE_LOG(LogLostWithin, Error, TEXT("%s has no usable ragdoll physics asset."), Target.Label);
		return false;
	}

	Blueprint->Modify();
	int32 ComponentCount = 0;
	TInlineComponentArray<UMonsterAIComponent*> NativeComponents(Character);
	for (const UMonsterAIComponent* Component : NativeComponents)
	{
		ComponentCount += Component->CreationMethod == EComponentCreationMethod::Native ? 1 : 0;
	}
	for (UClass* Class = Blueprint->GeneratedClass; Class; Class = Class->GetSuperClass())
	{
		const UBlueprint* OwnerBlueprint = Cast<UBlueprint>(Class->ClassGeneratedBy);
		if (OwnerBlueprint && OwnerBlueprint->SimpleConstructionScript)
		{
			for (const USCS_Node* Node : OwnerBlueprint->SimpleConstructionScript->GetAllNodes())
			{
				ComponentCount += Node->ComponentClass && Node->ComponentClass->IsChildOf(UMonsterAIComponent::StaticClass()) ? 1 : 0;
			}
		}
	}
	if (ComponentCount > 1)
	{
		UE_LOG(LogLostWithin, Error, TEXT("%s has %d cat-reaction components. Keep one before running setup; no components were deleted."), Target.Label, ComponentCount);
		return false;
	}
	if (ComponentCount == 0)
	{
		USCS_Node* Node = Blueprint->SimpleConstructionScript->CreateNode(UMonsterAIComponent::StaticClass(), TEXT("CatReactionAI"));
		Blueprint->SimpleConstructionScript->AddNode(Node);
	}
	for (UMonsterAIComponent* Component : NativeComponents)
	{
		ResetReactionTemplate(Component);
		ConfigureReactionTemplate(Component, Target, Ragdoll);
	}
	for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
	{
		if (UMonsterAIComponent* Component = Cast<UMonsterAIComponent>(Node->ComponentTemplate))
		{
			ResetReactionTemplate(Component);
			ConfigureReactionTemplate(Component, Target, Ragdoll);
		}
	}
	if (!LostWithinMonsterSetup::RepairRoamQueries(Blueprint))
	{
		return false;
	}

	TArray<UEdGraph*> Graphs;
	Blueprint->GetAllGraphs(Graphs);
	for (UEdGraph* Graph : Graphs)
	{
		TArray<UEdGraphNode*> MovementNodes;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (IsMovementNode(Node))
			{
				MovementNodes.Add(Node);
			}
		}
		for (UEdGraphNode* Node : MovementNodes)
		{
			if (!GuardMovementNode(Graph, Node))
			{
				return false;
			}
		}
	}
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	FCompilerResultsLog CompileResults;
	FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::None, &CompileResults);
	if (CompileResults.NumErrors > 0)
	{
		UE_LOG(LogLostWithin, Error, TEXT("%s cat reaction compile failed with %d errors; assets have not been saved."), Target.Label, CompileResults.NumErrors);
		return false;
	}
	Character = CastChecked<ACharacter>(Blueprint->GeneratedClass->GetDefaultObject());
	if (!LostWithinMonsterSetup::RepairMovementDefaults(Blueprint))
	{
		return false;
	}
	if (Target.RagdollPackage)
	{
		Character->GetMesh()->Modify();
		Character->GetMesh()->SetPhysicsAsset(Ragdoll);
	}
	Blueprint->MarkPackageDirty();
	if (bNeedsPhysicsSave && !SaveAsset(Ragdoll))
	{
		return false;
	}
	if (!SaveAsset(Blueprint))
	{
		return false;
	}
	FBlueprintEditorUtils::PostEditChangeBlueprintActors(Blueprint, true);
	if (Target.bRepairNavigation && !LostWithinMonsterSetup::BuildAndSaveNavigation(World, Blueprint->GeneratedClass))
	{
		UE_LOG(LogLostWithin, Error, TEXT("%s assets were saved, but level navigation repair failed. Resolve the preceding error and rerun setup."), Target.Label);
		return false;
	}
	UE_LOG(LogLostWithin, Display, TEXT("Saved %s movement guards and cat reaction (panic gait %.1f cm/s); %s has %d bodies and %d joints."),
		Target.Label, Target.PanicGaitSpeed, *Ragdoll->GetPathName(), Ragdoll->SkeletalBodySetups.Num(), Ragdoll->ConstraintSetup.Num());
	return true;
}
}
