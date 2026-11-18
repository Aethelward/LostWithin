// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "SetupMonsterCatReactionCommandlet.generated.h"

/** Wires the cat reaction into every roaming monster Blueprint and repairs their movement/navigation through Unreal's asset APIs. */
UCLASS()
class LOSTWITHINEDITOR_API USetupMonsterCatReactionCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	USetupMonsterCatReactionCommandlet();
	virtual int32 Main(const FString& Params) override;

	/** Configures Lunger, BP_BoneDog, BP_Doll and BP_Ghoul; shared by the commandlet and the LostWithin.SetupMonsterCatReaction console command. */
	static bool ConfigureMonster();
};
