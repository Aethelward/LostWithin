// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/GameEndWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateColorBrush.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "LostWithinPlayerController.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateTypes.h"

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
		? NSLOCTEXT("LostWithin", "EndTitleEscaped", "YOU ESCAPED")
		: NSLOCTEXT("LostWithin", "EndTitleDied", "YOU DIED");
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
			? NSLOCTEXT("LostWithin", "EndSubtitleEscaped", "The skull lock gave way. You are out of the maze.")
			: NSLOCTEXT("LostWithin", "EndSubtitleDied", "It touched you."));
	}
	if (RestartLabel)
	{
		RestartLabel->SetText(bEscaped
			? NSLOCTEXT("LostWithin", "EndPlayAgain", "Play again")
			: NSLOCTEXT("LostWithin", "EndTryAgain", "Try again"));
	}
	if (QuitLabel)
	{
		QuitLabel->SetText(NSLOCTEXT("LostWithin", "EndQuit", "Quit"));
	}
}

UButton* UGameEndWidget::CreateMenuButton(const FName Name, TObjectPtr<UTextBlock>& OutLabel)
{
	UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), Name);

	FButtonStyle Style;
	Style.SetNormal(FSlateColorBrush(FLinearColor(0.08f, 0.08f, 0.09f, 0.95f)));
	Style.SetHovered(FSlateColorBrush(FLinearColor(0.45f, 0.06f, 0.05f, 1.0f)));
	Style.SetPressed(FSlateColorBrush(FLinearColor(0.30f, 0.04f, 0.03f, 1.0f)));
	Style.SetNormalPadding(FMargin(28.0f, 10.0f));
	Style.SetPressedPadding(FMargin(28.0f, 11.0f, 28.0f, 9.0f));
	Button->SetStyle(Style);

	OutLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	OutLabel->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 18));
	OutLabel->SetColorAndOpacity(FSlateColor(FLinearColor(0.95f, 0.93f, 0.90f, 1.0f)));
	OutLabel->SetJustification(ETextJustify::Center);
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
	TitleText->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 64));
	TitleText->SetJustification(ETextJustify::Center);
	TitleText->SetShadowOffset(FVector2D(2.0f, 2.0f));
	TitleText->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.9f));
	if (UVerticalBoxSlot* TitleSlot = Layout->AddChildToVerticalBox(TitleText))
	{
		TitleSlot->SetHorizontalAlignment(HAlign_Center);
		TitleSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));
	}

	SubtitleText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("EndSubtitle"));
	SubtitleText->SetFont(FCoreStyle::GetDefaultFontStyle("Italic", 18));
	SubtitleText->SetColorAndOpacity(FSlateColor(FLinearColor(0.80f, 0.78f, 0.74f, 1.0f)));
	SubtitleText->SetJustification(ETextJustify::Center);
	if (UVerticalBoxSlot* SubtitleSlot = Layout->AddChildToVerticalBox(SubtitleText))
	{
		SubtitleSlot->SetHorizontalAlignment(HAlign_Center);
		SubtitleSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 36.0f));
	}

	RestartButton = CreateMenuButton(TEXT("EndRestartButton"), RestartLabel);
	RestartButton->OnClicked.AddDynamic(this, &UGameEndWidget::HandleRestartClicked);
	if (UVerticalBoxSlot* RestartSlot = Layout->AddChildToVerticalBox(RestartButton))
	{
		RestartSlot->SetHorizontalAlignment(HAlign_Center);
		RestartSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 12.0f));
	}

	QuitButton = CreateMenuButton(TEXT("EndQuitButton"), QuitLabel);
	QuitButton->OnClicked.AddDynamic(this, &UGameEndWidget::HandleQuitClicked);
	if (UVerticalBoxSlot* QuitSlot = Layout->AddChildToVerticalBox(QuitButton))
	{
		QuitSlot->SetHorizontalAlignment(HAlign_Center);
	}
}

void UGameEndWidget::HandleRestartClicked()
{
	if (ALostWithinPlayerController* PlayerController = GetOwningPlayer<ALostWithinPlayerController>())
	{
		PlayerController->RestartRun();
	}
}

void UGameEndWidget::HandleQuitClicked()
{
	if (ALostWithinPlayerController* PlayerController = GetOwningPlayer<ALostWithinPlayerController>())
	{
		PlayerController->QuitRun();
	}
}
