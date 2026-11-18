// Copyright Epic Games, Inc. All Rights Reserved.

#include "Player/MonsterRevealSoundComponent.h"

#include "AI/MonsterAIComponent.h"
#include "AI/MonsterVoiceComponent.h"
#include "Components/AudioComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "LostWithin.h"
#include "LostWithinCharacter.h"
#include "LostWithinGameMode.h"
#include "Player/DeathCameraActor.h"
#include "Player/PlayerDeathComponent.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

UMonsterRevealSoundComponent::UMonsterRevealSoundComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
	PrimaryComponentTick.TickInterval = 0.05f;

	static const ConstructorHelpers::FObjectFinder<USoundBase> RevealOne(
		TEXT("/Game/Audio/SFX/Player/Scare/SFX_MonsterReveal_01.SFX_MonsterReveal_01"));
	static const ConstructorHelpers::FObjectFinder<USoundBase> RevealTwo(
		TEXT("/Game/Audio/SFX/Player/Scare/SFX_MonsterReveal_02.SFX_MonsterReveal_02"));
	static const ConstructorHelpers::FObjectFinder<USoundBase> RevealThree(
		TEXT("/Game/Audio/SFX/Player/Scare/SFX_MonsterReveal_03.SFX_MonsterReveal_03"));
	USoundBase* DefaultSounds[] = { RevealOne.Object, RevealTwo.Object, RevealThree.Object };
	for (USoundBase* Sound : DefaultSounds)
	{
		if (Sound)
		{
			RevealSounds.Add(Sound);
		}
	}
}

void UMonsterRevealSoundComponent::BeginPlay()
{
	Super::BeginPlay();
	Random.GenerateNewSeed();
	if (UPlayerDeathComponent* Death = GetOwner()->FindComponentByClass<UPlayerDeathComponent>())
	{
		AddTickPrerequisiteComponent(Death);
	}
}

void UMonsterRevealSoundComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopReveal();
	Super::EndPlay(EndPlayReason);
}

void UMonsterRevealSoundComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	UWorld* World = GetWorld();
	ALostWithinCharacter* Player = Cast<ALostWithinCharacter>(GetOwner());
	APlayerController* Controller = Player ? Cast<APlayerController>(Player->GetController()) : nullptr;
	const ALostWithinGameMode* GameMode = World ? World->GetAuthGameMode<ALostWithinGameMode>() : nullptr;
	if (!World || !Controller || !Controller->IsLocalPlayerController() || Player->IsDead()
		|| Controller->GetViewTarget() != Player || (GameMode && !GameMode->IsRunActive()))
	{
		StopReveal();
		Sightings.Reset();
		bViewPrimed = false;
		return;
	}

	FIntPoint ViewportSize;
	Controller->GetViewportSize(ViewportSize.X, ViewportSize.Y);
	if (ViewportSize.X <= 0 || ViewportSize.Y <= 0 || RevealSounds.IsEmpty())
	{
		return;
	}
	FVector CameraLocation;
	FRotator CameraRotation;
	Controller->GetPlayerViewPoint(CameraLocation, CameraRotation);

	const double Now = World->GetTimeSeconds();
	ACharacter* NearestReveal = nullptr;
	double BestDistanceSquared = FMath::Square(CloseRange);
	for (TActorIterator<ACharacter> It(World); It; ++It)
	{
		ACharacter* Monster = *It;
		const UMonsterAIComponent* AI = Monster->FindComponentByClass<UMonsterAIComponent>();
		if (!AI || Monster == Player)
		{
			continue;
		}

		const TWeakObjectPtr<ACharacter> Key(Monster);
		const bool bFirstObservation = !Sightings.Contains(Key);
		FSighting& Sighting = Sightings.FindOrAdd(Key);
		const bool bVisible = IsMonsterInView(*Controller, *Monster, CameraLocation, ViewportSize);
		if (!bVisible && (Sighting.bVisible || bFirstObservation))
		{
			Sighting.HiddenSince = Now;
		}
		const bool bSudden = bVisible && (bFirstObservation
			|| (!Sighting.bVisible && Now - Sighting.HiddenSince >= MinimumHiddenTime));
		Sighting.bVisible = bVisible;

		const double DistanceSquared = FVector::DistSquared(Player->GetActorLocation(), Monster->GetActorLocation());
		if (bViewPrimed && bSudden && !AI->bIsRagdolled && Now >= NextRevealTime && DistanceSquared <= BestDistanceSquared)
		{
			NearestReveal = Monster;
			BestDistanceSquared = DistanceSquared;
		}
	}
	for (auto It = Sightings.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}
	bViewPrimed = true;
	if (NearestReveal)
	{
		PlayReveal(*NearestReveal, Now);
	}
}

bool UMonsterRevealSoundComponent::IsMonsterInView(const APlayerController& Controller, const ACharacter& Monster,
	const FVector& CameraLocation, const FIntPoint& ViewportSize) const
{
	const USkeletalMeshComponent* Mesh = Monster.GetMesh();
	if (Monster.IsHidden() || !Mesh || !Mesh->IsVisible() || !Mesh->GetSkeletalMeshAsset())
	{
		return false;
	}

	FCollisionQueryParams Query(SCENE_QUERY_STAT(LostWithinMonsterReveal), false, GetOwner());
	Query.AddIgnoredActor(&Monster);
	const FVector Samples[] = { ADeathCameraActor::FindLookTarget(&Monster), Mesh->Bounds.Origin };
	for (const FVector& Sample : Samples)
	{
		FVector2D Pixel;
		if (Controller.ProjectWorldLocationToScreen(Sample, Pixel)
			&& Pixel.X >= 0.0 && Pixel.Y >= 0.0 && Pixel.X < ViewportSize.X && Pixel.Y < ViewportSize.Y
			&& !GetWorld()->LineTraceTestByChannel(CameraLocation, Sample, ECC_Visibility, Query))
		{
			return true;
		}
	}
	return false;
}

void UMonsterRevealSoundComponent::PlayReveal(const ACharacter& Monster, double Now)
{
	const int32 Index = MonsterVoiceRules::PickIndexNoRepeat(RevealSounds.Num(), LastSoundIndex, Random);
	USoundBase* Sound = RevealSounds.IsValidIndex(Index) ? RevealSounds[Index].Get() : nullptr;
	if (!Sound)
	{
		return;
	}

	StopReveal();
	if (UAudioComponent* Audio = UGameplayStatics::CreateSound2D(this, Sound))
	{
		Audio->bIsUISound = false;
		Audio->Play();
		PlayingReveal = Audio;
		LastSoundIndex = Index;
		NextRevealTime = Now + Cooldown;
		UE_LOG(LogLostWithin, Log, TEXT("Monster reveal: %s saw %s, playing %s"),
			*GetNameSafe(GetOwner()), *Monster.GetName(), *Sound->GetName());
	}
}

void UMonsterRevealSoundComponent::StopReveal()
{
	if (UAudioComponent* Audio = PlayingReveal.Get())
	{
		Audio->Stop();
	}
	PlayingReveal.Reset();
}
