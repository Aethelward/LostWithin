// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "AI/MonsterAIComponent.h"
#include "AI/MonsterHearingComponent.h"
#include "AI/MonsterVoiceComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "Sound/SoundWave.h"
#include "UObject/Package.h"

namespace
{
	struct FScopedVoiceTestWorld
	{
		UWorld* World = nullptr;

		FScopedVoiceTestWorld()
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

		~FScopedVoiceTestWorld()
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

	FMonsterVoiceState MakeState(bool bInvestigating, bool bFleeing, bool bRagdolled)
	{
		FMonsterVoiceState State;
		State.bInvestigating = bInvestigating;
		State.bFleeing = bFleeing;
		State.bRagdolled = bRagdolled;
		return State;
	}

	USoundBase* MakeTestSound(const TCHAR* BaseName)
	{
		UPackage* Package = GetTransientPackage();
		return NewObject<USoundWave>(Package, MakeUniqueObjectName(Package, USoundWave::StaticClass(), FName(BaseName)));
	}

	ACharacter* SpawnTestCharacter(UWorld* World, const FVector& Location)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<ACharacter>(ACharacter::StaticClass(), FTransform(Location), Params);
	}

	/** Registers a voice on Actor after configuring it; every test sound counts as SoundSeconds long. */
	UMonsterVoiceComponent* AddVoice(ACharacter* Actor, const FVector2D& IdleRange, float SoundSeconds,
		int32 IdleCount, int32 AlertCount, int32 PanicCount, int32 ThrownCount)
	{
		UMonsterVoiceComponent* Voice = NewObject<UMonsterVoiceComponent>(Actor);
		for (int32 Index = 0; Index < IdleCount; ++Index) { Voice->IdleSounds.Add(MakeTestSound(TEXT("VoiceTest_Idle"))); }
		for (int32 Index = 0; Index < AlertCount; ++Index) { Voice->AlertSounds.Add(MakeTestSound(TEXT("VoiceTest_Alert"))); }
		for (int32 Index = 0; Index < PanicCount; ++Index) { Voice->PanicSounds.Add(MakeTestSound(TEXT("VoiceTest_Panic"))); }
		for (int32 Index = 0; Index < ThrownCount; ++Index) { Voice->ThrownSounds.Add(MakeTestSound(TEXT("VoiceTest_Thrown"))); }
		Voice->IdleIntervalRange = IdleRange;
		Voice->RepeatCooldown = 3.0f;
		Voice->SetRandomSeed(1234);
		Voice->SetSoundDurationProvider([SoundSeconds](const USoundBase*) { return SoundSeconds; });
		Voice->RegisterComponent();
		return Voice;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLostWithinMonsterVoiceRulesTest,
	"LostWithin.Audio.MonsterVoice.Rules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FLostWithinMonsterVoiceRulesTest::RunTest(const FString& Parameters)
{
	using namespace MonsterVoiceRules;
	const FMonsterVoiceState Calm = MakeState(false, false, false);

	TestTrue(TEXT("investigating rising edge is Alert"), DetectEvent(Calm, MakeState(true, false, false)) == EMonsterVoiceEvent::Alert);
	TestTrue(TEXT("continued investigating is no event"), DetectEvent(MakeState(true, false, false), MakeState(true, false, false)) == EMonsterVoiceEvent::None);
	TestTrue(TEXT("fleeing rising edge is Panic"), DetectEvent(Calm, MakeState(false, true, false)) == EMonsterVoiceEvent::Panic);
	TestTrue(TEXT("ragdoll rising edge is Thrown"), DetectEvent(MakeState(false, true, false), MakeState(false, false, true)) == EMonsterVoiceEvent::Thrown);
	TestTrue(TEXT("simultaneous edges pick the highest priority"), DetectEvent(Calm, MakeState(true, true, true)) == EMonsterVoiceEvent::Thrown);
	TestTrue(TEXT("falling edges are no event"), DetectEvent(MakeState(true, true, true), Calm) == EMonsterVoiceEvent::None);

	TestTrue(TEXT("anything plays over silence"), CanInterrupt(EMonsterVoiceEvent::None, EMonsterVoiceEvent::Idle));
	TestTrue(TEXT("Alert interrupts Idle"), CanInterrupt(EMonsterVoiceEvent::Idle, EMonsterVoiceEvent::Alert));
	TestTrue(TEXT("Thrown interrupts Panic"), CanInterrupt(EMonsterVoiceEvent::Panic, EMonsterVoiceEvent::Thrown));
	TestFalse(TEXT("Idle does not interrupt Alert"), CanInterrupt(EMonsterVoiceEvent::Alert, EMonsterVoiceEvent::Idle));
	TestFalse(TEXT("equal priority does not interrupt"), CanInterrupt(EMonsterVoiceEvent::Panic, EMonsterVoiceEvent::Panic));
	TestFalse(TEXT("Panic does not interrupt Thrown"), CanInterrupt(EMonsterVoiceEvent::Thrown, EMonsterVoiceEvent::Panic));
	TestFalse(TEXT("None never plays"), CanInterrupt(EMonsterVoiceEvent::None, EMonsterVoiceEvent::None));

	TestTrue(TEXT("idle while roaming"), CanPlayIdle(Calm));
	TestTrue(TEXT("idle while investigating"), CanPlayIdle(MakeState(true, false, false)));
	TestFalse(TEXT("no idle while fleeing"), CanPlayIdle(MakeState(false, true, false)));
	TestFalse(TEXT("no idle while ragdolled"), CanPlayIdle(MakeState(false, false, true)));

	TestFalse(TEXT("Panic waits for its cooldown"), IsPastCooldown(EMonsterVoiceEvent::Panic, 2.9, 3.0f));
	TestTrue(TEXT("Panic repeats after its cooldown"), IsPastCooldown(EMonsterVoiceEvent::Panic, 3.0, 3.0f));
	TestFalse(TEXT("Alert waits for its cooldown"), IsPastCooldown(EMonsterVoiceEvent::Alert, 0.1, 3.0f));
	TestTrue(TEXT("Thrown has no cooldown"), IsPastCooldown(EMonsterVoiceEvent::Thrown, 0.1, 3.0f));

	FRandomStream Stream(42);
	TestEqual(TEXT("no sounds gives no index"), PickIndexNoRepeat(0, INDEX_NONE, Stream), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("one sound may repeat"), PickIndexNoRepeat(1, 0, Stream), 0);
	int32 Last = INDEX_NONE;
	TSet<int32> Seen;
	bool bRepeated = false;
	for (int32 Draw = 0; Draw < 200; ++Draw)
	{
		const int32 Index = PickIndexNoRepeat(3, Last, Stream);
		bRepeated |= (Index == Last);
		Seen.Add(Index);
		Last = Index;
	}
	TestFalse(TEXT("never the same sound twice in a row"), bRepeated);
	TestEqual(TEXT("every sound is reachable"), Seen.Num(), 3);

	for (int32 Draw = 0; Draw < 50; ++Draw)
	{
		const float Delay = NextIdleDelay(FVector2D(20.0, 8.0), Stream);
		if (!TestTrue(TEXT("idle delay within a reversed range"), Delay >= 8.0f && Delay <= 20.0f))
		{
			break;
		}
	}
	TestEqual(TEXT("idle delay never below 0.5 s"), NextIdleDelay(FVector2D(0.0, 0.0), Stream), 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLostWithinMonsterVoiceComponentTest,
	"LostWithin.Audio.MonsterVoice.Component",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FLostWithinMonsterVoiceComponentTest::RunTest(const FString& Parameters)
{
	FScopedVoiceTestWorld TestWorld;
	UWorld* World = TestWorld.World;
	if (!TestNotNull(TEXT("test world"), World))
	{
		return false;
	}
	const FMonsterVoiceState Calm = MakeState(false, false, false);
	const FMonsterVoiceState Investigating = MakeState(true, false, false);
	const FMonsterVoiceState Fleeing = MakeState(false, true, false);
	const FMonsterVoiceState Ragdolled = MakeState(false, false, true);

	// Reading state: a monster without a hearing component (Lunger) is never investigating.
	ACharacter* Deaf = SpawnTestCharacter(World, FVector(0.0, 0.0, 500.0));
	if (!TestNotNull(TEXT("deaf monster"), Deaf))
	{
		return false;
	}
	UMonsterAIComponent* DeafAI = NewObject<UMonsterAIComponent>(Deaf);
	DeafAI->RegisterComponent();
	UMonsterVoiceComponent* Voice = AddVoice(Deaf, FVector2D(1.0, 1.0), 2.0f, 2, 1, 1, 1);
	DeafAI->bIsFleeing = true;
	TestTrue(TEXT("reads fleeing from MonsterAI"), Voice->ReadObservedState().bFleeing);
	TestFalse(TEXT("no hearing component means never investigating"), Voice->ReadObservedState().bInvestigating);
	DeafAI->bIsFleeing = false;
	DeafAI->bIsRagdolled = true;
	TestTrue(TEXT("reads ragdoll from MonsterAI"), Voice->ReadObservedState().bRagdolled);
	DeafAI->bIsRagdolled = false;

	ACharacter* Listener = SpawnTestCharacter(World, FVector(2000.0, 0.0, 500.0));
	if (TestNotNull(TEXT("hearing monster"), Listener))
	{
		UMonsterHearingComponent* Hearing = NewObject<UMonsterHearingComponent>(Listener);
		Hearing->RegisterComponent();
		UMonsterVoiceComponent* ListenerVoice = AddVoice(Listener, FVector2D(10.0, 10.0), 1.0f, 1, 1, 1, 1);
		TestFalse(TEXT("an idle hearing component is not investigating"), ListenerVoice->ReadObservedState().bInvestigating);
	}

	// Event sequence with the idle interval at exactly 1 s and every sound 2 s long.
	Voice->UpdateVoice(Calm, 0.5f);
	TestEqual(TEXT("no idle before the interval"), Voice->GetNumVoicesStarted(), 0);
	Voice->UpdateVoice(Calm, 0.6f);
	TestTrue(TEXT("idle after the interval"), Voice->GetLastPlayedEvent() == EMonsterVoiceEvent::Idle);
	TestEqual(TEXT("one vocal so far"), Voice->GetNumVoicesStarted(), 1);
	TestTrue(TEXT("vocal is audible"), Voice->IsVoiceAudible());

	Voice->UpdateVoice(Investigating, 0.1f);
	TestTrue(TEXT("Alert interrupts the idle vocal"), Voice->GetLastPlayedEvent() == EMonsterVoiceEvent::Alert);
	TestEqual(TEXT("two vocals"), Voice->GetNumVoicesStarted(), 2);

	Voice->UpdateVoice(Investigating, 1.1f);
	TestEqual(TEXT("the idle timer firing during Alert does not interrupt it"), Voice->GetNumVoicesStarted(), 2);
	TestTrue(TEXT("Alert is still the last vocal"), Voice->GetLastPlayedEvent() == EMonsterVoiceEvent::Alert);

	Voice->UpdateVoice(Fleeing, 0.1f);
	TestTrue(TEXT("Panic interrupts Alert"), Voice->GetLastPlayedEvent() == EMonsterVoiceEvent::Panic);
	TestEqual(TEXT("three vocals"), Voice->GetNumVoicesStarted(), 3);
	Voice->UpdateVoice(Fleeing, 5.0f);
	TestEqual(TEXT("no idle while fleeing"), Voice->GetNumVoicesStarted(), 3);

	Voice->UpdateVoice(Ragdolled, 0.1f);
	TestTrue(TEXT("Thrown plays on the ragdoll edge"), Voice->GetLastPlayedEvent() == EMonsterVoiceEvent::Thrown);
	Voice->UpdateVoice(Ragdolled, 5.0f);
	TestEqual(TEXT("no idle while ragdolled"), Voice->GetNumVoicesStarted(), 4);

	// Cooldown: idle far away, short sounds, so only the cooldown can block a repeat.
	ACharacter* Flicker = SpawnTestCharacter(World, FVector(4000.0, 0.0, 500.0));
	if (TestNotNull(TEXT("cooldown monster"), Flicker))
	{
		UMonsterVoiceComponent* Cooldown = AddVoice(Flicker, FVector2D(100.0, 100.0), 0.5f, 1, 1, 1, 1);
		Cooldown->UpdateVoice(Fleeing, 0.1f);
		TestEqual(TEXT("first Panic plays"), Cooldown->GetNumVoicesStarted(), 1);
		Cooldown->UpdateVoice(Calm, 1.0f);
		Cooldown->UpdateVoice(Fleeing, 0.1f);
		TestEqual(TEXT("a Panic 1.1 s later is inside the cooldown"), Cooldown->GetNumVoicesStarted(), 1);
		Cooldown->UpdateVoice(Calm, 2.0f);
		Cooldown->UpdateVoice(Fleeing, 0.1f);
		TestEqual(TEXT("a Panic 3.2 s later plays"), Cooldown->GetNumVoicesStarted(), 2);
		Cooldown->UpdateVoice(Ragdolled, 0.1f);
		Cooldown->UpdateVoice(Calm, 0.6f);
		Cooldown->UpdateVoice(Ragdolled, 0.1f);
		TestEqual(TEXT("Thrown has no cooldown"), Cooldown->GetNumVoicesStarted(), 4);
	}

	// No repeats: two idle sounds that end before the next idle.
	ACharacter* Roamer = SpawnTestCharacter(World, FVector(6000.0, 0.0, 500.0));
	if (TestNotNull(TEXT("roaming monster"), Roamer))
	{
		UMonsterVoiceComponent* Idle = AddVoice(Roamer, FVector2D(1.0, 1.0), 0.5f, 2, 0, 0, 0);
		const USoundBase* Previous = nullptr;
		bool bRepeated = false;
		for (int32 Step = 0; Step < 6; ++Step)
		{
			Idle->UpdateVoice(Calm, 1.05f);
			bRepeated |= (Idle->GetLastPlayedSound() == Previous);
			Previous = Idle->GetLastPlayedSound();
		}
		TestEqual(TEXT("an idle vocal every interval"), Idle->GetNumVoicesStarted(), 6);
		TestFalse(TEXT("consecutive idle vocals differ"), bRepeated);
	}

	// No sounds: nothing plays and the component does not tick.
	ACharacter* Silent = SpawnTestCharacter(World, FVector(8000.0, 0.0, 500.0));
	if (TestNotNull(TEXT("silent monster"), Silent))
	{
		UMonsterVoiceComponent* Empty = AddVoice(Silent, FVector2D(1.0, 1.0), 1.0f, 0, 0, 0, 0);
		TestFalse(TEXT("a voice without sounds does not tick"), Empty->IsComponentTickEnabled());
		Empty->UpdateVoice(Calm, 5.0f);
		Empty->UpdateVoice(MakeState(true, true, true), 0.1f);
		TestFalse(TEXT("PlayVoice without sounds fails"), Empty->PlayVoice(EMonsterVoiceEvent::Thrown));
		TestEqual(TEXT("nothing played"), Empty->GetNumVoicesStarted(), 0);
	}
	return true;
}

#endif
