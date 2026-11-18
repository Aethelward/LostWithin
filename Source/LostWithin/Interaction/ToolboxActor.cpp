// Copyright Epic Games, Inc. All Rights Reserved.

#include "Interaction/ToolboxActor.h"

#include "Animation/AnimSequenceBase.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Interaction/PickupItemActor.h"
#include "Kismet/GameplayStatics.h"
#include "LostWithin.h"
#include "LostWithinCharacter.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

AToolboxActor::AToolboxActor()
{
	PrimaryActorTick.bCanEverTick = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	CollisionBox = CreateDefaultSubobject<UBoxComponent>(TEXT("CollisionBox"));
	CollisionBox->SetupAttachment(Root);
	CollisionBox->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);
	CollisionBox->SetBoxExtent(FVector(30.0f, 15.0f, 15.0f));
	CollisionBox->SetRelativeLocation(FVector(0.0f, 0.0f, 15.0f));
	CollisionBox->SetCanEverAffectNavigation(true);

	Mesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Root);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	Mesh->SetCanEverAffectNavigation(false);

	static ConstructorHelpers::FObjectFinder<USkeletalMesh> ToolboxMesh(
		TEXT("/Game/Level/Objects/tool_box/SkeletalMeshes/tool_box.tool_box"));
	if (ToolboxMesh.Succeeded())
	{
		Mesh->SetSkeletalMeshAsset(ToolboxMesh.Object);
	}

	static ConstructorHelpers::FObjectFinder<UAnimSequenceBase> ToolboxOpen(
		TEXT("/Game/Level/Objects/tool_box/SkeletalMeshes/tool_box_Anim.tool_box_Anim"));
	OpenAnimation = ToolboxOpen.Object;

	static ConstructorHelpers::FObjectFinder<USoundBase> PadlockOpen(
		TEXT("/Game/Level/MAZE/Objects/ScaryDoor/SFX/SC_Padlock_Open_01.SC_Padlock_Open_01"));
	UnlockSound = PadlockOpen.Object;

	static ConstructorHelpers::FObjectFinder<USoundBase> PadlockRattle(
		TEXT("/Game/Level/MAZE/Objects/ScaryDoor/SFX/SC_Padlock_Interact_01.SC_Padlock_Interact_01"));
	LockedSound = PadlockRattle.Object;

	// Generated with ElevenLabs (Tools/SFX/sfx-manifest.json, id toolbox_open).
	static ConstructorHelpers::FObjectFinder<USoundBase> LidOpen(
		TEXT("/Game/Level/Objects/tool_box/SFX/SFX_ToolboxOpen_01.SFX_ToolboxOpen_01"));
	OpenSound = LidOpen.Object;

#if WITH_EDITORONLY_DATA
	bIsSpatiallyLoaded = false;
#endif
}

void AToolboxActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	FitMeshToTargetWidth();
}

void AToolboxActor::FitMeshToTargetWidth()
{
	const USkeletalMesh* Asset = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
	if (!Asset || !CollisionBox)
	{
		return;
	}

	const FBox Bounds = Asset->GetBounds().GetBox();
	const FVector Size = Bounds.GetSize();
	const double Width = FMath::Max(Size.X, Size.Y);
	if (Width <= UE_KINDA_SMALL_NUMBER)
	{
		return;
	}

	const double Scale = TargetWidthCm / Width;
	const FVector Center = Bounds.GetCenter();
	Mesh->SetRelativeScale3D(FVector(Scale));
	Mesh->SetRelativeLocation(FVector(-Center.X * Scale, -Center.Y * Scale, -Bounds.Min.Z * Scale));

	const FVector ScaledSize = Size * Scale;
	CollisionBox->SetBoxExtent(ScaledSize * 0.5, /*bUpdateOverlaps=*/false);
	CollisionBox->SetRelativeLocation(FVector(0.0f, 0.0f, ScaledSize.Z * 0.5));
}

void AToolboxActor::SetRequiredKey(FName KeyID)
{
	if (!bOpen)
	{
		RequiredKeyID = KeyID;
	}
}

void AToolboxActor::SetContents(TSubclassOf<APickupItemActor> ItemClass, const FInventoryItem& Item)
{
	ContentsClass = ItemClass;
	ContentsItem = Item;
	ContentsItem.Quantity = FMath::Max(1, ContentsItem.Quantity);
	bHasContents = true;
}

void AToolboxActor::ClearContents()
{
	ContentsClass = nullptr;
	ContentsItem = FInventoryItem();
	bHasContents = false;
}

FText AToolboxActor::GetRequiredKeyName() const
{
	return APickupItemActor::GetItemDisplayName(RequiredKeyID);
}

bool AToolboxActor::InteractorHasRequiredKey(const AActor* Interactor) const
{
	const UInventoryComponent* Inventory = Interactor ? Interactor->FindComponentByClass<UInventoryComponent>() : nullptr;
	return Inventory && !RequiredKeyID.IsNone() && Inventory->HasItem(RequiredKeyID);
}

void AToolboxActor::PlaySound(USoundBase* Sound) const
{
	if (Sound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, Sound, GetActorLocation());
	}
}

void AToolboxActor::NotifyPlayer(AActor* Interactor, const FText& Message) const
{
	if (ALostWithinCharacter* Character = Cast<ALostWithinCharacter>(Interactor))
	{
		Character->ShowStatusMessage(Message);
	}
}

bool AToolboxActor::TryUnlock(AActor* Interactor)
{
	if (!IsLocked())
	{
		return true;
	}

	UInventoryComponent* Inventory = Interactor ? Interactor->FindComponentByClass<UInventoryComponent>() : nullptr;
	if (!Inventory || !Inventory->HasItem(RequiredKeyID))
	{
		PlaySound(LockedSound);
		NotifyPlayer(Interactor, bRevealRequiredKey
			? FText::Format(NSLOCTEXT("LostWithin", "ToolboxNeedsKey", "Locked. It needs the {0}."), GetRequiredKeyName())
			: NSLOCTEXT("LostWithin", "ToolboxLocked", "Locked."));
		return false;
	}

	const FText KeyName = GetRequiredKeyName();
	if (bConsumeKeyOnUnlock)
	{
		Inventory->RemoveItemByID(RequiredKeyID, 1);
	}
	RequiredKeyID = NAME_None;
	PlaySound(UnlockSound);
	NotifyPlayer(Interactor, FText::Format(NSLOCTEXT("LostWithin", "ToolboxUnlocked", "Unlocked with the {0}."), KeyName));
	return true;
}

bool AToolboxActor::Open()
{
	if (bOpen || IsLocked())
	{
		return false;
	}

	bOpen = true;
	if (Mesh && OpenAnimation)
	{
		Mesh->PlayAnimation(OpenAnimation, /*bLooping=*/false);
	}
	PlaySound(OpenSound);

	UWorld* World = GetWorld();
	if (!World || ContentsDelay <= 0.0f)
	{
		SpawnContents();
		return true;
	}

	TWeakObjectPtr<AToolboxActor> WeakThis(this);
	World->GetTimerManager().SetTimer(ContentsTimerHandle, [WeakThis]()
	{
		if (AToolboxActor* Toolbox = WeakThis.Get())
		{
			Toolbox->SpawnContents();
		}
	}, ContentsDelay, false);
	return true;
}

APickupItemActor* AToolboxActor::SpawnContents()
{
	if (!bHasContents)
	{
		NotifyPlayer(LastInteractor.Get(), NSLOCTEXT("LostWithin", "ToolboxEmpty", "The toolbox is empty."));
		return nullptr;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	bHasContents = false;

	const FVector Extent = CollisionBox ? CollisionBox->GetScaledBoxExtent() : FVector(30.0f, 15.0f, 15.0f);
	const FVector Forward = GetActorForwardVector().GetSafeNormal2D();
	const FVector Top = GetActorLocation() + FVector(0.0f, 0.0f, Extent.Z * 2.0f + 12.0f);
	const FVector Front = GetActorLocation() + Forward * (Extent.X + 35.0f) + FVector(0.0f, 0.0f, 20.0f);

	UClass* SpawnClass = ContentsClass ? ContentsClass.Get() : APickupItemActor::StaticClass();
	APickupItemActor* Pickup = World->SpawnActorDeferred<APickupItemActor>(
		SpawnClass, FTransform(Top), this, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Pickup)
	{
		UE_LOG(LogLostWithin, Warning, TEXT("%s could not spawn its contents (%s)."), *GetName(), *GetNameSafe(SpawnClass));
		return nullptr;
	}

	Pickup->ApplyItemData(ContentsItem);
	Pickup->FinishSpawning(FTransform(Top));

	if (Pickup->IsNoteItem())
	{
		Pickup->PlaceFlatOnFloor(Front);
	}
	else
	{
		Pickup->DropWithPhysics(Top, Forward * 160.0f + FVector(0.0f, 0.0f, 220.0f), { this });
	}
	return Pickup;
}

void AToolboxActor::OnInteract_Implementation(AActor* Interactor)
{
	if (bOpen)
	{
		return;
	}

	LastInteractor = Interactor;
	if (TryUnlock(Interactor))
	{
		Open();
	}
}

FText AToolboxActor::GetInteractionPrompt_Implementation() const
{
	if (!IsLocked())
	{
		return NSLOCTEXT("LostWithin", "ToolboxOpenPrompt", "[LMB] Open toolbox");
	}

	if (!bRevealRequiredKey)
	{
		return NSLOCTEXT("LostWithin", "ToolboxLockedPrompt", "[LMB] Try the padlock");
	}

	const APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
	return InteractorHasRequiredKey(Player)
		? FText::Format(NSLOCTEXT("LostWithin", "ToolboxUnlockPrompt", "[LMB] Unlock with the {0}"), GetRequiredKeyName())
		: FText::Format(NSLOCTEXT("LostWithin", "ToolboxNeedsKeyPrompt", "Locked: needs the {0}"), GetRequiredKeyName());
}

bool AToolboxActor::CanInteract_Implementation(AActor* Interactor) const
{
	return !bOpen;
}
