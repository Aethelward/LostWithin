// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/LostWithinScentSubsystem.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Gameplay/LostWithinLightSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "LostWithinCharacter.h"

namespace
{
	/** Masking and expiry are checked this often; nodes only fade slowly, so a coarse prune is enough. */
	constexpr float ScentPruneIntervalSeconds = 0.5f;
}

ULostWithinScentSubsystem* ULostWithinScentSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World ? World->GetSubsystem<ULostWithinScentSubsystem>() : nullptr;
}

bool ULostWithinScentSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId ULostWithinScentSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(ULostWithinScentSubsystem, STATGROUP_Tickables);
}

void ULostWithinScentSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	const float Step = FMath::Max(0.0f, DeltaTime);
	Clock += Step;
	if (bSamplePlayer)
	{
		SamplePlayer(Step);
	}
	PruneTimer -= Step;
	if (PruneTimer <= 0.0f)
	{
		PruneTimer = ScentPruneIntervalSeconds;
		PruneNodes();
	}
}

float ULostWithinScentSubsystem::GetEffectiveStrength(const FLostWithinScentNode& Node) const
{
	const double Age = Clock - Node.Created;
	if (Node.Lifetime <= 0.0f || Age >= Node.Lifetime)
	{
		return 0.0f;
	}
	return Node.Strength * static_cast<float>(1.0 - FMath::Max(0.0, Age) / Node.Lifetime);
}

bool ULostWithinScentSubsystem::IsScentMasked(const FVector& Point) const
{
	if (TorchMaskRadius <= 0.0f)
	{
		return false;
	}
	const ULostWithinLightSubsystem* Light = ULostWithinLightSubsystem::Get(this);
	if (!Light)
	{
		return false;
	}
	TArray<FVector> Torches;
	Light->GetLitTorchLocations(Torches);
	const float RadiusSquared = FMath::Square(TorchMaskRadius);
	for (const FVector& Torch : Torches)
	{
		if (FVector::DistSquared(Torch, Point) <= RadiusSquared)
		{
			return true;
		}
	}
	return false;
}

int32 ULostWithinScentSubsystem::AddNode(const FVector& Location, float Strength, float Lifetime)
{
	if (Strength <= 0.0f || Lifetime <= 0.0f || IsScentMasked(Location))
	{
		return 0;
	}
	FLostWithinScentNode& Node = Nodes.AddDefaulted_GetRef();
	Node.Id = NextNodeId++;
	Node.Location = Location;
	Node.Strength = Strength;
	Node.Lifetime = Lifetime;
	Node.Created = Clock;
	return Node.Id;
}

void ULostWithinScentSubsystem::SamplePlayer(float DeltaTime)
{
	const ALostWithinCharacter* Player = Cast<ALostWithinCharacter>(UGameplayStatics::GetPlayerCharacter(this, 0));
	if (!IsValid(Player) || Player->IsDead())
	{
		bHasLastNode = false;
		bHasPoolAnchor = false;
		StillSeconds = 0.0f;
		return;
	}

	const FVector Location = Player->GetActorLocation();
	if (!bHasLastNode || FVector::Dist2D(Location, LastNodeLocation) >= NodeSpacing)
	{
		const int32 Gait = Player->GetMovementGait();
		const float Strength = Gait >= 2 ? SprintStrength : (Gait == 1 ? RunStrength : WalkStrength);
		AddNode(Location, Strength, Gait >= 2 ? SprintNodeLifetime : NodeLifetime);
		// A masked point still counts as a step, so the trail resumes NodeSpacing past the torch's smoke.
		LastNodeLocation = Location;
		bHasLastNode = true;
	}

	if (!bHasPoolAnchor || FVector::Dist2D(Location, PoolAnchor) > PoolRadius)
	{
		PoolAnchor = Location;
		bHasPoolAnchor = true;
		StillSeconds = 0.0f;
		return;
	}
	StillSeconds += DeltaTime;
	if (StillSeconds >= PoolDelay)
	{
		StillSeconds = 0.0f;
		AddNode(Location, PoolStrength, NodeLifetime);
	}
}

void ULostWithinScentSubsystem::PruneNodes()
{
	TArray<FVector> Torches;
	if (TorchMaskRadius > 0.0f)
	{
		if (const ULostWithinLightSubsystem* Light = ULostWithinLightSubsystem::Get(this))
		{
			Light->GetLitTorchLocations(Torches);
		}
	}
	const float MaskSquared = FMath::Square(TorchMaskRadius);
	Nodes.RemoveAll([this, &Torches, MaskSquared](const FLostWithinScentNode& Node)
	{
		if (GetEffectiveStrength(Node) <= 0.0f)
		{
			return true;
		}
		for (const FVector& Torch : Torches)
		{
			if (FVector::DistSquared(Torch, Node.Location) <= MaskSquared)
			{
				return true;
			}
		}
		return false;
	});
	Items.RemoveAll([this](const FLostWithinItemScent& Scent)
	{
		return !Scent.Item.IsValid() || Clock - Scent.Created >= Scent.Lifetime;
	});
}

const FLostWithinScentNode* ULostWithinScentSubsystem::FindFreshestNodeWithin(const FVector& Point, float Radius, int32 MinId) const
{
	const float RadiusSquared = FMath::Square(Radius);
	// Nodes are stored oldest first, so the first match from the back is the freshest.
	for (int32 Index = Nodes.Num() - 1; Index >= 0; --Index)
	{
		const FLostWithinScentNode& Node = Nodes[Index];
		if (Node.Id <= MinId)
		{
			break;
		}
		if (GetEffectiveStrength(Node) > 0.0f && FVector::DistSquared(Node.Location, Point) <= RadiusSquared)
		{
			return &Node;
		}
	}
	return nullptr;
}

const FLostWithinScentNode* ULostWithinScentSubsystem::FindNextNode(int32 AfterId) const
{
	for (const FLostWithinScentNode& Node : Nodes)
	{
		if (Node.Id > AfterId && GetEffectiveStrength(Node) > 0.0f)
		{
			return &Node;
		}
	}
	return nullptr;
}

const FLostWithinScentNode* ULostWithinScentSubsystem::FindNode(int32 Id) const
{
	for (const FLostWithinScentNode& Node : Nodes)
	{
		if (Node.Id == Id)
		{
			return GetEffectiveStrength(Node) > 0.0f ? &Node : nullptr;
		}
	}
	return nullptr;
}

void ULostWithinScentSubsystem::AddItemScent(AActor* Item, float Strength, float Lifetime)
{
	if (!IsValid(Item) || Strength <= 0.0f || Lifetime <= 0.0f)
	{
		return;
	}
	Items.RemoveAll([Item](const FLostWithinItemScent& Scent) { return Scent.Item.Get() == Item; });
	FLostWithinItemScent& Scent = Items.AddDefaulted_GetRef();
	Scent.Item = Item;
	Scent.Strength = Strength;
	Scent.Lifetime = Lifetime;
	Scent.Created = Clock;
}

float ULostWithinScentSubsystem::GetItemScentStrength(const AActor* Item) const
{
	for (const FLostWithinItemScent& Scent : Items)
	{
		if (Scent.Item.Get() == Item)
		{
			const double Age = Clock - Scent.Created;
			return Age >= Scent.Lifetime ? 0.0f : Scent.Strength * static_cast<float>(1.0 - FMath::Max(0.0, Age) / Scent.Lifetime);
		}
	}
	return 0.0f;
}

bool ULostWithinScentSubsystem::HasItemScent(const AActor* Item) const
{
	return IsValid(Item) && GetItemScentStrength(Item) > 0.0f;
}

AActor* ULostWithinScentSubsystem::FindStrongestItemWithin(const FVector& Point, float Radius,
	const TSet<TWeakObjectPtr<AActor>>& Ignored, float& OutStrength) const
{
	OutStrength = 0.0f;
	AActor* Best = nullptr;
	const float RadiusSquared = FMath::Square(Radius);
	for (const FLostWithinItemScent& Scent : Items)
	{
		AActor* Item = Scent.Item.Get();
		if (!IsValid(Item) || Ignored.Contains(Scent.Item)
			|| FVector::DistSquared(Item->GetActorLocation(), Point) > RadiusSquared || IsScentMasked(Item->GetActorLocation()))
		{
			continue;
		}
		const float Strength = GetItemScentStrength(Item);
		if (Strength > OutStrength)
		{
			OutStrength = Strength;
			Best = Item;
		}
	}
	return Best;
}

void ULostWithinScentSubsystem::ClearScent()
{
	Nodes.Reset();
	Items.Reset();
	bHasLastNode = false;
	bHasPoolAnchor = false;
	StillSeconds = 0.0f;
}
