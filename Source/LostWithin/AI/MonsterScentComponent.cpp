// Copyright Epic Games, Inc. All Rights Reserved.

#include "AI/MonsterScentComponent.h"

#include "AI/MonsterAIComponent.h"
#include "AI/MonsterLightAttractionComponent.h"
#include "AIController.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Gameplay/LostWithinScentSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "LostWithinCharacter.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "Sound/SoundBase.h"

namespace
{
	/** A move toward an unchanged target is re-requested this often, so a blocked request recovers. */
	constexpr float ScentRepathSeconds = 1.0f;
	/** A new target this close to the last one keeps the current move. */
	constexpr float ScentRetargetDistance = 30.0f;
	/** Sniff cadence: slow on a weak (cold) scent, fast on a fresh one. */
	constexpr float ScentSlowSniffInterval = 1.4f;
	constexpr float ScentFastSniffInterval = 0.55f;
	constexpr float ScentFreshThreshold = 0.6f;
}

UMonsterScentComponent::UMonsterScentComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	SetAutoActivate(true);
}

void UMonsterScentComponent::BeginPlay()
{
	Super::BeginPlay();
	ThinkTimer = 0.0f;
}

void UMonsterScentComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopTracking();
	Super::EndPlay(EndPlayReason);
}

bool UMonsterScentComponent::IsActorTrackingScent(const AActor* Actor)
{
	const UMonsterScentComponent* Nose = IsValid(Actor) ? Actor->FindComponentByClass<UMonsterScentComponent>() : nullptr;
	return Nose && Nose->IsTracking();
}

bool UMonsterScentComponent::IsIgnoringItem(const AActor* Item) const
{
	return IgnoredItems.Contains(TWeakObjectPtr<AActor>(const_cast<AActor*>(Item)));
}

UMonsterAIComponent* UMonsterScentComponent::GetMonsterAI() const
{
	const AActor* Owner = GetOwner();
	return Owner ? Owner->FindComponentByClass<UMonsterAIComponent>() : nullptr;
}

bool UMonsterScentComponent::IsReactingToCat() const
{
	const UMonsterAIComponent* MonsterAI = GetMonsterAI();
	return MonsterAI && (MonsterAI->bCatIsNear || MonsterAI->bIsFleeing || MonsterAI->bIsRagdolled);
}

void UMonsterScentComponent::SetState(EMonsterScentState NewState)
{
	if (State == NewState)
	{
		return;
	}
	const EMonsterScentState Previous = State;
	State = NewState;
	if (NewState == EMonsterScentState::AirLock)
	{
		PlayOneShot(AirLockSound);
	}
	if (NewState == EMonsterScentState::Idle)
	{
		bHasMoveTarget = false;
		RestoreSpeed();
	}
	else
	{
		float Speed = TrailSpeed;
		if (NewState == EMonsterScentState::AirLock)
		{
			const UMonsterAIComponent* MonsterAI = GetMonsterAI();
			Speed = AirLockSpeed > 0.0f ? AirLockSpeed
				: (MonsterAI && MonsterAI->PanicGaitSpeed > 0.0f ? MonsterAI->PanicGaitSpeed : TrailSpeed * 1.5f);
		}
		ApplySpeed(Speed);
	}
	if (Previous == EMonsterScentState::AirLock && NewState != EMonsterScentState::AirLock)
	{
		bHasMoveTarget = false; // drop the MoveToActor so the next MoveTo issues a fresh request
	}
	OnScentStateChanged.Broadcast(State);
}

bool UMonsterScentComponent::SmellsPlayerDirectly(const ALostWithinCharacter* Player) const
{
	const AActor* Owner = GetOwner();
	if (!IsValid(Owner) || !IsValid(Player) || Player->IsDead() || AirScentRange <= 0.0f)
	{
		return false;
	}
	const FVector From = Owner->GetActorLocation();
	const FVector To = Player->GetActorLocation();
	if (FVector::Dist(From, To) > AirScentRange)
	{
		return false;
	}
	// Scent travels around corners, not through walls: measure the walking path when navigation exists.
	UWorld* World = GetWorld();
	if (World && UNavigationSystemV1::GetCurrent(World))
	{
		const UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(World, From, To, const_cast<AActor*>(Owner));
		if (Path && Path->IsValid() && !Path->IsPartial())
		{
			return Path->GetPathLength() <= AirScentRange;
		}
	}
	return true;
}

void UMonsterScentComponent::StartTrail(int32 NodeId)
{
	CurrentNodeId = NodeId;
	DecoyTarget.Reset();
	SetState(EMonsterScentState::Trailing);
	if (const ULostWithinScentSubsystem* Scent = ULostWithinScentSubsystem::Get(this))
	{
		if (const FLostWithinScentNode* Node = Scent->FindNode(NodeId))
		{
			MoveTo(Node->Location, /*bForce=*/true);
		}
	}
}

void UMonsterScentComponent::StartCast()
{
	CastOrigin = GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector;
	CastIndex = 0;
	CastElapsed = 0.0f;
	DecoyTarget.Reset();
	SetState(EMonsterScentState::Casting);
	bHasMoveTarget = false;
}

void UMonsterScentComponent::LoseTrail()
{
	if (const ULostWithinScentSubsystem* Scent = ULostWithinScentSubsystem::Get(this))
	{
		IgnoreNodesUpToId = FMath::Max(IgnoreNodesUpToId, Scent->GetLatestNodeId());
	}
	CurrentNodeId = 0;
	PlayOneShot(LostTrailSound);
	StopOwnerMovement();
	SetState(EMonsterScentState::Idle);
}

void UMonsterScentComponent::ReleaseForCatReaction()
{
	// Restore the walk speed but keep the flee move the cat reaction is about to issue.
	RestoreSpeed();
	CurrentNodeId = 0;
	DecoyTarget.Reset();
	if (State != EMonsterScentState::Idle)
	{
		State = EMonsterScentState::Idle;
		bHasMoveTarget = false;
		OnScentStateChanged.Broadcast(State);
	}
}

void UMonsterScentComponent::StopTracking()
{
	if (State != EMonsterScentState::Idle)
	{
		StopOwnerMovement();
	}
	CurrentNodeId = 0;
	DecoyTarget.Reset();
	SetState(EMonsterScentState::Idle);
}

void UMonsterScentComponent::Think()
{
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	const ULostWithinScentSubsystem* Scent = ULostWithinScentSubsystem::Get(this);
	if (!Character || !Scent || !IsActive())
	{
		return;
	}

	if (IsReactingToCat())
	{
		// The cat reaction owns movement and speed now (MonsterAI calls ReleaseForCatReaction before it caches them).
		ReleaseForCatReaction();
		return;
	}

	// Its short sight wins while it sees the player; the nose waits.
	if (UMonsterLightAttractionComponent::IsActorAttractedToLight(Character))
	{
		// The sight chase owns movement now. Keep the walk speed saved before tracking (bSpeedApplied stays set), so
		// once both let go the dog walks at its own speed, not at the trail speed the sight chase cached.
		if (State != EMonsterScentState::Idle)
		{
			CurrentNodeId = 0;
			DecoyTarget.Reset();
			State = EMonsterScentState::Idle;
			bHasMoveTarget = false;
			OnScentStateChanged.Broadcast(State);
		}
		return;
	}

	ALostWithinCharacter* Player = Cast<ALostWithinCharacter>(UGameplayStatics::GetPlayerCharacter(this, 0));
	if (SmellsPlayerDirectly(Player))
	{
		SetState(EMonsterScentState::AirLock);
		ChaseActor(Player);
		return;
	}
	if (State == EMonsterScentState::AirLock)
	{
		// Lost the direct smell: carry on along the freshest trail it can reach, else cast where it stands.
		const FLostWithinScentNode* Node = Scent->FindFreshestNodeWithin(Character->GetActorLocation(), NoseRange, IgnoreNodesUpToId);
		if (Node)
		{
			StartTrail(Node->Id);
		}
		else
		{
			StartCast();
		}
	}

	// A scented item stronger than the trail under its nose draws it away.
	float TrailStrength = 0.0f;
	if (const FLostWithinScentNode* Current = State == EMonsterScentState::Trailing ? Scent->FindNode(CurrentNodeId) : nullptr)
	{
		TrailStrength = Scent->GetEffectiveStrength(*Current);
	}
	else if (const FLostWithinScentNode* Candidate = Scent->FindFreshestNodeWithin(Character->GetActorLocation(), NoseRange, IgnoreNodesUpToId))
	{
		TrailStrength = Scent->GetEffectiveStrength(*Candidate);
	}
	float ItemStrength = 0.0f;
	if (AActor* Item = Scent->FindStrongestItemWithin(Character->GetActorLocation(), NoseRange, IgnoredItems, ItemStrength);
		Item && ItemStrength > TrailStrength && DecoyTarget.Get() != Item)
	{
		DecoyTarget = Item;
		CurrentNodeId = 0;
		SetState(EMonsterScentState::Decoy);
		MoveTo(Item->GetActorLocation(), /*bForce=*/true);
		return;
	}

	switch (State)
	{
	case EMonsterScentState::Idle:
		if (const FLostWithinScentNode* Node = Scent->FindFreshestNodeWithin(Character->GetActorLocation(), NoseRange, IgnoreNodesUpToId))
		{
			StartTrail(Node->Id);
		}
		else
		{
			RestoreSpeed(); // a no-op unless the sight chase took over while it was tracking
		}
		break;
	case EMonsterScentState::Trailing:
		UpdateTrail();
		break;
	case EMonsterScentState::Casting:
		UpdateCast();
		break;
	case EMonsterScentState::Decoy:
		UpdateDecoy();
		break;
	default:
		break;
	}
}

void UMonsterScentComponent::UpdateTrail()
{
	const AActor* Owner = GetOwner();
	const ULostWithinScentSubsystem* Scent = ULostWithinScentSubsystem::Get(this);
	if (!Owner || !Scent)
	{
		return;
	}
	const FVector Here = Owner->GetActorLocation();
	const FLostWithinScentNode* Current = Scent->FindNode(CurrentNodeId);
	FVector BreakPoint = Current ? Current->Location : Here;

	// Advance past every node it has reached; a node that faded or was masked counts as reached where it stands.
	while (!Current || FVector::Dist2D(Here, Current->Location) <= NodeReachRadius)
	{
		const FLostWithinScentNode* Next = Scent->FindNextNode(CurrentNodeId);
		if (!Next)
		{
			// The end of the trail and no player within direct smell: sniff around for where they went.
			StartCast();
			return;
		}
		if (FVector::Dist2D(BreakPoint, Next->Location) > TrailGap)
		{
			StartCast();
			return;
		}
		CurrentNodeId = Next->Id;
		Current = Next;
		BreakPoint = Next->Location;
	}
	MoveTo(Current->Location);
}

void UMonsterScentComponent::UpdateCast()
{
	const AActor* Owner = GetOwner();
	const ULostWithinScentSubsystem* Scent = ULostWithinScentSubsystem::Get(this);
	if (!Owner || !Scent)
	{
		return;
	}
	if (CastElapsed >= CastDuration)
	{
		LoseTrail();
		return;
	}
	// Only a later node close to where it sniffs picks the trail up again.
	if (const FLostWithinScentNode* Node = Scent->FindFreshestNodeWithin(Owner->GetActorLocation(), CastRadius,
		FMath::Max(CurrentNodeId, IgnoreNodesUpToId)))
	{
		StartTrail(Node->Id);
		return;
	}
	const int32 NumPoints = FMath::Max(1, CastPoints);
	const float Angle = (2.0f * UE_PI * static_cast<float>(CastIndex % NumPoints)) / static_cast<float>(NumPoints) + 0.6f;
	const FVector CastPoint = CastOrigin + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f) * CastRadius;
	if (bHasMoveTarget && FVector::Dist2D(Owner->GetActorLocation(), MoveTarget) <= NodeReachRadius)
	{
		++CastIndex;
		bHasMoveTarget = false;
		return;
	}
	MoveTo(CastPoint);
}

void UMonsterScentComponent::UpdateDecoy()
{
	const AActor* Owner = GetOwner();
	AActor* Item = DecoyTarget.Get();
	const ULostWithinScentSubsystem* Scent = ULostWithinScentSubsystem::Get(this);
	if (!Owner || !Scent || !IsValid(Item) || !Scent->HasItemScent(Item))
	{
		DecoyTarget.Reset();
		SetState(EMonsterScentState::Idle);
		return;
	}
	if (FVector::Dist2D(Owner->GetActorLocation(), Item->GetActorLocation()) <= NodeReachRadius + 30.0f)
	{
		// Only the item, no player: it ignores this one from now on.
		IgnoredItems.Add(Item);
		DecoyTarget.Reset();
		PlayOneShot(LostTrailSound);
		StartCast();
		return;
	}
	MoveTo(Item->GetActorLocation());
}

void UMonsterScentComponent::MoveTo(const FVector& Target, bool bForce)
{
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!Character)
	{
		return;
	}
	const bool bNewTarget = !bHasMoveTarget || FVector::Dist(Target, MoveTarget) > ScentRetargetDistance;
	if (!bForce && !bNewTarget && SinceMoveRequest < ScentRepathSeconds)
	{
		return;
	}
	MoveTarget = Target;
	bHasMoveTarget = true;
	SinceMoveRequest = 0.0f;

	AAIController* Controller = Cast<AAIController>(Character->GetController());
	const bool bHasNav = UNavigationSystemV1::GetCurrent(GetWorld()) != nullptr;
	if (Controller && bHasNav)
	{
		const EPathFollowingRequestResult::Type Result = Controller->MoveToLocation(Target, 20.0f, /*bStopOnOverlap=*/false,
			/*bUsePathfinding=*/true, /*bProjectDestinationToNavigation=*/true, /*bCanStrafe=*/false,
			/*FilterClass=*/nullptr, /*bAllowPartialPath=*/true);
		bUseDirectMovement = Result == EPathFollowingRequestResult::Failed;
	}
	else
	{
		bUseDirectMovement = true;
	}
}

void UMonsterScentComponent::ChaseActor(AActor* Target)
{
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!Character || !IsValid(Target))
	{
		return;
	}
	MoveTarget = Target->GetActorLocation();
	bHasMoveTarget = true;
	AAIController* Controller = Cast<AAIController>(Character->GetController());
	if (Controller && UNavigationSystemV1::GetCurrent(GetWorld()))
	{
		// No stop-on-overlap: it keeps going until its body touches the player.
		bUseDirectMovement = Controller->MoveToActor(Target, 5.0f, /*bStopOnOverlap=*/false) == EPathFollowingRequestResult::Failed;
	}
	else
	{
		bUseDirectMovement = true;
	}
}

void UMonsterScentComponent::ApplySpeed(float Speed)
{
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	if (!Movement)
	{
		return;
	}
	if (!bSpeedApplied)
	{
		SavedMaxWalkSpeed = Movement->MaxWalkSpeed;
		bSpeedApplied = true;
	}
	if (Speed > 0.0f)
	{
		Movement->MaxWalkSpeed = Speed;
	}
}

void UMonsterScentComponent::RestoreSpeed()
{
	if (!bSpeedApplied)
	{
		return;
	}
	bSpeedApplied = false;
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr)
	{
		Movement->MaxWalkSpeed = SavedMaxWalkSpeed;
	}
}

void UMonsterScentComponent::StopOwnerMovement() const
{
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!Character)
	{
		return;
	}
	if (AAIController* Controller = Cast<AAIController>(Character->GetController()))
	{
		Controller->StopMovement();
	}
	if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
	{
		Movement->StopActiveMovement();
	}
}

void UMonsterScentComponent::PlayOneShot(USoundBase* Sound) const
{
	const AActor* Owner = GetOwner();
	if (Sound && Owner && Owner->GetWorld() && Owner->GetWorld()->IsGameWorld() && Owner->GetNetMode() != NM_DedicatedServer)
	{
		UGameplayStatics::PlaySoundAtLocation(this, Sound, Owner->GetActorLocation(), 1.0f, 1.0f, 0.0f, SoundAttenuation);
	}
}

void UMonsterScentComponent::PlaySniff(float DeltaTime)
{
	if (SniffSounds.IsEmpty() || (State != EMonsterScentState::Trailing && State != EMonsterScentState::Casting))
	{
		SniffTimer = 0.0f;
		return;
	}
	SniffTimer -= DeltaTime;
	if (SniffTimer > 0.0f)
	{
		return;
	}
	float Freshness = 0.0f;
	if (const ULostWithinScentSubsystem* Scent = ULostWithinScentSubsystem::Get(this))
	{
		if (const FLostWithinScentNode* Node = Scent->FindNode(CurrentNodeId))
		{
			Freshness = Node->Strength > 0.0f ? Scent->GetEffectiveStrength(*Node) / Node->Strength : 0.0f;
		}
	}
	SniffTimer = Freshness >= ScentFreshThreshold ? ScentFastSniffInterval : ScentSlowSniffInterval;
	PlayOneShot(SniffSounds[FMath::RandRange(0, SniffSounds.Num() - 1)]);
}

void UMonsterScentComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	SinceMoveRequest += DeltaTime;
	if (State == EMonsterScentState::Casting)
	{
		CastElapsed += DeltaTime;
	}

	ThinkTimer -= DeltaTime;
	if (ThinkTimer <= 0.0f)
	{
		ThinkTimer = ThinkInterval;
		Think();
	}

	if (State != EMonsterScentState::Idle && bUseDirectMovement && bHasMoveTarget)
	{
		if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
		{
			FVector Target = MoveTarget;
			if (State == EMonsterScentState::AirLock)
			{
				if (const APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0))
				{
					Target = Player->GetActorLocation();
				}
			}
			Character->AddMovementInput((Target - Character->GetActorLocation()).GetSafeNormal2D(), 1.0f);
		}
	}
	PlaySniff(DeltaTime);
}
