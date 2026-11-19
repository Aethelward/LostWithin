// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "AI/MonsterAIComponent.h"
#include "AI/MonsterHearingComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/ProximityFloatSpinComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
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
#include "TimerManager.h"

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

	UProximityFloatSpinComponent* SpawnOiiaiTestCat(UWorld* World, const FVector& Location)
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
		// These fixtures exercise the plain pet-count trigger; PetSpinRitual covers the default ritual.
		Cat->bRequirePetSpinSequence = false;
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

	UProximityFloatSpinComponent* Cat = SpawnOiiaiTestCat(World, FVector::ZeroVector);
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

	// After the key drops, the prompt is plain: chaos progress is hidden.
	const FString PostDropPrompt = Cat->GetInteractionPrompt_Implementation().ToString();
	TestEqual(TEXT("Prompt is plain after the key drop"), PostDropPrompt, FString(TEXT("[LMB] Pet Cat")));
	TestFalse(TEXT("Prompt shows no chaos count"), PostDropPrompt.Contains(TEXT("Chaos")));

	return true;
}

// -----------------------------------------------------------------------------
// Test 2b: Chaos mode by the pet-and-spin ritual (pet, spin, pet, spin, pet, spin)
// -----------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLostWithinOiiaiCatPetSpinRitualTest, "LostWithin.Components.OiiaiCat.PetSpinRitual", LOSTWITHIN_TEST_FLAGS)

bool FLostWithinOiiaiCatPetSpinRitualTest::RunTest(const FString& Parameters)
{
	using namespace BoneDogAndOiiaiCatTestHelpers;

	FScopedBoneDogTestWorld Env;
	UWorld* World = Env.World;
	if (!TestNotNull(TEXT("World"), World))
	{
		return false;
	}

	// Feeds a turn in small steps, as the per-tick view yaw would arrive, from StartYaw.
	auto Turn = [](UProximityFloatSpinComponent* Cat, const FVector& Where, float StartYaw, float Degrees)
	{
		const int32 Steps = FMath::Max(1, FMath::CeilToInt(FMath::Abs(Degrees) / 20.0f));
		Cat->UpdatePlayerSpin(Where, StartYaw);
		for (int32 Step = 1; Step <= Steps; ++Step)
		{
			Cat->UpdatePlayerSpin(Where, FRotator::NormalizeAxis(StartYaw + Degrees * Step / Steps));
		}
	};
	const FVector Near(150.0f, 0.0f, 0.0f);
	const FVector Far(2000.0f, 0.0f, 0.0f);

	UProximityFloatSpinComponent* Cat = SpawnOiiaiTestCat(World, FVector::ZeroVector);
	if (!TestNotNull(TEXT("Cat component"), Cat))
	{
		return false;
	}
	Cat->bRequirePetSpinSequence = true;
	Cat->PetSpinRounds = 3;
	Cat->PlayerSpinDegrees = 360.0f;
	Cat->PlayerSpinRadius = 500.0f;
	// High enough that the key is still held when the ritual completes (5 pets happen below).
	Cat->ItemDropInteractionThreshold = 10;
	AActor* Petter = Cat->GetOwner();

	// A spin before any pet does nothing.
	Turn(Cat, Near, 0.0f, 400.0f);
	TestEqual(TEXT("Spinning before petting completes no round"), Cat->GetCompletedPetSpinRounds(), 0);

	// Round 1: pet, then partial turns and a turn that goes back do not count; a full circle does.
	IInteractableInterface::Execute_OnInteract(Cat, Petter);
	TestTrue(TEXT("A pet waits for a spin"), Cat->IsAwaitingPlayerSpin());
	Turn(Cat, Near, 0.0f, 300.0f);
	TestEqual(TEXT("300 degrees is not a spin"), Cat->GetCompletedPetSpinRounds(), 0);
	Turn(Cat, Near, 300.0f, -300.0f);
	TestEqual(TEXT("Turning back cancels the turn"), Cat->GetCompletedPetSpinRounds(), 0);
	Turn(Cat, Far, 0.0f, 400.0f);
	TestEqual(TEXT("Turning far from the cat does not count"), Cat->GetCompletedPetSpinRounds(), 0);
	Turn(Cat, Near, 0.0f, -365.0f);
	TestEqual(TEXT("A full circle near the cat completes round 1 (either direction)"), Cat->GetCompletedPetSpinRounds(), 1);
	TestFalse(TEXT("The round is closed"), Cat->IsAwaitingPlayerSpin());

	// Petting twice in a row restarts the ritual with the second pet as its first.
	IInteractableInterface::Execute_OnInteract(Cat, Petter);
	IInteractableInterface::Execute_OnInteract(Cat, Petter);
	TestEqual(TEXT("A double pet restarts the ritual"), Cat->GetCompletedPetSpinRounds(), 0);
	TestTrue(TEXT("The restarting pet waits for a spin"), Cat->IsAwaitingPlayerSpin());
	Turn(Cat, Near, 90.0f, 370.0f);
	TestEqual(TEXT("Round 1 again"), Cat->GetCompletedPetSpinRounds(), 1);

	IInteractableInterface::Execute_OnInteract(Cat, Petter);
	Turn(Cat, Near, 0.0f, 360.0f);
	TestEqual(TEXT("Round 2"), Cat->GetCompletedPetSpinRounds(), 2);
	TestFalse(TEXT("Not active after two rounds"), Cat->IsFloatSpinActive());

	IInteractableInterface::Execute_OnInteract(Cat, Petter);
	TestEqual(TEXT("Five pets so far"), Cat->GetInteractionCount(), 5);
	TestFalse(TEXT("The key is still held before chaos mode"), Cat->HasDroppedKey());
	TestFalse(TEXT("The prompt shows no chaos count during the ritual"),
		Cat->GetInteractionPrompt_Implementation().ToString().Contains(TEXT("Chaos")));
	Turn(Cat, Near, 0.0f, 360.0f);
	TestTrue(TEXT("The third pet-and-spin round starts chaos mode"), Cat->IsFloatSpinActive());
	TestTrue(TEXT("Chaos Sanctuary is active"), Cat->IsChaosSanctuaryActive());
	TestTrue(TEXT("The key still held drops when chaos mode starts"), Cat->HasDroppedKey());

	// Pets alone never start chaos mode while the ritual is on.
	UProximityFloatSpinComponent* PettedOnly = SpawnOiiaiTestCat(World, FVector(5000.0f, 0.0f, 0.0f));
	if (TestNotNull(TEXT("Second cat"), PettedOnly))
	{
		PettedOnly->bRequirePetSpinSequence = true;
		PettedOnly->SpinTriggerInteractionThreshold = 20;
		for (int32 Pet = 0; Pet < 25; ++Pet)
		{
			IInteractableInterface::Execute_OnInteract(PettedOnly, PettedOnly->GetOwner());
		}
		TestFalse(TEXT("25 pets without spinning do not start chaos mode"), PettedOnly->IsFloatSpinActive());
		TestTrue(TEXT("The key still drops by petting"), PettedOnly->HasDroppedKey());
		TestFalse(TEXT("The prompt shows no chaos count"), PettedOnly->GetInteractionPrompt_Implementation().ToString().Contains(TEXT("Chaos")));
	}
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

	UProximityFloatSpinComponent* Cat = SpawnOiiaiTestCat(World, FVector::ZeroVector);
	if (!TestNotNull(TEXT("Cat component"), Cat))
	{
		return false;
	}

	Cat->ChaosSanctuaryRadius = 700.0f;
	Cat->SanctuaryRepelRadius = 400.0f;
	Cat->bRepelMonstersInSanctuary = true;

	TestFalse(TEXT("Sanctuary is dormant before float/spin activates"), Cat->IsChaosSanctuaryActive());

	// Trigger spin sequence
	Cat->SpinTriggerInteractionThreshold = 1;
	IInteractableInterface::Execute_OnInteract(Cat, Cat->GetOwner());
	TestTrue(TEXT("Float/spin is active"), Cat->IsFloatSpinActive());
	TestTrue(TEXT("Chaos Sanctuary is active"), Cat->IsChaosSanctuaryActive());

	// The outer sanctuary should protect a point outside the standard 500 cm cat flee radius.
	const FBoneDogTestRig Monster = SpawnBoneDog(World, FVector(600.0f, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("Monster exists"), Monster.Character) || !TestNotNull(TEXT("Monster AI exists"), Monster.AI))
	{
		return false;
	}

	static_cast<UActorComponent*>(Monster.AI)->TickComponent(0.05f, LEVELTICK_All, nullptr);

	// Monster in sanctuary perimeter must be repelled/fleeing
	TestTrue(TEXT("Monster inside Chaos Sanctuary is frightened"), Monster.AI->bCatIsNear);
	TestTrue(TEXT("Monster inside Chaos Sanctuary is fleeing"), Monster.AI->bIsFleeing);

	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/NPC/Lunger/base_basic_pbr.base_basic_pbr"));
	if (!TestNotNull(TEXT("Monster skeletal mesh for the ragdoll probe"), Mesh))
	{
		return false;
	}
	Monster.Character->GetMesh()->SetSkeletalMesh(Mesh);

	// The inner sanctuary should throw the monster even outside its normal 300 cm capsule-surface radius.
	Monster.Character->SetActorLocation(FVector(375.0f, 0.0f, 0.0f));
	++GFrameCounter;
	World->GetTimerManager().Tick(0.0f);
	static_cast<UActorComponent*>(Monster.AI)->TickComponent(0.05f, LEVELTICK_All, nullptr);

	TestTrue(TEXT("Monster inside repel radius entered ragdoll"), Monster.AI->bIsRagdolled);
	TestTrue(TEXT("Ragdoll bodies simulate in the sanctuary"), Monster.Character->GetMesh()->IsSimulatingPhysics());

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
