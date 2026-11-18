// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Math/RandomStream.h"
#include "Sound/SoundAttenuation.h"
#include "MonsterFootstepComponent.generated.h"

class USoundAttenuation;
class USoundBase;

/** Pure stride rules, kept free of worlds and audio so automation can check them directly. */
namespace MonsterFootstepRules
{
	/** Ground distance counts toward the next step only while on the ground and at least MinSpeed. */
	LOSTWITHIN_API bool ShouldAccumulate(float GroundSpeed, bool bOnGround, float MinSpeed);

	/** Adds Distance to InOutAccumulated and returns how many StrideLength steps are now due (consuming them). */
	LOSTWITHIN_API int32 Advance(float& InOutAccumulated, float Distance, float StrideLength);
}

/**
 *  Gives a monster audible footsteps: one random take, spatialized at its feet, each time it covers StrideLength of
 *  ground distance. Faster movement (fleeing, investigating) therefore steps faster. Silent while airborne,
 *  ragdolled or standing. Only observes the owner's movement; it never changes it.
 */
UCLASS(ClassGroup=(Audio), meta=(BlueprintSpawnableComponent))
class LOSTWITHIN_API UMonsterFootstepComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMonsterFootstepComponent();

	/** Footstep takes; one is picked per step, never the same twice in a row. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Monster Footsteps")
	TArray<TObjectPtr<USoundBase>> FootstepSounds;

	/** Ground distance between two footstep sounds. Shorter for small or four-legged creatures. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Monster Footsteps", meta=(ClampMin="10.0", Units="cm"))
	float StrideLength = 70.0f;

	/** Below this horizontal speed the monster counts as standing still. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Monster Footsteps", meta=(ClampMin="0.0", Units="cm/s"))
	float MinSpeed = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Monster Footsteps", meta=(ClampMin="0.0", ClampMax="4.0"))
	float VolumeMultiplier = 0.8f;

	/** Random pitch per step (X to Y). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Monster Footsteps")
	FVector2D PitchRange = FVector2D(0.92, 1.08);

	/** Spatial falloff for the steps. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Monster Footsteps|Attenuation")
	FSoundAttenuationSettings Attenuation;

	/** Advances by DeltaTime at GroundSpeed; the tick calls this with the owner's movement. */
	void UpdateFootsteps(float DeltaTime, float GroundSpeed, bool bOnGround);

	int32 GetNumStepsPlayed() const { return NumStepsPlayed; }
	USoundBase* GetLastStepSound() const { return LastStepSound.Get(); }
	void SetRandomSeed(int32 Seed) { Stream.Initialize(Seed); }

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	void PlayStep();
	FVector GetFeetLocation() const;

	UPROPERTY(Transient)
	TObjectPtr<USoundAttenuation> RuntimeAttenuation;

	TWeakObjectPtr<USoundBase> LastStepSound;
	FRandomStream Stream;
	float AccumulatedDistance = 0.0f;
	int32 LastSoundIndex = INDEX_NONE;
	int32 NumStepsPlayed = 0;
};
