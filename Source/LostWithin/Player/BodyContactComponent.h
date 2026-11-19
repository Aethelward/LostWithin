#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "BodyContactComponent.generated.h"

class ACharacter;
class UPhysicsAsset;
class UShapeComponent;
class USkeletalMeshComponent;

/** Contact primitives follow visible bones; they never participate in navigation or ragdoll simulation. */
UCLASS(ClassGroup=(Player), meta=(BlueprintSpawnableComponent))
class LOSTWITHIN_API UBodyContactComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UBodyContactComponent();
	static constexpr ECollisionChannel PlayerMovementChannel = ECC_GameTraceChannel2;
	static constexpr ECollisionChannel BodyContactChannel = ECC_GameTraceChannel3;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Body Contact")
	TSoftObjectPtr<UPhysicsAsset> ContactPhysicsAsset;

	/** Body for the MetaHuman; None uses the character's main mesh. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Body Contact")
	FName MeshComponentName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Body Contact")
	bool bPlayerBody = false;

	/**
	 * A one-frame move of the owner farther than this is a teleport (respawn, recovery, scripted move): contact restarts
	 * from the new pose instead of sweeping every shape along the jump. Ordinary fast motion stays well below it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Body Contact", meta=(ClampMin="50.0", Units="cm"))
	float MaxSweepDistance = 300.0f;

	UFUNCTION(BlueprintCallable, Category="Body Contact") void RebuildContactBodies();
	UFUNCTION(BlueprintPure, Category="Body Contact") int32 GetContactShapeCount() const { return Shapes.Num(); }
	UFUNCTION(BlueprintPure, Category="Body Contact") USkeletalMeshComponent* GetContactMesh() const { return SourceMesh.Get(); }
	/** Current or swept body contact, with a blocking-world check. No capsule-distance fallback. */
	bool HasBodyContactWith(const UBodyContactComponent& Other) const;
	/** Designers can supplement an asset with a conservative bone-local sphere (also used by synthetic fixtures). */
	UShapeComponent* AddContactSphere(FName Bone, const FVector& LocalCenter, float Radius);
	void RefreshContactTransforms();
	static void SetupMonstersForWorld(UWorld* World);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;

private:
	USkeletalMeshComponent* ResolveMesh() const;
	void RegisterShape(UShapeComponent* Shape, FName Bone, const FTransform& LocalTransform);
	bool QueryContactAgainst(const UBodyContactComponent& Other) const;
	bool IsUnobstructed(const FVector& From, const FVector& To, const AActor* OtherOwner) const;
	FBox GetSweptBounds() const;
	UPROPERTY(Transient) TObjectPtr<USkeletalMeshComponent> SourceMesh;
	UPROPERTY(Transient) TArray<TObjectPtr<UShapeComponent>> Shapes;
	TArray<FTransform> PreviousTransforms;
	TArray<FTransform> CurrentTransforms;
	FVector LastOwnerLocation = FVector::ZeroVector;
	bool bHasPoseHistory = false;
};
