// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "SetupDollSightFreezeCommandlet.generated.h"

/** Commandlet and utility to configure MonsterSightFreezeComponent on BP_Doll */
UCLASS()
class LOSTWITHINEDITOR_API USetupDollSightFreezeCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	USetupDollSightFreezeCommandlet();
	virtual int32 Main(const FString& Params) override;

	/** Configures BP_Doll with UMonsterSightFreezeComponent; can be called from editor console or commandlet */
	static bool ConfigureDoll();
};
