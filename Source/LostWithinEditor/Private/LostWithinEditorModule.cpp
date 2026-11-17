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
		TEXT("Repair BP_Monster movement, build maze navigation and configure the cat ragdoll reaction. Open Lvl_FirstPerson and stop PIE first. Saves the target map, its dirty actor packages and monster assets."),
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
