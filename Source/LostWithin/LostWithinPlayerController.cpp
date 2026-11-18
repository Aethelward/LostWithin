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
