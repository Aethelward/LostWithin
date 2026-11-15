// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/InventoryItemRowWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "InputCoreTypes.h"
#include "Interaction/PickupItemActor.h"
#include "Styling/CoreStyle.h"
#include "UI/InventoryDragDropOperation.h"
#include "UI/InventoryWidget.h"

UInventoryItemRowWidget::UInventoryItemRowWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetIsFocusable(false);
}

void UInventoryItemRowWidget::NativeConstruct()
{
	Super::NativeConstruct();

	BuildWidgetTreeIfNeeded();
	ApplyItemDisplay();
}

void UInventoryItemRowWidget::SetupRow(UInventoryWidget* InParentWidget, int32 InSlotIndex, const FInventoryItem& InItemData)
{
	ParentInventoryWidget = InParentWidget;
	SlotIndex = InSlotIndex;
	ItemData = InItemData;
	bIsDragGhost = false;

	SetVisibility(ESlateVisibility::Visible);
	BuildWidgetTreeIfNeeded();
	ApplyItemDisplay();
}

void UInventoryItemRowWidget::SetupAsDragGhost(const FInventoryItem& InItemData)
{
	ParentInventoryWidget.Reset();
	SlotIndex = INDEX_NONE;
	ItemData = InItemData;
	bIsDragGhost = true;

	SetVisibility(ESlateVisibility::HitTestInvisible);
	BuildWidgetTreeIfNeeded();
	ApplyItemDisplay();
}

void UInventoryItemRowWidget::NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseEnter(InGeometry, InMouseEvent);

	if (!bIsDragGhost && RowBorder)
	{
		RowBorder->SetBrushColor(FLinearColor(0.18f, 0.22f, 0.31f, 0.96f));
	}
}

void UInventoryItemRowWidget::NativeOnMouseLeave(const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseLeave(InMouseEvent);

	if (!bIsDragGhost && RowBorder)
	{
		RowBorder->SetBrushColor(FLinearColor(0.11f, 0.13f, 0.19f, 0.92f));
	}
}

FReply UInventoryItemRowWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bIsDragGhost)
	{
		return FReply::Unhandled();
	}

	if (InMouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		if (UInventoryWidget* ParentWidget = ParentInventoryWidget.Get())
		{
			ParentWidget->OpenItemContextMenu(SlotIndex, InMouseEvent.GetScreenSpacePosition());
			return FReply::Handled();
		}
	}
	else if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		if (UInventoryWidget* ParentWidget = ParentInventoryWidget.Get())
		{
			ParentWidget->CloseItemContextMenu();
		}

		return FReply::Handled().DetectDrag(TakeWidget(), EKeys::LeftMouseButton);
	}

	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

void UInventoryItemRowWidget::NativeOnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent, UDragDropOperation*& OutOperation)
{
	Super::NativeOnDragDetected(InGeometry, InMouseEvent, OutOperation);

	if (bIsDragGhost)
	{
		return;
	}

	UInventoryDragDropOperation* DragOp = NewObject<UInventoryDragDropOperation>(GetTransientPackage(), UInventoryDragDropOperation::StaticClass());
	if (!DragOp)
	{
		return;
	}

	DragOp->SlotIndex = SlotIndex;
	DragOp->ItemData = ItemData;
	DragOp->SourceInventoryWidget = ParentInventoryWidget;
	DragOp->Pivot = EDragPivot::MouseDown;

	if (APlayerController* OwningPC = GetOwningPlayer())
	{
		if (UInventoryItemRowWidget* DragVisual = CreateWidget<UInventoryItemRowWidget>(OwningPC, UInventoryItemRowWidget::StaticClass()))
		{
			DragVisual->SetupAsDragGhost(ItemData);
			DragOp->DefaultDragVisual = DragVisual;
		}
	}

	OutOperation = DragOp;
}

void UInventoryItemRowWidget::NativeOnDragCancelled(const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	Super::NativeOnDragCancelled(InDragDropEvent, InOperation);

	if (UInventoryWidget* ParentWidget = ParentInventoryWidget.Get())
	{
		ParentWidget->HandleItemDragCancelled(InOperation, InDragDropEvent);
	}
}

bool UInventoryItemRowWidget::CanAcceptBatteryDrag(UDragDropOperation* InOperation) const
{
	if (bIsDragGhost || ItemData.ItemID != APickupItemActor::FlashlightItemID)
	{
		return false;
	}

	const UInventoryDragDropOperation* DragOp = Cast<UInventoryDragDropOperation>(InOperation);
	return DragOp
		&& DragOp->SlotIndex != SlotIndex
		&& DragOp->ItemData.ItemID == APickupItemActor::BatteryItemID
		&& DragOp->ItemData.BatteryChargePercent > KINDA_SMALL_NUMBER;
}

bool UInventoryItemRowWidget::NativeOnDragOver(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	if (CanAcceptBatteryDrag(InOperation))
	{
		if (RowBorder)
		{
			RowBorder->SetBrushColor(FLinearColor(0.20f, 0.34f, 0.22f, 0.96f));
		}
		return true;
	}

	return Super::NativeOnDragOver(InGeometry, InDragDropEvent, InOperation);
}

void UInventoryItemRowWidget::NativeOnDragLeave(const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	Super::NativeOnDragLeave(InDragDropEvent, InOperation);

	if (!bIsDragGhost && RowBorder)
	{
		RowBorder->SetBrushColor(FLinearColor(0.11f, 0.13f, 0.19f, 0.92f));
	}
}

bool UInventoryItemRowWidget::NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	if (!bIsDragGhost && RowBorder)
	{
		RowBorder->SetBrushColor(FLinearColor(0.11f, 0.13f, 0.19f, 0.92f));
	}

	if (CanAcceptBatteryDrag(InOperation))
	{
		if (const UInventoryDragDropOperation* DragOp = Cast<UInventoryDragDropOperation>(InOperation))
		{
			if (UInventoryWidget* ParentWidget = ParentInventoryWidget.Get())
			{
				ParentWidget->HandleBatteryDroppedOnFlashlight(DragOp->SlotIndex);
				return true;
			}
		}
	}

	return Super::NativeOnDrop(InGeometry, InDragDropEvent, InOperation);
}

void UInventoryItemRowWidget::BuildWidgetTreeIfNeeded()
{
	if (!WidgetTree || bHasBuiltTree || WidgetTree->RootWidget)
	{
		return;
	}

	bHasBuiltTree = true;

	RowBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("RowBorder"));
	RowBorder->SetBrushColor(FLinearColor(0.11f, 0.13f, 0.19f, 0.92f));
	RowBorder->SetPadding(FMargin(12.0f, 8.0f));
	WidgetTree->RootWidget = RowBorder;

	UVerticalBox* RowContent = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("RowContent"));

	TitleText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("TitleText"));
	TitleText->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 14));
	TitleText->SetColorAndOpacity(FSlateColor(FLinearColor(0.96f, 0.92f, 0.76f, 1.0f)));
	TitleText->SetVisibility(ESlateVisibility::HitTestInvisible);
	RowContent->AddChildToVerticalBox(TitleText);

	DescText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("DescText"));
	DescText->SetFont(FCoreStyle::GetDefaultFontStyle("Regular", 11));
	DescText->SetColorAndOpacity(FSlateColor(FLinearColor(0.76f, 0.80f, 0.88f, 1.0f)));
	DescText->SetAutoWrapText(true);
	DescText->SetVisibility(ESlateVisibility::HitTestInvisible);
	if (UVerticalBoxSlot* DescSlot = RowContent->AddChildToVerticalBox(DescText))
	{
		DescSlot->SetPadding(FMargin(0.0f, 2.0f, 0.0f, 0.0f));
	}

	RowBorder->SetContent(RowContent);
}

void UInventoryItemRowWidget::ApplyItemDisplay()
{
	if (RowBorder)
	{
		RowBorder->SetBrushColor(bIsDragGhost
			? FLinearColor(0.16f, 0.20f, 0.29f, 0.85f)
			: FLinearColor(0.11f, 0.13f, 0.19f, 0.92f));
	}

	if (TitleText)
	{
		FText ItemDisplayName = ItemData.DisplayName.IsEmpty() ? FText::FromName(ItemData.ItemID) : ItemData.DisplayName;
		if (ItemData.ItemID == APickupItemActor::BatteryItemID)
		{
			ItemDisplayName = FText::FromString(FString::Printf(
				TEXT("%s (%s)"),
				*ItemDisplayName.ToString(),
				*UInventoryWidget::FormatBatteryBayPercentString(ItemData.BatteryChargePercent)));
		}

		TitleText->SetText(FText::Format(
			NSLOCTEXT("LostWithin", "InventoryItemTitle", "{0}   x{1}"),
			ItemDisplayName,
			FText::AsNumber(ItemData.Quantity)));
	}

	if (DescText)
	{
		if (bIsDragGhost)
		{
			DescText->SetVisibility(ESlateVisibility::Collapsed);
		}
		else if (ItemData.ItemID == APickupItemActor::FlashlightItemID)
		{
			const FString Cell1Label = UInventoryWidget::FormatBatteryBayPercentString(ItemData.BatterySlot1Percent);
			const FString Cell2Label = UInventoryWidget::FormatBatteryBayPercentString(ItemData.BatterySlot2Percent);
			const int32 InstalledCount = ItemData.GetInstalledBatteryCount();
			const int32 CombinedRounded = FMath::Clamp(FMath::RoundToInt(ItemData.GetCombinedBatteryPercent()), 0, 100);
			const FString BatteryStatusLine = FString::Printf(
				TEXT("Batteries (%d/2): %d%% Total  [Cell 1: %s | Cell 2: %s]"),
				InstalledCount,
				CombinedRounded,
				*Cell1Label,
				*Cell2Label);
			const FString FullDesc = ItemData.Description.IsEmpty()
				? BatteryStatusLine
				: FString::Printf(TEXT("%s\n%s"), *ItemData.Description.ToString(), *BatteryStatusLine);
			DescText->SetText(FText::FromString(FullDesc));
			DescText->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
		else if (ItemData.Description.IsEmpty())
		{
			DescText->SetVisibility(ESlateVisibility::Collapsed);
		}
		else
		{
			DescText->SetText(ItemData.Description);
			DescText->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
	}
}

