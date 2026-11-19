// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/GameEndWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "LostWithinPlayerController.h"
#include "Styling/SlateTypes.h"
#include "UI/LostWithinMenu.h"

UGameEndWidget::UGameEndWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetIsFocusable(true);
}

TSharedRef<SWidget> UGameEndWidget::RebuildWidget()
{
	BuildWidgetTreeIfNeeded();
	ApplyTexts();
	return Super::RebuildWidget();
}

void UGameEndWidget::Configure(bool bInEscaped)
{
	bEscaped = bInEscaped;
	ApplyTexts();
}

FText UGameEndWidget::GetTitle() const
{
	return bEscaped
		? NSLOCTEXT("LostWithin", "EndTitleEscaped", "EXPERIMENT CONCLUDED")
		: NSLOCTEXT("LostWithin", "EndTitleDied", "YOU WERE CAUGHT");
}

void UGameEndWidget::ApplyTexts()
{
	if (TitleText)
	{
		TitleText->SetText(GetTitle());
		TitleText->SetColorAndOpacity(FSlateColor(bEscaped
			? FLinearColor(0.93f, 0.86f, 0.62f, 1.0f)
			: FLinearColor(0.78f, 0.08f, 0.06f, 1.0f)));
	}
	if (SubtitleText)
	{
		SubtitleText->SetText(bEscaped
			? NSLOCTEXT("LostWithin", "EndSubtitleEscaped", "SUBJECT 09 ESCAPED\n\nYou outsmarted Dr. Victor Vance's four mutated underlings, decoded the labyrinth, and escaped the Happy Virus containment facility alive.\n\nLOST WITHIN")
			: NSLOCTEXT("LostWithin", "EndSubtitleDied", "Dr. Vance's experiment claims another victim...\nSUBJECT 09 TERMINATED"));
	}
	if (RestartLabel)
	{
		RestartLabel->SetText(bEscaped
			? NSLOCTEXT("LostWithin", "EndPlayAgain", "Play again")
			: NSLOCTEXT("LostWithin", "EndTryAgain", "Try again"));
	}
	if (QuitLabel)
	{
		QuitLabel->SetText(NSLOCTEXT("LostWithin", "EndQuit", "Main Menu"));
	}
}

UButton* UGameEndWidget::CreateMenuButton(const FName Name, TObjectPtr<UTextBlock>& OutLabel, bool bPrimaryAccent)
{
	UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
	Button->SetStyle(MakeLostWithinHorrorButtonStyle(bPrimaryAccent));

	OutLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	OutLabel->SetFont(MakeLostWithinCinzelFont(true, 18));
	OutLabel->SetColorAndOpacity(FSlateColor(FLinearColor(0.95f, 0.93f, 0.90f, 1.0f)));
	OutLabel->SetJustification(ETextJustify::Center);
	OutLabel->SetShadowOffset(FVector2D(1.0f, 1.5f));
	OutLabel->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.85f));
	Button->SetContent(OutLabel);
	return Button;
}

void UGameEndWidget::BuildWidgetTreeIfNeeded()
{
	if (!WidgetTree || WidgetTree->RootWidget)
	{
		return;
	}

	UCanvasPanel* RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("EndRootCanvas"));
	WidgetTree->RootWidget = RootCanvas;

	UBorder* Backdrop = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("EndBackdrop"));
	Backdrop->SetVisibility(ESlateVisibility::Visible);
	Backdrop->SetBrushColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.86f));
	Backdrop->SetHorizontalAlignment(HAlign_Center);
	Backdrop->SetVerticalAlignment(VAlign_Center);
	if (UCanvasPanelSlot* BackdropSlot = RootCanvas->AddChildToCanvas(Backdrop))
	{
		BackdropSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
		BackdropSlot->SetOffsets(FMargin(0.0f));
	}

	UVerticalBox* Layout = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("EndLayout"));
	Backdrop->SetContent(Layout);

	TitleText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("EndTitle"));
	TitleText->SetFont(MakeLostWithinCinzelFont(true, 58));
	TitleText->SetJustification(ETextJustify::Center);
	TitleText->SetShadowOffset(FVector2D(2.0f, 2.0f));
	TitleText->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.9f));
	if (UVerticalBoxSlot* TitleSlot = Layout->AddChildToVerticalBox(TitleText))
	{
		TitleSlot->SetHorizontalAlignment(HAlign_Center);
		TitleSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));
	}

	SubtitleText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("EndSubtitle"));
	SubtitleText->SetFont(MakeLostWithinCinzelFont(false, 20));
	SubtitleText->SetColorAndOpacity(FSlateColor(FLinearColor(0.84f, 0.81f, 0.76f, 1.0f)));
	SubtitleText->SetJustification(ETextJustify::Center);
	SubtitleText->SetAutoWrapText(true);
	SubtitleText->SetWrapTextAt(900.0f);
	SubtitleText->SetShadowOffset(FVector2D(1.0f, 1.5f));
	SubtitleText->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.85f));
	if (UVerticalBoxSlot* SubtitleSlot = Layout->AddChildToVerticalBox(SubtitleText))
	{
		SubtitleSlot->SetHorizontalAlignment(HAlign_Center);
		SubtitleSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 36.0f));
	}

	USizeBox* RestartBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("EndRestartBox"));
	RestartBox->SetWidthOverride(280.0f);
	RestartButton = CreateMenuButton(TEXT("EndRestartButton"), RestartLabel, true);
	RestartButton->OnClicked.AddDynamic(this, &UGameEndWidget::HandleRestartClicked);
	RestartBox->SetContent(RestartButton);
	if (UVerticalBoxSlot* RestartSlot = Layout->AddChildToVerticalBox(RestartBox))
	{
		RestartSlot->SetHorizontalAlignment(HAlign_Center);
		RestartSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 12.0f));
	}

	USizeBox* QuitBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("EndQuitBox"));
	QuitBox->SetWidthOverride(280.0f);
	QuitButton = CreateMenuButton(TEXT("EndQuitButton"), QuitLabel, false);
	QuitButton->OnClicked.AddDynamic(this, &UGameEndWidget::HandleQuitClicked);
	QuitBox->SetContent(QuitButton);
	if (UVerticalBoxSlot* QuitSlot = Layout->AddChildToVerticalBox(QuitBox))
	{
		QuitSlot->SetHorizontalAlignment(HAlign_Center);
	}
}

FReply UGameEndWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	SetKeyboardFocus();
	return FReply::Handled();
}

void UGameEndWidget::HandleRestartClicked()
{
	if (GetWorld())
	{
		PlayLostWithinUIClickSound(this);
	}
	if (ALostWithinPlayerController* PlayerController = GetOwningPlayer<ALostWithinPlayerController>())
	{
		PlayerController->RestartRun();
	}
}

void UGameEndWidget::HandleQuitClicked()
{
	if (GetWorld())
	{
		PlayLostWithinUIClickSound(this);
	}
	if (ALostWithinPlayerController* PlayerController = GetOwningPlayer<ALostWithinPlayerController>())
	{
		PlayerController->ReturnToMainMenu();
	}
}
