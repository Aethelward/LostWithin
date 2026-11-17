// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UBlueprint;
class UWorld;

namespace LostWithinMonsterSetup
{
	/** Interactive setup never switches away from an unrelated open map. */
	UWorld* GetSetupWorld();
	bool RepairMovementDefaults(UBlueprint* Blueprint);
	bool RepairRoamQueries(UBlueprint* Blueprint);
	bool BuildAndSaveNavigation(UWorld* World, UClass* MonsterClass);
}
