// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "SetupMonsterCatReactionCommandlet.generated.h"

/** Repairs BP_Monster movement/navigation and cat reaction wiring through Unreal's asset APIs. */
UCLASS()
class LOSTWITHINEDITOR_API USetupMonsterCatReactionCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	USetupMonsterCatReactionCommandlet();
	virtual int32 Main(const FString& Params) override;

	/** Shared by the commandlet and the LostWithin.SetupMonsterCatReaction editor console command. */
	static bool ConfigureMonster();
};
