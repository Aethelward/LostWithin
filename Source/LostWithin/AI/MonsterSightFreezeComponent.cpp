// Copyright Epic Games, Inc. All Rights Reserved.

#include "AI/MonsterSightFreezeComponent.h"

#include "AI/MonsterAIComponent.h"
#include "AI/MonsterHearingComponent.h"
#include "AIController.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SpotLightComponent.h"
#include "Gameplay/LostWithinLightSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "LostWithinCharacter.h"

UMonsterSightFreezeComponent::UMonsterSightFreezeComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

void UMonsterSightFreezeComponent::RegisterComponentTickFunctions(bool bRegister)
{
	Super::RegisterComponentTickFunctions(bRegister);

	if (AActor* Owner = GetOwner())
	{
		if (bRegister)
		{
			RemoveTickPrerequisiteActor(Owner);
			Owner->AddTickPrerequisiteComponent(this);

			if (ACharacter* Character = Cast<ACharacter>(Owner))
			{
				if (UCharacterMovementComponent* MoveComp = Character->GetCharacterMovement())
				{
					MoveComp->AddTickPrerequisiteComponent(this);
				}
			}
		}
		else
		{
			Owner->RemoveTickPrerequisiteComponent(this);

			if (ACharacter* Character = Cast<ACharacter>(Owner))
			{
				if (UCharacterMovementComponent* MoveComp = Character->GetCharacterMovement())
				{
					MoveComp->RemoveTickPrerequisiteComponent(this);
				}
			}
		}
	}
}

void UMonsterSightFreezeComponent::BeginPlay()
{
	Super::BeginPlay();

	if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		if (UCharacterMovementComponent* MoveComp = Character->GetCharacterMovement())
		{
			CachedMaxWalkSpeed = MoveComp->MaxWalkSpeed;
			bSavedInitialSpeed = true;
		}

		// Ensure controller exists for AI stalking
		if (!Character->GetController() && Character->GetWorld())
		{
			Character->SpawnDefaultController();
		}
	}
}

void UMonsterSightFreezeComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bIsFrozen)
	{
		ApplyUnfrozenState();
	}

	Super::EndPlay(EndPlayReason);
}

void UMonsterSightFreezeComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	UpdateSightFreeze(DeltaTime);

	if (bIsFrozen)
	{
		// Enforce zero movement while frozen
		if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
		{
			if (UCharacterMovementComponent* MoveComp = Character->GetCharacterMovement())
			{
				MoveComp->StopMovementImmediately();
				if (MoveComp->MovementMode != MOVE_None)
				{
					MoveComp->DisableMovement();
				}
			}
		}
	}
	else if (bAutoStalkWhenUnobserved)
	{
		UpdateStalkingMovement(DeltaTime);
	}
}

bool UMonsterSightFreezeComponent::IsActorSightFrozen(const AActor* Actor)
{
	if (!IsValid(Actor))
	{
		return false;
	}

	const UMonsterSightFreezeComponent* FreezeComp = Actor->FindComponentByClass<UMonsterSightFreezeComponent>();
	return FreezeComp ? FreezeComp->IsFrozen() : false;
}

bool UMonsterSightFreezeComponent::CanActorMove(const AActor* Actor)
{
	if (!IsValid(Actor))
	{
		return false;
	}

	const UMonsterSightFreezeComponent* FreezeComp = Actor->FindComponentByClass<UMonsterSightFreezeComponent>();
	if (FreezeComp && FreezeComp->IsFrozen())
	{
		return false;
	}

	return true;
}

ALostWithinCharacter* UMonsterSightFreezeComponent::FindPlayerCharacter() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	return Cast<ALostWithinCharacter>(UGameplayStatics::GetPlayerCharacter(World, 0));
}

APlayerController* UMonsterSightFreezeComponent::FindPlayerController() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	return UGameplayStatics::GetPlayerController(World, 0);
}

UMonsterAIComponent* UMonsterSightFreezeComponent::GetMonsterAI() const
{
	const AActor* Owner = GetOwner();
	return Owner ? Owner->FindComponentByClass<UMonsterAIComponent>() : nullptr;
}

bool UMonsterSightFreezeComponent::IsReactingToCat() const
{
	if (!bCatReactionHasPriority)
	{
		return false;
	}

	const UMonsterAIComponent* MonsterAI = GetMonsterAI();
	return MonsterAI && (MonsterAI->bCatIsNear || MonsterAI->bIsFleeing || MonsterAI->bIsRagdolled);
}

bool UMonsterSightFreezeComponent::IsPlayerInMenuOrPadlock(const AActor* PlayerActor) const
{
	if (!IsValid(PlayerActor))
	{
		return false;
	}

	const ALostWithinCharacter* LostWithinChar = Cast<ALostWithinCharacter>(PlayerActor);
	if (LostWithinChar)
	{
		if (LostWithinChar->IsDead())
		{
			return true;
		}

		// Secondary camera target has control (ScaryDoor skull padlock dial view)
		if (LostWithinChar->IsGameplayInputBlocked())
		{
			return true;
		}

		// Paper note is being inspected full-screen
		if (LostWithinChar->IsNoteOpen())
		{
			return true;
		}

		// Inventory UI is currently open
		if (LostWithinChar->IsInventoryOpen())
		{
			return true;
		}
	}

	// Any UI state where the mouse cursor is visible
	const APlayerController* PC = Cast<APlayerController>(PlayerActor->GetInstigatorController());
	if (!PC && LostWithinChar)
	{
		PC = Cast<APlayerController>(LostWithinChar->GetController());
	}
	if (PC && PC->bShowMouseCursor)
	{
		return true;
	}

	return false;
}

bool UMonsterSightFreezeComponent::IsPointInPlayerFrustum(const FVector& TargetPoint, const FVector& CameraLoc, const FVector& CameraFwd, float HalfFOVDegrees) const
{
	const FVector ToTarget = TargetPoint - CameraLoc;
	const float Dist = ToTarget.Size();
	if (Dist < KINDA_SMALL_NUMBER)
	{
		return true;
	}

	const FVector Dir = ToTarget / Dist;
	const float Dot = FVector::DotProduct(CameraFwd, Dir);
	const float MinDot = FMath::Cos(FMath::DegreesToRadians(FMath::Clamp(HalfFOVDegrees, 1.0f, 89.9f)));

	return Dot >= MinDot;
}

bool UMonsterSightFreezeComponent::HasLineOfSightToPoint(const FVector& FromLocation, const FVector& TargetPoint, const AActor* IgnoredActor1, const AActor* IgnoredActor2) const
{
	if (!bRequireLineOfSight)
	{
		return true;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return true;
	}

	const FVector ToTarget = TargetPoint - FromLocation;
	const float TotalDist = ToTarget.Size();
	if (TotalDist <= 1.0f)
	{
		return true;
	}

	FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(MonsterSightFreezeTrace), false, IgnoredActor1);
	if (IgnoredActor1)
	{
		TraceParams.AddIgnoredActor(IgnoredActor1);
	}
	if (IgnoredActor2)
	{
		TraceParams.AddIgnoredActor(IgnoredActor2);
	}

	FHitResult Hit;
	if (World->LineTraceSingleByChannel(Hit, FromLocation, TargetPoint, SightTraceChannel, TraceParams))
	{
		if (Hit.bBlockingHit && Hit.GetActor() != GetOwner() && Hit.Distance < TotalDist - 30.0f)
		{
			return false;
		}
	}

	return true;
}

bool UMonsterSightFreezeComponent::IsPointIlluminatedByFlashlight(const ALostWithinCharacter* PlayerChar, const FVector& TargetPoint) const
{
	if (!IsValid(PlayerChar) || !PlayerChar->IsFlashlightOn())
	{
		return false;
	}

	const USpotLightComponent* Spot = PlayerChar->GetFlashlightSpotLight();
	if (!Spot || !Spot->IsVisible())
	{
		return false;
	}

	const FVector LightPos = Spot->GetComponentLocation();
	const FVector LightFwd = Spot->GetForwardVector();
	const FVector ToTarget = TargetPoint - LightPos;
	const float Dist = ToTarget.Size();

	const float AttenuationRadius = Spot->AttenuationRadius;
	if (Dist > AttenuationRadius || Dist < KINDA_SMALL_NUMBER)
	{
		return false;
	}

	const FVector Dir = ToTarget / Dist;
	const float Dot = FVector::DotProduct(LightFwd, Dir);
	const float AngleDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Dot, -1.0f, 1.0f)));

	if (AngleDeg > Spot->OuterConeAngle)
	{
		return false;
	}

	// Line of sight from flashlight to target
	if (bRequireLineOfSight)
	{
		return HasLineOfSightToPoint(LightPos, TargetPoint, PlayerChar, GetOwner());
	}

	return true;
}

bool UMonsterSightFreezeComponent::EvaluatePlayerObservation(const APlayerController* PC, bool& OutObserved, bool& OutIlluminated)
{
	OutObserved = false;
	OutIlluminated = false;

	if (!IsValid(PC))
	{
		return false;
	}

	const APawn* PlayerPawn = PC->GetPawn();
	const ALostWithinCharacter* LostWithinChar = Cast<ALostWithinCharacter>(PlayerPawn);

	// Check if player is distracted in menus, note inspection, or padlock dial view
	bIsPlayerDistracted = IsPlayerInMenuOrPadlock(PlayerPawn);
	if (bIgnoreSightWhenInMenus && bIsPlayerDistracted)
	{
		return false;
	}

	const AActor* Owner = GetOwner();
	if (!IsValid(Owner))
	{
		return false;
	}

	// Retrieve camera view
	FVector CamLoc = FVector::ZeroVector;
	FRotator CamRot = FRotator::ZeroRotator;
	float CamFOV = 90.0f;

	if (const APlayerCameraManager* CamMgr = PC->PlayerCameraManager)
	{
		CamLoc = CamMgr->GetCameraLocation();
		CamRot = CamMgr->GetCameraRotation();
		CamFOV = CamMgr->GetFOVAngle();
	}
	else
	{
		PC->GetPlayerViewPoint(CamLoc, CamRot);
	}

	const FVector CamFwd = CamRot.Vector();
	const float EffectiveHalfFOV = (bUsePlayerCameraFOV && CamFOV > 1.0f)
		? (CamFOV * 0.5f + FOVMarginDegrees)
		: FrustumHalfAngle;

	// Build sample points on the Doll (center, head, base)
	TArray<FVector, TInlineAllocator<4>> SamplePoints;

	const FVector OwnerLoc = Owner->GetActorLocation();
	SamplePoints.Add(OwnerLoc + FVector(0.0f, 0.0f, DetectionTraceOffsetZ));

	float HalfHeight = 88.0f;
	if (const ACharacter* Char = Cast<ACharacter>(Owner))
	{
		if (const UCapsuleComponent* Capsule = Char->GetCapsuleComponent())
		{
			HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
		}

		if (const USkeletalMeshComponent* Mesh = Char->GetMesh())
		{
			if (Mesh->DoesSocketExist(HeadSocketOrBoneName))
			{
				SamplePoints.Add(Mesh->GetSocketLocation(HeadSocketOrBoneName));
			}
			else
			{
				SamplePoints.Add(OwnerLoc + FVector(0.0f, 0.0f, HalfHeight * 0.7f));
			}
		}
		else
		{
			SamplePoints.Add(OwnerLoc + FVector(0.0f, 0.0f, HalfHeight * 0.7f));
		}

		SamplePoints.Add(OwnerLoc - FVector(0.0f, 0.0f, HalfHeight * 0.6f));
	}
	else
	{
		SamplePoints.Add(OwnerLoc + FVector(0.0f, 0.0f, 50.0f));
	}

	bool bAnyInFrustumAndLOS = false;
	bool bAnyIlluminated = false;
	const ULostWithinLightSubsystem* LightSubsystem = ULostWithinLightSubsystem::Get(this);

	for (const FVector& Pt : SamplePoints)
	{
		const float Dist = FVector::Dist(CamLoc, Pt);
		if (Dist > MaxSightDistance)
		{
			continue;
		}

		// Check camera frustum
		if (!IsPointInPlayerFrustum(Pt, CamLoc, CamFwd, EffectiveHalfFOV))
		{
			continue;
		}

		// Check line of sight
		if (!HasLineOfSightToPoint(CamLoc, Pt, PlayerPawn, Owner))
		{
			continue;
		}

		// Point has clear line of sight in frustum
		const bool bPtIlluminated = (LostWithinChar && IsPointIlluminatedByFlashlight(LostWithinChar, Pt))
			|| (bTorchLightCounts && LightSubsystem && LightSubsystem->IsPointTorchLit(Pt, Owner, PlayerPawn));
		if (bPtIlluminated)
		{
			bAnyIlluminated = true;
		}

		// Determine if visibility condition is met based on distance & illumination
		bool bVisibleAtDistance = false;
		if (bFreezeOnlyWhenLitAndWatched || bRequireFlashlightAlways)
		{
			bVisibleAtDistance = bPtIlluminated && (Dist <= IlluminatedMaxDistance);
		}
		else if (bRequireIlluminationAtDistance)
		{
			if (bPtIlluminated)
			{
				bVisibleAtDistance = (Dist <= IlluminatedMaxDistance);
			}
			else
			{
				bVisibleAtDistance = (Dist <= UnilluminatedMaxDistance);
			}
		}
		else
		{
			bVisibleAtDistance = (Dist <= MaxSightDistance);
		}

		if (bVisibleAtDistance)
		{
			bAnyInFrustumAndLOS = true;
		}
	}

	OutObserved = bAnyInFrustumAndLOS;
	OutIlluminated = bAnyIlluminated;

	return OutObserved;
}

void UMonsterSightFreezeComponent::UpdateSightFreeze(float DeltaTime)
{
	// Float-spinning cat reactions take priority over sight freezing
	if (IsReactingToCat())
	{
		if (bIsFrozen)
		{
			SetFrozen(false);
		}
		bIsObserved = false;
		bIsIlluminated = false;
		return;
	}

	const APlayerController* PC = FindPlayerController();
	bool bObserved = false;
	bool bIlluminated = false;

	if (PC)
	{
		EvaluatePlayerObservation(PC, bObserved, bIlluminated);
	}

	if (bIsObserved != bObserved)
	{
		bIsObserved = bObserved;
		OnObservedChanged.Broadcast(bIsObserved);
	}
	bIsIlluminated = bIlluminated;

	const bool bShouldFreeze = bIsObserved;
	if (bShouldFreeze != bIsFrozen)
	{
		SetFrozen(bShouldFreeze);
	}
}

void UMonsterSightFreezeComponent::SetFrozen(bool bFreeze)
{
	if (bIsFrozen == bFreeze)
	{
		return;
	}

	bIsFrozen = bFreeze;

	if (bIsFrozen)
	{
		ApplyFrozenState();
	}
	else
	{
		ApplyUnfrozenState();
	}

	OnSightFreezeChanged.Broadcast(bIsFrozen);
}

void UMonsterSightFreezeComponent::ApplyFrozenState()
{
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!Character)
	{
		return;
	}
	if (UMonsterHearingComponent* Hearing = Character->FindComponentByClass<UMonsterHearingComponent>(); Hearing && Hearing->IsInvestigating())
	{
		Hearing->StopInvestigating();
	}

	// 1. Stop AI movement along paths
	if (AAIController* AIC = Cast<AAIController>(Character->GetController()))
	{
		AIC->StopMovement();
	}

	// 2. Halt character physics and disable movement
	if (UCharacterMovementComponent* MoveComp = Character->GetCharacterMovement())
	{
		MoveComp->StopMovementImmediately();
		MoveComp->DisableMovement();
	}

	// 3. Pause skeletal mesh animation in place
	if (USkeletalMeshComponent* Mesh = Character->GetMesh())
	{
		Mesh->bPauseAnims = true;
	}
}

void UMonsterSightFreezeComponent::ApplyUnfrozenState()
{
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!Character)
	{
		return;
	}

	// 1. Restore movement mode and apply creeping walk speed
	const UMonsterAIComponent* MonsterAI = GetMonsterAI();
	if (UCharacterMovementComponent* MoveComp = Character->GetCharacterMovement();
		MoveComp && (!MonsterAI || !MonsterAI->bIsRagdolled))
	{
		MoveComp->SetMovementMode(MOVE_Walking);
		MoveComp->MaxWalkSpeed = StalkWalkSpeed;
	}

	// 2. Resume skeletal animation
	if (USkeletalMeshComponent* Mesh = Character->GetMesh())
	{
		Mesh->bPauseAnims = false;
	}

	// 3. Immediately resume stalking toward player
	if (bAutoStalkWhenUnobserved)
	{
		ResumeStalking();
	}
}

void UMonsterSightFreezeComponent::ResumeStalking()
{
	RepathTimer = StalkRepathInterval; // Force immediate path recalculation
	UpdateStalkingMovement(0.0f);
}

void UMonsterSightFreezeComponent::UpdateStalkingMovement(float DeltaTime)
{
	if (bIsFrozen || IsReactingToCat() || UMonsterHearingComponent::IsActorInvestigating(GetOwner()))
	{
		return;
	}

	RepathTimer += DeltaTime;
	if (RepathTimer < StalkRepathInterval)
	{
		return;
	}
	RepathTimer = 0.0f;

	ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!Character)
	{
		return;
	}

	ALostWithinCharacter* Player = FindPlayerCharacter();
	if (!Player || Player->IsDead())
	{
		return;
	}

	// Ensure correct stalking walk speed
	if (UCharacterMovementComponent* MoveComp = Character->GetCharacterMovement())
	{
		if (MoveComp->MovementMode == MOVE_None)
		{
			MoveComp->SetMovementMode(MOVE_Walking);
		}
		MoveComp->MaxWalkSpeed = StalkWalkSpeed;
	}

	if (AAIController* AIC = Cast<AAIController>(Character->GetController()))
	{
		// No stop-on-overlap: the capsules overlap freely, so the Doll keeps walking until its body touches the player.
		AIC->MoveToActor(Player, StalkAcceptanceRadius, /*bStopOnOverlap=*/false);
	}
	else
	{
		// Fallback direct movement input if running without an AIController
		const FVector ToPlayer = Player->GetActorLocation() - Character->GetActorLocation();
		const FVector Direction = ToPlayer.GetSafeNormal2D();
		Character->AddMovementInput(Direction, 1.0f);
	}
}
