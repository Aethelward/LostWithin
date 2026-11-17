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
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace
{
	constexpr const TCHAR* MonsterBlueprintPath = TEXT("/Game/NPC/Monster/BP_Monster.BP_Monster");
	constexpr const TCHAR* RagdollPackageName = TEXT("/Game/NPC/Monster/PA_MonsterCatRagdoll");
	constexpr const TCHAR* RagdollObjectPath = TEXT("/Game/NPC/Monster/PA_MonsterCatRagdoll.PA_MonsterCatRagdoll");

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

	// Build capsules/spheres along the actual reference bones. This also handles this
	// mesh's short bones that the default minimum-size auto-generation would discard.
	UPhysicsAsset* GetOrCreateRagdollAsset(USkeletalMesh* Mesh, bool& bOutNeedsSave)
	{
		bOutNeedsSave = false;
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
			return Asset;
		}

		const FReferenceSkeleton& Skeleton = Mesh->GetRefSkeleton();
		if (Skeleton.FindBoneIndex(TEXT("Hips")) == INDEX_NONE || Skeleton.FindBoneIndex(TEXT("Head")) == INDEX_NONE)
		{
			UE_LOG(LogLostWithin, Error, TEXT("The monster mesh must have Hips and Head bones."));
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

		UPackage* Package = CreatePackage(RagdollPackageName);
		UPhysicsAsset* Asset = NewObject<UPhysicsAsset>(Package, TEXT("PA_MonsterCatRagdoll"), RF_Public | RF_Standalone | RF_Transactional);
		const float Height = FMath::Max(1.0f, static_cast<float>(Mesh->GetBounds().BoxExtent.Z * 2.0));
		const TSet<FName> PhysicalBones = {
			TEXT("Hips"), TEXT("Spine"), TEXT("Spine1"), TEXT("Spine2"), TEXT("Neck"), TEXT("Head"),
			TEXT("LeftShoulder"), TEXT("RightShoulder"), TEXT("LeftUpLeg"), TEXT("LeftLeg"),
			TEXT("LeftFoot"), TEXT("RightUpLeg"), TEXT("RightLeg"), TEXT("RightFoot")
		};
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

			FVector ChildOffset = FVector::ZeroVector;
			for (int32 Child = Index + 1; Child < Skeleton.GetNum(); ++Child)
			{
				if (Skeleton.GetParentIndex(Child) == Index)
				{
					ChildOffset = BoneTransforms[Index].InverseTransformPosition(BoneTransforms[Child].GetLocation());
					break;
				}
			}
			const float SegmentLength = static_cast<float>(ChildOffset.Size());
			if (Bone != TEXT("Hips") && Bone != TEXT("Head") && SegmentLength > Height * 0.015f)
			{
				FKSphylElem Shape;
				Shape.Center = ChildOffset * 0.5f;
				Shape.Rotation = FRotationMatrix::MakeFromZ(ChildOffset).Rotator();
				Shape.Radius = FMath::Clamp(SegmentLength * 0.22f, Height * 0.015f, Height * 0.055f);
				Shape.Length = FMath::Max(0.1f, SegmentLength - 2.0f * Shape.Radius);
				Body->AggGeom.SphylElems.Add(Shape);
			}
			else
			{
				FKSphereElem Shape;
				Shape.Radius = Height * (Bone == TEXT("Hips") ? 0.075f : Bone == TEXT("Head") ? 0.065f : 0.03f);
				Body->AggGeom.SphereElems.Add(Shape);
			}
			Body->CreatePhysicsMeshes();
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
	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, MonsterBlueprintPath);
	ACharacter* Character = Blueprint && Blueprint->GeneratedClass ? Cast<ACharacter>(Blueprint->GeneratedClass->GetDefaultObject()) : nullptr;
	if (!Character || !Character->GetMesh() || !Character->GetMesh()->GetSkeletalMeshAsset() || !Blueprint->SimpleConstructionScript)
	{
		UE_LOG(LogLostWithin, Error, TEXT("BP_Monster must be a Character Blueprint with a skeletal mesh."));
		return false;
	}
	bool bNeedsPhysicsSave = false;
	UPhysicsAsset* Ragdoll = GetOrCreateRagdollAsset(Character->GetMesh()->GetSkeletalMeshAsset(), bNeedsPhysicsSave);
	if (!Ragdoll)
	{
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
		UE_LOG(LogLostWithin, Error, TEXT("BP_Monster has %d cat-reaction components. Keep one before running setup; no components were deleted."), ComponentCount);
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
	}
	for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
	{
		if (UMonsterAIComponent* Component = Cast<UMonsterAIComponent>(Node->ComponentTemplate))
		{
			ResetReactionTemplate(Component);
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
		UE_LOG(LogLostWithin, Error, TEXT("BP_Monster cat reaction compile failed with %d errors; assets have not been saved."), CompileResults.NumErrors);
		return false;
	}
	Character = CastChecked<ACharacter>(Blueprint->GeneratedClass->GetDefaultObject());
	if (!LostWithinMonsterSetup::RepairMovementDefaults(Blueprint))
	{
		return false;
	}
	Character->GetMesh()->Modify();
	Character->GetMesh()->SetPhysicsAsset(Ragdoll);
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
	if (!LostWithinMonsterSetup::BuildAndSaveNavigation(World, Blueprint->GeneratedClass))
	{
		UE_LOG(LogLostWithin, Error, TEXT("BP_Monster assets were saved, but level navigation repair failed. Resolve the preceding error and rerun setup."));
		return false;
	}
	UE_LOG(LogLostWithin, Display, TEXT("Saved BP_Monster movement and cat reaction: 5 m fear, 1 m throw, 3 s recovery; %s has %d bodies and %d joints."),
		*Ragdoll->GetPathName(), Ragdoll->SkeletalBodySetups.Num(), Ragdoll->ConstraintSetup.Num());
	return true;
}
