// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/InteractableInterface.h"
#include "Inventory/InventoryComponent.h"
#include "PickupItemActor.generated.h"

class UPointLightComponent;
class USphereComponent;
class USoundBase;
class USpotLightComponent;
class UStaticMesh;
class UStaticMeshComponent;

/**
 *  Shared beam/fill lighting parameters for both held and dropped tactical flashlights.
 */
USTRUCT(BlueprintType)
struct LOSTWITHIN_API FLostWithinFlashlightLightParams
{
	GENERATED_BODY()

	/** Flashlight spotlight intensity in candelas */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flashlight|Lighting", meta = (ClampMin = "0.0"))
	float SpotIntensity = 25.0f;

	/** Close-range fill point light intensity in candelas */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flashlight|Lighting", meta = (ClampMin = "0.0"))
	float FillIntensity = 0.5f;

	/** Maximum range of the flashlight spotlight beam */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flashlight|Lighting", meta = (ClampMin = "100.0", Units = "cm"))
	float AttenuationRadius = 1500.0f;

	/** Inner beam cone angle in degrees */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flashlight|Lighting", meta = (ClampMin = "1.0", ClampMax = "80.0", Units = "deg"))
	float InnerConeAngle = 8.0f;

	/** Outer beam cone angle in degrees */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flashlight|Lighting", meta = (ClampMin = "2.0", ClampMax = "89.0", Units = "deg"))
	float OuterConeAngle = 18.0f;

	/** Beam tint color */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flashlight|Lighting")
	FLinearColor LightColor = FLinearColor(1.0f, 0.95f, 0.86f, 1.0f);
};

/**
 *  Built-in catalog presets mapping assets under /Game/Item/* to inventory item definitions.
 */
UENUM(BlueprintType)
enum class ELostWithinItemPreset : uint8
{
	/** Uses custom ItemData / assigned /Game/Item/* mesh directly */
	Custom UMETA(DisplayName = "Custom (Use Mesh / ItemData)"),
	/** Tactical flashlight (/Game/Item/Flashlight/StaticMeshes/defaultMaterial) */
	Flashlight UMETA(DisplayName = "Flashlight (/Game/Item/Flashlight)"),
	/** AA dry-cell battery (/Game/Item/Battery/StaticMeshes/dry_cell_-_aa_battery) */
	Battery UMETA(DisplayName = "Battery (/Game/Item/Battery)"),
	/** Fairy Tale Kitty Key (/Game/Item/Keys/fairy_tale_kitty_key/StaticMeshes/fairy_tale_kitty_key) */
	FairyTaleKittyKey UMETA(DisplayName = "Fairy Tale Kitty Key (/Game/Item/Keys/fairy_tale_kitty_key)"),
	/** Standard Key (/Game/Item/Keys/key/StaticMeshes/key) */
	Key UMETA(DisplayName = "Key (/Game/Item/Keys/key)"),
	/** Old Key (/Game/Item/Keys/old_key/StaticMeshes/old_key) */
	OldKey UMETA(DisplayName = "Old Key (/Game/Item/Keys/old_key)"),
	/** Royal Key (/Game/Item/Keys/royal_key_2/StaticMeshes/royal_key_2) */
	RoyalKey UMETA(DisplayName = "Royal Key (/Game/Item/Keys/royal_key_2)"),
	/** Paper note with a readable phrase (/Game/Item/paper/StaticMeshes/paper); never stacks, lies flat, no physics */
	PaperNote UMETA(DisplayName = "Paper Note (/Game/Item/paper)")
};

/**
 *  World pickup actor representing an inventory item under /Game/Item/*.
 *  Simulates Chaos physics with no spin/bob animation for both placed and dropped items.
 */
UCLASS()
class LOSTWITHIN_API APickupItemActor : public AActor, public IInteractableInterface
{
	GENERATED_BODY()

	/** Physics-simulating visual mesh and root component for the world pickup */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> MeshComponent;

	/** Query-only sphere attached to MeshComponent to assist interaction traces against small pickups */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USphereComponent> CollisionSphere;

	/** Spotlight attached to MeshComponent so dropped ON flashlights cast light along their barrel */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USpotLightComponent> FlashlightSpotLight;

	/** Close-range fill light attached near the dropped flashlight lens */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UPointLightComponent> FlashlightFillLight;

public:

	APickupItemActor();

	/** Canonical ItemIDs for built-in /Game/Item/* pickups */
	static const FName PlaceholderItemID;
	static const FName FlashlightItemID;
	static const FName BatteryItemID;
	static const FName FairyTaleKittyKeyItemID;
	static const FName KeyItemID;
	static const FName OldKeyItemID;
	static const FName RoyalKeyItemID;
	/** Base ItemID of paper notes; each note gets a unique "PaperNote_<id>" so notes never stack */
	static const FName PaperNoteItemID;

	/** Asset paths for built-in /Game/Item/* static meshes */
	static const TCHAR* FlashlightMeshAssetPath;
	static const TCHAR* FlashlightAltMeshAssetPath;
	static const TCHAR* BatteryMeshAssetPath;
	static const TCHAR* FairyTaleKittyKeyMeshAssetPath;
	static const TCHAR* KeyMeshAssetPath;
	static const TCHAR* OldKeyMeshAssetPath;
	static const TCHAR* RoyalKeyMeshAssetPath;
	static const TCHAR* PaperNoteMeshAssetPath;

	/** True for the base or any unique paper note ItemID */
	static bool IsNoteItemID(FName ItemID);

	/** Returns a new unique paper note ItemID ("PaperNote_<guid>") */
	static FName MakeUniqueNoteItemID();

	/** True for Key, Old Key, Royal Key and Fairy Tale Kitty Key */
	static bool IsKeyItemID(FName ItemID);

	/** Display name of a built-in key ItemID ("Old Key"); the ItemID itself for anything else */
	static FText GetItemDisplayName(FName ItemID);

	/** Default uniform pickup scale applied to AA batteries */
	static constexpr float DefaultBatteryPickupScale = 0.025f;

	/** Quick-select preset for any built-in /Game/Item/* pickup */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pickup")
	ELostWithinItemPreset ItemPreset = ELostWithinItemPreset::Custom;

	/** Inventory item data granted when picked up via [LMB] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pickup")
	FInventoryItem ItemData;

	/** World mesh scale applied when ItemData / ItemPreset represents the tactical flashlight */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pickup|Flashlight", meta = (ClampMin = "0.01"))
	float FlashlightPickupScale = 0.15f;

	/** World mesh scale applied when ItemData / ItemPreset represents a Battery */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pickup|Battery", meta = (ClampMin = "0.001"))
	float BatteryPickupScale = DefaultBatteryPickupScale;

	/** Seconds of continuous ON time per 100% combined battery charge when dropped lit in the world */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pickup|Flashlight", meta = (ClampMin = "1.0", Units = "s"))
	float FlashlightBatteryDurationSeconds = 180.0f;

	/** Base beam & fill lighting configuration (defined at 25% combined battery baseline; scales to 3x at 100%) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pickup|Flashlight")
	FLostWithinFlashlightLightParams FlashlightLighting;

	/** Local rotation relative to MeshComponent orienting the spotlight's +X beam axis out of the flashlight lens */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pickup|Flashlight")
	FRotator DroppedFlashlightBeamRotation = FRotator::ZeroRotator;

	/** World-space clearance ahead of the flashlight front lens face so shadow-casting never blocks the beam */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pickup|Flashlight", meta = (ClampMin = "0.0", Units = "cm"))
	float DroppedFlashlightMuzzleClearance = 2.0f;

	/** Target maximum world dimension (cm) used to normalize key pickups cleanly regardless of import scale */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pickup|Keys", meta = (ClampMin = "1.0", Units = "cm"))
	float TargetKeyWorldSizeCm = 18.0f;

	/** Target maximum world dimension (cm) of a paper note */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pickup|Note", meta = (ClampMin = "1.0", Units = "cm"))
	float TargetNoteWorldSizeCm = 24.0f;

	/** Pitch and roll that lay the paper mesh flat (its sheet faces +Y in the asset); yaw is randomized */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pickup|Note")
	FRotator NoteFlatRotation = FRotator(0.0f, 0.0f, 90.0f);

	/** Radius of the noise made when this item hits the floor hard enough to play its impact sound (0 = silent) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pickup|Noise", meta = (ClampMin = "0.0", Units = "cm"))
	float ImpactNoiseRadius = 3000.0f;

	/** If true, simulates physics on BeginPlay (for placed items) as well as when dropped */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pickup|Physics")
	bool bSimulatePhysics = true;

	/** Sound variations played immediately when this item is dropped */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pickup|Audio")
	TArray<TObjectPtr<USoundBase>> DropSounds;

	/** Volume multiplier for item drop SFX */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pickup|Audio", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float DropSoundVolume = 0.85f;

	/** Configures this pickup from one of the built-in /Game/Item/* presets */
	UFUNCTION(BlueprintCallable, Category = "Pickup")
	void ConfigureFromPreset(ELostWithinItemPreset Preset, bool bForceApplyPresetScale = true);

	UFUNCTION()
	void OnMeshHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);


	/** Configures this pickup as the tactical flashlight using /Game/Item/Flashlight/StaticMeshes/defaultMaterial at scale 0.15 */
	UFUNCTION(BlueprintCallable, Category = "Pickup")
	void ConfigureAsFlashlight();

	/** Configures this pickup as an AA Battery using /Game/Item/Battery/StaticMeshes/dry_cell_-_aa_battery at scale 0.025 */
	UFUNCTION(BlueprintCallable, Category = "Pickup")
	void ConfigureAsBattery(float ChargePercent = 100.0f);

	/** Configures this pickup as the Fairy Tale Kitty Key using /Game/Item/Keys/fairy_tale_kitty_key/StaticMeshes/fairy_tale_kitty_key */
	UFUNCTION(BlueprintCallable, Category = "Pickup")
	void ConfigureAsFairyTaleKittyKey();

	/** Configures this pickup as a paper note showing NoteText (unique ItemID, lies flat, no physics) */
	UFUNCTION(BlueprintCallable, Category = "Pickup|Note")
	void ConfigureAsNote(const FText& NoteText);

	/** Returns true when this pickup currently represents a paper note */
	UFUNCTION(BlueprintPure, Category = "Pickup|Note")
	bool IsNoteItem() const;

	/** Lays a note flat on the floor below Around with a random yaw */
	UFUNCTION(BlueprintCallable, Category = "Pickup|Note")
	void PlaceFlatOnFloor(const FVector& Around);

	/** Assigns ItemData and refreshes visual mesh/scale representation (e.g. when dropped from inventory) */
	UFUNCTION(BlueprintCallable, Category = "Pickup")
	void ApplyItemData(const FInventoryItem& InItemData);

	/** Launches this pickup from StartLocation under Chaos physics with InitialVelocity and plays drop SFX */
	UFUNCTION(BlueprintCallable, Category = "Pickup|Physics")
	void DropWithPhysics(const FVector& StartLocation, const FVector& InitialVelocity, const TArray<AActor*>& ActorsToIgnore);

	/** Returns true when MeshComponent is actively simulating physics */
	UFUNCTION(BlueprintPure, Category = "Pickup|Physics")
	bool IsSimulatingItemPhysics() const;

	/** Returns true when this pickup currently represents a tactical flashlight */
	UFUNCTION(BlueprintPure, Category = "Pickup|Flashlight")
	bool IsFlashlightItem() const;

	/** Returns true when this pickup is a flashlight whose world light is currently ON */
	UFUNCTION(BlueprintPure, Category = "Pickup|Flashlight")
	bool IsFlashlightOn() const { return IsFlashlightItem() && ItemData.bIsFlashlightOn && ItemData.CanOperateFlashlight(); }

	/** Sets the dropped/placed flashlight ON/OFF power state and updates its world light components */
	UFUNCTION(BlueprintCallable, Category = "Pickup|Flashlight")
	void SetFlashlightOn(bool bInIsOn);

	/** Constructs a canonical Battery inventory item at ChargePercent (0..100) with scale 0.025 */
	static FInventoryItem MakeBatteryInventoryItem(float ChargePercent = 100.0f);

	/** Computes effective Brightness & Cone params from BaseParams (25% baseline) and CombinedBatteryPercent (0..100) */
	static FLostWithinFlashlightLightParams ComputeEffectiveFlashlightLightParams(
		const FLostWithinFlashlightLightParams& BaseParams,
		float CombinedBatteryPercent);

	/** Applies shared flashlight beam and fill lighting parameters to any SpotLight / FillLight pair */
	static void ApplyFlashlightLightingParams(
		USpotLightComponent* SpotLight,
		UPointLightComponent* FillLight,
		const FLostWithinFlashlightLightParams& Params);

	//~ Begin IInteractableInterface
	virtual void OnInteract_Implementation(AActor* Interactor) override;
	virtual FText GetInteractionPrompt_Implementation() const override;
	virtual bool CanInteract_Implementation(AActor* Interactor) const override;
	//~ End IInteractableInterface

protected:

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;

public:

	virtual void Tick(float DeltaTime) override;

protected:

#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:

	/** World time of this item's last impact sound. Kept per instance so each Play session starts fresh. */
	double LastImpactSoundTime = -1.0e9;

	/** Minimum seconds between two impact sounds of the same item (bounces). */
	static constexpr double ImpactSoundCooldownSeconds = 0.5;

	/** Synchronizes ItemPreset, ItemData, and MeshComponent mesh/scale cleanly */
	void RefreshItemSetup(bool bForceApplyCatalogScale);

	/** Applies custom or normalized world scale for a resolved pickup mesh */
	void ApplyPickupMeshScale(const UStaticMesh* Mesh, bool bIsFlashlight, bool bIsKey, bool bIsBattery, bool bForceApply, bool bIsNote = false);

	/** Reports the impact noise to hearing monsters */
	void ReportImpactNoise(const FVector& Location);

	/** Positions, configures, and shows/hides dropped flashlight lighting matching IsFlashlightOn() */
	void RefreshFlashlightLightState();

	/** Computes local offset ahead of MeshComponent bounds along DroppedFlashlightBeamRotation */
	FVector ComputeDroppedFlashlightMuzzleLocalLocation(float ExtraWorldForwardCm = 0.0f) const;

	/** Enables Chaos physics and gravity on MeshComponent without any artificial spin or bob animation */
	void EnableItemPhysicsSimulation();

	/** Populates DropSounds with default item drop SFX and clears looping flags */
	void EnsureDefaultDropSounds();

	/** Plays a random drop sound at the pickup's current world location */
	void PlayRandomDropSound();

	int32 LastDropSoundIndex = INDEX_NONE;
};
