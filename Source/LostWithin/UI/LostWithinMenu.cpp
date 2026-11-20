// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/LostWithinMenu.h"

#include "AudioMaterialSlate/AudioMaterialSlider.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Components/AudioComponent.h"
#include "Components/Button.h"
#include "Components/MultiLineEditableTextBox.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/GameUserSettings.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "LostWithin.h"
#include "LostWithinPlayerController.h"
#include "Misc/ConfigCacheIni.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundMix.h"
#include "Styling/CoreStyle.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	constexpr const TCHAR* AudioOptionsSection = TEXT("LostWithin.Audio");
	constexpr const TCHAR* AudioOptionsMusicKey = TEXT("MusicVolume");
	constexpr const TCHAR* AudioOptionsEffectsKey = TEXT("EffectsVolume");
	constexpr const TCHAR* AudioOptionsMixPath = TEXT("/Game/Audio/Mix/SM_Options.SM_Options");
	constexpr const TCHAR* AudioOptionsMusicClassPath = TEXT("/Game/Audio/Mix/SC_Music.SC_Music");
	constexpr const TCHAR* AudioOptionsEffectsClassPath = TEXT("/Game/Audio/Mix/SC_Sfx.SC_Sfx");
	constexpr const TCHAR* MenuCinzelBoldFontPath = TEXT("/Game/UI/Fonts/Cinzel-Bold_Font.Cinzel-Bold_Font");
	constexpr const TCHAR* MenuCinzelRegularFontPath = TEXT("/Game/UI/Fonts/Cinzel-Regular_Font.Cinzel-Regular_Font");
	constexpr const TCHAR* MenuUIClickSoundPath = TEXT("/Game/Audio/SFX/UI/click.click");
	constexpr double MenuSfxPreviewIntervalSeconds = 0.12;
	constexpr double MenuAudioFlushIdleSeconds = 0.15;

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

	float ChangeAudioVolume(const UObject* Context, const TCHAR* Key, const TCHAR* ClassPath, float Value)
	{
		const float Volume = FMath::IsFinite(Value) ? FMath::Clamp(Value, 0.0f, 1.0f) : 1.0f;
		if (GConfig)
		{
			GConfig->SetFloat(AudioOptionsSection, Key, Volume, GGameUserSettingsIni);
		}
		if (USoundMix* Mix = LoadObject<USoundMix>(nullptr, AudioOptionsMixPath))
		{
			ApplyAudioVolume(Context, Mix, ClassPath, Volume);
		}
		return Volume;
	}

	FLinearColor MenuColorFromHex(const TCHAR* HexRRGGBBAA)
	{
		return FLinearColor(FColor::FromHex(HexRRGGBBAA));
	}

	FSlateBrush MakeMenuHorrorButtonBrush(const FLinearColor& Fill, const FLinearColor& Outline, float OutlineWidth, float CornerRadius)
	{
		FSlateBrush Brush;
		Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
		Brush.TintColor = FSlateColor(Fill);
		Brush.OutlineSettings.CornerRadii = FVector4(CornerRadius, CornerRadius, CornerRadius, CornerRadius);
		Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
		Brush.OutlineSettings.Color = FSlateColor(Outline);
		Brush.OutlineSettings.Width = OutlineWidth;
		Brush.OutlineSettings.bUseBrushTransparency = true;
		return Brush;
	}

	FButtonStyle MakeMenuTabButtonStyle(bool bActive)
	{
		FButtonStyle Style;
		constexpr float Radius = 4.0f;

		if (bActive)
		{
			Style.SetNormal(MakeMenuHorrorButtonBrush(MenuColorFromHex(TEXT("781815F5")), MenuColorFromHex(TEXT("D4423CFF")), 1.5f, Radius));
			Style.SetHovered(MakeMenuHorrorButtonBrush(MenuColorFromHex(TEXT("8E201CF5")), MenuColorFromHex(TEXT("E6524BFF")), 1.5f, Radius));
			Style.SetPressed(MakeMenuHorrorButtonBrush(MenuColorFromHex(TEXT("5A110FF5")), MenuColorFromHex(TEXT("8F1916FF")), 1.5f, Radius));
		}
		else
		{
			Style.SetNormal(MakeMenuHorrorButtonBrush(MenuColorFromHex(TEXT("15171EE0")), MenuColorFromHex(TEXT("4A2322CC")), 1.0f, Radius));
			Style.SetHovered(MakeMenuHorrorButtonBrush(MenuColorFromHex(TEXT("571311EB")), MenuColorFromHex(TEXT("C93832FF")), 1.0f, Radius));
			Style.SetPressed(MakeMenuHorrorButtonBrush(MenuColorFromHex(TEXT("3B0B0AF5")), MenuColorFromHex(TEXT("8F1916FF")), 1.0f, Radius));
		}
		Style.SetDisabled(MakeMenuHorrorButtonBrush(MenuColorFromHex(TEXT("0D0E12AA")), MenuColorFromHex(TEXT("2A1E1E66")), 1.0f, Radius));
		Style.SetNormalPadding(FMargin(16.0f, 8.0f));
		Style.SetPressedPadding(FMargin(16.0f, 9.0f, 16.0f, 7.0f));
		return Style;
	}

	void ApplyMenuUIOnlyInput(APlayerController* Controller, UUserWidget* FocusTarget)
	{
		if (!FocusTarget)
		{
			return;
		}
		if (Controller)
		{
			Controller->SetShowMouseCursor(true);
			FInputModeUIOnly Mode;
			Mode.SetWidgetToFocus(FocusTarget->TakeWidget());
			Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
			Controller->SetInputMode(Mode);
		}
		FocusTarget->SetKeyboardFocus();
	}

	bool IsMenuPointerHeld()
	{
		return FSlateApplication::IsInitialized()
			&& FSlateApplication::Get().GetPressedMouseButtons().Contains(EKeys::LeftMouseButton);
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

FSlateFontInfo MakeLostWithinCinzelFont(bool bBold, int32 Size)
{
	const int32 ClampedSize = FMath::Max(1, Size);
	const TCHAR* AssetPath = bBold ? MenuCinzelBoldFontPath : MenuCinzelRegularFontPath;
	if (UFont* FontAsset = LoadObject<UFont>(nullptr, AssetPath))
	{
		FSlateFontInfo FontInfo(FontAsset, static_cast<float>(ClampedSize));
		FontInfo.TypefaceFontName = FName(TEXT("Default"));
		return FontInfo;
	}
	return FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", ClampedSize);
}

void PlayLostWithinUIClickSound(const UObject* WorldContextObject)
{
	UWorld* World = (GEngine && WorldContextObject)
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
		: nullptr;
	if (!World)
	{
		return;
	}

	if (USoundBase* ClickSound = LoadObject<USoundBase>(nullptr, MenuUIClickSoundPath))
	{
		UGameplayStatics::PlaySound2D(World, ClickSound, 1.0f, 1.0f, 0.0f, nullptr, nullptr, /*bIsUISound=*/true);
	}
}

FButtonStyle MakeLostWithinHorrorButtonStyle(bool bPrimaryAccent)
{
	FButtonStyle Style;
	constexpr float Radius = 4.0f;

	if (bPrimaryAccent)
	{
		Style.SetNormal(MakeMenuHorrorButtonBrush(MenuColorFromHex(TEXT("6B1613F2")), MenuColorFromHex(TEXT("C93832E0")), 1.5f, Radius));
		Style.SetHovered(MakeMenuHorrorButtonBrush(MenuColorFromHex(TEXT("8C1E1AF8")), MenuColorFromHex(TEXT("EB524BFF")), 1.5f, Radius));
		Style.SetPressed(MakeMenuHorrorButtonBrush(MenuColorFromHex(TEXT("4A0D0BF8")), MenuColorFromHex(TEXT("B32D27FF")), 1.5f, Radius));
		Style.SetDisabled(MakeMenuHorrorButtonBrush(MenuColorFromHex(TEXT("261111AA")), MenuColorFromHex(TEXT("4A222266")), 1.0f, Radius));
	}
	else
	{
		Style.SetNormal(MakeMenuHorrorButtonBrush(MenuColorFromHex(TEXT("12141AE8")), MenuColorFromHex(TEXT("4A2322CC")), 1.0f, Radius));
		Style.SetHovered(MakeMenuHorrorButtonBrush(MenuColorFromHex(TEXT("751412F5")), MenuColorFromHex(TEXT("C93832FF")), 1.25f, Radius));
		Style.SetPressed(MakeMenuHorrorButtonBrush(MenuColorFromHex(TEXT("480B0AF5")), MenuColorFromHex(TEXT("9E2622FF")), 1.25f, Radius));
		Style.SetDisabled(MakeMenuHorrorButtonBrush(MenuColorFromHex(TEXT("0D0E12AA")), MenuColorFromHex(TEXT("2B1B1B66")), 1.0f, Radius));
	}

	Style.SetNormalPadding(FMargin(20.0f, 8.0f));
	Style.SetPressedPadding(FMargin(20.0f, 9.0f, 20.0f, 7.0f));
	return Style;
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
	StopMenuMusic();
	Super::StartPlay();

	APlayerController* Controller = UGameplayStatics::GetPlayerController(this, 0);
	if (!Controller || !Controller->IsLocalController())
	{
		return;
	}
	if (!MainMenuClass)
	{
		UE_LOG(LogLostWithin, Warning, TEXT("ALostWithinMenuGameMode: MainMenuClass is unavailable."));
		return;
	}

	ApplyLostWithinAudioOptions(this);
	if (UUserWidget* Menu = CreateWidget<UUserWidget>(Controller, MainMenuClass))
	{
		Menu->AddToViewport();
		ApplyMenuUIOnlyInput(Controller, Menu);

		if (MenuMusic)
		{
			MenuMusicComponent = UGameplayStatics::CreateSound2D(
				this,
				MenuMusic,
				0.4f,
				1.0f,
				0.0f,
				nullptr,
				/*bPersistAcrossLevelTransition=*/false,
				/*bAutoDestroy=*/true);
			if (MenuMusicComponent)
			{
				MenuMusicComponent->bIsUISound = true;
				MenuMusicComponent->Play();
			}
		}
	}
}

void ALostWithinMenuGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopMenuMusic();
	Super::EndPlay(EndPlayReason);
}

void ALostWithinMenuGameMode::StopMenuMusic()
{
	if (IsValid(MenuMusicComponent))
	{
		MenuMusicComponent->Stop();
	}
	MenuMusicComponent = nullptr;
}

ULostWithinMainMenu::ULostWithinMainMenu(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetIsFocusable(true);
}

template <typename TWidget>
TWidget* ULostWithinMainMenu::FindRequiredWidget(FName Name)
{
	TWidget* Found = Cast<TWidget>(GetWidgetFromName(Name));
	if (!Found && !LoggedMissingWidgets.Contains(Name))
	{
		LoggedMissingWidgets.Add(Name);
		UE_LOG(LogLostWithin, Warning, TEXT("ULostWithinMainMenu: missing required widget '%s'."), *Name.ToString());
	}
	return Found;
}

void ULostWithinMainMenu::NativeConstruct()
{
	Super::NativeConstruct();
	SetIsFocusable(true);

	if (UButton* PlayButton = FindRequiredWidget<UButton>(TEXT("Button_0")))
	{
		PlayButton->OnClicked.Clear();
		PlayButton->OnClicked.AddDynamic(this, &ULostWithinMainMenu::HandlePlayClicked);
	}
	if (UButton* OptionsButton = FindRequiredWidget<UButton>(TEXT("Button_19")))
	{
		OptionsButton->OnClicked.Clear();
		OptionsButton->OnClicked.AddDynamic(this, &ULostWithinMainMenu::HandleOptionsClicked);
	}
	if (UButton* QuitButton = FindRequiredWidget<UButton>(TEXT("Button_43")))
	{
		QuitButton->OnClicked.Clear();
		QuitButton->OnClicked.AddDynamic(this, &ULostWithinMainMenu::HandleQuitClicked);
	}

	ApplyMenuUIOnlyInput(GetOwningPlayer(), this);
}

void ULostWithinMainMenu::NativeDestruct()
{
	if (UButton* PlayButton = Cast<UButton>(GetWidgetFromName(TEXT("Button_0"))))
	{
		PlayButton->OnClicked.RemoveDynamic(this, &ULostWithinMainMenu::HandlePlayClicked);
	}
	if (UButton* OptionsButton = Cast<UButton>(GetWidgetFromName(TEXT("Button_19"))))
	{
		OptionsButton->OnClicked.RemoveDynamic(this, &ULostWithinMainMenu::HandleOptionsClicked);
	}
	if (UButton* QuitButton = Cast<UButton>(GetWidgetFromName(TEXT("Button_43"))))
	{
		QuitButton->OnClicked.RemoveDynamic(this, &ULostWithinMainMenu::HandleQuitClicked);
	}
	Super::NativeDestruct();
}

FReply ULostWithinMainMenu::NativeOnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& MouseEvent)
{
	SetKeyboardFocus();
	return FReply::Handled();
}

FReply ULostWithinMainMenu::NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Key)
{
	if (!Key.IsRepeat() && Key.GetKey() == EKeys::Escape)
	{
		SetKeyboardFocus();
		return FReply::Handled();
	}
	return Super::NativeOnPreviewKeyDown(Geometry, Key);
}

void ULostWithinMainMenu::HandlePlayClicked()
{
	PlayLostWithinUIClickSound(this);

	if (UWorld* World = GetWorld())
	{
		if (ALostWithinMenuGameMode* MenuMode = Cast<ALostWithinMenuGameMode>(World->GetAuthGameMode()))
		{
			MenuMode->StopMenuMusic();
		}
	}

	if (APlayerController* PC = GetOwningPlayer())
	{
		PC->SetShowMouseCursor(false);
		PC->SetInputMode(FInputModeGameOnly());
	}

	UGameplayStatics::OpenLevel(this, FName(TEXT("/Game/FirstPerson/Lvl_FirstPerson")));
}

void ULostWithinMainMenu::HandleOptionsClicked()
{
	PlayLostWithinUIClickSound(this);

	UClass* OptionsClass = LoadClass<ULostWithinOptionsMenu>(nullptr, TEXT("/Game/UI/Menu/OptionsMenu.OptionsMenu_C"));
	if (!OptionsClass)
	{
		UE_LOG(LogLostWithin, Warning, TEXT("ULostWithinMainMenu: could not load /Game/UI/Menu/OptionsMenu."));
		return;
	}

	APlayerController* PC = GetOwningPlayer();
	UWorld* World = GetWorld();
	ULostWithinOptionsMenu* Options = PC
		? CreateWidget<ULostWithinOptionsMenu>(PC, OptionsClass)
		: (World ? CreateWidget<ULostWithinOptionsMenu>(World, OptionsClass) : nullptr);
	if (!Options)
	{
		UE_LOG(LogLostWithin, Warning, TEXT("ULostWithinMainMenu: failed to create OptionsMenu widget."));
		return;
	}

	RemoveFromParent();
	Options->SetReturnToPause(false);
	Options->AddToViewport(50);
	ApplyMenuUIOnlyInput(PC, Options);
}

void ULostWithinMainMenu::HandleQuitClicked()
{
	PlayLostWithinUIClickSound(this);
	UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit, false);
}

ULostWithinOptionsMenu::ULostWithinOptionsMenu(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetIsFocusable(true);
}

template <typename TWidget>
TWidget* ULostWithinOptionsMenu::FindRequiredWidget(FName Name)
{
	TWidget* Found = Cast<TWidget>(GetWidgetFromName(Name));
	if (!Found && !LoggedMissingWidgets.Contains(Name))
	{
		LoggedMissingWidgets.Add(Name);
		UE_LOG(LogLostWithin, Warning, TEXT("ULostWithinOptionsMenu: missing required widget '%s'."), *Name.ToString());
	}
	return Found;
}

void ULostWithinOptionsMenu::NativeConstruct()
{
	Super::NativeConstruct();
	SetIsFocusable(true);

	if (UButton* BackButton = FindRequiredWidget<UButton>(TEXT("Button_1")))
	{
		BackButton->OnClicked.Clear();
		BackButton->OnClicked.AddDynamic(this, &ULostWithinOptionsMenu::HandleBack);
	}
	if (UButton* HowToPlayTab = FindRequiredWidget<UButton>(TEXT("Button_3")))
	{
		HowToPlayTab->OnClicked.Clear();
		HowToPlayTab->OnClicked.AddDynamic(this, &ULostWithinOptionsMenu::OnHowToPlayTabClicked);
	}
	if (UButton* ScreenTab = FindRequiredWidget<UButton>(TEXT("Button_4")))
	{
		ScreenTab->OnClicked.Clear();
		ScreenTab->OnClicked.AddDynamic(this, &ULostWithinOptionsMenu::OnScreenTabClicked);
	}
	if (UButton* SoundTab = FindRequiredWidget<UButton>(TEXT("Button_5")))
	{
		SoundTab->OnClicked.Clear();
		SoundTab->OnClicked.AddDynamic(this, &ULostWithinOptionsMenu::OnSoundTabClicked);
	}

	PopulateSurvivalGuide();
	InitializeScreenSettings();
	ApplyLostWithinAudioOptions(this);

	const float MusicVol = ReadAudioVolume(AudioOptionsMusicKey);
	if (UAudioMaterialSlider* Music = FindRequiredWidget<UAudioMaterialSlider>(TEXT("MusicSlider")))
	{
		Music->SetValue(MusicVol);
		Music->OnValueChanged.RemoveDynamic(this, &ULostWithinOptionsMenu::OnMusicVolumeChanged);
		Music->OnValueChanged.AddUniqueDynamic(this, &ULostWithinOptionsMenu::OnMusicVolumeChanged);
	}
	UpdateAudioPercentText(TEXT("MusicPercentText"), MusicVol);

	const float EffectsVol = ReadAudioVolume(AudioOptionsEffectsKey);
	if (UAudioMaterialSlider* Effects = FindRequiredWidget<UAudioMaterialSlider>(TEXT("SfxSlider")))
	{
		Effects->SetValue(EffectsVol);
		Effects->OnValueChanged.RemoveDynamic(this, &ULostWithinOptionsMenu::OnEffectsVolumeChanged);
		Effects->OnValueChanged.AddUniqueDynamic(this, &ULostWithinOptionsMenu::OnEffectsVolumeChanged);
	}
	UpdateAudioPercentText(TEXT("SfxPercentText"), EffectsVol);

	bAudioSettingsChanged = false;
	bAudioFlushPending = false;
	bSfxReleasePreviewPending = false;
	LastSfxPreviewTime = 0.0;
	LastAudioChangeTime = 0.0;

	ShowTab(0);
	SetKeyboardFocus();
}

void ULostWithinOptionsMenu::NativeDestruct()
{
	RevertDisplayChanges();

	if (UAudioMaterialSlider* Music = Cast<UAudioMaterialSlider>(GetWidgetFromName(TEXT("MusicSlider"))))
	{
		Music->OnValueChanged.RemoveDynamic(this, &ULostWithinOptionsMenu::OnMusicVolumeChanged);
	}
	if (UAudioMaterialSlider* Effects = Cast<UAudioMaterialSlider>(GetWidgetFromName(TEXT("SfxSlider"))))
	{
		Effects->OnValueChanged.RemoveDynamic(this, &ULostWithinOptionsMenu::OnEffectsVolumeChanged);
	}
	for (const TCHAR* ButtonName : {
		TEXT("Button_0"), TEXT("Button_1"), TEXT("Button_3"), TEXT("Button_4"), TEXT("Button_5"),
		TEXT("windowmode-"), TEXT("windowmode+"), TEXT("resolution-"), TEXT("resolution+"),
		TEXT("graphics-"), TEXT("graphics+"), TEXT("vsync-"), TEXT("vsync+") })
	{
		if (UButton* Button = Cast<UButton>(GetWidgetFromName(ButtonName)))
		{
			Button->OnClicked.RemoveAll(this);
		}
	}

	FlushAudioConfigIfDirty(/*bForceFlush=*/true);
	Super::NativeDestruct();
}

void ULostWithinOptionsMenu::PopulateSurvivalGuide()
{
	if (UMultiLineEditableTextBox* Guide = FindRequiredWidget<UMultiLineEditableTextBox>(TEXT("MultiLineEditableTextBox_2")))
	{
		Guide->SetIsReadOnly(true);
		Guide->AllowContextMenu = false;
		Guide->SynchronizeProperties();
		Guide->SetText(NSLOCTEXT("LostWithin", "Subject09Guide",
			"SUBJECT 09 - SURVIVAL GUIDE\n\n"
			"W A S D - Move\n"
			"Mouse - Look\n"
			"Space - Jump\n"
			"Left Ctrl - Sprint\n"
			"Left Shift - Walk\n"
			"E - Interact, read, search, collect and try the exit padlock\n"
			"F - Flashlight\n"
			"R - Replace a flashlight battery\n"
			"Tab - Inventory (read, use, drop or drag items)\n"
			"Esc - Close a note or pause\n\n"
			"Recover the three exit digits from randomized notes. Search toolboxes and side rooms; some toolboxes require a matching key. Read collected notes again from your inventory. Set the skull padlock's dials to the digits, in order, at the southeastern exit.\n\n"
			"HEADPHONES STRONGLY RECOMMENDED - listen closely to survive. Every creature tells you where it is by sound.\n\n"
			"THE FOUR CREATURES - each hunts with one sense\n"
			"Doll SEES. It sleeps in the dark and wakes the moment any light touches it - your flashlight or a torch. Awake, it sees you only when you are lit, and then it rushes you - watching it does not stop it. Only darkness puts it back to sleep.\n"
			"Ghoul HEARS. It is blind and hears your exact footsteps; stay silent and it gives up. Light burns it away, but it waits at a torch's edge and puts the torch out.\n"
			"BoneDog SMELLS. It is deaf and sees only 4 m, flashlight or not. It follows your trail, even to where you stand still. Torch smoke breaks the trail, and items you carried a while draw it once.\n"
			"Lunger FEELS. Blind and deaf, it feels sprints, landings and movement close by. When you hear the ratchet, freeze or sidestep: its lunge goes in a straight line.\n"
			"The cat's active chaos sanctuary can protect you.\n\n"
			"There are no weapons or hiding cabinets. A monster must physically touch your body to kill you. Reading, inventory and the padlock view do not stop danger. The flashlight needs two batteries; conserve its charge."));
	}
}

void ULostWithinOptionsMenu::UpdateAudioPercentText(const TCHAR* WidgetName, float ClampedVolume)
{
	// Optional until Wave 4 adds MusicPercentText / SfxPercentText to OptionsMenu.uasset.
	if (UTextBlock* PercentLabel = Cast<UTextBlock>(GetWidgetFromName(WidgetName)))
	{
		const int32 Percent = FMath::Clamp(FMath::RoundToInt(ClampedVolume * 100.0f), 0, 100);
		PercentLabel->SetText(FText::FromString(FString::Printf(TEXT("%d%%"), Percent)));
	}
}

void ULostWithinOptionsMenu::FlushAudioConfigIfDirty(bool bForceFlush)
{
	if (!bAudioSettingsChanged && !bAudioFlushPending && !bSfxReleasePreviewPending)
	{
		return;
	}

	const double Now = FPlatformTime::Seconds();
	const bool bMouseHeld = IsMenuPointerHeld();
	const bool bReadyToFlush = bForceFlush || !bMouseHeld || (Now - LastAudioChangeTime >= MenuAudioFlushIdleSeconds);
	if (!bReadyToFlush)
	{
		return;
	}

	if (bSfxReleasePreviewPending && !bForceFlush)
	{
		PlayLostWithinUIClickSound(this);
		LastSfxPreviewTime = Now;
	}
	bSfxReleasePreviewPending = false;

	if ((bAudioSettingsChanged || bAudioFlushPending) && GConfig)
	{
		GConfig->Flush(false, GGameUserSettingsIni);
	}
	bAudioSettingsChanged = false;
	bAudioFlushPending = false;
}

void ULostWithinOptionsMenu::OnMusicVolumeChanged(float Value)
{
	const float Clamped = ChangeAudioVolume(this, AudioOptionsMusicKey, AudioOptionsMusicClassPath, Value);
	UpdateAudioPercentText(TEXT("MusicPercentText"), Clamped);

	bAudioSettingsChanged = true;
	bAudioFlushPending = true;
	LastAudioChangeTime = FPlatformTime::Seconds();
	if (!IsMenuPointerHeld())
	{
		FlushAudioConfigIfDirty(/*bForceFlush=*/true);
	}
}

void ULostWithinOptionsMenu::OnEffectsVolumeChanged(float Value)
{
	const float Clamped = ChangeAudioVolume(this, AudioOptionsEffectsKey, AudioOptionsEffectsClassPath, Value);
	UpdateAudioPercentText(TEXT("SfxPercentText"), Clamped);

	const double Now = FPlatformTime::Seconds();
	bAudioSettingsChanged = true;
	bAudioFlushPending = true;
	LastAudioChangeTime = Now;

	if (Now - LastSfxPreviewTime >= MenuSfxPreviewIntervalSeconds)
	{
		PlayLostWithinUIClickSound(this);
		LastSfxPreviewTime = Now;
		bSfxReleasePreviewPending = false;
	}
	else
	{
		bSfxReleasePreviewPending = true;
	}

	if (!IsMenuPointerHeld())
	{
		if (bSfxReleasePreviewPending)
		{
			PlayLostWithinUIClickSound(this);
			LastSfxPreviewTime = Now;
			bSfxReleasePreviewPending = false;
		}
		FlushAudioConfigIfDirty(/*bForceFlush=*/true);
	}
}

void ULostWithinOptionsMenu::ShowTab(int32 TabIndex)
{
	ActiveTabIndex = FMath::Clamp(TabIndex, 0, 2);

	const TCHAR* BoxNames[] = { TEXT("VB_HowToPlay"), TEXT("VB_ScreenSettings"), TEXT("VB_SoundSettings") };
	const TCHAR* LegacyScaleNames[] = { TEXT("ScaleBox_119"), TEXT("ScaleBox_87"), TEXT("ScaleBox_148") };
	const TCHAR* TabButtonNames[] = { TEXT("Button_3"), TEXT("Button_4"), TEXT("Button_5") };

	for (int32 Index = 0; Index < 3; ++Index)
	{
		const bool bActive = (Index == ActiveTabIndex);
		if (UWidget* Panel = FindRequiredWidget<UWidget>(BoxNames[Index]))
		{
			Panel->SetVisibility(bActive ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
		}
		if (UWidget* LegacyScale = GetWidgetFromName(LegacyScaleNames[Index]))
		{
			LegacyScale->SetVisibility(bActive ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
		}
		if (UButton* TabButton = FindRequiredWidget<UButton>(TabButtonNames[Index]))
		{
			TabButton->SetStyle(MakeMenuTabButtonStyle(bActive));
			TabButton->SetBackgroundColor(FLinearColor::White);
		}
	}

	SetKeyboardFocus();
}

void ULostWithinOptionsMenu::OnHowToPlayTabClicked()
{
	PlayLostWithinUIClickSound(this);
	ShowTab(0);
}

void ULostWithinOptionsMenu::OnScreenTabClicked()
{
	PlayLostWithinUIClickSound(this);
	ShowTab(1);
}

void ULostWithinOptionsMenu::OnSoundTabClicked()
{
	PlayLostWithinUIClickSound(this);
	ShowTab(2);
}

void ULostWithinOptionsMenu::CaptureDisplaySnapshot(UGameUserSettings* Settings)
{
	if (!Settings)
	{
		return;
	}

	DisplaySnapshot.bValid = true;
	DisplaySnapshot.Resolution = Settings->GetScreenResolution();
	DisplaySnapshot.WindowMode = Settings->GetFullscreenMode();
	DisplaySnapshot.OverallQuality = Settings->GetOverallScalabilityLevel();
	DisplaySnapshot.ViewDistance = Settings->GetViewDistanceQuality();
	DisplaySnapshot.AntiAliasing = Settings->GetAntiAliasingQuality();
	DisplaySnapshot.Shadow = Settings->GetShadowQuality();
	DisplaySnapshot.GlobalIllumination = Settings->GetGlobalIlluminationQuality();
	DisplaySnapshot.Reflection = Settings->GetReflectionQuality();
	DisplaySnapshot.PostProcess = Settings->GetPostProcessingQuality();
	DisplaySnapshot.Texture = Settings->GetTextureQuality();
	DisplaySnapshot.Effects = Settings->GetVisualEffectQuality();
	DisplaySnapshot.Foliage = Settings->GetFoliageQuality();
	DisplaySnapshot.Shading = Settings->GetShadingQuality();
	DisplaySnapshot.bVSync = Settings->IsVSyncEnabled();
}

void ULostWithinOptionsMenu::SetScreenSteppersEnabled(bool bEnabled)
{
	for (const TCHAR* Name : {
		TEXT("windowmode-"), TEXT("windowmode+"),
		TEXT("resolution-"), TEXT("resolution+"),
		TEXT("graphics-"), TEXT("graphics+"),
		TEXT("vsync-"), TEXT("vsync+") })
	{
		if (UWidget* Control = FindRequiredWidget<UWidget>(Name))
		{
			Control->SetIsEnabled(bEnabled);
		}
	}
}

void ULostWithinOptionsMenu::InitializeScreenSettings()
{
	if (UButton* B = FindRequiredWidget<UButton>(TEXT("windowmode-"))) { B->OnClicked.Clear(); B->OnClicked.AddDynamic(this, &ULostWithinOptionsMenu::WindowPrevious); }
	if (UButton* B = FindRequiredWidget<UButton>(TEXT("windowmode+"))) { B->OnClicked.Clear(); B->OnClicked.AddDynamic(this, &ULostWithinOptionsMenu::WindowNext); }
	if (UButton* B = FindRequiredWidget<UButton>(TEXT("resolution-"))) { B->OnClicked.Clear(); B->OnClicked.AddDynamic(this, &ULostWithinOptionsMenu::ResolutionPrevious); }
	if (UButton* B = FindRequiredWidget<UButton>(TEXT("resolution+"))) { B->OnClicked.Clear(); B->OnClicked.AddDynamic(this, &ULostWithinOptionsMenu::ResolutionNext); }
	if (UButton* B = FindRequiredWidget<UButton>(TEXT("graphics-"))) { B->OnClicked.Clear(); B->OnClicked.AddDynamic(this, &ULostWithinOptionsMenu::GraphicsPrevious); }
	if (UButton* B = FindRequiredWidget<UButton>(TEXT("graphics+"))) { B->OnClicked.Clear(); B->OnClicked.AddDynamic(this, &ULostWithinOptionsMenu::GraphicsNext); }
	if (UButton* B = FindRequiredWidget<UButton>(TEXT("vsync-"))) { B->OnClicked.Clear(); B->OnClicked.AddDynamic(this, &ULostWithinOptionsMenu::ToggleVSync); }
	if (UButton* B = FindRequiredWidget<UButton>(TEXT("vsync+"))) { B->OnClicked.Clear(); B->OnClicked.AddDynamic(this, &ULostWithinOptionsMenu::ToggleVSync); }
	if (UButton* B = FindRequiredWidget<UButton>(TEXT("Button_0"))) { B->OnClicked.Clear(); B->OnClicked.AddDynamic(this, &ULostWithinOptionsMenu::ApplyScreenSettings); }

	UGameUserSettings* Settings = UGameUserSettings::GetGameUserSettings();
	if (!Settings)
	{
		UE_LOG(LogLostWithin, Warning, TEXT("ULostWithinOptionsMenu: UGameUserSettings is unavailable."));
		Resolutions = { FIntPoint(1280, 720) };
		SelectedResolution = 0;
		SelectedWindowMode = 1;
		SelectedQuality = 2;
		bSelectedVSync = false;
		SetScreenSteppersEnabled(true);
		RefreshScreenLabels();
		return;
	}

	SelectedWindowMode = FMath::Clamp(static_cast<int32>(Settings->GetFullscreenMode()), 0, 2);
	const int32 RawQuality = Settings->GetOverallScalabilityLevel();
	SelectedQuality = (RawQuality >= 0 && RawQuality <= 4) ? RawQuality : -1;
	bSelectedVSync = Settings->IsVSyncEnabled();

	Resolutions.Empty();
	UKismetSystemLibrary::GetSupportedFullscreenResolutions(Resolutions);
	FIntPoint Current = Settings->GetScreenResolution();
	if (Current.X <= 0 || Current.Y <= 0)
	{
		if (APlayerController* PC = GetOwningPlayer())
		{
			PC->GetViewportSize(Current.X, Current.Y);
		}
	}
	if (Current.X <= 0 || Current.Y <= 0)
	{
		Current = FIntPoint(1280, 720);
	}
	Resolutions.AddUnique(Current);
	Resolutions.RemoveAll([](FIntPoint Value) { return Value.X <= 0 || Value.Y <= 0; });
	Resolutions.Sort([](FIntPoint A, FIntPoint B) { return A.X == B.X ? A.Y < B.Y : A.X < B.X; });
	SelectedResolution = Resolutions.IndexOfByKey(Current);
	if (SelectedResolution == INDEX_NONE && !Resolutions.IsEmpty())
	{
		SelectedResolution = 0;
	}

	CaptureDisplaySnapshot(Settings);
	SetScreenSteppersEnabled(!bAwaitingConfirmation);
	RefreshScreenLabels();
}

void ULostWithinOptionsMenu::RefreshScreenLabels()
{
	auto SetLabelText = [this](FName Name, const FString& Value)
	{
		if (UTextBlock* Label = FindRequiredWidget<UTextBlock>(Name))
		{
			Label->SetText(FText::FromString(Value));
		}
	};

	const TCHAR* Modes[] = { TEXT("Fullscreen"), TEXT("Borderless"), TEXT("Windowed") };
	const TCHAR* Qualities[] = { TEXT("Low"), TEXT("Medium"), TEXT("High"), TEXT("Epic"), TEXT("Cinematic") };

	SetLabelText(TEXT("TextBlock_124"), Modes[FMath::Clamp(SelectedWindowMode, 0, 2)]);
	if (Resolutions.IsValidIndex(SelectedResolution))
	{
		const FIntPoint Size = Resolutions[SelectedResolution];
		SetLabelText(TEXT("TextBlock_2"), FString::Printf(TEXT("%d x %d"), Size.X, Size.Y));
	}
	else
	{
		FindRequiredWidget<UTextBlock>(TEXT("TextBlock_2"));
	}

	const TCHAR* QualityText = (SelectedQuality >= 0 && SelectedQuality <= 4) ? Qualities[SelectedQuality] : TEXT("Custom");
	SetLabelText(TEXT("TextBlock_6"), QualityText);
	SetLabelText(TEXT("TextBlock_10"), bSelectedVSync ? TEXT("On") : TEXT("Off"));

	if (bAwaitingConfirmation)
	{
		const int32 CeilSec = FMath::Max(1, FMath::CeilToInt(ConfirmationRemaining));
		LastDisplayedCountdown = CeilSec;
		SetLabelText(TEXT("TextBlock_0"), FString::Printf(TEXT("KEEP CHANGES (%d)"), CeilSec));
	}
	else
	{
		LastDisplayedCountdown = -1;
		SetLabelText(TEXT("TextBlock_0"), TEXT("APPLY"));
	}
}

void ULostWithinOptionsMenu::WindowPrevious()
{
	PlayLostWithinUIClickSound(this);
	SelectedWindowMode = (SelectedWindowMode + 2) % 3;
	RefreshScreenLabels();
}

void ULostWithinOptionsMenu::WindowNext()
{
	PlayLostWithinUIClickSound(this);
	SelectedWindowMode = (SelectedWindowMode + 1) % 3;
	RefreshScreenLabels();
}

void ULostWithinOptionsMenu::ChangeResolution(int32 Direction)
{
	if (!Resolutions.IsEmpty())
	{
		const int32 SafeCurrent = Resolutions.IsValidIndex(SelectedResolution) ? SelectedResolution : 0;
		SelectedResolution = (SafeCurrent + Direction + Resolutions.Num()) % Resolutions.Num();
		RefreshScreenLabels();
	}
}

void ULostWithinOptionsMenu::ResolutionPrevious()
{
	PlayLostWithinUIClickSound(this);
	ChangeResolution(-1);
}

void ULostWithinOptionsMenu::ResolutionNext()
{
	PlayLostWithinUIClickSound(this);
	ChangeResolution(1);
}

void ULostWithinOptionsMenu::GraphicsPrevious()
{
	PlayLostWithinUIClickSound(this);
	SelectedQuality = (SelectedQuality < 0) ? 4 : (SelectedQuality + 4) % 5;
	RefreshScreenLabels();
}

void ULostWithinOptionsMenu::GraphicsNext()
{
	PlayLostWithinUIClickSound(this);
	SelectedQuality = (SelectedQuality < 0) ? 0 : (SelectedQuality + 1) % 5;
	RefreshScreenLabels();
}

void ULostWithinOptionsMenu::ToggleVSync()
{
	PlayLostWithinUIClickSound(this);
	bSelectedVSync = !bSelectedVSync;
	RefreshScreenLabels();
}

void ULostWithinOptionsMenu::ApplyScreenSettings()
{
	PlayLostWithinUIClickSound(this);

	UGameUserSettings* Settings = UGameUserSettings::GetGameUserSettings();
	if (!Settings || !Resolutions.IsValidIndex(SelectedResolution))
	{
		return;
	}

	if (bAwaitingConfirmation)
	{
		Settings->ConfirmVideoMode();
		Settings->SaveSettings();
		CaptureDisplaySnapshot(Settings);
		bAwaitingConfirmation = false;
		ConfirmationRemaining = 0.0f;
		LastDisplayedCountdown = -1;
		SetScreenSteppersEnabled(true);
		RefreshScreenLabels();
		return;
	}

	CaptureDisplaySnapshot(Settings);

	const FIntPoint Size = Resolutions[SelectedResolution];
	const EWindowMode::Type Mode = static_cast<EWindowMode::Type>(FMath::Clamp(SelectedWindowMode, 0, 2));
	const bool bVideoChanged = Settings->GetScreenResolution() != Size || Settings->GetFullscreenMode() != Mode;

	Settings->ConfirmVideoMode();
	Settings->SetScreenResolution(Size);
	Settings->SetFullscreenMode(Mode);
	if (SelectedQuality >= 0 && SelectedQuality <= 4)
	{
		Settings->SetOverallScalabilityLevel(SelectedQuality);
	}
	Settings->SetVSyncEnabled(bSelectedVSync);
	Settings->ApplySettings(false);

	if (bVideoChanged)
	{
		bAwaitingConfirmation = true;
		ConfirmationRemaining = 15.0f;
		LastDisplayedCountdown = 15;
		SetScreenSteppersEnabled(false);
		if (UButton* ApplyButton = Cast<UButton>(GetWidgetFromName(TEXT("Button_0"))))
		{
			ApplyButton->SetKeyboardFocus();
		}
		else
		{
			SetKeyboardFocus();
		}
	}
	else
	{
		Settings->SaveSettings();
		CaptureDisplaySnapshot(Settings);
	}

	RefreshScreenLabels();
}

void ULostWithinOptionsMenu::RevertDisplayChanges()
{
	if (!bAwaitingConfirmation)
	{
		return;
	}

	bAwaitingConfirmation = false;
	ConfirmationRemaining = 0.0f;
	LastDisplayedCountdown = -1;

	if (UGameUserSettings* Settings = UGameUserSettings::GetGameUserSettings())
	{
		Settings->RevertVideoMode();

		if (DisplaySnapshot.bValid)
		{
			Settings->SetFullscreenMode(DisplaySnapshot.WindowMode);
			if (DisplaySnapshot.Resolution.X > 0 && DisplaySnapshot.Resolution.Y > 0)
			{
				Settings->SetScreenResolution(DisplaySnapshot.Resolution);
			}
			Settings->SetVSyncEnabled(DisplaySnapshot.bVSync);

			if (DisplaySnapshot.OverallQuality >= 0 && DisplaySnapshot.OverallQuality <= 4)
			{
				Settings->SetOverallScalabilityLevel(DisplaySnapshot.OverallQuality);
			}
			else
			{
				Settings->SetViewDistanceQuality(DisplaySnapshot.ViewDistance);
				Settings->SetAntiAliasingQuality(DisplaySnapshot.AntiAliasing);
				Settings->SetShadowQuality(DisplaySnapshot.Shadow);
				Settings->SetGlobalIlluminationQuality(DisplaySnapshot.GlobalIllumination);
				Settings->SetReflectionQuality(DisplaySnapshot.Reflection);
				Settings->SetPostProcessingQuality(DisplaySnapshot.PostProcess);
				Settings->SetTextureQuality(DisplaySnapshot.Texture);
				Settings->SetVisualEffectQuality(DisplaySnapshot.Effects);
				Settings->SetFoliageQuality(DisplaySnapshot.Foliage);
				Settings->SetShadingQuality(DisplaySnapshot.Shading);
			}
		}

		Settings->ApplySettings(false);
		Settings->SaveSettings();

		SelectedWindowMode = FMath::Clamp(static_cast<int32>(Settings->GetFullscreenMode()), 0, 2);
		const int32 RevertedQuality = Settings->GetOverallScalabilityLevel();
		SelectedQuality = (RevertedQuality >= 0 && RevertedQuality <= 4) ? RevertedQuality : -1;
		bSelectedVSync = Settings->IsVSyncEnabled();

		FIntPoint RevertedRes = Settings->GetScreenResolution();
		if (RevertedRes.X <= 0 || RevertedRes.Y <= 0)
		{
			RevertedRes = DisplaySnapshot.Resolution;
		}
		if (RevertedRes.X > 0 && RevertedRes.Y > 0)
		{
			SelectedResolution = Resolutions.IndexOfByKey(RevertedRes);
			if (SelectedResolution == INDEX_NONE)
			{
				Resolutions.AddUnique(RevertedRes);
				Resolutions.Sort([](FIntPoint A, FIntPoint B) { return A.X == B.X ? A.Y < B.Y : A.X < B.X; });
				SelectedResolution = Resolutions.IndexOfByKey(RevertedRes);
			}
		}
		else if (!Resolutions.IsEmpty())
		{
			SelectedResolution = FMath::Clamp(SelectedResolution, 0, Resolutions.Num() - 1);
		}
		else
		{
			Resolutions = { FIntPoint(1280, 720) };
			SelectedResolution = 0;
		}
	}

	SetScreenSteppersEnabled(true);
	RefreshScreenLabels();
}

void ULostWithinOptionsMenu::NativeTick(const FGeometry& Geometry, float DeltaTime)
{
	Super::NativeTick(Geometry, DeltaTime);

	FlushAudioConfigIfDirty(/*bForceFlush=*/false);

	if (bAwaitingConfirmation)
	{
		ConfirmationRemaining -= DeltaTime;
		if (ConfirmationRemaining <= 0.0f)
		{
			RevertDisplayChanges();
		}
		else
		{
			const int32 CeilSec = FMath::Max(1, FMath::CeilToInt(ConfirmationRemaining));
			if (CeilSec != LastDisplayedCountdown)
			{
				LastDisplayedCountdown = CeilSec;
				if (UTextBlock* ApplyLabel = FindRequiredWidget<UTextBlock>(TEXT("TextBlock_0")))
				{
					ApplyLabel->SetText(FText::FromString(FString::Printf(TEXT("KEEP CHANGES (%d)"), CeilSec)));
				}
			}
		}
	}
}

void ULostWithinOptionsMenu::HandleBack()
{
	PlayLostWithinUIClickSound(this);
	RevertDisplayChanges();
	FlushAudioConfigIfDirty(/*bForceFlush=*/true);

	if (bReturnToPause)
	{
		if (ALostWithinPlayerController* PC = GetOwningPlayer<ALostWithinPlayerController>())
		{
			PC->ClosePauseOptions();
		}
		else
		{
			RemoveFromParent();
		}
		return;
	}

	UClass* MainMenuClass = LoadClass<UUserWidget>(nullptr, TEXT("/Game/UI/Menu/MainMenu.MainMenu_C"));
	if (!MainMenuClass)
	{
		UE_LOG(LogLostWithin, Warning, TEXT("ULostWithinOptionsMenu::HandleBack: could not load /Game/UI/Menu/MainMenu."));
		return;
	}

	UWorld* World = GetWorld();
	APlayerController* PC = GetOwningPlayer();
	RemoveFromParent();

	if (World)
	{
		TArray<UUserWidget*> ExistingMenus;
		UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World, ExistingMenus, MainMenuClass, /*TopLevelOnly=*/false);
		for (UUserWidget* ExistingMenu : ExistingMenus)
		{
			if (IsValid(ExistingMenu))
			{
				ExistingMenu->RemoveFromParent();
			}
		}
	}

	UUserWidget* Menu = PC
		? CreateWidget<UUserWidget>(PC, MainMenuClass)
		: (World ? CreateWidget<UUserWidget>(World, MainMenuClass) : nullptr);
	if (Menu)
	{
		Menu->AddToViewport();
		ApplyMenuUIOnlyInput(PC, Menu);
	}
}

FReply ULostWithinOptionsMenu::NativeOnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& MouseEvent)
{
	SetKeyboardFocus();
	return FReply::Handled();
}

FReply ULostWithinOptionsMenu::NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Key)
{
	if (!Key.IsRepeat() && Key.GetKey() == EKeys::Escape)
	{
		HandleBack();
		return FReply::Handled();
	}
	return Super::NativeOnPreviewKeyDown(Geometry, Key);
}
