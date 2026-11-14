// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Inventory/InventoryComponent.h"
#include "InventoryItemRowWidget.generated.h"

class UBorder;
class UDragDropOperation;
class UInventoryWidget;
class UTextBlock;

/**
 *  Interactive item row inside UInventoryWidget supporting hover feedback, [RMB] drop context menu, and drag-out dropping.
 */
UCLASS()
class LOSTWITHIN_API UInventoryItemRowWidget : public UUserWidget
{
	GENERATED_BODY()

public:

	UInventoryItemRowWidget(const FObjectInitializer& ObjectInitializer);

	/** Configures this row for interactive display inside ParentWidget */
	void SetupRow(UInventoryWidget* InParentWidget, int32 InSlotIndex, const FInventoryItem& InItemData);

	/** Configures this widget as a non-interactive translucent drag preview */
	void SetupAsDragGhost(const FInventoryItem& InItemData);

protected:

	virtual void NativeConstruct() override;
	virtual void NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnMouseLeave(const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent, UDragDropOperation*& OutOperation) override;
	virtual void NativeOnDragCancelled(const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;

private:

	/** Ensures the programmatic UMG hierarchy exists */
	void BuildWidgetTreeIfNeeded();

	/** Updates title, description, and brush visuals from current state */
	void ApplyItemDisplay();

	UPROPERTY(Transient)
	TObjectPtr<UBorder> RowBorder = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> TitleText = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> DescText = nullptr;

	TWeakObjectPtr<UInventoryWidget> ParentInventoryWidget;
	int32 SlotIndex = INDEX_NONE;
	FInventoryItem ItemData;
	bool bIsDragGhost = false;
	bool bHasBuiltTree = false;
};

