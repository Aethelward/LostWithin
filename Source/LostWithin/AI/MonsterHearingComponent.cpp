// Copyright Epic Games, Inc. All Rights Reserved.

#include "AI/MonsterHearingComponent.h"

#include "AI/MonsterAIComponent.h"
#include "AIController.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "LostWithin.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"

namespace
{
	/** A move request needs a moment before an Idle path-following status means "done" rather than "not started". */
	constexpr float MinMoveTimeBeforeIdleCountsAsArrived = 0.3f;
	/** One full left-right-left look-around cycle while searching. */
	constexpr float SearchSweepPeriodSeconds = 2.5f;
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
	if (!CanHear(Noise) || IsReactingToCat())
	{
		return false;
	}

	InvestigationLocation = Noise.Location;
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

	// UE5.6 can project a missing-navigation goal to the origin and report AlreadyAtGoal, so check navigation first.
	const EPathFollowingRequestResult::Type Result = UNavigationSystemV1::GetCurrent(GetWorld())
		? Controller->MoveToLocation(InvestigationLocation, AcceptanceRadius, /*bStopOnOverlap=*/true,
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

	const float CapsuleRadius = Character->GetCapsuleComponent() ? Character->GetCapsuleComponent()->GetScaledCapsuleRadius() : 0.0f;
	if (FVector::Dist2D(Character->GetActorLocation(), InvestigationLocation) <= AcceptanceRadius + CapsuleRadius)
	{
		return true;
	}

	// A partial path or a goal that could not be reached ends where the path ends.
	if (bMoveRequested && StateElapsed >= MinMoveTimeBeforeIdleCountsAsArrived)
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
	if (StateElapsed >= SearchDuration)
	{
		Finish(/*bRestoreSpeed=*/true);
		return;
	}

	if (AActor* Owner = GetOwner())
	{
		const float Phase = (StateElapsed / SearchSweepPeriodSeconds) * 2.0f * UE_PI;
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
	bUseDirectMovement = false;
	bMoveRequested = false;
	SetComponentTickEnabled(false);
}
