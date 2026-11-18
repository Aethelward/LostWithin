// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "LostWithinPlayerController.generated.h"

class UGameEndWidget;
class UInputMappingContext;
class UUserWidget;

/**
 *  Simple first person Player Controller
 *  Manages the input mapping context.
 *  Overrides the Player Camera Manager class.
 */
UCLASS(abstract)
class LOSTWITHIN_API ALostWithinPlayerController : public APlayerController
{
	GENERATED_BODY()
	
public:

	/** Constructor */
	ALostWithinPlayerController();

	/** Shows the "You died" / "You escaped" screen, frees the cursor and pauses the game */
	UFUNCTION(BlueprintCallable, Category="Game End")
	void ShowEndScreen(bool bEscaped);

	/** True once the end screen is on screen */
	UFUNCTION(BlueprintPure, Category="Game End")
	bool IsEndScreenVisible() const { return EndScreenWidget != nullptr; }

	/** Reloads the current level for a fresh random run */
	UFUNCTION(BlueprintCallable, Category="Game End")
	void RestartRun();

	/** Quits the game (ends Play In Editor) */
	UFUNCTION(BlueprintCallable, Category="Game End")
	void QuitRun();

	/** End screen widget class */
	UPROPERTY(EditAnywhere, Category="Game End")
	TSubclassOf<UGameEndWidget> EndScreenWidgetClass;

protected:

	/** Input Mapping Contexts */
	UPROPERTY(EditAnywhere, Category="Input|Input Mappings")
	TArray<UInputMappingContext*> DefaultMappingContexts;

	/** Input Mapping Contexts */
	UPROPERTY(EditAnywhere, Category="Input|Input Mappings")
	TArray<UInputMappingContext*> MobileExcludedMappingContexts;

	/** Mobile controls widget to spawn */
	UPROPERTY(EditAnywhere, Category="Input|Touch Controls")
	TSubclassOf<UUserWidget> MobileControlsWidgetClass;

	/** Pointer to the mobile controls widget */
	TObjectPtr<UUserWidget> MobileControlsWidget;

	/** The end screen, once shown */
	UPROPERTY(Transient)
	TObjectPtr<UGameEndWidget> EndScreenWidget;

	/** Gameplay initialization */
	virtual void BeginPlay() override;

	/** Input mapping context setup */
	virtual void SetupInputComponent() override;

};
