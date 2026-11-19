// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Character.h"
#include "Misc/AutomationTest.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMonsterRagdollAssetGeometryTest,
	"LostWithin.AI.MonsterCatReaction.GeneratedAssetGeometry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FMonsterRagdollAssetGeometryTest::RunTest(const FString& Parameters)
{
	/** Where a monster's production assets live; Lunger's folder and Blueprint no longer share its ragdoll's name. */
	struct FGeneratedRagdollMonster
	{
		const TCHAR* Name;
		const TCHAR* Folder;
		const TCHAR* RagdollName;
		const TCHAR* BlueprintName;
	};

	auto CheckGeneratedRagdoll = [this](const FGeneratedRagdollMonster& Monster) -> bool
	{
		const TCHAR* Name = Monster.Name;
		USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr,
			*FString::Printf(TEXT("/Game/NPC/%s/base_basic_pbr.base_basic_pbr"), Monster.Folder));
		UPhysicsAsset* Asset = LoadObject<UPhysicsAsset>(nullptr,
			*FString::Printf(TEXT("/Game/NPC/%s/%s.%s"), Monster.Folder, Monster.RagdollName, Monster.RagdollName));
		UClass* MonsterClass = LoadClass<ACharacter>(nullptr,
			*FString::Printf(TEXT("/Game/NPC/%s/%s.%s_C"), Monster.Folder, Monster.BlueprintName, Monster.BlueprintName));
		if (!TestNotNull(*FString::Printf(TEXT("%s production skeletal mesh exists"), Name), Mesh)
			|| !TestNotNull(*FString::Printf(TEXT("%s: run setup to create the articulated physics asset"), Name), Asset)
			|| !TestNotNull(*FString::Printf(TEXT("%s production class exists"), Name), MonsterClass))
		{
			return false;
		}
		AddInfo(FString::Printf(TEXT("Checking %s"), *Asset->GetPathName()));
		TestTrue(TEXT("The ragdoll is articulated"), Asset->SkeletalBodySetups.Num() >= 6);
		TestTrue(TEXT("Every body has a connecting joint"), Asset->ConstraintSetup.Num() >= Asset->SkeletalBodySetups.Num() - 1);
		TestTrue(TEXT("The hip recovery body exists"), Asset->FindBodyIndex(TEXT("Hips")) != INDEX_NONE);
		TestNotEqual(TEXT("The mesh's original physics asset is preserved"), Mesh->GetPhysicsAsset(), Asset);
		TestEqual(TEXT("The monster component uses the articulated override"),
			MonsterClass->GetDefaultObject<ACharacter>()->GetMesh()->GetPhysicsAsset(), Asset);

		const FReferenceSkeleton& Skeleton = Mesh->GetRefSkeleton();
		TArray<FTransform> Bones;
		Bones.SetNum(Skeleton.GetNum());
		for (int32 Index = 0; Index < Skeleton.GetNum(); ++Index)
		{
			const int32 Parent = Skeleton.GetParentIndex(Index);
			Bones[Index] = Parent == INDEX_NONE ? Skeleton.GetRefBonePose()[Index]
				: Skeleton.GetRefBonePose()[Index] * Bones[Parent];
		}
		FBox PhysicsBounds(ForceInit);
		for (const USkeletalBodySetup* Body : Asset->SkeletalBodySetups)
		{
			if (!TestNotNull(TEXT("Physics body exists"), Body))
			{
				return false;
			}
			const int32 BoneIndex = Skeleton.FindBoneIndex(Body->BoneName);
			if (!TestTrue(TEXT("Physics body names a real mesh bone"), BoneIndex != INDEX_NONE))
			{
				return false;
			}
			TestTrue(TEXT("Fast throw bodies use continuous collision detection"), Body->DefaultInstance.bUseCCD);
			PhysicsBounds += Body->AggGeom.CalcAABB(Bones[BoneIndex]);
		}
		if (!TestTrue(TEXT("Physics bounds are valid"), PhysicsBounds.IsValid != 0))
		{
			return false;
		}
		const double VisibleSize = Mesh->GetBounds().BoxExtent.GetAbsMax() * 2.0;
		const double PhysicsSize = PhysicsBounds.GetSize().GetAbsMax();
		TestTrue(FString::Printf(TEXT("Colliders fit the visible mesh after reference bone scale (physics %.2f cm, mesh %.2f cm)"),
			PhysicsSize, VisibleSize), PhysicsSize <= VisibleSize * 1.5);
		return true;
	};

	// Every monster whose setup generates a ragdoll (the Ghoul keeps its imported, already articulated asset).
	const FGeneratedRagdollMonster Monsters[] = {
		{ TEXT("Lunger"), TEXT("Lunger"), TEXT("PA_MonsterCatRagdoll"), TEXT("Lunger") },
		{ TEXT("Doll"), TEXT("Doll"), TEXT("PA_DollCatRagdoll"), TEXT("BP_Doll") },
		{ TEXT("BoneDog"), TEXT("BoneDog"), TEXT("PA_BoneDogCatRagdoll"), TEXT("BP_BoneDog") },
	};
	bool bAllPassed = true;
	for (const FGeneratedRagdollMonster& Monster : Monsters)
	{
		bAllPassed &= CheckGeneratedRagdoll(Monster);
	}
	return bAllPassed;
}

#endif
