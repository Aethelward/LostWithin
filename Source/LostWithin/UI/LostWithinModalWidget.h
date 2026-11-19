#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "LostWithinModalWidget.generated.h"

class UEscapeDoorComponent;
class UTextBlock;
class ULostWithinModalWidget;

UCLASS()
class LOSTWITHIN_API ULostWithinDigitButton : public UButton
{
	GENERATED_BODY()
public:
	void Configure(ULostWithinModalWidget* Panel, int32 Value);
private:
	UFUNCTION() void PressDigit();
	UPROPERTY(Transient) TObjectPtr<ULostWithinModalWidget> OwnerPanel;
	int32 Digit = 0;
};

/** Focused, live-world keypad or explicitly paused menu. */
UCLASS()
class LOSTWITHIN_API ULostWithinModalWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	ULostWithinModalWidget(const FObjectInitializer& Initializer);
	void ConfigureKeypad(UEscapeDoorComponent* InDoor);
	void AppendDigit(int32 Digit);
	void Backspace();
	void SubmitCode();
	bool IsKeypad() const { return bKeypad; }
	FString GetEnteredCode() const { return EnteredCode; }
	FText GetFeedback() const;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Key) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& MouseEvent) override;

private:
	void UpdateCodeDisplay();
	UFUNCTION() void HandleClose();
	UFUNCTION() void HandleOptions();
	UFUNCTION() void HandleRestart();
	UFUNCTION() void HandleMainMenu();
	UFUNCTION() void HandleSubmit();
	UFUNCTION() void HandleBackspace();
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CodeText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> FeedbackText;
	TWeakObjectPtr<UEscapeDoorComponent> Door;
	FString EnteredCode;
	bool bKeypad = false;
};
