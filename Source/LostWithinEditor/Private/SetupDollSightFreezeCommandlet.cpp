// Copyright Epic Games, Inc. All Rights Reserved.

#include "SetupDollSightFreezeCommandlet.h"

#include "AI/MonsterSightFreezeComponent.h"
#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "GameFramework/Character.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "LostWithin.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace DollSightFreezeSetupHelpers
{
	constexpr const TCHAR* DollBlueprintPath = TEXT("/Game/NPC/Doll/BP_Doll.BP_Doll");

	bool SaveBlueprintAsset(UObject* Asset)
	{
		if (!Asset)
		{
			return false;
		}

		const FString Filename = FPackageName::LongPackageNameToFilename(
			Asset->GetOutermost()->GetName(),
			FPackageName::GetAssetPackageExtension());

		if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true))
		{
			UE_LOG(LogLostWithin, Error, TEXT("Cannot create content folder for %s."), *Asset->GetPathName());
			return false;
		}

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		if (!UPackage::SavePackage(Asset->GetOutermost(), Asset, *Filename, SaveArgs))
		{
			UE_LOG(LogLostWithin, Error, TEXT("Could not save %s."), *Asset->GetPathName());
			return false;
		}

		return true;
	}
}

using namespace DollSightFreezeSetupHelpers;

USetupDollSightFreezeCommandlet::USetupDollSightFreezeCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 USetupDollSightFreezeCommandlet::Main(const FString& Params)
{
	return ConfigureDoll() ? 0 : 1;
}

bool USetupDollSightFreezeCommandlet::ConfigureDoll()
{
	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, DollBlueprintPath);
	if (!Blueprint)
	{
		UE_LOG(LogLostWithin, Error, TEXT("Could not load Doll Blueprint at %s."), DollBlueprintPath);
		return false;
	}

	ACharacter* Character = Blueprint->GeneratedClass ? Cast<ACharacter>(Blueprint->GeneratedClass->GetDefaultObject()) : nullptr;
	if (!Character || !Blueprint->SimpleConstructionScript)
	{
		UE_LOG(LogLostWithin, Error, TEXT("BP_Doll must be a Character Blueprint with a SimpleConstructionScript."));
		return false;
	}

	Blueprint->Modify();

	// Check if UMonsterSightFreezeComponent is already present
	int32 ComponentCount = 0;
	TInlineComponentArray<UMonsterSightFreezeComponent*> NativeComponents(Character);
	ComponentCount += NativeComponents.Num();

	for (const USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
	{
		if (Node->ComponentClass && Node->ComponentClass->IsChildOf(UMonsterSightFreezeComponent::StaticClass()))
		{
			ComponentCount++;
		}
	}

	if (ComponentCount == 0)
	{
		USCS_Node* Node = Blueprint->SimpleConstructionScript->CreateNode(
			UMonsterSightFreezeComponent::StaticClass(),
			TEXT("SightFreezeAI"));

		if (Node)
		{
			if (UMonsterSightFreezeComponent* Template = Cast<UMonsterSightFreezeComponent>(Node->ComponentTemplate))
			{
				Template->bAutoStalkWhenUnobserved = true;
				Template->StalkWalkSpeed = 220.0f;
				Template->bRequireIlluminationAtDistance = true;
				Template->UnilluminatedMaxDistance = 800.0f;
				Template->IlluminatedMaxDistance = 3500.0f;
				Template->bIgnoreSightWhenInMenus = true;
			}

			Blueprint->SimpleConstructionScript->AddNode(Node);
			UE_LOG(LogLostWithin, Display, TEXT("Added UMonsterSightFreezeComponent to BP_Doll."));
		}
	}
	else
	{
		UE_LOG(LogLostWithin, Display, TEXT("BP_Doll already has UMonsterSightFreezeComponent attached."));
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

	if (!SaveBlueprintAsset(Blueprint))
	{
		UE_LOG(LogLostWithin, Error, TEXT("Failed to save BP_Doll asset package."));
		return false;
	}

	UE_LOG(LogLostWithin, Display, TEXT("Successfully configured and saved BP_Doll with sight-freeze stalker mechanics!"));
	return true;
}

// Auto-register console command in editor
static FAutoConsoleCommand GSetupDollSightFreezeCmd(
	TEXT("LostWithin.SetupDollSightFreeze"),
	TEXT("Adds UMonsterSightFreezeComponent to BP_Doll and configures weeping stalker mechanics."),
	FConsoleCommandDelegate::CreateStatic([]()
	{
		USetupDollSightFreezeCommandlet::ConfigureDoll();
	})
);
