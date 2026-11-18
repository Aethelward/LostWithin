// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "AI/MonsterAIComponent.h"
#include "AI/MonsterHearingComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/ProximityFloatSpinComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "Gameplay/LostWithinNoiseSubsystem.h"
#include "Interaction/InteractableInterface.h"
#include "Interaction/PickupItemActor.h"
#include "Inventory/InventoryComponent.h"
#include "LostWithinCharacter.h"
#include "Misc/AutomationTest.h"

#define LOSTWITHIN_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

namespace BoneDogAndOiiaiCatTestHelpers
{
	struct FScopedBoneDogTestWorld
	{
		UWorld* World = nullptr;

		FScopedBoneDogTestWorld()
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

		~FScopedBoneDogTestWorld()
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

	struct FBoneDogTestRig
	{
		ACharacter* Character = nullptr;
		UMonsterAIComponent* AI = nullptr;
		UMonsterHearingComponent* Hearing = nullptr;
	};

	FBoneDogTestRig SpawnBoneDog(UWorld* World, const FVector& Location)
	{
		FBoneDogTestRig Rig;
		const FTransform Transform(Location);
		Rig.Character = World->SpawnActorDeferred<ACharacter>(ACharacter::StaticClass(), Transform, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Rig.Character)
		{
			return Rig;
		}

		Rig.AI = NewObject<UMonsterAIComponent>(Rig.Character, TEXT("CatReactionAI"));
		Rig.AI->PanicGaitSpeed = 350.0f;
		Rig.AI->RegisterComponent();

		Rig.Hearing = NewObject<UMonsterHearingComponent>(Rig.Character, TEXT("Hearing"));
		Rig.Hearing->RegisterComponent();

		Rig.Character->FinishSpawning(Transform);
		return Rig;
	}

	UProximityFloatSpinComponent* SpawnTestCat(UWorld* World, const FVector& Location)
	{
		const FTransform Transform(Location);
		AActor* Actor = World->SpawnActorDeferred<AActor>(AActor::StaticClass(), Transform, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Actor)
		{
			return nullptr;
		}

		USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("Root"));
		Root->SetMobility(EComponentMobility::Movable);
		Actor->SetRootComponent(Root);
		Root->SetWorldTransform(Transform);
		Root->RegisterComponent();

		UProximityFloatSpinComponent* Cat = NewObject<UProximityFloatSpinComponent>(Actor, TEXT("FloatSpin"));
		Cat->bAutoSetHierarchyMovable = false;
		Cat->bDarkenSkyOnSpinWindow = false;
		Cat->bEnableDiscoEnvironmentOnSpinWindow = false;
		Cat->bUseMorphVicinityCheck = false;
		Cat->bEnableDirectedPath = false;
		Cat->bJumpBeforeSpin = false;
		Cat->bLoopMusic = false;
		Cat->FloatHeight = 0.0f;
		Cat->BobAmplitude = 0.0f;
		Cat->SpinTriggerInteractionThreshold = 5;
		Cat->ItemDropInteractionThreshold = 2;
		Cat->InteractionSoundSearchPaths.Reset();
		Cat->RegisterComponent();

		Actor->FinishSpawning(Transform);
		return Cat;
	}
}

// -----------------------------------------------------------------------------
// Test 1: BP_BoneDog Audio Hunter & Distraction Priority
// -----------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLostWithinBoneDogAudioHunterTest, "LostWithin.AI.BoneDog.AudioHunter", LOSTWITHIN_TEST_FLAGS)

bool FLostWithinBoneDogAudioHunterTest::RunTest(const FString& Parameters)
{
	using namespace BoneDogAndOiiaiCatTestHelpers;

	FScopedBoneDogTestWorld Env;
	UWorld* World = Env.World;
	if (!TestNotNull(TEXT("World"), World))
	{
		return false;
	}

	ULostWithinNoiseSubsystem* NoiseSubsystem = World->GetSubsystem<ULostWithinNoiseSubsystem>();
	if (!TestNotNull(TEXT("Noise subsystem"), NoiseSubsystem))
	{
		return false;
	}

	const FBoneDogTestRig Dog = SpawnBoneDog(World, FVector::ZeroVector);
	if (!TestNotNull(TEXT("BoneDog Character"), Dog.Character) || !TestNotNull(TEXT("BoneDog Hearing"), Dog.Hearing))
	{
		return false;
	}

	// 1. Verify gait noise radii matching speed (crouch stealth vs walk vs run vs sprint)
	// Crouch footstep (150 cm radius): unheard at 300 cm
	const FVector CrouchNoiseLoc(300.0f, 0.0f, 0.0f);
	TestEqual(TEXT("Crouch noise at 300 cm is unheard"),
		ULostWithinNoiseSubsystem::ReportNoise(World, CrouchNoiseLoc, 150.0f, ELostWithinNoiseKind::Footstep, nullptr), 0);
	TestFalse(TEXT("Dog is not investigating unheard crouch footstep"), Dog.Hearing->IsInvestigating());

	// Walk footstep (400 cm radius): unheard at 500 cm
	const FVector WalkNoiseLoc(500.0f, 0.0f, 0.0f);
	TestEqual(TEXT("Walk noise at 500 cm is unheard"),
		ULostWithinNoiseSubsystem::ReportNoise(World, WalkNoiseLoc, 400.0f, ELostWithinNoiseKind::Footstep, nullptr), 0);
	TestFalse(TEXT("Dog is not investigating"), Dog.Hearing->IsInvestigating());

	// Run footstep (900 cm radius): heard at 500 cm
	TestEqual(TEXT("Run noise at 500 cm is heard"),
		ULostWithinNoiseSubsystem::ReportNoise(World, WalkNoiseLoc, 900.0f, ELostWithinNoiseKind::Footstep, nullptr), 1);
	TestTrue(TEXT("Dog investigates run footstep"), Dog.Hearing->IsInvestigating());
	TestEqual(TEXT("Investigation is for footstep"), Dog.Hearing->GetCurrentNoiseKind(), ELostWithinNoiseKind::Footstep);
	TestFalse(TEXT("Not investigating an item distraction yet"), Dog.Hearing->IsInvestigatingDistraction());

	// 2. Thrown item distraction: reliably draws dog away from puzzle/door and resists interruption
	const FVector DistractionLocation(2000.0f, 500.0f, 0.0f);
	TestEqual(TEXT("Item impact distraction (radius 3000 cm) is heard"),
		ULostWithinNoiseSubsystem::ReportNoise(World, DistractionLocation, 3000.0f, ELostWithinNoiseKind::ItemImpact, nullptr), 1);
	TestTrue(TEXT("Dog redirected to item distraction"), Dog.Hearing->IsInvestigating());
	TestTrue(TEXT("Dog is actively investigating distraction"), Dog.Hearing->IsInvestigatingDistraction());
	TestEqual(TEXT("Investigation destination updated to item impact"), Dog.Hearing->GetInvestigationLocation(), DistractionLocation);

	// While investigating the item distraction, a player walking nearby (400 cm footstep at 800 cm) must NOT interrupt it!
	const FVector SneakFootstepLoc(800.0f, 0.0f, 0.0f);
	TestEqual(TEXT("Walk footstep during active item distraction is rejected by distraction priority"),
		ULostWithinNoiseSubsystem::ReportNoise(World, SneakFootstepLoc, 400.0f, ELostWithinNoiseKind::Footstep, nullptr), 0);
	TestTrue(TEXT("Dog remains on item distraction course"), Dog.Hearing->IsInvestigatingDistraction());
	TestEqual(TEXT("Destination remained at item impact"), Dog.Hearing->GetInvestigationLocation(), DistractionLocation);

	// A loud sprint footstep right near the dog (e.g. 1500 cm radius at 200 cm) CAN interrupt
	const FVector SprintPanicLoc(200.0f, 0.0f, 0.0f);
	TestEqual(TEXT("Loud close sprint footstep interrupts item distraction"),
		ULostWithinNoiseSubsystem::ReportNoise(World, SprintPanicLoc, 1500.0f, ELostWithinNoiseKind::Footstep, nullptr), 1);
	TestEqual(TEXT("Dog switched to loud close sprint"), Dog.Hearing->GetCurrentNoiseKind(), ELostWithinNoiseKind::Footstep);
	TestEqual(TEXT("Destination switched to sprint location"), Dog.Hearing->GetInvestigationLocation(), SprintPanicLoc);

	// Clean up
	Dog.Hearing->StopInvestigating();
	TestFalse(TEXT("Investigation cleanly stopped"), Dog.Hearing->IsInvestigating());

	return true;
}

// -----------------------------------------------------------------------------
// Test 2: Oiiai Cat Key Dropper & Clear Player Feedback
// -----------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLostWithinOiiaiCatKeyDropperTest, "LostWithin.Components.OiiaiCat.KeyDropper", LOSTWITHIN_TEST_FLAGS)

bool FLostWithinOiiaiCatKeyDropperTest::RunTest(const FString& Parameters)
{
	using namespace BoneDogAndOiiaiCatTestHelpers;

	FScopedBoneDogTestWorld Env;
	UWorld* World = Env.World;
	if (!TestNotNull(TEXT("World"), World))
	{
		return false;
	}

	UProximityFloatSpinComponent* Cat = SpawnTestCat(World, FVector::ZeroVector);
	if (!TestNotNull(TEXT("Cat component"), Cat))
	{
		return false;
	}

	Cat->ItemDropInteractionThreshold = 3;
	Cat->SpinTriggerInteractionThreshold = 10;

	// Initial prompt shows petting progress toward key drop
	FText InitialPrompt = Cat->GetInteractionPrompt_Implementation();
	TestTrue(TEXT("Initial prompt shows progress (0/3)"), InitialPrompt.ToString().Contains(TEXT("0/3")));
	TestFalse(TEXT("Key has not been dropped yet"), Cat->HasDroppedKey());

	// First pet
	IInteractableInterface::Execute_OnInteract(Cat, Cat->GetOwner());
	TestEqual(TEXT("Interaction count is 1"), Cat->GetInteractionCount(), 1);
	TestFalse(TEXT("Key not dropped after 1 pet"), Cat->HasDroppedKey());
	FText Pet1Prompt = Cat->GetInteractionPrompt_Implementation();
	TestTrue(TEXT("Prompt updates to (1/3)"), Pet1Prompt.ToString().Contains(TEXT("1/3")));

	// Second pet
	IInteractableInterface::Execute_OnInteract(Cat, Cat->GetOwner());
	TestEqual(TEXT("Interaction count is 2"), Cat->GetInteractionCount(), 2);
	TestFalse(TEXT("Key not dropped after 2 pets"), Cat->HasDroppedKey());

	// Third pet: reaches threshold and drops Fairy Tale Kitty Key!
	IInteractableInterface::Execute_OnInteract(Cat, Cat->GetOwner());
	TestEqual(TEXT("Interaction count is 3"), Cat->GetInteractionCount(), 3);
	TestTrue(TEXT("Key is marked as dropped"), Cat->HasDroppedKey());

	// Find the dropped key actor
	APickupItemActor* DroppedKeyActor = nullptr;
	for (TActorIterator<APickupItemActor> It(World); It; ++It)
	{
		if (IsValid(*It) && !(*It)->IsActorBeingDestroyed())
		{
			DroppedKeyActor = *It;
			break;
		}
	}

	TestNotNull(TEXT("Dropped key actor exists in world"), DroppedKeyActor);
	if (DroppedKeyActor)
	{
		TestEqual(TEXT("Dropped item has FairyTaleKittyKey ID"), DroppedKeyActor->ItemData.ItemID, APickupItemActor::FairyTaleKittyKeyItemID);
		TestEqual(TEXT("Key display name matches Fairy Tale Kitty Key"), DroppedKeyActor->ItemData.DisplayName.ToString(), FString(TEXT("Fairy Tale Kitty Key")));
		TestTrue(TEXT("Dropped key simulates physics"), DroppedKeyActor->IsSimulatingItemPhysics());
	}

	// After key dropped, prompt updates toward Chaos spin threshold
	FText PostDropPrompt = Cat->GetInteractionPrompt_Implementation();
	TestTrue(TEXT("Prompt switches to Chaos progress after key drop"), PostDropPrompt.ToString().Contains(TEXT("Chaos: 3/10")));

	return true;
}

// -----------------------------------------------------------------------------
// Test 3: Oiiai Cat Chaos Sanctuary & Monster Repulsion
// -----------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLostWithinOiiaiCatChaosSanctuaryTest, "LostWithin.Components.OiiaiCat.ChaosSanctuary", LOSTWITHIN_TEST_FLAGS)

bool FLostWithinOiiaiCatChaosSanctuaryTest::RunTest(const FString& Parameters)
{
	using namespace BoneDogAndOiiaiCatTestHelpers;

	FScopedBoneDogTestWorld Env;
	UWorld* World = Env.World;
	if (!TestNotNull(TEXT("World"), World))
	{
		return false;
	}

	UProximityFloatSpinComponent* Cat = SpawnTestCat(World, FVector::ZeroVector);
	if (!TestNotNull(TEXT("Cat component"), Cat))
	{
		return false;
	}

	Cat->ChaosSanctuaryRadius = 700.0f;
	Cat->SanctuaryRepelRadius = 350.0f;
	Cat->bRepelMonstersInSanctuary = true;

	TestFalse(TEXT("Sanctuary is dormant before float/spin activates"), Cat->IsChaosSanctuaryActive());

	// Trigger spin sequence
	Cat->SpinTriggerInteractionThreshold = 1;
	IInteractableInterface::Execute_OnInteract(Cat, Cat->GetOwner());
	TestTrue(TEXT("Float/spin is active"), Cat->IsFloatSpinActive());
	TestTrue(TEXT("Chaos Sanctuary is active"), Cat->IsChaosSanctuaryActive());

	// Spawn a monster inside the sanctuary perimeter (500 cm from cat)
	const FBoneDogTestRig Monster = SpawnBoneDog(World, FVector(500.0f, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("Monster exists"), Monster.Character) || !TestNotNull(TEXT("Monster AI exists"), Monster.AI))
	{
		return false;
	}

	// Update sanctuary
	Cat->TickComponent(0.05f, LEVELTICK_All, nullptr);

	// Monster in sanctuary perimeter must be repelled/fleeing
	TestTrue(TEXT("Monster inside Chaos Sanctuary is frightened"), Monster.AI->bCatIsNear);
	TestTrue(TEXT("Monster inside Chaos Sanctuary is fleeing"), Monster.AI->bIsFleeing);

	// Move monster inside the inner repel radius (250 cm from cat)
	Monster.Character->SetActorLocation(FVector(250.0f, 0.0f, 0.0f));
	Cat->TickComponent(0.05f, LEVELTICK_All, nullptr);

	TestTrue(TEXT("Monster inside repel radius entered ragdoll"), Monster.AI->bIsRagdolled);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
