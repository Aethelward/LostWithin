// Copyright Epic Games, Inc. All Rights Reserved.

#include "AI/MonsterFootstepComponent.h"

#include "AI/MonsterVoiceComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "LostWithin.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"

namespace
{
	// Unity builds merge this file with MonsterVoiceComponent.cpp, so these names must differ from its constants.
	constexpr float FootstepMinPitch = 0.25f;
	constexpr float FootstepMaxPitch = 4.0f;
	constexpr float MinStrideLength = 10.0f;
}

namespace MonsterFootstepRules
{
	bool ShouldAccumulate(float GroundSpeed, bool bOnGround, float MinSpeed)
	{
		return bOnGround && GroundSpeed >= FMath::Max(0.0f, MinSpeed) && GroundSpeed > 0.0f;
	}

	int32 Advance(float& InOutAccumulated, float Distance, float StrideLength)
	{
		const float Stride = FMath::Max(MinStrideLength, StrideLength);
		InOutAccumulated += FMath::Max(0.0f, Distance);
		const int32 Due = FMath::FloorToInt(InOutAccumulated / Stride);
		InOutAccumulated -= Due * Stride;
		return Due;
	}
}

UMonsterFootstepComponent::UMonsterFootstepComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickInterval = 0.05f;

	// Close steps are clear; distant ones are muffled and gone beyond about 20 m.
	Attenuation.bAttenuate = true;
	Attenuation.bSpatialize = true;
	Attenuation.DistanceAlgorithm = EAttenuationDistanceModel::NaturalSound;
	Attenuation.AttenuationShape = EAttenuationShape::Sphere;
	Attenuation.AttenuationShapeExtents = FVector(150.0, 0.0, 0.0);
	Attenuation.FalloffDistance = 1800.0f;
	Attenuation.bAttenuateWithLPF = true;
	Attenuation.LPFRadiusMin = 400.0f;
	Attenuation.LPFRadiusMax = 1950.0f;
	Attenuation.LPFFrequencyAtMin = 20000.0f;
	Attenuation.LPFFrequencyAtMax = 2500.0f;

	Stream.GenerateNewSeed();
}

void UMonsterFootstepComponent::BeginPlay()
{
	Super::BeginPlay();

	const AActor* Owner = GetOwner();
	if (!Owner || Owner->GetNetMode() == NM_DedicatedServer || FootstepSounds.IsEmpty())
	{
		SetComponentTickEnabled(false);
		return;
	}
	RuntimeAttenuation = NewObject<USoundAttenuation>(this);
	RuntimeAttenuation->Attenuation = Attenuation;
	// The first step lands half a stride after the monster starts moving.
	AccumulatedDistance = FMath::Max(MinStrideLength, StrideLength) * 0.5f;
}

void UMonsterFootstepComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	const UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	const float GroundSpeed = Character ? static_cast<float>(Character->GetVelocity().Size2D()) : 0.0f;
	UpdateFootsteps(DeltaTime, GroundSpeed, Movement && Movement->IsMovingOnGround());
}

void UMonsterFootstepComponent::UpdateFootsteps(float DeltaTime, float GroundSpeed, bool bOnGround)
{
	if (!MonsterFootstepRules::ShouldAccumulate(GroundSpeed, bOnGround, MinSpeed))
	{
		return;
	}
	const float Distance = GroundSpeed * FMath::Max(0.0f, DeltaTime);
	// After a hitch several strides can be due at once; play one rather than a burst.
	if (MonsterFootstepRules::Advance(AccumulatedDistance, Distance, StrideLength) > 0)
	{
		PlayStep();
	}
}

void UMonsterFootstepComponent::PlayStep()
{
	const int32 Index = MonsterVoiceRules::PickIndexNoRepeat(FootstepSounds.Num(), LastSoundIndex, Stream);
	USoundBase* Sound = FootstepSounds.IsValidIndex(Index) ? FootstepSounds[Index].Get() : nullptr;
	if (!Sound)
	{
		return;
	}
	LastSoundIndex = Index;
	LastStepSound = Sound;
	++NumStepsPlayed;

	const float PitchLow = FMath::Clamp(static_cast<float>(FMath::Min(PitchRange.X, PitchRange.Y)), FootstepMinPitch, FootstepMaxPitch);
	const float PitchHigh = FMath::Clamp(static_cast<float>(FMath::Max(PitchRange.X, PitchRange.Y)), PitchLow, FootstepMaxPitch);
	const UWorld* World = GetWorld();
	if (World && World->GetNetMode() != NM_DedicatedServer)
	{
		UGameplayStatics::PlaySoundAtLocation(this, Sound, GetFeetLocation(), FRotator::ZeroRotator, VolumeMultiplier,
			Stream.FRandRange(PitchLow, PitchHigh), 0.0f, RuntimeAttenuation);
	}
	UE_LOG(LogLostWithin, Verbose, TEXT("MonsterFootstep: %s -> %s"), *GetNameSafe(GetOwner()), *GetNameSafe(Sound));
}

FVector UMonsterFootstepComponent::GetFeetLocation() const
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return FVector::ZeroVector;
	}
	if (const ACharacter* Character = Cast<ACharacter>(Owner))
	{
		if (const UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
		{
			return Capsule->GetComponentLocation() - FVector(0.0, 0.0, Capsule->GetScaledCapsuleHalfHeight());
		}
	}
	return Owner->GetActorLocation();
}
