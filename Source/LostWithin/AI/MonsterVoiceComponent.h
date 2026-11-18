// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Math/RandomStream.h"
#include "Sound/SoundAttenuation.h"
#include "MonsterVoiceComponent.generated.h"

class UAudioComponent;
class UMonsterAIComponent;
class UMonsterHearingComponent;
class USoundBase;

/** What a monster vocal reacts to. The order is the priority order: a later value interrupts an earlier one. */
UENUM(BlueprintType)
enum class EMonsterVoiceEvent : uint8
{
	None,
	Idle,
	Alert,
	Panic,
	Thrown
};

/** The parts of a monster's state that its vocals react to. */
USTRUCT(BlueprintType)
struct LOSTWITHIN_API FMonsterVoiceState
{
	GENERATED_BODY()

	/** The hearing component is moving to or searching around a noise. */
	UPROPERTY(BlueprintReadWrite, Category="Monster Voice")
	bool bInvestigating = false;

	/** MonsterAIComponent is panic-fleeing a float-spinning cat. */
	UPROPERTY(BlueprintReadWrite, Category="Monster Voice")
	bool bFleeing = false;

	/** MonsterAIComponent has thrown the body into ragdoll. */
	UPROPERTY(BlueprintReadWrite, Category="Monster Voice")
	bool bRagdolled = false;
};

/** Pure vocal rules, kept free of worlds and audio so automation can check them directly. */
namespace MonsterVoiceRules
{
	/** The highest-priority state that just became true: Thrown, then Panic, then Alert; otherwise None. */
	LOSTWITHIN_API EMonsterVoiceEvent DetectEvent(const FMonsterVoiceState& Previous, const FMonsterVoiceState& Current);

	/** Higher plays over lower: None 0, Idle 1, Alert 2, Panic 3, Thrown 4. */
	LOSTWITHIN_API int32 Priority(EMonsterVoiceEvent Event);

	/** Whether NewEvent may start while PlayingEvent is still audible (None = nothing audible). */
	LOSTWITHIN_API bool CanInterrupt(EMonsterVoiceEvent PlayingEvent, EMonsterVoiceEvent NewEvent);

	/** Idle vocals belong to normal roaming: never while fleeing or ragdolled. */
	LOSTWITHIN_API bool CanPlayIdle(const FMonsterVoiceState& State);

	/** Alert and Panic wait Cooldown seconds before repeating, so flickering states do not spam; Idle and Thrown do not. */
	LOSTWITHIN_API bool IsPastCooldown(EMonsterVoiceEvent Event, double SecondsSinceSameEvent, float Cooldown);

	/** A random index in [0, Count) that differs from LastIndex when Count > 1; INDEX_NONE when Count is 0. */
	LOSTWITHIN_API int32 PickIndexNoRepeat(int32 Count, int32 LastIndex, FRandomStream& Stream);

	/** Seconds until the next idle vocal: random within Range (either order), never below 0.5. */
	LOSTWITHIN_API float NextIdleDelay(const FVector2D& Range, FRandomStream& Stream);

	LOSTWITHIN_API const TCHAR* EventName(EMonsterVoiceEvent Event);
}

/**
 *  Gives a monster its voice: random idle vocals while roaming, and one-shot vocals when its hearing component starts
 *  investigating (Alert), when MonsterAIComponent starts fleeing a float-spinning cat (Panic) or throws it (Thrown).
 *  It only observes those components; it never changes their behaviour. One vocal plays at a time, spatialized
 *  from the mesh so it follows a ragdolled body.
 */
UCLASS(ClassGroup=(Audio), meta=(BlueprintSpawnableComponent))
class LOSTWITHIN_API UMonsterVoiceComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMonsterVoiceComponent();

	/** Played at random intervals while roaming normally. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Monster Voice|Sounds")
	TArray<TObjectPtr<USoundBase>> IdleSounds;

	/** Played when the hearing component starts investigating a noise. Leave empty for monsters that cannot hear. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Monster Voice|Sounds")
	TArray<TObjectPtr<USoundBase>> AlertSounds;

	/** Played when the monster starts panic-fleeing a float-spinning cat. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Monster Voice|Sounds")
	TArray<TObjectPtr<USoundBase>> PanicSounds;

	/** Played when a cat throws the monster into ragdoll. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Monster Voice|Sounds")
	TArray<TObjectPtr<USoundBase>> ThrownSounds;

	/** Random wait between idle vocals (X to Y seconds). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Monster Voice")
	FVector2D IdleIntervalRange = FVector2D(8.0, 20.0);

	/** Minimum time before the same Alert or Panic vocal can play again. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Monster Voice", meta=(ClampMin="0.0", Units="s"))
	float RepeatCooldown = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Monster Voice", meta=(ClampMin="0.0", ClampMax="4.0"))
	float VolumeMultiplier = 1.0f;

	/** Random pitch per vocal (X to Y), so repeated takes sound less identical. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Monster Voice")
	FVector2D PitchRange = FVector2D(0.95, 1.05);

	/** Spatial falloff for the vocals. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Monster Voice|Attenuation")
	FSoundAttenuationSettings Attenuation;

	/** Plays a vocal for Event if it has sounds, outranks what is audible and is past its cooldown. */
	UFUNCTION(BlueprintCallable, Category="Monster Voice")
	bool PlayVoice(EMonsterVoiceEvent Event);

	UFUNCTION(BlueprintPure, Category="Monster Voice")
	EMonsterVoiceEvent GetLastPlayedEvent() const { return LastPlayedEvent; }

	/** True while the last vocal is still expected to be audible. */
	UFUNCTION(BlueprintPure, Category="Monster Voice")
	bool IsVoiceAudible() const;

	/** Reads the owner's MonsterAI and hearing components (either may be missing). */
	FMonsterVoiceState ReadObservedState();

	/** Advances the voice clock and reacts to State; the tick calls this with ReadObservedState(). */
	void UpdateVoice(const FMonsterVoiceState& State, float DeltaTime);

	int32 GetNumVoicesStarted() const { return NumVoicesStarted; }
	USoundBase* GetLastPlayedSound() const { return LastPlayedSound.Get(); }
	void SetRandomSeed(int32 Seed) { Stream.Initialize(Seed); }

	/** Replaces USoundBase::GetDuration() when deciding how long a vocal stays audible (automation uses this). */
	void SetSoundDurationProvider(TFunction<float(const USoundBase*)> Provider) { DurationProvider = MoveTemp(Provider); }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	const TArray<TObjectPtr<USoundBase>>* SoundsFor(EMonsterVoiceEvent Event) const;
	bool HasAnySounds() const;
	void ResolveObservedComponents();
	void ScheduleIdle();
	void StartSound(USoundBase* Sound, EMonsterVoiceEvent Event);
	UAudioComponent* EnsureAudioComponent();

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> VoiceAudio;

	TWeakObjectPtr<UMonsterAIComponent> MonsterAI;
	TWeakObjectPtr<UMonsterHearingComponent> Hearing;
	TWeakObjectPtr<USoundBase> LastPlayedSound;
	TFunction<float(const USoundBase*)> DurationProvider;
	FMonsterVoiceState PreviousState;
	FRandomStream Stream;
	TArray<double> LastEventTime;
	TArray<int32> LastSoundIndex;
	EMonsterVoiceEvent LastPlayedEvent = EMonsterVoiceEvent::None;
	double VoiceClock = 0.0;
	double AudibleUntil = 0.0;
	double IdleTimeRemaining = 0.0;
	int32 NumVoicesStarted = 0;
	bool bIdleScheduled = false;
	bool bObservedComponentsResolved = false;
};
