// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/InventoryWidget.h"
#include "Gameplay/LostWithinNarrativeComponent.h"
#include "LostWithinPlayerController.h"
#include "Blueprint/DragDropOperation.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateColorBrush.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "InputCoreTypes.h"
#include "Interaction/PickupItemActor.h"
#include "Inventory/InventoryComponent.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateTypes.h"
#include "Engine/World.h"
#include "Engine/Texture2D.h"
#include "TimerManager.h"
#include "UI/InventoryDragDropOperation.h"
#include "UI/InventoryItemRowWidget.h"
#include "UObject/ConstructorHelpers.h"

UInventoryWidget::UInventoryWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetIsFocusable(false);
	static ConstructorHelpers::FClassFinder<UUserWidget> StagedHotbar(TEXT("/Game/UI/Inventory/WBP_HUD"));
	HotbarClass = StagedHotbar.Class;
	static ConstructorHelpers::FObjectFinder<UTexture2D> SlotTexture(TEXT("/Game/UI/Inventory/Textures/blank.blank"));
	EmptyHotbarIcon = SlotTexture.Object;
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
	if (HotbarView)
	{
		HotbarView->SetVisibility(bIsInventoryOpen ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}

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

FString UInventoryWidget::FormatBatteryBayPercentString(float BayPercent)
{
	if (BayPercent < 0.0f)
	{
		return TEXT("Empty");
	}
	return FString::Printf(TEXT("%d%%"), FMath::Clamp(FMath::RoundToInt(BayPercent), 0, 100));
}

void UInventoryWidget::UpdateFlashlightBatteryHUD(
	bool bHasFlashlight,
	bool bIsOn,
	int32 InstalledCount,
	float CombinedPercent,
	float Slot1Percent,
	float Slot2Percent)
{
	if (!FlashlightBatteryBorder || !FlashlightBatteryText)
	{
		return;
	}

	if (!bHasFlashlight)
	{
		FlashlightBatteryBorder->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}

	const FString Cell1Label = FormatBatteryBayPercentString(Slot1Percent);
	const FString Cell2Label = FormatBatteryBayPercentString(Slot2Percent);
	const int32 RoundedTotal = FMath::Clamp(FMath::RoundToInt(CombinedPercent), 0, 100);

	FString StatusString;
	FLinearColor TextTint(0.95f, 0.90f, 0.72f, 1.0f);
	if (InstalledCount < 2)
	{
		StatusString = FString::Printf(TEXT("Flashlight: Need 2 Batteries (%d/2)  [%s | %s]  [R] Load"), InstalledCount, *Cell1Label, *Cell2Label);
		TextTint = FLinearColor(0.96f, 0.54f, 0.42f, 1.0f);
	}
	else if (CombinedPercent <= KINDA_SMALL_NUMBER)
	{
		StatusString = FString::Printf(TEXT("Flashlight: Depleted (0%%)  [%s | %s]  [R] Replace"), *Cell1Label, *Cell2Label);
		TextTint = FLinearColor(0.96f, 0.44f, 0.36f, 1.0f);
	}
	else
	{
		StatusString = FString::Printf(
			TEXT("Flashlight [%s]: %d%%  (%s | %s)"),
			bIsOn ? TEXT("ON") : TEXT("OFF"),
			RoundedTotal,
			*Cell1Label,
			*Cell2Label);
		if (CombinedPercent <= 25.0f)
		{
			TextTint = FLinearColor(0.98f, 0.74f, 0.36f, 1.0f);
		}
		else
		{
			TextTint = FLinearColor(0.86f, 0.96f, 0.80f, 1.0f);
		}
	}

	FlashlightBatteryText->SetText(FText::FromString(StatusString));
	FlashlightBatteryText->SetColorAndOpacity(FSlateColor(TextTint));
	FlashlightBatteryBorder->SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UInventoryWidget::HandleBatteryDroppedOnFlashlight(int32 BatterySlotIndex)
{
	if (UInventoryComponent* Inventory = BoundInventory.Get())
	{
		Inventory->ReplaceFlashlightBatteryWithSlot(BatterySlotIndex);
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
	RefreshHotbar(Items);

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
		EmptyText->SetText(NSLOCTEXT("LostWithin", "EmptyInventory", "Inventory is empty. Pick up items with [E]."));
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
	if (!bIsInventoryOpen || !ContextMenuBorder || !ContextChangeBatteryButton || !ContextChangeBatteryText || !ContextDropPrimaryButton || !ContextDropPrimaryText || !ContextDropAllButton)
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
		FText DisplayName = Item.DisplayName.IsEmpty() ? FText::FromName(Item.ItemID) : Item.DisplayName;
		if (Item.ItemID == APickupItemActor::BatteryItemID)
		{
			DisplayName = FText::FromString(FString::Printf(TEXT("%s (%s)"), *DisplayName.ToString(), *FormatBatteryBayPercentString(Item.BatteryChargePercent)));
		}
		ContextItemTitleText->SetText(DisplayName);
	}

	bool bShowBatteryAction = false;
	if (Item.ItemID == APickupItemActor::FlashlightItemID && Inventory->CanReloadFlashlight())
	{
		bShowBatteryAction = true;
		ContextChangeBatteryText->SetText(NSLOCTEXT("LostWithin", "ContextChangeBattery", "Change Battery"));
	}
	else if (Item.ItemID == APickupItemActor::BatteryItemID
		&& Inventory->HasItem(APickupItemActor::FlashlightItemID)
		&& Inventory->CanReloadFlashlight(SlotIndex))
	{
		bShowBatteryAction = true;
		int32 InstalledCount = 0;
		float UnusedCombined = 0.0f;
		float UnusedSlot1 = 0.0f;
		float UnusedSlot2 = 0.0f;
		Inventory->GetFlashlightBatteryState(InstalledCount, UnusedCombined, UnusedSlot1, UnusedSlot2);
		ContextChangeBatteryText->SetText(InstalledCount < 2
			? NSLOCTEXT("LostWithin", "ContextLoadBattery", "Load into Flashlight")
			: NSLOCTEXT("LostWithin", "ContextSwapBattery", "Swap Battery"));
	}
	ContextChangeBatteryButton->SetVisibility(bShowBatteryAction ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);

	const bool bShowReadAction = APickupItemActor::IsNoteItemID(Item.ItemID);
	if (ContextReadButton)
	{
		ContextReadButton->SetVisibility(bShowReadAction ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
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
		const float EstimatedHeight = 80.0f + (Item.Quantity > 1 ? 36.0f : 0.0f) + (bShowBatteryAction ? 36.0f : 0.0f) + (bShowReadAction ? 36.0f : 0.0f);
		const FVector2D EstimatedMenuSize(185.0f, EstimatedHeight);

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
	if (!InKeyEvent.IsRepeat() && bIsNoteVisible && InKeyEvent.GetKey() == EKeys::E)
	{
		HideNote(); return FReply::Handled();
	}
	if (!InKeyEvent.IsRepeat() && InKeyEvent.GetKey() == EKeys::Escape && !bIsContextMenuOpen)
	{
		if (ALostWithinPlayerController* PC = GetOwningPlayer<ALostWithinPlayerController>())
		{
			PC->HandleEscape(); return FReply::Handled();
		}
	}
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

void UInventoryWidget::HandleContextChangeBatteryClicked()
{
	const int32 TargetSlotIndex = ContextMenuSlotIndex;
	CloseItemContextMenu();

	if (TargetSlotIndex == INDEX_NONE)
	{
		return;
	}

	if (UInventoryComponent* Inventory = BoundInventory.Get())
	{
		if (Inventory->GetItems().IsValidIndex(TargetSlotIndex))
		{
			const FInventoryItem& ClickedItem = Inventory->GetItems()[TargetSlotIndex];
			if (ClickedItem.ItemID == APickupItemActor::FlashlightItemID)
			{
				Inventory->ReloadBestBatteryIntoFlashlight();
			}
			else if (ClickedItem.ItemID == APickupItemActor::BatteryItemID)
			{
				Inventory->ReplaceFlashlightBatteryWithSlot(TargetSlotIndex);
			}
		}
	}
}

void UInventoryWidget::HandleContextReadClicked()
{
	const int32 TargetSlotIndex = ContextMenuSlotIndex;
	CloseItemContextMenu();

	const UInventoryComponent* Inventory = BoundInventory.Get();
	if (Inventory && Inventory->GetItems().IsValidIndex(TargetSlotIndex))
	{
		const FInventoryItem& Item = Inventory->GetItems()[TargetSlotIndex];
		ShowNote(Item.DisplayName, Item.Description, Item.StoryNoteID);
	}
}

void UInventoryWidget::HandleNoteCloseClicked()
{
	HideNote();
}

void UInventoryWidget::ShowNote(const FText& Title, const FText& Body, FName StoryID)
{
	if (bIsNoteVisible)
	{
		HideNote();
	}
	BuildWidgetTreeIfNeeded();
	VisibleNoteText = Body;
	VisibleStoryNoteID = StoryID;
	bIsNoteVisible = true;
	if (APawn* Pawn = GetOwningPlayerPawn())
	{
		if (ULostWithinNarrativeComponent* Story = Pawn->FindComponentByClass<ULostWithinNarrativeComponent>())
		{
			Story->NoteOpened(StoryID);
		}
	}
	if (NoteTitleText)
	{
		NoteTitleText->SetText(Title.IsEmpty() ? NSLOCTEXT("LostWithin", "NoteDefaultTitle", "Note") : Title);
	}
	if (NoteBodyText)
	{
		NoteBodyText->SetText(Body);
	}
	if (NotePanelBorder)
	{
		NotePanelBorder->SetVisibility(ESlateVisibility::Visible);
	}
}

void UInventoryWidget::HideNote()
{
	if (!bIsNoteVisible)
	{
		return;
	}
	const FName ClosedID = VisibleStoryNoteID;
	VisibleStoryNoteID = NAME_None;
	bIsNoteVisible = false;
	VisibleNoteText = FText::GetEmpty();
	if (NotePanelBorder)
	{
		NotePanelBorder->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (APawn* Pawn = GetOwningPlayerPawn())
	{
		if (ULostWithinNarrativeComponent* Story = Pawn->FindComponentByClass<ULostWithinNarrativeComponent>())
		{
			Story->NoteClosed(ClosedID);
		}
	}
}

void UInventoryWidget::ShowStatusMessage(const FText& Message, float DurationSeconds)
{
	BuildWidgetTreeIfNeeded();
	StatusMessage = Message;
	if (StatusMessageText && StatusMessageBorder)
	{
		StatusMessageText->SetText(Message);
		StatusMessageBorder->SetVisibility(Message.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(StatusMessageTimerHandle);
		if (!Message.IsEmpty() && DurationSeconds > 0.0f)
		{
			World->GetTimerManager().SetTimer(StatusMessageTimerHandle,
				FTimerDelegate::CreateUObject(this, &UInventoryWidget::HideStatusMessage), DurationSeconds, false);
		}
	}
}

void UInventoryWidget::HideStatusMessage()
{
	StatusMessage = FText::GetEmpty();
	if (StatusMessageBorder)
	{
		StatusMessageBorder->SetVisibility(ESlateVisibility::Collapsed);
	}
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

void UInventoryWidget::BuildHotbar(UCanvasPanel* RootCanvas)
{
	APlayerController* Controller = GetOwningPlayer();
	if (!Controller || !HotbarClass)
	{
		return;
	}

	HotbarView = CreateWidget<UUserWidget>(Controller, HotbarClass);
	if (!HotbarView)
	{
		return;
	}

	HotbarView->SetVisibility(ESlateVisibility::HitTestInvisible);
	if (UWidget* LegacyHelp = HotbarView->GetWidgetFromName(TEXT("TextBlock_56")))
	{
		LegacyHelp->SetVisibility(ESlateVisibility::Collapsed);
	}
	UCanvasPanelSlot* ViewSlot = RootCanvas->AddChildToCanvas(HotbarView);
	ViewSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
	ViewSlot->SetOffsets(FMargin(0.0f));

	constexpr int32 HotbarSlots = 6;
	HotbarImages.SetNum(HotbarSlots);
	HotbarItemNames.SetNum(HotbarSlots);
	for (int32 Index = 0; Index < HotbarSlots; ++Index)
	{
		const FString Suffix = Index == 0 ? FString() : FString::Printf(TEXT("_%d"), Index);
		UImage* Image = Cast<UImage>(HotbarView->GetWidgetFromName(FName(*(FString(TEXT("SlotImage")) + Suffix))));
		UBorder* Border = Cast<UBorder>(HotbarView->GetWidgetFromName(FName(*(FString(TEXT("SlotBorder")) + Suffix))));
		if (!Image || !Border)
		{
			continue;
		}

		UOverlay* Content = WidgetTree->ConstructWidget<UOverlay>();
		Border->SetContent(Content);
		Content->AddChildToOverlay(Image);
		const FName Name(*FString::Printf(TEXT("HotbarItemName_%d"), Index));
		UTextBlock* ItemName = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
		ItemName->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 11));
		ItemName->SetAutoWrapText(true);
		ItemName->SetJustification(ETextJustify::Center);
		ItemName->SetVisibility(ESlateVisibility::Collapsed);
		UOverlaySlot* NameSlot = Content->AddChildToOverlay(ItemName);
		NameSlot->SetHorizontalAlignment(HAlign_Center);
		NameSlot->SetVerticalAlignment(VAlign_Center);
		NameSlot->SetPadding(FMargin(4.0f));
		HotbarImages[Index] = Image;
		HotbarItemNames[Index] = ItemName;
	}
}

void UInventoryWidget::RefreshHotbar(const TArray<FInventoryItem>& Items)
{
	for (int32 Index = 0; Index < HotbarImages.Num(); ++Index)
	{
		UImage* Image = HotbarImages[Index];
		UTextBlock* ItemName = HotbarItemNames[Index];
		if (!Image || !ItemName)
		{
			continue;
		}

		const FInventoryItem* Item = Items.IsValidIndex(Index) ? &Items[Index] : nullptr;
		UTexture2D* Icon = Item ? Item->Icon.Get() : nullptr;
		Image->SetBrushFromTexture(Icon ? Icon : EmptyHotbarIcon.Get());
		ItemName->SetText(Item ? Item->DisplayName : FText::GetEmpty());
		ItemName->SetVisibility(Item && !Icon ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
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
	BuildHotbar(RootCanvas);

	// Fullscreen drop surface outside the inventory panel
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
	ControlsHintText->SetText(NSLOCTEXT("LostWithin", "ControlsHint", "[Tab] Inventory  |  [E] Interact  |  [F] Flashlight  |  [R] Change Battery  |  [Shift] Walk (quiet)  |  [Ctrl] Sprint (loud)"));
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

	// Top-right HUD flashlight battery status readout
	FlashlightBatteryBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("FlashlightBatteryBorder"));
	FlashlightBatteryBorder->SetBrushColor(FLinearColor(0.03f, 0.04f, 0.06f, 0.65f));
	FlashlightBatteryBorder->SetPadding(FMargin(12.0f, 6.0f));
	FlashlightBatteryBorder->SetVisibility(ESlateVisibility::Collapsed);
	FlashlightBatteryText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("FlashlightBatteryText"));
	FlashlightBatteryText->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 11));
	FlashlightBatteryText->SetColorAndOpacity(FSlateColor(FLinearColor(0.86f, 0.96f, 0.80f, 1.0f)));
	FlashlightBatteryText->SetVisibility(ESlateVisibility::HitTestInvisible);
	FlashlightBatteryBorder->SetContent(FlashlightBatteryText);
	if (UCanvasPanelSlot* BatterySlot = RootCanvas->AddChildToCanvas(FlashlightBatteryBorder))
	{
		BatterySlot->SetAnchors(FAnchors(1.0f, 0.0f, 1.0f, 0.0f));
		BatterySlot->SetAlignment(FVector2D(1.0f, 0.0f));
		BatterySlot->SetPosition(FVector2D(-16.0f, 16.0f));
		BatterySlot->SetAutoSize(true);
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

	// Centered inventory overlay panel ([Tab])
	InventoryPanelBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("InventoryPanelBorder"));
	InventoryPanelBorder->SetBrushColor(FLinearColor(0.04f, 0.05f, 0.08f, 0.92f));
	InventoryPanelBorder->SetPadding(FMargin(24.0f));
	InventoryPanelBorder->SetVisibility(ESlateVisibility::Collapsed);

	UVerticalBox* PanelLayout = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("PanelLayout"));
	PanelLayout->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	UTextBlock* HeaderText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("InventoryHeaderText"));
	HeaderText->SetText(NSLOCTEXT("LostWithin", "InventoryHeader", "Inventory   (Press [Tab] to Close)"));
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
	ActionHintText->SetText(NSLOCTEXT("LostWithin", "InventoryActionHint", "[RMB] an item to Read / Use / Drop  |  Drag Battery onto Flashlight  |  Drag outside panel to Drop"));
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

	ContextReadButton = CreateContextMenuButton(
		MenuLayout,
		TEXT("ContextReadButton"),
		NSLOCTEXT("LostWithin", "ContextRead", "Read"));
	ContextReadButton->OnClicked.AddDynamic(this, &UInventoryWidget::HandleContextReadClicked);
	ContextReadButton->SetVisibility(ESlateVisibility::Collapsed);

	ContextChangeBatteryButton = CreateContextMenuButton(
		MenuLayout,
		TEXT("ContextChangeBatteryButton"),
		NSLOCTEXT("LostWithin", "ContextChangeBattery", "Change Battery"),
		&ContextChangeBatteryText);
	ContextChangeBatteryButton->OnClicked.AddDynamic(this, &UInventoryWidget::HandleContextChangeBatteryClicked);
	ContextChangeBatteryButton->SetVisibility(ESlateVisibility::Collapsed);

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

	// Status message just above the interaction prompt ("Locked. It needs the Old Key.")
	StatusMessageBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("StatusMessageBorder"));
	StatusMessageBorder->SetBrushColor(FLinearColor(0.10f, 0.03f, 0.03f, 0.80f));
	StatusMessageBorder->SetPadding(FMargin(14.0f, 6.0f));
	StatusMessageBorder->SetVisibility(ESlateVisibility::Collapsed);
	StatusMessageText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("StatusMessageText"));
	StatusMessageText->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 13));
	StatusMessageText->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.86f, 0.80f, 1.0f)));
	StatusMessageText->SetJustification(ETextJustify::Center);
	StatusMessageText->SetVisibility(ESlateVisibility::HitTestInvisible);
	StatusMessageBorder->SetContent(StatusMessageText);
	if (UCanvasPanelSlot* StatusSlot = RootCanvas->AddChildToCanvas(StatusMessageBorder))
	{
		StatusSlot->SetAnchors(FAnchors(0.5f, 0.66f, 0.5f, 0.66f));
		StatusSlot->SetAlignment(FVector2D(0.5f, 0.5f));
		StatusSlot->SetAutoSize(true);
	}

	// Paper note reader
	NotePanelBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("NotePanelBorder"));
	NotePanelBorder->SetBrushColor(FLinearColor(0.80f, 0.74f, 0.60f, 0.97f));
	NotePanelBorder->SetPadding(FMargin(32.0f, 26.0f));
	NotePanelBorder->SetVisibility(ESlateVisibility::Collapsed);

	UVerticalBox* NoteLayout = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("NoteLayout"));
	NoteTitleText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("NoteTitleText"));
	NoteTitleText->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 13));
	NoteTitleText->SetColorAndOpacity(FSlateColor(FLinearColor(0.28f, 0.20f, 0.14f, 1.0f)));
	if (UVerticalBoxSlot* NoteTitleSlot = NoteLayout->AddChildToVerticalBox(NoteTitleText))
	{
		NoteTitleSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 12.0f));
	}

	NoteBodyText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("NoteBodyText"));
	NoteBodyText->SetFont(FCoreStyle::GetDefaultFontStyle("Italic", 20));
	NoteBodyText->SetColorAndOpacity(FSlateColor(FLinearColor(0.10f, 0.07f, 0.06f, 1.0f)));
	NoteBodyText->SetAutoWrapText(true);
	if (UVerticalBoxSlot* NoteBodySlot = NoteLayout->AddChildToVerticalBox(NoteBodyText))
	{
		NoteBodySlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 18.0f));
	}

	NoteCloseButton = CreateContextMenuButton(
		NoteLayout,
		TEXT("NoteCloseButton"),
		NSLOCTEXT("LostWithin", "NoteClose", "Put away  [E] / [Esc]"));
	NoteCloseButton->OnClicked.AddDynamic(this, &UInventoryWidget::HandleNoteCloseClicked);

	USizeBox* NoteWidth = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("NoteWidth"));
	NoteWidth->SetWidthOverride(700.0f);
	NoteWidth->SetContent(NoteLayout);
	NotePanelBorder->SetContent(NoteWidth);
	if (UCanvasPanelSlot* NoteSlot = RootCanvas->AddChildToCanvas(NotePanelBorder))
	{
		NoteSlot->SetAnchors(FAnchors(0.5f, 0.42f, 0.5f, 0.42f));
		NoteSlot->SetAlignment(FVector2D(0.5f, 0.5f));
		NoteSlot->SetAutoSize(true);
		NoteSlot->SetZOrder(30);
	}
}
