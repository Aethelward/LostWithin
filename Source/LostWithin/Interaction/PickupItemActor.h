// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/InteractableInterface.h"
#include "Inventory/InventoryComponent.h"
#include "PickupItemActor.generated.h"

class USphereComponent;
class USoundBase;
class UStaticMeshComponent;

/**
 *  World pickup actor representing an inventory item (defaults to a placeholder item).
 *  Can be placed directly in levels or spawned at runtime (e.g. dropped by the cat or player).
 */
UCLASS()
class LOSTWITHIN_API APickupItemActor : public AActor, public IInteractableInterface
{
	GENERATED_BODY()

	/** Query-only sphere used by interaction and hit traces */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USphereComponent> CollisionSphere;

	/** Visual mesh for the dropped/world pickup */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> MeshComponent;

public:

	APickupItemActor();

	/** Inventory item data granted when picked up via [F] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pickup")
	FInventoryItem ItemData;

	/** Bobbing amplitude once settled on the ground */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pickup|Animation", meta = (ClampMin = "0.0", Units = "cm"))
	float IdleBobAmplitude = 6.0f;

	/** Bobbing frequency in cycles per second */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pickup|Animation", meta = (ClampMin = "0.0", Units = "Hz"))
	float IdleBobFrequency = 1.0f;

	/** Yaw spin rate while resting in the world */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pickup|Animation", meta = (Units = "deg/s"))
	float IdleSpinRate = 90.0f;

	/** Sound variations played immediately when this item is dropped */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pickup|Audio")
	TArray<TObjectPtr<USoundBase>> DropSounds;

	/** Volume multiplier for item drop SFX */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pickup|Audio", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float DropSoundVolume = 0.85f;

	/** Starts a smooth parabolic drop arc from StartLocation to TargetLocation */
	UFUNCTION(BlueprintCallable, Category = "Pickup")
	void StartDropArc(const FVector& StartLocation, const FVector& TargetLocation, float ArcHeight = 55.0f, float Duration = 0.45f);

	//~ Begin IInteractableInterface
	virtual void OnInteract_Implementation(AActor* Interactor) override;
	virtual void OnHit_Implementation(AActor* Attacker, const FHitResult& HitResult) override;
	virtual FText GetInteractionPrompt_Implementation() const override;
	virtual bool CanInteract_Implementation(AActor* Interactor) const override;
	//~ End IInteractableInterface

protected:

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;

public:

	virtual void Tick(float DeltaTime) override;

private:

	/** Ensures MeshComponent has a fallback mesh assigned if none was set in Blueprint */
	void EnsureDefaultMesh();

	/** Populates DropSounds with default item drop SFX and clears looping flags */
	void EnsureDefaultDropSounds();

	/** Plays a random drop sound at the pickup's current world location */
	void PlayRandomDropSound();

	/** Traces downward from DesiredPoint to settle cleanly above floor geometry */
	FVector FindGroundLocation(const FVector& DesiredPoint, const TArray<AActor*>& ActorsToIgnore) const;

	bool bIsDropping = false;
	float DropElapsed = 0.0f;
	float DropDuration = 0.45f;
	float DropArcHeight = 55.0f;
	FVector DropStartLocation = FVector::ZeroVector;
	FVector DropEndLocation = FVector::ZeroVector;

	FVector RestLocation = FVector::ZeroVector;
	float IdleTime = 0.0f;
	int32 LastDropSoundIndex = INDEX_NONE;
};

