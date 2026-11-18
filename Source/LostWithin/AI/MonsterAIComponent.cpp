// Copyright Epic Games, Inc. All Rights Reserved.

#include "AI/MonsterAIComponent.h"

#include "AI/MonsterHearingComponent.h"
#include "AI/MonsterLightAverseComponent.h"
#include "AI/MonsterSightFreezeComponent.h"
#include "AI/MonsterVibrationComponent.h"
#include "AIController.h"
#include "Components/CapsuleComponent.h"
#include "Components/ProximityFloatSpinComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "LostWithin.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "TimerManager.h"

namespace
{
	constexpr float FleeRepathInterval = 0.25f;
	constexpr float RecoveryRetryInterval = 0.25f;
	// Search box around the landed hips for a navigable standing spot when the landing spot itself is unusable.
	const FVector RecoveryNavigationExtent(300.0f, 300.0f, 500.0f);

	double DistanceToCapsuleSurface(const UCapsuleComponent& Capsule, const FVector& Point)
	{
		float Radius = 0.0f;
		float HalfHeight = 0.0f;
		Capsule.GetScaledCapsuleSize(Radius, HalfHeight);
		const FVector Center = Capsule.GetComponentLocation();
		const FVector AxisOffset = Capsule.GetUpVector() * FMath::Max(0.0f, HalfHeight - Radius);
		const FVector ClosestAxisPoint = FMath::ClosestPointOnSegment(Point, Center - AxisOffset, Center + AxisOffset);
		return FMath::Max(0.0, FVector::Dist(Point, ClosestAxisPoint) - static_cast<double>(Radius));
	}
}

UMonsterAIComponent::UMonsterAIComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UMonsterAIComponent::RegisterComponentTickFunctions(bool bRegister)
{
	Super::RegisterComponentTickFunctions(bRegister);
	if (AActor* Owner = GetOwner())
	{
		if (bRegister)
		{
			// Standard components depend on the actor. Reverse that dependency first
			// so the Blueprint sees this frame's fear state without a tick cycle.
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

bool UMonsterAIComponent::CanRunNormalAI(AActor* Actor)
{
	if (!IsValid(Actor))
	{
		return false;
	}

	const UMonsterAIComponent* Component = Actor->FindComponentByClass<UMonsterAIComponent>();
	if (Component && (Component->bCatIsNear || Component->bIsFleeing || Component->bIsRagdolled))
	{
		return false;
	}
	// A monster walking to a heard noise owns its movement until it has searched there.
	if (UMonsterHearingComponent::IsActorInvestigating(Actor))
	{
		return false;
	}
	// A blind monster tracking floor tremors or air turbulence owns its movement until it has searched there.
	if (UMonsterVibrationComponent::IsActorInvestigating(Actor))
	{
		return false;
	}
	// A weeping doll observed by the player is frozen in place and cannot run normal AI movement.
	if (UMonsterSightFreezeComponent::IsActorSightFrozen(Actor))
	{
		return false;
	}
	// A light-averse monster repelled by flashlight retreats and shields face until back in shadows.
	return !UMonsterLightAverseComponent::IsActorLightRepelled(Actor);
}

AActor* UMonsterAIComponent::GetThreateningCat() const
{
	return ThreateningCat.Get();
}

bool UMonsterAIComponent::FindReachableRoamLocation(UObject* WorldContextObject, const FVector& Origin,
	FVector& RandomLocation, float Radius, ANavigationData* NavData, TSubclassOf<UNavigationQueryFilter> FilterClass)
{
	RandomLocation = Origin;
	if (!IsValid(WorldContextObject) || !FMath::IsFinite(Radius) || Radius <= 0.0f)
	{
		return false;
	}
	if (!NavData)
	{
		const APawn* Pawn = Cast<APawn>(WorldContextObject);
		UNavigationSystemV1* Navigation = UNavigationSystemV1::GetCurrent(WorldContextObject);
		if (Pawn && Navigation)
		{
			NavData = Navigation->GetNavDataForProps(Pawn->GetNavAgentPropertiesRef(), Origin, INVALID_NAVEXTENT);
			if (!NavData)
			{
				return false;
			}
		}
	}
	const bool bFound = UNavigationSystemV1::K2_GetRandomReachablePointInRadius(
		WorldContextObject, Origin, RandomLocation, Radius, NavData, FilterClass);
	if (!bFound)
	{
		RandomLocation = Origin;
	}
	return bFound;
}

void UMonsterAIComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (bIsRagdolled)
	{
		return;
	}

	const TWeakObjectPtr<AActor> PreviousCat = ThreateningCat;
	UpdateCatProximity();
	if (bIsRagdolled)
	{
		// UpdateCatProximity can enter ragdoll. Do not restart navigation this frame.
		return;
	}
	if (PreviousCat != ThreateningCat)
	{
		FleeRepathTimeRemaining = 0.0f;
	}
	UpdatePlayerProximity();
	UpdateFleeMovement(DeltaTime);
}

void UMonsterAIComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RagdollRecoveryTimerHandle);
	}
	EndPanic(Cast<ACharacter>(GetOwner()));
	if (AActor* Owner = GetOwner())
	{
		Owner->RemoveTickPrerequisiteComponent(this);
	}
	Super::EndPlay(EndPlayReason);
}

void UMonsterAIComponent::UpdateCatProximity()
{
	bCatIsNear = false;
	ThreateningCat.Reset();
	FleeDirection = FVector::ZeroVector;

	ACharacter* Character = Cast<ACharacter>(GetOwner());
	const UCapsuleComponent* Capsule = Character ? Character->GetCapsuleComponent() : nullptr;
	UWorld* World = GetWorld();
	if (!Character || !Capsule || !World)
	{
		return;
	}

	const float RagdollRadius = FMath::Max(0.0f, CatRagdollRadius);
	const double FleeRadiusSquared = FMath::Square(static_cast<double>(FMath::Max(0.0f, CatFleeRadius)));
	const FVector OwnerLocation = Character->GetActorLocation();
	double ClosestFleeDistanceSquared = TNumericLimits<double>::Max();
	double ClosestThrowDistance = TNumericLimits<double>::Max();
	AActor* FleeCat = nullptr;
	AActor* ThrowCat = nullptr;
	UProximityFloatSpinComponent* RepellingCat = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Cat = *It;
		if (!IsValid(Cat) || Cat == Character || Cat->IsActorBeingDestroyed())
		{
			continue;
		}
		UProximityFloatSpinComponent* FloatSpin = Cat->FindComponentByClass<UProximityFloatSpinComponent>();
		if (!FloatSpin || !FloatSpin->IsFloatSpinThreatening())
		{
			continue;
		}

		const FVector CatLocation = Cat->GetActorLocation();
		const double CenterDistanceSquared = FVector::DistSquared(OwnerLocation, CatLocation);
		const bool bInsideSanctuary = FloatSpin->bRepelMonstersInSanctuary
			&& CenterDistanceSquared <= FMath::Square(static_cast<double>(FloatSpin->ChaosSanctuaryRadius));
		const bool bInsideRepelRadius = bInsideSanctuary
			&& CenterDistanceSquared <= FMath::Square(static_cast<double>(FloatSpin->SanctuaryRepelRadius));
		const double SurfaceDistance = DistanceToCapsuleSurface(*Capsule, CatLocation);
		if ((SurfaceDistance <= RagdollRadius || bInsideRepelRadius) && SurfaceDistance <= ClosestThrowDistance)
		{
			ClosestThrowDistance = SurfaceDistance;
			ThrowCat = Cat;
			RepellingCat = bInsideRepelRadius ? FloatSpin : nullptr;
		}
		if ((CenterDistanceSquared <= FleeRadiusSquared || bInsideSanctuary) && CenterDistanceSquared <= ClosestFleeDistanceSquared)
		{
			ClosestFleeDistanceSquared = CenterDistanceSquared;
			FleeCat = Cat;
		}
	}

	// A hovering throw threat takes priority even if another cat is closer by center.
	ThreateningCat = ThrowCat ? ThrowCat : FleeCat;
	if (const AActor* Cat = ThreateningCat.Get())
	{
		bCatIsNear = true;
		if (!bIsFleeing)
		{
			// Release movement and animation overrides before panic or ragdoll caches the character settings.
			if (UMonsterHearingComponent* Hearing = Character->FindComponentByClass<UMonsterHearingComponent>(); Hearing && Hearing->IsInvestigating())
			{
				Hearing->StopInvestigating();
			}
			if (UMonsterVibrationComponent* Vibration = Character->FindComponentByClass<UMonsterVibrationComponent>(); Vibration && Vibration->IsInvestigating())
			{
				Vibration->StopInvestigating();
			}
			if (UMonsterLightAverseComponent* LightAverse = Character->FindComponentByClass<UMonsterLightAverseComponent>())
			{
				LightAverse->StopRepulsion();
			}
			if (UMonsterSightFreezeComponent* SightFreeze = Character->FindComponentByClass<UMonsterSightFreezeComponent>();
				SightFreeze && SightFreeze->bCatReactionHasPriority && SightFreeze->IsFrozen())
			{
				SightFreeze->SetFrozen(false);
			}
		}
		FleeDirection = (OwnerLocation - Cat->GetActorLocation()).GetSafeNormal2D();
		if (FleeDirection.IsNearlyZero())
		{
			FleeDirection = Character->GetActorForwardVector().GetSafeNormal2D();
			if (FleeDirection.IsNearlyZero())
			{
				FleeDirection = FVector::ForwardVector;
			}
		}
		if (ThrowCat)
		{
			const float LaunchStrength = RepellingCat && ClosestThrowDistance > RagdollRadius
				? RepellingCat->SanctuaryRepelForce : RagdollLaunchStrength;
			if (ApplyRagdoll(LaunchStrength) && IsValid(RepellingCat))
			{
				RepellingCat->OnCatRepelledMonster.Broadcast(Character);
			}
		}
	}
}

void UMonsterAIComponent::UpdateFleeMovement(float DeltaTime)
{
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	AAIController* Controller = Character ? Cast<AAIController>(Character->GetController()) : nullptr;
	if (!bCatIsNear)
	{
		if (bIsFleeing && Controller)
		{
			Controller->StopMovement();
		}
		EndPanic(Character);
		bIsFleeing = false;
		bUseDirectFleeMovement = false;
		FleeRepathTimeRemaining = 0.0f;
		bWarnedAboutFleeSetup = false;
		return;
	}

	if (!bIsFleeing && Controller)
	{
		Controller->StopMovement();
	}
	bIsFleeing = true;
	BeginPanic(Character);
	if (!Controller)
	{
		if (!bWarnedAboutFleeSetup)
		{
			UE_LOG(LogLostWithin, Warning, TEXT("%s needs an AIController to flee from the active cat."), *GetNameSafe(GetOwner()));
			bWarnedAboutFleeSetup = true;
		}
		return;
	}

	FleeRepathTimeRemaining -= DeltaTime;
	if (FleeRepathTimeRemaining <= 0.0f)
	{
		FleeRepathTimeRemaining = FleeRepathInterval;
		const FVector Destination = Character->GetActorLocation() + FleeDirection * FMath::Max(1.0f, FleeDistance);
		// UE5.6 can project a missing-navigation goal to the origin and report AlreadyAtGoal.
		const EPathFollowingRequestResult::Type Result = UNavigationSystemV1::GetCurrent(GetWorld())
			? Controller->MoveToLocation(Destination, 25.0f, false, true, true, false, nullptr, false)
			: EPathFollowingRequestResult::Failed;
		bUseDirectFleeMovement = Result == EPathFollowingRequestResult::Failed;
		if (bUseDirectFleeMovement)
		{
			Controller->StopMovement();
			if (!bWarnedAboutFleeSetup)
			{
				UE_LOG(LogLostWithin, Warning, TEXT("%s has no escape navigation path; using collision-aware movement away from the cat."), *GetNameSafe(Character));
				bWarnedAboutFleeSetup = true;
			}
		}
	}
	if (bUseDirectFleeMovement)
	{
		Character->AddMovementInput(FleeDirection);
	}
}

bool UMonsterAIComponent::IsPlayerInSight()
{
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	const APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!Character || !PlayerPawn)
	{
		return false;
	}
	const FVector ToPlayer = PlayerPawn->GetActorLocation() - Character->GetActorLocation();
	if (ToPlayer.SizeSquared() > FMath::Square(FMath::Max(0.0f, PlayerDetectionRadius)))
	{
		return false;
	}
	const float Dot = FMath::Clamp(static_cast<float>(FVector::DotProduct(Character->GetActorForwardVector(), ToPlayer.GetSafeNormal())), -1.0f, 1.0f);
	return FMath::RadiansToDegrees(FMath::Acos(Dot)) <= FMath::Max(0.0f, PeripheralVisionAngle);
}

void UMonsterAIComponent::UpdatePlayerProximity()
{
	bPlayerIsNear = IsPlayerInSight();
}

bool UMonsterAIComponent::ApplyRagdoll(float LaunchStrength)
{
	if (bIsRagdolled)
	{
		return false;
	}
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	USkeletalMeshComponent* Mesh = Character ? Character->GetMesh() : nullptr;
	UCapsuleComponent* Capsule = Character ? Character->GetCapsuleComponent() : nullptr;
	UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	UPhysicsAsset* PhysicsAsset = Mesh ? Mesh->GetPhysicsAsset() : nullptr;
	if (!Mesh || !Capsule || !Movement || !PhysicsAsset || PhysicsAsset->SkeletalBodySetups.IsEmpty())
	{
		if (!bWarnedAboutPhysicsSetup)
		{
			UE_LOG(LogLostWithin, Warning, TEXT("%s cannot ragdoll: a character mesh and valid physics asset are required."), *GetNameSafe(GetOwner()));
			bWarnedAboutPhysicsSetup = true;
		}
		return false;
	}

	ActiveRagdollBone = RagdollRecoveryBone;
	if (PhysicsAsset->FindBodyIndex(ActiveRagdollBone) == INDEX_NONE)
	{
		const USkeletalBodySetup* FirstBody = PhysicsAsset->SkeletalBodySetups[0];
		if (!FirstBody)
		{
			UE_LOG(LogLostWithin, Warning, TEXT("%s has an invalid first ragdoll body."), *GetNameSafe(Character));
			return false;
		}
		ActiveRagdollBone = FirstBody->BoneName;
	}

	// Panic speed and animation rate belong to running; the ragdoll and its recovery use normal settings.
	EndPanic(Character);
	ThrowOriginLocation = Character->GetActorLocation();
	RecoveryRetryElapsed = 0.0f;
	CachedMeshRelativeTransform = Mesh->GetRelativeTransform();
	CachedMeshAttachParent = Mesh->GetAttachParent();
	CachedMeshAttachSocket = Mesh->GetAttachSocketName();
	CachedMeshCollisionProfile = Mesh->GetCollisionProfileName();
	CachedMeshCollisionResponses = Mesh->GetCollisionResponseToChannels();
	CachedMeshObjectType = Mesh->GetCollisionObjectType();
	CachedMeshCollisionEnabled = Mesh->GetCollisionEnabled();
	bCachedMeshGravityEnabled = Mesh->IsGravityEnabled();
	CachedCapsuleCollisionEnabled = Capsule->GetCollisionEnabled();
	CachedMovementMode = Movement->MovementMode;
	CachedCustomMovementMode = Movement->CustomMovementMode;

	if (AAIController* Controller = Cast<AAIController>(Character->GetController()))
	{
		Controller->StopMovement();
	}
	Movement->StopMovementImmediately();
	Movement->DisableMovement();
	Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
	Mesh->SetCollisionProfileName(TEXT("Ragdoll"));
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Mesh->SetEnableGravity(true);
	Mesh->SetSimulatePhysics(true);
	Mesh->SetAllBodiesSimulatePhysics(true);
	Mesh->SetPhysicsBlendWeight(1.0f);
	if (!Mesh->IsSimulatingPhysics(ActiveRagdollBone))
	{
		RestoreCharacterSettings(Character);
		if (!bWarnedAboutPhysicsSetup)
		{
			UE_LOG(LogLostWithin, Warning, TEXT("%s could not simulate its ragdoll body %s."), *GetNameSafe(Character), *ActiveRagdollBone.ToString());
			bWarnedAboutPhysicsSetup = true;
		}
		return false;
	}

	bIsRagdolled = true;
	bIsFleeing = false;
	bPlayerIsNear = false;
	bUseDirectFleeMovement = false;
	Mesh->WakeAllRigidBodies();
	const FVector LaunchVelocity = FleeDirection * FMath::Max(0.0f, LaunchStrength)
		+ FVector::UpVector * FMath::Max(0.0f, RagdollUpwardStrength);
	Mesh->AddImpulseToAllBodiesBelow(LaunchVelocity, ActiveRagdollBone, true, true);
	ScheduleRecovery(FMath::Max(0.1f, RagdollDuration));
	return true;
}

void UMonsterAIComponent::ScheduleRecovery(float Delay)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(RagdollRecoveryTimerHandle, this, &UMonsterAIComponent::RecoverFromRagdoll, Delay, false);
	}
}

void UMonsterAIComponent::RecoverFromRagdoll()
{
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!bIsRagdolled || !IsValid(Character) || !GetWorld())
	{
		return;
	}
	USkeletalMeshComponent* Mesh = Character->GetMesh();
	UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
	UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	if (!Mesh || !Capsule || !Movement)
	{
		UE_LOG(LogLostWithin, Warning, TEXT("%s lost its mesh or movement components during ragdoll recovery."), *GetNameSafe(Character));
		bIsRagdolled = false;
		return;
	}

	const FVector HipLocation = Mesh->GetSocketLocation(ActiveRagdollBone);
	const bool bHipUsable = !HipLocation.ContainsNaN();
	const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MonsterRagdollRecovery), false, Character);
	FHitResult FloorHit;
	const bool bFoundFloor = bHipUsable && GetWorld()->LineTraceSingleByChannel(
		FloorHit, HipLocation + FVector::UpVector * HalfHeight,
		HipLocation - FVector::UpVector * (HalfHeight + 200.0f),
		Capsule->GetCollisionObjectType(), QueryParams);
	if (bFoundFloor && Movement->IsWalkable(FloorHit) && TryStandAt(Character, FloorHit.ImpactPoint))
	{
		return;
	}
	if (RecoveryRetryElapsed < FMath::Max(0.0f, RecoveryFallbackDelay))
	{
		RecoveryRetryElapsed += RecoveryRetryInterval;
		ScheduleRecovery(RecoveryRetryInterval);
		return;
	}

	// The monster must never stay limp: stand on navigation near the body, else where the throw started.
	if (UNavigationSystemV1* Navigation = UNavigationSystemV1::GetCurrent(GetWorld()); Navigation && bHipUsable)
	{
		const ANavigationData* NavData = Navigation->GetNavDataForProps(Character->GetNavAgentPropertiesRef(), HipLocation, INVALID_NAVEXTENT);
		FNavLocation Projected;
		if (NavData && Navigation->ProjectPointToNavigation(HipLocation, Projected, RecoveryNavigationExtent, NavData)
			&& TryStandAt(Character, Projected.Location))
		{
			UE_LOG(LogLostWithin, Display, TEXT("%s could not stand where its ragdoll landed; it stood up on nearby navigation."), *GetNameSafe(Character));
			return;
		}
	}
	UE_LOG(LogLostWithin, Display, TEXT("%s could not stand near its ragdoll; it returned to where the throw started."), *GetNameSafe(Character));
	FinishRecovery(Character, ThrowOriginLocation);
}

bool UMonsterAIComponent::TryStandAt(ACharacter* Character, const FVector& FloorPoint)
{
	const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
	const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	const float Radius = Capsule->GetScaledCapsuleRadius();
	const FVector StandLocation = FloorPoint + FVector::UpVector * (HalfHeight + 2.0f);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MonsterRagdollRecovery), false, Character);
	const bool bBlocked = GetWorld()->OverlapBlockingTestByChannel(
		StandLocation, FQuat::Identity, Capsule->GetCollisionObjectType(),
		FCollisionShape::MakeCapsule(Radius, HalfHeight), QueryParams,
		FCollisionResponseParams(Capsule->GetCollisionResponseToChannels()));
	if (bBlocked)
	{
		return false;
	}
	FinishRecovery(Character, StandLocation);
	return true;
}

void UMonsterAIComponent::FinishRecovery(ACharacter* Character, const FVector& StandLocation)
{
	Character->SetActorLocation(StandLocation, false, nullptr, ETeleportType::TeleportPhysics);
	RestoreCharacterSettings(Character);
	bIsRagdolled = false;
	RecoveryRetryElapsed = 0.0f;
	FleeRepathTimeRemaining = 0.0f;
	// Keep the fear flag until the next tick has evaluated the cat at the new position.
}

void UMonsterAIComponent::BeginPanic(ACharacter* Character)
{
	UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	USkeletalMeshComponent* Mesh = Character ? Character->GetMesh() : nullptr;
	if (bPanicApplied || !Movement || !Mesh)
	{
		return;
	}
	const float Multiplier = FMath::Clamp(PanicSpeedMultiplier, 1.0f, 4.0f);
	CachedPanicMaxWalkSpeed = Movement->MaxWalkSpeed;
	CachedPanicAnimRateScale = Mesh->GlobalAnimRateScale;
	// The speed and the animation rate scale together, so the panic gait's feet stay matched to the ground.
	Movement->MaxWalkSpeed = (PanicGaitSpeed > 0.0f ? PanicGaitSpeed : CachedPanicMaxWalkSpeed) * Multiplier;
	Mesh->GlobalAnimRateScale = CachedPanicAnimRateScale * Multiplier;
	bPanicApplied = true;
}

void UMonsterAIComponent::EndPanic(ACharacter* Character)
{
	if (!bPanicApplied)
	{
		return;
	}
	bPanicApplied = false;
	if (UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr)
	{
		Movement->MaxWalkSpeed = CachedPanicMaxWalkSpeed;
	}
	if (USkeletalMeshComponent* Mesh = Character ? Character->GetMesh() : nullptr)
	{
		Mesh->GlobalAnimRateScale = CachedPanicAnimRateScale;
	}
}

void UMonsterAIComponent::RestoreCharacterSettings(ACharacter* Character)
{
	USkeletalMeshComponent* Mesh = Character->GetMesh();
	Mesh->SetAllPhysicsLinearVelocity(FVector::ZeroVector);
	Mesh->SetAllPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
	Mesh->SetAllBodiesSimulatePhysics(false);
	Mesh->SetSimulatePhysics(false);
	Mesh->SetPhysicsBlendWeight(0.0f);
	Mesh->SetEnableGravity(bCachedMeshGravityEnabled);
	Mesh->SetCollisionProfileName(CachedMeshCollisionProfile);
	Mesh->SetCollisionObjectType(CachedMeshObjectType);
	Mesh->SetCollisionResponseToChannels(CachedMeshCollisionResponses);
	Mesh->SetCollisionEnabled(CachedMeshCollisionEnabled);
	USceneComponent* Parent = CachedMeshAttachParent.IsValid() ? CachedMeshAttachParent.Get() : Character->GetCapsuleComponent();
	Mesh->AttachToComponent(Parent, FAttachmentTransformRules::KeepWorldTransform, CachedMeshAttachSocket);
	Mesh->SetRelativeTransform(CachedMeshRelativeTransform);
	Character->GetCapsuleComponent()->SetCollisionEnabled(CachedCapsuleCollisionEnabled);
	Character->GetCharacterMovement()->SetMovementMode(static_cast<EMovementMode>(CachedMovementMode), CachedCustomMovementMode);
}
