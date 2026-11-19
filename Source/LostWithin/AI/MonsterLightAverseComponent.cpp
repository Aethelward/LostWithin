// Copyright Epic Games, Inc. All Rights Reserved.

#include "AI/MonsterLightAverseComponent.h"

#include "AI/MonsterAIComponent.h"
#include "AI/MonsterHearingComponent.h"
#include "AIController.h"
#include "Gameplay/LostWithinLightSubsystem.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SpotLightComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Navigation/PathFollowingComponent.h"
#include "LostWithinCharacter.h"
#include "NavigationSystem.h"

UMonsterLightAverseComponent::UMonsterLightAverseComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

void UMonsterLightAverseComponent::RegisterComponentTickFunctions(bool bRegister)
{
	Super::RegisterComponentTickFunctions(bRegister);
	if (AActor* Owner = GetOwner())
	{
		if (bRegister)
		{
			RemoveTickPrerequisiteActor(Owner);
			Owner->AddTickPrerequisiteComponent(this);
			if (ACharacter* Character = Cast<ACharacter>(Owner); Character && Character->GetCharacterMovement())
			{
				Character->GetCharacterMovement()->AddTickPrerequisiteComponent(this);
			}
		}
		else
		{
			Owner->RemoveTickPrerequisiteComponent(this);
			if (ACharacter* Character = Cast<ACharacter>(Owner); Character && Character->GetCharacterMovement())
			{
				Character->GetCharacterMovement()->RemoveTickPrerequisiteComponent(this);
			}
		}
	}
}

void UMonsterLightAverseComponent::BeginPlay()
{
	Super::BeginPlay();
	if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		if (UCharacterMovementComponent* Move = Character->GetCharacterMovement())
		{
			SavedMaxWalkSpeed = Move->MaxWalkSpeed;
		}
	}
}

void UMonsterLightAverseComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopRepulsion();
	Super::EndPlay(EndPlayReason);
}

void UMonsterLightAverseComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	UpdateExposureAndRepulsion(DeltaTime);
}

bool UMonsterLightAverseComponent::IsActorLightRepelled(const AActor* Actor)
{
	if (!IsValid(Actor))
	{
		return false;
	}
	const UMonsterLightAverseComponent* LightAverse = Actor->FindComponentByClass<UMonsterLightAverseComponent>();
	return LightAverse && LightAverse->IsRepelled();
}

bool UMonsterLightAverseComponent::IsActorDarkStalkingPlayer(const AActor* Actor)
{
	const UMonsterLightAverseComponent* LightAverse = IsValid(Actor) ? Actor->FindComponentByClass<UMonsterLightAverseComponent>() : nullptr;
	return LightAverse && LightAverse->IsChasingPlayer();
}

void UMonsterLightAverseComponent::StopOwnerMove() const
{
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (AAIController* AI = Character ? Cast<AAIController>(Character->GetController()) : nullptr)
	{
		AI->StopMovement();
	}
}

void UMonsterLightAverseComponent::EndChase()
{
	if (bChasingPlayer)
	{
		bChasingPlayer = false;
		StopOwnerMove();
	}
}

void UMonsterLightAverseComponent::EndRepulsionNow()
{
	StopOwnerMove(); // the retreat ends in this frame
	CurrentLightState = EGhoulLightState::DarkStalking;
	bIsRepelled = false;
	bIsShieldingFace = false;
	bIsRetreating = false;
	bIsStalkingInDarkness = true;
	StateElapsed = 0.0f;
	RepathTimer = 0.0f;
	bDirectMovementFallback = false;
	RestoreWalkSpeed();
	OnLightRecovered.Broadcast();
	OnDarkStalkChanged.Broadcast(true);
}

ALostWithinCharacter* UMonsterLightAverseComponent::FindPlayerCharacter() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	return Cast<ALostWithinCharacter>(UGameplayStatics::GetPlayerCharacter(World, 0));
}

UMonsterAIComponent* UMonsterLightAverseComponent::GetMonsterAI() const
{
	const AActor* Owner = GetOwner();
	return Owner ? Owner->FindComponentByClass<UMonsterAIComponent>() : nullptr;
}

bool UMonsterLightAverseComponent::IsReactingToCat() const
{
	const UMonsterAIComponent* MonsterAI = GetMonsterAI();
	return MonsterAI && (MonsterAI->bCatIsNear || MonsterAI->bIsFleeing || MonsterAI->bIsRagdolled);
}

bool UMonsterLightAverseComponent::EvaluateFlashlightExposure(const ALostWithinCharacter* PlayerChar, float& OutIntensity, FVector& OutBeamDirection)
{
	OutIntensity = 0.0f;
	OutBeamDirection = FVector::ZeroVector;

	if (!IsValid(PlayerChar) || !PlayerChar->IsFlashlightOn())
	{
		return false;
	}

	const USpotLightComponent* SpotLight = PlayerChar->FindComponentByClass<USpotLightComponent>();
	if (!SpotLight || !SpotLight->IsVisible())
	{
		return false;
	}

	const AActor* Owner = GetOwner();
	if (!IsValid(Owner))
	{
		return false;
	}

	const FVector LightPos = SpotLight->GetComponentLocation();
	const FVector LightFwd = SpotLight->GetForwardVector();
	OutBeamDirection = LightFwd;

	const float AttenuationRadius = SpotLight->AttenuationRadius;
	if (AttenuationRadius <= 0.0f)
	{
		return false;
	}

	// Sample key points on the Ghoul: chest/center and head if socket exists
	TArray<FVector, TInlineAllocator<3>> SamplePoints;
	SamplePoints.Add(Owner->GetActorLocation() + FVector(0.0f, 0.0f, DetectionTraceOffsetZ));
	if (const ACharacter* Character = Cast<ACharacter>(Owner))
	{
		if (const USkeletalMeshComponent* Mesh = Character->GetMesh())
		{
			if (Mesh->DoesSocketExist(HeadSocketOrBoneName))
			{
				SamplePoints.Add(Mesh->GetSocketLocation(HeadSocketOrBoneName));
			}
		}
	}

	float MaxCalculatedIntensity = 0.0f;
	const float InnerCone = SpotLight->InnerConeAngle;
	const float OuterCone = SpotLight->OuterConeAngle;
	const float BaseIntensity = SpotLight->Intensity;

	UWorld* World = GetWorld();

	for (const FVector& TargetPt : SamplePoints)
	{
		const FVector ToTarget = TargetPt - LightPos;
		const float Dist = ToTarget.Size();
		if (Dist > AttenuationRadius || Dist < KINDA_SMALL_NUMBER)
		{
			continue;
		}

		const FVector Dir = ToTarget / Dist;
		const float Dot = FVector::DotProduct(LightFwd, Dir);
		const float AngleDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Dot, -1.0f, 1.0f)));

		if (AngleDeg > OuterCone)
		{
			continue;
		}

		// Line of sight occlusion trace
		if (bRequireLineOfSight && World)
		{
			FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(GhoulLightTrace), false, PlayerChar);
			TraceParams.AddIgnoredActor(PlayerChar);
			FHitResult Hit;
			if (World->LineTraceSingleByChannel(Hit, LightPos, TargetPt, LightTraceChannel, TraceParams))
			{
				if (Hit.bBlockingHit && Hit.GetActor() != Owner && Hit.Distance < Dist - 30.0f)
				{
					continue;
				}
			}
		}

		// Angular falloff between inner and outer cone
		float AngularFactor = 1.0f;
		if (AngleDeg > InnerCone)
		{
			const float CosAngle = FMath::Cos(FMath::DegreesToRadians(AngleDeg));
			const float CosInner = FMath::Cos(FMath::DegreesToRadians(InnerCone));
			const float CosOuter = FMath::Cos(FMath::DegreesToRadians(OuterCone));
			AngularFactor = FMath::Clamp((CosAngle - CosOuter) / FMath::Max(CosInner - CosOuter, 0.0001f), 0.0f, 1.0f);
		}

		// Radial distance falloff
		const float DistRatio = Dist / AttenuationRadius;
		const float RadialFactor = FMath::Square(FMath::Clamp(1.0f - DistRatio, 0.0f, 1.0f));

		const float PointIntensity = BaseIntensity * AngularFactor * RadialFactor;
		if (PointIntensity > MaxCalculatedIntensity)
		{
			MaxCalculatedIntensity = PointIntensity;
		}
	}

	OutIntensity = MaxCalculatedIntensity;
	return MaxCalculatedIntensity >= LightToleranceThreshold;
}

bool UMonsterLightAverseComponent::EvaluateTorchExposure(FVector& OutTorchLocation) const
{
	const AActor* Owner = GetOwner();
	const ULostWithinLightSubsystem* Light = ULostWithinLightSubsystem::Get(this);
	if (!bTorchLightRepels || !IsValid(Owner) || !Light)
	{
		return false;
	}
	TArray<FVector, TInlineAllocator<2>> SamplePoints;
	SamplePoints.Add(Owner->GetActorLocation() + FVector(0.0f, 0.0f, DetectionTraceOffsetZ));
	if (const ACharacter* Character = Cast<ACharacter>(Owner))
	{
		if (const USkeletalMeshComponent* Mesh = Character->GetMesh(); Mesh && Mesh->DoesSocketExist(HeadSocketOrBoneName))
		{
			SamplePoints.Add(Mesh->GetSocketLocation(HeadSocketOrBoneName));
		}
	}
	const AActor* Player = FindPlayerCharacter();
	for (const FVector& Point : SamplePoints)
	{
		if (Light->IsPointTorchLit(Point, Owner, Player, &OutTorchLocation))
		{
			return true;
		}
	}
	return false;
}

bool UMonsterLightAverseComponent::WouldStepIntoTorchLight(const FVector& Target) const
{
	const AActor* Owner = GetOwner();
	const ULostWithinLightSubsystem* Light = ULostWithinLightSubsystem::Get(this);
	if (!bTorchLightRepels || TorchEdgeLookAhead <= 0.0f || !IsValid(Owner) || !Light)
	{
		return false;
	}
	const FVector Origin = Owner->GetActorLocation();
	const FVector Direction = (Target - Origin).GetSafeNormal2D();
	if (Direction.IsNearlyZero())
	{
		return false;
	}
	const FVector Ahead = Origin + Direction * TorchEdgeLookAhead + FVector(0.0f, 0.0f, DetectionTraceOffsetZ);
	return Light->IsPointTorchLit(Ahead, Owner, FindPlayerCharacter());
}

FVector UMonsterLightAverseComponent::ComputeRetreatDirection(const FVector& LightPos, const FVector& LightFwd) const
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return -LightFwd.GetSafeNormal2D();
	}

	const FVector OwnerPos = Owner->GetActorLocation();
	FVector LightToMonster2D = (OwnerPos - LightPos).GetSafeNormal2D();
	FVector BeamFwd2D = LightFwd.GetSafeNormal2D();

	if (LightToMonster2D.IsNearlyZero())
	{
		LightToMonster2D = BeamFwd2D.IsNearlyZero() ? FVector::ForwardVector : BeamFwd2D;
	}

	// Lateral vector perpendicular to beam direction
	const FVector BeamRight2D = FVector::CrossProduct(FVector::UpVector, BeamFwd2D).GetSafeNormal();
	const float LateralDot = FVector::DotProduct(LightToMonster2D, BeamRight2D);
	const FVector EscapeLateral = (LateralDot >= 0.0f ? BeamRight2D : -BeamRight2D);

	// Escape diagonally away from light origin and cone center into dark
	FVector RetreatDir = (LightToMonster2D * 0.65f + EscapeLateral * 0.75f).GetSafeNormal2D();
	if (RetreatDir.IsNearlyZero())
	{
		RetreatDir = -BeamFwd2D;
	}
	return RetreatDir;
}

void UMonsterLightAverseComponent::TriggerRepulsion(const FVector& LightOrigin, const FVector& LightForward, float Intensity)
{
	CurrentLightIntensity = Intensity;
	bIsIlluminatedByFlashlight = true;
	CurrentRetreatDirection = ComputeRetreatDirection(LightOrigin, LightForward);

	EndChase();
	StopOwnerMove();
	CurrentLightState = StartledDuration > 0.0f ? EGhoulLightState::Startled : EGhoulLightState::Retreating;
	StateElapsed = 0.0f;
	RepathTimer = 0.0f;
	bDirectMovementFallback = false;
	bIsRepelled = true;
	bIsShieldingFace = true;
	bIsRetreating = CurrentLightState == EGhoulLightState::Retreating;
	bIsStalkingInDarkness = false;

	ApplyRetreatSpeed();
	OnLightRepelled.Broadcast(Intensity);
}

void UMonsterLightAverseComponent::StopRepulsion()
{
	bChasingPlayer = false; // a cat reaction or EndPlay owns movement now
	CurrentLightState = EGhoulLightState::DarkStalking;
	bIsIlluminatedByFlashlight = false;
	bIsTorchLit = false;
	bIsHoldingAtTorchEdge = false;
	bIsRepelled = false;
	bIsShieldingFace = false;
	bIsRetreating = false;
	bIsStalkingInDarkness = true;
	CurrentLightIntensity = 0.0f;
	CurrentRetreatDirection = FVector::ZeroVector;
	StateElapsed = 0.0f;
	RestoreWalkSpeed();
}

void UMonsterLightAverseComponent::UpdateExposureAndRepulsion(float DeltaTime)
{
	if (IsReactingToCat())
	{
		if (bIsRepelled || bIsShieldingFace)
		{
			StopRepulsion();
		}
		else if (bChasingPlayer)
		{
			bChasingPlayer = false; // the cat reaction owns movement; do not stop its flee move
			RestoreWalkSpeed();
		}
		return;
	}

	ALostWithinCharacter* PlayerChar = FindPlayerCharacter();
	float Intensity = 0.0f;
	FVector BeamDirection = FVector::ZeroVector;
	const bool bBeamLit = EvaluateFlashlightExposure(PlayerChar, Intensity, BeamDirection);
	FVector TorchLocation = FVector::ZeroVector;
	const bool bTorchLitNow = !bBeamLit && EvaluateTorchExposure(TorchLocation);
	const bool bLit = bBeamLit || bTorchLitNow;

	CurrentLightIntensity = Intensity;
	bIsIlluminatedByFlashlight = bBeamLit;
	bIsTorchLit = bTorchLitNow;
	if (bLit)
	{
		bIsHoldingAtTorchEdge = false;
	}

	if (bLit && bTorchLitNow)
	{
		// Straight away from the torch until out of its light.
		CurrentRetreatDirection = (GetOwner()->GetActorLocation() - TorchLocation).GetSafeNormal2D();
		if (CurrentRetreatDirection.IsNearlyZero())
		{
			CurrentRetreatDirection = -GetOwner()->GetActorForwardVector().GetSafeNormal2D();
		}
	}
	else if (bLit)
	{
		FVector LightPos = PlayerChar ? PlayerChar->GetActorLocation() : (GetOwner()->GetActorLocation() - BeamDirection * 500.0f);
		if (PlayerChar)
		{
			if (const USpotLightComponent* Spot = PlayerChar->FindComponentByClass<USpotLightComponent>())
			{
				LightPos = Spot->GetComponentLocation();
			}
		}

		CurrentRetreatDirection = ComputeRetreatDirection(LightPos, BeamDirection);
	}

	if (bLit)
	{

		if (!bIsRepelled || CurrentLightState == EGhoulLightState::Recovering)
		{
			// Same frame the beam lands: drop the chase and (without a configured flinch) start retreating.
			EndChase();
			StopOwnerMove();
			CurrentLightState = StartledDuration > 0.0f ? EGhoulLightState::Startled : EGhoulLightState::Retreating;
			StateElapsed = 0.0f;
			RepathTimer = 0.0f;
			bDirectMovementFallback = false;
			bIsRepelled = true;
			bIsShieldingFace = true;
			bIsRetreating = CurrentLightState == EGhoulLightState::Retreating;
			bIsStalkingInDarkness = false;
			ApplyRetreatSpeed();
			OnLightRepelled.Broadcast(Intensity);
		}
		else
		{
			StateElapsed += DeltaTime;
			if (CurrentLightState == EGhoulLightState::Startled && StateElapsed >= StartledDuration)
			{
				CurrentLightState = EGhoulLightState::Retreating;
				bIsRetreating = true;
			}
		}

		if (CurrentLightState == EGhoulLightState::Retreating)
		{
			UpdateRetreatMovement(DeltaTime);
		}
	}
	else
	{
		if (bIsRepelled)
		{
			if (RecoveryDuration <= 0.0f)
			{
				// Same frame the light goes off or leaves it: stop retreating and chase again below.
				EndRepulsionNow();
			}
			else if (CurrentLightState != EGhoulLightState::Recovering)
			{
				StopOwnerMove();
				CurrentLightState = EGhoulLightState::Recovering;
				StateElapsed = 0.0f;
				bIsRetreating = false;
			}
			else
			{
				StateElapsed += DeltaTime;
				if (StateElapsed >= RecoveryDuration)
				{
					EndRepulsionNow();
				}
			}
		}
		if (!bIsRepelled)
		{
			CurrentLightState = EGhoulLightState::DarkStalking;
			bIsStalkingInDarkness = true;
			bIsShieldingFace = false;
			UpdateDarkStalking(DeltaTime, PlayerChar);
		}
	}
}

void UMonsterLightAverseComponent::UpdateRetreatMovement(float DeltaTime)
{
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!Character)
	{
		return;
	}

	RepathTimer -= DeltaTime;
	const FVector RetreatDestination = Character->GetActorLocation() + CurrentRetreatDirection * RetreatDistance;

	AAIController* AI = Cast<AAIController>(Character->GetController());
	if (AI && !bDirectMovementFallback)
	{
		if (RepathTimer <= 0.0f)
		{
			RepathTimer = 0.25f;
			EPathFollowingRequestResult::Type Result = AI->MoveToLocation(RetreatDestination, 50.0f, false, true, true, false);
			if (Result == EPathFollowingRequestResult::Failed)
			{
				bDirectMovementFallback = true;
			}
		}
	}

	if (!AI || bDirectMovementFallback)
	{
		Character->AddMovementInput(CurrentRetreatDirection, 1.0f);
		if (UCharacterMovementComponent* Move = Character->GetCharacterMovement())
		{
			Move->AddInputVector(CurrentRetreatDirection, true);
		}
	}
}

void UMonsterLightAverseComponent::UpdateDarkStalking(float DeltaTime, ALostWithinCharacter* PlayerChar)
{
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	const bool bInvestigating = UMonsterHearingComponent::IsActorInvestigating(GetOwner());
	const bool bShouldChase = bEnableAggressiveDarkStalking && !bInvestigating && Character && IsValid(PlayerChar)
		&& !PlayerChar->IsDead()
		&& FVector::Dist(Character->GetActorLocation(), PlayerChar->GetActorLocation()) <= DarkStalkDetectionRadius;

	if (!bShouldChase)
	{
		bIsHoldingAtTorchEdge = false;
		if (bChasingPlayer)
		{
			bChasingPlayer = false;
			if (!bInvestigating)
			{
				// Out of range: hand the Ghoul back to its Blueprint roam loop at its own walk speed.
				StopOwnerMove();
				RestoreWalkSpeed();
			}
		}
		return;
	}

	// While chasing, CanRunNormalAI reports false, so the roam loop cannot issue competing moves or idles and the
	// Animation Blueprint plays the fastest gait (it used to look static: idle poses while the chase slid it along).
	if (!bChasingPlayer)
	{
		bChasingPlayer = true;
		RepathTimer = 0.0f;
	}
	ApplyDarkStalkSpeed();

	if (AAIController* AI = Cast<AAIController>(Character->GetController()))
	{
		RepathTimer -= DeltaTime;
		if (RepathTimer <= 0.0f)
		{
			RepathTimer = 0.35f;
			// Keep closing in all the way, so the Ghoul's body really touches the player.
			AI->MoveToActor(PlayerChar, 5.0f, /*bStopOnOverlap=*/false);
		}
		// This tick runs before movement: if the path's next stretch enters torch light, stop before taking that step.
		const UPathFollowingComponent* PathFollowing = AI->GetPathFollowingComponent();
		const FVector NextTarget = PathFollowing && PathFollowing->GetStatus() == EPathFollowingStatus::Moving
			? PathFollowing->GetCurrentTargetLocation() : PlayerChar->GetActorLocation();
		bIsHoldingAtTorchEdge = WouldStepIntoTorchLight(NextTarget);
		if (bIsHoldingAtTorchEdge)
		{
			StopOwnerMove();
		}
	}
	else
	{
		const FVector Target = PlayerChar->GetActorLocation();
		bIsHoldingAtTorchEdge = WouldStepIntoTorchLight(Target);
		if (!bIsHoldingAtTorchEdge)
		{
			Character->AddMovementInput((Target - Character->GetActorLocation()).GetSafeNormal2D(), 1.0f);
		}
	}
}

void UMonsterLightAverseComponent::ApplyRetreatSpeed()
{
	if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		if (UMonsterHearingComponent* Hearing = Character->FindComponentByClass<UMonsterHearingComponent>(); Hearing && Hearing->IsInvestigating())
		{
			Hearing->StopInvestigating();
		}
		if (UCharacterMovementComponent* Move = Character->GetCharacterMovement())
		{
			if (!bSpeedApplied)
			{
				SavedMaxWalkSpeed = Move->MaxWalkSpeed;
				bSpeedApplied = true;
			}
			const float TargetSpeed = (RetreatGaitSpeed > 0.0f) ? RetreatGaitSpeed : (SavedMaxWalkSpeed * RetreatSpeedMultiplier);
			Move->MaxWalkSpeed = TargetSpeed;
		}
	}
}

void UMonsterLightAverseComponent::ApplyDarkStalkSpeed()
{
	if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		if (UCharacterMovementComponent* Move = Character->GetCharacterMovement())
		{
			if (!bSpeedApplied)
			{
				SavedMaxWalkSpeed = Move->MaxWalkSpeed;
				bSpeedApplied = true;
			}
			const float TargetSpeed = (DarkStalkApproachSpeed > 0.0f) ? DarkStalkApproachSpeed : (SavedMaxWalkSpeed * DarkStalkSpeedMultiplier);
			Move->MaxWalkSpeed = TargetSpeed;
		}
	}
}

void UMonsterLightAverseComponent::RestoreWalkSpeed()
{
	if (bSpeedApplied)
	{
		if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
		{
			if (UCharacterMovementComponent* Move = Character->GetCharacterMovement())
			{
				Move->MaxWalkSpeed = SavedMaxWalkSpeed;
			}
		}
		bSpeedApplied = false;
	}
}
