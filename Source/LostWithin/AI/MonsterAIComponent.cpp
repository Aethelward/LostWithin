// Copyright Epic Games, Inc. All Rights Reserved.

#include "AI/MonsterAIComponent.h"

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
	return !Component || !(Component->bCatIsNear || Component->bIsFleeing || Component->bIsRagdolled);
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

	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	UWorld* World = GetWorld();
	if (!Character || !World)
	{
		return;
	}

	const float RagdollRadius = FMath::Max(0.0f, CatRagdollRadius);
	const float FleeRadius = FMath::Max(RagdollRadius, CatFleeRadius);
	const FVector OwnerLocation = Character->GetActorLocation();
	float ClosestDistanceSquared = FMath::Square(FleeRadius);
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Cat = *It;
		if (!IsValid(Cat) || Cat == Character || Cat->IsActorBeingDestroyed())
		{
			continue;
		}
		const UProximityFloatSpinComponent* FloatSpin = Cat->FindComponentByClass<UProximityFloatSpinComponent>();
		if (!FloatSpin || !FloatSpin->IsFloatSpinActive())
		{
			continue;
		}

		const float DistanceSquared = FVector::DistSquared(OwnerLocation, Cat->GetActorLocation());
		if (DistanceSquared <= ClosestDistanceSquared)
		{
			ClosestDistanceSquared = DistanceSquared;
			ThreateningCat = Cat;
		}
	}

	if (const AActor* Cat = ThreateningCat.Get())
	{
		bCatIsNear = true;
		FleeDirection = (OwnerLocation - Cat->GetActorLocation()).GetSafeNormal2D();
		if (FleeDirection.IsNearlyZero())
		{
			FleeDirection = Character->GetActorForwardVector().GetSafeNormal2D();
			if (FleeDirection.IsNearlyZero())
			{
				FleeDirection = FVector::ForwardVector;
			}
		}
		if (ClosestDistanceSquared <= FMath::Square(RagdollRadius))
		{
			ApplyRagdoll();
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
		const EPathFollowingRequestResult::Type Result = Controller->MoveToLocation(
			Destination, 25.0f, false, true, true, false, nullptr, false);
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

bool UMonsterAIComponent::ApplyRagdoll()
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
	const FVector LaunchVelocity = FleeDirection * FMath::Max(0.0f, RagdollLaunchStrength)
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
	const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	const float Radius = Capsule->GetScaledCapsuleRadius();
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MonsterRagdollRecovery), false, Character);
	FHitResult FloorHit;
	const bool bFoundFloor = GetWorld()->LineTraceSingleByChannel(
		FloorHit, HipLocation + FVector::UpVector * HalfHeight,
		HipLocation - FVector::UpVector * (HalfHeight + 200.0f),
		Capsule->GetCollisionObjectType(), QueryParams);
	if (!bFoundFloor || !Movement->IsWalkable(FloorHit))
	{
		ScheduleRecovery(RecoveryRetryInterval);
		return;
	}
	const FVector StandLocation = FloorHit.ImpactPoint + FVector::UpVector * (HalfHeight + 2.0f);
	const bool bBlocked = GetWorld()->OverlapBlockingTestByChannel(
		StandLocation, FQuat::Identity, Capsule->GetCollisionObjectType(),
		FCollisionShape::MakeCapsule(Radius, HalfHeight), QueryParams,
		FCollisionResponseParams(Capsule->GetCollisionResponseToChannels()));
	if (bBlocked)
	{
		ScheduleRecovery(RecoveryRetryInterval);
		return;
	}

	Character->SetActorLocation(StandLocation, false, nullptr, ETeleportType::TeleportPhysics);
	RestoreCharacterSettings(Character);
	bIsRagdolled = false;
	FleeRepathTimeRemaining = 0.0f;
	// Keep the fear flag until the next tick has evaluated the cat at the new position.
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
