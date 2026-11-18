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
	auto CheckGeneratedRagdoll = [this](const TCHAR* Name) -> bool
	{
		USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *FString::Printf(TEXT("/Game/NPC/%s/base_basic_pbr.base_basic_pbr"), Name));
		UPhysicsAsset* Asset = LoadObject<UPhysicsAsset>(nullptr, *FString::Printf(TEXT("/Game/NPC/%s/PA_%sCatRagdoll.PA_%sCatRagdoll"), Name, Name, Name));
		UClass* MonsterClass = LoadClass<ACharacter>(nullptr, *FString::Printf(TEXT("/Game/NPC/%s/BP_%s.BP_%s_C"), Name, Name, Name));
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
	const TCHAR* const Monsters[] = { TEXT("Monster"), TEXT("Doll"), TEXT("BoneDog") };
	bool bAllPassed = true;
	for (const TCHAR* Name : Monsters)
	{
		bAllPassed &= CheckGeneratedRagdoll(Name);
	}
	return bAllPassed;
}

#endif
