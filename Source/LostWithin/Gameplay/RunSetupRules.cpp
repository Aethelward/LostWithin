// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/RunSetupRules.h"

namespace
{
	const TCHAR* const GNumberWords[] = {
		TEXT("zero"), TEXT("one"), TEXT("two"), TEXT("three"), TEXT("four"),
		TEXT("five"), TEXT("six"), TEXT("seven"), TEXT("eight"), TEXT("nine")
	};

	const TCHAR* const GOrdinalWords[] = { TEXT("first"), TEXT("second"), TEXT("third") };

	static_assert(static_cast<int32>(UE_ARRAY_COUNT(GOrdinalWords)) == LostWithinRunRules::CombinationLength,
		"One ordinal word per padlock dial");

	FString Capitalize(const FString& Word)
	{
		if (Word.IsEmpty())
		{
			return Word;
		}
		FString Result = Word;
		Result[0] = FChar::ToUpper(Result[0]);
		return Result;
	}

	/** Lower-cased alphabetic words of Text, in order. */
	TArray<FString> SplitWords(const FString& Text)
	{
		TArray<FString> Words;
		FString Current;
		for (const TCHAR Character : Text)
		{
			if (FChar::IsAlpha(Character))
			{
				Current.AppendChar(FChar::ToLower(Character));
			}
			else if (!Current.IsEmpty())
			{
				Words.Add(MoveTemp(Current));
				Current.Reset();
			}
		}
		if (!Current.IsEmpty())
		{
			Words.Add(MoveTemp(Current));
		}
		return Words;
	}

	template <int32 N>
	TArray<FString> FindWordsFrom(const FString& Text, const TCHAR* const (&Vocabulary)[N])
	{
		TArray<FString> Found;
		for (const FString& Word : SplitWords(Text))
		{
			for (const TCHAR* Candidate : Vocabulary)
			{
				if (Word == Candidate)
				{
					Found.Add(Word);
					break;
				}
			}
		}
		return Found;
	}

	bool ContainsDigitCharacter(const FString& Text)
	{
		for (const TCHAR Character : Text)
		{
			if (FChar::IsDigit(Character))
			{
				return true;
			}
		}
		return false;
	}

	FString ReplaceExact(const FString& Source, const TCHAR* From, const FString& To)
	{
		return Source.Replace(From, *To, ESearchCase::CaseSensitive);
	}
}

namespace LostWithinRunRules
{
	TArray<int32> MakeCombination(FRandomStream& Stream)
	{
		TArray<int32> Digits;
		do
		{
			Digits.Reset();
			for (int32 Index = 0; Index < CombinationLength; ++Index)
			{
				Digits.Add(Stream.RandRange(0, 9));
			}
		}
		while (!Digits.ContainsByPredicate([](int32 Digit) { return Digit != 0; }));
		return Digits;
	}

	FString CombinationToString(const TArray<int32>& Digits)
	{
		FString Result;
		for (const int32 Digit : Digits)
		{
			Result += FString::FromInt(Digit);
		}
		return Result;
	}

	FString NumberWord(int32 Digit)
	{
		return (Digit >= 0 && Digit < static_cast<int32>(UE_ARRAY_COUNT(GNumberWords))) ? FString(GNumberWords[Digit]) : FString();
	}

	FString OrdinalWord(int32 DialIndex)
	{
		return (DialIndex >= 0 && DialIndex < static_cast<int32>(UE_ARRAY_COUNT(GOrdinalWords))) ? FString(GOrdinalWords[DialIndex]) : FString();
	}

	const TArray<FString>& DefaultHintTemplates()
	{
		static const TArray<FString> Templates = {
			TEXT("The {ordinal} dial was set to {number}. I watched him do it."),
			TEXT("{Ordinal} skull: {number}. Do not tell the dog."),
			TEXT("They whispered \"{number}\" whenever they turned the {ordinal} wheel."),
			TEXT("I scratched it into the wall so I would not forget. {Ordinal}: {number}."),
			TEXT("If the door asks, the {ordinal} number is {number}."),
			TEXT("The {ordinal} lock remembers {number}. It always remembers."),
			TEXT("Count the teeth on the {ordinal} skull. {Number}, and not a tooth more.")
		};
		return Templates;
	}

	const TArray<FString>& DefaultFlavorPhrases()
	{
		static const TArray<FString> Phrases = {
			TEXT("It hears you when you run. Walk, and pray it is not hungry."),
			TEXT("The cat knows the way out. Follow the music."),
			TEXT("Do not let them touch you. Whatever happens, do not let them touch you."),
			TEXT("The walls moved again last night. I am sure of it."),
			TEXT("Something was dragging itself along the hedge. I did not look."),
			TEXT("The door with the skull lock is the only way out."),
			TEXT("My flashlight is dying. I can hear the dark breathing."),
			TEXT("Do not drop anything. They come running at the sound."),
			TEXT("I hid the keys where the dead would not look."),
			TEXT("Every path I take leads back here."),
			TEXT("The dog has no eyes, but it found me anyway."),
			TEXT("The doll was not in that corner a moment ago.")
		};
		return Phrases;
	}

	FString FormatHintPhrase(const FString& Template, int32 DialIndex, int32 Digit)
	{
		const FString Ordinal = OrdinalWord(DialIndex);
		const FString Number = NumberWord(Digit);
		FString Result = ReplaceExact(Template, TEXT("{Ordinal}"), Capitalize(Ordinal));
		Result = ReplaceExact(Result, TEXT("{ordinal}"), Ordinal);
		Result = ReplaceExact(Result, TEXT("{Number}"), Capitalize(Number));
		Result = ReplaceExact(Result, TEXT("{number}"), Number);
		return Result;
	}

	TArray<FString> FindNumberWords(const FString& Text)
	{
		return FindWordsFrom(Text, GNumberWords);
	}

	TArray<FString> FindOrdinalWords(const FString& Text)
	{
		return FindWordsFrom(Text, GOrdinalWords);
	}

	bool IsValidFlavorPhrase(const FString& Text)
	{
		return !Text.TrimStartAndEnd().IsEmpty()
			&& !ContainsDigitCharacter(Text)
			&& FindNumberWords(Text).IsEmpty()
			&& FindOrdinalWords(Text).IsEmpty();
	}

	bool IsValidHintTemplate(const FString& Template)
	{
		const int32 NumberPlaceholders = (Template.Contains(TEXT("{number}"), ESearchCase::CaseSensitive) ? 1 : 0)
			+ (Template.Contains(TEXT("{Number}"), ESearchCase::CaseSensitive) ? 1 : 0);
		const bool bHasOrdinal = Template.Contains(TEXT("{ordinal}"), ESearchCase::CaseSensitive)
			|| Template.Contains(TEXT("{Ordinal}"), ESearchCase::CaseSensitive);
		if (NumberPlaceholders != 1 || !bHasOrdinal)
		{
			return false;
		}

		FString Stripped = ReplaceExact(Template, TEXT("{Ordinal}"), FString());
		Stripped = ReplaceExact(Stripped, TEXT("{ordinal}"), FString());
		Stripped = ReplaceExact(Stripped, TEXT("{Number}"), FString());
		Stripped = ReplaceExact(Stripped, TEXT("{number}"), FString());
		return !ContainsDigitCharacter(Stripped)
			&& FindNumberWords(Stripped).IsEmpty()
			&& FindOrdinalWords(Stripped).IsEmpty();
	}

	TArray<int32> ShuffledIndices(int32 Count, FRandomStream& Stream)
	{
		TArray<int32> Indices;
		Indices.Reserve(FMath::Max(0, Count));
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Indices.Add(Index);
		}
		for (int32 Index = Indices.Num() - 1; Index > 0; --Index)
		{
			Indices.Swap(Index, Stream.RandRange(0, Index));
		}
		return Indices;
	}

	TArray<FName> PlanToolboxLocks(
		const TArray<FToolboxLockRequest>& Toolboxes,
		const TArray<FName>& ObtainableKeys,
		float LockChance,
		FRandomStream& Stream)
	{
		TArray<FName> Result;
		Result.Init(NAME_None, Toolboxes.Num());

		TArray<FName> KeyPool;
		for (const FName Key : ObtainableKeys)
		{
			if (!Key.IsNone())
			{
				KeyPool.AddUnique(Key);
			}
		}
		{
			const TArray<int32> Order = ShuffledIndices(KeyPool.Num(), Stream);
			TArray<FName> Shuffled;
			for (const int32 Index : Order)
			{
				Shuffled.Add(KeyPool[Index]);
			}
			KeyPool = MoveTemp(Shuffled);
		}

		// 1. Designer-fixed keys that are obtainable this run.
		for (int32 Index = 0; Index < Toolboxes.Num(); ++Index)
		{
			const FToolboxLockRequest& Request = Toolboxes[Index];
			if (!Request.bNeverLocked && !Request.FixedKey.IsNone() && KeyPool.Remove(Request.FixedKey) > 0)
			{
				Result[Index] = Request.FixedKey;
			}
		}

		// 2. Always-locked toolboxes without a usable fixed key.
		for (int32 Index = 0; Index < Toolboxes.Num(); ++Index)
		{
			const FToolboxLockRequest& Request = Toolboxes[Index];
			if (Request.bAlwaysLocked && !Request.bNeverLocked && Result[Index].IsNone() && !KeyPool.IsEmpty())
			{
				Result[Index] = KeyPool.Pop(EAllowShrinking::No);
			}
		}

		// 3. Random toolboxes, in random order, while keys remain.
		const float Chance = FMath::Clamp(LockChance, 0.0f, 1.0f);
		for (const int32 Index : ShuffledIndices(Toolboxes.Num(), Stream))
		{
			const FToolboxLockRequest& Request = Toolboxes[Index];
			if (KeyPool.IsEmpty())
			{
				break;
			}
			if (Request.bAlwaysLocked || Request.bNeverLocked || !Result[Index].IsNone() || !Request.FixedKey.IsNone())
			{
				continue;
			}
			if (Stream.FRand() < Chance)
			{
				Result[Index] = KeyPool.Pop(EAllowShrinking::No);
			}
		}

		return Result;
	}
}
