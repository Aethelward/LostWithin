// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "AI/MonsterAIComponent.h"
#include "AI/MonsterHearingComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/ProximityFloatSpinComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
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
#include "InputCoreTypes.h"
#include "LostWithinCharacter.h"
#include "Misc/AutomationTest.h"
#include "Player/DeathCameraActor.h"
#include "Player/PlayerDeathComponent.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "Player/BodyContactComponent.h"
#include "Gameplay/LostWithinStory.h"
#include "Gameplay/LostWithinNarrativeComponent.h"
#include "UI/LostWithinModalWidget.h"
#include "Sound/SoundBase.h"
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
	TestEqual(TEXT("Opening yields its item"), GatherGameplayPickups(World).Num(), PickupsBefore + 1);
	TestFalse(TEXT("An open toolbox cannot be used again"), IInteractableInterface::Execute_CanInteract(Toolbox, Player));

	AToolboxActor* Empty = SpawnGameplayToolbox(World, FVector(400.0f, 0.0f, 0.0f), TEXT("Toolbox_Empty"));
	Empty->ContentsDelay = 0.0f;
	const int32 PickupsBeforeEmpty = GatherGameplayPickups(World).Num();
	TestTrue(TEXT("An unlocked toolbox opens"), Empty->Open());
	TestEqual(TEXT("An empty toolbox yields nothing"), GatherGameplayPickups(World).Num(), PickupsBeforeEmpty);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLostWithinToolboxHollowContentsTest, "LostWithin.Gameplay.ToolboxHollowContents", LOSTWITHIN_GAMEPLAY_TEST_FLAGS)

bool FLostWithinToolboxHollowContentsTest::RunTest(const FString& Parameters)
{
	FScopedGameplayTestWorld TestWorld;
	UWorld* World = TestWorld.World;
	if (!TestNotNull(TEXT("Test world"), World))
	{
		return false;
	}

	struct FToolboxCase
	{
		const TCHAR* Name;
		double Yaw;
		bool bNote;
	};
	const FToolboxCase Cases[] = {
		{ TEXT("Toolbox_Battery"), 0.0, false },
		{ TEXT("Toolbox_BatteryTurned"), 37.0, false },
		{ TEXT("Toolbox_Note"), 0.0, true },
		{ TEXT("Toolbox_NoteTurned"), 127.0, true },
	};

	double OffsetX = 0.0;
	for (const FToolboxCase& Case : Cases)
	{
		AToolboxActor* Toolbox = SpawnGameplayToolbox(World, FVector(OffsetX, 4000.0, 0.0), Case.Name);
		OffsetX += 600.0;
		if (!TestNotNull(Case.Name, Toolbox))
		{
			return false;
		}
		Toolbox->SetActorRotation(FRotator(0.0, Case.Yaw, 0.0));
		const FTransform BoxTransform = Toolbox->GetActorTransform();
		const FBox Cavity = Toolbox->GetCavityBox();
		const FString Label = Case.Name;

		TestTrue(Label + TEXT(": the cavity is a real space (> 50 x 24 x 18 cm)"),
			Cavity.GetSize().X > 50.0 && Cavity.GetSize().Y > 24.0 && Cavity.GetSize().Z > 18.0);

		// Nothing blocks inside the cavity.
		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(LostWithinToolboxCavityTest), /*bTraceComplex=*/false);
		const FVector CavityCenter = BoxTransform.TransformPosition(Cavity.GetCenter());
		TestFalse(Label + TEXT(": no collision fills the cavity"), World->OverlapBlockingTestByChannel(CavityCenter,
			BoxTransform.GetRotation(), ECC_PhysicsBody, FCollisionShape::MakeBox(Cavity.GetExtent() - FVector(0.25)), QueryParams));

		// From the middle the walls are hit at the inner faces, and the floor at the inner floor.
		const FVector MidHeight(Cavity.GetCenter().X, Cavity.GetCenter().Y, 10.0);
		struct FProbe { const TCHAR* What; FVector Direction; double Expected; int32 Axis; };
		const FProbe Probes[] = {
			{ TEXT("+X wall"), FVector(1.0, 0.0, 0.0), Cavity.Max.X, 0 },
			{ TEXT("-X wall"), FVector(-1.0, 0.0, 0.0), Cavity.Min.X, 0 },
			{ TEXT("+Y wall"), FVector(0.0, 1.0, 0.0), Cavity.Max.Y, 1 },
			{ TEXT("-Y wall"), FVector(0.0, -1.0, 0.0), Cavity.Min.Y, 1 },
			{ TEXT("inner floor"), FVector(0.0, 0.0, -1.0), Cavity.Min.Z, 2 },
		};
		for (const FProbe& Probe : Probes)
		{
			FHitResult Hit;
			const bool bHit = World->LineTraceSingleByChannel(Hit, BoxTransform.TransformPosition(MidHeight),
				BoxTransform.TransformPosition(MidHeight + Probe.Direction * 60.0), ECC_Visibility, QueryParams);
			const FVector HitInBox = BoxTransform.InverseTransformPosition(Hit.ImpactPoint);
			TestTrue(FString::Printf(TEXT("%s: the %s blocks at its inner face (hit %d at %.2f, expected %.2f)"), *Label,
				Probe.What, bHit ? 1 : 0, HitInBox[Probe.Axis], Probe.Expected),
				bHit && Hit.GetActor() == Toolbox && FMath::IsNearlyEqual(HitInBox[Probe.Axis], Probe.Expected, 0.3));
		}

		// The closed lid caps the box and rides the lid bone.
		UBoxComponent* Lid = Toolbox->GetLidCollision();
		if (TestNotNull(Label + TEXT(": lid collision"), Lid))
		{
			TestEqual(Label + TEXT(": the lid collision follows the lid bone"), Lid->GetAttachSocketName(), AToolboxActor::LidBoneName);
			FHitResult LidHit;
			const bool bLidHit = World->LineTraceSingleByChannel(LidHit, BoxTransform.TransformPosition(FVector(0.0, 0.0, 80.0)),
				BoxTransform.TransformPosition(FVector(0.0, 0.0, 5.0)), ECC_Visibility, QueryParams);
			const double LidTopZ = BoxTransform.InverseTransformPosition(LidHit.ImpactPoint).Z;
			TestTrue(FString::Printf(TEXT("%s: the closed lid is hit on top (%.2f cm)"), *Label, LidTopZ),
				bLidHit && LidHit.GetComponent() == Lid && LidTopZ > Cavity.Max.Z + 3.0 && LidTopZ < Cavity.Max.Z + 4.5);
		}

		// The contents are placed inside, resting on the inner floor.
		if (Case.bNote)
		{
			// As the run setup builds a hint note for a toolbox.
			FInventoryItem Note;
			Note.ItemID = APickupItemActor::MakeUniqueNoteItemID();
			Note.Description = FText::FromString(TEXT("The second lock remembers seven."));
			Note.MaxStackSize = 1;
			Note.WorldMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(APickupItemActor::PaperNoteMeshAssetPath));
			Toolbox->SetContents(APickupItemActor::StaticClass(), Note);
		}
		else
		{
			Toolbox->SetContents(APickupItemActor::StaticClass(), APickupItemActor::MakeBatteryInventoryItem(80.0f));
		}
		const TArray<APickupItemActor*> Before = GatherGameplayPickups(World);
		TestTrue(Label + TEXT(": opens"), Toolbox->Open());
		APickupItemActor* Item = nullptr;
		for (APickupItemActor* Pickup : GatherGameplayPickups(World))
		{
			if (!Before.Contains(Pickup))
			{
				Item = Pickup;
			}
		}
		if (!TestNotNull(Label + TEXT(": the contents exist as soon as the lid starts opening"), Item))
		{
			continue;
		}

		const UPrimitiveComponent* Body = Cast<UPrimitiveComponent>(Item->GetRootComponent());
		const FBox ItemInBox = Body->CalcBounds(Body->GetComponentTransform().GetRelativeTransform(BoxTransform)).GetBox();
		TestTrue(FString::Printf(TEXT("%s: the item lies within the inner walls and below the rim (%s)"), *Label, *ItemInBox.ToString()),
			Cavity.ExpandBy(0.05).IsInside(ItemInBox));
		TestTrue(FString::Printf(TEXT("%s: the item rests on the inner floor (gap %.2f cm)"), *Label, ItemInBox.Min.Z - Cavity.Min.Z),
			ItemInBox.Min.Z >= Cavity.Min.Z - 0.05 && ItemInBox.Min.Z <= Cavity.Min.Z + 1.0);
		TestEqual(Label + TEXT(": the right item"), Item->IsNoteItem(), Case.bNote);
		if (Case.bNote)
		{
			TestTrue(Label + TEXT(": the note lies flat"), ItemInBox.GetSize().Z < 1.0);
			TestTrue(Label + TEXT(": the note's long side runs along the box"), ItemInBox.GetSize().X >= ItemInBox.GetSize().Y);
		}
		TestFalse(Label + TEXT(": an open toolbox cannot be used again"), IInteractableInterface::Execute_CanInteract(Toolbox, Item));
	}
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
	NoteA->ItemData.StoryNoteID = LostWithinStory::Warning;
	NoteA->ItemData.ClueDigitIndex = 0;
	NoteA->ItemData.ClueDigit = 4;
	NoteB->ConfigureAsNote(FText::FromString(TEXT("Every path I take leads back here.")));

	TestTrue(TEXT("Note is recognized"), NoteA->IsNoteItem());
	TestTrue(TEXT("Note IDs are paper note IDs"), APickupItemActor::IsNoteItemID(NoteA->ItemData.ItemID));
	TestTrue(TEXT("Each note has its own ID"), NoteA->ItemData.ItemID != NoteB->ItemData.ItemID);
	TestEqual(TEXT("Notes never stack"), NoteA->ItemData.MaxStackSize, 1);
	TestEqual(TEXT("The note keeps its text"), NoteA->ItemData.Description.ToString(), HintText.ToString());
	TestFalse(TEXT("Notes do not simulate physics"), NoteA->IsSimulatingItemPhysics());
	TestEqual(TEXT("Paper makes no impact noise"), NoteA->ImpactNoiseRadius, 0.0f);
	TestEqual(TEXT("Prompt reads the note"), IInteractableInterface::Execute_GetInteractionPrompt(NoteA).ToString(), FString(TEXT("Press [E] to Read Note")));

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
	TestEqual(TEXT("Collected note keeps story identity"), Inventory->GetItems()[HintSlot].StoryNoteID, LostWithinStory::Warning);
	TestEqual(TEXT("Collected note keeps explicit code digit"), Inventory->GetItems()[HintSlot].ClueDigit, 4);
	TestTrue(TEXT("The note can be dropped"), Inventory->DropItemAtSlot(HintSlot, 1));

	const APickupItemActor* Dropped = nullptr;
	for (const APickupItemActor* Pickup : GatherGameplayPickups(World))
	{
		if (Pickup->IsNoteItem() && Pickup->ItemData.Description.ToString() == HintText.ToString())
		{
			TestEqual(TEXT("Dropped note keeps story identity"), Pickup->ItemData.StoryNoteID, LostWithinStory::Warning);
			TestEqual(TEXT("Dropped note keeps its explicit code digit"), Pickup->ItemData.ClueDigit, 4);
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
	for (const TCHAR* Name : {TEXT("Subject09"), TEXT("Lunger"), TEXT("BoneDog"), TEXT("Doll"), TEXT("Ghoul")})
	{
		UPhysicsAsset* Profile = LoadObject<UPhysicsAsset>(nullptr, *FString::Printf(TEXT("/Game/Gameplay/Subject09/Contact/PA_BodyContact_%s.PA_BodyContact_%s"), Name, Name));
		if (TestNotNull(FString::Printf(TEXT("%s contact profile loads"), Name), Profile))
		{ TestTrue(FString::Printf(TEXT("%s contact fits separate body regions, not a merged whole-body capsule"), Name), Profile->SkeletalBodySetups.Num() >= 8); }
	}
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

	const FGameplayMonster Monster = SpawnGameplayMonster(World, FVector(1000.0f, 0.0f, 0.0f), false);
	Player->GetMesh()->SetRelativeLocation(FVector::ZeroVector);
	Monster.Actor->GetMesh()->SetRelativeLocation(FVector::ZeroVector);
	UBodyContactComponent* PlayerBody = NewObject<UBodyContactComponent>(Player);
	PlayerBody->bPlayerBody = true;
	PlayerBody->RegisterComponent();
	UBodyContactComponent* MonsterBody = NewObject<UBodyContactComponent>(Monster.Actor);
	MonsterBody->RegisterComponent();
	PlayerBody->AddContactSphere(NAME_None, FVector::ZeroVector, 12.0f);
	MonsterBody->AddContactSphere(NAME_None, FVector::ZeroVector, 12.0f);
	PlayerBody->RefreshContactTransforms();
	MonsterBody->RefreshContactTransforms();
	TestNull(TEXT("A distant monster does not touch"), Death->FindTouchingMonster());
	TestFalse(TEXT("An arbitrary BeginDeath call cannot kill from far away"), Death->BeginDeath(Monster.Actor));

	Monster.Actor->SetActorLocation(FVector(30.0f, 0.0f, 0.0f));
	MonsterBody->RefreshContactTransforms();
	TestNull(TEXT("Movement capsules overlap but a visible body gap does not kill"), Death->FindTouchingMonster());
	TestFalse(TEXT("A capsule-only direct death request is rejected"), Death->BeginDeath(Monster.Actor));

	Monster.Actor->SetActorLocation(FVector(22.0f, 0.0f, 0.0f));
	MonsterBody->RefreshContactTransforms();
	AActor* Wall = SpawnGameplayRootedActor(World, FVector(11.0f, 0.0f, 0.0f));
	UBoxComponent* WallBox = NewObject<UBoxComponent>(Wall);
	WallBox->SetupAttachment(Wall->GetRootComponent());
	WallBox->InitBoxExtent(FVector(1.0f, 100.0f, 100.0f));
	WallBox->SetCollisionProfileName(TEXT("BlockAll"));
	WallBox->RegisterComponent();
	TestNull(TEXT("A wall between touching body proxies prevents death"), Death->FindTouchingMonster());
	Wall->Destroy();
	TestTrue(TEXT("Unobstructed visible body contact is found"), Death->FindTouchingMonster() == Monster.Actor);

	Monster.Actor->SetActorLocation(FVector(-100.0f, 0.0f, 0.0f));
	MonsterBody->RefreshContactTransforms(); MonsterBody->RefreshContactTransforms();
	TestNull(TEXT("Reset to a separated body pose"), Death->FindTouchingMonster());
	Monster.Actor->SetActorLocation(FVector(100.0f, 0.0f, 0.0f));
	MonsterBody->RefreshContactTransforms();
	TestTrue(TEXT("A fast crossing body is detected between frames"), Death->FindTouchingMonster() == Monster.Actor);
	// A teleport (a one-frame jump beyond MaxSweepDistance) is not a sweep: its path across the player is not contact.
	Monster.Actor->SetActorLocation(FVector(-400.0f, 0.0f, 0.0f));
	MonsterBody->RefreshContactTransforms(); MonsterBody->RefreshContactTransforms();
	TestNull(TEXT("Reset beyond the teleport distance"), Death->FindTouchingMonster());
	Monster.Actor->SetActorLocation(FVector(400.0f, 0.0f, 0.0f));
	MonsterBody->RefreshContactTransforms();
	TestNull(TEXT("A teleport across the player is not body contact"), Death->FindTouchingMonster());
	Monster.Actor->SetActorLocation(FVector(22.0f, 0.0f, 0.0f));
	MonsterBody->RefreshContactTransforms(); MonsterBody->RefreshContactTransforms();

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

	// The death camera starts the jumpscare and the impact together.
	const ADeathCameraActor* DeathCameraDefaults = GetDefault<ADeathCameraActor>();
	TestTrue(TEXT("The death camera has the jumpscare"), DeathCameraDefaults->JumpscareSound
		&& DeathCameraDefaults->JumpscareSound->GetName() == TEXT("SFX_DeathJumpscare_01"));
	TestTrue(TEXT("The death camera has the impact"), DeathCameraDefaults->StingSound
		&& DeathCameraDefaults->StingSound->GetName() == TEXT("SFX_DeathImpact_01"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSubject09StoryNotesTest, "LostWithin.Gameplay.Subject09.StoryNotes", LOSTWITHIN_GAMEPLAY_TEST_FLAGS)

bool FSubject09StoryNotesTest::RunTest(const FString& Parameters)
{
	FScopedGameplayTestWorld LegacyWorld, StoryWorld;
	if (!LegacyWorld.World || !StoryWorld.World) { return false; }
	auto Populate = [](UWorld* World)
	{
		for (int32 Index = 0; Index < 3; ++Index)
		{ SpawnGameplayPoint(World, ELostWithinSpawnPointKind::Key, FVector(Index * 300.0f, 0.0f, 100.0f), *FString::Printf(TEXT("Key_%d"), Index)); }
		for (int32 Index = 0; Index < 12; ++Index)
		{ SpawnGameplayPoint(World, ELostWithinSpawnPointKind::Note, FVector(Index * 300.0f, 1000.0f, 100.0f), *FString::Printf(TEXT("Note_%d"), Index)); }
		for (int32 Index = 0; Index < 11; ++Index)
		{ SpawnGameplayToolbox(World, FVector(Index * 300.0f, 2000.0f, 0.0f), *FString::Printf(TEXT("Box_%d"), Index)); }
	};
	Populate(LegacyWorld.World); Populate(StoryWorld.World);
	FLostWithinRunConfig Config;
	Config.SpawnableKeys = {APickupItemActor::KeyItemID, APickupItemActor::OldKeyItemID, APickupItemActor::RoyalKeyItemID};
	FRandomStream LegacyStream(909), StoryStream(909);
	const TArray<int32> Digits{0, 7, 0};
	const FLostWithinRunSummary Legacy = LostWithinRunSetup::SetupWorld(LegacyWorld.World, Config, Digits, LegacyStream);
	Config.bIncludeSubject09Story = true;
	const FLostWithinRunSummary Story = LostWithinRunSetup::SetupWorld(StoryWorld.World, Config, Digits, StoryStream);
	TestEqual(TEXT("Story consumes no extra layout random draws"), StoryStream.GetCurrentSeed(), LegacyStream.GetCurrentSeed());
	TestTrue(TEXT("Existing generated riddles are retained"), Legacy.HintTexts == Story.HintTexts);
	TestTrue(TEXT("Existing unique keys are retained"), Legacy.SpawnedKeys == Story.SpawnedKeys);
	TestEqual(TEXT("Locked box count retained"), Story.LockedToolboxes.Num(), Legacy.LockedToolboxes.Num());
	TestEqual(TEXT("Battery loot retained"), Story.ToolboxBatteries, Legacy.ToolboxBatteries);
	TestEqual(TEXT("Empty loot retained"), Story.EmptyToolboxes, Legacy.EmptyToolboxes);
	TestEqual(TEXT("Five entries use existing notes"), Story.StoryNotesPlaced, 5);
	TestTrue(TEXT("All clues obtainable"), Story.bAllHintsPlaced);
	TSet<FName> IDs;
	int32 ClueCount = 0;
	auto InspectNote = [this, &IDs, &ClueCount, &Digits](const FInventoryItem& Item)
	{
		if (Item.StoryNoteID.IsNone()) { return; }
		IDs.Add(Item.StoryNoteID);
		TestFalse(TEXT("Digit placeholders substituted"), Item.Description.ToString().Contains(TEXT("{digit}")));
		if (Item.ClueDigitIndex != INDEX_NONE)
		{
			++ClueCount;
			TestEqual(TEXT("Clue matches run digit"), Item.ClueDigit, Digits[Item.ClueDigitIndex]);
			TestTrue(TEXT("Clue printed visibly"), Item.Description.ToString().Contains(FString::Printf(TEXT("[ %d ]"), Item.ClueDigit)));
		}
	};
	for (TActorIterator<APickupItemActor> It(StoryWorld.World); It; ++It) { InspectNote(It->ItemData); }
	int32 BoxCount = 0;
	for (TActorIterator<AToolboxActor> It(StoryWorld.World); It; ++It) { ++BoxCount; if (It->HasContents()) { InspectNote(It->GetContentsItem()); } }
	TestEqual(TEXT("11 toolboxes retained"), BoxCount, 11);
	TestEqual(TEXT("Five unique story IDs"), IDs.Num(), 5);
	TestEqual(TEXT("Three explicit clues"), ClueCount, 3);
	const FString Survival = LostWithinStory::FormatNote(LostWithinStory::Survival, Digits).Body.ToString();
	TestTrue(TEXT("Instructions retain blind Lunger"), Survival.Contains(TEXT("blind and deaf")));
	TestTrue(TEXT("Instructions explain cat protection"), Survival.Contains(TEXT("chaos sanctuary")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSubject09NarrativeTest, "LostWithin.Gameplay.Subject09.Narrative", LOSTWITHIN_GAMEPLAY_TEST_FLAGS)

bool FSubject09NarrativeTest::RunTest(const FString& Parameters)
{
	FScopedGameplayTestWorld TestWorld;
	AActor* Owner = nullptr;
	UInventoryComponent* Inventory = SpawnGameplayInventoryHolder(TestWorld.World, FVector::ZeroVector, Owner);
	ULostWithinNarrativeComponent* Story = NewObject<ULostWithinNarrativeComponent>(Owner);
	Story->RegisterComponent();
	FInventoryItem Flashlight; Flashlight.ItemID = APickupItemActor::FlashlightItemID;
	int32 Remaining = 0; Inventory->AddItem(Flashlight, Remaining);
	Story->NoteOpened(LostWithinStory::Orientation);
	Story->NoteOpened(LostWithinStory::FinalEntry);
	Story->NoteClosed(LostWithinStory::FinalEntry);
	TestEqual(TEXT("Final clue first discovers one digit"), Story->GetKnownDigitCount(), 1);
	TestFalse(TEXT("Out-of-order reaction is truthful"), Story->GetSubtitle().ToString().Contains(TEXT("all three digits now")));
	Story->NoteClosed(LostWithinStory::FinalEntry);
	Story->TickComponent(9.0f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("Rereading does not repeat reaction"), Story->GetSubtitle().IsEmpty());
	Story->NoteOpened(LostWithinStory::Warning); Story->NoteOpened(LostWithinStory::Report);
	TestEqual(TEXT("Three unique digits discovered"), Story->GetKnownDigitCount(), 3);
	TestTrue(TEXT("Objective points to exit"), Story->GetObjective().ToString().Contains(TEXT("southeastern exit")));
	Story->ObserveBattery(true, 20.0f);
	TestTrue(TEXT("20 percent does not warn"), Story->GetSubtitle().IsEmpty());
	Story->ObserveBattery(true, 19.0f);
	TestFalse(TEXT("Below 20 percent warns"), Story->GetSubtitle().IsEmpty());
	Story->TickComponent(7.0f, LEVELTICK_All, nullptr);
	Story->ObserveBattery(true, 19.0f);
	TestTrue(TEXT("Low charge does not spam"), Story->GetSubtitle().IsEmpty());
	Story->ObserveBattery(true, 100.0f); Story->ObserveBattery(true, 19.0f);
	TestFalse(TEXT("Recharge rearms warning"), Story->GetSubtitle().IsEmpty());
	Story->ObserveBattery(true, 0.0f);
	TestTrue(TEXT("Depletion warning has priority"), Story->GetSubtitle().ToString().Contains(TEXT("dead")));
	Story->NotifyEnding(true);
	const FText Ending = Story->GetSubtitle();
	Story->QueueSubtitle(FText::FromString(TEXT("stale exploration")), 1);
	TestTrue(TEXT("Ending suppresses exploration"), Story->GetSubtitle().EqualTo(Ending));
	Story->NoteOpened(LostWithinStory::Journal);
	TestFalse(TEXT("Ending blocks new discoveries"), Story->HasReadNote(LostWithinStory::Journal));
	ULostWithinNarrativeComponent* Fresh = NewObject<ULostWithinNarrativeComponent>(Owner);
	TestEqual(TEXT("New run has no remembered digits"), Fresh->GetKnownDigitCount(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSubject09KeypadTest, "LostWithin.Gameplay.Subject09.KeypadInput", LOSTWITHIN_GAMEPLAY_TEST_FLAGS)

bool FSubject09KeypadTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Leading zeros valid"), UEscapeDoorComponent::IsValidCode(TEXT("007")));
	TestTrue(TEXT("Repeated digits valid"), UEscapeDoorComponent::IsValidCode(TEXT("777")));
	TestFalse(TEXT("Incomplete code invalid"), UEscapeDoorComponent::IsValidCode(TEXT("07")));
	TestFalse(TEXT("Extra digits invalid"), UEscapeDoorComponent::IsValidCode(TEXT("0007")));
	TestFalse(TEXT("Letters invalid"), UEscapeDoorComponent::IsValidCode(TEXT("0a7")));
	TestFalse(TEXT("Whitespace invalid"), UEscapeDoorComponent::IsValidCode(TEXT(" 07")));
	ULostWithinModalWidget* Keypad = NewObject<ULostWithinModalWidget>();
	Keypad->ConfigureKeypad(nullptr);
	Keypad->AppendDigit(0); Keypad->AppendDigit(0); Keypad->AppendDigit(7); Keypad->AppendDigit(9);
	TestEqual(TEXT("Keypad preserves zeros and caps three digits"), Keypad->GetEnteredCode(), FString(TEXT("007")));
	Keypad->Backspace(); Keypad->AppendDigit(-1);
	TestEqual(TEXT("Backspace edits; invalid digit ignored"), Keypad->GetEnteredCode(), FString(TEXT("00")));
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
	UEscapeDoorComponent* MutableDoor = const_cast<UEscapeDoorComponent*>(Escape);

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
	TestFalse(TEXT("The door uses the skull padlock dials by default"), Escape->bUseKeypadUI);
	TestEqual(TEXT("A locked door offers the padlock"),
		IInteractableInterface::Execute_GetInteractionPrompt(MutableDoor).ToString(), FString(TEXT("[E] Try the skull padlock")));
	AActor* Interactor = SpawnGameplayRootedActor(World, FVector(300.0f, 0.0f, 0.0f));
	TestFalse(TEXT("Without the keypad option a code cannot be submitted"), MutableDoor->TrySubmitCode(TEXT("307"), Interactor));
	MutableDoor->bUseKeypadUI = true; // the optional keypad path stays covered
	TestFalse(TEXT("Wrong keypad code is rejected"), MutableDoor->TrySubmitCode(TEXT("000"), Interactor));
	TestTrue(TEXT("Wrong code keeps the actual door locked"), MutableDoor->IsDoorLocked());
	TestTrue(TEXT("Correct keypad code starts escape"), MutableDoor->TrySubmitCode(TEXT("307"), Interactor));
	TestFalse(TEXT("Correct code unlocks the actual door"), MutableDoor->IsDoorLocked());
	TestFalse(TEXT("An escape cannot be submitted twice"), MutableDoor->TrySubmitCode(TEXT("307"), Interactor));
	AActor* LegacyDoor = World->SpawnActor<AActor>(DoorClass, FVector(800.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	const UEscapeDoorComponent* LegacyEscape = LegacyDoor->FindComponentByClass<UEscapeDoorComponent>();

	TestTrue(TEXT("The door's UnlockDoor event is callable"), UEscapeDoorComponent::CallBlueprintFunction(LegacyDoor, TEXT("UnlockDoor")));
	TestFalse(TEXT("UnlockDoor clears the lock"), LegacyEscape->IsDoorLocked());
	TestEqual(TEXT("An unlocked door offers escape"),
		IInteractableInterface::Execute_GetInteractionPrompt(const_cast<UEscapeDoorComponent*>(LegacyEscape)).ToString(), FString(TEXT("[E] Open the door")));
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSubject09InteractKeyTest, "LostWithin.Gameplay.Subject09.InteractKeyE", LOSTWITHIN_GAMEPLAY_TEST_FLAGS)

bool FSubject09InteractKeyTest::RunTest(const FString& Parameters)
{
	FScopedGameplayTestWorld TestWorld;
	if (!TestWorld.World) { return false; }
	// ALostWithinCharacter is abstract; the game's player is its Blueprint.
	UClass* PlayerClass = LoadClass<ALostWithinCharacter>(nullptr,
		TEXT("/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter.BP_FirstPersonCharacter_C"));
	if (!TestNotNull(TEXT("BP_FirstPersonCharacter loaded"), PlayerClass)) { return false; }
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ALostWithinCharacter* Player = TestWorld.World->SpawnActor<ALostWithinCharacter>(
		PlayerClass, FVector(0.0f, 0.0f, 200.0f), FRotator::ZeroRotator, Params);
	if (!TestNotNull(TEXT("Player spawned"), Player)) { return false; }

	const TArray<FKey> Keys = Player->GetInteractKeys();
	TestTrue(TEXT("E interacts"), Keys.Contains(EKeys::E));
	TestFalse(TEXT("The left mouse button does not interact"), Keys.Contains(EKeys::LeftMouseButton));
	TestEqual(TEXT("E is the only interaction key"), Keys.Num(), 1);

	// Every native prompt names [E] and never [LMB].
	APickupItemActor* Item = TestWorld.World->SpawnActor<APickupItemActor>(APickupItemActor::StaticClass(), FVector(0.0f, 300.0f, 50.0f), FRotator::ZeroRotator);
	AToolboxActor* Toolbox = SpawnGameplayToolbox(TestWorld.World, FVector(300.0f, 0.0f, 0.0f), TEXT("InteractKeyToolbox"));
	AActor* CatActor = SpawnGameplayRootedActor(TestWorld.World, FVector(-300.0f, 0.0f, 0.0f));
	UProximityFloatSpinComponent* Cat = NewObject<UProximityFloatSpinComponent>(CatActor, TEXT("FloatSpin"));
	Cat->bAutoSetHierarchyMovable = false;
	Cat->InteractionSoundSearchPaths.Reset();
	Cat->RegisterComponent();
	TArray<UObject*> Targets = { Item, Toolbox, Cat };
	for (UObject* Target : Targets)
	{
		if (!TestNotNull(TEXT("Interactable spawned"), Target)) { continue; }
		const FString Prompt = IInteractableInterface::Execute_GetInteractionPrompt(Target).ToString();
		TestTrue(FString::Printf(TEXT("%s prompt names [E]: %s"), *Target->GetName(), *Prompt), Prompt.Contains(TEXT("[E]")));
		TestFalse(FString::Printf(TEXT("%s prompt has no [LMB]: %s"), *Target->GetName(), *Prompt), Prompt.Contains(TEXT("[LMB]")));
	}
	return true;
}

#undef LOSTWITHIN_GAMEPLAY_TEST_FLAGS

#endif // WITH_DEV_AUTOMATION_TESTS
