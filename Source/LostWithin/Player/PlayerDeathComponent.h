// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PlayerDeathComponent.generated.h"

class ADeathCameraActor;
class UCapsuleComponent;
class UPrimitiveComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPlayerDeathStarted, AActor*, Killer);

/**
 *  Ends the run when the player touches a monster (any actor with UMonsterAIComponent that is not ragdolled):
 *  freezes the player and the killer, plays the death camera, then asks the game mode for the game-over screen.
 *  Contact is checked after animation using dedicated fitted body volumes and swept queries. Navigation
 *  capsules and nearby monsters cannot cause death; walls, closed doors and ragdolls remain nonlethal.
 */
UCLASS(ClassGroup=(Player), meta=(BlueprintSpawnableComponent))
class LOSTWITHIN_API UPlayerDeathComponent : public UActorComponent
{
	GENERATED_BODY()

public:

	UPlayerDeathComponent();

	/** Legacy serialized setting; body contact does not use capsule tolerances. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death", meta = (ClampMin = "0.0", Units = "cm"))
	float TouchTolerance = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death")
	TSubclassOf<ADeathCameraActor> DeathCameraClass;

	UPROPERTY(BlueprintAssignable, Category = "Death")
	FOnPlayerDeathStarted OnDeathStarted;

	UFUNCTION(BlueprintPure, Category = "Death")
	bool IsDead() const { return bDead; }

	UFUNCTION(BlueprintPure, Category = "Death")
	AActor* GetKiller() const { return Killer.Get(); }

	/** Starts the death sequence once; false when already dead or the run is already ending */
	UFUNCTION(BlueprintCallable, Category = "Death")
	bool BeginDeath(AActor* InKiller);

	/** A monster that can kill on contact: has UMonsterAIComponent and is not ragdolled */
	static bool IsLethalMonster(const AActor* Actor);

	/** True when the two capsules are within Tolerance of each other */
	static bool AreCapsulesTouching(const UCapsuleComponent& A, const UCapsuleComponent& B, float Tolerance);

	/** The first lethal monster touching the owner's capsule, if any */
	AActor* FindTouchingMonster() const;
	/** Requires fitted visible body contact and an unobstructed path. */
	bool IsTouchingMonster(const AActor* Monster) const;

	/** The death camera while it plays */
	ADeathCameraActor* GetDeathCamera() const { return DeathCamera.Get(); }

protected:

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:

	UFUNCTION()
	void HandleCapsuleHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		FVector NormalImpulse, const FHitResult& Hit);

	void FreezeKiller(AActor* InKiller) const;
	void StartDeathCamera();
	void FinishDeath();

	TWeakObjectPtr<AActor> Killer;
	TWeakObjectPtr<ADeathCameraActor> DeathCamera;
	FTimerHandle FinishTimerHandle;
	bool bDead = false;
	bool bFinished = false;
};
