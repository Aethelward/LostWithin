// Copyright Epic Games, Inc. All Rights Reserved.

#include "LostWithinGameInstance.h"

#if WITH_EDITOR
#include "Editor/EditorEngine.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameMapsSettings.h"
#include "Misc/PackageName.h"
#include "Settings/LevelEditorPlaySettings.h"

FGameInstancePIEResult ULostWithinGameInstance::InitializeForPlayInEditor(
	int32 PIEInstanceIndex, const FGameInstancePIEParameters& Params)
{
	if (Params.bSimulateInEditor || Params.bStartInSpectatorMode
		|| Params.NetMode != EPlayNetMode::PIE_Standalone || !Params.OverrideMapURL.IsEmpty())
	{
		return Super::InitializeForPlayInEditor(PIEInstanceIndex, Params);
	}

	UEditorEngine* EditorEngine = CastChecked<UEditorEngine>(GetEngine());
	const UWorld* EditorWorld = EditorEngine->GetEditorWorldContext().World();
	const FName FirstPersonMap(TEXT("/Game/FirstPerson/Lvl_FirstPerson"));
	if (!EditorWorld || EditorWorld->GetOutermost()->GetFName() != FirstPersonMap
		|| !EditorEngine->UserEditedPlayWorldURL.IsEmpty())
	{
		return Super::InitializeForPlayInEditor(PIEInstanceIndex, Params);
	}

	const TOptional<FPlayInEditorSessionInfo> Session = EditorEngine->GetPlayInEditorSessionInfo();
	if (Session.IsSet() && (Session->OriginalRequestParams.HasPlayWorldPlacement()
		|| Session->OriginalRequestParams.GameModeOverride))
	{
		return Super::InitializeForPlayInEditor(PIEInstanceIndex, Params);
	}

	const FString StartupMap = UGameMapsSettings::GetGameDefaultMap();
	if (StartupMap.IsEmpty() || FName(*StartupMap) == FirstPersonMap)
	{
		return Super::InitializeForPlayInEditor(PIEInstanceIndex, Params);
	}

	FGameInstancePIEParameters StartupParams = Params;
	StartupParams.OverrideMapURL = StartupMap + TEXT(".") + FPackageName::GetShortName(StartupMap);
	return Super::InitializeForPlayInEditor(PIEInstanceIndex, StartupParams);
}
#endif
