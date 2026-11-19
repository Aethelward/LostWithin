// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Interaction/InteractableInterface.h"
#include "EscapeDoorComponent.generated.h"

/**
 *  Makes BP_ScaryDoor the exit of the maze.
 *
 *  The door and its skull padlock (BP_PadlockBase-3X, a child actor of the door) come from a Blueprint pack that only
 *  talks through BPI_InteractionPadlock::Interact. This component bridges the [E] interaction to it: while the door
 *  is locked, [E] starts the padlock's dial view; once the padlock has unlocked the door, [E] opens it and the run
 *  ends in an escape. At BeginPlay it gives the padlock this run's combination and points it at this door.
 */
UCLASS(ClassGroup=(Interaction), meta=(BlueprintSpawnableComponent))
class LOSTWITHIN_API UEscapeDoorComponent : public UActorComponent, public IInteractableInterface
{
	GENERATED_BODY()

public:

	UEscapeDoorComponent();

	/** Seconds between opening the door and the escape screen (lets the door swing open) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Escape", meta = (ClampMin = "0.0", Units = "s"))
	float EscapeDelay = 1.6f;

	/** Off (default): [E] opens the skull padlock's own dial view. On: a three-digit keypad modal replaces it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Escape")
	bool bUseKeypadUI = false;

	/** Gives the padlock the game mode's random combination; off keeps the padlock's own Combination */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Escape")
	bool bApplyRunCombination = true;

	/** Door Blueprint bool that is true while locked */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Escape|Blueprint Bridge")
	FName DoorLockedPropertyName = TEXT("DoorLock?");

	/** Padlock Blueprint string compared with the dials */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Escape|Blueprint Bridge")
	FName PadlockCombinationPropertyName = TEXT("Combination");

	/** Padlock Blueprint reference to the door it unlocks */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Escape|Blueprint Bridge")
	FName PadlockDoorPropertyName = TEXT("Door");

	/** BPI_InteractionPadlock function implemented by both the door and the padlock */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Escape|Blueprint Bridge")
	FName InteractFunctionName = TEXT("Interact");

	/** True while the door Blueprint reports itself locked */
	UFUNCTION(BlueprintPure, Category = "Escape")
	bool IsDoorLocked() const;

	/** The padlock actor that guards this door, if found */
	UFUNCTION(BlueprintPure, Category = "Escape")
	AActor* GetPadlock() const { return Padlock.Get(); }

	/** Writes Combination into the padlock (digits only, e.g. "307"); false when there is no padlock */
	UFUNCTION(BlueprintCallable, Category = "Escape")
	bool ApplyCombination(const FString& Combination);

	UFUNCTION(BlueprintCallable, Category="Escape")
	bool TrySubmitCode(const FString& Code, AActor* Interactor);
	static bool IsValidCode(const FString& Code);

	/** Calls a parameterless Blueprint function or event by name (BPI_InteractionPadlock::Interact) */
	static bool CallBlueprintFunction(UObject* Target, FName FunctionName);

	/** Reflection helpers for the pack's Blueprint variables */
	static bool SetStringProperty(UObject* Target, FName PropertyName, const FString& Value);
	static bool SetObjectProperty(UObject* Target, FName PropertyName, UObject* Value);
	static bool GetBoolProperty(const UObject* Target, FName PropertyName, bool& OutValue);
	static bool HasStringProperty(const UObject* Target, FName PropertyName);

	//~ Begin IInteractableInterface
	virtual void OnInteract_Implementation(AActor* Interactor) override;
	virtual FText GetInteractionPrompt_Implementation() const override;
	virtual bool CanInteract_Implementation(AActor* Interactor) const override;
	//~ End IInteractableInterface

protected:

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:

	/** Child actor or attached actor with a Combination property; else a level padlock whose Door is the owner */
	AActor* FindPadlock() const;
	bool StartEscape(AActor* Interactor);
	void PlayPadlockUnlock();
	void CompleteEscape();

	TWeakObjectPtr<AActor> Padlock;
	TWeakObjectPtr<AController> EscapingController;
	FTimerHandle EscapeTimerHandle;
	bool bEscaping = false;
	bool bKeypadUnlocked = false;
	FString CachedCombination;
};
