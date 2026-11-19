// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ItemSpawnPoint.generated.h"

class UArrowComponent;
class UBillboardComponent;

/** What a spawn point may receive when a run starts. */
UENUM(BlueprintType)
enum class ELostWithinSpawnPointKind : uint8
{
	/** One of the unique keys (never the Fairy Tale Kitty Key, which only the cat drops) */
	Key,
	/** A paper note: a combination hint or a flavour note */
	Note,
	/** Reserved orientation note on the starting table; not part of the randomized note pool. */
	Orientation
};

/**
 *  A designer-placed spot where the game mode may put a key or a note at the start of a run.
 *  Place it a little above the floor; keys fall onto it and notes are laid flat on the floor below it.
 *  Always loaded, so World Partition streaming never hides it from the run setup.
 */
UCLASS()
class LOSTWITHIN_API AItemSpawnPoint : public AActor
{
	GENERATED_BODY()

public:

	AItemSpawnPoint();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawn")
	ELostWithinSpawnPointKind Kind = ELostWithinSpawnPointKind::Key;

	/** Where the item appears. */
	UFUNCTION(BlueprintPure, Category = "Spawn")
	FVector GetSpawnLocation() const { return GetActorLocation(); }

protected:

	virtual void OnConstruction(const FTransform& Transform) override;

private:

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TObjectPtr<UBillboardComponent> SpriteComponent;

	UPROPERTY()
	TObjectPtr<UArrowComponent> ArrowComponent;
#endif
};
