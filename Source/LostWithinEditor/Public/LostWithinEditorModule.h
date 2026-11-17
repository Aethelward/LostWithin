// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

class IConsoleObject;

class FLostWithinEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	FName RegisteredVisualizerClassName = NAME_None;
	IConsoleObject* MonsterCatSetupCommand = nullptr;
};
