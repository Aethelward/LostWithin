// Copyright Epic Games, Inc. All Rights Reserved.

#include "AI/MonsterVibrationComponent.h"

#include "AI/MonsterAIComponent.h"
#include "AIController.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"

namespace
{
	constexpr float VibrationMinMoveTimeBeforeIdleCountsAsArrived = 0.3f;
	constexpr float VibrationSearchSweepPeriodSeconds = 2.5f;
}

UMonsterVibrationComponent::UMonsterVibrationComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	SetAutoActivate(true);
}

void UMonsterVibrationComponent::BeginPlay()
{
	Super::BeginPlay();

	// Enforce complete blindness on the monster AI if enabled.
	if (bBlind && bEnforceBlindnessOnAI)
	{
		if (UMonsterAIComponent* MonsterAI = GetMonsterAI())
		{
			MonsterAI->PlayerDetectionRadius = 0.0f;
			MonsterAI->PeripheralVisionAngle = 0.0f;
		}
	}

	// Hook into the world noise subsystem to intercept footstep and tremor events.
	if (UWorld* World = GetWorld())
	{
		if (ULostWithinNoiseSubsystem* Subsystem = World->GetSubsystem<ULostWithinNoiseSubsystem>())
		{
			NoiseBroadcastHandle = Subsystem->OnNoiseBroadcast.AddUObject(this, &UMonsterVibrationComponent::HandleNoiseBroadcast);
		}
	}
}

void UMonsterVibrationComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		if (ULostWithinNoiseSubsystem* Subsystem = World->GetSubsystem<ULostWithinNoiseSubsystem>())
		{
			Subsystem->OnNoiseBroadcast.Remove(NoiseBroadcastHandle);
		}
	}

	Finish(/*bRestoreSpeed=*/true);
	Super::EndPlay(EndPlayReason);
}

UMonsterAIComponent* UMonsterVibrationComponent::GetMonsterAI() const
{
	const AActor* Owner = GetOwner();
	return Owner ? Owner->FindComponentByClass<UMonsterAIComponent>() : nullptr;
}

bool UMonsterVibrationComponent::IsReactingToCat() const
{
	const UMonsterAIComponent* MonsterAI = GetMonsterAI();
	return MonsterAI && (MonsterAI->bCatIsNear || MonsterAI->bIsFleeing || MonsterAI->bIsRagdolled);
}

bool UMonsterVibrationComponent::IsActorInvestigating(const AActor* Actor)
{
	const UMonsterVibrationComponent* Comp = FindVibrationComponent(Actor);
	return Comp && Comp->IsInvestigating();
}

UMonsterVibrationComponent* UMonsterVibrationComponent::FindVibrationComponent(const AActor* Actor)
{
	return IsValid(Actor) ? Actor->FindComponentByClass<UMonsterVibrationComponent>() : nullptr;
}

void UMonsterVibrationComponent::SetState(EMonsterVibrationState NewState)
{
	if (State != NewState)
	{
		State = NewState;
		OnStateChanged.Broadcast(State);
	}
}

bool UMonsterVibrationComponent::CanSenseFloorVibration(const FVector& Location, float Radius, const AActor* InstigatorActor) const
{
	const AActor* Owner = GetOwner();
	if (!IsActive() || !IsValid(Owner) || Radius <= 0.0f)
	{
		return false;
	}

	const FVector OwnerLocation = Owner->GetActorLocation();

	// Check floor contact and vertical separation.
	if (bRequireFloorContact)
	{
		const float ZDiff = FMath::Abs(OwnerLocation.Z - Location.Z);
		if (ZDiff > MaxFloorZDifference)
		{
			return false;
		}

		if (const ACharacter* Character = Cast<ACharacter>(Owner))
		{
			if (const UCharacterMovementComponent* MoveComp = Character->GetCharacterMovement())
			{
				if (!MoveComp->IsMovingOnGround())
				{
					return false;
				}
			}
		}
	}

	const double DistSq2D = FVector::DistSquared2D(OwnerLocation, Location);
	return DistSq2D <= FMath::Square(static_cast<double>(Radius));
}

bool UMonsterVibrationComponent::CanSenseTurbulence(const AActor* PotentialActor) const
{
	const AActor* Owner = GetOwner();
	if (!IsActive() || !IsValid(Owner) || !IsValid(PotentialActor))
	{
		return false;
	}

	const FVector OwnerLocation = Owner->GetActorLocation();
	const FVector ActorLocation = PotentialActor->GetActorLocation();

	const float ZDiff = FMath::Abs(OwnerLocation.Z - ActorLocation.Z);
	if (ZDiff > MaxFloorZDifference)
	{
		return false;
	}

	const double DistSq2D = FVector::DistSquared2D(OwnerLocation, ActorLocation);
	if (DistSq2D > FMath::Square(static_cast<double>(TurbulenceRadius)))
	{
		return false;
	}

	const float Speed = PotentialActor->GetVelocity().Size();
	return Speed >= TurbulenceSpeedThreshold;
}

bool UMonsterVibrationComponent::SenseFloorVibration(const FVector& Location, float Radius, AActor* InstigatorActor)
{
	if (!CanSenseFloorVibration(Location, Radius, InstigatorActor) || IsReactingToCat())
	{
		return false;
	}

	InvestigationLocation = Location;
	LastSenseKind = (Radius >= (LandingVibrationRadius - 50.0f))
		? EMonsterVibrationSenseKind::LandingTremor
		: EMonsterVibrationSenseKind::SprintTremor;

	if (State == EMonsterVibrationState::Idle)
	{
		ApplyInvestigateSpeed();
	}

	SetState(EMonsterVibrationState::Investigating);
	StateElapsed = 0.0f;
	bUseDirectMovement = false;
	IssueMove();

	OnVibrationSensed.Broadcast(InvestigationLocation, LastSenseKind);
	return true;
}

bool UMonsterVibrationComponent::SenseTurbulence(const FVector& Location, AActor* InstigatorActor)
{
	if (IsReactingToCat())
	{
		return false;
	}

	InvestigationLocation = Location;
	LastSenseKind = EMonsterVibrationSenseKind::ProximityTurbulence;

	if (UMonsterAIComponent* MonsterAI = GetMonsterAI())
	{
		MonsterAI->bPlayerIsNear = true;
	}

	if (State == EMonsterVibrationState::Idle)
	{
		ApplyInvestigateSpeed();
	}

	SetState(EMonsterVibrationState::Investigating);
	StateElapsed = 0.0f;
	bUseDirectMovement = false;
	IssueMove();

	OnVibrationSensed.Broadcast(InvestigationLocation, LastSenseKind);
	return true;
}

void UMonsterVibrationComponent::HandleNoiseBroadcast(const FLostWithinNoiseEvent& Noise)
{
	// Airborne noises (dropped items, toolboxes) are ignored by the blind and deaf monster.
	if (bDeafToAirborne && Noise.Kind != ELostWithinNoiseKind::Footstep)
	{
		return;
	}

	// Only footsteps that carry enough force (sprints or landings) generate floor tremors.
	if (Noise.Kind == ELostWithinNoiseKind::Footstep)
	{
		bool bIsTremor = Noise.Radius >= (SprintVibrationRadius * 0.6f);
		if (!bIsTremor && IsValid(Noise.NoiseMaker))
		{
			const float MakerSpeed = Noise.NoiseMaker->GetVelocity().Size2D();
			bIsTremor = MakerSpeed >= SprintSpeedThreshold;
		}

		if (bIsTremor)
		{
			SenseFloorVibration(Noise.Location, Noise.Radius, Noise.NoiseMaker);
		}
	}
}

void UMonsterVibrationComponent::CheckPlayerProximityAndMovement()
{
	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!IsValid(PlayerPawn))
	{
		return;
	}

	const ACharacter* PlayerChar = Cast<ACharacter>(PlayerPawn);
	const UCharacterMovementComponent* PlayerMove = PlayerChar ? PlayerChar->GetCharacterMovement() : nullptr;

	// 1. Close-range air turbulence check: Moving while within TurbulenceRadius alerts the monster.
	if (CanSenseTurbulence(PlayerPawn))
	{
		SenseTurbulence(PlayerPawn->GetActorLocation(), PlayerPawn);
	}

	// 2. Landing tremor detection from movement mode transitions.
	if (PlayerMove)
	{
		const bool bIsOnGround = PlayerMove->IsMovingOnGround();
		if (bLastPlayerWasFalling && bIsOnGround)
		{
			// Landing occurred this frame.
			SenseFloorVibration(PlayerPawn->GetActorLocation(), LandingVibrationRadius, PlayerPawn);
		}
		bLastPlayerWasFalling = PlayerMove->IsFalling();

		// 3. Continuous sprint floor vibration detection.
		if (bIsOnGround && PlayerMove->Velocity.Size2D() >= SprintSpeedThreshold)
		{
			SenseFloorVibration(PlayerPawn->GetActorLocation(), SprintVibrationRadius, PlayerPawn);
		}
	}
}

void UMonsterVibrationComponent::StopInvestigating()
{
	if (IsInvestigating())
	{
		StopOwnerMovement();
	}
	Finish(/*bRestoreSpeed=*/true);
}

float UMonsterVibrationComponent::ResolveInvestigateSpeed(const ACharacter* Character) const
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

void UMonsterVibrationComponent::ApplyInvestigateSpeed()
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

void UMonsterVibrationComponent::RestoreWalkSpeed()
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

void UMonsterVibrationComponent::IssueMove()
{
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	AAIController* Controller = Character ? Cast<AAIController>(Character->GetController()) : nullptr;
	bMoveRequested = false;
	if (!Controller)
	{
		bUseDirectMovement = true;
		return;
	}

	const EPathFollowingRequestResult::Type Result = UNavigationSystemV1::GetCurrent(GetWorld())
		? Controller->MoveToLocation(InvestigationLocation, AcceptanceRadius, /*bStopOnOverlap=*/false,
			/*bUsePathfinding=*/true, /*bProjectDestinationToNavigation=*/true, /*bCanStrafe=*/false,
			/*FilterClass=*/nullptr, /*bAllowPartialPath=*/true)
		: EPathFollowingRequestResult::Failed;

	bUseDirectMovement = (Result == EPathFollowingRequestResult::Failed);
	bMoveRequested = (Result == EPathFollowingRequestResult::RequestSuccessful);
	if (bUseDirectMovement)
	{
		Controller->StopMovement();
	}
}

void UMonsterVibrationComponent::StopOwnerMovement() const
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

bool UMonsterVibrationComponent::HasArrived() const
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

	if (bMoveRequested && StateElapsed >= VibrationMinMoveTimeBeforeIdleCountsAsArrived)
	{
		if (const AAIController* Controller = Cast<AAIController>(Character->GetController()))
		{
			return Controller->GetMoveStatus() == EPathFollowingStatus::Idle;
		}
	}
	return false;
}

void UMonsterVibrationComponent::BeginSearch()
{
	StopOwnerMovement();
	SetState(EMonsterVibrationState::Searching);
	StateElapsed = 0.0f;
	SearchBaseYaw = GetOwner() ? GetOwner()->GetActorRotation().Yaw : 0.0f;
}

void UMonsterVibrationComponent::UpdateMoving(float DeltaTime)
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

void UMonsterVibrationComponent::UpdateSearching(float DeltaTime)
{
	if (StateElapsed >= SearchDuration)
	{
		Finish(/*bRestoreSpeed=*/true);
		return;
	}

	if (AActor* Owner = GetOwner())
	{
		const float Phase = (StateElapsed / VibrationSearchSweepPeriodSeconds) * 2.0f * UE_PI;
		FRotator Rotation = Owner->GetActorRotation();
		Rotation.Yaw = SearchBaseYaw + FMath::Sin(Phase) * SearchSweepAngle;
		Owner->SetActorRotation(Rotation);
	}
}

void UMonsterVibrationComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (IsReactingToCat())
	{
		bSpeedApplied = false;
		Finish(/*bRestoreSpeed=*/false);
		return;
	}

	CheckPlayerProximityAndMovement();

	if (State == EMonsterVibrationState::Idle)
	{
		return;
	}

	StateElapsed += DeltaTime;
	if (State == EMonsterVibrationState::Investigating)
	{
		UpdateMoving(DeltaTime);
	}
	else if (State == EMonsterVibrationState::Searching)
	{
		UpdateSearching(DeltaTime);
	}
}

void UMonsterVibrationComponent::Finish(bool bRestoreSpeed)
{
	if (bRestoreSpeed)
	{
		RestoreWalkSpeed();
	}
	SetState(EMonsterVibrationState::Idle);
	LastSenseKind = EMonsterVibrationSenseKind::None;
	StateElapsed = 0.0f;
	bUseDirectMovement = false;
	bMoveRequested = false;
}

int32 UMonsterVibrationComponent::ReportVibration(const UObject* WorldContextObject, FVector Location, float Radius,
	EMonsterVibrationSenseKind Kind, AActor* Instigator)
{
	if (Radius <= 0.0f || !FMath::IsFinite(Radius))
	{
		return 0;
	}

	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (!World)
	{
		return 0;
	}

	int32 Reacted = 0;
	for (TActorIterator<ACharacter> It(World); It; ++It)
	{
		ACharacter* Character = *It;
		if (UMonsterVibrationComponent* Vibration = FindVibrationComponent(Character))
		{
			const bool bSuccess = (Kind == EMonsterVibrationSenseKind::ProximityTurbulence)
				? Vibration->SenseTurbulence(Location, Instigator)
				: Vibration->SenseFloorVibration(Location, Radius, Instigator);
			if (bSuccess)
			{
				++Reacted;
			}
		}
	}
	return Reacted;
}
