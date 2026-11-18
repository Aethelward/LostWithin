// Copyright Epic Games, Inc. All Rights Reserved.

#include "LostWithinEditorModule.h"
#include "Components/ProximityFloatSpinComponent.h"
#include "Editor/UnrealEdEngine.h"
#include "HAL/IConsoleManager.h"
#include "Modules/ModuleManager.h"
#include "ProximityFloatSpinComponentVisualizer.h"
#include "SetupMonsterCatReactionCommandlet.h"
#include "UnrealEdGlobals.h"

IMPLEMENT_MODULE(FLostWithinEditorModule, LostWithinEditor)

void FLostWithinEditorModule::StartupModule()
{
	MonsterCatSetupCommand = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("LostWithin.SetupMonsterCatReaction"),
		TEXT("Configure the cat reaction on BP_Monster, BP_BoneDog, BP_Doll and BP_Ghoul (panic-flee any active float-spin cat, 3 m capsule-radius ragdoll throws, guaranteed recovery), repair monster movement and build maze navigation. Open Lvl_FirstPerson and stop PIE first. Saves the target map, its dirty actor packages and monster assets."),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			USetupMonsterCatReactionCommandlet::ConfigureMonster();
		}), ECVF_Default);

	if (GUnrealEd)
	{
		const FName ComponentClassName = UProximityFloatSpinComponent::StaticClass()->GetFName();
		const TSharedPtr<FProximityFloatSpinComponentVisualizer> Visualizer = MakeShared<FProximityFloatSpinComponentVisualizer>();
		GUnrealEd->RegisterComponentVisualizer(ComponentClassName, Visualizer);
		Visualizer->OnRegister();
		RegisteredVisualizerClassName = ComponentClassName;
	}
}

void FLostWithinEditorModule::ShutdownModule()
{
	if (MonsterCatSetupCommand)
	{
		IConsoleManager::Get().UnregisterConsoleObject(MonsterCatSetupCommand);
		MonsterCatSetupCommand = nullptr;
	}
	if (GUnrealEd && !RegisteredVisualizerClassName.IsNone())
	{
		GUnrealEd->UnregisterComponentVisualizer(RegisteredVisualizerClassName);
	}
	RegisteredVisualizerClassName = NAME_None;
}
