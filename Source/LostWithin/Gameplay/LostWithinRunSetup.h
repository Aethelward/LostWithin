// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Inventory/InventoryComponent.h"
#include "Templates/SubclassOf.h"

class APickupItemActor;
class UWorld;

/** Tuning for one run; ALostWithinGameMode fills it from its editable properties. */
struct LOSTWITHIN_API FLostWithinRunConfig
{
	/** Production story decorates the existing note slots; generic setup fixtures may leave it off. */
	bool bIncludeSubject09Story = false;
	/** Keys placed at key spawn points, one each. The Fairy Tale Kitty Key is always removed from this list. */
	TArray<FName> SpawnableKeys;
	float ToolboxLockChance = 0.5f;
	float BatteryLootWeight = 35.0f;
	float NoteLootWeight = 15.0f;
	float EmptyLootWeight = 50.0f;
	int32 FlavorNoteCount = 3;
	float HintNoteInToolboxChance = 0.35f;
	/** Empty uses LostWithinRunRules::DefaultHintTemplates / DefaultFlavorPhrases */
	TArray<FString> HintTemplates;
	TArray<FString> FlavorPhrases;
	/** Pickup class for notes (BP_Paper); null uses APickupItemActor configured as a note */
	TSubclassOf<APickupItemActor> NoteClass;
};

/** What the setup did, for logs and automation. */
struct LOSTWITHIN_API FLostWithinRunSummary
{
	FString Combination;
	TArray<FName> SpawnedKeys;
	TArray<FName> ObtainableKeys;
	/** Toolbox name -> key it needs */
	TMap<FString, FName> LockedToolboxes;
	TArray<FString> HintTexts;
	int32 HintNotesAtPoints = 0;
	int32 HintNotesInToolboxes = 0;
	int32 FlavorNotesAtPoints = 0;
	int32 ToolboxBatteries = 0;
	int32 ToolboxFlavorNotes = 0;
	int32 EmptyToolboxes = 0;
	int32 StoryNotesPlaced = 0;
	/** False when a hint note could not be placed anywhere */
	bool bAllHintsPlaced = true;
};

namespace LostWithinRunSetup
{
	/**
	 *  Populates World for a run: spawns the unique keys at key spawn points, locks toolboxes with obtainable keys,
	 *  places the three hint notes (at note points or in toolboxes) and the flavour notes, and rolls toolbox loot.
	 *  Spawn points and toolboxes are processed in name order, so a fixed stream seed gives the same run.
	 */
	LOSTWITHIN_API FLostWithinRunSummary SetupWorld(UWorld* World, const FLostWithinRunConfig& Config,
		const TArray<int32>& CombinationDigits, FRandomStream& Stream);

	/** A note inventory item with a unique ID and Text as its description */
	LOSTWITHIN_API FInventoryItem MakeNoteItem(const FString& Text);
}
