// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "AudioMaterialSlate/AudioMaterialSlider.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/AudioComponent.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/MultiLineEditableTextBox.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameUserSettings.h"
#include "GameFramework/HUD.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "Interaction/EscapeDoorComponent.h"
#include "Interaction/ItemSpawnPoint.h"
#include "Inventory/InventoryComponent.h"
#include "Kismet/GameplayStatics.h"
#include "LostWithinCharacter.h"
#include "LostWithinGameInstance.h"
#include "LostWithinGameMode.h"
#include "LostWithinPlayerController.h"
#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"
#include "Sound/AudioSettings.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundMix.h"
#include "Styling/SlateTypes.h"
#include "UI/GameEndWidget.h"
#include "UI/InventoryWidget.h"
#include "UI/LostWithinMenu.h"
#include "UI/LostWithinModalWidget.h"
#include "UObject/UObjectIterator.h"
#include "Widgets/SWidget.h"

#if WITH_EDITOR
#include "Editor.h"
#include "GameMapsSettings.h"
#include "PlayInEditorDataTypes.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#endif

#include <limits>

#define LOSTWITHIN_MENU_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// Symbol names are unique to this translation unit for Unreal unity builds.
namespace LostWithinMenuTestPrivate
{
	constexpr const TCHAR* MenuTestAudioSection = TEXT("LostWithin.Audio");
	constexpr const TCHAR* MenuTestMusicKey = TEXT("MusicVolume");
	constexpr const TCHAR* MenuTestEffectsKey = TEXT("EffectsVolume");
	constexpr const TCHAR* MenuTestMainMenuClassPath = TEXT("/Game/UI/Menu/MainMenu.MainMenu_C");
	constexpr const TCHAR* MenuTestOptionsMenuClassPath = TEXT("/Game/UI/Menu/OptionsMenu.OptionsMenu_C");
	constexpr const TCHAR* MenuTestGameModeClassPath = TEXT("/Game/UI/Menu/MenuGamemode.MenuGamemode_C");
	constexpr const TCHAR* MenuTestFirstPersonPCClassPath = TEXT("/Game/FirstPerson/Blueprints/BP_FirstPersonPlayerController.BP_FirstPersonPlayerController_C");
	constexpr const TCHAR* MenuTestFirstPersonCharClassPath = TEXT("/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter.BP_FirstPersonCharacter_C");
	constexpr const TCHAR* MenuTestFirstPersonGMClassPath = TEXT("/Game/FirstPerson/Blueprints/BP_FirstPersonGameMode.BP_FirstPersonGameMode_C");

	struct FScopedMenuTestWorld
	{
		UWorld* World = nullptr;
		TArray<TPair<TWeakObjectPtr<APlayerController>, TWeakObjectPtr<ULocalPlayer>>> AttachedLocalPlayers;

		explicit FScopedMenuTestWorld(UClass* GameModeOverride = AGameModeBase::StaticClass(), bool bAutoBeginPlay = true)
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			if (World && GEngine)
			{
				FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
				Context.SetCurrentWorld(World);
				if (AWorldSettings* Settings = World->GetWorldSettings())
				{
					Settings->DefaultGameMode = GameModeOverride;
				}
				World->InitializeActorsForPlay(FURL());
				if (bAutoBeginPlay)
				{
					StartPlay();
				}
			}
		}

		void StartPlay() const
		{
			if (!World)
			{
				return;
			}
			World->BeginPlay();
			if (AWorldSettings* Settings = World->GetWorldSettings())
			{
				Settings->NotifyBeginPlay();
			}
		}

		ULocalPlayer* AttachLocalPlayer(APlayerController* PC)
		{
			if (!PC || !GEngine)
			{
				return nullptr;
			}
			ULocalPlayer* LP = NewObject<ULocalPlayer>(GEngine);
			if (LP)
			{
				PC->SetPlayer(LP);
				AttachedLocalPlayers.Emplace(PC, LP);
			}
			return LP;
		}

		~FScopedMenuTestWorld()
		{
			if (World)
			{
				World->EndPlay(EEndPlayReason::Quit);
			}
			for (const auto& Pair : AttachedLocalPlayers)
			{
				if (APlayerController* PC = Pair.Key.Get())
				{
					PC->Player = nullptr;
				}
				if (ULocalPlayer* LP = Pair.Value.Get())
				{
					LP->PlayerController = nullptr;
					LP->MarkAsGarbage();
				}
			}
			AttachedLocalPlayers.Reset();

			if (World)
			{
				World->DestroyWorld(false);
				if (GEngine)
				{
					GEngine->DestroyWorldContext(World);
				}
				World = nullptr;
			}
			CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
		}
	};

	struct FScopedMenuSettingsGuard
	{
		bool bHasSettings = false;
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

		bool bHadMusicKey = false;
		float SavedMusicVolume = 1.0f;
		bool bHadEffectsKey = false;
		float SavedEffectsVolume = 1.0f;

		FScopedMenuSettingsGuard()
		{
			if (UGameUserSettings* Settings = UGameUserSettings::GetGameUserSettings())
			{
				bHasSettings = true;
				Resolution = Settings->GetScreenResolution();
				WindowMode = Settings->GetFullscreenMode();
				OverallQuality = Settings->GetOverallScalabilityLevel();
				ViewDistance = Settings->GetViewDistanceQuality();
				AntiAliasing = Settings->GetAntiAliasingQuality();
				Shadow = Settings->GetShadowQuality();
				GlobalIllumination = Settings->GetGlobalIlluminationQuality();
				Reflection = Settings->GetReflectionQuality();
				PostProcess = Settings->GetPostProcessingQuality();
				Texture = Settings->GetTextureQuality();
				Effects = Settings->GetVisualEffectQuality();
				Foliage = Settings->GetFoliageQuality();
				Shading = Settings->GetShadingQuality();
				bVSync = Settings->IsVSyncEnabled();
			}
			if (GConfig)
			{
				bHadMusicKey = GConfig->GetFloat(MenuTestAudioSection, MenuTestMusicKey, SavedMusicVolume, GGameUserSettingsIni);
				bHadEffectsKey = GConfig->GetFloat(MenuTestAudioSection, MenuTestEffectsKey, SavedEffectsVolume, GGameUserSettingsIni);
			}
		}

		~FScopedMenuSettingsGuard()
		{
			if (bHasSettings)
			{
				if (UGameUserSettings* Settings = UGameUserSettings::GetGameUserSettings())
				{
					Settings->RevertVideoMode();
					Settings->SetFullscreenMode(WindowMode);
					if (Resolution.X > 0 && Resolution.Y > 0)
					{
						Settings->SetScreenResolution(Resolution);
					}
					Settings->SetVSyncEnabled(bVSync);
					if (OverallQuality >= 0 && OverallQuality <= 4)
					{
						Settings->SetOverallScalabilityLevel(OverallQuality);
					}
					else
					{
						Settings->SetViewDistanceQuality(ViewDistance);
						Settings->SetAntiAliasingQuality(AntiAliasing);
						Settings->SetShadowQuality(Shadow);
						Settings->SetGlobalIlluminationQuality(GlobalIllumination);
						Settings->SetReflectionQuality(Reflection);
						Settings->SetPostProcessingQuality(PostProcess);
						Settings->SetTextureQuality(Texture);
						Settings->SetVisualEffectQuality(Effects);
						Settings->SetFoliageQuality(Foliage);
						Settings->SetShadingQuality(Shading);
					}
					Settings->ConfirmVideoMode();
					Settings->ApplySettings(false);
					Settings->SaveSettings();
				}
			}
			if (GConfig)
			{
				if (bHadMusicKey)
				{
					GConfig->SetFloat(MenuTestAudioSection, MenuTestMusicKey, SavedMusicVolume, GGameUserSettingsIni);
				}
				else
				{
					GConfig->RemoveKey(MenuTestAudioSection, MenuTestMusicKey, GGameUserSettingsIni);
				}
				if (bHadEffectsKey)
				{
					GConfig->SetFloat(MenuTestAudioSection, MenuTestEffectsKey, SavedEffectsVolume, GGameUserSettingsIni);
				}
				else
				{
					GConfig->RemoveKey(MenuTestAudioSection, MenuTestEffectsKey, GGameUserSettingsIni);
				}
				GConfig->Flush(false, GGameUserSettingsIni);
			}
		}
	};

	UTextBlock* GetButtonLabelText(const UUserWidget* Parent, FName ButtonName)
	{
		const UButton* Button = Parent ? Cast<UButton>(Parent->GetWidgetFromName(ButtonName)) : nullptr;
		return Button ? Cast<UTextBlock>(Button->GetContent()) : nullptr;
	}

	template <typename TWidget>
	TWidget* FindFirstViewportWidget(UWorld* World)
	{
		for (TObjectIterator<TWidget> It; It; ++It)
		{
			TWidget* Candidate = *It;
			if (IsValid(Candidate) && !Candidate->HasAnyFlags(RF_ClassDefaultObject)
				&& Candidate->GetWorld() == World && Candidate->IsInViewport())
			{
				return Candidate;
			}
		}
		return nullptr;
	}

	template <typename TWidget>
	int32 CountViewportWidgets(UWorld* World)
	{
		int32 Count = 0;
		for (TObjectIterator<TWidget> It; It; ++It)
		{
			TWidget* Candidate = *It;
			if (IsValid(Candidate) && !Candidate->HasAnyFlags(RF_ClassDefaultObject)
				&& Candidate->GetWorld() == World && Candidate->IsInViewport())
			{
				++Count;
			}
		}
		return Count;
	}

	void PopulateMinimalRunSpawnPoints(UWorld* World)
	{
		if (!World)
		{
			return;
		}
		for (int32 Index = 0; Index < 3; ++Index)
		{
			if (AItemSpawnPoint* KeyPoint = World->SpawnActor<AItemSpawnPoint>(
				AItemSpawnPoint::StaticClass(), FVector(Index * 200.0f, 0.0f, 50.0f), FRotator::ZeroRotator))
			{
				KeyPoint->Kind = ELostWithinSpawnPointKind::Key;
			}
		}
		for (int32 Index = 0; Index < 5; ++Index)
		{
			if (AItemSpawnPoint* NotePoint = World->SpawnActor<AItemSpawnPoint>(
				AItemSpawnPoint::StaticClass(), FVector(Index * 200.0f, 400.0f, 50.0f), FRotator::ZeroRotator))
			{
				NotePoint->Kind = ELostWithinSpawnPointKind::Note;
			}
		}
	}

#if WITH_EDITOR
	class FObserveMenuFlow final : public IAutomationLatentCommand
	{
	public:
		explicit FObserveMenuFlow(FAutomationTestBase* InTest)
			: Test(InTest), StartedAt(FPlatformTime::Seconds()) {}

		virtual bool Update() override
		{
			if (FPlatformTime::Seconds() - StartedAt > 45.0)
			{
				Test->AddError(TEXT("PIE menu flow timed out before reaching Lvl_FirstPerson gameplay."));
				return true;
			}

			UWorld* PlayWorld = GEditor ? GEditor->PlayWorld : nullptr;
			if (!PlayWorld || !PlayWorld->HasBegunPlay())
			{
				return false;
			}

			const FString WorldName = PlayWorld->GetOutermost()->GetName();

			if (Step == EMenuFlowStep::WaitForMainMenu)
			{
				if (!WorldName.Contains(TEXT("Lvl_MainMenu")))
				{
					return false;
				}
				APlayerController* PC = UGameplayStatics::GetPlayerController(PlayWorld, 0);
				ULostWithinMainMenu* MainMenu = FindFirstViewportWidget<ULostWithinMainMenu>(PlayWorld);
				ALostWithinMenuGameMode* MenuGM = Cast<ALostWithinMenuGameMode>(PlayWorld->GetAuthGameMode());
				if (!PC || !MainMenu || !MenuGM)
				{
					return false;
				}

				MenuMusic = MenuGM->GetMenuMusicComponent();
				Test->TestNotNull(TEXT("PIE MenuMusicComponent spawned"), MenuMusic.Get());
				if (MenuMusic.IsValid())
				{
					Test->TestTrue(TEXT("PIE MenuMusicComponent is playing"), MenuMusic->IsPlaying());
					Test->TestTrue(TEXT("PIE MenuMusicComponent is flagged bIsUISound"), MenuMusic->bIsUISound);
				}
				Test->TestTrue(TEXT("PIE MainMenu is visible in viewport"), MainMenu->IsInViewport());
				Test->TestTrue(TEXT("PIE MainMenu enables mouse cursor"), PC->ShouldShowMouseCursor());

				// Open Options and verify tab switching.
				MainMenu->HandleOptionsClicked();
				Test->TestFalse(TEXT("PIE MainMenu leaves viewport when Options opens"), MainMenu->IsInViewport());

				ULostWithinOptionsMenu* Options = FindFirstViewportWidget<ULostWithinOptionsMenu>(PlayWorld);
				if (!Test->TestNotNull(TEXT("PIE OptionsMenu added to viewport"), Options))
				{
					return true;
				}
				Test->TestEqual(TEXT("PIE OptionsMenu starts on How To Play tab 0"), Options->GetActiveTabIndex(), 0);
				Options->ShowTab(1);
				Test->TestEqual(TEXT("PIE OptionsMenu switches to Screen tab 1"), Options->GetActiveTabIndex(), 1);
				Options->ShowTab(2);
				Test->TestEqual(TEXT("PIE OptionsMenu switches to Sound tab 2"), Options->GetActiveTabIndex(), 2);

				// Return to MainMenu via HandleBack and check deduplication.
				Options->HandleBack();
				Test->TestFalse(TEXT("PIE OptionsMenu leaves viewport after HandleBack"), Options->IsInViewport());

				ULostWithinMainMenu* ReturnedMenu = FindFirstViewportWidget<ULostWithinMainMenu>(PlayWorld);
				Test->TestEqual(TEXT("PIE HandleBack leaves exactly one MainMenu in viewport"),
					CountViewportWidgets<ULostWithinMainMenu>(PlayWorld), 1);
				if (!Test->TestNotNull(TEXT("PIE MainMenu restored after HandleBack"), ReturnedMenu))
				{
					return true;
				}
				Test->TestTrue(TEXT("PIE cursor remains shown after returning to MainMenu"), PC->ShouldShowMouseCursor());

				// Invoke Play to stop menu music and travel to Lvl_FirstPerson.
				ReturnedMenu->HandlePlayClicked();
				Test->TestFalse(TEXT("PIE menu music stops immediately when Play is clicked"),
					MenuMusic.IsValid() && MenuMusic->IsPlaying());

				Step = EMenuFlowStep::WaitForFirstPerson;
				return false;
			}

			if (Step == EMenuFlowStep::WaitForFirstPerson)
			{
				if (!WorldName.Contains(TEXT("Lvl_FirstPerson")))
				{
					return false;
				}
				ALostWithinPlayerController* PC = Cast<ALostWithinPlayerController>(UGameplayStatics::GetPlayerController(PlayWorld, 0));
				ALostWithinCharacter* Char = Cast<ALostWithinCharacter>(PC ? PC->GetPawn() : nullptr);
				if (!PC || !Char)
				{
					return false;
				}

				Test->TestTrue(TEXT("PIE FirstPerson uses ALostWithinGameMode"),
					PlayWorld->GetAuthGameMode() && PlayWorld->GetAuthGameMode()->IsA<ALostWithinGameMode>());
				Test->TestFalse(TEXT("PIE menu music remains stopped in Lvl_FirstPerson"),
					MenuMusic.IsValid() && MenuMusic->IsPlaying());
				Test->TestFalse(TEXT("PIE cursor is hidden in Lvl_FirstPerson gameplay"), PC->ShouldShowMouseCursor());
				Test->TestEqual(TEXT("PIE MainMenu is gone in Lvl_FirstPerson"),
					CountViewportWidgets<ULostWithinMainMenu>(PlayWorld), 0);
				Test->TestEqual(TEXT("PIE OptionsMenu is gone in Lvl_FirstPerson"),
					CountViewportWidgets<ULostWithinOptionsMenu>(PlayWorld), 0);
				return true;
			}

			return true;
		}

	private:
		enum class EMenuFlowStep : uint8
		{
			WaitForMainMenu,
			WaitForFirstPerson
		};

		FAutomationTestBase* Test = nullptr;
		double StartedAt = 0.0;
		EMenuFlowStep Step = EMenuFlowStep::WaitForMainMenu;
		TWeakObjectPtr<UAudioComponent> MenuMusic;
	};
#endif
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLostWithinStartupAndAssetParentsTest,
	"LostWithin.UI.StartupAndAssetParents",
	LOSTWITHIN_MENU_TEST_FLAGS)

bool FLostWithinStartupAndAssetParentsTest::RunTest(const FString& Parameters)
{
#if WITH_EDITOR
	const FString DefaultMap = UGameMapsSettings::GetGameDefaultMap();
	TestTrue(FString::Printf(TEXT("GameDefaultMap is Lvl_MainMenu (was '%s')"), *DefaultMap),
		DefaultMap.StartsWith(TEXT("/Game/FirstPerson/Lvl_MainMenu")));

	const UGameMapsSettings* MapsSettings = GetDefault<UGameMapsSettings>();
	TestNotNull(TEXT("GameMapsSettings CDO available"), MapsSettings);
	if (MapsSettings)
	{
		TestEqual(TEXT("GameInstanceClass resolves to ULostWithinGameInstance"),
			MapsSettings->GameInstanceClass.TryLoadClass<UGameInstance>(),
			static_cast<UClass*>(ULostWithinGameInstance::StaticClass()));
	}
#endif

	const UAudioSettings* AudioSettings = GetDefault<UAudioSettings>();
	if (TestNotNull(TEXT("AudioSettings CDO available"), AudioSettings))
	{
		TestEqual(TEXT("DefaultSoundClassName points to SC_Sfx"),
			AudioSettings->DefaultSoundClassName.ToString(), FString(TEXT("/Game/Audio/Mix/SC_Sfx.SC_Sfx")));
		TestEqual(TEXT("DefaultBaseSoundMix points to SM_Options"),
			AudioSettings->DefaultBaseSoundMix.ToString(), FString(TEXT("/Game/Audio/Mix/SM_Options.SM_Options")));
	}
	TestNotNull(TEXT("SC_Sfx loads"), LoadObject<USoundClass>(nullptr, TEXT("/Game/Audio/Mix/SC_Sfx.SC_Sfx")));
	TestNotNull(TEXT("SC_Music loads"), LoadObject<USoundClass>(nullptr, TEXT("/Game/Audio/Mix/SC_Music.SC_Music")));
	TestNotNull(TEXT("SM_Options loads"), LoadObject<USoundMix>(nullptr, TEXT("/Game/Audio/Mix/SM_Options.SM_Options")));
	TestNotNull(TEXT("UI click sound loads"), LoadObject<USoundBase>(nullptr, TEXT("/Game/Audio/SFX/UI/click.click")));

	const ALostWithinMenuGameMode* MenuDefaults = GetDefault<ALostWithinMenuGameMode>();
	if (TestNotNull(TEXT("ALostWithinMenuGameMode CDO available"), MenuDefaults))
	{
		TestNull(TEXT("ALostWithinMenuGameMode has null DefaultPawnClass"), MenuDefaults->DefaultPawnClass.Get());
		TestNull(TEXT("ALostWithinMenuGameMode has null HUDClass"), MenuDefaults->HUDClass.Get());
	}

	UClass* MenuGMClass = LoadClass<AGameModeBase>(nullptr, LostWithinMenuTestPrivate::MenuTestGameModeClassPath);
	if (TestNotNull(TEXT("MenuGamemode Blueprint loads"), MenuGMClass))
	{
		TestTrue(TEXT("MenuGamemode inherits ALostWithinMenuGameMode"),
			MenuGMClass->IsChildOf(ALostWithinMenuGameMode::StaticClass()));
		const AGameModeBase* GMDefaults = MenuGMClass->GetDefaultObject<AGameModeBase>();
		TestNull(TEXT("MenuGamemode Blueprint keeps null DefaultPawnClass"), GMDefaults->DefaultPawnClass.Get());
		TestNull(TEXT("MenuGamemode Blueprint keeps null HUDClass"), GMDefaults->HUDClass.Get());
	}

	UClass* MainMenuClass = LoadClass<UUserWidget>(nullptr, LostWithinMenuTestPrivate::MenuTestMainMenuClassPath);
	if (TestNotNull(TEXT("MainMenu Blueprint loads"), MainMenuClass))
	{
		TestTrue(TEXT("MainMenu inherits ULostWithinMainMenu"),
			MainMenuClass->IsChildOf(ULostWithinMainMenu::StaticClass()));
	}

	UClass* OptionsMenuClass = LoadClass<UUserWidget>(nullptr, LostWithinMenuTestPrivate::MenuTestOptionsMenuClassPath);
	if (TestNotNull(TEXT("OptionsMenu Blueprint loads"), OptionsMenuClass))
	{
		TestTrue(TEXT("OptionsMenu inherits ULostWithinOptionsMenu"),
			OptionsMenuClass->IsChildOf(ULostWithinOptionsMenu::StaticClass()));
	}

	UFont* CinzelBold = LoadObject<UFont>(nullptr, TEXT("/Game/UI/Fonts/Cinzel-Bold_Font.Cinzel-Bold_Font"));
	UFont* CinzelRegular = LoadObject<UFont>(nullptr, TEXT("/Game/UI/Fonts/Cinzel-Regular_Font.Cinzel-Regular_Font"));
	TestNotNull(TEXT("Cinzel-Bold_Font loads"), CinzelBold);
	TestNotNull(TEXT("Cinzel-Regular_Font loads"), CinzelRegular);

	const FSlateFontInfo BoldFontInfo = MakeLostWithinCinzelFont(true, 26);
	TestEqual(TEXT("MakeLostWithinCinzelFont(true) uses Cinzel-Bold_Font"),
		BoldFontInfo.FontObject.Get(), static_cast<const UObject*>(CinzelBold));
	TestEqual(TEXT("MakeLostWithinCinzelFont(true) uses Default typeface"),
		BoldFontInfo.TypefaceFontName, FName(TEXT("Default")));
	TestEqual(TEXT("MakeLostWithinCinzelFont(true) preserves size"),
		FMath::RoundToInt(BoldFontInfo.Size), 26);

	const FSlateFontInfo RegularFontInfo = MakeLostWithinCinzelFont(false, 18);
	TestEqual(TEXT("MakeLostWithinCinzelFont(false) uses Cinzel-Regular_Font"),
		RegularFontInfo.FontObject.Get(), static_cast<const UObject*>(CinzelRegular));
	TestEqual(TEXT("MakeLostWithinCinzelFont(false) uses Default typeface"),
		RegularFontInfo.TypefaceFontName, FName(TEXT("Default")));

	const FButtonStyle DefaultHorrorStyle = MakeLostWithinHorrorButtonStyle(false);
	const FButtonStyle AccentHorrorStyle = MakeLostWithinHorrorButtonStyle(true);
	TestEqual(TEXT("Horror normal brush uses RoundedBox"),
		DefaultHorrorStyle.Normal.DrawAs.GetValue(), ESlateBrushDrawType::RoundedBox);
	TestEqual(TEXT("Horror hovered brush uses RoundedBox"),
		DefaultHorrorStyle.Hovered.DrawAs.GetValue(), ESlateBrushDrawType::RoundedBox);
	TestEqual(TEXT("Horror pressed brush uses RoundedBox"),
		DefaultHorrorStyle.Pressed.DrawAs.GetValue(), ESlateBrushDrawType::RoundedBox);
	TestEqual(TEXT("Horror disabled brush uses RoundedBox"),
		DefaultHorrorStyle.Disabled.DrawAs.GetValue(), ESlateBrushDrawType::RoundedBox);
	TestTrue(TEXT("Horror button style has positive outline width"),
		DefaultHorrorStyle.Normal.OutlineSettings.Width > 0.0f);
	TestFalse(TEXT("Horror button hovered tint differs from normal"),
		DefaultHorrorStyle.Normal.TintColor.GetSpecifiedColor() == DefaultHorrorStyle.Hovered.TintColor.GetSpecifiedColor());
	TestFalse(TEXT("Primary accent horror button differs from standard"),
		AccentHorrorStyle.Normal.TintColor.GetSpecifiedColor() == DefaultHorrorStyle.Normal.TintColor.GetSpecifiedColor());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLostWithinOptionsWidgetTreeAndTabsTest,
	"LostWithin.UI.OptionsWidgetTreeAndTabs",
	LOSTWITHIN_MENU_TEST_FLAGS)

bool FLostWithinOptionsWidgetTreeAndTabsTest::RunTest(const FString& Parameters)
{
	LostWithinMenuTestPrivate::FScopedMenuSettingsGuard SettingsGuard;
	LostWithinMenuTestPrivate::FScopedMenuTestWorld TestWorld;
	if (!TestNotNull(TEXT("Test world created"), TestWorld.World))
	{
		return false;
	}

	UClass* OptionsClass = LoadClass<ULostWithinOptionsMenu>(nullptr, LostWithinMenuTestPrivate::MenuTestOptionsMenuClassPath);
	if (!TestNotNull(TEXT("OptionsMenu class loaded"), OptionsClass))
	{
		return false;
	}

	ULostWithinOptionsMenu* Options = CreateWidget<ULostWithinOptionsMenu>(TestWorld.World, OptionsClass);
	if (!TestNotNull(TEXT("OptionsMenu widget constructed"), Options))
	{
		return false;
	}
	Options->TakeWidget();

	TestNotNull(TEXT("MusicSlider exists"), Cast<UAudioMaterialSlider>(Options->GetWidgetFromName(TEXT("MusicSlider"))));
	TestNotNull(TEXT("SfxSlider exists"), Cast<UAudioMaterialSlider>(Options->GetWidgetFromName(TEXT("SfxSlider"))));

	for (const TCHAR* ButtonName : {
		TEXT("Button_0"), TEXT("Button_1"), TEXT("Button_3"), TEXT("Button_4"), TEXT("Button_5"),
		TEXT("windowmode-"), TEXT("windowmode+"), TEXT("resolution-"), TEXT("resolution+"),
		TEXT("graphics-"), TEXT("graphics+"), TEXT("vsync-"), TEXT("vsync+") })
	{
		TestNotNull(FString::Printf(TEXT("Button %s exists"), ButtonName),
			Cast<UButton>(Options->GetWidgetFromName(ButtonName)));
	}

	for (const TCHAR* LabelName : {
		TEXT("TextBlock_124"), TEXT("TextBlock_2"), TEXT("TextBlock_6"), TEXT("TextBlock_10"), TEXT("TextBlock_0") })
	{
		TestNotNull(FString::Printf(TEXT("TextBlock %s exists"), LabelName),
			Cast<UTextBlock>(Options->GetWidgetFromName(LabelName)));
	}

	UTextBlock* ScreenTabLabel = Cast<UTextBlock>(Options->GetWidgetFromName(TEXT("TextBlock_452")));
	if (TestNotNull(TEXT("Screen tab label TextBlock_452 exists"), ScreenTabLabel))
	{
		const FString ScreenText = ScreenTabLabel->GetText().ToString();
		TestFalse(TEXT("Screen tab label has no trailing space"), ScreenText.EndsWith(TEXT(" ")));
		TestTrue(TEXT("Screen tab label reads SCREEN"),
			ScreenText.TrimStartAndEnd().Equals(TEXT("SCREEN"), ESearchCase::IgnoreCase));
	}

	UMultiLineEditableTextBox* Guide = Cast<UMultiLineEditableTextBox>(Options->GetWidgetFromName(TEXT("MultiLineEditableTextBox_2")));
	if (TestNotNull(TEXT("Survival guide MultiLineEditableTextBox_2 exists"), Guide))
	{
		TestTrue(TEXT("Survival guide is read-only"), Guide->GetIsReadOnly());
		TestFalse(TEXT("Survival guide disables context menu"), Guide->AllowContextMenu);
		const FString GuideStr = Guide->GetText().ToString();
		TestTrue(TEXT("Survival guide mentions BoneDog"), GuideStr.Contains(TEXT("BoneDog")));
		TestTrue(TEXT("Survival guide mentions BoneDog flashlight attraction"),
			GuideStr.Contains(TEXT("Flashlight draws BoneDog"), ESearchCase::IgnoreCase)
			|| (GuideStr.Contains(TEXT("BoneDog")) && GuideStr.Contains(TEXT("flashlight"), ESearchCase::IgnoreCase)));
	}

	UWidget* HowToPlayBox = Options->GetWidgetFromName(TEXT("VB_HowToPlay"));
	UWidget* ScreenBox = Options->GetWidgetFromName(TEXT("VB_ScreenSettings"));
	UWidget* SoundBox = Options->GetWidgetFromName(TEXT("VB_SoundSettings"));
	if (!TestNotNull(TEXT("VB_HowToPlay exists"), HowToPlayBox)
		|| !TestNotNull(TEXT("VB_ScreenSettings exists"), ScreenBox)
		|| !TestNotNull(TEXT("VB_SoundSettings exists"), SoundBox))
	{
		return false;
	}

	TestEqual(TEXT("OptionsMenu opens on HowToPlay tab 0"), Options->GetActiveTabIndex(), 0);
	TestEqual(TEXT("HowToPlay panel visible initially"), HowToPlayBox->GetVisibility(), ESlateVisibility::SelfHitTestInvisible);
	TestEqual(TEXT("Screen panel collapsed initially"), ScreenBox->GetVisibility(), ESlateVisibility::Collapsed);
	TestEqual(TEXT("Sound panel collapsed initially"), SoundBox->GetVisibility(), ESlateVisibility::Collapsed);

	Options->ShowTab(1);
	TestEqual(TEXT("ActiveTabIndex updates to 1"), Options->GetActiveTabIndex(), 1);
	TestEqual(TEXT("HowToPlay collapsed on Screen tab"), HowToPlayBox->GetVisibility(), ESlateVisibility::Collapsed);
	TestEqual(TEXT("Screen visible on Screen tab"), ScreenBox->GetVisibility(), ESlateVisibility::SelfHitTestInvisible);
	TestEqual(TEXT("Sound collapsed on Screen tab"), SoundBox->GetVisibility(), ESlateVisibility::Collapsed);

	Options->ShowTab(2);
	TestEqual(TEXT("ActiveTabIndex updates to 2"), Options->GetActiveTabIndex(), 2);
	TestEqual(TEXT("HowToPlay collapsed on Sound tab"), HowToPlayBox->GetVisibility(), ESlateVisibility::Collapsed);
	TestEqual(TEXT("Screen collapsed on Sound tab"), ScreenBox->GetVisibility(), ESlateVisibility::Collapsed);
	TestEqual(TEXT("Sound visible on Sound tab"), SoundBox->GetVisibility(), ESlateVisibility::SelfHitTestInvisible);

	Options->ShowTab(0);
	TestEqual(TEXT("ActiveTabIndex returns to 0"), Options->GetActiveTabIndex(), 0);
	TestEqual(TEXT("HowToPlay visible again"), HowToPlayBox->GetVisibility(), ESlateVisibility::SelfHitTestInvisible);

	if (UButton* ScreenBtn = Cast<UButton>(Options->GetWidgetFromName(TEXT("Button_4"))))
	{
		ScreenBtn->OnClicked.Broadcast();
		TestEqual(TEXT("Clicking Screen tab switches to tab 1"), Options->GetActiveTabIndex(), 1);
	}
	if (UButton* SoundBtn = Cast<UButton>(Options->GetWidgetFromName(TEXT("Button_5"))))
	{
		SoundBtn->OnClicked.Broadcast();
		TestEqual(TEXT("Clicking Sound tab switches to tab 2"), Options->GetActiveTabIndex(), 2);
	}
	if (UButton* GuideBtn = Cast<UButton>(Options->GetWidgetFromName(TEXT("Button_3"))))
	{
		GuideBtn->OnClicked.Broadcast();
		TestEqual(TEXT("Clicking HowToPlay tab switches to tab 0"), Options->GetActiveTabIndex(), 0);
	}

	Options->ShowTab(99);
	TestEqual(TEXT("Out-of-range high tab clamps to 2"), Options->GetActiveTabIndex(), 2);
	Options->ShowTab(-5);
	TestEqual(TEXT("Out-of-range low tab clamps to 0"), Options->GetActiveTabIndex(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLostWithinOptionsSettingsAndRevertTest,
	"LostWithin.UI.OptionsSettingsAndRevert",
	LOSTWITHIN_MENU_TEST_FLAGS)

bool FLostWithinOptionsSettingsAndRevertTest::RunTest(const FString& Parameters)
{
	LostWithinMenuTestPrivate::FScopedMenuSettingsGuard SettingsGuard;
	LostWithinMenuTestPrivate::FScopedMenuTestWorld TestWorld;
	if (!TestNotNull(TEXT("Test world created"), TestWorld.World))
	{
		return false;
	}

	UGameUserSettings* Settings = UGameUserSettings::GetGameUserSettings();
	UClass* OptionsClass = LoadClass<ULostWithinOptionsMenu>(nullptr, LostWithinMenuTestPrivate::MenuTestOptionsMenuClassPath);
	if (!TestNotNull(TEXT("UGameUserSettings available"), Settings) || !TestNotNull(TEXT("OptionsMenu class loaded"), OptionsClass))
	{
		return false;
	}

	// 1. Cinematic (4) quality preserved without downgrade to Epic (3).
	Settings->SetFullscreenMode(EWindowMode::Windowed);
	Settings->SetScreenResolution(FIntPoint(1280, 720));
	Settings->SetOverallScalabilityLevel(4);
	Settings->SetVSyncEnabled(false);
	Settings->ConfirmVideoMode();

	ULostWithinOptionsMenu* CinematicOptions = CreateWidget<ULostWithinOptionsMenu>(TestWorld.World, OptionsClass);
	if (!TestNotNull(TEXT("Cinematic OptionsMenu created"), CinematicOptions))
	{
		return false;
	}
	CinematicOptions->TakeWidget();
	TestEqual(TEXT("SelectedQuality reads Cinematic (4)"), CinematicOptions->GetSelectedQuality(), 4);
	if (UTextBlock* QualityLabel = Cast<UTextBlock>(CinematicOptions->GetWidgetFromName(TEXT("TextBlock_6"))))
	{
		TestEqual(TEXT("Quality label shows Cinematic"), QualityLabel->GetText().ToString(), FString(TEXT("Cinematic")));
	}
	CinematicOptions->ApplyScreenSettings();
	TestFalse(TEXT("Apply without video change does not start countdown"), CinematicOptions->IsDisplayConfirmationPending());
	TestEqual(TEXT("Cinematic quality (4) stays 4 after Apply"), Settings->GetOverallScalabilityLevel(), 4);

	// 2. Custom (-1) quality preserved without overwrite to Low (0).
	Settings->SetOverallScalabilityLevel(2);
	Settings->SetShadowQuality(0);
	Settings->SetViewDistanceQuality(3);
	TestEqual(TEXT("Mismatched scalability groups yield Custom (-1)"), Settings->GetOverallScalabilityLevel(), -1);

	ULostWithinOptionsMenu* CustomOptions = CreateWidget<ULostWithinOptionsMenu>(TestWorld.World, OptionsClass);
	if (!TestNotNull(TEXT("Custom OptionsMenu created"), CustomOptions))
	{
		return false;
	}
	CustomOptions->TakeWidget();
	TestEqual(TEXT("SelectedQuality reads Custom (-1)"), CustomOptions->GetSelectedQuality(), -1);
	if (UTextBlock* QualityLabel = Cast<UTextBlock>(CustomOptions->GetWidgetFromName(TEXT("TextBlock_6"))))
	{
		TestEqual(TEXT("Quality label shows Custom"), QualityLabel->GetText().ToString(), FString(TEXT("Custom")));
	}
	CustomOptions->ApplyScreenSettings();
	TestEqual(TEXT("Custom quality (-1) preserved after Apply"), Settings->GetOverallScalabilityLevel(), -1);
	TestEqual(TEXT("Custom ShadowQuality preserved"), Settings->GetShadowQuality(), 0);
	TestEqual(TEXT("Custom ViewDistanceQuality preserved"), Settings->GetViewDistanceQuality(), 3);

	CustomOptions->GraphicsPrevious();
	TestEqual(TEXT("GraphicsPrevious from Custom (-1) selects Cinematic (4)"), CustomOptions->GetSelectedQuality(), 4);
	CustomOptions->GraphicsNext();
	TestEqual(TEXT("GraphicsNext wraps 4 -> Low (0)"), CustomOptions->GetSelectedQuality(), 0);

	// 3. 15s timeout full revert (Video + Quality + VSync), 1-Hz countdown check, and INDEX_NONE resolution fallback.
	Settings->SetFullscreenMode(EWindowMode::Windowed);
	Settings->SetScreenResolution(FIntPoint(1280, 720));
	Settings->SetOverallScalabilityLevel(1);
	Settings->SetVSyncEnabled(false);
	Settings->ConfirmVideoMode();

	ULostWithinOptionsMenu* RevertOptions = CreateWidget<ULostWithinOptionsMenu>(TestWorld.World, OptionsClass);
	if (!TestNotNull(TEXT("Revert OptionsMenu created"), RevertOptions))
	{
		return false;
	}
	TSharedRef<SWidget> RevertSlate = RevertOptions->TakeWidget();

	// Mutate confirmed resolution AFTER construction so it is not in Resolutions (forces INDEX_NONE fallback on revert).
	const FIntPoint UnlistedRes(1111, 666);
	Settings->SetScreenResolution(UnlistedRes);
	Settings->ConfirmVideoMode();

	RevertOptions->WindowNext();
	RevertOptions->GraphicsNext(); // 1 (Medium) -> 2 (High)
	RevertOptions->ToggleVSync();  // false -> true
	RevertOptions->ApplyScreenSettings();

	TestTrue(TEXT("Video change starts 15s confirmation countdown"), RevertOptions->IsDisplayConfirmationPending());
	TestEqual(TEXT("Initial countdown displays 15"), RevertOptions->GetLastDisplayedCountdown(), 15);
	UTextBlock* ApplyLabel = Cast<UTextBlock>(RevertOptions->GetWidgetFromName(TEXT("TextBlock_0")));
	if (TestNotNull(TEXT("Apply button label TextBlock_0 exists"), ApplyLabel))
	{
		TestEqual(TEXT("Apply label reads KEEP CHANGES (15)"), ApplyLabel->GetText().ToString(), FString(TEXT("KEEP CHANGES (15)")));
	}
	for (const TCHAR* StepperName : {
		TEXT("windowmode-"), TEXT("windowmode+"), TEXT("resolution-"), TEXT("resolution+"),
		TEXT("graphics-"), TEXT("graphics+"), TEXT("vsync-"), TEXT("vsync+") })
	{
		if (UWidget* Stepper = RevertOptions->GetWidgetFromName(StepperName))
		{
			TestFalse(FString::Printf(TEXT("Stepper %s disabled during confirmation"), StepperName), Stepper->GetIsEnabled());
		}
	}

	// Step 1 second -> 14; then verify 1-Hz tick does not rewrite TextBlock_0 inside the same second.
	RevertSlate->Tick(FGeometry(), 1.0, 1.0f);
	TestEqual(TEXT("Countdown decrements to 14 after 1.0s"), RevertOptions->GetLastDisplayedCountdown(), 14);
	if (ApplyLabel)
	{
		TestEqual(TEXT("Apply label updates to KEEP CHANGES (14)"), ApplyLabel->GetText().ToString(), FString(TEXT("KEEP CHANGES (14)")));
		ApplyLabel->SetText(FText::FromString(TEXT("SENTINEL_SAME_SECOND")));
		RevertSlate->Tick(FGeometry(), 1.2, 0.2f);
		TestEqual(TEXT("Same-second sub-tick preserves label without per-frame rewrite"),
			ApplyLabel->GetText().ToString(), FString(TEXT("SENTINEL_SAME_SECOND")));
		RevertSlate->Tick(FGeometry(), 2.1, 0.9f);
		TestEqual(TEXT("Crossing second boundary updates countdown to 13"), RevertOptions->GetLastDisplayedCountdown(), 13);
		TestEqual(TEXT("Crossing second boundary updates label to KEEP CHANGES (13)"),
			ApplyLabel->GetText().ToString(), FString(TEXT("KEEP CHANGES (13)")));
	}

	// Expire remaining confirmation window -> must revert Video + Quality + VSync + unlisted resolution.
	RevertSlate->Tick(FGeometry(), 16.0, 13.5f);
	TestFalse(TEXT("15s timeout ends confirmation"), RevertOptions->IsDisplayConfirmationPending());
	TestEqual(TEXT("Timeout resets LastDisplayedCountdown to -1"), RevertOptions->GetLastDisplayedCountdown(), -1);
	if (ApplyLabel)
	{
		TestEqual(TEXT("Apply label returns to APPLY"), ApplyLabel->GetText().ToString(), FString(TEXT("APPLY")));
	}
	TestEqual(TEXT("WindowMode reverted to Windowed"), Settings->GetFullscreenMode(), EWindowMode::Windowed);
	TestEqual(TEXT("Quality reverted to Medium (1)"), Settings->GetOverallScalabilityLevel(), 1);
	TestFalse(TEXT("VSync reverted to false"), Settings->IsVSyncEnabled());
	TestEqual(TEXT("Settings resolution reverted to unlisted resolution"), Settings->GetScreenResolution(), UnlistedRes);
	TestEqual(TEXT("INDEX_NONE fallback keeps SelectedResolution valid at reverted res"),
		RevertOptions->GetSelectedResolution(), UnlistedRes);
	if (UTextBlock* ResLabel = Cast<UTextBlock>(RevertOptions->GetWidgetFromName(TEXT("TextBlock_2"))))
	{
		TestEqual(TEXT("Resolution label reflects reverted unlisted resolution"),
			ResLabel->GetText().ToString(), FString(TEXT("1111 x 666")));
	}
	if (UWidget* WinMinus = RevertOptions->GetWidgetFromName(TEXT("windowmode-")))
	{
		TestTrue(TEXT("Steppers re-enabled after revert"), WinMinus->GetIsEnabled());
	}

	// 4. Esc and HandleBack revert unconfirmed Custom (-1) quality, VSync, and VideoMode.
	Settings->SetFullscreenMode(EWindowMode::WindowedFullscreen);
	Settings->SetScreenResolution(FIntPoint(1280, 720));
	Settings->SetOverallScalabilityLevel(2);
	Settings->SetShadowQuality(0);
	Settings->SetViewDistanceQuality(3);
	Settings->SetVSyncEnabled(true);
	Settings->ConfirmVideoMode();

	ULostWithinOptionsMenu* EscRevertOptions = CreateWidget<ULostWithinOptionsMenu>(TestWorld.World, OptionsClass);
	if (!TestNotNull(TEXT("Esc revert OptionsMenu created"), EscRevertOptions))
	{
		return false;
	}
	TSharedRef<SWidget> EscSlate = EscRevertOptions->TakeWidget();
	EscRevertOptions->WindowNext(); // Borderless -> Windowed
	EscRevertOptions->GraphicsNext(); // Custom (-1) -> Low (0)
	EscRevertOptions->ToggleVSync(); // true -> false
	EscRevertOptions->ApplyScreenSettings();
	TestTrue(TEXT("Confirmation active before Esc"), EscRevertOptions->IsDisplayConfirmationPending());

	const FKeyEvent EscapeKey(EKeys::Escape, FModifierKeysState(), 0, false, 0, 0);
	const FReply EscReply = EscSlate->OnPreviewKeyDown(FGeometry(), EscapeKey);
	TestTrue(TEXT("OptionsMenu handles Esc key"), EscReply.IsEventHandled());
	TestFalse(TEXT("Esc clears confirmation pending"), EscRevertOptions->IsDisplayConfirmationPending());
	TestEqual(TEXT("Esc reverts WindowMode to WindowedFullscreen"), Settings->GetFullscreenMode(), EWindowMode::WindowedFullscreen);
	TestEqual(TEXT("Esc reverts Custom quality (-1)"), Settings->GetOverallScalabilityLevel(), -1);
	TestEqual(TEXT("Esc reverts Custom ShadowQuality"), Settings->GetShadowQuality(), 0);
	TestEqual(TEXT("Esc reverts Custom ViewDistanceQuality"), Settings->GetViewDistanceQuality(), 3);
	TestTrue(TEXT("Esc reverts VSync to true"), Settings->IsVSyncEnabled());

	// 5. HandleBack with bReturnToPause=true also reverts unconfirmed changes.
	ULostWithinOptionsMenu* BackRevertOptions = CreateWidget<ULostWithinOptionsMenu>(TestWorld.World, OptionsClass);
	if (!TestNotNull(TEXT("HandleBack revert OptionsMenu created"), BackRevertOptions))
	{
		return false;
	}
	BackRevertOptions->SetReturnToPause(true);
	BackRevertOptions->TakeWidget();
	BackRevertOptions->WindowPrevious();
	BackRevertOptions->GraphicsNext();
	BackRevertOptions->ToggleVSync();
	BackRevertOptions->ApplyScreenSettings();
	TestTrue(TEXT("Confirmation active before HandleBack"), BackRevertOptions->IsDisplayConfirmationPending());
	BackRevertOptions->HandleBack();
	TestFalse(TEXT("HandleBack clears confirmation pending"), BackRevertOptions->IsDisplayConfirmationPending());
	TestEqual(TEXT("HandleBack reverts WindowMode"), Settings->GetFullscreenMode(), EWindowMode::WindowedFullscreen);
	TestEqual(TEXT("HandleBack reverts Custom quality"), Settings->GetOverallScalabilityLevel(), -1);
	TestTrue(TEXT("HandleBack reverts VSync"), Settings->IsVSyncEnabled());

	// 6. Keep Changes confirmation keeps Video + Quality + VSync and exits countdown.
	BackRevertOptions->WindowNext(); // WindowedFullscreen -> Windowed
	BackRevertOptions->GraphicsNext(); // Custom (-1) -> Low (0)
	BackRevertOptions->ToggleVSync();  // true -> false
	BackRevertOptions->ApplyScreenSettings();
	TestTrue(TEXT("Confirmation active before Keep Changes"), BackRevertOptions->IsDisplayConfirmationPending());
	BackRevertOptions->ApplyScreenSettings();
	TestFalse(TEXT("Keep Changes exits confirmation state"), BackRevertOptions->IsDisplayConfirmationPending());
	TestEqual(TEXT("Keep Changes resets countdown to -1"), BackRevertOptions->GetLastDisplayedCountdown(), -1);
	TestEqual(TEXT("Keep Changes preserves Windowed mode"), Settings->GetFullscreenMode(), EWindowMode::Windowed);
	TestEqual(TEXT("Keep Changes preserves Low quality"), Settings->GetOverallScalabilityLevel(), 0);
	TestFalse(TEXT("Keep Changes preserves VSync off"), Settings->IsVSyncEnabled());
	if (UTextBlock* ConfirmApplyLabel = Cast<UTextBlock>(BackRevertOptions->GetWidgetFromName(TEXT("TextBlock_0"))))
	{
		TestEqual(TEXT("Apply label returns to APPLY after confirmation"),
			ConfirmApplyLabel->GetText().ToString(), FString(TEXT("APPLY")));
	}
	if (UWidget* ConfirmStepper = BackRevertOptions->GetWidgetFromName(TEXT("windowmode-")))
	{
		TestTrue(TEXT("Steppers re-enabled after Keep Changes"), ConfirmStepper->GetIsEnabled());
	}

	// 7. Audio volume clamping, GConfig storage, and percentage readouts.
	BackRevertOptions->OnMusicVolumeChanged(-0.35f);
	float StoredMusic = -1.0f;
	TestTrue(TEXT("MusicVolume stored in GConfig"),
		GConfig && GConfig->GetFloat(LostWithinMenuTestPrivate::MenuTestAudioSection, LostWithinMenuTestPrivate::MenuTestMusicKey, StoredMusic, GGameUserSettingsIni));
	TestEqual(TEXT("Negative music volume clamps to 0.0"), StoredMusic, 0.0f);
	if (UTextBlock* MusicPct = Cast<UTextBlock>(BackRevertOptions->GetWidgetFromName(TEXT("MusicPercentText"))))
	{
		TestEqual(TEXT("MusicPercentText shows 0%"), MusicPct->GetText().ToString(), FString(TEXT("0%")));
	}

	BackRevertOptions->OnMusicVolumeChanged(2.4f);
	GConfig->GetFloat(LostWithinMenuTestPrivate::MenuTestAudioSection, LostWithinMenuTestPrivate::MenuTestMusicKey, StoredMusic, GGameUserSettingsIni);
	TestEqual(TEXT("Over-1 music volume clamps to 1.0"), StoredMusic, 1.0f);
	if (UTextBlock* MusicPct = Cast<UTextBlock>(BackRevertOptions->GetWidgetFromName(TEXT("MusicPercentText"))))
	{
		TestEqual(TEXT("MusicPercentText shows 100%"), MusicPct->GetText().ToString(), FString(TEXT("100%")));
	}

	BackRevertOptions->OnEffectsVolumeChanged(0.42f);
	float StoredSfx = -1.0f;
	TestTrue(TEXT("EffectsVolume stored in GConfig"),
		GConfig && GConfig->GetFloat(LostWithinMenuTestPrivate::MenuTestAudioSection, LostWithinMenuTestPrivate::MenuTestEffectsKey, StoredSfx, GGameUserSettingsIni));
	TestTrue(TEXT("EffectsVolume stores 0.42 accurately"), FMath::IsNearlyEqual(StoredSfx, 0.42f, 0.001f));
	if (UTextBlock* SfxPct = Cast<UTextBlock>(BackRevertOptions->GetWidgetFromName(TEXT("SfxPercentText"))))
	{
		TestEqual(TEXT("SfxPercentText shows 42%"), SfxPct->GetText().ToString(), FString(TEXT("42%")));
	}

	BackRevertOptions->OnEffectsVolumeChanged(std::numeric_limits<float>::quiet_NaN());
	GConfig->GetFloat(LostWithinMenuTestPrivate::MenuTestAudioSection, LostWithinMenuTestPrivate::MenuTestEffectsKey, StoredSfx, GGameUserSettingsIni);
	TestEqual(TEXT("Non-finite SFX volume safely defaults to 1.0"), StoredSfx, 1.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLostWithinModalAndEndScreenTest,
	"LostWithin.UI.ModalAndEndScreen",
	LOSTWITHIN_MENU_TEST_FLAGS)

bool FLostWithinModalAndEndScreenTest::RunTest(const FString& Parameters)
{
	LostWithinMenuTestPrivate::FScopedMenuTestWorld TestWorld;
	if (!TestNotNull(TEXT("Test world created"), TestWorld.World))
	{
		return false;
	}

	UFont* CinzelBold = LoadObject<UFont>(nullptr, TEXT("/Game/UI/Fonts/Cinzel-Bold_Font.Cinzel-Bold_Font"));
	UFont* CinzelRegular = LoadObject<UFont>(nullptr, TEXT("/Game/UI/Fonts/Cinzel-Regular_Font.Cinzel-Regular_Font"));

	// 1. Pause modal: "Options" plural label, Cinzel font, backdrop visible, focus retention.
	ULostWithinModalWidget* PauseModal = CreateWidget<ULostWithinModalWidget>(TestWorld.World, ULostWithinModalWidget::StaticClass());
	if (!TestNotNull(TEXT("Pause modal created"), PauseModal))
	{
		return false;
	}
	TSharedRef<SWidget> PauseSlate = PauseModal->TakeWidget();
	TestFalse(TEXT("Default modal is pause mode"), PauseModal->IsKeypad());
	TestTrue(TEXT("Pause modal is focusable"), PauseModal->IsFocusable());

	if (UTextBlock* OptionsLabel = LostWithinMenuTestPrivate::GetButtonLabelText(PauseModal, TEXT("PauseOptions")))
	{
		TestEqual(TEXT("PauseOptions label is plural 'Options'"), OptionsLabel->GetText().ToString(), FString(TEXT("Options")));
	}
	else
	{
		AddError(TEXT("PauseOptions button or child label missing"));
	}
	TestNotNull(TEXT("PauseResume exists"), PauseModal->GetWidgetFromName(TEXT("PauseResume")));
	TestNotNull(TEXT("PauseRestart exists"), PauseModal->GetWidgetFromName(TEXT("PauseRestart")));
	TestNotNull(TEXT("PauseMainMenu exists"), PauseModal->GetWidgetFromName(TEXT("PauseMainMenu")));

	if (UTextBlock* ModalTitle = Cast<UTextBlock>(PauseModal->GetWidgetFromName(TEXT("ModalTitle"))))
	{
		if (CinzelBold)
		{
			TestEqual(TEXT("ModalTitle uses Cinzel-Bold_Font"),
				ModalTitle->GetFont().FontObject.Get(), static_cast<const UObject*>(CinzelBold));
		}
	}
	if (UPanelWidget* PauseRoot = Cast<UPanelWidget>(PauseModal->GetRootWidget()))
	{
		UBorder* Backdrop = Cast<UBorder>(PauseRoot->GetChildAt(0));
		TestTrue(TEXT("Pause backdrop is Visible to catch clicks"), Backdrop && Backdrop->GetVisibility() == ESlateVisibility::Visible);
	}
	const FPointerEvent DummyMouseEvent;
	TestTrue(TEXT("Pause modal handles mouse-down on backdrop to retain focus"),
		PauseSlate->OnMouseButtonDown(FGeometry(), DummyMouseEvent).IsEventHandled());

	// 2. Keypad modal: digits 0..9, BackSpace, Enter, Esc, and focus retention.
	AActor* DoorOwner = TestWorld.World->SpawnActor<AActor>(AActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
	UEscapeDoorComponent* Door = NewObject<UEscapeDoorComponent>(DoorOwner, TEXT("EscapeDoor"));
	Door->bUseKeypadUI = true;
	Door->RegisterComponent();

	ULostWithinModalWidget* KeypadModal = CreateWidget<ULostWithinModalWidget>(TestWorld.World, ULostWithinModalWidget::StaticClass());
	if (!TestNotNull(TEXT("Keypad modal created"), KeypadModal))
	{
		return false;
	}
	KeypadModal->ConfigureKeypad(Door);
	TSharedRef<SWidget> KeypadSlate = KeypadModal->TakeWidget();
	TestTrue(TEXT("Keypad modal reports IsKeypad"), KeypadModal->IsKeypad());

	for (int32 Digit = 0; Digit <= 9; ++Digit)
	{
		TestNotNull(FString::Printf(TEXT("KeypadDigit%d exists"), Digit),
			Cast<ULostWithinDigitButton>(KeypadModal->GetWidgetFromName(*FString::Printf(TEXT("KeypadDigit%d"), Digit))));
	}
	TestNotNull(TEXT("KeypadBackspace exists"), KeypadModal->GetWidgetFromName(TEXT("KeypadBackspace")));
	TestNotNull(TEXT("KeypadSubmit exists"), KeypadModal->GetWidgetFromName(TEXT("KeypadSubmit")));
	TestNotNull(TEXT("KeypadClose exists"), KeypadModal->GetWidgetFromName(TEXT("KeypadClose")));

	const FKeyEvent KeyOne(EKeys::One, FModifierKeysState(), 0, false, 0, 0);
	const FKeyEvent KeyTwo(EKeys::NumPadTwo, FModifierKeysState(), 0, false, 0, 0);
	const FKeyEvent KeyBack(EKeys::BackSpace, FModifierKeysState(), 0, false, 0, 0);
	const FKeyEvent KeyZero(EKeys::Zero, FModifierKeysState(), 0, false, 0, 0);
	const FKeyEvent KeyNine(EKeys::Nine, FModifierKeysState(), 0, false, 0, 0);
	const FKeyEvent KeyEnter(EKeys::Enter, FModifierKeysState(), 0, false, 0, 0);
	const FKeyEvent KeyEsc(EKeys::Escape, FModifierKeysState(), 0, false, 0, 0);

	TestTrue(TEXT("Keypad handles digit 1"), KeypadSlate->OnPreviewKeyDown(FGeometry(), KeyOne).IsEventHandled());
	TestTrue(TEXT("Keypad handles numpad 2"), KeypadSlate->OnPreviewKeyDown(FGeometry(), KeyTwo).IsEventHandled());
	TestEqual(TEXT("Entered code after 1,2"), KeypadModal->GetEnteredCode(), FString(TEXT("12")));
	TestTrue(TEXT("Keypad handles BackSpace"), KeypadSlate->OnPreviewKeyDown(FGeometry(), KeyBack).IsEventHandled());
	TestEqual(TEXT("Entered code after BackSpace"), KeypadModal->GetEnteredCode(), FString(TEXT("1")));

	TestTrue(TEXT("Keypad handles Enter when incomplete"), KeypadSlate->OnPreviewKeyDown(FGeometry(), KeyEnter).IsEventHandled());
	TestTrue(TEXT("Incomplete feedback shown"), KeypadModal->GetFeedback().ToString().Contains(TEXT("three digits")));

	KeypadSlate->OnPreviewKeyDown(FGeometry(), KeyZero);
	KeypadSlate->OnPreviewKeyDown(FGeometry(), KeyNine);
	TestEqual(TEXT("Entered code is 109"), KeypadModal->GetEnteredCode(), FString(TEXT("109")));
	KeypadSlate->OnPreviewKeyDown(FGeometry(), KeyEnter);
	TestTrue(TEXT("Incorrect submit clears entered code"), KeypadModal->GetEnteredCode().IsEmpty());
	TestTrue(TEXT("Incorrect feedback shown"), KeypadModal->GetFeedback().ToString().Contains(TEXT("INCORRECT")));

	if (ULostWithinDigitButton* Digit7 = Cast<ULostWithinDigitButton>(KeypadModal->GetWidgetFromName(TEXT("KeypadDigit7"))))
	{
		Digit7->OnClicked.Broadcast();
		TestEqual(TEXT("Clicking KeypadDigit7 appends '7'"), KeypadModal->GetEnteredCode(), FString(TEXT("7")));
	}
	TestTrue(TEXT("Keypad handles mouse-down on backdrop"),
		KeypadSlate->OnMouseButtonDown(FGeometry(), DummyMouseEvent).IsEventHandled());
	TestTrue(TEXT("Keypad handles Esc"), KeypadSlate->OnPreviewKeyDown(FGeometry(), KeyEsc).IsEventHandled());

	// 3. GameEndWidget: Configure(false/true), Cinzel fonts, focus retention.
	UGameEndWidget* EndWidget = CreateWidget<UGameEndWidget>(TestWorld.World, UGameEndWidget::StaticClass());
	if (!TestNotNull(TEXT("GameEndWidget created"), EndWidget))
	{
		return false;
	}
	TSharedRef<SWidget> EndSlate = EndWidget->TakeWidget();
	TestTrue(TEXT("EndWidget is focusable"), EndWidget->IsFocusable());

	EndWidget->Configure(false);
	TestFalse(TEXT("Configure(false) sets WasEscape false"), EndWidget->WasEscape());
	TestEqual(TEXT("Death title"), EndWidget->GetTitle().ToString(), FString(TEXT("YOU WERE CAUGHT")));
	if (UTextBlock* RestartLbl = LostWithinMenuTestPrivate::GetButtonLabelText(EndWidget, TEXT("EndRestartButton")))
	{
		TestEqual(TEXT("Death restart label"), RestartLbl->GetText().ToString(), FString(TEXT("Try again")));
	}
	if (UTextBlock* QuitLbl = LostWithinMenuTestPrivate::GetButtonLabelText(EndWidget, TEXT("EndQuitButton")))
	{
		TestEqual(TEXT("End quit label"), QuitLbl->GetText().ToString(), FString(TEXT("Main Menu")));
	}

	EndWidget->Configure(true);
	TestTrue(TEXT("Configure(true) sets WasEscape true"), EndWidget->WasEscape());
	TestEqual(TEXT("Escape title"), EndWidget->GetTitle().ToString(), FString(TEXT("EXPERIMENT CONCLUDED")));
	if (UTextBlock* RestartLbl = LostWithinMenuTestPrivate::GetButtonLabelText(EndWidget, TEXT("EndRestartButton")))
	{
		TestEqual(TEXT("Escape restart label"), RestartLbl->GetText().ToString(), FString(TEXT("Play again")));
	}
	if (UTextBlock* EndTitle = Cast<UTextBlock>(EndWidget->GetWidgetFromName(TEXT("EndTitle"))))
	{
		if (CinzelBold)
		{
			TestEqual(TEXT("EndTitle uses Cinzel-Bold_Font"),
				EndTitle->GetFont().FontObject.Get(), static_cast<const UObject*>(CinzelBold));
		}
	}
	if (UTextBlock* EndSubtitle = Cast<UTextBlock>(EndWidget->GetWidgetFromName(TEXT("EndSubtitle"))))
	{
		if (CinzelRegular)
		{
			TestEqual(TEXT("EndSubtitle uses Cinzel-Regular_Font"),
				EndSubtitle->GetFont().FontObject.Get(), static_cast<const UObject*>(CinzelRegular));
		}
		TestTrue(TEXT("Escape subtitle mentions SUBJECT 09 ESCAPED"),
			EndSubtitle->GetText().ToString().Contains(TEXT("SUBJECT 09 ESCAPED")));
	}
	if (UBorder* EndBackdrop = Cast<UBorder>(EndWidget->GetWidgetFromName(TEXT("EndBackdrop"))))
	{
		TestEqual(TEXT("EndBackdrop is Visible"), EndBackdrop->GetVisibility(), ESlateVisibility::Visible);
	}
	TestTrue(TEXT("EndWidget handles mouse-down on backdrop to retain focus"),
		EndSlate->OnMouseButtonDown(FGeometry(), DummyMouseEvent).IsEventHandled());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLostWithinControllerFlowAndPadlockTest,
	"LostWithin.UI.ControllerFlowAndPadlock",
	LOSTWITHIN_MENU_TEST_FLAGS)

bool FLostWithinControllerFlowAndPadlockTest::RunTest(const FString& Parameters)
{
	UClass* PCClass = LoadClass<ALostWithinPlayerController>(nullptr, LostWithinMenuTestPrivate::MenuTestFirstPersonPCClassPath);
	UClass* CharClass = LoadClass<ALostWithinCharacter>(nullptr, LostWithinMenuTestPrivate::MenuTestFirstPersonCharClassPath);
	UClass* GMClass = LoadClass<ALostWithinGameMode>(nullptr, LostWithinMenuTestPrivate::MenuTestFirstPersonGMClassPath);
	if (!TestNotNull(TEXT("BP_FirstPersonPlayerController loads"), PCClass)
		|| !TestNotNull(TEXT("BP_FirstPersonCharacter loads"), CharClass)
		|| !TestNotNull(TEXT("BP_FirstPersonGameMode loads"), GMClass))
	{
		return false;
	}

	// Part A: PlayerController BeginPlay resets cursor hidden independently of Character HUD,
	// plus HandleEscape context-menu ordering, Escaping inventory lock, ShowEndScreen panel close, and EndScreen cleanup.
	{
		LostWithinMenuTestPrivate::FScopedMenuTestWorld TestWorld(GMClass, /*bAutoBeginPlay=*/false);
		if (!TestNotNull(TEXT("Test world created"), TestWorld.World))
		{
			return false;
		}
		LostWithinMenuTestPrivate::PopulateMinimalRunSpawnPoints(TestWorld.World);

		ALostWithinPlayerController* PC = TestWorld.World->SpawnActor<ALostWithinPlayerController>(PCClass);
		ALostWithinCharacter* Char = TestWorld.World->SpawnActor<ALostWithinCharacter>(CharClass, FVector(0.0f, 0.0f, 120.0f), FRotator::ZeroRotator);
		if (!TestNotNull(TEXT("PlayerController spawned"), PC) || !TestNotNull(TEXT("Character spawned"), Char))
		{
			return false;
		}
		TestWorld.AttachLocalPlayer(PC);
		PC->SetShowMouseCursor(true);
		PC->Possess(Char);

		TestWorld.StartPlay();

		TestFalse(TEXT("ALostWithinPlayerController::BeginPlay resets mouse cursor hidden"), PC->ShouldShowMouseCursor());

		ALostWithinGameMode* Mode = Cast<ALostWithinGameMode>(TestWorld.World->GetAuthGameMode());
		if (!TestNotNull(TEXT("ALostWithinGameMode active"), Mode) || !TestTrue(TEXT("Run starts Active"), Mode->IsRunActive()))
		{
			return false;
		}

		UFunction* ToggleInventoryFn = Char->FindFunction(TEXT("DoToggleInventory"));
		if (!TestNotNull(TEXT("DoToggleInventory UFunction exists"), ToggleInventoryFn))
		{
			return false;
		}

		// Locate the player's UInventoryWidget and test Esc closing RMB context menu before Inventory.
		UInventoryWidget* InvWidget = nullptr;
		for (TObjectIterator<UInventoryWidget> It; It; ++It)
		{
			if (IsValid(*It) && !It->HasAnyFlags(RF_ClassDefaultObject) && It->GetOwningPlayer() == PC)
			{
				InvWidget = *It;
				break;
			}
		}
		if (TestNotNull(TEXT("Character created UInventoryWidget"), InvWidget)
			&& TestNotNull(TEXT("Character has InventoryComponent"), Char->GetInventoryComponent()))
		{
			InvWidget->TakeWidget();
			FInventoryItem NoteItem;
			NoteItem.ItemID = TEXT("Note_MenuFlowTest");
			NoteItem.DisplayName = FText::FromString(TEXT("Test Note"));
			NoteItem.Description = FText::FromString(TEXT("Test body"));
			NoteItem.Quantity = 1;
			int32 Remaining = 0;
			Char->GetInventoryComponent()->AddItem(NoteItem, Remaining);

			Char->ProcessEvent(ToggleInventoryFn, nullptr);
			TestTrue(TEXT("Inventory opens while run is Active"), Char->IsInventoryOpen());

			InvWidget->OpenItemContextMenu(0, FVector2D::ZeroVector);
			TestTrue(TEXT("Inventory RMB context menu opened"), Char->IsInventoryContextMenuOpen());

			PC->HandleEscape();
			TestFalse(TEXT("First Esc closes Inventory RMB context menu"), Char->IsInventoryContextMenuOpen());
			TestTrue(TEXT("First Esc keeps Inventory open"), Char->IsInventoryOpen());

			PC->HandleEscape();
			TestFalse(TEXT("Second Esc closes Inventory"), Char->IsInventoryOpen());
		}

		// Open Inventory + Note while Active, then verify ShowEndScreen closes both panels.
		Char->ProcessEvent(ToggleInventoryFn, nullptr);
		Char->ShowNote(FText::FromString(TEXT("NoteTitle")), FText::FromString(TEXT("NoteBody")));
		TestTrue(TEXT("Inventory open before ShowEndScreen"), Char->IsInventoryOpen());
		TestTrue(TEXT("Note open before ShowEndScreen"), Char->IsNoteOpen());

		PC->ShowEndScreen(/*bEscaped=*/true);
		TestTrue(TEXT("ShowEndScreen sets EndScreenWidget visible"), PC->IsEndScreenVisible());
		TestFalse(TEXT("ShowEndScreen closes open Inventory"), Char->IsInventoryOpen());
		TestFalse(TEXT("ShowEndScreen closes open Note"), Char->IsNoteOpen());
		TestTrue(TEXT("ShowEndScreen shows mouse cursor"), PC->ShouldShowMouseCursor());
		TestTrue(TEXT("ShowEndScreen pauses world"), TestWorld.World->IsPaused());

		PC->RestartRun();
		TestFalse(TEXT("RestartRun removes and clears EndScreenWidget"), PC->IsEndScreenVisible());
		TestFalse(TEXT("RestartRun unpauses world"), TestWorld.World->IsPaused());

		PC->ShowEndScreen(/*bEscaped=*/false);
		TestTrue(TEXT("ShowEndScreen(false) displays death end screen"), PC->IsEndScreenVisible());
		PC->ReturnToMainMenu();
		TestFalse(TEXT("ReturnToMainMenu removes and clears EndScreenWidget"), PC->IsEndScreenVisible());
		TestFalse(TEXT("ReturnToMainMenu unpauses world"), TestWorld.World->IsPaused());

		// Verify DoToggleInventory is blocked during Escaping (!IsRunActive()).
		TestTrue(TEXT("BeginEscape transitions RunState to Escaping"), Mode->BeginEscape());
		TestFalse(TEXT("Run is no longer active during Escaping"), Mode->IsRunActive());
		Char->ProcessEvent(ToggleInventoryFn, nullptr);
		TestFalse(TEXT("DoToggleInventory is blocked during Escaping"), Char->IsInventoryOpen());
	}

	// Part B: BeginPlay cursor reset with NO Pawn at all, plus CloseGameplayModal padlock ViewTarget preservation vs dead/inactive hide.
	{
		LostWithinMenuTestPrivate::FScopedMenuTestWorld PadlockWorld(GMClass, /*bAutoBeginPlay=*/false);
		if (!TestNotNull(TEXT("Padlock test world created"), PadlockWorld.World))
		{
			return false;
		}
		LostWithinMenuTestPrivate::PopulateMinimalRunSpawnPoints(PadlockWorld.World);

		ALostWithinPlayerController* PC = PadlockWorld.World->SpawnActor<ALostWithinPlayerController>(PCClass);
		if (!TestNotNull(TEXT("Standalone controller spawned"), PC))
		{
			return false;
		}
		PadlockWorld.AttachLocalPlayer(PC);
		PC->SetShowMouseCursor(true);
		PadlockWorld.StartPlay();

		TestFalse(TEXT("PlayerController::BeginPlay alone resets cursor without needing a pawn"), PC->ShouldShowMouseCursor());

		ALostWithinCharacter* Char = PadlockWorld.World->SpawnActor<ALostWithinCharacter>(CharClass, FVector(0.0f, 0.0f, 120.0f), FRotator::ZeroRotator);
		AActor* PadlockView = PadlockWorld.World->SpawnActor<AActor>(AActor::StaticClass(), FVector(300.0f, 0.0f, 120.0f), FRotator::ZeroRotator);
		if (!TestNotNull(TEXT("Padlock test character spawned"), Char) || !TestNotNull(TEXT("Padlock view actor spawned"), PadlockView))
		{
			return false;
		}
		PC->Possess(Char);
		PC->SetViewTarget(PadlockView);
		TestEqual(TEXT("ViewTarget switched away from pawn to padlock"), PC->GetViewTarget(), PadlockView);

		// Press Esc in padlock view -> opens Pause, then ResumeRun -> must preserve cursor visible.
		PC->HandleEscape();
		TestTrue(TEXT("Esc in padlock view opens pause modal"), PC->IsGameplayModalOpen());
		TestTrue(TEXT("Pause modal shows cursor"), PC->ShouldShowMouseCursor());

		PC->ResumeRun();
		TestFalse(TEXT("ResumeRun closes pause modal"), PC->IsGameplayModalOpen());
		TestFalse(TEXT("ResumeRun unpauses world"), PadlockWorld.World->IsPaused());
		TestTrue(TEXT("CloseGameplayModal keeps cursor shown while alive/active in padlock ViewTarget"),
			PC->ShouldShowMouseCursor());

		// Return view target to pawn -> ResumeRun must hide cursor.
		PC->SetViewTarget(Char);
		PC->HandleEscape();
		TestTrue(TEXT("Esc on pawn opens pause modal"), PC->IsGameplayModalOpen());
		PC->ResumeRun();
		TestFalse(TEXT("CloseGameplayModal hides cursor when ViewTarget is pawn"), PC->ShouldShowMouseCursor());

		// When player is dead, CloseGameplayModal hides cursor even if ViewTarget != Pawn.
		PC->SetViewTarget(PadlockView);
		Char->EnterDeathState();
		TestTrue(TEXT("Character marked dead"), Char->IsDead());
		PC->SetShowMouseCursor(true);
		PC->CloseGameplayModal();
		TestFalse(TEXT("CloseGameplayModal hides cursor when dead even with external ViewTarget"),
			PC->ShouldShowMouseCursor());
	}

	// Part C: CloseGameplayModal hides cursor when !IsRunActive() (Escaping) even if ViewTarget != Pawn.
	{
		LostWithinMenuTestPrivate::FScopedMenuTestWorld EscapingWorld(GMClass, /*bAutoBeginPlay=*/false);
		if (!TestNotNull(TEXT("Escaping test world created"), EscapingWorld.World))
		{
			return false;
		}
		LostWithinMenuTestPrivate::PopulateMinimalRunSpawnPoints(EscapingWorld.World);

		ALostWithinPlayerController* PC = EscapingWorld.World->SpawnActor<ALostWithinPlayerController>(PCClass);
		ALostWithinCharacter* Char = EscapingWorld.World->SpawnActor<ALostWithinCharacter>(CharClass, FVector(0.0f, 0.0f, 120.0f), FRotator::ZeroRotator);
		AActor* ExternalView = EscapingWorld.World->SpawnActor<AActor>(AActor::StaticClass(), FVector(300.0f, 0.0f, 120.0f), FRotator::ZeroRotator);
		if (!TestNotNull(TEXT("Escaping PC"), PC) || !TestNotNull(TEXT("Escaping Char"), Char) || !TestNotNull(TEXT("External view"), ExternalView))
		{
			return false;
		}
		EscapingWorld.AttachLocalPlayer(PC);
		PC->Possess(Char);
		EscapingWorld.StartPlay();

		ALostWithinGameMode* Mode = Cast<ALostWithinGameMode>(EscapingWorld.World->GetAuthGameMode());
		TestTrue(TEXT("EscapingWorld begins escape"), Mode && Mode->BeginEscape());
		PC->SetViewTarget(ExternalView);
		PC->SetShowMouseCursor(true);
		PC->CloseGameplayModal();
		TestFalse(TEXT("CloseGameplayModal hides cursor when run is not active"), PC->ShouldShowMouseCursor());
	}

	return true;
}

#if WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLostWithinMenuFlowPIETest,
	"LostWithin.UI.MenuFlow.PIE",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLostWithinMenuFlowPIETest::RunTest(const FString& Parameters)
{
	if (!GEditor || GEditor->PlayWorld)
	{
		AddError(TEXT("Close active PIE before running LostWithin.UI.MenuFlow.PIE."));
		return false;
	}

	FRequestPlaySessionParams PlayParams;
	PlayParams.GlobalMapOverride = TEXT("/Game/FirstPerson/Lvl_MainMenu.Lvl_MainMenu");
	GEditor->RequestPlaySession(PlayParams);
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<LostWithinMenuTestPrivate::FObserveMenuFlow>(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}
#endif

#undef LOSTWITHIN_MENU_TEST_FLAGS

#endif // WITH_DEV_AUTOMATION_TESTS
