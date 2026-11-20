// Copyright Epic Games, Inc. All Rights Reserved.

#include "AI/MonsterVoiceComponent.h"

#include "AI/MonsterAIComponent.h"
#include "AI/MonsterHearingComponent.h"
#include "AI/MonsterSenseProfile.h"
#include "Components/AudioComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "LostWithin.h"
#include "Sound/SoundBase.h"

namespace
{
	constexpr int32 NumVoiceEvents = static_cast<int32>(EMonsterVoiceEvent::Thrown) + 1;
	constexpr float MinIdleDelaySeconds = 0.5f;
	constexpr float VoiceMinPitch = 0.25f;
	constexpr float VoiceMaxPitch = 4.0f;
	constexpr double NeverPlayed = -1.0e9;

	int32 EventSlot(EMonsterVoiceEvent Event)
	{
		return FMath::Clamp(static_cast<int32>(Event), 0, NumVoiceEvents - 1);
	}
}

namespace MonsterVoiceRules
{
	EMonsterVoiceEvent DetectEvent(const FMonsterVoiceState& Previous, const FMonsterVoiceState& Current)
	{
		if (Current.bRagdolled && !Previous.bRagdolled)
		{
			return EMonsterVoiceEvent::Thrown;
		}
		if (Current.bFleeing && !Previous.bFleeing)
		{
			return EMonsterVoiceEvent::Panic;
		}
		if (Current.bInvestigating && !Previous.bInvestigating)
		{
			return EMonsterVoiceEvent::Alert;
		}
		return EMonsterVoiceEvent::None;
	}

	int32 Priority(EMonsterVoiceEvent Event)
	{
		return static_cast<int32>(Event);
	}

	bool CanInterrupt(EMonsterVoiceEvent PlayingEvent, EMonsterVoiceEvent NewEvent)
	{
		if (NewEvent == EMonsterVoiceEvent::None)
		{
			return false;
		}
		return PlayingEvent == EMonsterVoiceEvent::None || Priority(NewEvent) > Priority(PlayingEvent);
	}

	bool CanPlayIdle(const FMonsterVoiceState& State)
	{
		return !State.bFleeing && !State.bRagdolled;
	}

	bool IsPastCooldown(EMonsterVoiceEvent Event, double SecondsSinceSameEvent, float Cooldown)
	{
		if (Event != EMonsterVoiceEvent::Alert && Event != EMonsterVoiceEvent::Panic)
		{
			return true;
		}
		return SecondsSinceSameEvent >= FMath::Max(0.0f, Cooldown);
	}

	int32 PickIndexNoRepeat(int32 Count, int32 LastIndex, FRandomStream& Stream)
	{
		if (Count <= 0)
		{
			return INDEX_NONE;
		}
		int32 Index = Stream.RandRange(0, Count - 1);
		if (Count > 1 && Index == LastIndex)
		{
			Index = (Index + 1) % Count;
		}
		return Index;
	}

	float NextIdleDelay(const FVector2D& Range, FRandomStream& Stream)
	{
		const float Low = FMath::Max(MinIdleDelaySeconds, static_cast<float>(FMath::Min(Range.X, Range.Y)));
		const float High = FMath::Max(Low, static_cast<float>(FMath::Max(Range.X, Range.Y)));
		return Stream.FRandRange(Low, High);
	}

	const TCHAR* EventName(EMonsterVoiceEvent Event)
	{
		switch (Event)
		{
		case EMonsterVoiceEvent::Idle: return TEXT("Idle");
		case EMonsterVoiceEvent::Alert: return TEXT("Alert");
		case EMonsterVoiceEvent::Panic: return TEXT("Panic");
		case EMonsterVoiceEvent::Thrown: return TEXT("Thrown");
		default: return TEXT("None");
		}
	}
}

UMonsterVoiceComponent::UMonsterVoiceComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickInterval = 0.1f;

	// Audible across a few maze corridors, muffled with distance, silent beyond about 27 m.
	Attenuation.bAttenuate = true;
	Attenuation.bSpatialize = true;
	Attenuation.DistanceAlgorithm = EAttenuationDistanceModel::NaturalSound;
	Attenuation.AttenuationShape = EAttenuationShape::Sphere;
	Attenuation.AttenuationShapeExtents = FVector(250.0, 0.0, 0.0);
	Attenuation.FalloffDistance = 2500.0f;
	Attenuation.bAttenuateWithLPF = true;
	Attenuation.LPFRadiusMin = 600.0f;
	Attenuation.LPFRadiusMax = 2750.0f;
	Attenuation.LPFFrequencyAtMin = 20000.0f;
	Attenuation.LPFFrequencyAtMax = 3000.0f;

	// Headphones: binaural HRTF panning (Resonance Audio in the project audio settings), muffled through walls.
	Attenuation.SpatializationAlgorithm = ESoundSpatializationAlgorithm::SPATIALIZATION_HRTF;
	Attenuation.bEnableOcclusion = true;
	Attenuation.OcclusionTraceChannel = ECC_Visibility;
	Attenuation.OcclusionLowPassFilterFrequency = 1500.0f;
	Attenuation.OcclusionVolumeAttenuation = 0.5f;
	Attenuation.OcclusionInterpolationTime = 0.2f;

	LastEventTime.Init(NeverPlayed, NumVoiceEvents);
	LastSoundIndex.Init(INDEX_NONE, NumVoiceEvents);
	Stream.GenerateNewSeed();
}

void UMonsterVoiceComponent::BeginPlay()
{
	Super::BeginPlay();

	const AActor* Owner = GetOwner();
	if (!Owner || Owner->GetNetMode() == NM_DedicatedServer || !HasAnySounds())
	{
		SetComponentTickEnabled(false);
		return;
	}
	ResolveObservedComponents();
	PreviousState = ReadObservedState();
	ScheduleIdle();
}

void UMonsterVoiceComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (VoiceAudio)
	{
		VoiceAudio->Stop();
		VoiceAudio->DestroyComponent();
		VoiceAudio = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}

void UMonsterVoiceComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	UpdateVoice(ReadObservedState(), DeltaTime);
}

void UMonsterVoiceComponent::ResolveObservedComponents()
{
	if (const AActor* Owner = GetOwner())
	{
		MonsterAI = Owner->FindComponentByClass<UMonsterAIComponent>();
		Hearing = Owner->FindComponentByClass<UMonsterHearingComponent>();
		bObservedComponentsResolved = true;
	}
}

FMonsterVoiceState UMonsterVoiceComponent::ReadObservedState()
{
	if (!bObservedComponentsResolved)
	{
		ResolveObservedComponents();
	}
	FMonsterVoiceState State;
	if (const UMonsterAIComponent* AI = MonsterAI.Get())
	{
		State.bFleeing = AI->bIsFleeing;
		State.bRagdolled = AI->bIsRagdolled;
	}
	if (const UMonsterHearingComponent* Ears = Hearing.Get())
	{
		State.bInvestigating = Ears->IsInvestigating();
	}
	// Every specialty sense (nose, sight rush, ear chase, tremor lunge) counts as hunting, not only hearing.
	State.bInvestigating = State.bInvestigating || UMonsterSenseProfile::IsActorHunting(GetOwner());
	return State;
}

void UMonsterVoiceComponent::UpdateVoice(const FMonsterVoiceState& State, float DeltaTime)
{
	const double Step = FMath::Max(0.0, static_cast<double>(DeltaTime));
	VoiceClock += Step;
	if (!bIdleScheduled)
	{
		ScheduleIdle();
	}

	const EMonsterVoiceEvent Event = MonsterVoiceRules::DetectEvent(PreviousState, State);
	PreviousState = State;
	if (Event != EMonsterVoiceEvent::None && PlayVoice(Event))
	{
		// A reaction vocal restarts the idle wait, so an idle growl never follows it immediately.
		ScheduleIdle();
		return;
	}

	if (!MonsterVoiceRules::CanPlayIdle(State))
	{
		return;
	}
	IdleTimeRemaining -= Step;
	if (IdleTimeRemaining <= 0.0)
	{
		PlayVoice(EMonsterVoiceEvent::Idle);
		ScheduleIdle();
	}
}

bool UMonsterVoiceComponent::PlayVoice(EMonsterVoiceEvent Event)
{
	const TArray<TObjectPtr<USoundBase>>* Sounds = SoundsFor(Event);
	if (!Sounds || Sounds->IsEmpty())
	{
		return false;
	}
	const EMonsterVoiceEvent Audible = IsVoiceAudible() ? LastPlayedEvent : EMonsterVoiceEvent::None;
	if (!MonsterVoiceRules::CanInterrupt(Audible, Event))
	{
		return false;
	}
	const int32 Slot = EventSlot(Event);
	if (!MonsterVoiceRules::IsPastCooldown(Event, VoiceClock - LastEventTime[Slot], RepeatCooldown))
	{
		return false;
	}
	const int32 Index = MonsterVoiceRules::PickIndexNoRepeat(Sounds->Num(), LastSoundIndex[Slot], Stream);
	USoundBase* Sound = Sounds->IsValidIndex(Index) ? (*Sounds)[Index].Get() : nullptr;
	if (!Sound)
	{
		return false;
	}
	LastSoundIndex[Slot] = Index;
	LastEventTime[Slot] = VoiceClock;
	StartSound(Sound, Event);
	return true;
}

bool UMonsterVoiceComponent::IsVoiceAudible() const
{
	return LastPlayedEvent != EMonsterVoiceEvent::None && VoiceClock < AudibleUntil;
}

const TArray<TObjectPtr<USoundBase>>* UMonsterVoiceComponent::SoundsFor(EMonsterVoiceEvent Event) const
{
	switch (Event)
	{
	case EMonsterVoiceEvent::Idle: return &IdleSounds;
	case EMonsterVoiceEvent::Alert: return &AlertSounds;
	case EMonsterVoiceEvent::Panic: return &PanicSounds;
	case EMonsterVoiceEvent::Thrown: return &ThrownSounds;
	default: return nullptr;
	}
}

bool UMonsterVoiceComponent::HasAnySounds() const
{
	return !IdleSounds.IsEmpty() || !AlertSounds.IsEmpty() || !PanicSounds.IsEmpty() || !ThrownSounds.IsEmpty();
}

void UMonsterVoiceComponent::ScheduleIdle()
{
	IdleTimeRemaining = MonsterVoiceRules::NextIdleDelay(IdleIntervalRange, Stream);
	bIdleScheduled = true;
}

void UMonsterVoiceComponent::StartSound(USoundBase* Sound, EMonsterVoiceEvent Event)
{
	const float PitchLow = FMath::Clamp(static_cast<float>(FMath::Min(PitchRange.X, PitchRange.Y)), VoiceMinPitch, VoiceMaxPitch);
	const float PitchHigh = FMath::Clamp(static_cast<float>(FMath::Max(PitchRange.X, PitchRange.Y)), PitchLow, VoiceMaxPitch);
	const float Pitch = Stream.FRandRange(PitchLow, PitchHigh);
	const float Duration = DurationProvider ? DurationProvider(Sound) : Sound->GetDuration();

	AudibleUntil = VoiceClock + (Duration > 0.0f ? Duration / Pitch : 0.0f);
	LastPlayedEvent = Event;
	LastPlayedSound = Sound;
	++NumVoicesStarted;

	if (UAudioComponent* Audio = EnsureAudioComponent())
	{
		Audio->Stop();
		Audio->SetSound(Sound);
		Audio->SetVolumeMultiplier(VolumeMultiplier);
		Audio->SetPitchMultiplier(Pitch);
		Audio->Play();
	}

	if (Event == EMonsterVoiceEvent::Idle)
	{
		UE_LOG(LogLostWithin, Verbose, TEXT("MonsterVoice: %s %s -> %s"), *GetNameSafe(GetOwner()), MonsterVoiceRules::EventName(Event), *GetNameSafe(Sound));
	}
	else
	{
		UE_LOG(LogLostWithin, Log, TEXT("MonsterVoice: %s %s -> %s"), *GetNameSafe(GetOwner()), MonsterVoiceRules::EventName(Event), *GetNameSafe(Sound));
	}
}

UAudioComponent* UMonsterVoiceComponent::EnsureAudioComponent()
{
	if (VoiceAudio)
	{
		return VoiceAudio;
	}
	AActor* Owner = GetOwner();
	const UWorld* World = GetWorld();
	if (!Owner || !World || World->GetNetMode() == NM_DedicatedServer)
	{
		return nullptr;
	}

	// Attach to the mesh so the voice follows a ragdolled body away from the capsule.
	USceneComponent* AttachParent = Owner->GetRootComponent();
	if (const ACharacter* Character = Cast<ACharacter>(Owner))
	{
		if (USkeletalMeshComponent* Mesh = Character->GetMesh())
		{
			AttachParent = Mesh;
		}
	}
	if (!AttachParent)
	{
		return nullptr;
	}

	UAudioComponent* Audio = NewObject<UAudioComponent>(Owner, MakeUniqueObjectName(Owner, UAudioComponent::StaticClass(), TEXT("MonsterVoiceAudio")));
	Audio->SetAutoActivate(false);
	Audio->bAutoDestroy = false;
	Audio->bStopWhenOwnerDestroyed = true;
	Audio->bAllowSpatialization = true;
	Audio->bOverrideAttenuation = true;
	Audio->AttenuationOverrides = Attenuation;
	Audio->SetupAttachment(AttachParent);
	Audio->RegisterComponent();
	VoiceAudio = Audio;
	return Audio;
}
