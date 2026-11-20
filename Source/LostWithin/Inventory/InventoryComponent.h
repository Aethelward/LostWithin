// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InventoryComponent.generated.h"

class APickupItemActor;
class UStaticMesh;
class UTexture2D;

/**
 *  Represents a single stack of an item stored inside UInventoryComponent.
 */
USTRUCT(BlueprintType)
struct LOSTWITHIN_API FInventoryItem
{
	GENERATED_BODY()

	/** Unique identifier used for stacking and lookups */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory")
	FName ItemID = NAME_None;

	/** Human-readable name displayed in the inventory UI and pickup prompts */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory")
	FText DisplayName;

	/** Short item description displayed in the inventory UI */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory")
	FText Description;

	/** Stable story identity; independent of the unique pickup/stack identifier. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Story")
	FName StoryNoteID = NAME_None;

	/** Explicit clue metadata keeps numbers in the lore separate from the actual exit clue. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Story")
	int32 ClueDigitIndex = INDEX_NONE;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Story")
	int32 ClueDigit = INDEX_NONE;

	/** Current quantity in this stack */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory", meta = (ClampMin = "1"))
	int32 Quantity = 1;

	/** Maximum units allowed per slot */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory", meta = (ClampMin = "1"))
	int32 MaxStackSize = 99;

	/** Optional UI icon texture */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory")
	TObjectPtr<UTexture2D> Icon = nullptr;

	/** Static mesh represented by this item when placed or dropped in the world */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory")
	TSoftObjectPtr<UStaticMesh> WorldMesh;

	/** If true, WorldMeshScale is restored when dropping this item back into the world */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory")
	bool bHasCustomWorldMeshScale = false;

	/** World scale preserved across pickup -> inventory -> drop */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory", meta = (EditCondition = "bHasCustomWorldMeshScale"))
	FVector WorldMeshScale = FVector::OneVector;

	/** Per-instance powered state for light-emitting items (preserved across hold, drop, and pickup) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory")
	bool bIsFlashlightOn = false;

	/** Sentinel stored in BatterySlot1Percent / BatterySlot2Percent when that battery bay is empty */
	static constexpr float EmptyBatterySlotValue = -1.0f;

	/** Remaining charge (%) when this item represents an individual Battery (0..100) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Battery", meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float BatteryChargePercent = 100.0f;

	/** Installed battery bay 1 charge (%) for a Flashlight (-1 = empty bay, 0..100 = installed battery) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Battery", meta = (ClampMin = "-1.0", ClampMax = "100.0"))
	float BatterySlot1Percent = 100.0f;

	/** Installed battery bay 2 charge (%) for a Flashlight (-1 = empty bay, 0..100 = installed battery) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Battery", meta = (ClampMin = "-1.0", ClampMax = "100.0"))
	float BatterySlot2Percent = 100.0f;

	/** Returns true when BayIndex (0 or 1) holds a physical battery */
	bool HasBatteryInSlot(int32 BayIndex) const;

	/** Returns charge (%) in BayIndex (0 or 1), or EmptyBatterySlotValue (-1) if empty */
	float GetBatterySlotPercent(int32 BayIndex) const;

	/** Sets charge (%) in BayIndex (0 or 1); values < 0 mark the bay empty */
	void SetBatterySlotPercent(int32 BayIndex, float InPercent);

	/** Returns the number of installed physical batteries (0, 1, or 2) */
	int32 GetInstalledBatteryCount() const;

	/** Returns the combined flashlight percentage ((Slot1 + Slot2) / 2, treating empty bays as 0) */
	float GetCombinedBatteryPercent() const;

	/** Returns true when 2 batteries are installed and combined charge is > 0% */
	bool CanOperateFlashlight() const;

	/** Drains combined flashlight charge over DeltaTime; returns false if empty or missing cells */
	bool ConsumeFlashlightCharge(float DeltaTime, float FullDurationSeconds);

	/** Picks the preferred bay for loading/swapping (empty bay first, otherwise lowest-charged bay) */
	int32 GetPreferredBatteryReplaceBay() const;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnInventoryUpdated);

/**
 *  Reusable slot-based inventory component supporting stacking, addition, removal, and UI change notifications.
 */
UCLASS(ClassGroup=(Inventory), meta=(BlueprintSpawnableComponent))
class LOSTWITHIN_API UInventoryComponent : public UActorComponent
{
	GENERATED_BODY()

public:

	UInventoryComponent();

	/** Maximum number of distinct item slots */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory", meta = (ClampMin = "1"))
	int32 MaxSlots = 16;

	/** Pickup actor class spawned when dropping an item back into the world */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory")
	TSubclassOf<APickupItemActor> WorldPickupClass;

	/** Broadcast whenever items are added, stacked, or removed */
	UPROPERTY(BlueprintAssignable, Category = "Inventory")
	FOnInventoryUpdated OnInventoryUpdated;

	/** Attempts to add an item stack to the inventory; returns true if any quantity was added */
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	bool AddItem(const FInventoryItem& ItemToAdd, int32& OutRemainingQuantity);

	/** Removes up to Quantity units of ItemID; returns true if the full requested amount was removed */
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	bool RemoveItemByID(FName ItemID, int32 Quantity = 1);

	/** Removes up to Quantity units (or entire stack if Quantity <= 0) from SlotIndex; returns true if anything was removed */
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	bool RemoveItemAtSlot(int32 SlotIndex, int32 Quantity, FInventoryItem& OutRemovedItem);

	/** Removes up to Quantity units (or entire stack if Quantity <= 0) from SlotIndex and spawns a world pickup actor in front of Owner */
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	bool DropItemAtSlot(int32 SlotIndex, int32 Quantity = -1);

	/** Returns true if the inventory contains at least MinQuantity units of ItemID */
	UFUNCTION(BlueprintPure, Category = "Inventory")
	bool HasItem(FName ItemID, int32 MinQuantity = 1) const;

	/** Returns total quantity across all stacks matching ItemID */
	UFUNCTION(BlueprintPure, Category = "Inventory")
	int32 GetItemCount(FName ItemID) const;

	/** Returns all current item stacks */
	UFUNCTION(BlueprintPure, Category = "Inventory")
	const TArray<FInventoryItem>& GetItems() const { return Items; }

	/** Returns maximum slot capacity */
	UFUNCTION(BlueprintPure, Category = "Inventory")
	int32 GetMaxSlots() const { return MaxSlots; }

	/** Returns the powered ON/OFF state of the first stored tactical flashlight slot (false if none) */
	UFUNCTION(BlueprintPure, Category = "Inventory")
	bool GetFlashlightOnState() const;

	/** Updates the stored powered ON/OFF state on the first tactical flashlight slot without triggering a full inventory refresh */
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	void SetFlashlightOnState(bool bInIsOn);

	/** Returns true if the stored tactical flashlight has 2 batteries and >0% combined charge */
	UFUNCTION(BlueprintPure, Category = "Inventory|Battery")
	bool CanOperateFlashlight() const;

	/** Outputs installed battery count (0..2), combined %, and individual bay charges (-1 = empty) */
	UFUNCTION(BlueprintPure, Category = "Inventory|Battery")
	void GetFlashlightBatteryState(
		int32& OutInstalledCount,
		float& OutCombinedPercent,
		float& OutSlot1Percent,
		float& OutSlot2Percent) const;

	/** Returns index of the highest-charged usable Battery slot (>0%), or INDEX_NONE if none */
	UFUNCTION(BlueprintPure, Category = "Inventory|Battery")
	int32 GetBestUsableBatterySlotIndex() const;

	/** Returns true if SpecificBatterySlotIndex (or best available battery when -1) can load/upgrade the flashlight */
	UFUNCTION(BlueprintPure, Category = "Inventory|Battery")
	bool CanReloadFlashlight(int32 SpecificBatterySlotIndex = -1) const;

	/** Loads or swaps the battery at BatterySlotIndex into the stored flashlight */
	UFUNCTION(BlueprintCallable, Category = "Inventory|Battery")
	bool ReplaceFlashlightBatteryWithSlot(int32 BatterySlotIndex);

	/** Loads or swaps the highest-charged usable battery in inventory into the stored flashlight */
	UFUNCTION(BlueprintCallable, Category = "Inventory|Battery")
	bool ReloadBestBatteryIntoFlashlight();

	/** Drains charge on the stored flashlight over DeltaTime; sets OutTurnedOff=true if power hit 0% */
	bool ConsumeFlashlightBattery(float DeltaTime, float FullDurationSeconds, bool& OutTurnedOff);

protected:

	/** Stored item stacks */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Inventory")
	TArray<FInventoryItem> Items;

private:

	FInventoryItem* FindFirstItemSlotByID(FName ItemID);
	const FInventoryItem* FindFirstItemSlotByID(FName ItemID) const;
	int32 FindFirstItemIndexByID(FName ItemID) const;
	/** Spawns the dropped pickup; bCarriesScent gives it the player's scent for the BoneDog. */
	void SpawnWorldPickupForItem(const FInventoryItem& DroppedItem, bool bCarriesScent = false) const;

	/** Seconds the player has held ItemID since it was first picked up in this run (0 when never held). */
	float GetSecondsHeld(FName ItemID) const;

	/** World time when each item id was first added (kept after it is dropped). */
	TMap<FName, double> FirstHeldTime;
};

