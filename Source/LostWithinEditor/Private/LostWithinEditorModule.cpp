// Copyright Epic Games, Inc. All Rights Reserved.

#include "LostWithinEditorModule.h"
#include "Components/ProximityFloatSpinComponent.h"
#include "Editor/UnrealEdEngine.h"
#include "Modules/ModuleManager.h"
#include "ProximityFloatSpinComponentVisualizer.h"
#include "UnrealEdGlobals.h"

IMPLEMENT_MODULE(FLostWithinEditorModule, LostWithinEditor)

void FLostWithinEditorModule::StartupModule()
{
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
	if (GUnrealEd && !RegisteredVisualizerClassName.IsNone())
	{
		GUnrealEd->UnregisterComponentVisualizer(RegisteredVisualizerClassName);
	}
	RegisteredVisualizerClassName = NAME_None;
}

