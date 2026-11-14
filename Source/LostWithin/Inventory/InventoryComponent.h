// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InventoryComponent.generated.h"

class APickupItemActor;
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

	/** Current quantity in this stack */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory", meta = (ClampMin = "1"))
	int32 Quantity = 1;

	/** Maximum units allowed per slot */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory", meta = (ClampMin = "1"))
	int32 MaxStackSize = 99;

	/** Optional UI icon texture */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory")
	TObjectPtr<UTexture2D> Icon = nullptr;
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

protected:

	/** Stored item stacks */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Inventory")
	TArray<FInventoryItem> Items;
};

