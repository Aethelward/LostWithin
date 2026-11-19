// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/InteractableInterface.h"
#include "Inventory/InventoryComponent.h"
#include "ToolboxActor.generated.h"

class APickupItemActor;
class UAnimSequenceBase;
class UBoxComponent;
class USkeletalMeshComponent;
class USoundBase;

/** Whether the run may lock this toolbox. */
UENUM(BlueprintType)
enum class ELostWithinToolboxLockMode : uint8
{
	/** The run decides (locks are capped by the number of unique obtainable keys) */
	Random,
	/** Always locked while a key is available (FixedKeyID when it is obtainable, otherwise any free key) */
	AlwaysLocked,
	/** Never locked */
	NeverLocked
};

/**
 *  A toolbox (/Game/Level/Objects/tool_box) that may be padlocked by one specific unique key and holds one item or
 *  nothing. The game mode decides its lock and contents when the run starts; [LMB] unlocks (using up the key) and
 *  opens the lid, revealing the contents resting inside. The actor origin is the bottom centre: place it on the floor.
 *
 *  Collision follows the visible box: a floor, four walls up to the rim and a lid cap that swings open with the lid
 *  bone. The cavity between them is free, so the contents lie really inside and nothing invisible blocks once open.
 */
UCLASS()
class LOSTWITHIN_API AToolboxActor : public AActor, public IInteractableInterface
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> Root;

	/** Floor of the hollow box (keeps its original name so saved Blueprint and level data still bind) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UBoxComponent> CollisionBox;

	/** Wall on the hinge side (-Y), from the floor to the rim */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UBoxComponent> BackWallCollision;

	/** Wall on the latch side (+Y), from the floor to the rim */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UBoxComponent> FrontWallCollision;

	/** End wall at -X, from the floor to the rim */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UBoxComponent> LeftWallCollision;

	/** End wall at +X, from the floor to the rim */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UBoxComponent> RightWallCollision;

	/** Lid cap, attached to the lid bone so it swings open with the lid animation */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UBoxComponent> LidCollision;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USkeletalMeshComponent> Mesh;

public:

	AToolboxActor();

	/** The bone the open animation rotates; LidCollision follows it */
	static const FName LidBoneName;

	/** Lid animation played once when opened (holds its last frame) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toolbox")
	TObjectPtr<UAnimSequenceBase> OpenAnimation;

	/** The imported mesh is huge; it is uniformly scaled so its widest side is this long */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toolbox", meta = (ClampMin = "10.0", Units = "cm"))
	float TargetWidthCm = 60.0f;

	/**
	 *  Seconds after the lid starts opening before an empty toolbox says so. Contents are placed inside the closed box
	 *  as the lid starts to move, so the opening lid reveals them.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toolbox", meta = (ClampMin = "0.0", Units = "s"))
	float ContentsDelay = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toolbox|Lock")
	ELostWithinToolboxLockMode LockMode = ELostWithinToolboxLockMode::Random;

	/** Optional designer-chosen key (Key, OldKey, RoyalKey, FairyTaleKittyKey); used only when obtainable */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toolbox|Lock")
	FName FixedKeyID = NAME_None;

	/** Prompts name the key a locked toolbox needs */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toolbox|Lock")
	bool bRevealRequiredKey = true;

	/** Removes the key from the inventory when it opens this toolbox (each unique key fits one toolbox) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toolbox|Lock")
	bool bConsumeKeyOnUnlock = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toolbox|Audio")
	TObjectPtr<USoundBase> UnlockSound;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toolbox|Audio")
	TObjectPtr<USoundBase> LockedSound;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toolbox|Audio")
	TObjectPtr<USoundBase> OpenSound;

	/** Sets the key this toolbox needs this run (NAME_None = unlocked); ignored once open */
	UFUNCTION(BlueprintCallable, Category = "Toolbox|Lock")
	void SetRequiredKey(FName KeyID);

	UFUNCTION(BlueprintPure, Category = "Toolbox|Lock")
	FName GetRequiredKey() const { return RequiredKeyID; }

	UFUNCTION(BlueprintPure, Category = "Toolbox|Lock")
	bool IsLocked() const { return !bOpen && !RequiredKeyID.IsNone(); }

	UFUNCTION(BlueprintPure, Category = "Toolbox")
	bool IsOpen() const { return bOpen; }

	/** Sets what the toolbox yields when opened: an item of ItemClass described by Item */
	void SetContents(TSubclassOf<APickupItemActor> ItemClass, const FInventoryItem& Item);

	/** The toolbox yields nothing */
	UFUNCTION(BlueprintCallable, Category = "Toolbox")
	void ClearContents();

	UFUNCTION(BlueprintPure, Category = "Toolbox")
	bool HasContents() const { return bHasContents; }

	const FInventoryItem& GetContentsItem() const { return ContentsItem; }

	/** Unlocks with the required key from Interactor's inventory; true when the toolbox is (now) unlocked */
	UFUNCTION(BlueprintCallable, Category = "Toolbox|Lock")
	bool TryUnlock(AActor* Interactor);

	/** Opens an unlocked toolbox: lid animation, with the contents placed inside as it starts */
	UFUNCTION(BlueprintCallable, Category = "Toolbox")
	bool Open();

	/**
	 *  Spawns the contents now, resting on the inner floor in the middle of the cavity (normally called by Open);
	 *  returns the spawned pickup, if any. An empty toolbox tells the last interactor instead.
	 */
	APickupItemActor* SpawnContents();

	/** The free space inside the box in actor space: inner floor to rim, between the inner walls */
	FBox GetCavityBox() const;

	/** The lid cap (follows the lid bone) */
	UBoxComponent* GetLidCollision() const { return LidCollision; }

	//~ Begin IInteractableInterface
	virtual void OnInteract_Implementation(AActor* Interactor) override;
	virtual FText GetInteractionPrompt_Implementation() const override;
	virtual bool CanInteract_Implementation(AActor* Interactor) const override;
	//~ End IInteractableInterface

protected:

	virtual void OnConstruction(const FTransform& Transform) override;

private:

	/** Creates one blocking collision box of the hollow toolbox */
	UBoxComponent* CreateCollisionPart(FName Name, USceneComponent* Parent, FName Socket, bool bAffectsNavigation);

	/** Scales and centres the mesh on the bottom-centre origin and fits the floor, walls and lid cap to it */
	void FitMeshToTargetWidth();

	/** Sizes the floor and the four walls around the cavity for TargetWidthCm */
	void FitBaseCollision();

	/** Places the lid cap over the closed box, relative to the lid bone's reference pose so the animation carries it */
	void FitLidCollision(const FBox& ClosedLidInActor);

	/** Rests Pickup on the inner floor in the middle of the cavity, its longer side along the box (notes lie flat) */
	void PlaceInsideCavity(APickupItemActor& Pickup) const;

	FText GetRequiredKeyName() const;
	bool InteractorHasRequiredKey(const AActor* Interactor) const;
	void PlaySound(USoundBase* Sound) const;
	void NotifyPlayer(AActor* Interactor, const FText& Message) const;

	UPROPERTY(VisibleInstanceOnly, Category = "Toolbox|Run")
	FName RequiredKeyID = NAME_None;

	UPROPERTY(VisibleInstanceOnly, Category = "Toolbox|Run")
	bool bOpen = false;

	UPROPERTY(VisibleInstanceOnly, Category = "Toolbox|Run")
	bool bHasContents = false;

	UPROPERTY(VisibleInstanceOnly, Category = "Toolbox|Run")
	TSubclassOf<APickupItemActor> ContentsClass;

	UPROPERTY(VisibleInstanceOnly, Category = "Toolbox|Run")
	FInventoryItem ContentsItem;

	FTimerHandle ContentsTimerHandle;

	/** Who last used the toolbox, for "it's empty" feedback */
	TWeakObjectPtr<AActor> LastInteractor;
};
