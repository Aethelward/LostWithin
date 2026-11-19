// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/LostWithinLightSubsystem.h"

#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "LostWithinCharacter.h"

namespace
{
	constexpr double TorchCacheRefreshSeconds = 1.0;
}

ULostWithinLightSubsystem* ULostWithinLightSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World ? World->GetSubsystem<ULostWithinLightSubsystem>() : nullptr;
}

void ULostWithinLightSubsystem::RefreshTorches()
{
	TorchLights.Reset();
	LastRefreshSeconds = FPlatformTime::Seconds();
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		TInlineComponentArray<UPointLightComponent*> Lights(*It);
		for (UPointLightComponent* Light : Lights)
		{
			// Spot lights (the flashlight, pickups) derive from point lights; only real point lights are torches.
			if (Light && !Light->IsA<USpotLightComponent>() && Light->GetName().StartsWith(TorchLightNamePrefix))
			{
				TorchLights.Add(Light);
			}
		}
	}
}

void ULostWithinLightSubsystem::RefreshIfStale() const
{
	if (LastRefreshSeconds < 0.0 || FPlatformTime::Seconds() - LastRefreshSeconds >= TorchCacheRefreshSeconds)
	{
		const_cast<ULostWithinLightSubsystem*>(this)->RefreshTorches();
	}
}

bool ULostWithinLightSubsystem::HasClearLine(const FVector& From, const FVector& To, const AActor* LightOwner,
	const AActor* IgnoreA, const AActor* IgnoreB) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(LostWithinTorchLight), false);
	Params.AddIgnoredActor(LightOwner);
	Params.AddIgnoredActor(IgnoreA);
	Params.AddIgnoredActor(IgnoreB);
	FHitResult Hit;
	return !World->LineTraceSingleByChannel(Hit, From, To, LightTraceChannel, Params);
}

bool ULostWithinLightSubsystem::IsPointTorchLit(const FVector& Point, const AActor* IgnoreA, const AActor* IgnoreB,
	FVector* OutLightLocation) const
{
	RefreshIfStale();
	const float RadiusSquared = FMath::Square(TorchLitRadius);
	float BestDistanceSquared = TNumericLimits<float>::Max();
	bool bLit = false;
	for (const TWeakObjectPtr<UPointLightComponent>& Weak : TorchLights)
	{
		const UPointLightComponent* Light = Weak.Get();
		if (!Light || !Light->IsVisible() || Light->Intensity <= 0.0f || !Light->IsRegistered())
		{
			continue;
		}
		const FVector LightLocation = Light->GetComponentLocation();
		const float DistanceSquared = FVector::DistSquared(LightLocation, Point);
		if (DistanceSquared > RadiusSquared || DistanceSquared >= BestDistanceSquared)
		{
			continue;
		}
		if (HasClearLine(LightLocation, Point, Light->GetOwner(), IgnoreA, IgnoreB))
		{
			bLit = true;
			BestDistanceSquared = DistanceSquared;
			if (OutLightLocation)
			{
				*OutLightLocation = LightLocation;
			}
		}
	}
	return bLit;
}

bool ULostWithinLightSubsystem::IsPointInFlashlightBeam(const ALostWithinCharacter* Player, const FVector& Point, const AActor* Ignore)
{
	if (!IsValid(Player) || !Player->IsFlashlightOn())
	{
		return false;
	}
	const USpotLightComponent* Spot = Player->GetFlashlightSpotLight();
	if (!Spot || !Spot->IsVisible())
	{
		return false;
	}
	const FVector LightLocation = Spot->GetComponentLocation();
	const FVector ToPoint = Point - LightLocation;
	const float Distance = ToPoint.Size();
	if (Distance < KINDA_SMALL_NUMBER)
	{
		return true;
	}
	if (Distance > Spot->AttenuationRadius
		|| FVector::DotProduct(Spot->GetForwardVector(), ToPoint / Distance) < FMath::Cos(FMath::DegreesToRadians(Spot->OuterConeAngle)))
	{
		return false;
	}
	const UWorld* World = Player->GetWorld();
	if (!World)
	{
		return false;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(LostWithinBeamLight), false, Player);
	Params.AddIgnoredActor(Ignore);
	TArray<AActor*> Carried;
	Player->GetAttachedActors(Carried, true, true);
	Params.AddIgnoredActors(Carried);
	FHitResult Hit;
	return !World->LineTraceSingleByChannel(Hit, LightLocation, Point, ECC_Visibility, Params);
}

bool ULostWithinLightSubsystem::IsPointLit(const FVector& Point, const AActor* Ignore, FVector* OutLightLocation) const
{
	const ALostWithinCharacter* Player = Cast<ALostWithinCharacter>(UGameplayStatics::GetPlayerCharacter(this, 0));
	if (IsPointInFlashlightBeam(Player, Point, Ignore))
	{
		if (OutLightLocation)
		{
			*OutLightLocation = Player->GetFlashlightSpotLight()->GetComponentLocation();
		}
		return true;
	}
	return IsPointTorchLit(Point, Ignore, Player, OutLightLocation);
}
