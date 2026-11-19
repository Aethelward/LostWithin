#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "LostWithinStoryHUD.generated.h"

class UTextBlock;

/** Separate from inventory so ending subtitles survive the death camera hiding the normal HUD. */
UCLASS()
class LOSTWITHIN_API ULostWithinStoryHUD : public UUserWidget
{
	GENERATED_BODY()

public:
	void UpdateStory(const FText& Objective, const FText& Tutorial, const FText& Subtitle);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ObjectiveText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TutorialText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> SubtitleText;
};
