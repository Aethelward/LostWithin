// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/HitResult.h"
#include "UObject/Interface.h"
#include "InteractableInterface.generated.h"

UINTERFACE(MinimalAPI, Blueprintable)
class UInteractableInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 *  Common interface for interactable actors and components ([F] interact and [LMB] hit).
 */
class LOSTWITHIN_API IInteractableInterface
{
	GENERATED_BODY()

public:

	/** Called when the player interacts with this target using [F] */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Interaction")
	void OnInteract(AActor* Interactor);

	/** Called when the player hits this target using [LMB] */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Interaction")
	void OnHit(AActor* Attacker, const FHitResult& HitResult);

	/** Returns the prompt text shown when the player looks at this target */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Interaction")
	FText GetInteractionPrompt() const;

	/** Returns true if this target can currently be interacted with */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Interaction")
	bool CanInteract(AActor* Interactor) const;
};

