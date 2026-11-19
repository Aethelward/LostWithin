// Copyright Epic Games, Inc. All Rights Reserved.


#include "LostWithinPlayerController.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "InputMappingContext.h"
#include "LostWithinCameraManager.h"
#include "Blueprint/UserWidget.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "LostWithin.h"
#include "Sound/SoundBase.h"
#include "UI/GameEndWidget.h"
#include "UObject/ConstructorHelpers.h"
#include "Widgets/Input/SVirtualJoystick.h"
#include "Components/InputComponent.h"
#include "InputCoreTypes.h"
#include "Interaction/EscapeDoorComponent.h"
#include "LostWithinCharacter.h"
#include "LostWithinGameMode.h"
#include "UI/LostWithinMenu.h"
#include "UI/LostWithinModalWidget.h"

ALostWithinPlayerController::ALostWithinPlayerController()
{
	// set the player camera manager class
	PlayerCameraManagerClass = ALostWithinCameraManager::StaticClass();

	// The ScaryDoor padlock picks its active dial on cursor hover and click.
	bEnableMouseOverEvents = true;
	bEnableClickEvents = true;

	EndScreenWidgetClass = UGameEndWidget::StaticClass();

	// Game-over sound for the death screen, generated with ElevenLabs (Tools/SFX/sfx-manifest.json, id death_game_over).
	static ConstructorHelpers::FObjectFinder<USoundBase> GameOver(
		TEXT("/Game/Audio/SFX/Player/Death/SFX_DeathGameOver_01.SFX_DeathGameOver_01"));
	DeathEndSound = GameOver.Object;
}

void ALostWithinPlayerController::ShowEndScreen(bool bEscaped)
{
	if (EndScreenWidget || !IsLocalPlayerController())
	{
		return;
	}

	CloseGameplayModal();
	if (ALostWithinCharacter* Char = Cast<ALostWithinCharacter>(GetPawn()))
	{
		Char->CloseGameplayPanels();
	}
	UClass* WidgetClass = EndScreenWidgetClass ? EndScreenWidgetClass.Get() : UGameEndWidget::StaticClass();
	EndScreenWidget = CreateWidget<UGameEndWidget>(this, WidgetClass);
	if (!EndScreenWidget)
	{
		UE_LOG(LogLostWithin, Error, TEXT("Could not create the end screen widget."));
		return;
	}

	EndScreenWidget->Configure(bEscaped);
	EndScreenWidget->AddToViewport(100);

	if (!bEscaped && DeathEndSound)
	{
		// A UI sound, so SetPause below does not stop it.
		UGameplayStatics::PlaySound2D(this, DeathEndSound, 1.0f, 1.0f, 0.0f, nullptr, nullptr, /*bIsUISound=*/true);
		UE_LOG(LogLostWithin, Log, TEXT("Death end screen: game over %s"), *DeathEndSound->GetName());
	}

	SetShowMouseCursor(true);
	FInputModeUIOnly InputMode;
	InputMode.SetWidgetToFocus(EndScreenWidget->TakeWidget());
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(InputMode);
	SetPause(true);
}

void ALostWithinPlayerController::RestartRun()
{
	if (EndScreenWidget)
	{
		EndScreenWidget->RemoveFromParent();
		EndScreenWidget = nullptr;
	}
	CloseGameplayModal();
	SetPause(false);
	UGameplayStatics::OpenLevel(this, FName(*UGameplayStatics::GetCurrentLevelName(this, /*bRemovePrefixString=*/true)));
}

void ALostWithinPlayerController::QuitRun()
{
	UKismetSystemLibrary::QuitGame(this, this, EQuitPreference::Quit, /*bIgnorePlatformRestrictions=*/false);
}

void ALostWithinPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (IsLocalPlayerController() && !IsGameplayModalOpen())
	{
		SetShowMouseCursor(false);
		SetInputMode(FInputModeGameOnly());
	}

	
	// only spawn touch controls on local player controllers
	if (SVirtualJoystick::ShouldDisplayTouchInterface() && IsLocalPlayerController())
	{
		// spawn the mobile controls widget
		MobileControlsWidget = CreateWidget<UUserWidget>(this, MobileControlsWidgetClass);

		if (MobileControlsWidget)
		{
			// add the controls to the player screen
			MobileControlsWidget->AddToPlayerScreen(0);

		} else {

			UE_LOG(LogLostWithin, Error, TEXT("Could not spawn mobile controls widget."));

		}

	}
}

void ALostWithinPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	if (InputComponent)
	{
		FInputKeyBinding& EscapeBinding = InputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &ALostWithinPlayerController::HandleEscape);
		EscapeBinding.bExecuteWhenPaused = true;
	}

	// only add IMCs for local player controllers
	if (IsLocalPlayerController())
	{
		// Add Input Mapping Context
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
		{
			for (UInputMappingContext* CurrentContext : DefaultMappingContexts)
			{
				Subsystem->AddMappingContext(CurrentContext, 0);
			}

			// only add these IMCs if we're not using mobile touch input
			if (!SVirtualJoystick::ShouldDisplayTouchInterface())
			{
				for (UInputMappingContext* CurrentContext : MobileExcludedMappingContexts)
				{
					Subsystem->AddMappingContext(CurrentContext, 0);
				}
			}
		}
	}
	
}

bool ALostWithinPlayerController::IsGameplayModalOpen() const
{
	return GameplayModal != nullptr || PauseOptions != nullptr || EndScreenWidget != nullptr;
}

void ALostWithinPlayerController::FocusModal(UUserWidget* Widget)
{
	if (!Widget) { return; }
	SetShowMouseCursor(true);
	FInputModeUIOnly Mode;
	Mode.SetWidgetToFocus(Widget->TakeWidget());
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(Mode);
	Widget->SetKeyboardFocus();
}

void ALostWithinPlayerController::CloseGameplayModal()
{
	if (GameplayModal) { GameplayModal->RemoveFromParent(); GameplayModal = nullptr; }
	if (PauseOptions) { PauseOptions->RemoveFromParent(); PauseOptions = nullptr; }
	if (bPauseContext) { SetPause(false); bPauseContext = false; }

	const ALostWithinCharacter* Char = Cast<ALostWithinCharacter>(GetPawn());
	const ALostWithinGameMode* Mode = GetWorld() ? Cast<ALostWithinGameMode>(GetWorld()->GetAuthGameMode()) : nullptr;
	const bool bPreserveExternalView = Char && !Char->IsDead() && (!Mode || Mode->IsRunActive()) && GetViewTarget() && GetViewTarget() != Char;
	if (bPreserveExternalView)
	{
		SetShowMouseCursor(true);
		FInputModeGameAndUI InputMode;
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		InputMode.SetHideCursorDuringCapture(true);
		SetInputMode(InputMode);
	}
	else
	{
		SetShowMouseCursor(false);
		SetInputMode(FInputModeGameOnly());
	}
}

void ALostWithinPlayerController::ShowKeypad(UEscapeDoorComponent* Door)
{
	if (!Door || EndScreenWidget || !IsLocalController()) { return; }
	CloseGameplayModal();
	if (ALostWithinCharacter* PlayerCharacter = Cast<ALostWithinCharacter>(GetPawn())) { PlayerCharacter->CloseGameplayPanels(); }
	GameplayModal = CreateWidget<ULostWithinModalWidget>(this);
	if (GameplayModal)
	{
		GameplayModal->ConfigureKeypad(Door);
		GameplayModal->AddToViewport(40);
		FocusModal(GameplayModal);
	}
}

void ALostWithinPlayerController::ShowPauseMenu()
{
	if (EndScreenWidget || !IsLocalController()) { return; }
	if (GameplayModal) { GameplayModal->RemoveFromParent(); GameplayModal = nullptr; }
	if (ALostWithinCharacter* PlayerCharacter = Cast<ALostWithinCharacter>(GetPawn())) { PlayerCharacter->CloseGameplayPanels(); }
	bPauseContext = true;
	SetPause(true);
	GameplayModal = CreateWidget<ULostWithinModalWidget>(this);
	if (GameplayModal) { GameplayModal->AddToViewport(50); FocusModal(GameplayModal); }
}

void ALostWithinPlayerController::HandleEscape()
{
	if (EndScreenWidget) { return; }
	if (PauseOptions) { ClosePauseOptions(); return; }
	if (GameplayModal) { if (bPauseContext) { ResumeRun(); } else { CloseGameplayModal(); } return; }
	if (ALostWithinCharacter* PlayerCharacter = Cast<ALostWithinCharacter>(GetPawn()))
	{
		if (PlayerCharacter->IsDead()) { return; }
		if (PlayerCharacter->IsNoteOpen()) { PlayerCharacter->HideNote(); return; }
		if (PlayerCharacter->IsInventoryContextMenuOpen()) { PlayerCharacter->CloseInventoryContextMenu(); return; }
		if (PlayerCharacter->IsInventoryOpen()) { PlayerCharacter->CloseGameplayPanels(); return; }
	}
	if (const ALostWithinGameMode* Mode = GetWorld() ? Cast<ALostWithinGameMode>(GetWorld()->GetAuthGameMode()) : nullptr;
		Mode && !Mode->IsRunActive()) { return; }
	ShowPauseMenu();
}

void ALostWithinPlayerController::ResumeRun() { CloseGameplayModal(); }

void ALostWithinPlayerController::OpenPauseOptions()
{
	if (!bPauseContext || PauseOptions) { return; }
	UClass* Class = LoadClass<ULostWithinOptionsMenu>(nullptr, TEXT("/Game/UI/Menu/OptionsMenu.OptionsMenu_C"));
	if (!Class) { UE_LOG(LogLostWithin, Error, TEXT("Pause options widget is unavailable.")); return; }
	PauseOptions = CreateWidget<ULostWithinOptionsMenu>(this, Class);
	if (!PauseOptions) { return; }
	if (GameplayModal) { GameplayModal->RemoveFromParent(); GameplayModal = nullptr; }
	PauseOptions->SetReturnToPause(true);
	PauseOptions->AddToViewport(50);
	FocusModal(PauseOptions);
}

void ALostWithinPlayerController::ClosePauseOptions()
{
	if (PauseOptions) { PauseOptions->RemoveFromParent(); PauseOptions = nullptr; }
	ShowPauseMenu();
}

void ALostWithinPlayerController::ReturnToMainMenu()
{
	if (EndScreenWidget)
	{
		EndScreenWidget->RemoveFromParent();
		EndScreenWidget = nullptr;
	}
	CloseGameplayModal();
	SetPause(false);
	UGameplayStatics::OpenLevel(this, FName(TEXT("/Game/FirstPerson/Lvl_MainMenu")));
}
