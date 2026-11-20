// Copyright Epic Games, Inc. All Rights Reserved.

#include "LostWithinGameMode.h"

#include "AI/MonsterSenseProfile.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Gameplay/RunSetupRules.h"
#include "Interaction/PickupItemActor.h"
#include "LostWithin.h"
#include "LostWithinPlayerController.h"
#include "UI/LostWithinMenu.h"
#include "Player/BodyContactComponent.h"

ALostWithinGameMode::ALostWithinGameMode()
{
	SpawnableKeys = { APickupItemActor::KeyItemID, APickupItemActor::OldKeyItemID, APickupItemActor::RoyalKeyItemID };
	NoteActorClass = TSoftClassPtr<APickupItemActor>(FSoftObjectPath(TEXT("/Game/Item/paper/BP_Paper.BP_Paper_C")));
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
	UBodyContactComponent::SetupMonstersForWorld(GetWorld());
	// One specialty sense per monster (also adds the BoneDog's near-sight and nose).
	UMonsterSenseProfile::ApplyForWorld(this);
}

FLostWithinRunConfig ALostWithinGameMode::MakeRunConfig() const
{
	FLostWithinRunConfig Config;
	Config.bIncludeSubject09Story = bIncludeSubject09Story;
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
