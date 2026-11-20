// Copyright Epic Games, Inc. All Rights Reserved.

#include "LostWithinGameMode.h"

#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Gameplay/RunSetupRules.h"
#include "Interaction/PickupItemActor.h"
#include "LostWithin.h"
#include "LostWithinCharacter.h"
#include "LostWithinPlayerController.h"
#include "Player/WakeupCinematicActor.h"
#include "UI/LostWithinMenu.h"

ALostWithinGameMode::ALostWithinGameMode()
{
	SpawnableKeys = { APickupItemActor::KeyItemID, APickupItemActor::OldKeyItemID, APickupItemActor::RoyalKeyItemID };
	NoteActorClass = TSoftClassPtr<APickupItemActor>(FSoftObjectPath(TEXT("/Game/Item/paper/BP_Paper.BP_Paper_C")));
	bEnableOpeningCinematic = true;
	OpeningCinematicClass = AWakeupCinematicActor::StaticClass();
}

void ALostWithinGameMode::StartPlay()
{
	// The combination exists before any actor's BeginPlay, so the door can hand it to its padlock.
	RunStream.Initialize(RunSeed != 0 ? RunSeed : FMath::Rand());
	CombinationDigits = LostWithinRunRules::MakeCombination(RunStream);
	RunState = ELostWithinRunState::Active;
	ApplyLostWithinAudioOptions(this);

	Super::StartPlay();

	// Every placed spawn point and toolbox has begun play now.
	RunSummary = LostWithinRunSetup::SetupWorld(GetWorld(), MakeRunConfig(), CombinationDigits, RunStream);

	if (bEnableOpeningCinematic && !ActiveOpeningCinematic)
	{
		StartOpeningCinematic();
	}
}

void ALostWithinGameMode::RestartPlayer(AController* NewPlayer)
{
	Super::RestartPlayer(NewPlayer);

	if (bEnableOpeningCinematic && !ActiveOpeningCinematic)
	{
		StartOpeningCinematic();
	}
	else if (ActiveOpeningCinematic && ActiveOpeningCinematic->IsPlaying())
	{
		if (APlayerController* PC = Cast<APlayerController>(NewPlayer))
		{
			if (ALostWithinCharacter* LostChar = Cast<ALostWithinCharacter>(PC->GetPawn()))
			{
				LostChar->EnterCinematicState();
			}
			PC->SetIgnoreMoveInput(true);
			PC->SetIgnoreLookInput(true);
			PC->SetViewTarget(ActiveOpeningCinematic);
		}
	}
}

void ALostWithinGameMode::StartOpeningCinematic()
{
	if (!bEnableOpeningCinematic)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const FString MapName = World->GetMapName();
	if (MapName.Contains(TEXT("MainMenu")))
	{
		return;
	}

	UClass* CinematicClass = OpeningCinematicClass ? OpeningCinematicClass.Get() : AWakeupCinematicActor::StaticClass();
	if (!CinematicClass)
	{
		return;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.Owner = this;

	ActiveOpeningCinematic = World->SpawnActor<AWakeupCinematicActor>(CinematicClass, FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
	if (!ActiveOpeningCinematic)
	{
		UE_LOG(LogLostWithin, Warning, TEXT("Failed to spawn OpeningCinematicClass in GameMode."));
		return;
	}

	APlayerController* PC = World->GetFirstPlayerController();
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;

	if (ALostWithinCharacter* LostChar = Cast<ALostWithinCharacter>(Pawn))
	{
		LostChar->EnterCinematicState();
	}
	else if (Pawn)
	{
		if (ACharacter* Char = Cast<ACharacter>(Pawn))
		{
			if (UCharacterMovementComponent* MoveComp = Char->GetCharacterMovement())
			{
				MoveComp->StopMovementImmediately();
			}
		}
	}

	if (PC)
	{
		PC->SetIgnoreMoveInput(true);
		PC->SetIgnoreLookInput(true);
	}

	ActiveOpeningCinematic->OnCinematicFinished.AddDynamic(this, &ALostWithinGameMode::HandleOpeningCinematicFinished);
	ActiveOpeningCinematic->StartSequence(Pawn, PC);
}

void ALostWithinGameMode::HandleOpeningCinematicFinished()
{
	UE_LOG(LogLostWithin, Log, TEXT("ALostWithinGameMode: Opening cinematic finished. Restoring controls and HUD."));

	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;

	if (PC)
	{
		PC->ResetIgnoreMoveInput();
		PC->ResetIgnoreLookInput();
	}

	if (ALostWithinCharacter* LostChar = Cast<ALostWithinCharacter>(Pawn))
	{
		LostChar->ExitCinematicState();
		LostChar->SetHUDVisible(true);
	}

	if (ActiveOpeningCinematic)
	{
		ActiveOpeningCinematic->OnCinematicFinished.RemoveDynamic(this, &ALostWithinGameMode::HandleOpeningCinematicFinished);
	}
}

FLostWithinRunConfig ALostWithinGameMode::MakeRunConfig() const
{
	FLostWithinRunConfig Config;
	Config.SpawnableKeys = SpawnableKeys;
	Config.ToolboxLockChance = ToolboxLockChance;
	Config.BatteryLootWeight = BatteryLootWeight;
	Config.NoteLootWeight = NoteLootWeight;
	Config.EmptyLootWeight = EmptyLootWeight;
	Config.FlavorNoteCount = FlavorNoteCount;
	Config.HintNoteInToolboxChance = HintNoteInToolboxChance;
	Config.HintTemplates = HintTemplates;
	Config.FlavorPhrases = FlavorPhrases;
	Config.NoteClass = NoteActorClass.LoadSynchronous();
	if (!Config.NoteClass && !NoteActorClass.IsNull())
	{
		UE_LOG(LogLostWithin, Warning, TEXT("Note class %s did not load; notes use the plain pickup class."), *NoteActorClass.ToString());
	}
	return Config;
}

FString ALostWithinGameMode::GetDoorCombination() const
{
	return LostWithinRunRules::CombinationToString(CombinationDigits);
}

bool ALostWithinGameMode::BeginPlayerDeath()
{
	if (RunState != ELostWithinRunState::Active)
	{
		return false;
	}
	RunState = ELostWithinRunState::Dying;
	return true;
}

bool ALostWithinGameMode::BeginEscape()
{
	if (RunState != ELostWithinRunState::Active)
	{
		return false;
	}
	RunState = ELostWithinRunState::Escaping;
	return true;
}

void ALostWithinGameMode::HandlePlayerKilled(AController* Victim, AActor* Killer)
{
	if (RunState == ELostWithinRunState::Lost || RunState == ELostWithinRunState::Escaped)
	{
		return;
	}
	RunState = ELostWithinRunState::Lost;
	UE_LOG(LogLostWithin, Log, TEXT("Run lost: %s was caught by %s."), *GetNameSafe(Victim), *GetNameSafe(Killer));
	ShowEndScreen(Victim, /*bEscaped=*/false);
}

void ALostWithinGameMode::HandlePlayerEscaped(AController* Player)
{
	if (RunState == ELostWithinRunState::Lost || RunState == ELostWithinRunState::Escaped)
	{
		return;
	}
	RunState = ELostWithinRunState::Escaped;
	UE_LOG(LogLostWithin, Log, TEXT("Run won: %s escaped through the door."), *GetNameSafe(Player));
	ShowEndScreen(Player, /*bEscaped=*/true);
}

void ALostWithinGameMode::ShowEndScreen(AController* Controller, bool bEscaped)
{
	ALostWithinPlayerController* PlayerController = Cast<ALostWithinPlayerController>(Controller);
	if (!PlayerController)
	{
		PlayerController = Cast<ALostWithinPlayerController>(GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr);
	}
	if (PlayerController)
	{
		PlayerController->ShowEndScreen(bEscaped);
	}
}
