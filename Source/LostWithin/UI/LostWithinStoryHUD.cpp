#include "UI/LostWithinStoryHUD.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Styling/CoreStyle.h"

TSharedRef<SWidget> ULostWithinStoryHUD::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("StoryHUD"));
		WidgetTree->RootWidget = Canvas;
		auto AddText = [this, Canvas](FName Name, FVector2D Anchor, FVector2D Alignment, FVector2D Offset, float Width, int32 FontSize)
		{
			UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
			Text->SetFont(FCoreStyle::GetDefaultFontStyle("Regular", FontSize));
			Text->SetColorAndOpacity(FSlateColor(FLinearColor(0.94f, 0.92f, 0.84f)));
			Text->SetShadowColorAndOpacity(FLinearColor::Black);
			Text->SetShadowOffset(FVector2D(1.5f));
			Text->SetAutoWrapText(true);
			Text->SetWrapTextAt(Width - 32.0f);
			Text->SetJustification(Anchor.X == 0.0f ? ETextJustify::Left : ETextJustify::Center);
			UBorder* Border = WidgetTree->ConstructWidget<UBorder>();
			Border->SetBrushColor(FLinearColor(0.015f, 0.015f, 0.02f, 0.78f));
			Border->SetPadding(FMargin(16.0f, 10.0f));
			Border->SetContent(Text);
			USizeBox* Size = WidgetTree->ConstructWidget<USizeBox>();
			Size->SetMaxDesiredWidth(Width);
			Size->SetContent(Border);
			if (UCanvasPanelSlot* Placement = Canvas->AddChildToCanvas(Size))
			{
				Placement->SetAnchors(FAnchors(Anchor.X, Anchor.Y));
				Placement->SetAlignment(Alignment);
				Placement->SetPosition(Offset);
				Placement->SetAutoSize(true);
			}
			return Text;
		};
		ObjectiveText = AddText(TEXT("StoryObjective"), FVector2D(0.0f, 0.0f), FVector2D::ZeroVector, FVector2D(28.0f), 460.0f, 17);
		TutorialText = AddText(TEXT("StoryTutorial"), FVector2D(0.5f, 0.0f), FVector2D(0.5f, 0.0f), FVector2D(0.0f, 84.0f), 850.0f, 19);
		SubtitleText = AddText(TEXT("StorySubtitle"), FVector2D(0.5f, 1.0f), FVector2D(0.5f, 1.0f), FVector2D(0.0f, -42.0f), 1000.0f, 21);
		SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	return Super::RebuildWidget();
}

void ULostWithinStoryHUD::UpdateStory(const FText& Objective, const FText& Tutorial, const FText& Subtitle)
{
	auto SetText = [](UTextBlock* Target, const FText& Text)
	{
		if (Target)
		{
			Target->SetText(Text);
			if (UPanelWidget* Parent = Target->GetParent())
			{
				Parent->SetVisibility(Text.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
			}
		}
	};
	SetText(ObjectiveText, Objective);
	SetText(TutorialText, Tutorial);
	SetText(SubtitleText, Subtitle);
}
