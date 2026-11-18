// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/LostWithinNoiseSubsystem.h"
#include "AI/MonsterHearingComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"

namespace
{
	FName NoiseTag(ELostWithinNoiseKind Kind)
	{
		switch (Kind)
		{
		case ELostWithinNoiseKind::Footstep:
			return TEXT("Footstep");
		case ELostWithinNoiseKind::ItemImpact:
			return TEXT("ItemImpact");
		default:
			return TEXT("Noise");
		}
	}
}

int32 ULostWithinNoiseSubsystem::ReportNoise(const UObject* WorldContextObject, FVector Location, float Radius,
	ELostWithinNoiseKind Kind, AActor* NoiseMaker)
{
	if (Radius <= 0.0f || !FMath::IsFinite(Radius))
	{
		return 0;
	}

	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (!World)
	{
		return 0;
	}

	if (IsValid(NoiseMaker))
	{
		// Engine perception (AISense_Hearing) honours MaxRange; it needs a pawn instigator to report anything.
		APawn* Instigator = Cast<APawn>(NoiseMaker);
		NoiseMaker->MakeNoise(1.0f, Instigator ? Instigator : NoiseMaker->GetInstigator(), Location, Radius, NoiseTag(Kind));
	}

	ULostWithinNoiseSubsystem* Subsystem = World->GetSubsystem<ULostWithinNoiseSubsystem>();
	if (!Subsystem)
	{
		return 0;
	}

	FLostWithinNoiseEvent Noise;
	Noise.Location = Location;
	Noise.Radius = Radius;
	Noise.Kind = Kind;
	Noise.NoiseMaker = NoiseMaker;
	return Subsystem->Broadcast(Noise);
}

int32 ULostWithinNoiseSubsystem::Broadcast(const FLostWithinNoiseEvent& Noise)
{
	LastNoise = Noise;
	++ReportedNoiseCount;

	Listeners.RemoveAll([](const TWeakObjectPtr<UMonsterHearingComponent>& Listener) { return !Listener.IsValid(); });

	// Copy: a reaction may register or unregister listeners.
	const TArray<TWeakObjectPtr<UMonsterHearingComponent>> Snapshot = Listeners;
	int32 Reacted = 0;
	for (const TWeakObjectPtr<UMonsterHearingComponent>& Listener : Snapshot)
	{
		if (UMonsterHearingComponent* Hearing = Listener.Get(); Hearing && Hearing->HearNoise(Noise))
		{
			++Reacted;
		}
	}
	return Reacted;
}

void ULostWithinNoiseSubsystem::RegisterListener(UMonsterHearingComponent* Listener)
{
	if (Listener)
	{
		Listeners.AddUnique(Listener);
	}
}

void ULostWithinNoiseSubsystem::UnregisterListener(UMonsterHearingComponent* Listener)
{
	Listeners.Remove(Listener);
}
