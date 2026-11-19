#include "UI/LostWithinModalWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/SizeBox.h"
#include "Components/SizeBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/UniformGridPanel.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "InputCoreTypes.h"
#include "Interaction/EscapeDoorComponent.h"
#include "LostWithinPlayerController.h"
#include "Styling/SlateTypes.h"
#include "UI/LostWithinMenu.h"

void ULostWithinDigitButton::Configure(ULostWithinModalWidget* Panel, int32 Value)
{
	OwnerPanel = Panel;
	Digit = Value;
	OnClicked.AddUniqueDynamic(this, &ULostWithinDigitButton::PressDigit);
}

void ULostWithinDigitButton::PressDigit()
{
	if (GetWorld())
	{
		PlayLostWithinUIClickSound(this);
	}
	if (OwnerPanel)
	{
		OwnerPanel->AppendDigit(Digit);
		OwnerPanel->SetKeyboardFocus();
	}
}

ULostWithinModalWidget::ULostWithinModalWidget(const FObjectInitializer& Initializer) : Super(Initializer)
{
	SetIsFocusable(true);
}

void ULostWithinModalWidget::ConfigureKeypad(UEscapeDoorComponent* InDoor)
{
	Door = InDoor;
	bKeypad = true;
	EnteredCode.Empty();
}

TSharedRef<SWidget> ULostWithinModalWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("GameplayModal"));
		WidgetTree->RootWidget = Canvas;
		UBorder* Backdrop = WidgetTree->ConstructWidget<UBorder>();
		Backdrop->SetVisibility(ESlateVisibility::Visible);
		Backdrop->SetBrushColor(FLinearColor(0.0f, 0.0f, 0.0f, bKeypad ? 0.55f : 0.88f));
		Backdrop->SetHorizontalAlignment(HAlign_Center);
		Backdrop->SetVerticalAlignment(VAlign_Center);
		if (UCanvasPanelSlot* Placement = Canvas->AddChildToCanvas(Backdrop))
		{
			Placement->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f)); Placement->SetOffsets(FMargin(0.0f));
		}
		UBorder* PanelOutline = WidgetTree->ConstructWidget<UBorder>();
		PanelOutline->SetBrushColor(FLinearColor(0.43f, 0.11f, 0.10f, 0.72f));
		PanelOutline->SetPadding(FMargin(1.5f));
		Backdrop->SetContent(PanelOutline);
		UBorder* PanelFrame = WidgetTree->ConstructWidget<UBorder>();
		PanelFrame->SetBrushColor(FLinearColor(0.043f, 0.047f, 0.063f, 0.94f));
		PanelFrame->SetPadding(FMargin(32.0f, 28.0f));
		PanelOutline->SetContent(PanelFrame);
		USizeBox* Width = WidgetTree->ConstructWidget<USizeBox>();
		Width->SetWidthOverride(440.0f);
		PanelFrame->SetContent(Width);
		UVerticalBox* Layout = WidgetTree->ConstructWidget<UVerticalBox>();
		Width->SetContent(Layout);
		auto MakeText = [this](FName Name, const FText& Text, int32 Size, const FLinearColor& Color = FLinearColor(0.94f, 0.91f, 0.84f, 1.0f))
		{
			UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
			Label->SetText(Text);
			Label->SetFont(MakeLostWithinCinzelFont(true, Size));
			Label->SetJustification(ETextJustify::Center);
			Label->SetAutoWrapText(true);
			Label->SetColorAndOpacity(FSlateColor(Color));
			Label->SetShadowOffset(FVector2D(1.5f, 2.0f));
			Label->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.85f));
			return Label;
		};
		auto AddButton = [this, Layout, &MakeText](FName Name, const FText& Text, bool bPrimaryAccent = false)
		{
			UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
			Button->SetStyle(MakeLostWithinHorrorButtonStyle(bPrimaryAccent));
			Button->SetContent(MakeText(NAME_None, Text, 20));
			if (UVerticalBoxSlot* Placement = Layout->AddChildToVerticalBox(Button))
			{
				Placement->SetPadding(FMargin(0.0f, 6.0f));
			}
			return Button;
		};
		UTextBlock* TitleLabel = MakeText(TEXT("ModalTitle"), bKeypad
			? NSLOCTEXT("LostWithin", "KeypadTitle", "EXIT ACCESS - SUBJECT 09")
			: NSLOCTEXT("LostWithin", "PauseTitle", "LOST WITHIN - PAUSED"), 28);
		if (UVerticalBoxSlot* TitleSlot = Layout->AddChildToVerticalBox(TitleLabel))
		{
			TitleSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 14.0f));
		}
		if (bKeypad)
		{
			CodeText = MakeText(TEXT("KeypadCode"), FText::GetEmpty(), 42);
			if (UVerticalBoxSlot* CodeSlot = Layout->AddChildToVerticalBox(CodeText))
			{
				CodeSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 6.0f));
			}
			FeedbackText = MakeText(TEXT("KeypadFeedback"), NSLOCTEXT("LostWithin", "KeypadLiveWarning", "Enter the three digits. Stay alert - the facility is still active."), 16, FLinearColor(0.84f, 0.80f, 0.72f, 1.0f));
			if (UVerticalBoxSlot* FeedbackSlot = Layout->AddChildToVerticalBox(FeedbackText))
			{
				FeedbackSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 12.0f));
			}
			UUniformGridPanel* Grid = WidgetTree->ConstructWidget<UUniformGridPanel>();
			Grid->SetSlotPadding(FMargin(5.0f));
			if (UVerticalBoxSlot* GridSlot = Layout->AddChildToVerticalBox(Grid))
			{
				GridSlot->SetHorizontalAlignment(HAlign_Center);
				GridSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 10.0f));
			}
			FButtonStyle DigitStyle = MakeLostWithinHorrorButtonStyle(false);
			DigitStyle.SetNormalPadding(FMargin(0.0f));
			DigitStyle.SetPressedPadding(FMargin(0.0f, 1.0f, 0.0f, 0.0f));
			for (int32 Index = 0; Index < 10; ++Index)
			{
				const int32 Digit = (Index + 1) % 10;
				ULostWithinDigitButton* Button = WidgetTree->ConstructWidget<ULostWithinDigitButton>(ULostWithinDigitButton::StaticClass(), *FString::Printf(TEXT("KeypadDigit%d"), Digit));
				Button->Configure(this, Digit);
				Button->SetStyle(DigitStyle);
				USizeBox* Cell = WidgetTree->ConstructWidget<USizeBox>(); Cell->SetHeightOverride(48.0f); Cell->SetWidthOverride(115.0f);
				UTextBlock* DigitLabel = MakeText(NAME_None, FText::AsNumber(Digit), 26);
				Cell->SetContent(DigitLabel);
				if (USizeBoxSlot* CellSlot = Cast<USizeBoxSlot>(DigitLabel->Slot))
				{
					CellSlot->SetHorizontalAlignment(HAlign_Center);
					CellSlot->SetVerticalAlignment(VAlign_Center);
				}
				Button->SetContent(Cell);
				Grid->AddChildToUniformGrid(Button, Index / 3, Index == 9 ? 1 : Index % 3);
			}
			AddButton(TEXT("KeypadBackspace"), NSLOCTEXT("LostWithin", "KeypadBackspace", "Backspace"))->OnClicked.AddDynamic(this, &ULostWithinModalWidget::HandleBackspace);
			AddButton(TEXT("KeypadSubmit"), NSLOCTEXT("LostWithin", "KeypadSubmit", "Enter code"), true)->OnClicked.AddDynamic(this, &ULostWithinModalWidget::HandleSubmit);
			UpdateCodeDisplay();
		}
		else
		{
			AddButton(TEXT("PauseResume"), NSLOCTEXT("LostWithin", "PauseResume", "Resume"), true)->OnClicked.AddDynamic(this, &ULostWithinModalWidget::HandleClose);
			AddButton(TEXT("PauseOptions"), NSLOCTEXT("LostWithin", "PauseOptions", "Options"))->OnClicked.AddDynamic(this, &ULostWithinModalWidget::HandleOptions);
			AddButton(TEXT("PauseRestart"), NSLOCTEXT("LostWithin", "PauseRestart", "Restart"))->OnClicked.AddDynamic(this, &ULostWithinModalWidget::HandleRestart);
			AddButton(TEXT("PauseMainMenu"), NSLOCTEXT("LostWithin", "PauseMainMenu", "Quit to Main Menu"))->OnClicked.AddDynamic(this, &ULostWithinModalWidget::HandleMainMenu);
		}
		if (bKeypad)
		{
			AddButton(TEXT("KeypadClose"), NSLOCTEXT("LostWithin", "KeypadClose", "Step back [Esc]"))->OnClicked.AddDynamic(this, &ULostWithinModalWidget::HandleClose);
		}
	}
	return Super::RebuildWidget();
}

void ULostWithinModalWidget::UpdateCodeDisplay()
{
	FString Display;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		Display += Index < EnteredCode.Len() ? FString::Chr(EnteredCode[Index]) : TEXT("_");
		if (Index < 2) { Display += TEXT("  "); }
	}
	if (CodeText) { CodeText->SetText(FText::FromString(Display)); }
}

void ULostWithinModalWidget::AppendDigit(int32 Digit)
{
	if (bKeypad && Digit >= 0 && Digit <= 9 && EnteredCode.Len() < 3)
	{
		EnteredCode.AppendChar(static_cast<TCHAR>(TEXT('0') + Digit)); UpdateCodeDisplay();
	}
}

void ULostWithinModalWidget::Backspace()
{
	if (!EnteredCode.IsEmpty()) { EnteredCode.LeftChopInline(1); UpdateCodeDisplay(); }
}

void ULostWithinModalWidget::SubmitCode()
{
	if (!bKeypad || !Door.IsValid()) { return; }
	if (EnteredCode.Len() != 3)
	{
		if (FeedbackText) { FeedbackText->SetText(NSLOCTEXT("LostWithin", "KeypadIncomplete", "Enter all three digits.")); }
		return;
	}
	if (!Door->TrySubmitCode(EnteredCode, GetOwningPlayerPawn()))
	{
		if (FeedbackText) { FeedbackText->SetText(NSLOCTEXT("LostWithin", "KeypadIncorrect", "INCORRECT CODE — DOOR LOCKED")); }
		EnteredCode.Empty(); UpdateCodeDisplay();
	}
}

FText ULostWithinModalWidget::GetFeedback() const { return FeedbackText ? FeedbackText->GetText() : FText::GetEmpty(); }

FReply ULostWithinModalWidget::NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
	if (Event.IsRepeat()) { return FReply::Handled(); }
	const FKey Key = Event.GetKey();
	if (Key == EKeys::Escape) { HandleClose(); return FReply::Handled(); }
	if (bKeypad)
	{
		const FKey MainKeys[] = {EKeys::Zero,EKeys::One,EKeys::Two,EKeys::Three,EKeys::Four,EKeys::Five,EKeys::Six,EKeys::Seven,EKeys::Eight,EKeys::Nine};
		const FKey PadKeys[] = {EKeys::NumPadZero,EKeys::NumPadOne,EKeys::NumPadTwo,EKeys::NumPadThree,EKeys::NumPadFour,EKeys::NumPadFive,EKeys::NumPadSix,EKeys::NumPadSeven,EKeys::NumPadEight,EKeys::NumPadNine};
		for (int32 Digit = 0; Digit < 10; ++Digit) { if (Key == MainKeys[Digit] || Key == PadKeys[Digit]) { AppendDigit(Digit); return FReply::Handled(); } }
		if (Key == EKeys::BackSpace || Key == EKeys::Delete) { Backspace(); return FReply::Handled(); }
		if (Key == EKeys::Enter) { SubmitCode(); return FReply::Handled(); }
	}
	return Super::NativeOnPreviewKeyDown(Geometry, Event);
}

FReply ULostWithinModalWidget::NativeOnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& MouseEvent)
{
	SetKeyboardFocus();
	return FReply::Handled();
}

void ULostWithinModalWidget::HandleClose()
{
	if (GetWorld()) { PlayLostWithinUIClickSound(this); }
	if (ALostWithinPlayerController* PC = GetOwningPlayer<ALostWithinPlayerController>()) { PC->HandleEscape(); }
}

void ULostWithinModalWidget::HandleOptions()
{
	if (GetWorld()) { PlayLostWithinUIClickSound(this); }
	if (ALostWithinPlayerController* PC = GetOwningPlayer<ALostWithinPlayerController>()) { PC->OpenPauseOptions(); }
}

void ULostWithinModalWidget::HandleRestart()
{
	if (GetWorld()) { PlayLostWithinUIClickSound(this); }
	if (ALostWithinPlayerController* PC = GetOwningPlayer<ALostWithinPlayerController>()) { PC->RestartRun(); }
}

void ULostWithinModalWidget::HandleMainMenu()
{
	if (GetWorld()) { PlayLostWithinUIClickSound(this); }
	if (ALostWithinPlayerController* PC = GetOwningPlayer<ALostWithinPlayerController>()) { PC->ReturnToMainMenu(); }
}

void ULostWithinModalWidget::HandleSubmit()
{
	if (GetWorld()) { PlayLostWithinUIClickSound(this); }
	SubmitCode();
	SetKeyboardFocus();
}

void ULostWithinModalWidget::HandleBackspace()
{
	if (GetWorld()) { PlayLostWithinUIClickSound(this); }
	Backspace();
	SetKeyboardFocus();
}
