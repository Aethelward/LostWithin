// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/LostWithinMenu.h"

#include "AudioMaterialSlate/AudioMaterialSlider.h"
#include "Components/Button.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/ConfigCacheIni.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundMix.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	constexpr const TCHAR* AudioOptionsSection = TEXT("LostWithin.Audio");
	constexpr const TCHAR* AudioOptionsMusicKey = TEXT("MusicVolume");
	constexpr const TCHAR* AudioOptionsEffectsKey = TEXT("EffectsVolume");
	constexpr const TCHAR* AudioOptionsMixPath = TEXT("/Game/Audio/Mix/SM_Options.SM_Options");
	constexpr const TCHAR* AudioOptionsMusicClassPath = TEXT("/Game/Audio/Mix/SC_Music.SC_Music");
	constexpr const TCHAR* AudioOptionsEffectsClassPath = TEXT("/Game/Audio/Mix/SC_Sfx.SC_Sfx");

	float ReadAudioVolume(const TCHAR* Key)
	{
		float Volume = 1.0f;
		if (GConfig)
		{
			GConfig->GetFloat(AudioOptionsSection, Key, Volume, GGameUserSettingsIni);
		}
		return FMath::IsFinite(Volume) ? FMath::Clamp(Volume, 0.0f, 1.0f) : 1.0f;
	}

	void ApplyAudioVolume(const UObject* Context, USoundMix* Mix, const TCHAR* ClassPath, float Volume)
	{
		if (USoundClass* SoundClass = LoadObject<USoundClass>(nullptr, ClassPath))
		{
			UGameplayStatics::SetSoundMixClassOverride(Context, Mix, SoundClass, Volume, 1.0f, 0.08f, true);
		}
	}

	void ChangeAudioVolume(const UObject* Context, const TCHAR* Key, const TCHAR* ClassPath, float Value)
	{
		const float Volume = FMath::Clamp(Value, 0.0f, 1.0f);
		if (GConfig)
		{
			GConfig->SetFloat(AudioOptionsSection, Key, Volume, GGameUserSettingsIni);
		}
		if (USoundMix* Mix = LoadObject<USoundMix>(nullptr, AudioOptionsMixPath))
		{
			ApplyAudioVolume(Context, Mix, ClassPath, Volume);
		}
	}
}

void ApplyLostWithinAudioOptions(const UObject* WorldContextObject)
{
	if (USoundMix* Mix = LoadObject<USoundMix>(nullptr, AudioOptionsMixPath))
	{
		UGameplayStatics::SetBaseSoundMix(WorldContextObject, Mix);
		ApplyAudioVolume(WorldContextObject, Mix, AudioOptionsMusicClassPath, ReadAudioVolume(AudioOptionsMusicKey));
		ApplyAudioVolume(WorldContextObject, Mix, AudioOptionsEffectsClassPath, ReadAudioVolume(AudioOptionsEffectsKey));
	}
}

ALostWithinMenuGameMode::ALostWithinMenuGameMode()
{
	DefaultPawnClass = nullptr;
	HUDClass = nullptr;
	static ConstructorHelpers::FClassFinder<UUserWidget> Menu(TEXT("/Game/UI/Menu/MainMenu"));
	MainMenuClass = Menu.Class;
	static ConstructorHelpers::FObjectFinder<USoundBase> Music(
		TEXT("/Game/Audio/BGM/Menu/Juhani_Junkala_-_Post_Apocalyptic_Wastelands__Loop_Ready_.Juhani_Junkala_-_Post_Apocalyptic_Wastelands__Loop_Ready_"));
	MenuMusic = Music.Object;
}

void ALostWithinMenuGameMode::StartPlay()
{
	Super::StartPlay();

	APlayerController* Controller = UGameplayStatics::GetPlayerController(this, 0);
	if (!Controller || !Controller->IsLocalController() || !MainMenuClass)
	{
		return;
	}

	ApplyLostWithinAudioOptions(this);
	if (UUserWidget* Menu = CreateWidget<UUserWidget>(Controller, MainMenuClass))
	{
		Menu->AddToViewport();
		if (MenuMusic)
		{
			UGameplayStatics::SpawnSound2D(this, MenuMusic, 0.4f);
		}
	}
}

void ULostWithinOptionsMenu::NativeConstruct()
{
	Super::NativeConstruct();
	ApplyLostWithinAudioOptions(this);

	if (UAudioMaterialSlider* Music = Cast<UAudioMaterialSlider>(GetWidgetFromName(TEXT("MusicSlider"))))
	{
		Music->SetValue(ReadAudioVolume(AudioOptionsMusicKey));
		Music->OnValueChanged.AddUniqueDynamic(this, &ULostWithinOptionsMenu::OnMusicVolumeChanged);
	}
	if (UAudioMaterialSlider* Effects = Cast<UAudioMaterialSlider>(GetWidgetFromName(TEXT("SfxSlider"))))
	{
		Effects->SetValue(ReadAudioVolume(AudioOptionsEffectsKey));
		Effects->OnValueChanged.AddUniqueDynamic(this, &ULostWithinOptionsMenu::OnEffectsVolumeChanged);
	}
	if (UButton* SoundTab = Cast<UButton>(GetWidgetFromName(TEXT("Button_5"))))
	{
		SoundTab->OnClicked.AddUniqueDynamic(this, &ULostWithinOptionsMenu::PreviewEffectsVolume);
	}
}

void ULostWithinOptionsMenu::NativeDestruct()
{
	if (UAudioMaterialSlider* Music = Cast<UAudioMaterialSlider>(GetWidgetFromName(TEXT("MusicSlider"))))
	{
		Music->OnValueChanged.RemoveDynamic(this, &ULostWithinOptionsMenu::OnMusicVolumeChanged);
	}
	if (UAudioMaterialSlider* Effects = Cast<UAudioMaterialSlider>(GetWidgetFromName(TEXT("SfxSlider"))))
	{
		Effects->OnValueChanged.RemoveDynamic(this, &ULostWithinOptionsMenu::OnEffectsVolumeChanged);
	}
	if (UButton* SoundTab = Cast<UButton>(GetWidgetFromName(TEXT("Button_5"))))
	{
		SoundTab->OnClicked.RemoveDynamic(this, &ULostWithinOptionsMenu::PreviewEffectsVolume);
	}
	if (bAudioSettingsChanged && GConfig)
	{
		GConfig->Flush(false, GGameUserSettingsIni);
		bAudioSettingsChanged = false;
	}
	Super::NativeDestruct();
}

void ULostWithinOptionsMenu::OnMusicVolumeChanged(float Value)
{
	bAudioSettingsChanged = true;
	ChangeAudioVolume(this, AudioOptionsMusicKey, AudioOptionsMusicClassPath, Value);
}

void ULostWithinOptionsMenu::OnEffectsVolumeChanged(float Value)
{
	bAudioSettingsChanged = true;
	ChangeAudioVolume(this, AudioOptionsEffectsKey, AudioOptionsEffectsClassPath, Value);
}

void ULostWithinOptionsMenu::PreviewEffectsVolume()
{
	if (USoundBase* Click = LoadObject<USoundBase>(nullptr, TEXT("/Game/Audio/SFX/UI/click.click")))
	{
		UGameplayStatics::PlaySound2D(this, Click);
	}
}
