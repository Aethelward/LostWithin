// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/GameInstance.h"
#include "LostWithinGameInstance.generated.h"

UCLASS()
class LOSTWITHIN_API ULostWithinGameInstance : public UGameInstance
{
	GENERATED_BODY()

#if WITH_EDITOR
public:
	virtual FGameInstancePIEResult InitializeForPlayInEditor(int32 PIEInstanceIndex, const FGameInstancePIEParameters& Params) override;
#endif
};
