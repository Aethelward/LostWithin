// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "LostWithinScentSubsystem.generated.h"

class AActor;

/** One point of the player's scent trail. Ids grow along the trail, so a higher id is a later step. */
USTRUCT(BlueprintType)
struct LOSTWITHIN_API FLostWithinScentNode
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Scent")
	int32 Id = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Scent")
	FVector Location = FVector::ZeroVector;

	/** Strength when laid (walk 1, run 1.5, sprint 2, standing pool 3); it fades linearly to 0 over Lifetime. */
	UPROPERTY(BlueprintReadOnly, Category = "Scent")
	float Strength = 1.0f;

	/** Scent clock time when the node was laid. */
	double Created = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "Scent", meta = (Units = "s"))
	float Lifetime = 45.0f;
};

/** A dropped item that carries the player's scent; its location follows the actor. */
struct FLostWithinItemScent
{
	TWeakObjectPtr<AActor> Item;
	float Strength = 3.0f;
	double Created = 0.0;
	float Lifetime = 20.0f;
};

/**
 *  The player's scent for the BoneDog's nose: a trail of nodes laid every NodeSpacing of movement, pools while the
 *  player stands still, and scented items the player dropped. Lit wall torches mask scent: no node is laid within
 *  TorchMaskRadius of one, and nodes there are removed. Runs only in game and Play worlds; tests drive it with Tick.
 */
UCLASS()
class LOSTWITHIN_API ULostWithinScentSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scent|Trail", meta = (ClampMin = "10.0", Units = "cm"))
	float NodeSpacing = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scent|Trail", meta = (ClampMin = "0.0"))
	float WalkStrength = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scent|Trail", meta = (ClampMin = "0.0"))
	float RunStrength = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scent|Trail", meta = (ClampMin = "0.0"))
	float SprintStrength = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scent|Trail", meta = (ClampMin = "1.0", Units = "s"))
	float NodeLifetime = 45.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scent|Trail", meta = (ClampMin = "1.0", Units = "s"))
	float SprintNodeLifetime = 60.0f;

	/** Standing within PoolRadius of one spot this long lays a strong pool node (again every PoolDelay). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scent|Pool", meta = (ClampMin = "0.5", Units = "s"))
	float PoolDelay = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scent|Pool", meta = (ClampMin = "0.0", Units = "cm"))
	float PoolRadius = 50.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scent|Pool", meta = (ClampMin = "0.0"))
	float PoolStrength = 3.0f;

	/** Smoke of a lit wall torch hides scent within this distance of it. 0 disables masking. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scent|Masking", meta = (ClampMin = "0.0", Units = "cm"))
	float TorchMaskRadius = 400.0f;

	/** Items the player held at least this long carry its scent when dropped. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scent|Items", meta = (ClampMin = "0.0", Units = "s"))
	float ItemScentMinHeldSeconds = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scent|Items", meta = (ClampMin = "0.0"))
	float ItemScentStrength = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scent|Items", meta = (ClampMin = "1.0", Units = "s"))
	float ItemScentLifetime = 20.0f;

	/** When false, the subsystem does not sample the player itself (tests lay nodes by hand). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scent")
	bool bSamplePlayer = true;

	static ULostWithinScentSubsystem* Get(const UObject* WorldContext);

	// UTickableWorldSubsystem
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** Current scent clock (seconds of play since the subsystem started). */
	double GetClock() const { return Clock; }

	/** Strength of Node now: fades linearly from Strength to 0 over its lifetime. */
	float GetEffectiveStrength(const FLostWithinScentNode& Node) const;

	/** The trail, oldest first. */
	const TArray<FLostWithinScentNode>& GetNodes() const { return Nodes; }

	/** The freshest live node within Radius of Point whose id is above MinId; null when none. */
	const FLostWithinScentNode* FindFreshestNodeWithin(const FVector& Point, float Radius, int32 MinId = 0) const;

	/** The first live node laid after AfterId; null when the trail has nothing later. */
	const FLostWithinScentNode* FindNextNode(int32 AfterId) const;

	/** The node with this id, or null when it expired or was masked. */
	const FLostWithinScentNode* FindNode(int32 Id) const;

	/** Highest node id laid so far (0 before the first node). */
	int32 GetLatestNodeId() const { return NextNodeId - 1; }

	/** Lays a node now (the player sampler and tests). Returns its id, or 0 when the point is masked. */
	int32 AddNode(const FVector& Location, float Strength, float Lifetime);

	/** Gives a dropped item the player's scent. */
	void AddItemScent(AActor* Item, float Strength, float Lifetime);
	void AddItemScent(AActor* Item) { AddItemScent(Item, ItemScentStrength, ItemScentLifetime); }

	/** True when Item still carries scent. */
	bool HasItemScent(const AActor* Item) const;

	/** Effective strength of Item's scent now (0 when none). */
	float GetItemScentStrength(const AActor* Item) const;

	/** The strongest scented item within Radius of Point, skipping Ignored; OutStrength receives its strength. */
	AActor* FindStrongestItemWithin(const FVector& Point, float Radius, const TSet<TWeakObjectPtr<AActor>>& Ignored,
		float& OutStrength) const;

	/** True when a lit torch's smoke hides scent at Point. */
	bool IsScentMasked(const FVector& Point) const;

	/** Removes every node and item scent (new run, tests). */
	void ClearScent();

protected:

	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:

	void SamplePlayer(float DeltaTime);
	void PruneNodes();

	TArray<FLostWithinScentNode> Nodes;
	TArray<FLostWithinItemScent> Items;
	double Clock = 0.0;
	int32 NextNodeId = 1;

	bool bHasLastNode = false;
	FVector LastNodeLocation = FVector::ZeroVector;
	FVector PoolAnchor = FVector::ZeroVector;
	bool bHasPoolAnchor = false;
	float StillSeconds = 0.0f;
	float PruneTimer = 0.0f;
};
