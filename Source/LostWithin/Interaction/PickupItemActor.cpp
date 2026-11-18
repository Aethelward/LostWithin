// Copyright Epic Games, Inc. All Rights Reserved.

#include "Interaction/PickupItemActor.h"
#include "Components/PointLightComponent.h"
#include "Components/SphereComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Gameplay/LostWithinNoiseSubsystem.h"
#include "Gameplay/RunSetupRules.h"
#include "Kismet/GameplayStatics.h"
#include "LostWithinCharacter.h"
#include "PhysicsEngine/BodySetup.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundWave.h"
#include "Kismet/GameplayStatics.h"

// Define helper to load impact sounds
namespace
{
	USoundBase* LoadImpactSound(FName ItemID, int32 Index)
	{
		// Keys are in a shared folder "Keys" not named by ItemID directly
		FString FolderName = ItemID.ToString();
		if (ItemID == APickupItemActor::KeyItemID || ItemID == APickupItemActor::OldKeyItemID || ItemID == APickupItemActor::RoyalKeyItemID || ItemID == APickupItemActor::FairyTaleKittyKeyItemID)
		{
			FolderName = TEXT("Keys");
			Index = 1; // Keys only have Impact_01
		}
		else if (ItemID == APickupItemActor::BatteryItemID)
		{
			FolderName = TEXT("Battery");
		}
		else if (ItemID == APickupItemActor::FlashlightItemID)
		{
			FolderName = TEXT("Flashlight");
		}
		
		FString Path = FString::Printf(TEXT("/Game/Item/%s/SFX/Impact/Impact_%02d.%s"), *FolderName, Index, *FString::Printf(TEXT("Impact_%02d"), Index));
        
        // Use StaticLoadObject for more reliable loading during runtime
        USoundBase* LoadedSound = Cast<USoundBase>(StaticLoadObject(USoundBase::StaticClass(), nullptr, *Path));
        
        if (!LoadedSound)
        {
            UE_LOG(LogTemp, Warning, TEXT("Failed to load impact sound at path: %s"), *Path);
        }
        
		return LoadedSound;
	}
}

const FName APickupItemActor::PlaceholderItemID = TEXT("PlaceholderItem");
const FName APickupItemActor::FlashlightItemID = TEXT("Flashlight");
const FName APickupItemActor::BatteryItemID = TEXT("Battery");
const FName APickupItemActor::FairyTaleKittyKeyItemID = TEXT("FairyTaleKittyKey");
const FName APickupItemActor::KeyItemID = TEXT("Key");
const FName APickupItemActor::OldKeyItemID = TEXT("OldKey");
const FName APickupItemActor::RoyalKeyItemID = TEXT("RoyalKey");
const FName APickupItemActor::PaperNoteItemID = TEXT("PaperNote");

const TCHAR* APickupItemActor::FlashlightMeshAssetPath =
	TEXT("/Game/Item/Flashlight/StaticMeshes/defaultMaterial.defaultMaterial");
const TCHAR* APickupItemActor::FlashlightAltMeshAssetPath =
	TEXT("/Game/Item/Flashlight/StaticMeshes/defaultMaterial_823209595c9c04cda8328f84d8541395.defaultMaterial_823209595c9c04cda8328f84d8541395");
const TCHAR* APickupItemActor::BatteryMeshAssetPath =
	TEXT("/Game/Item/Battery/StaticMeshes/dry_cell_-_aa_battery.dry_cell_-_aa_battery");
const TCHAR* APickupItemActor::FairyTaleKittyKeyMeshAssetPath =
	TEXT("/Game/Item/Keys/fairy_tale_kitty_key/StaticMeshes/fairy_tale_kitty_key.fairy_tale_kitty_key");
const TCHAR* APickupItemActor::KeyMeshAssetPath =
	TEXT("/Game/Item/Keys/key/StaticMeshes/key.key");
const TCHAR* APickupItemActor::OldKeyMeshAssetPath =
	TEXT("/Game/Item/Keys/old_key/StaticMeshes/old_key.old_key");
const TCHAR* APickupItemActor::RoyalKeyMeshAssetPath =
	TEXT("/Game/Item/Keys/royal_key_2/StaticMeshes/royal_key_2.royal_key_2");
const TCHAR* APickupItemActor::PaperNoteMeshAssetPath =
	TEXT("/Game/Item/paper/StaticMeshes/paper.paper");

namespace
{
	constexpr float DefaultFallbackMeshScale = 0.25f;

	struct FItemCatalogEntry
	{
		ELostWithinItemPreset Preset;
		FName ItemID;
		const TCHAR* DisplayName;
		const TCHAR* Description;
		int32 MaxStackSize;
		const TCHAR* MeshPath;
		const TCHAR* AlternateMeshPath;
		bool bIsFlashlight;
		bool bIsKey;
		bool bIsBattery;
		bool bIsNote = false;
	};

	const FItemCatalogEntry GBuiltInItemCatalog[] = {
		{
			ELostWithinItemPreset::Flashlight,
			APickupItemActor::FlashlightItemID,
			TEXT("Flashlight"),
			TEXT("Tactical flashlight. Press [F] to toggle light, [R] to change battery."),
			1,
			APickupItemActor::FlashlightMeshAssetPath,
			APickupItemActor::FlashlightAltMeshAssetPath,
			true,
			false,
			false
		},
		{
			ELostWithinItemPreset::Battery,
			APickupItemActor::BatteryItemID,
			TEXT("Battery"),
			TEXT("AA dry-cell battery used to power the tactical flashlight."),
			1,
			APickupItemActor::BatteryMeshAssetPath,
			nullptr,
			false,
			false,
			true
		},
		{
			ELostWithinItemPreset::FairyTaleKittyKey,
			APickupItemActor::FairyTaleKittyKeyItemID,
			TEXT("Fairy Tale Kitty Key"),
			TEXT("An ornate cat-themed key dropped by the cat."),
			99,
			APickupItemActor::FairyTaleKittyKeyMeshAssetPath,
			nullptr,
			false,
			true,
			false
		},
		{
			ELostWithinItemPreset::Key,
			APickupItemActor::KeyItemID,
			TEXT("Key"),
			TEXT("A standard key."),
			99,
			APickupItemActor::KeyMeshAssetPath,
			nullptr,
			false,
			true,
			false
		},
		{
			ELostWithinItemPreset::OldKey,
			APickupItemActor::OldKeyItemID,
			TEXT("Old Key"),
			TEXT("A weathered old key."),
			99,
			APickupItemActor::OldKeyMeshAssetPath,
			nullptr,
			false,
			true,
			false
		},
		{
			ELostWithinItemPreset::RoyalKey,
			APickupItemActor::RoyalKeyItemID,
			TEXT("Royal Key"),
			TEXT("An ornate royal key."),
			99,
			APickupItemActor::RoyalKeyMeshAssetPath,
			nullptr,
			false,
			true,
			false
		},
		{
			ELostWithinItemPreset::PaperNote,
			APickupItemActor::PaperNoteItemID,
			TEXT("Note"),
			TEXT("A torn page with shaky handwriting."),
			1,
			APickupItemActor::PaperNoteMeshAssetPath,
			nullptr,
			false,
			false,
			false,
			true
		}
	};

	FString ExtractPackagePath(const FString& ObjectPath)
	{
		int32 DotIndex = INDEX_NONE;
		if (ObjectPath.FindChar(TEXT('.'), DotIndex))
		{
			return ObjectPath.Left(DotIndex);
		}
		return ObjectPath;
	}

	const FItemCatalogEntry* FindCatalogEntryByPreset(ELostWithinItemPreset Preset)
	{
		for (const FItemCatalogEntry& Entry : GBuiltInItemCatalog)
		{
			if (Entry.Preset == Preset)
			{
				return &Entry;
			}
		}
		return nullptr;
	}

	const FItemCatalogEntry* FindCatalogEntryByItemID(FName ItemID)
	{
		if (ItemID.IsNone())
		{
			return nullptr;
		}

		const bool bIsNote = APickupItemActor::IsNoteItemID(ItemID);
		for (const FItemCatalogEntry& Entry : GBuiltInItemCatalog)
		{
			if (Entry.ItemID == ItemID || (bIsNote && Entry.bIsNote))
			{
				return &Entry;
			}
		}
		return nullptr;
	}

	const FItemCatalogEntry* FindCatalogEntryByMeshPath(const FString& MeshObjectPath)
	{
		if (MeshObjectPath.IsEmpty())
		{
			return nullptr;
		}

		const FString TargetPackage = ExtractPackagePath(MeshObjectPath);
		for (const FItemCatalogEntry& Entry : GBuiltInItemCatalog)
		{
			if (ExtractPackagePath(Entry.MeshPath) == TargetPackage)
			{
				return &Entry;
			}
			if (Entry.AlternateMeshPath && ExtractPackagePath(Entry.AlternateMeshPath) == TargetPackage)
			{
				return &Entry;
			}
		}
		return nullptr;
	}

	FString FormatDisplayNameFromAssetName(const FString& AssetName)
	{
		TArray<FString> Tokens;
		AssetName.Replace(TEXT("-"), TEXT("_")).ParseIntoArray(Tokens, TEXT("_"), true);

		for (FString& Token : Tokens)
		{
			if (!Token.IsEmpty())
			{
				Token[0] = FChar::ToUpper(Token[0]);
			}
		}

		return Tokens.IsEmpty() ? AssetName : FString::Join(Tokens, TEXT(" "));
	}

	bool IsPlaceholderOrUnsetItemID(FName ItemID)
	{
		return ItemID.IsNone() || ItemID == APickupItemActor::PlaceholderItemID;
	}
}

bool APickupItemActor::IsNoteItemID(FName ItemID)
{
	return !ItemID.IsNone() && ItemID.ToString().StartsWith(PaperNoteItemID.ToString(), ESearchCase::CaseSensitive);
}

FName APickupItemActor::MakeUniqueNoteItemID()
{
	return FName(*FString::Printf(TEXT("%s_%s"), *PaperNoteItemID.ToString(), *FGuid::NewGuid().ToString(EGuidFormats::Base36Encoded)));
}

bool APickupItemActor::IsKeyItemID(FName ItemID)
{
	const FItemCatalogEntry* Entry = FindCatalogEntryByItemID(ItemID);
	return Entry && Entry->bIsKey;
}

FText APickupItemActor::GetItemDisplayName(FName ItemID)
{
	const FItemCatalogEntry* Entry = FindCatalogEntryByItemID(ItemID);
	return Entry ? FText::FromString(Entry->DisplayName) : FText::FromName(ItemID);
}

APickupItemActor::APickupItemActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComponent"));
	SetRootComponent(MeshComponent);
	MeshComponent->SetMobility(EComponentMobility::Movable);
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	MeshComponent->SetCollisionObjectType(ECC_PhysicsBody);
	MeshComponent->SetCollisionResponseToAllChannels(ECR_Block);
	MeshComponent->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	MeshComponent->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	MeshComponent->SetLinearDamping(0.35f);
	MeshComponent->SetAngularDamping(2.5f);
	MeshComponent->SetEnableGravity(true);
	MeshComponent->SetRelativeScale3D(FVector(DefaultFallbackMeshScale));
    MeshComponent->SetNotifyRigidBodyCollision(true); // Ensure collision events are generated

	CollisionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("CollisionSphere"));
	CollisionSphere->SetupAttachment(MeshComponent);
	CollisionSphere->SetUsingAbsoluteScale(true);
	CollisionSphere->InitSphereRadius(28.0f);
	CollisionSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	CollisionSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
	CollisionSphere->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	CollisionSphere->SetGenerateOverlapEvents(false);

	FlashlightSpotLight = CreateDefaultSubobject<USpotLightComponent>(TEXT("FlashlightSpotLight"));
	FlashlightSpotLight->SetupAttachment(MeshComponent);
	FlashlightSpotLight->SetUsingAbsoluteScale(true);
	FlashlightSpotLight->SetMobility(EComponentMobility::Movable);
	FlashlightSpotLight->SetCastShadows(true);
	FlashlightSpotLight->SetVisibility(false);

	FlashlightFillLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("FlashlightFillLight"));
	FlashlightFillLight->SetupAttachment(MeshComponent);
	FlashlightFillLight->SetUsingAbsoluteScale(true);
	FlashlightFillLight->SetMobility(EComponentMobility::Movable);
	FlashlightFillLight->SetCastShadows(false);
	FlashlightFillLight->SetVisibility(false);

	ApplyFlashlightLightingParams(FlashlightSpotLight, FlashlightFillLight, FlashlightLighting);

	ItemData.ItemID = PlaceholderItemID;
	ItemData.DisplayName = FText::FromString(TEXT("Placeholder Item"));
	ItemData.Description = FText::FromString(TEXT("An item resting in the world."));
	ItemData.Quantity = 1;
	ItemData.MaxStackSize = 99;
}

void APickupItemActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RefreshItemSetup(/*bForceApplyCatalogScale=*/false);
}

void APickupItemActor::BeginPlay()
{
	Super::BeginPlay();
	RefreshItemSetup(/*bForceApplyCatalogScale=*/false);
	EnsureDefaultDropSounds();
	EnableItemPhysicsSimulation();
	if (IsNoteItem())
	{
		// A note placed by hand (not by the run setup) still gets a random phrase.
		const TCHAR* DefaultNoteDescription = TEXT("A torn page with shaky handwriting.");
		if (ItemData.Description.IsEmpty() || ItemData.Description.ToString() == DefaultNoteDescription)
		{
			const TArray<FString>& Phrases = LostWithinRunRules::DefaultFlavorPhrases();
			ItemData.Description = FText::FromString(Phrases[FMath::RandRange(0, Phrases.Num() - 1)]);
		}
		PlaceFlatOnFloor(GetActorLocation());
	}
}

void APickupItemActor::OnMeshHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	if (IsNoteItem())
	{
		return; // Paper is silent and has no impact sounds.
	}

	if (NormalImpulse.Size() > 50.0f && OtherActor != this)
	{
		// Only trigger for surface impacts (Z normal > 0.7)
		if (Hit.ImpactNormal.Z < 0.7f) return;
		// Per-item debounce. A function-local static outlived each PIE world while world time restarted at 0.
		const double CurrentTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
		
		if (CurrentTime - LastImpactSoundTime > ImpactSoundCooldownSeconds)
		{
			LastImpactSoundTime = CurrentTime;
			ReportImpactNoise(Hit.ImpactPoint);

            UE_LOG(LogTemp, Log, TEXT("OnMeshHit called for item: %s, Impulse: %f"), *ItemData.ItemID.ToString(), NormalImpulse.Size());

			int32 ChosenIndex = 1;
			if (ItemData.ItemID != APickupItemActor::KeyItemID && 
			    ItemData.ItemID != APickupItemActor::OldKeyItemID && 
			    ItemData.ItemID != APickupItemActor::RoyalKeyItemID)
			{
				ChosenIndex = FMath::RandRange(1, 3);
			}

			if (USoundBase* ImpactSound = LoadImpactSound(ItemData.ItemID, ChosenIndex))
			{
				UGameplayStatics::PlaySoundAtLocation(this, ImpactSound, Hit.ImpactPoint, 1.0f, FMath::FRandRange(0.9f, 1.1f));
                UE_LOG(LogTemp, Log, TEXT("Playing impact sound: %s"), *ImpactSound->GetName());
			}
            else
            {
                UE_LOG(LogTemp, Warning, TEXT("Failed to load sound for item: %s, Index: %d"), *ItemData.ItemID.ToString(), ChosenIndex);
            }
		}
	}
}

void APickupItemActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!IsFlashlightOn())
	{
		SetActorTickEnabled(false);
		return;
	}

	const bool bStillOperational = ItemData.ConsumeFlashlightCharge(DeltaTime, FlashlightBatteryDurationSeconds);
	if (!bStillOperational)
	{
		ItemData.bIsFlashlightOn = false;
	}

	RefreshFlashlightLightState();
}

#if WITH_EDITOR
void APickupItemActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropName = PropertyChangedEvent.GetPropertyName();
	if (PropName == GET_MEMBER_NAME_CHECKED(APickupItemActor, ItemPreset))
	{
		if (ItemPreset != ELostWithinItemPreset::Custom)
		{
			ConfigureFromPreset(ItemPreset, /*bForceApplyPresetScale=*/true);
		}
		else
		{
			RefreshItemSetup(/*bForceApplyCatalogScale=*/false);
		}
		return;
	}

	if (PropName == GET_MEMBER_NAME_CHECKED(APickupItemActor, FlashlightPickupScale)
		|| PropName == GET_MEMBER_NAME_CHECKED(APickupItemActor, BatteryPickupScale)
		|| PropName == GET_MEMBER_NAME_CHECKED(APickupItemActor, TargetKeyWorldSizeCm))
	{
		RefreshItemSetup(/*bForceApplyCatalogScale=*/true);
		return;
	}

	RefreshItemSetup(/*bForceApplyCatalogScale=*/false);
}
#endif

void APickupItemActor::ConfigureFromPreset(ELostWithinItemPreset Preset, bool bForceApplyPresetScale)
{
	ItemPreset = Preset;
	if (const FItemCatalogEntry* Entry = FindCatalogEntryByPreset(Preset))
	{
		ItemData.ItemID = Entry->ItemID;
		ItemData.DisplayName = FText::FromString(Entry->DisplayName);
		ItemData.Description = FText::FromString(Entry->Description);
		ItemData.Quantity = FMath::Max(1, ItemData.Quantity);
		ItemData.MaxStackSize = Entry->MaxStackSize;
		ItemData.WorldMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(Entry->MeshPath));
		ItemData.bHasCustomWorldMeshScale = false;
		if (Entry->bIsFlashlight)
		{
			ItemData.BatterySlot1Percent = 100.0f;
			ItemData.BatterySlot2Percent = 100.0f;
		}
		else
		{
			ItemData.bIsFlashlightOn = false;
		}
		if (Entry->bIsBattery)
		{
			ItemData.BatteryChargePercent = FMath::Clamp(ItemData.BatteryChargePercent, 0.0f, 100.0f);
		}
	}

	RefreshItemSetup(bForceApplyPresetScale);

	if (HasActorBegunPlay())
	{
		EnableItemPhysicsSimulation();
	}
}

void APickupItemActor::ConfigureAsFlashlight()
{
	ItemData.Quantity = 1;
	ConfigureFromPreset(ELostWithinItemPreset::Flashlight, /*bForceApplyPresetScale=*/true);
}

void APickupItemActor::ConfigureAsBattery(float ChargePercent)
{
	ItemData.Quantity = 1;
	ItemData.BatteryChargePercent = FMath::Clamp(ChargePercent, 0.0f, 100.0f);
	ConfigureFromPreset(ELostWithinItemPreset::Battery, /*bForceApplyPresetScale=*/true);
}

void APickupItemActor::ConfigureAsFairyTaleKittyKey()
{
	ItemData.Quantity = 1;
	ConfigureFromPreset(ELostWithinItemPreset::FairyTaleKittyKey, /*bForceApplyPresetScale=*/true);
}

void APickupItemActor::ConfigureAsNote(const FText& NoteText)
{
	ItemData.Quantity = 1;
	ConfigureFromPreset(ELostWithinItemPreset::PaperNote, /*bForceApplyPresetScale=*/true);
	ItemData.ItemID = MakeUniqueNoteItemID();
	if (!NoteText.IsEmpty())
	{
		ItemData.Description = NoteText;
	}
}

bool APickupItemActor::IsNoteItem() const
{
	return ItemPreset == ELostWithinItemPreset::PaperNote || IsNoteItemID(ItemData.ItemID);
}

void APickupItemActor::PlaceFlatOnFloor(const FVector& Around)
{
	UWorld* World = GetWorld();
	if (!World || !MeshComponent)
	{
		return;
	}

	if (MeshComponent->IsSimulatingPhysics())
	{
		MeshComponent->SetSimulatePhysics(false);
	}

	SetActorRotation(FRotator(NoteFlatRotation.Pitch, FMath::FRandRange(-180.0f, 180.0f), NoteFlatRotation.Roll));

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(LostWithinNoteFloorTrace), /*bTraceComplex=*/false, this);
	FHitResult FloorHit;
	const bool bHasFloor = World->LineTraceSingleByObjectType(
		FloorHit,
		Around + FVector(0.0f, 0.0f, 60.0f),
		Around - FVector(0.0f, 0.0f, 1000.0f),
		FCollisionObjectQueryParams(ECC_WorldStatic),
		QueryParams);
	const double FloorZ = bHasFloor ? FloorHit.ImpactPoint.Z : Around.Z;

	SetActorLocation(FVector(Around.X, Around.Y, FloorZ));
	const FBox WorldBounds = MeshComponent->Bounds.GetBox();
	if (WorldBounds.IsValid)
	{
		// The mesh pivot is offset from the sheet; rest the sheet itself just above the floor.
		AddActorWorldOffset(FVector(0.0f, 0.0f, (FloorZ + 0.4) - WorldBounds.Min.Z));
	}
}

void APickupItemActor::ApplyItemData(const FInventoryItem& InItemData)
{
	ItemData = InItemData;
	ItemData.Quantity = FMath::Max(1, ItemData.Quantity);

	if (const FItemCatalogEntry* Entry = FindCatalogEntryByItemID(ItemData.ItemID))
	{
		ItemPreset = Entry->Preset;
	}
	else
	{
		ItemPreset = ELostWithinItemPreset::Custom;
	}

	if (MeshComponent && MeshComponent->IsSimulatingPhysics())
	{
		MeshComponent->SetSimulatePhysics(false);
	}

	RefreshItemSetup(/*bForceApplyCatalogScale=*/!ItemData.bHasCustomWorldMeshScale);

	if (HasActorBegunPlay())
	{
		EnableItemPhysicsSimulation();
	}
}

void APickupItemActor::DropWithPhysics(const FVector& StartLocation, const FVector& InitialVelocity, const TArray<AActor*>& ActorsToIgnore)
{
	if (IsNoteItem())
	{
		SetActorLocation(StartLocation);
		PlaceFlatOnFloor(StartLocation);
		return;
	}

	RefreshItemSetup(/*bForceApplyCatalogScale=*/false);

	if (MeshComponent)
	{
		MeshComponent->SetSimulatePhysics(false);
	}

	SetActorLocation(StartLocation, false, nullptr, ETeleportType::TeleportPhysics);

	if (MeshComponent)
	{
		for (AActor* IgnoredActor : ActorsToIgnore)
		{
			if (IgnoredActor)
			{
				MeshComponent->IgnoreActorWhenMoving(IgnoredActor, true);
			}
		}
	}

	EnableItemPhysicsSimulation();

	if (MeshComponent && MeshComponent->IsSimulatingPhysics())
	{
		MeshComponent->SetPhysicsLinearVelocity(InitialVelocity);
	}

	PlayRandomDropSound();
}

bool APickupItemActor::IsSimulatingItemPhysics() const
{
	return MeshComponent && MeshComponent->IsSimulatingPhysics();
}

bool APickupItemActor::IsFlashlightItem() const
{
	return ItemPreset == ELostWithinItemPreset::Flashlight || ItemData.ItemID == FlashlightItemID;
}

void APickupItemActor::SetFlashlightOn(bool bInIsOn)
{
	ItemData.bIsFlashlightOn = IsFlashlightItem() && bInIsOn && ItemData.CanOperateFlashlight();
	RefreshFlashlightLightState();
}

FInventoryItem APickupItemActor::MakeBatteryInventoryItem(float ChargePercent)
{
	FInventoryItem BatteryItem;
	BatteryItem.ItemID = BatteryItemID;
	BatteryItem.DisplayName = FText::FromString(TEXT("Battery"));
	BatteryItem.Description = FText::FromString(TEXT("AA dry-cell battery used to power the tactical flashlight."));
	BatteryItem.Quantity = 1;
	BatteryItem.MaxStackSize = 1;
	BatteryItem.BatteryChargePercent = FMath::Clamp(ChargePercent, 0.0f, 100.0f);
	BatteryItem.WorldMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(BatteryMeshAssetPath));
	BatteryItem.bHasCustomWorldMeshScale = true;
	BatteryItem.WorldMeshScale = FVector(DefaultBatteryPickupScale);
	return BatteryItem;
}

FLostWithinFlashlightLightParams APickupItemActor::ComputeEffectiveFlashlightLightParams(
	const FLostWithinFlashlightLightParams& BaseParams,
	float CombinedBatteryPercent)
{
	FLostWithinFlashlightLightParams Effective = BaseParams;
	const float ClampedPercent = FMath::Clamp(CombinedBatteryPercent, 0.0f, 100.0f);
	if (ClampedPercent <= KINDA_SMALL_NUMBER)
	{
		Effective.SpotIntensity = 0.0f;
		Effective.FillIntensity = 0.0f;
		return Effective;
	}

	// 25% uses base (1x) values; 100% reaches 3x brightness & cone; <25% dims linearly toward 0.25x brightness.
	float BrightnessMultiplier = 1.0f;
	float ConeMultiplier = 1.0f;
	if (ClampedPercent < 25.0f)
	{
		const float LowAlpha = FMath::Clamp(ClampedPercent / 25.0f, 0.0f, 1.0f);
		BrightnessMultiplier = FMath::Lerp(0.25f, 1.0f, LowAlpha);
		ConeMultiplier = 1.0f;
	}
	else
	{
		const float HighAlpha = FMath::Clamp((ClampedPercent - 25.0f) / 75.0f, 0.0f, 1.0f);
		BrightnessMultiplier = FMath::Lerp(1.0f, 3.0f, HighAlpha);
		ConeMultiplier = FMath::Lerp(1.0f, 3.0f, HighAlpha);
	}

	Effective.SpotIntensity = FMath::Max(0.0f, BaseParams.SpotIntensity * BrightnessMultiplier);
	Effective.FillIntensity = FMath::Max(0.0f, BaseParams.FillIntensity * BrightnessMultiplier);
	Effective.OuterConeAngle = FMath::Clamp(BaseParams.OuterConeAngle * ConeMultiplier, 2.0f, 89.0f);
	Effective.InnerConeAngle = FMath::Clamp(BaseParams.InnerConeAngle * ConeMultiplier, 1.0f, Effective.OuterConeAngle);
	return Effective;
}

void APickupItemActor::ApplyFlashlightLightingParams(
	USpotLightComponent* SpotLight,
	UPointLightComponent* FillLight,
	const FLostWithinFlashlightLightParams& Params)
{
	if (SpotLight)
	{
		SpotLight->SetIntensityUnits(ELightUnits::Candelas);
		SpotLight->SetIntensity(Params.SpotIntensity);
		SpotLight->SetAttenuationRadius(Params.AttenuationRadius);
		SpotLight->SetOuterConeAngle(Params.OuterConeAngle);
		SpotLight->SetInnerConeAngle(FMath::Min(Params.InnerConeAngle, Params.OuterConeAngle));
		SpotLight->SetLightColor(Params.LightColor);
	}

	if (FillLight)
	{
		FillLight->SetIntensityUnits(ELightUnits::Candelas);
		FillLight->SetIntensity(Params.FillIntensity);
		FillLight->SetAttenuationRadius(FMath::Min(280.0f, Params.AttenuationRadius * 0.12f));
		FillLight->SetLightColor(Params.LightColor);
	}
}

void APickupItemActor::OnInteract_Implementation(AActor* Interactor)
{
	if (!Interactor)
	{
		return;
	}

	if (UInventoryComponent* Inventory = Interactor->FindComponentByClass<UInventoryComponent>())
	{
		FInventoryItem ItemToAdd = ItemData;
		if (IsNoteItem() && (ItemToAdd.ItemID == PaperNoteItemID || !IsNoteItemID(ItemToAdd.ItemID)))
		{
			ItemToAdd.ItemID = MakeUniqueNoteItemID();
		}
		if (MeshComponent && MeshComponent->GetStaticMesh())
		{
			ItemToAdd.WorldMesh = MeshComponent->GetStaticMesh();
			if (!ItemToAdd.bHasCustomWorldMeshScale)
			{
				ItemToAdd.bHasCustomWorldMeshScale = true;
				ItemToAdd.WorldMeshScale = MeshComponent->GetRelativeScale3D();
			}
		}

		int32 RemainingQuantity = 0;
		if (Inventory->AddItem(ItemToAdd, RemainingQuantity))
		{
			if (ALostWithinCharacter* Character = Cast<ALostWithinCharacter>(Interactor))
			{
				Character->PlayPickupAnimation();
				if (IsNoteItem())
				{
					Character->ShowNote(ItemToAdd.DisplayName, ItemToAdd.Description);
				}
			}

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

FText APickupItemActor::GetInteractionPrompt_Implementation() const
{
	const FText SafeName = ItemData.DisplayName.IsEmpty()
		? FText::FromName(ItemData.ItemID)
		: ItemData.DisplayName;

	if (IsNoteItem())
	{
		return NSLOCTEXT("LostWithin", "PickupNotePrompt", "[LMB] Read note");
	}

	if (ItemPreset == ELostWithinItemPreset::Battery || ItemData.ItemID == BatteryItemID)
	{
		return FText::Format(
			NSLOCTEXT("LostWithin", "PickupBatteryPrompt", "[LMB] Pick up {0} ({1}%)"),
			SafeName,
			FText::AsNumber(FMath::RoundToInt(ItemData.BatteryChargePercent)));
	}

	if (IsFlashlightItem())
	{
		const int32 BatteryCount = ItemData.GetInstalledBatteryCount();
		if (BatteryCount < 2)
		{
			return FText::Format(
				NSLOCTEXT("LostWithin", "PickupFlashlightNeedBatteriesPrompt", "[LMB] Pick up {0} ({1}/2 Batteries)"),
				SafeName,
				FText::AsNumber(BatteryCount));
		}

		return FText::Format(
			NSLOCTEXT("LostWithin", "PickupFlashlightBatteryPrompt", "[LMB] Pick up {0} ({1}%)"),
			SafeName,
			FText::AsNumber(FMath::RoundToInt(ItemData.GetCombinedBatteryPercent())));
	}

	if (ItemData.Quantity > 1)
	{
		return FText::Format(
			NSLOCTEXT("LostWithin", "PickupItemStackPrompt", "[LMB] Pick up {0} (x{1})"),
			SafeName,
			FText::AsNumber(ItemData.Quantity));
	}

	return FText::Format(NSLOCTEXT("LostWithin", "PickupItemPrompt", "[LMB] Pick up {0}"), SafeName);
}

bool APickupItemActor::CanInteract_Implementation(AActor* Interactor) const
{
	return Interactor != nullptr && Interactor->FindComponentByClass<UInventoryComponent>() != nullptr;
}

void APickupItemActor::RefreshItemSetup(bool bForceApplyCatalogScale)
{
	if (!MeshComponent)
	{
		return;
	}

	const bool bWasSimulating = MeshComponent->IsSimulatingPhysics();
	if (bWasSimulating)
	{
		MeshComponent->SetSimulatePhysics(false);
	}

	// 1. Resolve catalog entry from Preset, ItemID, or assigned /Game/Item/* mesh
	const FItemCatalogEntry* CatalogEntry = nullptr;
	if (ItemPreset != ELostWithinItemPreset::Custom)
	{
		CatalogEntry = FindCatalogEntryByPreset(ItemPreset);
	}
	if (!CatalogEntry)
	{
		CatalogEntry = FindCatalogEntryByItemID(ItemData.ItemID);
	}
	if (!CatalogEntry && MeshComponent->GetStaticMesh())
	{
		if (const FItemCatalogEntry* MeshEntry = FindCatalogEntryByMeshPath(MeshComponent->GetStaticMesh()->GetPathName()))
		{
			CatalogEntry = MeshEntry;
			ItemPreset = MeshEntry->Preset;
		}
	}

	if (CatalogEntry)
	{
		if (IsPlaceholderOrUnsetItemID(ItemData.ItemID) || ItemPreset == CatalogEntry->Preset)
		{
			const bool bKeepUniqueNoteID = CatalogEntry->bIsNote && IsNoteItemID(ItemData.ItemID);
			ItemData.ItemID = bKeepUniqueNoteID ? ItemData.ItemID : CatalogEntry->ItemID;
			if (ItemData.DisplayName.IsEmpty() || ItemData.DisplayName.ToString() == TEXT("Placeholder Item"))
			{
				ItemData.DisplayName = FText::FromString(CatalogEntry->DisplayName);
			}
			if (ItemData.Description.IsEmpty() || ItemData.Description.ToString() == TEXT("An item resting in the world."))
			{
				ItemData.Description = FText::FromString(CatalogEntry->Description);
			}
			ItemData.MaxStackSize = CatalogEntry->MaxStackSize;
		}

		UStaticMesh* TargetMesh = !ItemData.WorldMesh.IsNull() ? ItemData.WorldMesh.LoadSynchronous() : nullptr;
		if (!TargetMesh)
		{
			TargetMesh = LoadObject<UStaticMesh>(nullptr, CatalogEntry->MeshPath);
		}
		if (TargetMesh)
		{
			MeshComponent->SetStaticMesh(TargetMesh);
			ItemData.WorldMesh = TargetMesh;
		}

		if (CatalogEntry->bIsNote)
		{
			ItemPreset = ELostWithinItemPreset::PaperNote;
			bSimulatePhysics = false;
			ImpactNoiseRadius = 0.0f;
		}

		ApplyPickupMeshScale(TargetMesh, CatalogEntry->bIsFlashlight, CatalogEntry->bIsKey, CatalogEntry->bIsBattery, bForceApplyCatalogScale, CatalogEntry->bIsNote);
		RefreshFlashlightLightState();
		return;
	}

	// 2. Any non-catalog /Game/Item/* mesh assigned on MeshComponent or ItemData.WorldMesh becomes pickable with preserved identity
	UStaticMesh* ActiveMesh = !ItemData.WorldMesh.IsNull() ? ItemData.WorldMesh.LoadSynchronous() : MeshComponent->GetStaticMesh().Get();
	if (ActiveMesh)
	{
		if (MeshComponent->GetStaticMesh() != ActiveMesh)
		{
			MeshComponent->SetStaticMesh(ActiveMesh);
		}

		const FString ActivePath = ActiveMesh->GetPathName();
		if (ActivePath.StartsWith(TEXT("/Game/Item/")))
		{
			if (IsPlaceholderOrUnsetItemID(ItemData.ItemID))
			{
				ItemData.ItemID = FName(*ActiveMesh->GetName());
			}
			if (ItemData.DisplayName.IsEmpty() || ItemData.DisplayName.ToString() == TEXT("Placeholder Item"))
			{
				ItemData.DisplayName = FText::FromString(FormatDisplayNameFromAssetName(ActiveMesh->GetName()));
			}
			ItemData.WorldMesh = ActiveMesh;
		}

		const bool bCustomKeyMesh = ActivePath.StartsWith(TEXT("/Game/Item/Keys/"));
		const bool bCustomBatteryMesh = ActivePath.StartsWith(TEXT("/Game/Item/Battery/"));
		ApplyPickupMeshScale(ActiveMesh, /*bIsFlashlight=*/false, bCustomKeyMesh, bCustomBatteryMesh, bForceApplyCatalogScale);
		RefreshFlashlightLightState();
		return;
	}

	// 3. Fallback shape only when no mesh is assigned
	UStaticMesh* FallbackMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/LevelPrototyping/Meshes/SM_ChamferCube.SM_ChamferCube"));
	if (!FallbackMesh)
	{
		FallbackMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	}
	if (FallbackMesh)
	{
		MeshComponent->SetStaticMesh(FallbackMesh);
	}

	ApplyPickupMeshScale(FallbackMesh, /*bIsFlashlight=*/false, /*bIsKey=*/false, /*bIsBattery=*/false, /*bForceApply=*/false);
	RefreshFlashlightLightState();
}

void APickupItemActor::ApplyPickupMeshScale(const UStaticMesh* Mesh, bool bIsFlashlight, bool bIsKey, bool bIsBattery, bool bForceApply, bool bIsNote)
{
	if (!MeshComponent)
	{
		return;
	}

	if (ItemData.bHasCustomWorldMeshScale)
	{
		MeshComponent->SetRelativeScale3D(ItemData.WorldMeshScale);
		return;
	}

	const bool bHasUntouchedScale = MeshComponent->GetRelativeScale3D().Equals(FVector(DefaultFallbackMeshScale), 0.001f);
	if (!bForceApply && !((bIsFlashlight || bIsKey || bIsBattery || bIsNote) && bHasUntouchedScale))
	{
		return;
	}

	float TargetScale = DefaultFallbackMeshScale;
	if (bIsFlashlight)
	{
		TargetScale = FlashlightPickupScale;
	}
	else if (bIsBattery)
	{
		TargetScale = BatteryPickupScale;
	}
	else if (bIsKey || bIsNote)
	{
		const float MaxUnscaledDim = Mesh ? Mesh->GetBoundingBox().GetSize().GetMax() : 0.0f;
		const float TargetSize = bIsNote ? TargetNoteWorldSizeCm : TargetKeyWorldSizeCm;
		TargetScale = (MaxUnscaledDim > KINDA_SMALL_NUMBER) ? (TargetSize / MaxUnscaledDim) : DefaultFallbackMeshScale;
	}

	MeshComponent->SetRelativeScale3D(FVector(TargetScale));
}

void APickupItemActor::RefreshFlashlightLightState()
{
	if (CollisionSphere && MeshComponent)
	{
		if (const UStaticMesh* ActiveMesh = MeshComponent->GetStaticMesh())
		{
			CollisionSphere->SetRelativeLocation(ActiveMesh->GetBoundingBox().GetCenter());
		}
		else
		{
			CollisionSphere->SetRelativeLocation(FVector::ZeroVector);
		}
	}

	if (IsFlashlightItem() && ItemData.bIsFlashlightOn && !ItemData.CanOperateFlashlight())
	{
		ItemData.bIsFlashlightOn = false;
	}

	const FLostWithinFlashlightLightParams EffectiveParams = ComputeEffectiveFlashlightLightParams(
		FlashlightLighting,
		ItemData.GetCombinedBatteryPercent());
	ApplyFlashlightLightingParams(FlashlightSpotLight, FlashlightFillLight, EffectiveParams);

	const bool bShouldLightBeVisible = IsFlashlightOn();
	if (FlashlightSpotLight)
	{
		if (bShouldLightBeVisible)
		{
			FlashlightSpotLight->SetRelativeLocationAndRotation(
				ComputeDroppedFlashlightMuzzleLocalLocation(/*ExtraWorldForwardCm=*/0.0f),
				DroppedFlashlightBeamRotation);
		}
		FlashlightSpotLight->SetVisibility(bShouldLightBeVisible);
	}

	if (FlashlightFillLight)
	{
		if (bShouldLightBeVisible)
		{
			FlashlightFillLight->SetRelativeLocation(
				ComputeDroppedFlashlightMuzzleLocalLocation(/*ExtraWorldForwardCm=*/6.0f));
		}
		FlashlightFillLight->SetVisibility(bShouldLightBeVisible);
	}

	SetActorTickEnabled(bShouldLightBeVisible);
}

FVector APickupItemActor::ComputeDroppedFlashlightMuzzleLocalLocation(float ExtraWorldForwardCm) const
{
	if (!MeshComponent)
	{
		return FVector::ZeroVector;
	}

	const FVector BeamDirLocal = DroppedFlashlightBeamRotation.Vector().GetSafeNormal();
	const FVector AbsDir(FMath::Abs(BeamDirLocal.X), FMath::Abs(BeamDirLocal.Y), FMath::Abs(BeamDirLocal.Z));
	const FVector MeshScale = MeshComponent->GetRelativeScale3D().GetAbs();
	const float ScaleAlongBeam = FMath::Max(0.01f, FVector::DotProduct(AbsDir, MeshScale));
	const float TotalWorldClearanceCm = FMath::Max(0.0f, DroppedFlashlightMuzzleClearance + ExtraWorldForwardCm);

	if (const UStaticMesh* ActiveMesh = MeshComponent->GetStaticMesh())
	{
		const FBox LocalBounds = ActiveMesh->GetBoundingBox();
		const float SupportRadiusLocal = FVector::DotProduct(AbsDir, LocalBounds.GetExtent());
		return LocalBounds.GetCenter() + BeamDirLocal * (SupportRadiusLocal + (TotalWorldClearanceCm / ScaleAlongBeam));
	}

	return BeamDirLocal * (TotalWorldClearanceCm / ScaleAlongBeam);
}

void APickupItemActor::EnableItemPhysicsSimulation()
{
	if (CollisionSphere)
	{
		CollisionSphere->SetSimulatePhysics(false);
		CollisionSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		CollisionSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
		CollisionSphere->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	}

	if (!MeshComponent)
	{
		return;
	}

    MeshComponent->SetNotifyRigidBodyCollision(true);
    MeshComponent->OnComponentHit.RemoveDynamic(this, &APickupItemActor::OnMeshHit);
    MeshComponent->OnComponentHit.AddDynamic(this, &APickupItemActor::OnMeshHit);

	if (!bSimulatePhysics)
	{
		MeshComponent->SetSimulatePhysics(false);
		MeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		MeshComponent->SetCollisionResponseToAllChannels(ECR_Ignore);
		MeshComponent->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
		return;
	}

	UStaticMesh* ActiveMesh = MeshComponent->GetStaticMesh();
	UBodySetup* BodySetup = ActiveMesh ? ActiveMesh->GetBodySetup() : nullptr;
	const bool bHasSimpleShapes = BodySetup && (BodySetup->AggGeom.GetElementCount() > 0);
	if (!bHasSimpleShapes)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("APickupItemActor '%s' mesh '%s' has no simple collision primitives; physics simulation disabled."),
			*GetNameSafe(this),
			*GetNameSafe(ActiveMesh));
		MeshComponent->SetSimulatePhysics(false);
		MeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		MeshComponent->SetCollisionResponseToAllChannels(ECR_Ignore);
		MeshComponent->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
		return;
	}

	MeshComponent->SetMobility(EComponentMobility::Movable);
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	MeshComponent->SetCollisionObjectType(ECC_PhysicsBody);
	MeshComponent->SetCollisionResponseToAllChannels(ECR_Block);
	MeshComponent->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	MeshComponent->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	MeshComponent->SetLinearDamping(0.35f);
	MeshComponent->SetAngularDamping(2.5f);
	MeshComponent->SetEnableGravity(true);
	MeshComponent->SetSimulatePhysics(true);
	MeshComponent->WakeAllRigidBodies();
}

void APickupItemActor::EnsureDefaultDropSounds()
{
	if (DropSounds.IsEmpty())
	{
		static constexpr const TCHAR* DefaultDropSoundPaths[] = {
			TEXT("/Game/Item/Popout_Drop/Drop_01.Drop_01"),
			TEXT("/Game/Item/Popout_Drop/Drop_02.Drop_02"),
			TEXT("/Game/Item/Popout_Drop/Drop_03.Drop_03")
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
			FMath::FRandRange(0.9f, 1.1f));
	}
}

void APickupItemActor::ReportImpactNoise(const FVector& Location)
{
	if (ImpactNoiseRadius > 0.0f && !IsNoteItem())
	{
		ULostWithinNoiseSubsystem::ReportNoise(this, Location, ImpactNoiseRadius, ELostWithinNoiseKind::ItemImpact, this);
	}
}
