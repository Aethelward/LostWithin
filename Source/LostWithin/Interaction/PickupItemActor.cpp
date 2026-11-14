// Copyright Epic Games, Inc. All Rights Reserved.

#include "Interaction/PickupItemActor.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundWave.h"

APickupItemActor::APickupItemActor()
{
	PrimaryActorTick.bCanEverTick = true;

	CollisionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("CollisionSphere"));
	SetRootComponent(CollisionSphere);
	CollisionSphere->InitSphereRadius(36.0f);
	CollisionSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	CollisionSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
	CollisionSphere->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	CollisionSphere->SetGenerateOverlapEvents(false);

	MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComponent"));
	MeshComponent->SetupAttachment(CollisionSphere);
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MeshComponent->SetRelativeScale3D(FVector(0.25f));

	ItemData.ItemID = TEXT("PlaceholderItem");
	ItemData.DisplayName = FText::FromString(TEXT("Placeholder Item"));
	ItemData.Description = FText::FromString(TEXT("A placeholder item dropped by the cat."));
	ItemData.Quantity = 1;
	ItemData.MaxStackSize = 99;
}

void APickupItemActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	EnsureDefaultMesh();
}

void APickupItemActor::BeginPlay()
{
	Super::BeginPlay();
	EnsureDefaultMesh();
	EnsureDefaultDropSounds();

	if (!bIsDropping)
	{
		RestLocation = GetActorLocation();
	}
}

void APickupItemActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (bIsDropping)
	{
		DropElapsed += DeltaTime;
		const float Alpha = (DropDuration > 0.0f) ? FMath::Clamp(DropElapsed / DropDuration, 0.0f, 1.0f) : 1.0f;
		const FVector HorizontalPos = FMath::Lerp(DropStartLocation, DropEndLocation, Alpha);
		const float ArcOffset = 4.0f * Alpha * (1.0f - Alpha) * DropArcHeight;
		SetActorLocation(HorizontalPos + FVector(0.0f, 0.0f, ArcOffset));

		if (Alpha >= 1.0f)
		{
			bIsDropping = false;
			RestLocation = DropEndLocation;
			SetActorLocation(RestLocation);
		}
	}
	else
	{
		IdleTime += DeltaTime;
		const float BobZ = FMath::Sin(IdleTime * 2.0f * PI * IdleBobFrequency) * IdleBobAmplitude;
		SetActorLocation(RestLocation + FVector(0.0f, 0.0f, BobZ));
	}

	if (MeshComponent && !FMath::IsNearlyZero(IdleSpinRate))
	{
		MeshComponent->AddLocalRotation(FRotator(0.0f, IdleSpinRate * DeltaTime, 0.0f));
	}
}

void APickupItemActor::StartDropArc(const FVector& StartLocation, const FVector& TargetLocation, float ArcHeight, float Duration)
{
	TArray<AActor*> IgnoreActors;
	IgnoreActors.Add(this);
	if (AActor* OwnerActor = GetOwner())
	{
		IgnoreActors.Add(OwnerActor);
	}
	if (AActor* InstigatorActor = GetInstigator())
	{
		IgnoreActors.Add(InstigatorActor);
	}

	DropStartLocation = StartLocation;
	DropEndLocation = FindGroundLocation(TargetLocation, IgnoreActors);
	RestLocation = DropEndLocation;
	DropArcHeight = FMath::Max(0.0f, ArcHeight);
	DropDuration = FMath::Max(0.01f, Duration);
	DropElapsed = 0.0f;
	bIsDropping = true;

	SetActorLocation(DropStartLocation);
	PlayRandomDropSound();
}

void APickupItemActor::OnInteract_Implementation(AActor* Interactor)
{
	if (!Interactor)
	{
		return;
	}

	if (UInventoryComponent* Inventory = Interactor->FindComponentByClass<UInventoryComponent>())
	{
		int32 RemainingQuantity = 0;
		if (Inventory->AddItem(ItemData, RemainingQuantity))
		{
			if (RemainingQuantity <= 0)
			{
				Destroy();
			}
			else
			{
				ItemData.Quantity = RemainingQuantity;
			}
		}
	}
}

void APickupItemActor::OnHit_Implementation(AActor* Attacker, const FHitResult& HitResult)
{
	if (bIsDropping)
	{
		return;
	}

	FVector PushDir = Attacker ? (GetActorLocation() - Attacker->GetActorLocation()).GetSafeNormal2D() : FVector::ForwardVector;
	if (PushDir.IsNearlyZero())
	{
		PushDir = FVector::ForwardVector;
	}

	const FVector CurrentLoc = GetActorLocation();
	StartDropArc(CurrentLoc, RestLocation + PushDir * 45.0f, 25.0f, 0.25f);
}

FText APickupItemActor::GetInteractionPrompt_Implementation() const
{
	const FText SafeName = ItemData.DisplayName.IsEmpty()
		? FText::FromName(ItemData.ItemID)
		: ItemData.DisplayName;

	if (ItemData.Quantity > 1)
	{
		return FText::Format(
			NSLOCTEXT("LostWithin", "PickupItemStackPrompt", "[F] Pick up {0} (x{1})"),
			SafeName,
			FText::AsNumber(ItemData.Quantity));
	}

	return FText::Format(NSLOCTEXT("LostWithin", "PickupItemPrompt", "[F] Pick up {0}"), SafeName);
}

bool APickupItemActor::CanInteract_Implementation(AActor* Interactor) const
{
	return Interactor != nullptr && Interactor->FindComponentByClass<UInventoryComponent>() != nullptr;
}

void APickupItemActor::EnsureDefaultMesh()
{
	if (!MeshComponent || MeshComponent->GetStaticMesh())
	{
		return;
	}

	UStaticMesh* FallbackMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/LevelPrototyping/Meshes/SM_ChamferCube.SM_ChamferCube"));
	if (!FallbackMesh)
	{
		FallbackMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	}

	if (FallbackMesh)
	{
		MeshComponent->SetStaticMesh(FallbackMesh);
	}
}

void APickupItemActor::EnsureDefaultDropSounds()
{
	if (DropSounds.IsEmpty())
	{
		static constexpr const TCHAR* DefaultDropSoundPaths[] = {
			TEXT("/Game/Item/SFX/Drop_01.Drop_01"),
			TEXT("/Game/Item/SFX/Drop_02.Drop_02"),
			TEXT("/Game/Item/SFX/Drop_03.Drop_03")
		};

		for (const TCHAR* AssetPath : DefaultDropSoundPaths)
		{
			if (USoundBase* LoadedSound = LoadObject<USoundBase>(nullptr, AssetPath))
			{
				DropSounds.AddUnique(LoadedSound);
			}
		}
	}

	for (const TObjectPtr<USoundBase>& Sound : DropSounds)
	{
		if (USoundWave* SoundWave = Cast<USoundWave>(Sound))
		{
			SoundWave->bLooping = false;
		}
	}
}

void APickupItemActor::PlayRandomDropSound()
{
	EnsureDefaultDropSounds();
	if (DropSounds.IsEmpty() || DropSoundVolume <= 0.0f)
	{
		return;
	}

	int32 ChosenIndex = FMath::RandRange(0, DropSounds.Num() - 1);
	if (DropSounds.Num() > 1 && ChosenIndex == LastDropSoundIndex)
	{
		ChosenIndex = (ChosenIndex + 1) % DropSounds.Num();
	}
	LastDropSoundIndex = ChosenIndex;

	if (USoundBase* SelectedSound = DropSounds[ChosenIndex])
	{
		UGameplayStatics::PlaySoundAtLocation(
			this,
			SelectedSound,
			GetActorLocation(),
			DropSoundVolume,
			FMath::FRandRange(0.96f, 1.04f));
	}
}

FVector APickupItemActor::FindGroundLocation(const FVector& DesiredPoint, const TArray<AActor*>& ActorsToIgnore) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return DesiredPoint;
	}

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActors(ActorsToIgnore);

	FHitResult GroundHit;
	const FVector TraceStart = DesiredPoint + FVector(0.0f, 0.0f, 120.0f);
	const FVector TraceEnd = DesiredPoint - FVector(0.0f, 0.0f, 300.0f);

	if (World->LineTraceSingleByChannel(GroundHit, TraceStart, TraceEnd, ECC_Visibility, QueryParams))
	{
		return GroundHit.ImpactPoint + FVector(0.0f, 0.0f, 22.0f);
	}

	return DesiredPoint;
}

