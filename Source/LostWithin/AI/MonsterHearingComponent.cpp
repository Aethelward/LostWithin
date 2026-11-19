// Copyright Epic Games, Inc. All Rights Reserved.

#include "AI/MonsterHearingComponent.h"

#include "AI/MonsterAIComponent.h"
#include "AI/MonsterLightAttractionComponent.h"
#include "AI/MonsterLightAverseComponent.h"
#include "AI/MonsterSightFreezeComponent.h"
#include "AIController.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "Gameplay/LostWithinLightSubsystem.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "LostWithin.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"

namespace
{
	/** A move request needs a moment before an Idle path-following status means "done" rather than "not started". */
	constexpr float HearingMinMoveTimeBeforeIdleCountsAsArrived = 0.3f;
	/** One full left-right-left look-around cycle while searching. */
	constexpr float HearingSearchSweepPeriodSeconds = 2.5f;
}

UMonsterHearingComponent::UMonsterHearingComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	SetAutoActivate(true);
}

void UMonsterHearingComponent::BeginPlay()
{
	Super::BeginPlay();
	if (UWorld* World = GetWorld())
	{
		if (ULostWithinNoiseSubsystem* Noise = World->GetSubsystem<ULostWithinNoiseSubsystem>())
		{
			Noise->RegisterListener(this);
		}
	}
}

void UMonsterHearingComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		if (ULostWithinNoiseSubsystem* Noise = World->GetSubsystem<ULostWithinNoiseSubsystem>())
		{
			Noise->UnregisterListener(this);
		}
	}
	Finish(/*bRestoreSpeed=*/true);
	Super::EndPlay(EndPlayReason);
}

bool UMonsterHearingComponent::IsActorInvestigating(const AActor* Actor)
{
	const UMonsterHearingComponent* Hearing = IsValid(Actor) ? Actor->FindComponentByClass<UMonsterHearingComponent>() : nullptr;
	return Hearing && Hearing->IsInvestigating();
}

UMonsterAIComponent* UMonsterHearingComponent::GetMonsterAI() const
{
	const AActor* Owner = GetOwner();
	return Owner ? Owner->FindComponentByClass<UMonsterAIComponent>() : nullptr;
}

bool UMonsterHearingComponent::IsReactingToCat() const
{
	const UMonsterAIComponent* MonsterAI = GetMonsterAI();
	return MonsterAI && (MonsterAI->bCatIsNear || MonsterAI->bIsFleeing || MonsterAI->bIsRagdolled);
}

bool UMonsterHearingComponent::CanHear(const FLostWithinNoiseEvent& Noise) const
{
	const AActor* Owner = GetOwner();
	if (!IsActive() || !IsValid(Owner) || Noise.Radius <= 0.0f || HearingRangeMultiplier <= 0.0f)
	{
		return false;
	}

	switch (Noise.Kind)
	{
	case ELostWithinNoiseKind::Footstep:
		if (!bHearFootsteps) return false;
		if (Noise.Radius < MinFootstepNoiseRadius) return false;
		break;
	case ELostWithinNoiseKind::ItemImpact:
		if (!bHearItemImpacts) return false;
		break;
	default:
		if (!bHearOtherNoises) return false;
		break;
	}

	const double Reach = static_cast<double>(Noise.Radius) * HearingRangeMultiplier;
	return FVector::DistSquared(Owner->GetActorLocation(), Noise.Location) <= FMath::Square(Reach);
}

bool UMonsterHearingComponent::HearNoise(const FLostWithinNoiseEvent& Noise)
{
	if (!CanHear(Noise) || IsReactingToCat() || UMonsterSightFreezeComponent::IsActorSightFrozen(GetOwner())
		|| UMonsterLightAverseComponent::IsActorLightRepelled(GetOwner())
		|| UMonsterLightAttractionComponent::IsActorAttractedToLight(GetOwner()))
	{
		return false;
	}

	// Distraction priority: an active item distraction investigation (thrown/dropped item)
	// reliably draws the monster away from doors/puzzles and cannot be cancelled by ordinary footsteps.
	if (bPrioritizeItemDistractions && State != EInvestigationState::Idle && CurrentNoiseKind == ELostWithinNoiseKind::ItemImpact)
	{
		if (Noise.Kind == ELostWithinNoiseKind::Footstep)
		{
			const AActor* Owner = GetOwner();
			const double DistToNoiseSq = Owner ? FVector::DistSquared(Owner->GetActorLocation(), Noise.Location) : MAX_flt;
			const double DistToItemSq = Owner ? FVector::DistSquared(Owner->GetActorLocation(), InvestigationLocation) : 0.0;
			// Only interrupt if the footstep is right next to the monster (closer than ratio) AND it is a loud sprint
			const bool bIsUrgentProximity = DistToNoiseSq < DistToItemSq * FMath::Square(static_cast<double>(DistractionInterruptDistanceRatio));
			const bool bIsLoudSprint = Noise.Radius >= SprintInterruptNoiseRadius;
			if (!bIsUrgentProximity || !bIsLoudSprint)
			{
				return false;
			}
		}
	}

	InvestigationLocation = ResolveInvestigationTarget(Noise.Location);
	CurrentNoiseKind = Noise.Kind;
	if (State == EInvestigationState::Idle)
	{
		ApplyInvestigateSpeed();
	}

	State = EInvestigationState::Moving;
	StateElapsed = 0.0f;
	bUseDirectMovement = false;
	IssueMove();
	SetComponentTickEnabled(true);

	OnNoiseHeard.Broadcast(Noise.Location, Noise.Kind);
	return true;
}

void UMonsterHearingComponent::StopInvestigating()
{
	if (IsInvestigating())
	{
		StopOwnerMovement();
	}
	Finish(/*bRestoreSpeed=*/true);
}

FVector UMonsterHearingComponent::ResolveInvestigationTarget(const FVector& Source)
{
	const AActor* Owner = GetOwner();
	const ULostWithinLightSubsystem* Light = ULostWithinLightSubsystem::Get(this);
	bLitAtLastNoise = Owner && Light && Light->IsPointLit(Owner->GetActorLocation() + FVector(0.0f, 0.0f, 40.0f), Owner);
	LastPinpointError = bLitAtLastNoise ? PinpointErrorInLight : PinpointErrorInDark;
	if (LastPinpointError <= 0.0f)
	{
		return Source;
	}

	// A random horizontal miss of 40-100% of the error, so it never lands right on the source.
	const float Angle = FMath::FRandRange(0.0f, 2.0f * PI);
	const float Miss = LastPinpointError * FMath::FRandRange(0.4f, 1.0f);
	const FVector Guess = Source + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f) * Miss;

	// Keep it reachable: the nearest navigable point, unless that would pull it back onto the source.
	if (UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(GetWorld()))
	{
		FNavLocation Projected;
		if (NavSys->ProjectPointToNavigation(Guess, Projected, FVector(300.0f, 300.0f, 250.0f))
			&& FVector::Dist2D(Projected.Location, Source) >= 0.4f * LastPinpointError)
		{
			return Projected.Location;
		}
	}
	return Guess;
}

float UMonsterHearingComponent::ResolveInvestigateSpeed(const ACharacter* Character) const
{
	if (InvestigateSpeed > 0.0f)
	{
		return InvestigateSpeed;
	}
	if (const UMonsterAIComponent* MonsterAI = GetMonsterAI(); MonsterAI && MonsterAI->PanicGaitSpeed > 0.0f)
	{
		return MonsterAI->PanicGaitSpeed;
	}
	const UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	return Movement ? Movement->MaxWalkSpeed * FallbackSpeedMultiplier : 0.0f;
}

void UMonsterHearingComponent::ApplyInvestigateSpeed()
{
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	if (!Movement || bSpeedApplied)
	{
		return;
	}

	SavedMaxWalkSpeed = Movement->MaxWalkSpeed;
	const float Speed = ResolveInvestigateSpeed(Character);
	if (Speed > 0.0f)
	{
		Movement->MaxWalkSpeed = Speed;
	}
	bSpeedApplied = true;
}

void UMonsterHearingComponent::RestoreWalkSpeed()
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

void UMonsterHearingComponent::IssueMove()
{
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	AAIController* Controller = Character ? Cast<AAIController>(Character->GetController()) : nullptr;
	bMoveRequested = false;
	if (!Controller)
	{
		bUseDirectMovement = true;
		return;
	}

	FVector MoveTarget = InvestigationLocation;
	UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(GetWorld());
	if (NavSys)
	{
		// Project destination onto navigation floor so impacts on doors/walls/tables find a walkable floor point
		FNavLocation ProjectedNav;
		if (NavSys->ProjectPointToNavigation(InvestigationLocation, ProjectedNav, FVector(300.0f, 300.0f, 500.0f)))
		{
			MoveTarget = ProjectedNav.Location;
		}
	}

	// UE5.6 can project a missing-navigation goal to the origin and report AlreadyAtGoal, so check navigation first.
	const EPathFollowingRequestResult::Type Result = NavSys
		? Controller->MoveToLocation(MoveTarget, AcceptanceRadius, /*bStopOnOverlap=*/false,
			/*bUsePathfinding=*/true, /*bProjectDestinationToNavigation=*/true, /*bCanStrafe=*/false,
			/*FilterClass=*/nullptr, /*bAllowPartialPath=*/true)
		: EPathFollowingRequestResult::Failed;

	bUseDirectMovement = Result == EPathFollowingRequestResult::Failed;
	bMoveRequested = Result == EPathFollowingRequestResult::RequestSuccessful;
	if (bUseDirectMovement)
	{
		Controller->StopMovement();
	}
}

void UMonsterHearingComponent::StopOwnerMovement() const
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

bool UMonsterHearingComponent::HasArrived() const
{
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!Character)
	{
		return true;
	}

	// Arrived means the monster stands on the source, not a capsule-plus-radius in front of it.
	if (FVector::Dist2D(Character->GetActorLocation(), InvestigationLocation) <= AcceptanceRadius)
	{
		return true;
	}

	// A partial path or a goal that could not be reached ends where the path ends.
	if (bMoveRequested && StateElapsed >= HearingMinMoveTimeBeforeIdleCountsAsArrived)
	{
		if (const AAIController* Controller = Cast<AAIController>(Character->GetController()))
		{
			return Controller->GetMoveStatus() == EPathFollowingStatus::Idle;
		}
	}
	return false;
}

void UMonsterHearingComponent::BeginSearch()
{
	StopOwnerMovement();
	State = EInvestigationState::Searching;
	StateElapsed = 0.0f;
	SearchBaseYaw = GetOwner() ? GetOwner()->GetActorRotation().Yaw : 0.0f;
}

void UMonsterHearingComponent::UpdateMoving(float DeltaTime)
{
	if (HasArrived() || StateElapsed >= GiveUpTime)
	{
		BeginSearch();
		return;
	}

	if (bUseDirectMovement)
	{
		if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
		{
			const FVector Direction = (InvestigationLocation - Character->GetActorLocation()).GetSafeNormal2D();
			Character->AddMovementInput(Direction);
		}
	}
}

void UMonsterHearingComponent::UpdateSearching(float DeltaTime)
{
	const float EffectiveSearchDuration = (CurrentNoiseKind == ELostWithinNoiseKind::ItemImpact && ItemDistractionSearchDuration > 0.0f)
		? ItemDistractionSearchDuration
		: SearchDuration;

	if (StateElapsed >= EffectiveSearchDuration)
	{
		Finish(/*bRestoreSpeed=*/true);
		return;
	}

	if (AActor* Owner = GetOwner())
	{
		const float Phase = (StateElapsed / HearingSearchSweepPeriodSeconds) * 2.0f * UE_PI;
		FRotator Rotation = Owner->GetActorRotation();
		Rotation.Yaw = SearchBaseYaw + FMath::Sin(Phase) * SearchSweepAngle;
		Owner->SetActorRotation(Rotation);
	}
}

void UMonsterHearingComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (State == EInvestigationState::Idle)
	{
		SetComponentTickEnabled(false);
		return;
	}

	if (IsReactingToCat())
	{
		// The cat panic caches and restores the walk speed itself; restoring here would fight it.
		bSpeedApplied = false;
		Finish(/*bRestoreSpeed=*/false);
		return;
	}

	StateElapsed += DeltaTime;
	if (State == EInvestigationState::Moving)
	{
		UpdateMoving(DeltaTime);
	}
	else
	{
		UpdateSearching(DeltaTime);
	}
}

void UMonsterHearingComponent::Finish(bool bRestoreSpeed)
{
	if (bRestoreSpeed)
	{
		RestoreWalkSpeed();
	}
	State = EInvestigationState::Idle;
	StateElapsed = 0.0f;
	CurrentNoiseKind = ELostWithinNoiseKind::Other;
	bUseDirectMovement = false;
	bMoveRequested = false;
	SetComponentTickEnabled(false);
}
