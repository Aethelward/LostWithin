// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Blueprint/UserWidget.h"
#include "GameFramework/GameModeBase.h"
#include "LostWithinMenu.generated.h"

class USoundBase;

/** Restores the player's volume choices on entry to a game world. */
void ApplyLostWithinAudioOptions(const UObject* WorldContextObject);

/** Opens the authored front-end widget on the menu map. */
UCLASS()
class LOSTWITHIN_API ALostWithinMenuGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ALostWithinMenuGameMode();
	virtual void StartPlay() override;

private:
	UPROPERTY()
	TSubclassOf<UUserWidget> MainMenuClass;

	UPROPERTY()
	TObjectPtr<USoundBase> MenuMusic;
};

/** Connects the existing OptionsMenu sliders to the game's sound mix. */
UCLASS()
class LOSTWITHIN_API ULostWithinOptionsMenu : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

private:
	UFUNCTION()
	void OnMusicVolumeChanged(float Value);

	UFUNCTION()
	void OnEffectsVolumeChanged(float Value);

	UFUNCTION()
	void PreviewEffectsVolume();

	bool bAudioSettingsChanged = false;
};
