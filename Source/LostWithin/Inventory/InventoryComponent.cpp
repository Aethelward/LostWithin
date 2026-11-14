// Copyright Epic Games, Inc. All Rights Reserved.

#include "Inventory/InventoryComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "Interaction/PickupItemActor.h"

UInventoryComponent::UInventoryComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	WorldPickupClass = APickupItemActor::StaticClass();
}

bool UInventoryComponent::AddItem(const FInventoryItem& ItemToAdd, int32& OutRemainingQuantity)
{
	OutRemainingQuantity = FMath::Max(0, ItemToAdd.Quantity);
	if (ItemToAdd.ItemID.IsNone() || OutRemainingQuantity <= 0)
	{
		return false;
	}

	const int32 EffectiveMaxStack = FMath::Max(1, ItemToAdd.MaxStackSize);
	const int32 InitialRemaining = OutRemainingQuantity;

	// First fill existing stacks of the same ItemID
	for (FInventoryItem& ExistingItem : Items)
	{
		if (ExistingItem.ItemID == ItemToAdd.ItemID && ExistingItem.Quantity < ExistingItem.MaxStackSize)
		{
			const int32 SpaceAvailable = ExistingItem.MaxStackSize - ExistingItem.Quantity;
			const int32 AmountToMove = FMath::Min(OutRemainingQuantity, SpaceAvailable);
			ExistingItem.Quantity += AmountToMove;
			OutRemainingQuantity -= AmountToMove;

			if (OutRemainingQuantity <= 0)
			{
				break;
			}
		}
	}

	// Create new slots for any remaining quantity
	while (OutRemainingQuantity > 0 && Items.Num() < MaxSlots)
	{
		FInventoryItem NewSlot = ItemToAdd;
		NewSlot.MaxStackSize = EffectiveMaxStack;
		NewSlot.Quantity = FMath::Min(OutRemainingQuantity, EffectiveMaxStack);
		if (NewSlot.DisplayName.IsEmpty())
		{
			NewSlot.DisplayName = FText::FromName(NewSlot.ItemID);
		}

		Items.Add(NewSlot);
		OutRemainingQuantity -= NewSlot.Quantity;
	}

	const bool bAddedAny = OutRemainingQuantity < InitialRemaining;
	if (bAddedAny)
	{
		OnInventoryUpdated.Broadcast();
	}

	return bAddedAny;
}

bool UInventoryComponent::RemoveItemByID(FName ItemID, int32 Quantity)
{
	if (ItemID.IsNone() || Quantity <= 0 || GetItemCount(ItemID) < Quantity)
	{
		return false;
	}

	int32 RemainingToRemove = Quantity;
	for (int32 Index = Items.Num() - 1; Index >= 0 && RemainingToRemove > 0; --Index)
	{
		if (Items[Index].ItemID == ItemID)
		{
			const int32 AmountRemoved = FMath::Min(Items[Index].Quantity, RemainingToRemove);
			Items[Index].Quantity -= AmountRemoved;
			RemainingToRemove -= AmountRemoved;

			if (Items[Index].Quantity <= 0)
			{
				Items.RemoveAt(Index);
			}
		}
	}

	OnInventoryUpdated.Broadcast();
	return true;
}

bool UInventoryComponent::RemoveItemAtSlot(int32 SlotIndex, int32 Quantity, FInventoryItem& OutRemovedItem)
{
	if (!Items.IsValidIndex(SlotIndex))
	{
		return false;
	}

	FInventoryItem& SlotItem = Items[SlotIndex];
	const int32 RequestedQuantity = (Quantity <= 0) ? SlotItem.Quantity : FMath::Min(Quantity, SlotItem.Quantity);
	if (RequestedQuantity <= 0)
	{
		return false;
	}

	OutRemovedItem = SlotItem;
	OutRemovedItem.Quantity = RequestedQuantity;

	SlotItem.Quantity -= RequestedQuantity;
	if (SlotItem.Quantity <= 0)
	{
		Items.RemoveAt(SlotIndex);
	}

	OnInventoryUpdated.Broadcast();
	return true;
}

bool UInventoryComponent::DropItemAtSlot(int32 SlotIndex, int32 Quantity)
{
	FInventoryItem DroppedItem;
	if (!RemoveItemAtSlot(SlotIndex, Quantity, DroppedItem))
	{
		return false;
	}

	AActor* Owner = GetOwner();
	UWorld* World = GetWorld();
	if (!Owner || !World)
	{
		return true;
	}

	FVector ForwardDir = FVector::ZeroVector;
	if (const APawn* OwnerPawn = Cast<APawn>(Owner))
	{
		const FRotator ViewYaw(0.0f, OwnerPawn->GetControlRotation().Yaw, 0.0f);
		ForwardDir = ViewYaw.Vector().GetSafeNormal2D();
	}
	if (ForwardDir.IsNearlyZero())
	{
		ForwardDir = Owner->GetActorForwardVector().GetSafeNormal2D();
	}
	if (ForwardDir.IsNearlyZero())
	{
		ForwardDir = FVector::ForwardVector;
	}

	const FVector BaseOrigin = Owner->GetActorLocation() + FVector(0.0f, 0.0f, 40.0f);
	const FVector DropStart = BaseOrigin + ForwardDir * 36.0f;
	FVector DropTarget = Owner->GetActorLocation() + ForwardDir * 140.0f;

	FCollisionQueryParams WallQueryParams(SCENE_QUERY_STAT(InventoryDropTrace), false, Owner);
	FHitResult WallHit;
	const FVector WallTraceEnd = BaseOrigin + ForwardDir * 150.0f;
	if (World->LineTraceSingleByChannel(WallHit, BaseOrigin, WallTraceEnd, ECC_Visibility, WallQueryParams))
	{
		const float SafeDistance = FMath::Max(45.0f, WallHit.Distance - 35.0f);
		DropTarget = Owner->GetActorLocation() + ForwardDir * SafeDistance;
	}

	UClass* SpawnClass = WorldPickupClass ? WorldPickupClass.Get() : APickupItemActor::StaticClass();
	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = Owner;
	SpawnParams.Instigator = Cast<APawn>(Owner);
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	if (APickupItemActor* DroppedPickup = World->SpawnActor<APickupItemActor>(SpawnClass, DropStart, FRotator::ZeroRotator, SpawnParams))
	{
		DroppedPickup->ItemData = DroppedItem;
		DroppedPickup->StartDropArc(DropStart, DropTarget, 45.0f, 0.40f);
	}

	return true;
}

bool UInventoryComponent::HasItem(FName ItemID, int32 MinQuantity) const
{
	return GetItemCount(ItemID) >= FMath::Max(1, MinQuantity);
}

int32 UInventoryComponent::GetItemCount(FName ItemID) const
{
	if (ItemID.IsNone())
	{
		return 0;
	}

	int32 TotalCount = 0;
	for (const FInventoryItem& Item : Items)
	{
		if (Item.ItemID == ItemID)
		{
			TotalCount += Item.Quantity;
		}
	}
	return TotalCount;
}

