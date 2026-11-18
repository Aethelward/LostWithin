// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Gameplay/LostWithinRunSetup.h"
#include "LostWithinGameMode.generated.h"

class APickupItemActor;

/** Where the current run stands. */
UENUM(BlueprintType)
enum class ELostWithinRunState : uint8
{
	Active,
	/** A monster touched the player; the death camera is playing */
	Dying,
	/** The door is swinging open */
	Escaping,
	Lost,
	Escaped
};

/**
 *  First person game mode: sets up a random, solvable run (door combination, keys, toolbox locks and contents,
 *  notes) and ends it with a death or an escape.
 */
UCLASS(abstract)
class ALostWithinGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ALostWithinGameMode();

	/** Fixed seed for a repeatable run; 0 picks a new random run every time */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Run")
	int32 RunSeed = 0;

	/** Keys placed once each at key spawn points (the Fairy Tale Kitty Key is never placed; only the cat drops it) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Run|Keys")
	TArray<FName> SpawnableKeys;

	/** Chance that a Random-mode toolbox is locked, while unique keys remain */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Run|Toolboxes", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ToolboxLockChance = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Run|Toolboxes", meta = (ClampMin = "0.0"))
	float BatteryLootWeight = 35.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Run|Toolboxes", meta = (ClampMin = "0.0"))
	float NoteLootWeight = 15.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Run|Toolboxes", meta = (ClampMin = "0.0"))
	float EmptyLootWeight = 50.0f;

	/** Flavour notes placed at note spawn points left over after the hint notes */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Run|Notes", meta = (ClampMin = "0"))
	int32 FlavorNoteCount = 3;

	/** Chance that a hint note goes into a toolbox instead of onto a note spawn point */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Run|Notes", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float HintNoteInToolboxChance = 0.35f;

	/** Hint phrases with {number} and {ordinal} (or {Number}/{Ordinal}); empty uses the built-in set */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Run|Notes", meta = (MultiLine = "true"))
	TArray<FString> HintTemplates;

	/** Flavour phrases without numbers or dial ordinals; empty uses the built-in set */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Run|Notes", meta = (MultiLine = "true"))
	TArray<FString> FlavorPhrases;

	/** Paper note Blueprint (BP_Paper, a PickupItemActor) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Run|Notes")
	TSoftClassPtr<APickupItemActor> NoteActorClass;

	/** This run's door combination as the padlock compares it ("307") */
	UFUNCTION(BlueprintPure, Category = "Run")
	FString GetDoorCombination() const;

	const TArray<int32>& GetCombinationDigits() const { return CombinationDigits; }

	UFUNCTION(BlueprintPure, Category = "Run")
	ELostWithinRunState GetRunState() const { return RunState; }

	UFUNCTION(BlueprintPure, Category = "Run")
	bool IsRunActive() const { return RunState == ELostWithinRunState::Active; }

	/** Active -> Dying; false when the run is already ending */
	bool BeginPlayerDeath();

	/** Active -> Escaping; false when the run is already ending */
	bool BeginEscape();

	/** Called when the death camera has finished: shows the "You died" screen */
	void HandlePlayerKilled(AController* Victim, AActor* Killer);

	/** Called when the door has swung open: shows the "You escaped" screen */
	void HandlePlayerEscaped(AController* Player);

	const FLostWithinRunSummary& GetRunSummary() const { return RunSummary; }

protected:

	virtual void StartPlay() override;

private:

	FLostWithinRunConfig MakeRunConfig() const;
	void ShowEndScreen(AController* Controller, bool bEscaped);

	FRandomStream RunStream;
	TArray<int32> CombinationDigits;
	ELostWithinRunState RunState = ELostWithinRunState::Active;
	FLostWithinRunSummary RunSummary;
};
