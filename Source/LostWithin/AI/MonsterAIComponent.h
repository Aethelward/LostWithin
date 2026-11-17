// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "MonsterAIComponent.generated.h"

class ACharacter;
class ANavigationData;
class UNavigationQueryFilter;
class USceneComponent;

/** Flees from active float/spin cats and throws the owning character into a recoverable ragdoll. */
UCLASS(ClassGroup=(AI), meta=(BlueprintSpawnableComponent))
class LOSTWITHIN_API UMonsterAIComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMonsterAIComponent();

	/** Distance at which an active float/spin cat causes the owning character to flee. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Monster AI|Cat Proximity", meta=(ClampMin="0.0", Units="cm"))
	float CatFleeRadius = 500.0f;

	/** Distance at which an active cat throws the owning character into ragdoll. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Monster AI|Cat Proximity", meta=(ClampMin="0.0", Units="cm"))
	float CatRagdollRadius = 100.0f;

	/** Distance at which the component reports the player as nearby. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Monster AI|Player Proximity", meta=(ClampMin="0.0", Units="cm"))
	float PlayerDetectionRadius = 1000.0f;

	/** Field of view in degrees (total view angle = 2 * PeripheralVisionAngle). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Monster AI|Player Proximity", meta=(ClampMin="0.0", ClampMax="180.0", Units="deg"))
	float PeripheralVisionAngle = 45.0f;

	/** Checks if the player is within sight radius and field of view. */
	UFUNCTION(BlueprintCallable, Category="Monster AI")
	bool IsPlayerInSight();

	/** How far from the character to place the flee destination. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Monster AI|Cat Proximity", meta=(ClampMin="0.0", Units="cm"))
	float FleeDistance = 500.0f;

	/** Time before recovering from a cat-triggered ragdoll. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Monster AI|Cat Proximity", meta=(ClampMin="0.1", Units="s"))
	float RagdollDuration = 3.0f;

	/** Horizontal velocity change applied away from the cat once per ragdoll. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Monster AI|Ragdoll", meta=(ClampMin="0.0", Units="cm/s"))
	float RagdollLaunchStrength = 1200.0f;

	/** Upward velocity change applied once per ragdoll. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Monster AI|Ragdoll", meta=(ClampMin="0.0", Units="cm/s"))
	float RagdollUpwardStrength = 600.0f;

	/** Simulated hip used to locate the landed body. Falls back to the first physics body. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Monster AI|Ragdoll")
	FName RagdollRecoveryBone = TEXT("Hips");

	/** Guard normal Blueprint movement, including movement requested by latent events. */
	UFUNCTION(BlueprintPure, Category="Monster AI", meta=(DefaultToSelf="Actor"))
	static bool CanRunNormalAI(AActor* Actor);

	/** Evaluate a roaming query once so its success flag and destination stay paired. */
	UFUNCTION(BlueprintCallable, Category="Monster AI|Navigation", meta=(WorldContext="WorldContextObject"))
	static bool FindReachableRoamLocation(UObject* WorldContextObject, const FVector& Origin, FVector& RandomLocation,
		float Radius, ANavigationData* NavData = nullptr, TSubclassOf<UNavigationQueryFilter> FilterClass = nullptr);

	UFUNCTION(BlueprintPure, Category="Monster AI|State")
	AActor* GetThreateningCat() const;

	UPROPERTY(BlueprintReadOnly, Category="Monster AI|State")
	bool bCatIsNear = false;

	UPROPERTY(BlueprintReadOnly, Category="Monster AI|State")
	bool bPlayerIsNear = false;

	UPROPERTY(BlueprintReadOnly, Category="Monster AI|State")
	bool bIsRagdolled = false;

	UPROPERTY(BlueprintReadOnly, Category="Monster AI|State")
	bool bIsFleeing = false;

protected:
	virtual void RegisterComponentTickFunctions(bool bRegister) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	void UpdateCatProximity();
	void UpdatePlayerProximity();
	void UpdateFleeMovement(float DeltaTime);
	bool ApplyRagdoll();
	void RecoverFromRagdoll();
	void RestoreCharacterSettings(ACharacter* Character);
	void ScheduleRecovery(float Delay);

	TWeakObjectPtr<AActor> ThreateningCat;
	FVector FleeDirection = FVector::ZeroVector;
	float FleeRepathTimeRemaining = 0.0f;
	bool bUseDirectFleeMovement = false;
	bool bWarnedAboutFleeSetup = false;
	bool bWarnedAboutPhysicsSetup = false;

	FTransform CachedMeshRelativeTransform = FTransform::Identity;
	TWeakObjectPtr<USceneComponent> CachedMeshAttachParent;
	FName CachedMeshAttachSocket;
	FName CachedMeshCollisionProfile;
	FName ActiveRagdollBone;
	FCollisionResponseContainer CachedMeshCollisionResponses;
	ECollisionChannel CachedMeshObjectType = ECC_Pawn;
	ECollisionEnabled::Type CachedMeshCollisionEnabled = ECollisionEnabled::NoCollision;
	ECollisionEnabled::Type CachedCapsuleCollisionEnabled = ECollisionEnabled::QueryAndPhysics;
	bool bCachedMeshGravityEnabled = true;
	uint8 CachedMovementMode = 0;
	uint8 CachedCustomMovementMode = 0;
	FTimerHandle RagdollRecoveryTimerHandle;
};
