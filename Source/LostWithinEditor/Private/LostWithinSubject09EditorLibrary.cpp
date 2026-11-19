#include "LostWithinSubject09EditorLibrary.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "Engine/Level.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerStart.h"
#include "HAL/FileManager.h"
#include "Interaction/ItemSpawnPoint.h"
#include "Interaction/PickupItemActor.h"
#include "LostWithin.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "PhysicsAssetUtils.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "RetargetEditor/IKRetargeterController.h"
#include "Retargeter/IKRetargeter.h"
#include "Rig/IKRigDefinition.h"
#include "RigEditor/IKRigController.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace
{
	template<typename T> T* Subject09Asset(const FString& PackageName, bool& bCreated)
	{
		const FString Name = FPackageName::GetLongPackageAssetName(PackageName);
		if (T* Existing = LoadObject<T>(nullptr, *(PackageName + TEXT(".") + Name))) { bCreated = false; return Existing; }
		UPackage* Package = CreatePackage(*PackageName);
		T* Asset = NewObject<T>(Package, *Name, RF_Public | RF_Standalone | RF_Transactional);
		FAssetRegistryModule::AssetCreated(Asset);
		Asset->MarkPackageDirty(); bCreated = true;
		return Asset;
	}

	bool Subject09Save(UPackage* Package, UObject* Asset = nullptr)
	{
		if (!Package) { return false; }
		const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true))
		{ UE_LOG(LogLostWithin, Error, TEXT("Subject09: cannot create asset package directory.")); return false; }
		FSavePackageArgs Args; Args.TopLevelFlags = RF_Public | RF_Standalone; Args.SaveFlags = SAVE_NoError;
		const bool bSaved = UPackage::SavePackage(Package, Asset, *Filename, Args);
		if (!bSaved) { UE_LOG(LogLostWithin, Error, TEXT("Subject09: failed to save %s"), *Package->GetName()); }
		return bSaved;
	}

	bool Subject09ContactProfile(const TCHAR* Name, USkeletalMesh* Mesh, USkeletalMesh* HeadMesh = nullptr)
	{
		if (!Mesh) { UE_LOG(LogLostWithin, Error, TEXT("Subject09: missing mesh for %s"), Name); return false; }
		bool bCreated = false;
		UPhysicsAsset* Profile = Subject09Asset<UPhysicsAsset>(FString::Printf(TEXT("/Game/Gameplay/Subject09/Contact/PA_BodyContact_%s"), Name), bCreated);
		if (!bCreated && Profile->SkeletalBodySetups.Num() >= 8) { return true; }
		// Imported Hips bones can be scaled 100x. The factory's size cutoff is in bone-local units.
		const FReferenceSkeleton& RefSkeleton = Mesh->GetRefSkeleton();
		const TArray<FTransform>& RefPose = RefSkeleton.GetRefBonePose();
		TArray<FTransform> ComponentPose; ComponentPose.SetNum(RefPose.Num());
		float LargestBoneScale = 1.0f;
		for (int32 Index = 0; Index < RefPose.Num(); ++Index)
		{
			const int32 Parent = RefSkeleton.GetParentIndex(Index);
			ComponentPose[Index] = Parent == INDEX_NONE ? RefPose[Index] : RefPose[Index] * ComponentPose[Parent];
			LargestBoneScale = FMath::Max(LargestBoneScale, static_cast<float>(ComponentPose[Index].GetScale3D().GetAbsMax()));
		}
		Profile->SkeletalBodySetups.Empty();
		Profile->UpdateBodySetupIndexMap();
		Profile->ConstraintSetup.Empty();
		Profile->CollisionDisableTable.Empty();
		FPhysAssetCreateParams Params;
		Params.MinBoneSize = 6.0f / LargestBoneScale;
		Params.GeomType = EFG_Sphyl;
		Params.bCreateConstraints = false;
		Params.bWalkPastSmall = false;
		Params.bBodyForAll = false;
		FText Error;
		// False is essential: never replace a mesh's existing ragdoll physics asset.
		if (!FPhysicsAssetUtils::CreateFromSkeletalMesh(Profile, Mesh, Params, Error, false, false))
		{
			UE_LOG(LogLostWithin, Error, TEXT("Subject09: contact fitting failed for %s: %s"), Name, *Error.ToString()); return false;
		}
		for (USkeletalBodySetup* Body : Profile->SkeletalBodySetups)
		{
			if (!Body) { continue; }
			// Inscribe the contact proxy inside the fitted silhouette, avoiding broad padding around thin limbs.
			for (FKSphylElem& Shape : Body->AggGeom.SphylElems) { Shape.Radius *= 0.85f; Shape.Length *= 0.9f; }
			for (FKSphereElem& Shape : Body->AggGeom.SphereElems) { Shape.Radius *= 0.85f; }
			Body->InvalidatePhysicsData();
		}
		// The separate MetaHuman face may contribute no vertices to the body mesh's head bone.
		if (HeadMesh && Profile->FindBodyIndex(TEXT("head")) == INDEX_NONE)
		{
			const FReferenceSkeleton& Skeleton = Mesh->GetRefSkeleton();
			const int32 HeadIndex = Skeleton.FindBoneIndex(TEXT("head"));
			if (HeadIndex != INDEX_NONE)
			{
				FTransform BoneToMesh = Skeleton.GetRefBonePose()[HeadIndex];
				for (int32 Parent = Skeleton.GetParentIndex(HeadIndex); Parent != INDEX_NONE; Parent = Skeleton.GetParentIndex(Parent))
				{ BoneToMesh *= Skeleton.GetRefBonePose()[Parent]; }
				USkeletalBodySetup* Head = NewObject<USkeletalBodySetup>(Profile);
				Head->BoneName = TEXT("head");
				FKSphereElem Sphere;
				Sphere.Center = BoneToMesh.InverseTransformPosition(HeadMesh->GetImportedBounds().Origin);
				Sphere.Radius = HeadMesh->GetImportedBounds().BoxExtent.GetMin() * 0.65f / FMath::Max(0.001f, BoneToMesh.GetScale3D().GetMin());
				Head->AggGeom.SphereElems.Add(Sphere);
				Profile->SkeletalBodySetups.Add(Head);
				Profile->UpdateBodySetupIndexMap();
			}
		}
		Profile->MarkPackageDirty();
		UE_LOG(LogLostWithin, Display, TEXT("Subject09: %s fitted to %d contact bodies"), Name, Profile->SkeletalBodySetups.Num());
		return Subject09Save(Profile->GetPackage(), Profile);
	}

	bool Subject09Retargeter(USkeletalMesh* Source, USkeletalMesh* Target)
	{
		if (!Source || !Target) { return false; }
		UIKRigDefinition* Rigs[2] = {};
		USkeletalMesh* Meshes[2] = {Source, Target};
		const TCHAR* Names[2] = {TEXT("IK_Manny"), TEXT("IK_Evann")};
		for (int32 Index = 0; Index < 2; ++Index)
		{
			bool bCreated = false;
			Rigs[Index] = Subject09Asset<UIKRigDefinition>(FString(TEXT("/Game/Gameplay/Subject09/")) + Names[Index], bCreated);
			if (bCreated)
			{
				UIKRigController* Controller = UIKRigController::GetController(Rigs[Index]);
				if (!Controller->SetSkeletalMesh(Meshes[Index]) || !Controller->ApplyAutoGeneratedRetargetDefinition())
				{ UE_LOG(LogLostWithin, Error, TEXT("Subject09: automatic retarget definition failed for %s"), Names[Index]); return false; }
				if (!Subject09Save(Rigs[Index]->GetPackage(), Rigs[Index])) { return false; }
			}
		}
		bool bCreated = false;
		UIKRetargeter* Retargeter = Subject09Asset<UIKRetargeter>(TEXT("/Game/Gameplay/Subject09/RTG_Manny_Evann"), bCreated);
		if (bCreated)
		{
			UIKRetargeterController* Controller = UIKRetargeterController::GetController(Retargeter);
			Controller->SetIKRig(ERetargetSourceOrTarget::Source, Rigs[0]);
			Controller->SetIKRig(ERetargetSourceOrTarget::Target, Rigs[1]);
			Controller->SetPreviewMesh(ERetargetSourceOrTarget::Source, Source);
			Controller->SetPreviewMesh(ERetargetSourceOrTarget::Target, Target);
			Controller->AddDefaultOps();
			Controller->AutoMapChains(EAutoMapChainType::Exact, true);
			return Subject09Save(Retargeter->GetPackage(), Retargeter);
		}
		return true;
	}

	bool Subject09StartingTable(UWorld* World)
	{
		APlayerStart* Start = nullptr;
		for (TActorIterator<APlayerStart> It(World); It; ++It) { Start = *It; break; }
		if (!Start) { UE_LOG(LogLostWithin, Error, TEXT("Subject09: no PlayerStart.")); return false; }
		AStaticMeshActor* Table = nullptr;
		double Best = TNumericLimits<double>::Max();
		for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
		{
			const UStaticMesh* Mesh = It->GetStaticMeshComponent()->GetStaticMesh();
			if (Mesh && Mesh->GetName().Contains(TEXT("table"), ESearchCase::IgnoreCase))
			{
				const double Distance = FVector::DistSquared(It->GetActorLocation(), Start->GetActorLocation());
				if (Distance < Best) { Best = Distance; Table = *It; }
			}
		}
		if (!Table) { UE_LOG(LogLostWithin, Error, TEXT("Subject09: starting table mesh not found.")); return false; }
		FVector Center, Extent; Table->GetActorBounds(false, Center, Extent);
		AItemSpawnPoint* Orientation = nullptr;
		for (TActorIterator<AItemSpawnPoint> It(World); It; ++It) { if (It->Kind == ELostWithinSpawnPointKind::Orientation) { Orientation = *It; break; } }
		if (!Orientation)
		{
			FActorSpawnParameters Spawn; Spawn.Name = TEXT("Subject09OrientationPoint"); Spawn.bCreateActorPackage = true;
			Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Orientation = World->SpawnActor<AItemSpawnPoint>(AItemSpawnPoint::StaticClass(), FVector(Center.X + 18.0f, Center.Y, Center.Z + Extent.Z + 4.0f), FRotator::ZeroRotator, Spawn);
			if (!Orientation) { return false; }
			Orientation->SetActorLabel(TEXT("Subject09_OrientationNote"));
			Orientation->Kind = ELostWithinSpawnPointKind::Orientation;
			Orientation->SetIsSpatiallyLoaded(false);
			Orientation->SetFolderPath(TEXT("Gameplay/RunSetup/SpawnPoints"));
			Orientation->Tags.AddUnique(TEXT("LostWithin.Subject09"));
			Orientation->MarkPackageDirty();
			UPackage* Package = Orientation->GetExternalPackage() ? Orientation->GetExternalPackage() : Orientation->GetPackage();
			if (!Subject09Save(Package)) { return false; }
		}
		APickupItemActor* Flashlight = nullptr;
		Best = TNumericLimits<double>::Max();
		for (TActorIterator<APickupItemActor> It(World); It; ++It)
		{
			if (It->IsFlashlightItem())
			{
				const double Distance = FVector::DistSquared(It->GetActorLocation(), Start->GetActorLocation());
				if (Distance < Best) { Best = Distance; Flashlight = *It; }
			}
		}
		if (!Flashlight) { UE_LOG(LogLostWithin, Error, TEXT("Subject09: existing flashlight pickup not found.")); return false; }
		if (!Flashlight->Tags.Contains(TEXT("LostWithin.Subject09.TableV2")))
		{
			Flashlight->Modify();
			Flashlight->SetActorLocation(FVector(Center.X - 18.0f, Center.Y, Center.Z + Extent.Z + 10.0f));
			UStaticMeshComponent* ItemMesh = Cast<UStaticMeshComponent>(Flashlight->GetRootComponent());
			if (!ItemMesh) { return false; }
			ItemMesh->UpdateBounds();
			const FBox ItemBounds = ItemMesh->Bounds.GetBox();
			Flashlight->AddActorWorldOffset(FVector(0.0f, 0.0f, Center.Z + Extent.Z + 2.0f - ItemBounds.Min.Z));
			Flashlight->Tags.AddUnique(TEXT("LostWithin.Subject09.Table"));
			Flashlight->Tags.AddUnique(TEXT("LostWithin.Subject09.TableV2"));
			Flashlight->Tags.AddUnique(TEXT("mcp.placement.ok"));
			Flashlight->MarkPackageDirty();
			UPackage* Package = Flashlight->GetExternalPackage() ? Flashlight->GetExternalPackage() : Flashlight->GetPackage();
			if (!Subject09Save(Package)) { return false; }
		}
		return true;
	}
}

bool ULostWithinSubject09EditorLibrary::SetupSubject09()
{
	if (!GEditor || GEditor->PlayWorld)
	{ UE_LOG(LogLostWithin, Error, TEXT("Subject09 setup requires edit mode; it never stops a running Play session.")); return false; }
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World || World->GetOutermost()->GetName() != TEXT("/Game/FirstPerson/Lvl_FirstPerson"))
	{ UE_LOG(LogLostWithin, Error, TEXT("Subject09 setup requires Lvl_FirstPerson.")); return false; }
	USkeletalMesh* Manny = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"));
	USkeletalMesh* Evann = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/MetaHumans/evann/Body/SKM_evann_BodyMesh.SKM_evann_BodyMesh"));
	USkeletalMesh* Face = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/MetaHumans/evann/Face/SKM_evann_FaceMesh.SKM_evann_FaceMesh"));
	bool bSuccess = Subject09Retargeter(Manny, Evann);
	bSuccess &= Subject09ContactProfile(TEXT("Subject09"), Evann, Face);
	const TCHAR* Creatures[] = {TEXT("Lunger"), TEXT("BoneDog"), TEXT("Doll"), TEXT("Ghoul")};
	for (const TCHAR* Creature : Creatures)
	{
		USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *FString::Printf(TEXT("/Game/NPC/%s/base_basic_pbr.base_basic_pbr"), Creature));
		bSuccess &= Subject09ContactProfile(Creature, Mesh);
	}
	bSuccess &= Subject09StartingTable(World);
	UE_LOG(LogLostWithin, Display, TEXT("Subject09 setup: %s"), bSuccess ? TEXT("complete") : TEXT("FAILED - inspect errors before continuing"));
	return bSuccess;
}
