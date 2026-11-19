#pragma once

#include "CoreMinimal.h"
#include "Inventory/InventoryComponent.h"

/** Authored story is separate from the legacy, deliberately number-free flavour pool. */
namespace LostWithinStory
{
	LOSTWITHIN_API extern const FName Orientation;
	LOSTWITHIN_API extern const FName Warning;
	LOSTWITHIN_API extern const FName Survival;
	LOSTWITHIN_API extern const FName Report;
	LOSTWITHIN_API extern const FName Journal;
	LOSTWITHIN_API extern const FName FinalEntry;

	struct LOSTWITHIN_API FNote
	{
		FText Title;
		FText Body;
		FText Reaction;
		int32 DigitIndex = INDEX_NONE;
	};

	LOSTWITHIN_API FName NoteForDigit(int32 DigitIndex);
	LOSTWITHIN_API FNote FormatNote(FName ID, const TArray<int32>& Digits);
	/** Decorates an existing note without replacing its unique inventory identity or generated clue. */
	LOSTWITHIN_API void ApplyToItem(FInventoryItem& Item, FName ID, const TArray<int32>& Digits);
}
