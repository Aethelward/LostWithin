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
#include "ReferenceSkeleton.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

const FName AToolboxActor::LidBoneName(TEXT("Cover_Box"));

// Names are unique to this file so unity builds cannot clash with other files' constants.
namespace LostWithinToolboxGeometry
{
	/**
	 *  Measured from /Game/Level/Objects/tool_box/SkeletalMeshes/tool_box fitted to this width, in actor space (cm,
	 *  origin at the bottom centre, +Y is the latch side). The lid bone hinges along X at Y -14.2, Z 20.4.
	 */
	constexpr double ReferenceWidthCm = 60.0;
	const FBox OuterBase(FVector(-30.0, -14.2, 0.0), FVector(30.0, 14.1, 20.4));
	const FBox Cavity(FVector(-28.5, -13.1, 0.4), FVector(28.0, 13.0, 20.4));
	const FBox ClosedLid(FVector(-30.0, -14.2, 20.4), FVector(30.0, 14.1, 24.4));

	/** Gap between a placed item and the inner floor: a note rests on it, a physics item settles onto it */
	constexpr double NoteFloorClearanceCm = 0.4;
	constexpr double ItemFloorClearanceCm = 0.5;

	FBox ScaledBox(const FBox& Box, double Scale)
	{
		return FBox(Box.Min * Scale, Box.Max * Scale);
	}

	void FitBoxToActorSpace(UBoxComponent* Box, const FBox& InActor)
	{
		if (Box)
		{
			Box->SetBoxExtent(InActor.GetExtent(), /*bUpdateOverlaps=*/false);
			Box->SetRelativeLocationAndRotation(InActor.GetCenter(), FRotator::ZeroRotator);
		}
	}
}

AToolboxActor::AToolboxActor()
{
	PrimaryActorTick.bCanEverTick = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Mesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Root);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	Mesh->SetCanEverAffectNavigation(false);
	// The lid cap rides the lid bone; keep it moving even while the box is off screen.
	Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;

	// A hollow box: the floor keeps the original component name; the cavity above it stays free.
	CollisionBox = CreateCollisionPart(TEXT("CollisionBox"), Root, NAME_None, /*bAffectsNavigation=*/true);
	BackWallCollision = CreateCollisionPart(TEXT("BackWallCollision"), Root, NAME_None, true);
	FrontWallCollision = CreateCollisionPart(TEXT("FrontWallCollision"), Root, NAME_None, true);
	LeftWallCollision = CreateCollisionPart(TEXT("LeftWallCollision"), Root, NAME_None, true);
	RightWallCollision = CreateCollisionPart(TEXT("RightWallCollision"), Root, NAME_None, true);
	LidCollision = CreateCollisionPart(TEXT("LidCollision"), Mesh, LidBoneName, /*bAffectsNavigation=*/false);

	// Defaults until OnConstruction fits them to the mesh (the lid cap needs the mesh's bones).
	FitBaseCollision();

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

UBoxComponent* AToolboxActor::CreateCollisionPart(FName Name, USceneComponent* Parent, FName Socket, bool bAffectsNavigation)
{
	UBoxComponent* Box = CreateDefaultSubobject<UBoxComponent>(Name);
	Box->SetupAttachment(Parent, Socket);
	Box->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);
	Box->SetCanEverAffectNavigation(bAffectsNavigation);
	Box->SetGenerateOverlapEvents(false);
	return Box;
}

void AToolboxActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	FitMeshToTargetWidth();
}

FBox AToolboxActor::GetCavityBox() const
{
	return LostWithinToolboxGeometry::ScaledBox(LostWithinToolboxGeometry::Cavity,
		TargetWidthCm / LostWithinToolboxGeometry::ReferenceWidthCm);
}

void AToolboxActor::FitMeshToTargetWidth()
{
	FitBaseCollision();

	const USkeletalMesh* Asset = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
	if (!Asset)
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

	FitLidCollision(LostWithinToolboxGeometry::ScaledBox(LostWithinToolboxGeometry::ClosedLid,
		TargetWidthCm / LostWithinToolboxGeometry::ReferenceWidthCm));
}

void AToolboxActor::FitBaseCollision()
{
	using namespace LostWithinToolboxGeometry;
	const FBox Outer = ScaledBox(OuterBase, TargetWidthCm / ReferenceWidthCm);
	const FBox Inside = GetCavityBox();

	// Floor up to the inner floor; walls from the floor to the rim around the cavity.
	FitBoxToActorSpace(CollisionBox, FBox(Outer.Min, FVector(Outer.Max.X, Outer.Max.Y, Inside.Min.Z)));
	FitBoxToActorSpace(BackWallCollision, FBox(Outer.Min, FVector(Outer.Max.X, Inside.Min.Y, Outer.Max.Z)));
	FitBoxToActorSpace(FrontWallCollision, FBox(FVector(Outer.Min.X, Inside.Max.Y, Outer.Min.Z), Outer.Max));
	FitBoxToActorSpace(LeftWallCollision,
		FBox(FVector(Outer.Min.X, Inside.Min.Y, Outer.Min.Z), FVector(Inside.Min.X, Inside.Max.Y, Outer.Max.Z)));
	FitBoxToActorSpace(RightWallCollision,
		FBox(FVector(Inside.Max.X, Inside.Min.Y, Outer.Min.Z), FVector(Outer.Max.X, Inside.Max.Y, Outer.Max.Z)));
}

void AToolboxActor::FitLidCollision(const FBox& ClosedLidInActor)
{
	if (!LidCollision || !Mesh)
	{
		return;
	}

	LidCollision->SetBoxExtent(ClosedLidInActor.GetExtent(), /*bUpdateOverlaps=*/false);
	const FTransform LidInActor(ClosedLidInActor.GetCenter());

	const USkeletalMesh* Asset = Mesh->GetSkeletalMeshAsset();
	const FReferenceSkeleton* RefSkeleton = Asset ? &Asset->GetRefSkeleton() : nullptr;
	const int32 LidBone = RefSkeleton ? RefSkeleton->FindBoneIndex(LidBoneName) : INDEX_NONE;
	if (LidBone == INDEX_NONE)
	{
		// Without the bone the cap is attached to the mesh itself and stays closed.
		UE_LOG(LogLostWithin, Warning, TEXT("%s: mesh %s has no bone %s; the lid collision will not open."),
			*GetName(), *GetNameSafe(Asset), *LidBoneName.ToString());
		LidCollision->SetRelativeTransform(LidInActor.GetRelativeTransform(Mesh->GetRelativeTransform()));
		return;
	}

	// The bone's reference pose (the closed lid) in actor space, independent of whatever pose the mesh shows now.
	FTransform BoneInMesh = FTransform::Identity;
	for (int32 Bone = LidBone; Bone != INDEX_NONE; Bone = RefSkeleton->GetParentIndex(Bone))
	{
		BoneInMesh = BoneInMesh * RefSkeleton->GetRefBonePose()[Bone];
	}
	const FTransform BoneInActor = BoneInMesh * Mesh->GetRelativeTransform();

	// Relative to the bone the cap carries the inverse of the bone's tiny import scale, so it keeps world scale 1.
	LidCollision->SetRelativeTransform(LidInActor.GetRelativeTransform(BoneInActor));
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

	// Contents go into the still-closed box now, so the lifting lid reveals them; only "empty" waits for the lid.
	UWorld* World = GetWorld();
	if (bHasContents || !World || ContentsDelay <= 0.0f)
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

	const FTransform SpawnTransform(GetActorQuat(), GetActorTransform().TransformPosition(GetCavityBox().GetCenter()));
	UClass* SpawnClass = ContentsClass ? ContentsClass.Get() : APickupItemActor::StaticClass();
	APickupItemActor* Pickup = World->SpawnActorDeferred<APickupItemActor>(
		SpawnClass, SpawnTransform, this, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Pickup)
	{
		UE_LOG(LogLostWithin, Warning, TEXT("%s could not spawn its contents (%s)."), *GetName(), *GetNameSafe(SpawnClass));
		return nullptr;
	}

	Pickup->ApplyItemData(ContentsItem);
	Pickup->FinishSpawning(SpawnTransform);
	PlaceInsideCavity(*Pickup);
	return Pickup;
}

void AToolboxActor::PlaceInsideCavity(APickupItemActor& Pickup) const
{
	UPrimitiveComponent* Body = Cast<UPrimitiveComponent>(Pickup.GetRootComponent());
	if (!Body)
	{
		return;
	}

	using namespace LostWithinToolboxGeometry;
	const FTransform BoxTransform = GetActorTransform();
	const FBox Inside = GetCavityBox();
	const bool bIsNote = Pickup.IsNoteItem();
	const FRotator Resting = bIsNote ? Pickup.NoteFlatRotation : FRotator::ZeroRotator;
	const FVector BodyScaleInBox = Body->GetComponentScale() / BoxTransform.GetScale3D();

	// The item's bounds in actor space for a yaw relative to the box, with its pivot at the origin.
	auto BoundsInBox = [Body, &Resting, &BodyScaleInBox](double ExtraYaw, FQuat& OutRotation)
	{
		OutRotation = FRotator(Resting.Pitch, Resting.Yaw + ExtraYaw, Resting.Roll).Quaternion();
		return Body->CalcBounds(FTransform(OutRotation, FVector::ZeroVector, BodyScaleInBox)).GetBox();
	};

	// Lay the longer side along the box's length.
	FQuat LocalRotation;
	FBox Bounds = BoundsInBox(0.0, LocalRotation);
	if (Bounds.GetSize().Y > Bounds.GetSize().X)
	{
		Bounds = BoundsInBox(90.0, LocalRotation);
	}

	const FVector Size = Bounds.GetSize();
	const FVector Room = Inside.GetSize();
	if (Size.X > Room.X || Size.Y > Room.Y || Size.Z > Room.Z)
	{
		UE_LOG(LogLostWithin, Warning, TEXT("%s: %s (%.1f x %.1f x %.1f cm) is larger than the cavity (%.1f x %.1f x %.1f cm)."),
			*GetName(), *Pickup.GetName(), Size.X, Size.Y, Size.Z, Room.X, Room.Y, Room.Z);
	}

	// Centred over the cavity, resting just above the inner floor.
	const double Clearance = bIsNote ? NoteFloorClearanceCm : ItemFloorClearanceCm;
	const FVector PivotInBox(
		Inside.GetCenter().X - Bounds.GetCenter().X,
		Inside.GetCenter().Y - Bounds.GetCenter().Y,
		Inside.Min.Z + Clearance - Bounds.Min.Z);

	Pickup.SetActorLocationAndRotation(BoxTransform.TransformPosition(PivotInBox), BoxTransform.GetRotation() * LocalRotation,
		/*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics);
	if (Body->IsSimulatingPhysics())
	{
		Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
		Body->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
	}
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
		return NSLOCTEXT("LostWithin", "ToolboxOpenPrompt", "[E] Open toolbox");
	}

	if (!bRevealRequiredKey)
	{
		return NSLOCTEXT("LostWithin", "ToolboxLockedPrompt", "[E] Try the padlock");
	}

	const APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
	return InteractorHasRequiredKey(Player)
		? FText::Format(NSLOCTEXT("LostWithin", "ToolboxUnlockPrompt", "[E] Unlock with the {0}"), GetRequiredKeyName())
		: FText::Format(NSLOCTEXT("LostWithin", "ToolboxNeedsKeyPrompt", "Locked: needs the {0}"), GetRequiredKeyName());
}

bool AToolboxActor::CanInteract_Implementation(AActor* Interactor) const
{
	return !bOpen;
}
