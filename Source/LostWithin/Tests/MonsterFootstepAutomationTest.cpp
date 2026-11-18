// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "AI/MonsterFootstepComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "Sound/SoundWave.h"
#include "UObject/Package.h"

namespace
{
	struct FScopedFootstepTestWorld
	{
		UWorld* World = nullptr;

		FScopedFootstepTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			if (World && GEngine)
			{
				GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
				World->InitializeActorsForPlay(FURL());
				World->BeginPlay();
				if (AWorldSettings* Settings = World->GetWorldSettings())
				{
					Settings->NotifyBeginPlay();
				}
			}
		}

		~FScopedFootstepTestWorld()
		{
			if (World)
			{
				World->EndPlay(EEndPlayReason::Quit);
				World->DestroyWorld(false);
				if (GEngine)
				{
					GEngine->DestroyWorldContext(World);
				}
			}
			CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
		}
	};

	UMonsterFootstepComponent* AddFootsteps(UWorld* World, const FVector& Location, int32 SoundCount)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ACharacter* Actor = World->SpawnActor<ACharacter>(ACharacter::StaticClass(), FTransform(Location), Params);
		if (!Actor)
		{
			return nullptr;
		}
		UMonsterFootstepComponent* Steps = NewObject<UMonsterFootstepComponent>(Actor);
		UPackage* Package = GetTransientPackage();
		for (int32 Index = 0; Index < SoundCount; ++Index)
		{
			Steps->FootstepSounds.Add(NewObject<USoundWave>(Package,
				MakeUniqueObjectName(Package, USoundWave::StaticClass(), FName(TEXT("StepTest")))));
		}
		Steps->StrideLength = 70.0f;
		Steps->MinSpeed = 20.0f;
		Steps->SetRandomSeed(77);
		Steps->RegisterComponent();
		return Steps;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLostWithinMonsterFootstepRulesTest,
	"LostWithin.Audio.MonsterFootsteps.Rules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FLostWithinMonsterFootstepRulesTest::RunTest(const FString& Parameters)
{
	using namespace MonsterFootstepRules;
	TestTrue(TEXT("walking on the ground counts"), ShouldAccumulate(62.6f, true, 20.0f));
	TestTrue(TEXT("exactly MinSpeed counts"), ShouldAccumulate(20.0f, true, 20.0f));
	TestFalse(TEXT("airborne does not count"), ShouldAccumulate(300.0f, false, 20.0f));
	TestFalse(TEXT("slower than MinSpeed does not count"), ShouldAccumulate(10.0f, true, 20.0f));
	TestFalse(TEXT("standing still never counts, even with MinSpeed 0"), ShouldAccumulate(0.0f, true, 0.0f));

	float Accumulated = 35.0f;
	TestEqual(TEXT("35 + 50 crosses one 70 cm stride"), Advance(Accumulated, 50.0f, 70.0f), 1);
	TestEqual(TEXT("remainder carried"), Accumulated, 15.0f);
	TestEqual(TEXT("no distance, no step"), Advance(Accumulated, 0.0f, 70.0f), 0);
	TestEqual(TEXT("negative distance ignored"), Advance(Accumulated, -100.0f, 70.0f), 0);
	TestEqual(TEXT("remainder unchanged"), Accumulated, 15.0f);
	TestEqual(TEXT("300 cm more is four strides"), Advance(Accumulated, 300.0f, 70.0f), 4);
	TestEqual(TEXT("leftover after four strides"), Accumulated, 35.0f);
	float Tiny = 0.0f;
	TestEqual(TEXT("stride is clamped to at least 10 cm"), Advance(Tiny, 25.0f, 0.0f), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLostWithinMonsterFootstepComponentTest,
	"LostWithin.Audio.MonsterFootsteps.Component",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FLostWithinMonsterFootstepComponentTest::RunTest(const FString& Parameters)
{
	FScopedFootstepTestWorld TestWorld;
	UWorld* World = TestWorld.World;
	if (!TestNotNull(TEXT("test world"), World))
	{
		return false;
	}

	UMonsterFootstepComponent* Steps = AddFootsteps(World, FVector(0.0, 0.0, 500.0), 2);
	if (!TestNotNull(TEXT("footstep component"), Steps))
	{
		return false;
	}
	// BeginPlay primes half a stride (35 cm), so the first step lands after 35 cm of walking.
	Steps->UpdateFootsteps(0.3f, 100.0f, true);
	TestEqual(TEXT("30 cm: no step yet"), Steps->GetNumStepsPlayed(), 0);
	Steps->UpdateFootsteps(0.1f, 100.0f, true);
	TestEqual(TEXT("40 cm: first step"), Steps->GetNumStepsPlayed(), 1);
	Steps->UpdateFootsteps(0.6f, 100.0f, true);
	TestEqual(TEXT("100 cm: second step not due until 105 cm"), Steps->GetNumStepsPlayed(), 1);
	Steps->UpdateFootsteps(0.1f, 100.0f, true);
	TestEqual(TEXT("110 cm: second step"), Steps->GetNumStepsPlayed(), 2);

	Steps->UpdateFootsteps(2.0f, 300.0f, false);
	TestEqual(TEXT("airborne or ragdolled: silent"), Steps->GetNumStepsPlayed(), 2);
	Steps->UpdateFootsteps(5.0f, 10.0f, true);
	TestEqual(TEXT("creeping below MinSpeed: silent"), Steps->GetNumStepsPlayed(), 2);
	Steps->UpdateFootsteps(5.0f, 0.0f, true);
	TestEqual(TEXT("standing: silent"), Steps->GetNumStepsPlayed(), 2);

	Steps->UpdateFootsteps(5.0f, 300.0f, true);
	TestEqual(TEXT("a 15 m hitch plays one step, not a burst"), Steps->GetNumStepsPlayed(), 3);

	const USoundBase* Previous = Steps->GetLastStepSound();
	bool bRepeated = false;
	for (int32 Step = 0; Step < 20; ++Step)
	{
		Steps->UpdateFootsteps(0.2f, 350.0f, true);   // 70 cm per update: one stride each
		bRepeated |= (Steps->GetLastStepSound() == Previous);
		Previous = Steps->GetLastStepSound();
	}
	TestEqual(TEXT("one step per stride while fleeing"), Steps->GetNumStepsPlayed(), 23);
	TestFalse(TEXT("consecutive takes differ"), bRepeated);

	UMonsterFootstepComponent* Silent = AddFootsteps(World, FVector(3000.0, 0.0, 500.0), 0);
	if (TestNotNull(TEXT("component without sounds"), Silent))
	{
		TestFalse(TEXT("no sounds: no tick"), Silent->IsComponentTickEnabled());
		Silent->UpdateFootsteps(5.0f, 300.0f, true);
		TestEqual(TEXT("no sounds: nothing played"), Silent->GetNumStepsPlayed(), 0);
	}
	return true;
}

#endif
