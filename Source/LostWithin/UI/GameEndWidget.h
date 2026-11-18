// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameEndWidget.generated.h"

class UBorder;
class UButton;
class UTextBlock;

/**
 *  Full-screen end of run: "You died" after the death camera, or "You escaped" after the ScaryDoor opens.
 *  Built in C++ like UInventoryWidget; Try again reloads the level for a new random run, Quit leaves the game.
 */
UCLASS()
class LOSTWITHIN_API UGameEndWidget : public UUserWidget
{
	GENERATED_BODY()

public:

	UGameEndWidget(const FObjectInitializer& ObjectInitializer);

	/** Sets the title, subtitle and button labels for a death or an escape */
	UFUNCTION(BlueprintCallable, Category = "Game End")
	void Configure(bool bInEscaped);

	UFUNCTION(BlueprintPure, Category = "Game End")
	bool WasEscape() const { return bEscaped; }

	UFUNCTION(BlueprintPure, Category = "Game End")
	FText GetTitle() const;

protected:

	virtual TSharedRef<SWidget> RebuildWidget() override;

private:

	UFUNCTION()
	void HandleRestartClicked();

	UFUNCTION()
	void HandleQuitClicked();

	void BuildWidgetTreeIfNeeded();
	void ApplyTexts();
	UButton* CreateMenuButton(const FName Name, TObjectPtr<UTextBlock>& OutLabel);

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> TitleText = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> SubtitleText = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UButton> RestartButton = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> RestartLabel = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UButton> QuitButton = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> QuitLabel = nullptr;

	bool bEscaped = false;
};
