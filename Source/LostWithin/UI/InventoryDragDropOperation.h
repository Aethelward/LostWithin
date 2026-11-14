// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/DragDropOperation.h"
#include "Inventory/InventoryComponent.h"
#include "InventoryDragDropOperation.generated.h"

class UInventoryWidget;

/**
 *  Drag-and-drop payload carrying the slot index and item stack dragged out of the Placeholder Inventory UI.
 */
UCLASS()
class LOSTWITHIN_API UInventoryDragDropOperation : public UDragDropOperation
{
	GENERATED_BODY()

public:

	/** Index of the source slot inside UInventoryComponent::Items */
	UPROPERTY(BlueprintReadOnly, Category = "Inventory|DragDrop")
	int32 SlotIndex = INDEX_NONE;

	/** Snapshot of the dragged item stack */
	UPROPERTY(BlueprintReadOnly, Category = "Inventory|DragDrop")
	FInventoryItem ItemData;

	/** Owning Placeholder Inventory widget that initiated the drag */
	TWeakObjectPtr<UInventoryWidget> SourceInventoryWidget;
};

