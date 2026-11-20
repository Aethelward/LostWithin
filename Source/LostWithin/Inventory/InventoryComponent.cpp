// Copyright Epic Games, Inc. All Rights Reserved.

#include "Inventory/InventoryComponent.h"

#include "Gameplay/LostWithinScentSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "Interaction/PickupItemActor.h"

bool FInventoryItem::HasBatteryInSlot(int32 BayIndex) const
{
	return GetBatterySlotPercent(BayIndex) >= 0.0f;
}

float FInventoryItem::GetBatterySlotPercent(int32 BayIndex) const
{
	if (BayIndex == 0)
	{
		return BatterySlot1Percent;
	}
	if (BayIndex == 1)
	{
		return BatterySlot2Percent;
	}
	return EmptyBatterySlotValue;
}

void FInventoryItem::SetBatterySlotPercent(int32 BayIndex, float InPercent)
{
	const float Normalized = (InPercent < 0.0f) ? EmptyBatterySlotValue : FMath::Clamp(InPercent, 0.0f, 100.0f);
	if (BayIndex == 0)
	{
		BatterySlot1Percent = Normalized;
	}
	else if (BayIndex == 1)
	{
		BatterySlot2Percent = Normalized;
	}
}

int32 FInventoryItem::GetInstalledBatteryCount() const
{
	return (BatterySlot1Percent >= 0.0f ? 1 : 0) + (BatterySlot2Percent >= 0.0f ? 1 : 0);
}

float FInventoryItem::GetCombinedBatteryPercent() const
{
	const float ValidSlot1 = FMath::Max(0.0f, BatterySlot1Percent);
	const float ValidSlot2 = FMath::Max(0.0f, BatterySlot2Percent);
	return FMath::Clamp((ValidSlot1 + ValidSlot2) * 0.5f, 0.0f, 100.0f);
}

bool FInventoryItem::CanOperateFlashlight() const
{
	return GetInstalledBatteryCount() == 2 && GetCombinedBatteryPercent() > KINDA_SMALL_NUMBER;
}

bool FInventoryItem::ConsumeFlashlightCharge(float DeltaTime, float FullDurationSeconds)
{
	if (!CanOperateFlashlight())
	{
		bIsFlashlightOn = false;
		return false;
	}

	if (DeltaTime <= 0.0f || FullDurationSeconds <= KINDA_SMALL_NUMBER)
	{
		return true;
	}

	// 100% combined charge = 200 total cell-percentage points across both bays.
	const float PointsToDrain = (200.0f / FullDurationSeconds) * DeltaTime;
	const bool bSlot1Alive = BatterySlot1Percent > KINDA_SMALL_NUMBER;
	const bool bSlot2Alive = BatterySlot2Percent > KINDA_SMALL_NUMBER;

	if (bSlot1Alive && bSlot2Alive)
	{
		const float HalfDrain = PointsToDrain * 0.5f;
		const float Drain1 = FMath::Min(BatterySlot1Percent, HalfDrain);
		const float Drain2 = FMath::Min(BatterySlot2Percent, HalfDrain);
		BatterySlot1Percent -= Drain1;
		BatterySlot2Percent -= Drain2;

		const float Remainder = PointsToDrain - Drain1 - Drain2;
		if (Remainder > KINDA_SMALL_NUMBER)
		{
			if (BatterySlot1Percent > KINDA_SMALL_NUMBER)
			{
				BatterySlot1Percent = FMath::Max(0.0f, BatterySlot1Percent - Remainder);
			}
			else if (BatterySlot2Percent > KINDA_SMALL_NUMBER)
			{
				BatterySlot2Percent = FMath::Max(0.0f, BatterySlot2Percent - Remainder);
			}
		}
	}
	else if (bSlot1Alive)
	{
		BatterySlot1Percent = FMath::Max(0.0f, BatterySlot1Percent - PointsToDrain);
	}
	else if (bSlot2Alive)
	{
		BatterySlot2Percent = FMath::Max(0.0f, BatterySlot2Percent - PointsToDrain);
	}

	if (BatterySlot1Percent >= 0.0f && BatterySlot1Percent <= KINDA_SMALL_NUMBER)
	{
		BatterySlot1Percent = 0.0f;
	}
	if (BatterySlot2Percent >= 0.0f && BatterySlot2Percent <= KINDA_SMALL_NUMBER)
	{
		BatterySlot2Percent = 0.0f;
	}

	const bool bStillUsable = CanOperateFlashlight();
	if (!bStillUsable)
	{
		bIsFlashlightOn = false;
	}
	return bStillUsable;
}

int32 FInventoryItem::GetPreferredBatteryReplaceBay() const
{
	if (BatterySlot1Percent < 0.0f)
	{
		return 0;
	}
	if (BatterySlot2Percent < 0.0f)
	{
		return 1;
	}
	return (BatterySlot1Percent <= BatterySlot2Percent) ? 0 : 1;
}

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
	if (const UWorld* World = GetWorld(); World && !FirstHeldTime.Contains(ItemToAdd.ItemID))
	{
		FirstHeldTime.Add(ItemToAdd.ItemID, World->GetTimeSeconds());
	}

	// First fill existing stacks of the same ItemID (keeping distinct battery charges separate)
	for (FInventoryItem& ExistingItem : Items)
	{
		const bool bMatchesBatteryCharge = (ExistingItem.ItemID != APickupItemActor::BatteryItemID)
			|| FMath::IsNearlyEqual(ExistingItem.BatteryChargePercent, ItemToAdd.BatteryChargePercent, 0.01f);

		if (ExistingItem.ItemID == ItemToAdd.ItemID
			&& bMatchesBatteryCharge
			&& ExistingItem.Quantity < ExistingItem.MaxStackSize)
		{
			if (ExistingItem.WorldMesh.IsNull() && !ItemToAdd.WorldMesh.IsNull())
			{
				ExistingItem.WorldMesh = ItemToAdd.WorldMesh;
			}
			if (!ExistingItem.bHasCustomWorldMeshScale && ItemToAdd.bHasCustomWorldMeshScale)
			{
				ExistingItem.bHasCustomWorldMeshScale = true;
				ExistingItem.WorldMeshScale = ItemToAdd.WorldMeshScale;
			}

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

	const ULostWithinScentSubsystem* Scent = ULostWithinScentSubsystem::Get(this);
	const bool bCarriesScent = Scent && GetSecondsHeld(DroppedItem.ItemID) >= Scent->ItemScentMinHeldSeconds;
	SpawnWorldPickupForItem(DroppedItem, bCarriesScent);
	return true;
}

float UInventoryComponent::GetSecondsHeld(FName ItemID) const
{
	const UWorld* World = GetWorld();
	const double* Since = FirstHeldTime.Find(ItemID);
	return (World && Since) ? static_cast<float>(World->GetTimeSeconds() - *Since) : 0.0f;
}

void UInventoryComponent::SpawnWorldPickupForItem(const FInventoryItem& DroppedItem, bool bCarriesScent) const
{
	AActor* Owner = GetOwner();
	UWorld* World = GetWorld();
	if (!Owner || !World)
	{
		return;
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

	const FVector BaseOrigin = Owner->GetActorLocation() + FVector(0.0f, 0.0f, 45.0f);
	float DropForwardOffset = 52.0f;

	FCollisionQueryParams WallQueryParams(SCENE_QUERY_STAT(InventoryDropTrace), false, Owner);
	FHitResult WallHit;
	const FVector WallTraceEnd = BaseOrigin + ForwardDir * 70.0f;
	if (World->LineTraceSingleByChannel(WallHit, BaseOrigin, WallTraceEnd, ECC_Visibility, WallQueryParams))
	{
		DropForwardOffset = FMath::Max(16.0f, WallHit.Distance - 25.0f);
	}

	const FVector DropStart = BaseOrigin + ForwardDir * DropForwardOffset;

	UClass* SpawnClass = WorldPickupClass ? WorldPickupClass.Get() : APickupItemActor::StaticClass();
	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = Owner;
	SpawnParams.Instigator = Cast<APawn>(Owner);
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	const FRotator SpawnRotation = (DroppedItem.ItemID == APickupItemActor::FlashlightItemID)
		? ForwardDir.Rotation()
		: FRotator::ZeroRotator;

	if (APickupItemActor* DroppedPickup = World->SpawnActor<APickupItemActor>(SpawnClass, DropStart, SpawnRotation, SpawnParams))
	{
		DroppedPickup->ApplyItemData(DroppedItem);
		const FVector LaunchVelocity = ForwardDir * 220.0f + FVector(0.0f, 0.0f, 160.0f);
		DroppedPickup->DropWithPhysics(DropStart, LaunchVelocity, { Owner });
		if (bCarriesScent)
		{
			if (ULostWithinScentSubsystem* Scent = ULostWithinScentSubsystem::Get(this))
			{
				Scent->AddItemScent(DroppedPickup);
			}
		}
	}
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

bool UInventoryComponent::GetFlashlightOnState() const
{
	const FInventoryItem* FlashlightSlot = FindFirstItemSlotByID(APickupItemActor::FlashlightItemID);
	return FlashlightSlot && FlashlightSlot->bIsFlashlightOn && FlashlightSlot->CanOperateFlashlight();
}

void UInventoryComponent::SetFlashlightOnState(bool bInIsOn)
{
	if (FInventoryItem* FlashlightSlot = FindFirstItemSlotByID(APickupItemActor::FlashlightItemID))
	{
		FlashlightSlot->bIsFlashlightOn = bInIsOn && FlashlightSlot->CanOperateFlashlight();
	}
}

bool UInventoryComponent::CanOperateFlashlight() const
{
	const FInventoryItem* FlashlightSlot = FindFirstItemSlotByID(APickupItemActor::FlashlightItemID);
	return FlashlightSlot && FlashlightSlot->CanOperateFlashlight();
}

void UInventoryComponent::GetFlashlightBatteryState(
	int32& OutInstalledCount,
	float& OutCombinedPercent,
	float& OutSlot1Percent,
	float& OutSlot2Percent) const
{
	if (const FInventoryItem* FlashlightSlot = FindFirstItemSlotByID(APickupItemActor::FlashlightItemID))
	{
		OutInstalledCount = FlashlightSlot->GetInstalledBatteryCount();
		OutCombinedPercent = FlashlightSlot->GetCombinedBatteryPercent();
		OutSlot1Percent = FlashlightSlot->BatterySlot1Percent;
		OutSlot2Percent = FlashlightSlot->BatterySlot2Percent;
		return;
	}

	OutInstalledCount = 0;
	OutCombinedPercent = 0.0f;
	OutSlot1Percent = FInventoryItem::EmptyBatterySlotValue;
	OutSlot2Percent = FInventoryItem::EmptyBatterySlotValue;
}

int32 UInventoryComponent::GetBestUsableBatterySlotIndex() const
{
	int32 BestSlotIndex = INDEX_NONE;
	float BestCharge = 0.0f;

	for (int32 SlotIdx = 0; SlotIdx < Items.Num(); ++SlotIdx)
	{
		const FInventoryItem& Candidate = Items[SlotIdx];
		if (Candidate.ItemID == APickupItemActor::BatteryItemID
			&& Candidate.Quantity > 0
			&& Candidate.BatteryChargePercent > KINDA_SMALL_NUMBER
			&& Candidate.BatteryChargePercent > BestCharge)
		{
			BestCharge = Candidate.BatteryChargePercent;
			BestSlotIndex = SlotIdx;
		}
	}

	return BestSlotIndex;
}

bool UInventoryComponent::CanReloadFlashlight(int32 SpecificBatterySlotIndex) const
{
	const FInventoryItem* FlashlightSlot = FindFirstItemSlotByID(APickupItemActor::FlashlightItemID);
	if (!FlashlightSlot)
	{
		return false;
	}

	const int32 CandidateIndex = (SpecificBatterySlotIndex == INDEX_NONE)
		? GetBestUsableBatterySlotIndex()
		: SpecificBatterySlotIndex;

	if (!Items.IsValidIndex(CandidateIndex))
	{
		return false;
	}

	const FInventoryItem& BatteryItem = Items[CandidateIndex];
	if (BatteryItem.ItemID != APickupItemActor::BatteryItemID || BatteryItem.Quantity <= 0)
	{
		return false;
	}

	const float IncomingCharge = FMath::Clamp(BatteryItem.BatteryChargePercent, 0.0f, 100.0f);
	if (IncomingCharge <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	const int32 TargetBay = FlashlightSlot->GetPreferredBatteryReplaceBay();
	const float ExistingCharge = FlashlightSlot->GetBatterySlotPercent(TargetBay);
	return ExistingCharge < 0.0f || IncomingCharge > (ExistingCharge + KINDA_SMALL_NUMBER);
}

bool UInventoryComponent::ReplaceFlashlightBatteryWithSlot(int32 BatterySlotIndex)
{
	if (!CanReloadFlashlight(BatterySlotIndex))
	{
		return false;
	}

	const int32 FlashlightIndex = FindFirstItemIndexByID(APickupItemActor::FlashlightItemID);
	if (!Items.IsValidIndex(FlashlightIndex))
	{
		return false;
	}

	const float IncomingCharge = FMath::Clamp(Items[BatterySlotIndex].BatteryChargePercent, 0.0f, 100.0f);
	const int32 TargetBay = Items[FlashlightIndex].GetPreferredBatteryReplaceBay();
	const float OldCharge = Items[FlashlightIndex].GetBatterySlotPercent(TargetBay);

	Items[FlashlightIndex].SetBatterySlotPercent(TargetBay, IncomingCharge);

	// Consume 1 battery from the chosen inventory slot before returning the replaced cell so full inventories keep space.
	Items[BatterySlotIndex].Quantity -= 1;
	if (Items[BatterySlotIndex].Quantity <= 0)
	{
		Items.RemoveAt(BatterySlotIndex);
	}

	// Return partially charged replaced batteries (>0%) to inventory; discard dead (0%) cells.
	if (OldCharge > KINDA_SMALL_NUMBER)
	{
		const FInventoryItem ReturnedBattery = APickupItemActor::MakeBatteryInventoryItem(OldCharge);
		if (Items.Num() < MaxSlots)
		{
			Items.Add(ReturnedBattery);
		}
		else
		{
			SpawnWorldPickupForItem(ReturnedBattery);
		}
	}

	OnInventoryUpdated.Broadcast();
	return true;
}

bool UInventoryComponent::ReloadBestBatteryIntoFlashlight()
{
	const int32 BestSlotIndex = GetBestUsableBatterySlotIndex();
	return BestSlotIndex != INDEX_NONE && ReplaceFlashlightBatteryWithSlot(BestSlotIndex);
}

bool UInventoryComponent::ConsumeFlashlightBattery(float DeltaTime, float FullDurationSeconds, bool& OutTurnedOff)
{
	OutTurnedOff = false;

	FInventoryItem* FlashlightSlot = FindFirstItemSlotByID(APickupItemActor::FlashlightItemID);
	if (!FlashlightSlot || !FlashlightSlot->bIsFlashlightOn)
	{
		return false;
	}

	const bool bStillUsable = FlashlightSlot->ConsumeFlashlightCharge(DeltaTime, FullDurationSeconds);
	if (!bStillUsable)
	{
		FlashlightSlot->bIsFlashlightOn = false;
		OutTurnedOff = true;
		OnInventoryUpdated.Broadcast();
	}

	return bStillUsable;
}

FInventoryItem* UInventoryComponent::FindFirstItemSlotByID(FName ItemID)
{
	return const_cast<FInventoryItem*>(static_cast<const UInventoryComponent*>(this)->FindFirstItemSlotByID(ItemID));
}

const FInventoryItem* UInventoryComponent::FindFirstItemSlotByID(FName ItemID) const
{
	const int32 SlotIndex = FindFirstItemIndexByID(ItemID);
	return Items.IsValidIndex(SlotIndex) ? &Items[SlotIndex] : nullptr;
}

int32 UInventoryComponent::FindFirstItemIndexByID(FName ItemID) const
{
	if (ItemID.IsNone())
	{
		return INDEX_NONE;
	}

	for (int32 SlotIdx = 0; SlotIdx < Items.Num(); ++SlotIdx)
	{
		if (Items[SlotIdx].ItemID == ItemID)
		{
			return SlotIdx;
		}
	}
	return INDEX_NONE;
}
