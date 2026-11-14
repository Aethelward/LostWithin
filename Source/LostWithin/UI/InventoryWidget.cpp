// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/InventoryWidget.h"
#include "Blueprint/DragDropOperation.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateColorBrush.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "InputCoreTypes.h"
#include "Inventory/InventoryComponent.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateTypes.h"
#include "UI/InventoryDragDropOperation.h"
#include "UI/InventoryItemRowWidget.h"

UInventoryWidget::UInventoryWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetIsFocusable(false);
}

TSharedRef<SWidget> UInventoryWidget::RebuildWidget()
{
	BuildWidgetTreeIfNeeded();
	return Super::RebuildWidget();
}

void UInventoryWidget::NativeConstruct()
{
	Super::NativeConstruct();

	SetInventoryOpen(bIsInventoryOpen);
	SetInteractionPrompt(PendingPromptText);
	RefreshInventory();
}

void UInventoryWidget::NativeDestruct()
{
	if (UInventoryComponent* Inventory = BoundInventory.Get())
	{
		Inventory->OnInventoryUpdated.RemoveDynamic(this, &UInventoryWidget::HandleInventoryUpdated);
	}

	Super::NativeDestruct();
}

void UInventoryWidget::BindInventory(UInventoryComponent* InInventoryComponent)
{
	if (UInventoryComponent* Previous = BoundInventory.Get())
	{
		Previous->OnInventoryUpdated.RemoveDynamic(this, &UInventoryWidget::HandleInventoryUpdated);
	}

	BoundInventory = InInventoryComponent;

	if (InInventoryComponent)
	{
		InInventoryComponent->OnInventoryUpdated.AddDynamic(this, &UInventoryWidget::HandleInventoryUpdated);
	}

	RefreshInventory();
}

void UInventoryWidget::SetInventoryOpen(bool bOpen)
{
	bIsInventoryOpen = bOpen;

	SetIsFocusable(bIsInventoryOpen);
	SetVisibility(bIsInventoryOpen ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::HitTestInvisible);

	if (OutsideDropCatcher)
	{
		OutsideDropCatcher->SetVisibility(bIsInventoryOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}

	if (InventoryPanelBorder)
	{
		InventoryPanelBorder->SetVisibility(bIsInventoryOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}

	if (CrosshairText)
	{
		CrosshairText->SetVisibility(bIsInventoryOpen ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}

	if (!bIsInventoryOpen)
	{
		CloseItemContextMenu();
		SetDragOutsideFeedback(false);
		SetInteractionPrompt(PendingPromptText);
	}
	else
	{
		if (InteractionPromptBorder)
		{
			InteractionPromptBorder->SetVisibility(ESlateVisibility::Collapsed);
		}
		CloseItemContextMenu();
		SetDragOutsideFeedback(false);
		RefreshInventory();
	}
}

void UInventoryWidget::SetInteractionPrompt(const FText& PromptText)
{
	PendingPromptText = PromptText;

	if (InteractionPromptText && InteractionPromptBorder)
	{
		const bool bHasPrompt = !bIsInventoryOpen && !PromptText.IsEmpty();
		InteractionPromptText->SetText(PromptText);
		InteractionPromptBorder->SetVisibility(bHasPrompt ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
}

void UInventoryWidget::HandleInventoryUpdated()
{
	RefreshInventory();
}

void UInventoryWidget::RefreshInventory()
{
	CloseItemContextMenu();

	if (!ItemListContainer || !WidgetTree)
	{
		return;
	}

	ItemListContainer->ClearChildren();

	const UInventoryComponent* Inventory = BoundInventory.Get();
	const TArray<FInventoryItem> EmptyItems;
	const TArray<FInventoryItem>& Items = Inventory ? Inventory->GetItems() : EmptyItems;
	const int32 MaxSlots = Inventory ? Inventory->GetMaxSlots() : 16;

	if (SlotSummaryText)
	{
		SlotSummaryText->SetText(FText::Format(
			NSLOCTEXT("LostWithin", "InventorySlotSummary", "Slots Used: {0} / {1}"),
			FText::AsNumber(Items.Num()),
			FText::AsNumber(MaxSlots)));
	}

	if (Items.IsEmpty())
	{
		UTextBlock* EmptyText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		EmptyText->SetText(NSLOCTEXT("LostWithin", "EmptyInventory", "Placeholder Inventory is empty. Pick up items with [F]."));
		EmptyText->SetFont(FCoreStyle::GetDefaultFontStyle("Italic", 13));
		EmptyText->SetColorAndOpacity(FSlateColor(FLinearColor(0.75f, 0.78f, 0.85f, 1.0f)));
		EmptyText->SetAutoWrapText(true);
		EmptyText->SetVisibility(ESlateVisibility::HitTestInvisible);

		if (UVerticalBoxSlot* EmptySlot = ItemListContainer->AddChildToVerticalBox(EmptyText))
		{
			EmptySlot->SetPadding(FMargin(8.0f, 12.0f));
		}
		return;
	}

	APlayerController* OwningPC = GetOwningPlayer();
	for (int32 SlotIdx = 0; SlotIdx < Items.Num(); ++SlotIdx)
	{
		UInventoryItemRowWidget* RowWidget = OwningPC
			? CreateWidget<UInventoryItemRowWidget>(OwningPC, UInventoryItemRowWidget::StaticClass())
			: WidgetTree->ConstructWidget<UInventoryItemRowWidget>(UInventoryItemRowWidget::StaticClass());

		if (!RowWidget)
		{
			continue;
		}

		RowWidget->SetupRow(this, SlotIdx, Items[SlotIdx]);
		if (UVerticalBoxSlot* RowSlot = ItemListContainer->AddChildToVerticalBox(RowWidget))
		{
			RowSlot->SetPadding(FMargin(0.0f, 4.0f));
		}
	}
}

void UInventoryWidget::OpenItemContextMenu(int32 SlotIndex, const FVector2D& ScreenSpacePosition)
{
	if (!bIsInventoryOpen || !ContextMenuBorder || !ContextDropPrimaryButton || !ContextDropPrimaryText || !ContextDropAllButton)
	{
		return;
	}

	const UInventoryComponent* Inventory = BoundInventory.Get();
	if (!Inventory || !Inventory->GetItems().IsValidIndex(SlotIndex))
	{
		CloseItemContextMenu();
		return;
	}

	const FInventoryItem& Item = Inventory->GetItems()[SlotIndex];
	ContextMenuSlotIndex = SlotIndex;
	bIsContextMenuOpen = true;

	if (ContextItemTitleText)
	{
		const FText DisplayName = Item.DisplayName.IsEmpty() ? FText::FromName(Item.ItemID) : Item.DisplayName;
		ContextItemTitleText->SetText(DisplayName);
	}

	if (Item.Quantity > 1)
	{
		ContextDropPrimaryText->SetText(NSLOCTEXT("LostWithin", "ContextDropOne", "Drop 1"));
		ContextDropAllButton->SetVisibility(ESlateVisibility::Visible);
	}
	else
	{
		ContextDropPrimaryText->SetText(NSLOCTEXT("LostWithin", "ContextDropSingle", "Drop"));
		ContextDropAllButton->SetVisibility(ESlateVisibility::Collapsed);
	}

	if (UCanvasPanelSlot* MenuSlot = Cast<UCanvasPanelSlot>(ContextMenuBorder->Slot))
	{
		const FGeometry& WidgetGeometry = GetCachedGeometry();
		const FVector2D ViewportLocalSize = WidgetGeometry.GetLocalSize();
		const FVector2D EstimatedMenuSize(170.0f, Item.Quantity > 1 ? 116.0f : 80.0f);

		FVector2D DesiredPos = WidgetGeometry.AbsoluteToLocal(ScreenSpacePosition) + FVector2D(4.0f, 4.0f);
		if (ViewportLocalSize.X > EstimatedMenuSize.X + 16.0f)
		{
			DesiredPos.X = FMath::Clamp(DesiredPos.X, 8.0f, ViewportLocalSize.X - EstimatedMenuSize.X - 8.0f);
		}
		if (ViewportLocalSize.Y > EstimatedMenuSize.Y + 16.0f)
		{
			DesiredPos.Y = FMath::Clamp(DesiredPos.Y, 8.0f, ViewportLocalSize.Y - EstimatedMenuSize.Y - 8.0f);
		}

		MenuSlot->SetPosition(DesiredPos);
	}

	ContextMenuBorder->SetVisibility(ESlateVisibility::Visible);
}

void UInventoryWidget::CloseItemContextMenu()
{
	bIsContextMenuOpen = false;
	ContextMenuSlotIndex = INDEX_NONE;

	if (ContextMenuBorder)
	{
		ContextMenuBorder->SetVisibility(ESlateVisibility::Collapsed);
	}
}

bool UInventoryWidget::IsScreenPositionInsideInventoryPanel(const FVector2D& ScreenSpacePosition) const
{
	if (!InventoryPanelBorder || !InventoryPanelBorder->IsVisible())
	{
		return false;
	}

	return InventoryPanelBorder->GetCachedGeometry().IsUnderLocation(ScreenSpacePosition);
}

void UInventoryWidget::HandleItemDragCancelled(UDragDropOperation* InOperation, const FDragDropEvent& InDragDropEvent)
{
	SetDragOutsideFeedback(false);

	if (!bIsInventoryOpen)
	{
		return;
	}

	const UInventoryDragDropOperation* DragOp = Cast<UInventoryDragDropOperation>(InOperation);
	if (!DragOp)
	{
		return;
	}

	// Mouse release outside the game viewport ends drag with LeftMouseButton; Esc cancel has no effecting button.
	if (InDragDropEvent.GetEffectingButton() == EKeys::LeftMouseButton
		&& !IsScreenPositionInsideInventoryPanel(InDragDropEvent.GetScreenSpacePosition()))
	{
		if (UInventoryComponent* Inventory = BoundInventory.Get())
		{
			Inventory->DropItemAtSlot(DragOp->SlotIndex, -1);
		}
	}
}

FReply UInventoryWidget::NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (bIsInventoryOpen && !InKeyEvent.IsRepeat())
	{
		if (InKeyEvent.GetKey() == EKeys::Tab)
		{
			CloseItemContextMenu();
			OnCloseRequested.Broadcast();
			return FReply::Handled();
		}

		if (InKeyEvent.GetKey() == EKeys::Escape && bIsContextMenuOpen)
		{
			CloseItemContextMenu();
			return FReply::Handled();
		}
	}

	return Super::NativeOnPreviewKeyDown(InGeometry, InKeyEvent);
}

FReply UInventoryWidget::NativeOnPreviewMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bIsInventoryOpen && bIsContextMenuOpen && ContextMenuBorder)
	{
		if (!ContextMenuBorder->GetCachedGeometry().IsUnderLocation(InMouseEvent.GetScreenSpacePosition()))
		{
			CloseItemContextMenu();
		}
	}

	return Super::NativeOnPreviewMouseButtonDown(InGeometry, InMouseEvent);
}

bool UInventoryWidget::NativeOnDragOver(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	if (!bIsInventoryOpen || !Cast<UInventoryDragDropOperation>(InOperation))
	{
		return Super::NativeOnDragOver(InGeometry, InDragDropEvent, InOperation);
	}

	const bool bIsOutsidePanel = !IsScreenPositionInsideInventoryPanel(InDragDropEvent.GetScreenSpacePosition());
	SetDragOutsideFeedback(bIsOutsidePanel);
	return true;
}

void UInventoryWidget::NativeOnDragLeave(const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	Super::NativeOnDragLeave(InDragDropEvent, InOperation);
	SetDragOutsideFeedback(false);
}

bool UInventoryWidget::NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	SetDragOutsideFeedback(false);

	if (!bIsInventoryOpen)
	{
		return Super::NativeOnDrop(InGeometry, InDragDropEvent, InOperation);
	}

	const UInventoryDragDropOperation* DragOp = Cast<UInventoryDragDropOperation>(InOperation);
	if (!DragOp)
	{
		return Super::NativeOnDrop(InGeometry, InDragDropEvent, InOperation);
	}

	if (!IsScreenPositionInsideInventoryPanel(InDragDropEvent.GetScreenSpacePosition()))
	{
		if (UInventoryComponent* Inventory = BoundInventory.Get())
		{
			Inventory->DropItemAtSlot(DragOp->SlotIndex, -1);
		}
	}

	return true;
}

void UInventoryWidget::HandleContextDropPrimaryClicked()
{
	ExecuteContextDrop(1);
}

void UInventoryWidget::HandleContextDropAllClicked()
{
	ExecuteContextDrop(-1);
}

void UInventoryWidget::ExecuteContextDrop(int32 QuantityToDrop)
{
	const int32 TargetSlotIndex = ContextMenuSlotIndex;
	CloseItemContextMenu();

	if (TargetSlotIndex != INDEX_NONE)
	{
		if (UInventoryComponent* Inventory = BoundInventory.Get())
		{
			Inventory->DropItemAtSlot(TargetSlotIndex, QuantityToDrop);
		}
	}
}

void UInventoryWidget::SetDragOutsideFeedback(bool bShowOutsideDropFeedback)
{
	if (bIsDragOutsideHighlighted == bShowOutsideDropFeedback)
	{
		return;
	}

	bIsDragOutsideHighlighted = bShowOutsideDropFeedback;

	if (OutsideDropCatcher)
	{
		OutsideDropCatcher->SetBrushColor(bShowOutsideDropFeedback
			? FLinearColor(0.75f, 0.20f, 0.16f, 0.14f)
			: FLinearColor::Transparent);
	}

	if (DropOutsideBanner)
	{
		DropOutsideBanner->SetVisibility(bShowOutsideDropFeedback ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
}

UButton* UInventoryWidget::CreateContextMenuButton(UVerticalBox* ParentBox, const FName ButtonName, const FText& DefaultLabel, TObjectPtr<UTextBlock>* OutLabelText)
{
	UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), ButtonName);

	FButtonStyle ButtonStyle;
	ButtonStyle.SetNormal(FSlateColorBrush(FLinearColor(0.14f, 0.17f, 0.25f, 1.0f)));
	ButtonStyle.SetHovered(FSlateColorBrush(FLinearColor(0.58f, 0.20f, 0.18f, 1.0f)));
	ButtonStyle.SetPressed(FSlateColorBrush(FLinearColor(0.42f, 0.13f, 0.12f, 1.0f)));
	ButtonStyle.SetNormalPadding(FMargin(14.0f, 7.0f));
	ButtonStyle.SetPressedPadding(FMargin(14.0f, 8.0f, 14.0f, 6.0f));
	Button->SetStyle(ButtonStyle);

	UTextBlock* LabelText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	LabelText->SetText(DefaultLabel);
	LabelText->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 12));
	LabelText->SetColorAndOpacity(FSlateColor(FLinearColor(0.96f, 0.96f, 0.98f, 1.0f)));
	LabelText->SetVisibility(ESlateVisibility::HitTestInvisible);
	Button->SetContent(LabelText);

	if (OutLabelText)
	{
		*OutLabelText = LabelText;
	}

	if (UVerticalBoxSlot* ButtonSlot = ParentBox->AddChildToVerticalBox(Button))
	{
		ButtonSlot->SetPadding(FMargin(0.0f, 2.0f));
	}

	return Button;
}

void UInventoryWidget::BuildWidgetTreeIfNeeded()
{
	if (!WidgetTree || WidgetTree->RootWidget)
	{
		return;
	}

	UCanvasPanel* RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("RootCanvas"));
	RootCanvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	WidgetTree->RootWidget = RootCanvas;

	// Fullscreen drop surface outside Placeholder Inventory panel
	OutsideDropCatcher = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("OutsideDropCatcher"));
	OutsideDropCatcher->SetBrushColor(FLinearColor::Transparent);
	OutsideDropCatcher->SetVisibility(ESlateVisibility::Collapsed);
	if (UCanvasPanelSlot* CatcherSlot = RootCanvas->AddChildToCanvas(OutsideDropCatcher))
	{
		CatcherSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
		CatcherSlot->SetOffsets(FMargin(0.0f));
	}

	// Center crosshair
	CrosshairText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("CrosshairText"));
	CrosshairText->SetText(FText::FromString(TEXT("+")));
	CrosshairText->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 16));
	CrosshairText->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 1.0f, 1.0f, 0.82f)));
	CrosshairText->SetShadowOffset(FVector2D(1.0f, 1.0f));
	CrosshairText->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.75f));
	CrosshairText->SetVisibility(ESlateVisibility::HitTestInvisible);
	if (UCanvasPanelSlot* CrosshairSlot = RootCanvas->AddChildToCanvas(CrosshairText))
	{
		CrosshairSlot->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
		CrosshairSlot->SetAlignment(FVector2D(0.5f, 0.5f));
		CrosshairSlot->SetAutoSize(true);
	}

	// Top-left subtle control legend
	UBorder* ControlsHintBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ControlsHintBorder"));
	ControlsHintBorder->SetBrushColor(FLinearColor(0.03f, 0.04f, 0.06f, 0.52f));
	ControlsHintBorder->SetPadding(FMargin(10.0f, 6.0f));
	ControlsHintBorder->SetVisibility(ESlateVisibility::HitTestInvisible);
	UTextBlock* ControlsHintText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ControlsHintText"));
	ControlsHintText->SetText(NSLOCTEXT("LostWithin", "ControlsHint", "[Tab] Placeholder Inventory  |  [F] Interact  |  [LMB] Hit  |  [F5] Switch POV  |  [Shift] Walk  |  [Ctrl] Sprint  |  [C] Crouch"));
	ControlsHintText->SetFont(FCoreStyle::GetDefaultFontStyle("Regular", 10));
	ControlsHintText->SetColorAndOpacity(FSlateColor(FLinearColor(0.85f, 0.88f, 0.94f, 0.85f)));
	ControlsHintText->SetVisibility(ESlateVisibility::HitTestInvisible);
	ControlsHintBorder->SetContent(ControlsHintText);
	if (UCanvasPanelSlot* HintSlot = RootCanvas->AddChildToCanvas(ControlsHintBorder))
	{
		HintSlot->SetAnchors(FAnchors(0.0f, 0.0f, 0.0f, 0.0f));
		HintSlot->SetPosition(FVector2D(16.0f, 16.0f));
		HintSlot->SetAutoSize(true);
	}

	// Bottom-center contextual interaction prompt
	InteractionPromptBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("InteractionPromptBorder"));
	InteractionPromptBorder->SetBrushColor(FLinearColor(0.03f, 0.04f, 0.06f, 0.78f));
	InteractionPromptBorder->SetPadding(FMargin(16.0f, 8.0f));
	InteractionPromptBorder->SetVisibility(ESlateVisibility::Collapsed);

	InteractionPromptText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("InteractionPromptText"));
	InteractionPromptText->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 14));
	InteractionPromptText->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.95f, 0.78f, 1.0f)));
	InteractionPromptText->SetJustification(ETextJustify::Center);
	InteractionPromptText->SetVisibility(ESlateVisibility::HitTestInvisible);
	InteractionPromptBorder->SetContent(InteractionPromptText);

	if (UCanvasPanelSlot* PromptSlot = RootCanvas->AddChildToCanvas(InteractionPromptBorder))
	{
		PromptSlot->SetAnchors(FAnchors(0.5f, 0.72f, 0.5f, 0.72f));
		PromptSlot->SetAlignment(FVector2D(0.5f, 0.5f));
		PromptSlot->SetAutoSize(true);
	}

	// Centered Placeholder Inventory overlay panel ([Tab])
	InventoryPanelBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("InventoryPanelBorder"));
	InventoryPanelBorder->SetBrushColor(FLinearColor(0.04f, 0.05f, 0.08f, 0.92f));
	InventoryPanelBorder->SetPadding(FMargin(24.0f));
	InventoryPanelBorder->SetVisibility(ESlateVisibility::Collapsed);

	UVerticalBox* PanelLayout = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("PanelLayout"));
	PanelLayout->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	UTextBlock* HeaderText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("InventoryHeaderText"));
	HeaderText->SetText(NSLOCTEXT("LostWithin", "InventoryHeader", "Placeholder Inventory   (Press [Tab] to Close)"));
	HeaderText->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 18));
	HeaderText->SetColorAndOpacity(FSlateColor(FLinearColor(0.98f, 0.85f, 0.48f, 1.0f)));
	HeaderText->SetVisibility(ESlateVisibility::HitTestInvisible);
	if (UVerticalBoxSlot* HeaderSlot = PanelLayout->AddChildToVerticalBox(HeaderText))
	{
		HeaderSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 4.0f));
	}

	SlotSummaryText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("SlotSummaryText"));
	SlotSummaryText->SetFont(FCoreStyle::GetDefaultFontStyle("Regular", 12));
	SlotSummaryText->SetColorAndOpacity(FSlateColor(FLinearColor(0.72f, 0.76f, 0.84f, 1.0f)));
	SlotSummaryText->SetVisibility(ESlateVisibility::HitTestInvisible);
	if (UVerticalBoxSlot* SummarySlot = PanelLayout->AddChildToVerticalBox(SlotSummaryText))
	{
		SummarySlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 2.0f));
	}

	UTextBlock* ActionHintText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("InventoryActionHintText"));
	ActionHintText->SetText(NSLOCTEXT("LostWithin", "InventoryActionHint", "[RMB] an item to Drop  |  Drag outside panel to Drop"));
	ActionHintText->SetFont(FCoreStyle::GetDefaultFontStyle("Regular", 11));
	ActionHintText->SetColorAndOpacity(FSlateColor(FLinearColor(0.60f, 0.66f, 0.76f, 1.0f)));
	ActionHintText->SetVisibility(ESlateVisibility::HitTestInvisible);
	if (UVerticalBoxSlot* ActionHintSlot = PanelLayout->AddChildToVerticalBox(ActionHintText))
	{
		ActionHintSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 10.0f));
	}

	UScrollBox* ItemScrollBox = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("ItemScrollBox"));
	ItemScrollBox->SetVisibility(ESlateVisibility::Visible);
	ItemListContainer = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ItemListContainer"));
	ItemListContainer->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	ItemScrollBox->AddChild(ItemListContainer);

	if (UVerticalBoxSlot* ScrollSlot = PanelLayout->AddChildToVerticalBox(ItemScrollBox))
	{
		ScrollSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}

	InventoryPanelBorder->SetContent(PanelLayout);

	if (UCanvasPanelSlot* PanelSlot = RootCanvas->AddChildToCanvas(InventoryPanelBorder))
	{
		PanelSlot->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
		PanelSlot->SetAlignment(FVector2D(0.5f, 0.5f));
		PanelSlot->SetSize(FVector2D(540.0f, 420.0f));
	}

	// Drag-outside world-drop indicator banner
	DropOutsideBanner = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("DropOutsideBanner"));
	DropOutsideBanner->SetBrushColor(FLinearColor(0.50f, 0.14f, 0.12f, 0.88f));
	DropOutsideBanner->SetPadding(FMargin(16.0f, 8.0f));
	DropOutsideBanner->SetVisibility(ESlateVisibility::Collapsed);
	UTextBlock* DropOutsideBannerText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("DropOutsideBannerText"));
	DropOutsideBannerText->SetText(NSLOCTEXT("LostWithin", "DropOutsideBanner", "Release [LMB] to Drop Item into World"));
	DropOutsideBannerText->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 13));
	DropOutsideBannerText->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.90f, 0.88f, 1.0f)));
	DropOutsideBannerText->SetVisibility(ESlateVisibility::HitTestInvisible);
	DropOutsideBanner->SetContent(DropOutsideBannerText);
	if (UCanvasPanelSlot* BannerSlot = RootCanvas->AddChildToCanvas(DropOutsideBanner))
	{
		BannerSlot->SetAnchors(FAnchors(0.5f, 0.85f, 0.5f, 0.85f));
		BannerSlot->SetAlignment(FVector2D(0.5f, 0.5f));
		BannerSlot->SetAutoSize(true);
	}

	// Floating [RMB] drop context dialog (highest ZOrder inside RootCanvas)
	ContextMenuBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ContextMenuBorder"));
	ContextMenuBorder->SetBrushColor(FLinearColor(0.07f, 0.09f, 0.14f, 0.98f));
	ContextMenuBorder->SetPadding(FMargin(6.0f));
	ContextMenuBorder->SetVisibility(ESlateVisibility::Collapsed);

	UVerticalBox* MenuLayout = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ContextMenuLayout"));

	ContextItemTitleText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ContextItemTitleText"));
	ContextItemTitleText->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 11));
	ContextItemTitleText->SetColorAndOpacity(FSlateColor(FLinearColor(0.95f, 0.84f, 0.50f, 1.0f)));
	ContextItemTitleText->SetVisibility(ESlateVisibility::HitTestInvisible);
	if (UVerticalBoxSlot* TitleSlot = MenuLayout->AddChildToVerticalBox(ContextItemTitleText))
	{
		TitleSlot->SetPadding(FMargin(8.0f, 2.0f, 8.0f, 6.0f));
	}

	ContextDropPrimaryButton = CreateContextMenuButton(
		MenuLayout,
		TEXT("ContextDropPrimaryButton"),
		NSLOCTEXT("LostWithin", "ContextDropSingle", "Drop"),
		&ContextDropPrimaryText);
	ContextDropPrimaryButton->OnClicked.AddDynamic(this, &UInventoryWidget::HandleContextDropPrimaryClicked);

	ContextDropAllButton = CreateContextMenuButton(
		MenuLayout,
		TEXT("ContextDropAllButton"),
		NSLOCTEXT("LostWithin", "ContextDropAll", "Drop All"));
	ContextDropAllButton->OnClicked.AddDynamic(this, &UInventoryWidget::HandleContextDropAllClicked);
	ContextDropAllButton->SetVisibility(ESlateVisibility::Collapsed);

	ContextMenuBorder->SetContent(MenuLayout);

	if (UCanvasPanelSlot* MenuSlot = RootCanvas->AddChildToCanvas(ContextMenuBorder))
	{
		MenuSlot->SetAnchors(FAnchors(0.0f, 0.0f, 0.0f, 0.0f));
		MenuSlot->SetAlignment(FVector2D(0.0f, 0.0f));
		MenuSlot->SetAutoSize(true);
		MenuSlot->SetZOrder(20);
	}
}
