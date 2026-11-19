// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "InventoryWidget.generated.h"

class UBorder;
class UButton;
class UCanvasPanel;
class UDragDropOperation;
class UImage;
class UInventoryComponent;
class UTextBlock;
class UTexture2D;
class UVerticalBox;
struct FInventoryItem;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnInventoryCloseRequested);

/**
 *  Programmatic temporary HUD and Placeholder Inventory UI toggled with [Tab].
 *  Displays center crosshair, contextual interaction prompt, slot list, [RMB] drop dialog, and drag-outside dropping.
 */
UCLASS()
class LOSTWITHIN_API UInventoryWidget : public UUserWidget
{
	GENERATED_BODY()

public:

	UInventoryWidget(const FObjectInitializer& ObjectInitializer);

	/** Fired when the user presses [Tab] while UI has keyboard focus */
	UPROPERTY(BlueprintAssignable, Category = "Inventory|UI")
	FOnInventoryCloseRequested OnCloseRequested;

	/** Binds this widget to the player's inventory component and refreshes displayed items */
	UFUNCTION(BlueprintCallable, Category = "Inventory|UI")
	void BindInventory(UInventoryComponent* InInventoryComponent);

	/** Opens or closes the temporary Placeholder Inventory panel */
	UFUNCTION(BlueprintCallable, Category = "Inventory|UI")
	void SetInventoryOpen(bool bOpen);

	/** Returns true if the Placeholder Inventory panel is currently visible */
	UFUNCTION(BlueprintPure, Category = "Inventory|UI")
	bool IsInventoryOpen() const { return bIsInventoryOpen; }

	/** Updates the bottom-center interaction prompt (hides if empty) */
	UFUNCTION(BlueprintCallable, Category = "Interaction|UI")
	void SetInteractionPrompt(const FText& PromptText);

	/** Shows a paper note (title + handwritten text) in the middle of the screen */
	UFUNCTION(BlueprintCallable, Category = "Note|UI")
	void ShowNote(const FText& Title, const FText& Body, FName StoryID = NAME_None);

	/** Hides the note panel */
	UFUNCTION(BlueprintCallable, Category = "Note|UI")
	void HideNote();

	/** True while the note panel is visible */
	UFUNCTION(BlueprintPure, Category = "Note|UI")
	bool IsNoteVisible() const { return bIsNoteVisible; }

	/** The text of the note on screen (empty when hidden) */
	UFUNCTION(BlueprintPure, Category = "Note|UI")
	FText GetVisibleNoteText() const { return bIsNoteVisible ? VisibleNoteText : FText::GetEmpty(); }

	/** Shows a short message above the interaction prompt for DurationSeconds */
	UFUNCTION(BlueprintCallable, Category = "Interaction|UI")
	void ShowStatusMessage(const FText& Message, float DurationSeconds = 3.0f);

	/** The status message on screen (empty when hidden) */
	UFUNCTION(BlueprintPure, Category = "Interaction|UI")
	FText GetStatusMessage() const { return StatusMessage; }

	/** Formats a single flashlight bay percentage (-1 -> "Empty", 0..100 -> "XX%") */
	static FString FormatBatteryBayPercentString(float BayPercent);

	/** Updates the HUD tactical flashlight battery status readout */
	UFUNCTION(BlueprintCallable, Category = "Flashlight|UI")
	void UpdateFlashlightBatteryHUD(
		bool bHasFlashlight,
		bool bIsOn,
		int32 InstalledCount,
		float CombinedPercent,
		float Slot1Percent,
		float Slot2Percent);

	/** Loads BatterySlotIndex into the flashlight when dragged onto the Flashlight row */
	void HandleBatteryDroppedOnFlashlight(int32 BatterySlotIndex);

	/** Rebuilds the inventory item rows from the bound InventoryComponent */
	UFUNCTION(BlueprintCallable, Category = "Inventory|UI")
	void RefreshInventory();

	/** Opens the [RMB] drop dialog anchored near ScreenSpacePosition for SlotIndex */
	void OpenItemContextMenu(int32 SlotIndex, const FVector2D& ScreenSpacePosition);

	/** Hides the [RMB] drop dialog if currently open */
	void CloseItemContextMenu();

	/** Returns true if the [RMB] drop dialog is currently open */
	bool IsContextMenuOpen() const { return bIsContextMenuOpen; }

	/** Returns true if ScreenSpacePosition lies inside the centered Placeholder Inventory panel */
	bool IsScreenPositionInsideInventoryPanel(const FVector2D& ScreenSpacePosition) const;

	/** Handles drag cancellation from item rows (drops item if [LMB] was released outside the panel/viewport) */
	void HandleItemDragCancelled(UDragDropOperation* InOperation, const FDragDropEvent& InDragDropEvent);

protected:

	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply NativeOnPreviewMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual bool NativeOnDragOver(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;
	virtual void NativeOnDragLeave(const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;
	virtual bool NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;

private:

	UFUNCTION()
	void HandleInventoryUpdated();

	UFUNCTION()
	void HandleContextChangeBatteryClicked();

	UFUNCTION()
	void HandleContextDropPrimaryClicked();

	UFUNCTION()
	void HandleContextDropAllClicked();

	UFUNCTION()
	void HandleContextReadClicked();

	UFUNCTION()
	void HandleNoteCloseClicked();

	void HideStatusMessage();

	/** Ensures the dynamic UMG widget hierarchy exists before accessing pointers */
	void BuildWidgetTreeIfNeeded();
	void BuildHotbar(UCanvasPanel* RootCanvas);
	void RefreshHotbar(const TArray<FInventoryItem>& Items);

	/** Creates a styled button inside ContextMenuBorder */
	UButton* CreateContextMenuButton(UVerticalBox* ParentBox, const FName ButtonName, const FText& DefaultLabel, TObjectPtr<UTextBlock>* OutLabelText = nullptr);

	/** Shows or clears visual feedback when dragging an item outside InventoryPanelBorder */
	void SetDragOutsideFeedback(bool bShowOutsideDropFeedback);

	/** Drops Quantity units (or entire stack if <= 0) from ContextMenuSlotIndex and closes the menu */
	void ExecuteContextDrop(int32 QuantityToDrop);

	UPROPERTY(Transient)
	TWeakObjectPtr<UInventoryComponent> BoundInventory;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> OutsideDropCatcher = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> CrosshairText = nullptr;

	UPROPERTY()
	TSubclassOf<UUserWidget> HotbarClass;

	UPROPERTY()
	TObjectPtr<UTexture2D> EmptyHotbarIcon;

	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> HotbarView;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UImage>> HotbarImages;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> HotbarItemNames;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> InventoryPanelBorder = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> SlotSummaryText = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> ItemListContainer = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> InteractionPromptBorder = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> InteractionPromptText = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> FlashlightBatteryBorder = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> FlashlightBatteryText = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> DropOutsideBanner = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> ContextMenuBorder = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ContextItemTitleText = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UButton> ContextChangeBatteryButton = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ContextChangeBatteryText = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UButton> ContextDropPrimaryButton = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ContextDropPrimaryText = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UButton> ContextDropAllButton = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UButton> ContextReadButton = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> NotePanelBorder = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> NoteTitleText = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> NoteBodyText = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UButton> NoteCloseButton = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> StatusMessageBorder = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> StatusMessageText = nullptr;

	FText VisibleNoteText;
	FName VisibleStoryNoteID = NAME_None;
	FText StatusMessage;
	FTimerHandle StatusMessageTimerHandle;
	bool bIsNoteVisible = false;

	bool bIsInventoryOpen = false;
	bool bIsContextMenuOpen = false;
	bool bIsDragOutsideHighlighted = false;
	int32 ContextMenuSlotIndex = INDEX_NONE;
	FText PendingPromptText;
};

