// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 *  Pure rules for one randomized run: the door combination, note phrases and toolbox locks.
 *  Kept free of UObjects so automation can check them without a world.
 */
namespace LostWithinRunRules
{
	/** Number of dials on the ScaryDoor padlock (BP_PadlockBase-3X). */
	inline constexpr int32 CombinationLength = 3;

	/** Returns CombinationLength digits in 0..9. Never all zero, because the dials start at 0-0-0. */
	LOSTWITHIN_API TArray<int32> MakeCombination(FRandomStream& Stream);

	/** Joins digits the way the padlock's CheckCombination builds its string ("307"). */
	LOSTWITHIN_API FString CombinationToString(const TArray<int32>& Digits);

	/** "zero".."nine"; empty for anything else. */
	LOSTWITHIN_API FString NumberWord(int32 Digit);

	/** "first", "second", "third" for dial 0..2; empty for anything else. */
	LOSTWITHIN_API FString OrdinalWord(int32 DialIndex);

	/** Built-in hint templates. Placeholders: {number}, {ordinal}, {Ordinal} (capitalized). */
	LOSTWITHIN_API const TArray<FString>& DefaultHintTemplates();

	/** Built-in flavour phrases: no number or ordinal words. */
	LOSTWITHIN_API const TArray<FString>& DefaultFlavorPhrases();

	/** Fills a hint template with the dial's ordinal word and the digit's number word. */
	LOSTWITHIN_API FString FormatHintPhrase(const FString& Template, int32 DialIndex, int32 Digit);

	/** Whole-word, case-insensitive number words ("zero".."nine") found in Text, in order. */
	LOSTWITHIN_API TArray<FString> FindNumberWords(const FString& Text);

	/** Whole-word, case-insensitive dial ordinals ("first", "second", "third") found in Text, in order. */
	LOSTWITHIN_API TArray<FString> FindOrdinalWords(const FString& Text);

	/** True when Text contains no digits, number words or dial ordinals. */
	LOSTWITHIN_API bool IsValidFlavorPhrase(const FString& Text);

	/** True when the template has {number} and an ordinal placeholder, and no other digits or number words. */
	LOSTWITHIN_API bool IsValidHintTemplate(const FString& Template);

	/** 0..Count-1 in random order. */
	LOSTWITHIN_API TArray<int32> ShuffledIndices(int32 Count, FRandomStream& Stream);

	/** How a toolbox may be locked this run. */
	struct FToolboxLockRequest
	{
		bool bAlwaysLocked = false;
		bool bNeverLocked = false;
		/** A designer-chosen key; used only when it is obtainable this run. */
		FName FixedKey = NAME_None;
	};

	/**
	 *  Assigns each toolbox the key it needs (NAME_None = unlocked).
	 *  Each obtainable key locks at most one toolbox, so the number of locked toolboxes never exceeds the number of
	 *  unique obtainable keys. Fixed keys go first, then always-locked boxes, then random boxes with LockChance.
	 */
	LOSTWITHIN_API TArray<FName> PlanToolboxLocks(
		const TArray<FToolboxLockRequest>& Toolboxes,
		const TArray<FName>& ObtainableKeys,
		float LockChance,
		FRandomStream& Stream);
}
