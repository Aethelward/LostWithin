// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "AI/MonsterAIComponent.h"
#include "AI/MonsterHearingComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/ProximityFloatSpinComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "Gameplay/LostWithinNoiseSubsystem.h"
#include "Gameplay/LostWithinRunSetup.h"
#include "Gameplay/RunSetupRules.h"
#include "Interaction/EscapeDoorComponent.h"
#include "Interaction/InteractableInterface.h"
#include "Interaction/ItemSpawnPoint.h"
#include "Interaction/PickupItemActor.h"
#include "Interaction/ToolboxActor.h"
#include "Inventory/InventoryComponent.h"
#include "Misc/AutomationTest.h"
#include "Player/PlayerDeathComponent.h"
#include "UObject/UnrealType.h"

#define LOSTWITHIN_GAMEPLAY_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// Helper names are unique to this file so unity builds cannot clash with other test files' helpers.
namespace
{
	struct FScopedGameplayTestWorld
	{
		UWorld* World = nullptr;

		FScopedGameplayTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			if (World && GEngine)
			{
				FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
				Context.SetCurrentWorld(World);
				World->InitializeActorsForPlay(FURL());
				World->BeginPlay();
				if (AWorldSettings* Settings = World->GetWorldSettings())
				{
					Settings->NotifyBeginPlay();
				}
			}
		}

		~FScopedGameplayTestWorld()
		{
			if (World)
			{
				World->EndPlay(EEndPlayReason::Quit);
				World->DestroyWorld(false);
				if (GEngine)
				{
					GEngine->DestroyWorldContext(World);
				}
				World = nullptr;
			}
			CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
		}
	};

	AActor* SpawnGameplayRootedActor(UWorld* World, const FVector& Location)
	{
		const FTransform Transform(Location);
		AActor* Actor = World->SpawnActorDeferred<AActor>(AActor::StaticClass(), Transform, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("Root"));
		Root->SetMobility(EComponentMobility::Movable);
		Actor->SetRootComponent(Root);
		Root->SetWorldTransform(Transform);
		Root->RegisterComponent();
		Actor->FinishSpawning(Transform);
		return Actor;
	}

	/** A plain actor with an inventory, standing in for the player in interaction tests. */
	UInventoryComponent* SpawnGameplayInventoryHolder(UWorld* World, const FVector& Location, AActor*& OutActor)
	{
		OutActor = SpawnGameplayRootedActor(World, Location);
		UInventoryComponent* Inventory = NewObject<UInventoryComponent>(OutActor, TEXT("Inventory"));
		Inventory->RegisterComponent();
		return Inventory;
	}

	struct FGameplayMonster
	{
		ACharacter* Actor = nullptr;
		UMonsterAIComponent* AI = nullptr;
		UMonsterHearingComponent* Hearing = nullptr;
	};

	FGameplayMonster SpawnGameplayMonster(UWorld* World, const FVector& Location, bool bWithHearing)
	{
		FGameplayMonster Result;
		const FTransform Transform(Location);
		Result.Actor = World->SpawnActorDeferred<ACharacter>(ACharacter::StaticClass(), Transform, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Result.Actor)
		{
			return Result;
		}
		Result.Actor->AutoPossessAI = EAutoPossessAI::Disabled;
		Result.AI = NewObject<UMonsterAIComponent>(Result.Actor, TEXT("CatReactionAI"));
		Result.AI->RegisterComponent();
		Result.Actor->FinishSpawning(Transform);
		if (bWithHearing)
		{
			Result.Hearing = NewObject<UMonsterHearingComponent>(Result.Actor, TEXT("Hearing"));
			Result.Hearing->RegisterComponent();
		}
		return Result;
	}

	AItemSpawnPoint* SpawnGameplayPoint(UWorld* World, ELostWithinSpawnPointKind Kind, const FVector& Location, const TCHAR* Name)
	{
		FActorSpawnParameters Params;
		Params.Name = FName(Name);
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AItemSpawnPoint* Point = World->SpawnActor<AItemSpawnPoint>(AItemSpawnPoint::StaticClass(), Location, FRotator::ZeroRotator, Params);
		if (Point)
		{
			Point->Kind = Kind;
		}
		return Point;
	}

	AToolboxActor* SpawnGameplayToolbox(UWorld* World, const FVector& Location, const TCHAR* Name)
	{
		FActorSpawnParameters Params;
		Params.Name = FName(Name);
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<AToolboxActor>(AToolboxActor::StaticClass(), Location, FRotator::ZeroRotator, Params);
	}

	TArray<APickupItemActor*> GatherGameplayPickups(UWorld* World)
	{
		TArray<APickupItemActor*> Pickups;
		for (TActorIterator<APickupItemActor> It(World); It; ++It)
		{
			if (IsValid(*It))
			{
				Pickups.Add(*It);
			}
		}
		return Pickups;
	}

	void AddGameplayCat(UWorld* World, const FVector& Location)
	{
		AActor* CatActor = SpawnGameplayRootedActor(World, Location);
		UProximityFloatSpinComponent* Cat = NewObject<UProximityFloatSpinComponent>(CatActor, TEXT("FloatSpin"));
		Cat->bAutoSetHierarchyMovable = false;
		Cat->bDarkenSkyOnSpinWindow = false;
		Cat->bEnableDiscoEnvironmentOnSpinWindow = false;
		Cat->bEnableDirectedPath = false;
		Cat->bLoopMusic = false;
		Cat->InteractionSoundSearchPaths.Reset();
		Cat->RegisterComponent();
	}

	void TickGameplayComponent(UActorComponent* Component, float DeltaTime)
	{
		++GFrameCounter;
		// Dispatch through the public actor-component API, as the engine tick does.
		static_cast<UActorComponent*>(Component)->TickComponent(DeltaTime, LEVELTICK_All, nullptr);
	}
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLostWithinRunRulesTest, "LostWithin.Gameplay.RunRules", LOSTWITHIN_GAMEPLAY_TEST_FLAGS)

bool FLostWithinRunRulesTest::RunTest(const FString& Parameters)
{
	using namespace LostWithinRunRules;

	// Combination: three digits 0-9, never 0-0-0 (the dials start there).
	bool bAllCombinationsValid = true;
	for (int32 Seed = 1; Seed <= 300; ++Seed)
	{
		FRandomStream Stream(Seed);
		const TArray<int32> Digits = MakeCombination(Stream);
		const bool bValid = Digits.Num() == CombinationLength
			&& !Digits.ContainsByPredicate([](int32 Digit) { return Digit < 0 || Digit > 9; })
			&& Digits.ContainsByPredicate([](int32 Digit) { return Digit != 0; });
		bAllCombinationsValid &= bValid;
	}
	TestTrue(TEXT("300 combinations are three digits 0-9 and never 000"), bAllCombinationsValid);
	TestEqual(TEXT("Combination string matches the padlock's digit concatenation"), CombinationToString({ 3, 0, 7 }), FString(TEXT("307")));

	TestEqual(TEXT("Number word for 7"), NumberWord(7), FString(TEXT("seven")));
	TestEqual(TEXT("Number word for 0"), NumberWord(0), FString(TEXT("zero")));
	TestTrue(TEXT("No number word outside 0-9"), NumberWord(10).IsEmpty());
	TestEqual(TEXT("Ordinal of dial 1"), OrdinalWord(1), FString(TEXT("second")));

	// Every built-in hint names exactly its digit (as a word) and its dial (as an ordinal).
	for (const FString& Template : DefaultHintTemplates())
	{
		TestTrue(*FString::Printf(TEXT("Hint template is valid: %s"), *Template), IsValidHintTemplate(Template));
		for (int32 Dial = 0; Dial < CombinationLength; ++Dial)
		{
			for (int32 Digit = 0; Digit <= 9; ++Digit)
			{
				const FString Phrase = FormatHintPhrase(Template, Dial, Digit);
				const TArray<FString> Numbers = FindNumberWords(Phrase);
				const TArray<FString> Ordinals = FindOrdinalWords(Phrase);
				if (Numbers.Num() != 1 || Numbers[0] != NumberWord(Digit) || Ordinals.Num() != 1 || Ordinals[0] != OrdinalWord(Dial)
					|| Phrase.Contains(TEXT("{")))
				{
					AddError(FString::Printf(TEXT("Hint phrase does not carry exactly '%s' and '%s': %s"),
						*NumberWord(Digit), *OrdinalWord(Dial), *Phrase));
				}
			}
		}
	}

	for (const FString& Phrase : DefaultFlavorPhrases())
	{
		TestTrue(*FString::Printf(TEXT("Flavour phrase has no number or ordinal: %s"), *Phrase), IsValidFlavorPhrase(Phrase));
	}
	TestFalse(TEXT("A flavour phrase with 'one' is rejected"), IsValidFlavorPhrase(TEXT("No one leaves.")));
	TestFalse(TEXT("A flavour phrase with a digit is rejected"), IsValidFlavorPhrase(TEXT("Room 4.")));
	TestFalse(TEXT("A hint template without {number} is rejected"), IsValidHintTemplate(TEXT("The {ordinal} dial.")));
	TestFalse(TEXT("A hint template with a stray number word is rejected"), IsValidHintTemplate(TEXT("Two of them: {ordinal} is {number}.")));
	TestFalse(TEXT("Number words are matched as whole words only"), FindNumberWords(TEXT("Someone was gone")).Num() > 0);

	// Toolbox locks: capped by unique keys, each key used once, fixed and never-locked modes respected.
	const TArray<FName> FourKeys = { APickupItemActor::KeyItemID, APickupItemActor::OldKeyItemID,
		APickupItemActor::RoyalKeyItemID, APickupItemActor::FairyTaleKittyKeyItemID };
	{
		TArray<FToolboxLockRequest> Six;
		Six.SetNum(6);
		FRandomStream Stream(42);
		const TArray<FName> Locks = PlanToolboxLocks(Six, FourKeys, 1.0f, Stream);
		TSet<FName> Used;
		int32 Locked = 0;
		for (const FName Lock : Locks)
		{
			if (!Lock.IsNone())
			{
				++Locked;
				TestFalse(TEXT("Each key locks at most one toolbox"), Used.Contains(Lock));
				Used.Add(Lock);
			}
		}
		TestEqual(TEXT("With chance 1, locks equal the unique key count"), Locked, 4);

		FRandomStream ZeroStream(42);
		const TArray<FName> NoLocks = PlanToolboxLocks(Six, FourKeys, 0.0f, ZeroStream);
		TestFalse(TEXT("With chance 0 no random toolbox is locked"), NoLocks.ContainsByPredicate([](FName Lock) { return !Lock.IsNone(); }));
	}
	{
		TArray<FToolboxLockRequest> Requests;
		Requests.SetNum(4);
		Requests[0].bAlwaysLocked = true;
		Requests[0].FixedKey = APickupItemActor::OldKeyItemID;
		Requests[1].bNeverLocked = true;
		Requests[2].FixedKey = TEXT("NotAKey");
		FRandomStream Stream(7);
		const TArray<FName> Locks = PlanToolboxLocks(Requests, FourKeys, 1.0f, Stream);
		TestTrue(TEXT("Fixed obtainable key is used"), Locks[0] == APickupItemActor::OldKeyItemID);
		TestTrue(TEXT("Never-locked toolbox stays open"), Locks[1].IsNone());
		TestTrue(TEXT("A fixed key that is not obtainable leaves the toolbox open"), Locks[2].IsNone());
		TestTrue(TEXT("The free random toolbox gets a different key"), !Locks[3].IsNone() && Locks[3] != APickupItemActor::OldKeyItemID);
	}
	{
		TArray<FToolboxLockRequest> AlwaysLocked;
		AlwaysLocked.SetNum(5);
		for (FToolboxLockRequest& Request : AlwaysLocked)
		{
			Request.bAlwaysLocked = true;
		}
		FRandomStream Stream(3);
		const TArray<FName> Locks = PlanToolboxLocks(AlwaysLocked, { APickupItemActor::KeyItemID, APickupItemActor::RoyalKeyItemID }, 1.0f, Stream);
		int32 Locked = 0;
		for (const FName Lock : Locks)
		{
			Locked += Lock.IsNone() ? 0 : 1;
		}
		TestEqual(TEXT("Always-locked toolboxes are still capped by the two keys"), Locked, 2);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLostWithinRunSetupTest, "LostWithin.Gameplay.RunSetup", LOSTWITHIN_GAMEPLAY_TEST_FLAGS)

bool FLostWithinRunSetupTest::RunTest(const FString& Parameters)
{
	for (const int32 Seed : { 11, 23, 57 })
	{
		FScopedGameplayTestWorld TestWorld;
		UWorld* World = TestWorld.World;
		if (!TestNotNull(TEXT("Test world"), World))
		{
			return false;
		}

		for (int32 Index = 0; Index < 4; ++Index)
		{
			SpawnGameplayPoint(World, ELostWithinSpawnPointKind::Key, FVector(Index * 300.0f, 0.0f, 100.0f), *FString::Printf(TEXT("KeyPoint_%d"), Index));
		}
		for (int32 Index = 0; Index < 3; ++Index)
		{
			SpawnGameplayPoint(World, ELostWithinSpawnPointKind::Note, FVector(Index * 300.0f, 900.0f, 100.0f), *FString::Printf(TEXT("NotePoint_%d"), Index));
		}
		TArray<AToolboxActor*> Toolboxes;
		for (int32 Index = 0; Index < 4; ++Index)
		{
			Toolboxes.Add(SpawnGameplayToolbox(World, FVector(Index * 300.0f, 1800.0f, 0.0f), *FString::Printf(TEXT("Toolbox_%d"), Index)));
		}

		FLostWithinRunConfig Config;
		Config.SpawnableKeys = { APickupItemActor::KeyItemID, APickupItemActor::OldKeyItemID,
			APickupItemActor::RoyalKeyItemID, APickupItemActor::FairyTaleKittyKeyItemID };
		Config.ToolboxLockChance = 1.0f;
		FRandomStream Stream(Seed);
		const TArray<int32> Digits = LostWithinRunRules::MakeCombination(Stream);
		const FLostWithinRunSummary Summary = LostWithinRunSetup::SetupWorld(World, Config, Digits, Stream);

		// Keys: one of each spawnable key, never the kitty key.
		TMap<FName, int32> KeyCounts;
		int32 NotePickups = 0;
		for (const APickupItemActor* Pickup : GatherGameplayPickups(World))
		{
			if (Pickup->IsNoteItem())
			{
				++NotePickups;
				TestFalse(TEXT("World notes never simulate physics"), Pickup->IsSimulatingItemPhysics());
			}
			else if (APickupItemActor::IsKeyItemID(Pickup->ItemData.ItemID))
			{
				KeyCounts.FindOrAdd(Pickup->ItemData.ItemID)++;
			}
		}
		TestEqual(TEXT("One Key"), KeyCounts.FindRef(APickupItemActor::KeyItemID), 1);
		TestEqual(TEXT("One Old Key"), KeyCounts.FindRef(APickupItemActor::OldKeyItemID), 1);
		TestEqual(TEXT("One Royal Key"), KeyCounts.FindRef(APickupItemActor::RoyalKeyItemID), 1);
		TestEqual(TEXT("The Fairy Tale Kitty Key never spawns at a point"), KeyCounts.FindRef(APickupItemActor::FairyTaleKittyKeyItemID), 0);
		TestFalse(TEXT("Without a cat the kitty key is not obtainable"), Summary.ObtainableKeys.Contains(APickupItemActor::FairyTaleKittyKeyItemID));

		// Locks: capped by obtainable keys, distinct, each obtainable.
		TSet<FName> UsedKeys;
		for (const AToolboxActor* Toolbox : Toolboxes)
		{
			if (Toolbox->IsLocked())
			{
				TestFalse(TEXT("Each key fits one toolbox"), UsedKeys.Contains(Toolbox->GetRequiredKey()));
				UsedKeys.Add(Toolbox->GetRequiredKey());
				TestTrue(TEXT("A locked toolbox needs an obtainable key"), Summary.ObtainableKeys.Contains(Toolbox->GetRequiredKey()));
			}
		}
		TestEqual(TEXT("Locked toolboxes equal the obtainable unique keys (chance 1)"), UsedKeys.Num(), Summary.ObtainableKeys.Num());

		// Hints: one per dial, the right number word and ordinal each.
		TestTrue(TEXT("All three hint notes were placed"), Summary.bAllHintsPlaced);
		TestEqual(TEXT("Three hint notes"), Summary.HintNotesAtPoints + Summary.HintNotesInToolboxes, 3);
		for (int32 Dial = 0; Dial < Summary.HintTexts.Num(); ++Dial)
		{
			const TArray<FString> Numbers = LostWithinRunRules::FindNumberWords(Summary.HintTexts[Dial]);
			TestTrue(*FString::Printf(TEXT("Hint %d carries its digit as a word"), Dial + 1),
				Numbers.Num() == 1 && Numbers[0] == LostWithinRunRules::NumberWord(Digits[Dial]));
			TestTrue(*FString::Printf(TEXT("Hint %d names its dial"), Dial + 1),
				LostWithinRunRules::FindOrdinalWords(Summary.HintTexts[Dial]).Contains(LostWithinRunRules::OrdinalWord(Dial)));
		}
		TestEqual(TEXT("Notes in the world match the notes placed at points"), NotePickups, Summary.HintNotesAtPoints + Summary.FlavorNotesAtPoints);

		int32 ToolboxHints = 0;
		for (const AToolboxActor* Toolbox : Toolboxes)
		{
			if (Toolbox->HasContents() && APickupItemActor::IsNoteItemID(Toolbox->GetContentsItem().ItemID)
				&& LostWithinRunRules::FindNumberWords(Toolbox->GetContentsItem().Description.ToString()).Num() == 1)
			{
				++ToolboxHints;
			}
			TestFalse(TEXT("Toolboxes never hold keys"), Toolbox->HasContents() && APickupItemActor::IsKeyItemID(Toolbox->GetContentsItem().ItemID));
		}
		TestEqual(TEXT("Hint notes inside toolboxes match the summary"), ToolboxHints, Summary.HintNotesInToolboxes);
	}

	// A cat makes the kitty key obtainable, so it may lock a toolbox, but it is still never spawned.
	{
		FScopedGameplayTestWorld TestWorld;
		UWorld* World = TestWorld.World;
		SpawnGameplayPoint(World, ELostWithinSpawnPointKind::Key, FVector(0.0f, 0.0f, 100.0f), TEXT("KeyPoint_A"));
		SpawnGameplayPoint(World, ELostWithinSpawnPointKind::Note, FVector(0.0f, 300.0f, 100.0f), TEXT("NotePoint_A"));
		AddGameplayCat(World, FVector(5000.0f, 0.0f, 0.0f));

		FLostWithinRunConfig Config;
		Config.SpawnableKeys = { APickupItemActor::KeyItemID, APickupItemActor::OldKeyItemID, APickupItemActor::RoyalKeyItemID };
		FRandomStream Stream(5);
		AddExpectedError(TEXT("Run setup: no note spawn point or toolbox left for hint note"),
			EAutomationExpectedErrorFlags::Contains, 2);
		const FLostWithinRunSummary Summary = LostWithinRunSetup::SetupWorld(World, Config, { 1, 2, 3 }, Stream);
		TestEqual(TEXT("Only one key fits one key point"), Summary.SpawnedKeys.Num(), 1);
		TestTrue(TEXT("With a cat the kitty key is obtainable"), Summary.ObtainableKeys.Contains(APickupItemActor::FairyTaleKittyKeyItemID));
		TestFalse(TEXT("Hints that do not fit anywhere are reported"), Summary.bAllHintsPlaced);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLostWithinNoiseHearingTest, "LostWithin.Gameplay.NoiseHearing", LOSTWITHIN_GAMEPLAY_TEST_FLAGS)

bool FLostWithinNoiseHearingTest::RunTest(const FString& Parameters)
{
	FScopedGameplayTestWorld TestWorld;
	UWorld* World = TestWorld.World;
	if (!TestNotNull(TEXT("Test world"), World) || !TestNotNull(TEXT("Noise subsystem"), World->GetSubsystem<ULostWithinNoiseSubsystem>()))
	{
		return false;
	}

	const FGameplayMonster Listener = SpawnGameplayMonster(World, FVector::ZeroVector, /*bWithHearing=*/true);
	const FGameplayMonster Deaf = SpawnGameplayMonster(World, FVector(0.0f, 200.0f, 0.0f), /*bWithHearing=*/false);
	if (!TestNotNull(TEXT("Hearing monster"), Listener.Hearing) || !TestNotNull(TEXT("Deaf monster"), Deaf.Actor))
	{
		return false;
	}
	Listener.AI->PanicGaitSpeed = 420.0f;
	UCharacterMovementComponent* Movement = Listener.Actor->GetCharacterMovement();
	Movement->MaxWalkSpeed = 150.0f;

	const FVector NoiseLocation(500.0f, 0.0f, 0.0f);
	TestEqual(TEXT("A footstep that carries 400 cm is not heard at 500 cm"),
		ULostWithinNoiseSubsystem::ReportNoise(World, NoiseLocation, 400.0f, ELostWithinNoiseKind::Footstep, nullptr), 0);
	TestFalse(TEXT("Not investigating after an unheard noise"), Listener.Hearing->IsInvestigating());
	TestTrue(TEXT("Normal roaming continues"), UMonsterAIComponent::CanRunNormalAI(Listener.Actor));

	TestEqual(TEXT("A sprint footstep that carries 600 cm is heard by the hearing monster only"),
		ULostWithinNoiseSubsystem::ReportNoise(World, NoiseLocation, 600.0f, ELostWithinNoiseKind::Footstep, nullptr), 1);
	TestTrue(TEXT("The hearing monster investigates"), Listener.Hearing->IsInvestigating());
	TestTrue(TEXT("It heads for the noise"), Listener.Hearing->GetInvestigationLocation().Equals(NoiseLocation));
	TestFalse(TEXT("The Blueprint roam loop is paused while investigating"), UMonsterAIComponent::CanRunNormalAI(Listener.Actor));
	TestTrue(TEXT("The deaf monster keeps roaming"), UMonsterAIComponent::CanRunNormalAI(Deaf.Actor));
	TestEqual(TEXT("Investigation runs at the panic gait speed"), Movement->MaxWalkSpeed, 420.0f);

	// Arrive, search, then hand roaming back with the original speed.
	Listener.Hearing->SearchDuration = 0.5f;
	Listener.Actor->SetActorLocation(NoiseLocation);
	TickGameplayComponent(Listener.Hearing, 0.1f);
	TestTrue(TEXT("Arrived monsters search"), Listener.Hearing->IsSearching());
	for (int32 Step = 0; Step < 8; ++Step)
	{
		TickGameplayComponent(Listener.Hearing, 0.1f);
	}
	TestFalse(TEXT("The search ends"), Listener.Hearing->IsInvestigating());
	TestTrue(TEXT("Roaming resumes after the search"), UMonsterAIComponent::CanRunNormalAI(Listener.Actor));
	TestEqual(TEXT("Walk speed is restored after the search"), Movement->MaxWalkSpeed, 150.0f);

	// Filters and cat priority.
	Listener.Hearing->bHearFootsteps = false;
	TestEqual(TEXT("A monster that ignores footsteps does not react"),
		ULostWithinNoiseSubsystem::ReportNoise(World, FVector::ZeroVector, 3000.0f, ELostWithinNoiseKind::Footstep, nullptr), 0);
	TestEqual(TEXT("It still hears a dropped item"),
		ULostWithinNoiseSubsystem::ReportNoise(World, FVector(2500.0f, 0.0f, 0.0f), 3000.0f, ELostWithinNoiseKind::ItemImpact, nullptr), 1);

	Listener.AI->bCatIsNear = true;
	TickGameplayComponent(Listener.Hearing, 0.1f);
	TestFalse(TEXT("The cat reaction cancels an investigation"), Listener.Hearing->IsInvestigating());
	TestEqual(TEXT("Noises are ignored while the cat is near"),
		ULostWithinNoiseSubsystem::ReportNoise(World, FVector::ZeroVector, 3000.0f, ELostWithinNoiseKind::ItemImpact, nullptr), 0);
	Listener.AI->bCatIsNear = false;
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLostWithinToolboxLockTest, "LostWithin.Gameplay.ToolboxLock", LOSTWITHIN_GAMEPLAY_TEST_FLAGS)

bool FLostWithinToolboxLockTest::RunTest(const FString& Parameters)
{
	FScopedGameplayTestWorld TestWorld;
	UWorld* World = TestWorld.World;
	if (!TestNotNull(TEXT("Test world"), World))
	{
		return false;
	}

	AActor* Player = nullptr;
	UInventoryComponent* Inventory = SpawnGameplayInventoryHolder(World, FVector(-200.0f, 0.0f, 100.0f), Player);
	AToolboxActor* Toolbox = SpawnGameplayToolbox(World, FVector::ZeroVector, TEXT("Toolbox_Locked"));
	if (!TestNotNull(TEXT("Toolbox"), Toolbox))
	{
		return false;
	}
	Toolbox->ContentsDelay = 0.0f;
	Toolbox->SetRequiredKey(APickupItemActor::OldKeyItemID);
	Toolbox->SetContents(APickupItemActor::StaticClass(), APickupItemActor::MakeBatteryInventoryItem(80.0f));

	TestTrue(TEXT("Toolbox is locked"), Toolbox->IsLocked());
	TestTrue(TEXT("The prompt names the key"), IInteractableInterface::Execute_GetInteractionPrompt(Toolbox).ToString().Contains(TEXT("Old Key")));
	TestFalse(TEXT("No key: still locked"), Toolbox->TryUnlock(Player));
	TestFalse(TEXT("A locked toolbox does not open"), Toolbox->Open());

	int32 Remaining = 0;
	FInventoryItem WrongKey;
	WrongKey.ItemID = APickupItemActor::KeyItemID;
	Inventory->AddItem(WrongKey, Remaining);
	IInteractableInterface::Execute_OnInteract(Toolbox, Player);
	TestTrue(TEXT("The wrong unique key does not fit"), Toolbox->IsLocked());
	TestTrue(TEXT("The wrong key is kept"), Inventory->HasItem(APickupItemActor::KeyItemID));

	FInventoryItem OldKey;
	OldKey.ItemID = APickupItemActor::OldKeyItemID;
	Inventory->AddItem(OldKey, Remaining);
	const int32 PickupsBefore = GatherGameplayPickups(World).Num();
	IInteractableInterface::Execute_OnInteract(Toolbox, Player);
	TestFalse(TEXT("The right key unlocks"), Toolbox->IsLocked());
	TestFalse(TEXT("The key is used up"), Inventory->HasItem(APickupItemActor::OldKeyItemID));
	TestTrue(TEXT("Unlocking opens the toolbox"), Toolbox->IsOpen());
	TestEqual(TEXT("Opening pops out its item"), GatherGameplayPickups(World).Num(), PickupsBefore + 1);
	TestFalse(TEXT("An open toolbox cannot be used again"), IInteractableInterface::Execute_CanInteract(Toolbox, Player));

	AToolboxActor* Empty = SpawnGameplayToolbox(World, FVector(400.0f, 0.0f, 0.0f), TEXT("Toolbox_Empty"));
	Empty->ContentsDelay = 0.0f;
	const int32 PickupsBeforeEmpty = GatherGameplayPickups(World).Num();
	TestTrue(TEXT("An unlocked toolbox opens"), Empty->Open());
	TestEqual(TEXT("An empty toolbox yields nothing"), GatherGameplayPickups(World).Num(), PickupsBeforeEmpty);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLostWithinPaperNoteTest, "LostWithin.Gameplay.PaperNote", LOSTWITHIN_GAMEPLAY_TEST_FLAGS)

bool FLostWithinPaperNoteTest::RunTest(const FString& Parameters)
{
	FScopedGameplayTestWorld TestWorld;
	UWorld* World = TestWorld.World;
	if (!TestNotNull(TEXT("Test world"), World))
	{
		return false;
	}

	const FText HintText = FText::FromString(TEXT("The first dial was set to four."));
	APickupItemActor* NoteA = World->SpawnActor<APickupItemActor>(APickupItemActor::StaticClass(), FVector(0.0f, 0.0f, 50.0f), FRotator::ZeroRotator);
	APickupItemActor* NoteB = World->SpawnActor<APickupItemActor>(APickupItemActor::StaticClass(), FVector(100.0f, 0.0f, 50.0f), FRotator::ZeroRotator);
	NoteA->ConfigureAsNote(HintText);
	NoteB->ConfigureAsNote(FText::FromString(TEXT("Every path I take leads back here.")));

	TestTrue(TEXT("Note is recognized"), NoteA->IsNoteItem());
	TestTrue(TEXT("Note IDs are paper note IDs"), APickupItemActor::IsNoteItemID(NoteA->ItemData.ItemID));
	TestTrue(TEXT("Each note has its own ID"), NoteA->ItemData.ItemID != NoteB->ItemData.ItemID);
	TestEqual(TEXT("Notes never stack"), NoteA->ItemData.MaxStackSize, 1);
	TestEqual(TEXT("The note keeps its text"), NoteA->ItemData.Description.ToString(), HintText.ToString());
	TestFalse(TEXT("Notes do not simulate physics"), NoteA->IsSimulatingItemPhysics());
	TestEqual(TEXT("Paper makes no impact noise"), NoteA->ImpactNoiseRadius, 0.0f);
	TestEqual(TEXT("Prompt reads the note"), IInteractableInterface::Execute_GetInteractionPrompt(NoteA).ToString(), FString(TEXT("[LMB] Read note")));

	AActor* Player = nullptr;
	UInventoryComponent* Inventory = SpawnGameplayInventoryHolder(World, FVector(-300.0f, 0.0f, 100.0f), Player);
	IInteractableInterface::Execute_OnInteract(NoteA, Player);
	IInteractableInterface::Execute_OnInteract(NoteB, Player);
	TestEqual(TEXT("Two notes take two slots"), Inventory->GetItems().Num(), 2);

	const int32 HintSlot = Inventory->GetItems().IndexOfByPredicate([&HintText](const FInventoryItem& Item)
	{
		return Item.Description.ToString() == HintText.ToString();
	});
	if (!TestTrue(TEXT("The hint note is in the inventory"), HintSlot != INDEX_NONE))
	{
		return false;
	}
	TestTrue(TEXT("The note can be dropped"), Inventory->DropItemAtSlot(HintSlot, 1));

	const APickupItemActor* Dropped = nullptr;
	for (const APickupItemActor* Pickup : GatherGameplayPickups(World))
	{
		if (Pickup->IsNoteItem() && Pickup->ItemData.Description.ToString() == HintText.ToString())
		{
			Dropped = Pickup;
		}
	}
	if (TestNotNull(TEXT("The dropped note is back in the world"), Dropped))
	{
		TestFalse(TEXT("A dropped note lies still instead of tumbling"), Dropped->IsSimulatingItemPhysics());
	}

	// Battery and key impacts are loud, notes are silent.
	const APickupItemActor* Battery = World->SpawnActor<APickupItemActor>(APickupItemActor::StaticClass(), FVector(0.0f, 300.0f, 50.0f), FRotator::ZeroRotator);
	TestTrue(TEXT("Other items make a large impact noise"), Battery && Battery->ImpactNoiseRadius >= 2000.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLostWithinTouchDeathTest, "LostWithin.Gameplay.TouchDeath", LOSTWITHIN_GAMEPLAY_TEST_FLAGS)

bool FLostWithinTouchDeathTest::RunTest(const FString& Parameters)
{
	FScopedGameplayTestWorld TestWorld;
	UWorld* World = TestWorld.World;
	if (!TestNotNull(TEXT("Test world"), World))
	{
		return false;
	}

	const FTransform PlayerTransform(FVector::ZeroVector);
	ACharacter* Player = World->SpawnActorDeferred<ACharacter>(ACharacter::StaticClass(), PlayerTransform, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	Player->AutoPossessAI = EAutoPossessAI::Disabled;
	Player->FinishSpawning(PlayerTransform);
	UPlayerDeathComponent* Death = NewObject<UPlayerDeathComponent>(Player, TEXT("Death"));
	Death->RegisterComponent();

	const float PlayerRadius = Player->GetCapsuleComponent()->GetScaledCapsuleRadius();
	const FGameplayMonster Monster = SpawnGameplayMonster(World, FVector(1000.0f, 0.0f, 0.0f), /*bWithHearing=*/false);
	const float MonsterRadius = Monster.Actor->GetCapsuleComponent()->GetScaledCapsuleRadius();

	TestNull(TEXT("A distant monster does not touch"), Death->FindTouchingMonster());

	Monster.Actor->SetActorLocation(FVector(PlayerRadius + MonsterRadius + 20.0f, 0.0f, 0.0f));
	TestNull(TEXT("A 20 cm gap is not a touch"), Death->FindTouchingMonster());

	Monster.Actor->SetActorLocation(FVector(PlayerRadius + MonsterRadius + 2.0f, 0.0f, 0.0f));
	TestTrue(TEXT("Capsules 2 cm apart touch"), UPlayerDeathComponent::AreCapsulesTouching(
		*Player->GetCapsuleComponent(), *Monster.Actor->GetCapsuleComponent(), Death->TouchTolerance));
	TestTrue(TEXT("The touching monster is found"), Death->FindTouchingMonster() == Monster.Actor);

	Monster.AI->bIsRagdolled = true;
	TestNull(TEXT("A ragdolled monster cannot kill"), Death->FindTouchingMonster());
	TestFalse(TEXT("A ragdolled monster is not lethal"), UPlayerDeathComponent::IsLethalMonster(Monster.Actor));
	Monster.AI->bIsRagdolled = false;

	TestFalse(TEXT("A plain character is not a monster"), UPlayerDeathComponent::IsLethalMonster(Player));

	TestTrue(TEXT("Touching starts the death"), Death->BeginDeath(Monster.Actor));
	TestTrue(TEXT("The player is dead"), Death->IsDead());
	TestTrue(TEXT("The killer is remembered"), Death->GetKiller() == Monster.Actor);
	TestFalse(TEXT("Death happens once"), Death->BeginDeath(Monster.Actor));
	TestTrue(TEXT("The killer is frozen"), Monster.Actor->GetCharacterMovement()->MovementMode == MOVE_None);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLostWithinEscapeDoorTest, "LostWithin.Gameplay.EscapeDoor", LOSTWITHIN_GAMEPLAY_TEST_FLAGS)

bool FLostWithinEscapeDoorTest::RunTest(const FString& Parameters)
{
	UClass* DoorClass = LoadClass<AActor>(nullptr, TEXT("/Game/Level/MAZE/Objects/ScaryDoor/Blueprints/BP_ScaryDoor.BP_ScaryDoor_C"));
	if (!TestNotNull(TEXT("BP_ScaryDoor loads"), DoorClass))
	{
		return false;
	}

	FScopedGameplayTestWorld TestWorld;
	UWorld* World = TestWorld.World;
	// The pack's dial BeginPlay enables input through player controller zero.
	World->SpawnActor<APlayerController>();
	AActor* Door = World->SpawnActor<AActor>(DoorClass, FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("Door spawns"), Door))
	{
		return false;
	}

	const UEscapeDoorComponent* Escape = Door->FindComponentByClass<UEscapeDoorComponent>();
	if (!TestNotNull(TEXT("BP_ScaryDoor carries the escape door component"), Escape))
	{
		return false;
	}

	bool bLocked = false;
	TestTrue(TEXT("The door's lock flag is readable"), UEscapeDoorComponent::GetBoolProperty(Door, Escape->DoorLockedPropertyName, bLocked));
	TestTrue(TEXT("The door starts locked"), Escape->IsDoorLocked());

	AActor* Padlock = Escape->GetPadlock();
	if (TestNotNull(TEXT("The skull padlock is attached to the door"), Padlock))
	{
		const FObjectPropertyBase* DoorProperty = FindFProperty<FObjectPropertyBase>(Padlock->GetClass(), Escape->PadlockDoorPropertyName);
		TestTrue(TEXT("The padlock points at its door"), DoorProperty && DoorProperty->GetObjectPropertyValue_InContainer(Padlock) == Door);

		UEscapeDoorComponent* MutableEscape = const_cast<UEscapeDoorComponent*>(Escape);
		TestTrue(TEXT("A run combination can be applied"), MutableEscape->ApplyCombination(TEXT("307")));
		const FStrProperty* Combination = FindFProperty<FStrProperty>(Padlock->GetClass(), Escape->PadlockCombinationPropertyName);
		TestTrue(TEXT("The padlock holds the run combination"), Combination && Combination->GetPropertyValue_InContainer(Padlock) == TEXT("307"));
	}

	TestTrue(TEXT("The door's UnlockDoor event is callable"), UEscapeDoorComponent::CallBlueprintFunction(Door, TEXT("UnlockDoor")));
	TestFalse(TEXT("UnlockDoor clears the lock"), Escape->IsDoorLocked());
	TestEqual(TEXT("An unlocked door offers escape"),
		IInteractableInterface::Execute_GetInteractionPrompt(const_cast<UEscapeDoorComponent*>(Escape)).ToString(), FString(TEXT("[LMB] Open the door")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLostWithinLightItemImpactTest, "LostWithin.Gameplay.LightItemImpact", LOSTWITHIN_GAMEPLAY_TEST_FLAGS)

bool FLostWithinLightItemImpactTest::RunTest(const FString& Parameters)
{
	FScopedGameplayTestWorld TestWorld;
	UWorld* World = TestWorld.World;
	const FGameplayMonster Listener = SpawnGameplayMonster(World, FVector(2000.0f, 0.0f, 0.0f), true);
	APickupItemActor* Battery = World->SpawnActor<APickupItemActor>();
	if (!TestNotNull(TEXT("Hearing listener"), Listener.Hearing) || !TestNotNull(TEXT("Battery"), Battery))
	{
		return false;
	}
	Battery->ConfigureAsBattery();
	UStaticMeshComponent* Mesh = Battery->FindComponentByClass<UStaticMeshComponent>();
	if (!TestNotNull(TEXT("Battery mesh"), Mesh)
		|| !TestTrue(TEXT("The production battery can simulate physics"), Battery->IsSimulatingItemPhysics()))
	{
		return false;
	}
	TestTrue(TEXT("Small pickups use continuous collision detection"), Mesh->BodyInstance.bUseCCD);
	AActor* Floor = SpawnGameplayRootedActor(World, FVector::ZeroVector);
	FHitResult Hit;
	Hit.bBlockingHit = true;
	Hit.Normal = Hit.ImpactNormal = FVector::UpVector;
	Hit.ImpactPoint = FVector(0.0f, 0.0f, -50.0f);

	Mesh->SetMassOverrideInKg(NAME_None, 0.02f, true);
	Battery->OnMeshHit(Mesh, Floor, nullptr, FVector(0.0f, 0.0f, 2.0f), Hit);
	TestTrue(TEXT("A light item's audible impact reaches a distant listener"), Listener.Hearing->IsInvestigating());
	TestTrue(TEXT("The investigation targets the impact point"),
		Listener.Hearing->GetInvestigationLocation().Equals(Hit.ImpactPoint));
	Listener.Hearing->StopInvestigating();

	APickupItemActor* Heavy = World->SpawnActor<APickupItemActor>();
	if (!TestNotNull(TEXT("Heavy impact fixture"), Heavy))
	{
		return false;
	}
	Heavy->ConfigureAsBattery();
	UStaticMeshComponent* HeavyMesh = Heavy->FindComponentByClass<UStaticMeshComponent>();
	if (!TestNotNull(TEXT("Heavy impact mesh"), HeavyMesh))
	{
		return false;
	}
	HeavyMesh->SetMassOverrideInKg(NAME_None, 1.0f, true);
	Heavy->OnMeshHit(HeavyMesh, Floor, nullptr, FVector(0.0f, 0.0f, 2.0f), Hit);
	TestFalse(TEXT("A heavy object's tiny velocity change stays quiet"), Listener.Hearing->IsInvestigating());
	return true;
}

#undef LOSTWITHIN_GAMEPLAY_TEST_FLAGS

#endif // WITH_DEV_AUTOMATION_TESTS
