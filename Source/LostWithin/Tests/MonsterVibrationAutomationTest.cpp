// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "AI/MonsterAIComponent.h"
#include "AI/MonsterVibrationComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "Gameplay/LostWithinNoiseSubsystem.h"
#include "Misc/AutomationTest.h"

#define LOSTWITHIN_VIBRATION_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

namespace
{
	struct FScopedVibrationTestWorld
	{
		UWorld* World = nullptr;

		FScopedVibrationTestWorld()
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

		~FScopedVibrationTestWorld()
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

	struct FVibrationMonsterSetup
	{
		ACharacter* Actor = nullptr;
		UMonsterAIComponent* AI = nullptr;
		UMonsterVibrationComponent* Vibration = nullptr;
	};

	FVibrationMonsterSetup SpawnVibrationMonster(UWorld* World, const FVector& Location)
	{
		FVibrationMonsterSetup Result;
		const FTransform Transform(Location);
		Result.Actor = World->SpawnActorDeferred<ACharacter>(ACharacter::StaticClass(), Transform, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Result.Actor)
		{
			return Result;
		}

		Result.Actor->AutoPossessAI = EAutoPossessAI::Disabled;
		Result.AI = NewObject<UMonsterAIComponent>(Result.Actor, TEXT("MonsterAI"));
		Result.AI->RegisterComponent();

		Result.Vibration = NewObject<UMonsterVibrationComponent>(Result.Actor, TEXT("VibrationSense"));
		Result.Vibration->RegisterComponent();

		Result.Actor->FinishSpawning(Transform);
		// The synthetic world has no floor, so establish the grounded state a maze NPC starts in.
		Result.Actor->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		return Result;
	}

	ACharacter* SpawnTestPawn(UWorld* World, const FVector& Location)
	{
		const FTransform Transform(Location);
		ACharacter* Pawn = World->SpawnActorDeferred<ACharacter>(ACharacter::StaticClass(), Transform, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Pawn)
		{
			Pawn->AutoPossessAI = EAutoPossessAI::Disabled;
			Pawn->FinishSpawning(Transform);
		}
		return Pawn;
	}

	void TickVibrationComponent(UMonsterVibrationComponent* Component, float DeltaTime)
	{
		if (Component)
		{
			static_cast<UActorComponent*>(Component)->TickComponent(DeltaTime, LEVELTICK_All, nullptr);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLostWithinMonsterBlindAndDeafTest,
	"LostWithin.AI.MonsterVibration.BlindAndDeaf", LOSTWITHIN_VIBRATION_TEST_FLAGS)

bool FLostWithinMonsterBlindAndDeafTest::RunTest(const FString& Parameters)
{
	FScopedVibrationTestWorld Env;
	UWorld* World = Env.World;
	if (!TestNotNull(TEXT("World exists"), World))
	{
		return false;
	}

	const FVibrationMonsterSetup Monster = SpawnVibrationMonster(World, FVector::ZeroVector);
	TestNotNull(TEXT("Monster actor created"), Monster.Actor);
	TestNotNull(TEXT("Monster AI created"), Monster.AI);
	TestNotNull(TEXT("Monster Vibration created"), Monster.Vibration);

	// 1. Blindness: AI sight radius forced to 0 by vibration component.
	TestTrue(TEXT("Monster is blind by default"), Monster.Vibration->bBlind);
	TestEqual(TEXT("Player detection radius enforced to 0"), Monster.AI->PlayerDetectionRadius, 0.0f);
	TestFalse(TEXT("Cannot see player in front"), Monster.AI->IsPlayerInSight());

	// 2. Deafness to airborne sounds: ignores item impacts and other non-footstep noises.
	TestTrue(TEXT("Monster is deaf to airborne sounds"), Monster.Vibration->bDeafToAirborne);

	const FVector NoiseLoc(300.0f, 0.0f, 0.0f);
	// Report item impact noise (e.g. dropped key/bottle).
	ULostWithinNoiseSubsystem::ReportNoise(World, NoiseLoc, 1000.0f, ELostWithinNoiseKind::ItemImpact, nullptr);
	TestFalse(TEXT("Airborne item impact does not trigger vibration investigation"), Monster.Vibration->IsInvestigating());
	TestTrue(TEXT("Normal roaming AI remains available"), UMonsterAIComponent::CanRunNormalAI(Monster.Actor));

	// Report generic other noise.
	ULostWithinNoiseSubsystem::ReportNoise(World, NoiseLoc, 1000.0f, ELostWithinNoiseKind::Other, nullptr);
	TestFalse(TEXT("Generic airborne noise does not trigger vibration investigation"), Monster.Vibration->IsInvestigating());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLostWithinMonsterFloorTremorsTest,
	"LostWithin.AI.MonsterVibration.FloorTremors", LOSTWITHIN_VIBRATION_TEST_FLAGS)

bool FLostWithinMonsterFloorTremorsTest::RunTest(const FString& Parameters)
{
	FScopedVibrationTestWorld Env;
	UWorld* World = Env.World;
	if (!TestNotNull(TEXT("World exists"), World))
	{
		return false;
	}

	const FVibrationMonsterSetup Monster = SpawnVibrationMonster(World, FVector::ZeroVector);
	Monster.Vibration->InvestigateSpeed = 400.0f;
	UCharacterMovementComponent* MoveComp = Monster.Actor->GetCharacterMovement();
	MoveComp->MaxWalkSpeed = 150.0f;

	// Distant sprint footstep within 1200 cm radius.
	const FVector SprintLocation(800.0f, 0.0f, 0.0f);
	TestTrue(TEXT("Can sense floor vibration in range"),
		Monster.Vibration->CanSenseFloorVibration(SprintLocation, Monster.Vibration->SprintVibrationRadius));

	// Sprint footstep reported via noise subsystem.
	ULostWithinNoiseSubsystem::ReportNoise(World, SprintLocation, 1000.0f, ELostWithinNoiseKind::Footstep, nullptr);

	TestTrue(TEXT("Monster senses sprint tremor and investigates"), Monster.Vibration->IsInvestigating());
	TestEqual(TEXT("Target location set to vibration origin"), Monster.Vibration->InvestigationLocation, SprintLocation);
	TestEqual(TEXT("Speed increased to investigate speed"), MoveComp->MaxWalkSpeed, 400.0f);
	TestFalse(TEXT("Normal AI roam paused during investigation"), UMonsterAIComponent::CanRunNormalAI(Monster.Actor));

	// Walk footstep with small radius (< 500 cm) is ignored at 800 cm.
	Monster.Vibration->StopInvestigating();
	TestFalse(TEXT("Investigation stopped"), Monster.Vibration->IsInvestigating());
	TestEqual(TEXT("Speed restored to roam speed"), MoveComp->MaxWalkSpeed, 150.0f);

	ULostWithinNoiseSubsystem::ReportNoise(World, SprintLocation, 300.0f, ELostWithinNoiseKind::Footstep, nullptr);
	TestFalse(TEXT("Light walking footstep does not trigger floor tremor"), Monster.Vibration->IsInvestigating());

	// Floor vertical separation check: tremor does not conduct across floors (Z difference > 250 cm).
	const FVector HighFloorLocation(500.0f, 0.0f, 400.0f);
	TestFalse(TEXT("Floor tremor does not conduct across large vertical gap"),
		Monster.Vibration->CanSenseFloorVibration(HighFloorLocation, Monster.Vibration->SprintVibrationRadius));

	MoveComp->SetMovementMode(MOVE_Falling);
	TestFalse(TEXT("Airborne monster does not sense floor tremors"),
		Monster.Vibration->CanSenseFloorVibration(SprintLocation, Monster.Vibration->SprintVibrationRadius));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLostWithinMonsterLandingTremorTest,
	"LostWithin.AI.MonsterVibration.LandingTremor", LOSTWITHIN_VIBRATION_TEST_FLAGS)

bool FLostWithinMonsterLandingTremorTest::RunTest(const FString& Parameters)
{
	FScopedVibrationTestWorld Env;
	UWorld* World = Env.World;
	if (!TestNotNull(TEXT("World exists"), World))
	{
		return false;
	}

	const FVibrationMonsterSetup Monster = SpawnVibrationMonster(World, FVector::ZeroVector);
	const FVector LandingLocation(1000.0f, 0.0f, 0.0f);

	TestTrue(TEXT("Landing tremor can be sensed at 1000 cm"),
		Monster.Vibration->CanSenseFloorVibration(LandingLocation, Monster.Vibration->LandingVibrationRadius));

	const bool bSensed = Monster.Vibration->SenseFloorVibration(LandingLocation, Monster.Vibration->LandingVibrationRadius);
	TestTrue(TEXT("Landing tremor sensed successfully"), bSensed);
	TestTrue(TEXT("Investigating landing tremor"), Monster.Vibration->IsInvestigating());
	TestEqual(TEXT("Sense kind classified as LandingTremor"),
		Monster.Vibration->LastSenseKind, EMonsterVibrationSenseKind::LandingTremor);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLostWithinMonsterTurbulenceTest,
	"LostWithin.AI.MonsterVibration.CloseRangeTurbulence", LOSTWITHIN_VIBRATION_TEST_FLAGS)

bool FLostWithinMonsterTurbulenceTest::RunTest(const FString& Parameters)
{
	FScopedVibrationTestWorld Env;
	UWorld* World = Env.World;
	if (!TestNotNull(TEXT("World exists"), World))
	{
		return false;
	}

	const FVibrationMonsterSetup Monster = SpawnVibrationMonster(World, FVector::ZeroVector);
	ACharacter* PlayerPawn = SpawnTestPawn(World, FVector(200.0f, 0.0f, 0.0f));
	TestNotNull(TEXT("Player pawn spawned"), PlayerPawn);

	// Case 1: Player is motionless (standing completely still / holding breath).
	PlayerPawn->GetCharacterMovement()->Velocity = FVector::ZeroVector;
	TestFalse(TEXT("Motionless player generates no air turbulence"), Monster.Vibration->CanSenseTurbulence(PlayerPawn));

	// Case 2: Player takes a step or sneaks (velocity = 50 cm/s > threshold of 15 cm/s).
	PlayerPawn->GetCharacterMovement()->Velocity = FVector(50.0f, 0.0f, 0.0f);
	TestTrue(TEXT("Moving player within turbulence radius is detected"), Monster.Vibration->CanSenseTurbulence(PlayerPawn));

	// Trigger turbulence sensing.
	const bool bSensed = Monster.Vibration->SenseTurbulence(PlayerPawn->GetActorLocation(), PlayerPawn);
	TestTrue(TEXT("Turbulence sense successful"), bSensed);
	TestTrue(TEXT("Investigating turbulence location"), Monster.Vibration->IsInvestigating());
	TestEqual(TEXT("Sense kind is ProximityTurbulence"), Monster.Vibration->LastSenseKind, EMonsterVibrationSenseKind::ProximityTurbulence);
	TestTrue(TEXT("Monster AI player proximity flagged"), Monster.AI->bPlayerIsNear);

	// Case 3: Player moving outside turbulence radius (e.g. 500 cm > 300 cm).
	PlayerPawn->SetActorLocation(FVector(500.0f, 0.0f, 0.0f));
	TestFalse(TEXT("Player moving beyond turbulence radius is not detected via air turbulence"),
		Monster.Vibration->CanSenseTurbulence(PlayerPawn));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLostWithinMonsterSearchAndCatPriorityTest,
	"LostWithin.AI.MonsterVibration.SearchAndCatPriority", LOSTWITHIN_VIBRATION_TEST_FLAGS)

bool FLostWithinMonsterSearchAndCatPriorityTest::RunTest(const FString& Parameters)
{
	FScopedVibrationTestWorld Env;
	UWorld* World = Env.World;
	if (!TestNotNull(TEXT("World exists"), World))
	{
		return false;
	}

	const FVibrationMonsterSetup Monster = SpawnVibrationMonster(World, FVector::ZeroVector);
	Monster.Vibration->SearchDuration = 0.4f;
	Monster.Actor->GetCharacterMovement()->MaxWalkSpeed = 150.0f;

	const FVector TargetLocation(300.0f, 0.0f, 0.0f);
	Monster.Vibration->SenseFloorVibration(TargetLocation, 1000.0f);
	TestTrue(TEXT("Investigating vibration"), Monster.Vibration->IsInvestigating());

	// Simulate arrival at the sensed location.
	Monster.Actor->SetActorLocation(TargetLocation);
	TickVibrationComponent(Monster.Vibration, 0.1f);
	TestTrue(TEXT("Monster arrives and begins searching"), Monster.Vibration->IsSearching());
	TestFalse(TEXT("Normal roam still paused during search"), UMonsterAIComponent::CanRunNormalAI(Monster.Actor));

	// Tick past SearchDuration to complete search.
	for (int32 Step = 0; Step < 6; ++Step)
	{
		TickVibrationComponent(Monster.Vibration, 0.1f);
	}
	TestFalse(TEXT("Search ends after duration"), Monster.Vibration->IsInvestigating());
	TestTrue(TEXT("Roaming resumes after search completes"), UMonsterAIComponent::CanRunNormalAI(Monster.Actor));
	TestEqual(TEXT("Speed restored to normal roam speed"), Monster.Actor->GetCharacterMovement()->MaxWalkSpeed, 150.0f);

	// Cat priority override test.
	Monster.Vibration->SenseFloorVibration(TargetLocation, 1000.0f);
	TestTrue(TEXT("Investigating again"), Monster.Vibration->IsInvestigating());

	// Threatening cat appears!
	Monster.AI->bCatIsNear = true;
	TickVibrationComponent(Monster.Vibration, 0.1f);
	TestFalse(TEXT("Cat panic overrides and cancels vibration investigation"), Monster.Vibration->IsInvestigating());

	// New vibrations are rejected while fearing the cat.
	TestFalse(TEXT("Vibration rejected while fearing cat"),
		Monster.Vibration->SenseFloorVibration(TargetLocation, 1000.0f));

	Monster.AI->bCatIsNear = false;
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
