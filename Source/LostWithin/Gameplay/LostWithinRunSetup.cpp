// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/LostWithinRunSetup.h"

#include "Components/ProximityFloatSpinComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Gameplay/RunSetupRules.h"
#include "Interaction/ItemSpawnPoint.h"
#include "Interaction/PickupItemActor.h"
#include "Interaction/ToolboxActor.h"
#include "LostWithin.h"
#include "Templates/Function.h"

namespace
{
	template <typename TActor>
	TArray<TActor*> GatherSortedByName(UWorld* World)
	{
		TArray<TActor*> Result;
		for (TActorIterator<TActor> It(World); It; ++It)
		{
			if (IsValid(*It))
			{
				Result.Add(*It);
			}
		}
		Result.Sort([](const TActor& A, const TActor& B) { return A.GetName() < B.GetName(); });
		return Result;
	}

	template <typename T>
	TArray<T> ShuffledCopy(const TArray<T>& Source, FRandomStream& Stream)
	{
		TArray<T> Result;
		for (const int32 Index : LostWithinRunRules::ShuffledIndices(Source.Num(), Stream))
		{
			Result.Add(Source[Index]);
		}
		return Result;
	}

	bool WorldHasItemDroppingCat(UWorld* World)
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->FindComponentByClass<UProximityFloatSpinComponent>())
			{
				return true;
			}
		}
		return false;
	}

	bool PresetForKey(FName KeyID, ELostWithinItemPreset& OutPreset)
	{
		if (KeyID == APickupItemActor::KeyItemID) { OutPreset = ELostWithinItemPreset::Key; return true; }
		if (KeyID == APickupItemActor::OldKeyItemID) { OutPreset = ELostWithinItemPreset::OldKey; return true; }
		if (KeyID == APickupItemActor::RoyalKeyItemID) { OutPreset = ELostWithinItemPreset::RoyalKey; return true; }
		return false;
	}

	APickupItemActor* SpawnPickup(UWorld* World, UClass* Class, const FVector& Location, FRandomStream& Stream,
		TFunctionRef<void(APickupItemActor&)> Configure)
	{
		const FTransform Transform(FRotator(0.0f, Stream.FRandRange(-180.0f, 180.0f), 0.0f), Location);
		APickupItemActor* Pickup = World->SpawnActorDeferred<APickupItemActor>(
			Class ? Class : APickupItemActor::StaticClass(), Transform, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Pickup)
		{
			Configure(*Pickup);
			Pickup->FinishSpawning(Transform);
		}
		return Pickup;
	}

	TArray<FString> ValidPhrases(const TArray<FString>& Configured, const TArray<FString>& Defaults, bool bHints)
	{
		const TArray<FString>& Source = Configured.IsEmpty() ? Defaults : Configured;
		TArray<FString> Valid;
		for (const FString& Phrase : Source)
		{
			const bool bOk = bHints ? LostWithinRunRules::IsValidHintTemplate(Phrase) : LostWithinRunRules::IsValidFlavorPhrase(Phrase);
			if (bOk)
			{
				Valid.Add(Phrase);
			}
			else
			{
				UE_LOG(LogLostWithin, Warning, TEXT("Ignoring %s phrase with stray numbers or placeholders: \"%s\""),
					bHints ? TEXT("hint") : TEXT("flavour"), *Phrase);
			}
		}
		return Valid.IsEmpty() ? (bHints ? LostWithinRunRules::DefaultHintTemplates() : LostWithinRunRules::DefaultFlavorPhrases()) : Valid;
	}

	enum class ELoot : uint8 { Battery, Note, Empty };

	ELoot RollLoot(const FLostWithinRunConfig& Config, FRandomStream& Stream)
	{
		const float Battery = FMath::Max(0.0f, Config.BatteryLootWeight);
		const float Note = FMath::Max(0.0f, Config.NoteLootWeight);
		const float Empty = FMath::Max(0.0f, Config.EmptyLootWeight);
		const float Total = Battery + Note + Empty;
		if (Total <= 0.0f)
		{
			return ELoot::Empty;
		}
		const float Roll = Stream.FRandRange(0.0f, Total);
		if (Roll < Battery)
		{
			return ELoot::Battery;
		}
		return Roll < Battery + Note ? ELoot::Note : ELoot::Empty;
	}
}

namespace LostWithinRunSetup
{
	FInventoryItem MakeNoteItem(const FString& Text)
	{
		FInventoryItem Item;
		Item.ItemID = APickupItemActor::MakeUniqueNoteItemID();
		Item.DisplayName = NSLOCTEXT("LostWithin", "NoteDisplayName", "Note");
		Item.Description = FText::FromString(Text);
		Item.Quantity = 1;
		Item.MaxStackSize = 1;
		Item.WorldMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(APickupItemActor::PaperNoteMeshAssetPath));
		return Item;
	}

	FLostWithinRunSummary SetupWorld(UWorld* World, const FLostWithinRunConfig& Config,
		const TArray<int32>& CombinationDigits, FRandomStream& Stream)
	{
		FLostWithinRunSummary Summary;
		Summary.Combination = LostWithinRunRules::CombinationToString(CombinationDigits);
		if (!World)
		{
			Summary.bAllHintsPlaced = false;
			return Summary;
		}

		TArray<AItemSpawnPoint*> KeyPoints;
		TArray<AItemSpawnPoint*> NotePoints;
		for (AItemSpawnPoint* Point : GatherSortedByName<AItemSpawnPoint>(World))
		{
			(Point->Kind == ELostWithinSpawnPointKind::Key ? KeyPoints : NotePoints).Add(Point);
		}
		const TArray<AToolboxActor*> Toolboxes = GatherSortedByName<AToolboxActor>(World);
		UClass* NoteClass = Config.NoteClass ? Config.NoteClass.Get() : APickupItemActor::StaticClass();

		// 1. Unique keys, one per key spawn point. The Fairy Tale Kitty Key only ever comes from the cat.
		TArray<FName> KeysToSpawn;
		for (const FName Key : Config.SpawnableKeys)
		{
			ELostWithinItemPreset Preset;
			if (Key != APickupItemActor::FairyTaleKittyKeyItemID && PresetForKey(Key, Preset))
			{
				KeysToSpawn.AddUnique(Key);
			}
		}
		if (KeysToSpawn.Num() > KeyPoints.Num())
		{
			UE_LOG(LogLostWithin, Warning, TEXT("Run setup: %d keys but only %d key spawn points; %d keys are left out."),
				KeysToSpawn.Num(), KeyPoints.Num(), KeysToSpawn.Num() - KeyPoints.Num());
		}
		const TArray<AItemSpawnPoint*> ShuffledKeyPoints = ShuffledCopy(KeyPoints, Stream);
		for (int32 Index = 0; Index < KeysToSpawn.Num() && Index < ShuffledKeyPoints.Num(); ++Index)
		{
			ELostWithinItemPreset Preset = ELostWithinItemPreset::Key;
			PresetForKey(KeysToSpawn[Index], Preset);
			const APickupItemActor* Key = SpawnPickup(World, APickupItemActor::StaticClass(),
				ShuffledKeyPoints[Index]->GetSpawnLocation(), Stream,
				[Preset](APickupItemActor& Pickup) { Pickup.ConfigureFromPreset(Preset, /*bForceApplyPresetScale=*/true); });
			if (Key)
			{
				Summary.SpawnedKeys.Add(KeysToSpawn[Index]);
			}
		}

		Summary.ObtainableKeys = Summary.SpawnedKeys;
		if (WorldHasItemDroppingCat(World))
		{
			Summary.ObtainableKeys.Add(APickupItemActor::FairyTaleKittyKeyItemID);
		}

		// 2. Toolbox locks: at most one toolbox per obtainable unique key.
		TArray<LostWithinRunRules::FToolboxLockRequest> Requests;
		for (const AToolboxActor* Toolbox : Toolboxes)
		{
			LostWithinRunRules::FToolboxLockRequest Request;
			Request.bAlwaysLocked = Toolbox->LockMode == ELostWithinToolboxLockMode::AlwaysLocked;
			Request.bNeverLocked = Toolbox->LockMode == ELostWithinToolboxLockMode::NeverLocked;
			Request.FixedKey = Toolbox->FixedKeyID;
			Requests.Add(Request);
		}
		const TArray<FName> Locks = LostWithinRunRules::PlanToolboxLocks(Requests, Summary.ObtainableKeys, Config.ToolboxLockChance, Stream);
		for (int32 Index = 0; Index < Toolboxes.Num(); ++Index)
		{
			Toolboxes[Index]->SetRequiredKey(Locks[Index]);
			Toolboxes[Index]->ClearContents();
			if (!Locks[Index].IsNone())
			{
				Summary.LockedToolboxes.Add(Toolboxes[Index]->GetName(), Locks[Index]);
			}
		}

		// 3. Hint notes: one per dial. Every toolbox is solvable because locks only use obtainable keys.
		const TArray<FString> HintTemplates = ShuffledCopy(ValidPhrases(Config.HintTemplates, LostWithinRunRules::DefaultHintTemplates(), true), Stream);
		const TArray<FString> FlavorPhrases = ShuffledCopy(ValidPhrases(Config.FlavorPhrases, LostWithinRunRules::DefaultFlavorPhrases(), false), Stream);

		TArray<AItemSpawnPoint*> FreeNotePoints = ShuffledCopy(NotePoints, Stream);
		TArray<AToolboxActor*> FreeToolboxes = ShuffledCopy(Toolboxes, Stream);

		for (int32 Dial = 0; Dial < CombinationDigits.Num(); ++Dial)
		{
			const FString Text = LostWithinRunRules::FormatHintPhrase(HintTemplates[Dial % HintTemplates.Num()], Dial, CombinationDigits[Dial]);
			Summary.HintTexts.Add(Text);

			const bool bUseToolbox = !FreeToolboxes.IsEmpty()
				&& (FreeNotePoints.IsEmpty() || Stream.FRand() < Config.HintNoteInToolboxChance);
			if (bUseToolbox)
			{
				FreeToolboxes.Pop(EAllowShrinking::No)->SetContents(NoteClass, MakeNoteItem(Text));
				++Summary.HintNotesInToolboxes;
			}
			else if (!FreeNotePoints.IsEmpty())
			{
				const FVector Location = FreeNotePoints.Pop(EAllowShrinking::No)->GetSpawnLocation();
				SpawnPickup(World, NoteClass, Location, Stream,
					[&Text](APickupItemActor& Pickup) { Pickup.ConfigureAsNote(FText::FromString(Text)); });
				++Summary.HintNotesAtPoints;
			}
			else
			{
				Summary.bAllHintsPlaced = false;
				UE_LOG(LogLostWithin, Error, TEXT("Run setup: no note spawn point or toolbox left for hint note %d; the door cannot be solved."), Dial + 1);
			}
		}

		// 4. Flavour notes at the remaining note points.
		for (int32 Index = 0; Index < Config.FlavorNoteCount && !FreeNotePoints.IsEmpty(); ++Index)
		{
			const FString& Text = FlavorPhrases[Index % FlavorPhrases.Num()];
			SpawnPickup(World, NoteClass, FreeNotePoints.Pop(EAllowShrinking::No)->GetSpawnLocation(), Stream,
				[&Text](APickupItemActor& Pickup) { Pickup.ConfigureAsNote(FText::FromString(Text)); });
			++Summary.FlavorNotesAtPoints;
		}

		// 5. Loot for the toolboxes still empty: a battery, a flavour note or nothing.
		for (AToolboxActor* Toolbox : FreeToolboxes)
		{
			switch (RollLoot(Config, Stream))
			{
			case ELoot::Battery:
				Toolbox->SetContents(APickupItemActor::StaticClass(), APickupItemActor::MakeBatteryInventoryItem(Stream.FRandRange(40.0f, 100.0f)));
				++Summary.ToolboxBatteries;
				break;
			case ELoot::Note:
				Toolbox->SetContents(NoteClass, MakeNoteItem(FlavorPhrases[Stream.RandRange(0, FlavorPhrases.Num() - 1)]));
				++Summary.ToolboxFlavorNotes;
				break;
			default:
				++Summary.EmptyToolboxes;
				break;
			}
		}

		FString LockText;
		for (const TPair<FString, FName>& Lock : Summary.LockedToolboxes)
		{
			LockText += FString::Printf(TEXT(" %s=%s"), *Lock.Key, *Lock.Value.ToString());
		}
		UE_LOG(LogLostWithin, Log,
			TEXT("Run setup: combination %s; keys spawned %d/%d; obtainable %d; locked toolboxes %d (%s ); hints at points %d, in toolboxes %d; flavour notes %d; toolbox loot: %d batteries, %d notes, %d empty."),
			*Summary.Combination, Summary.SpawnedKeys.Num(), KeysToSpawn.Num(), Summary.ObtainableKeys.Num(),
			Summary.LockedToolboxes.Num(), *LockText, Summary.HintNotesAtPoints, Summary.HintNotesInToolboxes,
			Summary.FlavorNotesAtPoints, Summary.ToolboxBatteries, Summary.ToolboxFlavorNotes, Summary.EmptyToolboxes);
		return Summary;
	}
}
