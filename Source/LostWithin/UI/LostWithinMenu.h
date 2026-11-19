// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Fonts/SlateFontInfo.h"
#include "GameFramework/GameModeBase.h"
#include "GenericPlatform/GenericWindow.h"
#include "Styling/SlateTypes.h"
#include "LostWithinMenu.generated.h"

class UAudioComponent;
class UGameUserSettings;
class USoundBase;
class UWidget;

/** Restores the player's volume choices on entry to a game world. */
LOSTWITHIN_API void ApplyLostWithinAudioOptions(const UObject* WorldContextObject);

/** Builds a Cinzel Slate font (Bold or Regular) with UE5.6 Default typeface fallback. */
LOSTWITHIN_API FSlateFontInfo MakeLostWithinCinzelFont(bool bBold, int32 Size);

/** Plays the shared UI click sound as a UI sound (works even while paused). */
LOSTWITHIN_API void PlayLostWithinUIClickSound(const UObject* WorldContextObject);

/** Builds the unified dark-obsidian / crimson-hover horror FButtonStyle. */
LOSTWITHIN_API FButtonStyle MakeLostWithinHorrorButtonStyle(bool bPrimaryAccent = false);

/** Snapshot of confirmed display & scalability settings so unconfirmed changes revert completely. */
struct FOptionsDisplaySnapshot
{
	bool bValid = false;
	FIntPoint Resolution = FIntPoint(1280, 720);
	EWindowMode::Type WindowMode = EWindowMode::Windowed;
	int32 OverallQuality = 2;
	int32 ViewDistance = 2;
	int32 AntiAliasing = 2;
	int32 Shadow = 2;
	int32 GlobalIllumination = 2;
	int32 Reflection = 2;
	int32 PostProcess = 2;
	int32 Texture = 2;
	int32 Effects = 2;
	int32 Foliage = 2;
	int32 Shading = 2;
	bool bVSync = false;
};

/** Opens the authored front-end widget on the menu map and manages menu music. */
UCLASS()
class LOSTWITHIN_API ALostWithinMenuGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ALostWithinMenuGameMode();
	virtual void StartPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION(BlueprintCallable, Category="Menu")
	void StopMenuMusic();

	UFUNCTION(BlueprintPure, Category="Menu")
	UAudioComponent* GetMenuMusicComponent() const { return MenuMusicComponent; }

private:
	UPROPERTY()
	TSubclassOf<UUserWidget> MainMenuClass;

	UPROPERTY()
	TObjectPtr<USoundBase> MenuMusic;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> MenuMusicComponent;
};

/** Native base class for the Main Menu widget (/Game/UI/Menu/MainMenu). */
UCLASS()
class LOSTWITHIN_API ULostWithinMainMenu : public UUserWidget
{
	GENERATED_BODY()

public:
	ULostWithinMainMenu(const FObjectInitializer& ObjectInitializer);

	UFUNCTION(BlueprintCallable, Category="Menu")
	void HandlePlayClicked();

	UFUNCTION(BlueprintCallable, Category="Menu")
	void HandleOptionsClicked();

	UFUNCTION(BlueprintCallable, Category="Menu")
	void HandleQuitClicked();

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& MouseEvent) override;
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Key) override;

private:
	template <typename TWidget>
	TWidget* FindRequiredWidget(FName Name);

	TSet<FName> LoggedMissingWidgets;
};

/** Connects the OptionsMenu tabs, screen settings/revert, guide, and audio sliders. */
UCLASS()
class LOSTWITHIN_API ULostWithinOptionsMenu : public UUserWidget
{
	GENERATED_BODY()

public:
	ULostWithinOptionsMenu(const FObjectInitializer& ObjectInitializer);

	UFUNCTION(BlueprintCallable, Category="Options")
	void SetReturnToPause(bool bValue) { bReturnToPause = bValue; }

	UFUNCTION(BlueprintCallable, Category="Options")
	void ShowTab(int32 TabIndex);

	UFUNCTION(BlueprintCallable, Category="Options")
	void ApplyScreenSettings();

	UFUNCTION(BlueprintCallable, Category="Options")
	void RevertDisplayChanges();

	UFUNCTION(BlueprintPure, Category="Options")
	bool IsDisplayConfirmationPending() const { return bAwaitingConfirmation; }

	UFUNCTION(BlueprintPure, Category="Options")
	int32 GetSelectedQuality() const { return SelectedQuality; }

	UFUNCTION(BlueprintPure, Category="Options")
	int32 GetActiveTabIndex() const { return ActiveTabIndex; }

	UFUNCTION(BlueprintPure, Category="Options")
	int32 GetSelectedWindowMode() const { return SelectedWindowMode; }

	UFUNCTION(BlueprintPure, Category="Options")
	bool GetSelectedVSync() const { return bSelectedVSync; }

	UFUNCTION(BlueprintPure, Category="Options")
	FIntPoint GetSelectedResolution() const
	{
		return Resolutions.IsValidIndex(SelectedResolution) ? Resolutions[SelectedResolution] : FIntPoint::ZeroValue;
	}

	UFUNCTION(BlueprintPure, Category="Options")
	int32 GetLastDisplayedCountdown() const { return LastDisplayedCountdown; }

	UFUNCTION(BlueprintCallable, Category="Options")
	void WindowPrevious();

	UFUNCTION(BlueprintCallable, Category="Options")
	void WindowNext();

	UFUNCTION(BlueprintCallable, Category="Options")
	void ResolutionPrevious();

	UFUNCTION(BlueprintCallable, Category="Options")
	void ResolutionNext();

	UFUNCTION(BlueprintCallable, Category="Options")
	void GraphicsPrevious();

	UFUNCTION(BlueprintCallable, Category="Options")
	void GraphicsNext();

	UFUNCTION(BlueprintCallable, Category="Options")
	void ToggleVSync();

	UFUNCTION(BlueprintCallable, Category="Options")
	void HandleBack();

	UFUNCTION()
	void OnMusicVolumeChanged(float Value);

	UFUNCTION()
	void OnEffectsVolumeChanged(float Value);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& Geometry, float DeltaTime) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& MouseEvent) override;
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Key) override;

private:
	template <typename TWidget>
	TWidget* FindRequiredWidget(FName Name);

	void InitializeScreenSettings();
	void PopulateSurvivalGuide();
	void CaptureDisplaySnapshot(UGameUserSettings* Settings);
	void SetScreenSteppersEnabled(bool bEnabled);
	void RefreshScreenLabels();
	void ChangeResolution(int32 Direction);
	void UpdateAudioPercentText(const TCHAR* WidgetName, float ClampedVolume);
	void FlushAudioConfigIfDirty(bool bForceFlush);

	UFUNCTION()
	void OnHowToPlayTabClicked();

	UFUNCTION()
	void OnScreenTabClicked();

	UFUNCTION()
	void OnSoundTabClicked();

	bool bAudioSettingsChanged = false;
	bool bAudioFlushPending = false;
	bool bSfxReleasePreviewPending = false;
	double LastSfxPreviewTime = 0.0;
	double LastAudioChangeTime = 0.0;

	bool bReturnToPause = false;
	bool bAwaitingConfirmation = false;
	float ConfirmationRemaining = 0.0f;
	int32 LastDisplayedCountdown = -1;
	int32 ActiveTabIndex = 0;

	TArray<FIntPoint> Resolutions;
	int32 SelectedResolution = 0;
	int32 SelectedWindowMode = 1;
	int32 SelectedQuality = 2;
	bool bSelectedVSync = false;

	FOptionsDisplaySnapshot DisplaySnapshot;
	TSet<FName> LoggedMissingWidgets;
};
