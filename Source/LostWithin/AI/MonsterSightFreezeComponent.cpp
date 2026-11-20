// Copyright Epic Games, Inc. All Rights Reserved.

#include "AI/MonsterSightFreezeComponent.h"

#include "AI/MonsterAIComponent.h"
#include "AI/MonsterFootstepComponent.h"
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
#include "Sound/SoundBase.h"

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
	if (bAutoStalkWhenUnobserved)
	{
		// Also runs while held, so a rush the hold interrupted ends cleanly.
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
		bIsDormant = false;
		bSeesPlayer = false;
		bHasSeenPlayer = false;
		bRushing = false;
		GazeLockSeconds = 0.0f;
		FlickerRemaining = 0.0f;
		return;
	}

	const APlayerController* PC = FindPlayerController();
	ALostWithinCharacter* PlayerChar = FindPlayerCharacter();
	bool bObserved = false;
	bool bIlluminated = false;

	if (PC)
	{
		EvaluatePlayerObservation(PC, bObserved, bIlluminated);
	}

	bool bBeamOnDoll = false;
	bIsDollLit = EvaluateDollLit(PlayerChar, bBeamOnDoll);

	// Gaze flicker: the lit beam held on the watched Doll builds interference until the flashlight blacks out.
	FlickerRemaining = FMath::Max(0.0f, FlickerRemaining - DeltaTime);
	if (bFreezeWhenWatched && GazeFlickerTime > 0.0f && FlickerRemaining <= 0.0f && bObserved && bBeamOnDoll && PlayerChar)
	{
		GazeLockSeconds += DeltaTime;
		if (!bGazeBuzzPlayed && GazeLockSeconds >= GazeFlickerTime - GazeBuzzLeadTime)
		{
			bGazeBuzzPlayed = true;
			PlaySoundAt(GazeBuzzSound, PlayerChar->GetActorLocation());
		}
		if (GazeLockSeconds >= GazeFlickerTime)
		{
			PlayerChar->BlackoutFlashlight(GazeFlickerBlackout);
			FlickerRemaining = GazeFlickerBlackout;
			GazeLockSeconds = 0.0f;
			bGazeBuzzPlayed = false;
		}
	}
	else if (FlickerRemaining <= 0.0f)
	{
		GazeLockSeconds = 0.0f;
		bGazeBuzzPlayed = false;
	}
	const bool bFlickering = FlickerRemaining > 0.0f;
	if (bFlickering)
	{
		// The beam is dark for a moment: the Doll is awake, unwatched and lunges at the player it was staring at.
		bObserved = false;
	}

	if (bIsObserved != bObserved)
	{
		bIsObserved = bObserved;
		OnObservedChanged.Broadcast(bIsObserved);
	}
	bIsIlluminated = bIlluminated;
	bIsDormant = bDormantInDarkness && !bIsDollLit && !bFlickering;

	// Sight is its only sense, and it needs light: dormant, it sees nothing and forgets.
	if (bIsDormant || !bRequireSightOfPlayer)
	{
		bSeesPlayer = false;
		if (bIsDormant)
		{
			bHasSeenPlayer = false;
		}
	}
	else
	{
		bSeesPlayer = bFlickering ? IsValid(PlayerChar) && !PlayerChar->IsDead() : CanSeePlayer(PlayerChar, bBeamOnDoll);
		if (bSeesPlayer)
		{
			bHasSeenPlayer = true;
			LastSeenPlayerLocation = PlayerChar->GetActorLocation();
		}
	}

	// Only darkness holds it now; watching freezes it only with the older bFreezeWhenWatched rule.
	const bool bShouldHold = (bFreezeWhenWatched && bIsObserved) || bIsDormant;
	if (bShouldHold != bIsFrozen)
	{
		SetFrozen(bShouldHold);
	}

	UpdateMimic(DeltaTime, PlayerChar);
}

bool UMonsterSightFreezeComponent::EvaluateDollLit(const ALostWithinCharacter* PlayerChar, bool& bOutBeamLit) const
{
	bOutBeamLit = false;
	const AActor* Owner = GetOwner();
	if (!IsValid(Owner))
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
	const ULostWithinLightSubsystem* Light = ULostWithinLightSubsystem::Get(this);
	bool bLit = false;
	for (const FVector& Point : SamplePoints)
	{
		if (PlayerChar && IsPointIlluminatedByFlashlight(PlayerChar, Point))
		{
			bOutBeamLit = true;
			bLit = true;
		}
		else if (bTorchLightCounts && Light && Light->IsPointTorchLit(Point, Owner, PlayerChar))
		{
			bLit = true;
		}
	}
	return bLit;
}

bool UMonsterSightFreezeComponent::CanSeePlayer(const ALostWithinCharacter* PlayerChar, bool bBeamOnDoll) const
{
	const AActor* Owner = GetOwner();
	if (!IsValid(Owner) || !IsValid(PlayerChar) || PlayerChar->IsDead())
	{
		return false;
	}
	FVector Eye = Owner->GetActorLocation() + FVector(0.0f, 0.0f, DetectionTraceOffsetZ);
	if (const ACharacter* Character = Cast<ACharacter>(Owner))
	{
		if (const USkeletalMeshComponent* Mesh = Character->GetMesh(); Mesh && Mesh->DoesSocketExist(HeadSocketOrBoneName))
		{
			Eye = Mesh->GetSocketLocation(HeadSocketOrBoneName);
		}
	}
	const FVector Body = PlayerChar->GetActorLocation();
	if (FVector::Dist(Eye, Body) > DollSightRange || !HasLineOfSightToPoint(Eye, Body, Owner, PlayerChar))
	{
		return false;
	}
	// The player is visible to it only when lit: standing in a torch's light, or behind a lit lens facing it.
	const ULostWithinLightSubsystem* Light = ULostWithinLightSubsystem::Get(this);
	const bool bPlayerTorchLit = bTorchLightCounts && Light && Light->IsPointTorchLit(Body, PlayerChar, Owner);
	return bPlayerTorchLit || (bBeamOnDoll && PlayerChar->IsFlashlightOn());
}

bool UMonsterSightFreezeComponent::IsActorRushingAtPlayer(const AActor* Actor)
{
	const UMonsterSightFreezeComponent* Doll = IsValid(Actor) ? Actor->FindComponentByClass<UMonsterSightFreezeComponent>() : nullptr;
	return Doll && Doll->IsRushing();
}

void UMonsterSightFreezeComponent::PlaySoundAt(USoundBase* Sound, const FVector& Location) const
{
	const UWorld* World = GetWorld();
	if (Sound && World && World->IsGameWorld())
	{
		UGameplayStatics::PlaySoundAtLocation(this, Sound, Location, 1.0f, 1.0f, 0.0f, SoundAttenuation);
	}
}

void UMonsterSightFreezeComponent::UpdateMimic(float DeltaTime, const ALostWithinCharacter* PlayerChar)
{
	const AActor* Owner = GetOwner();
	if (!bIsDormant || MimicPlayerRange <= 0.0f || !IsValid(Owner) || !IsValid(PlayerChar) || PlayerChar->IsDead()
		|| FVector::Dist(Owner->GetActorLocation(), PlayerChar->GetActorLocation()) > MimicPlayerRange)
	{
		MimicTimer = -1.0f;
		return;
	}
	const float LowInterval = FMath::Max(1.0f, static_cast<float>(FMath::Min(MimicIntervalRange.X, MimicIntervalRange.Y)));
	const float HighInterval = FMath::Max(LowInterval, static_cast<float>(FMath::Max(MimicIntervalRange.X, MimicIntervalRange.Y)));
	if (MimicTimer < 0.0f)
	{
		MimicTimer = FMath::FRandRange(LowInterval, HighInterval);
	}
	MimicTimer -= DeltaTime;
	if (MimicTimer > 0.0f)
	{
		return;
	}
	MimicTimer = FMath::FRandRange(LowInterval, HighInterval);

	const TArray<TObjectPtr<USoundBase>>* Sounds = &MimicSounds;
	if (Sounds->IsEmpty())
	{
		if (const UMonsterFootstepComponent* Steps = Owner->FindComponentByClass<UMonsterFootstepComponent>())
		{
			Sounds = &Steps->FootstepSounds;
		}
	}
	if (Sounds->IsEmpty())
	{
		return;
	}
	// A false cue to the player's left, right or behind, meant to turn their beam (and its light) away.
	const FVector Directions[] = { PlayerChar->GetActorRightVector(), -PlayerChar->GetActorRightVector(), -PlayerChar->GetActorForwardVector() };
	const FVector Direction = Directions[FMath::RandRange(0, 2)];
	const float Low = static_cast<float>(FMath::Min(MimicOffsetRange.X, MimicOffsetRange.Y));
	const float High = static_cast<float>(FMath::Max(MimicOffsetRange.X, MimicOffsetRange.Y));
	const FVector Location = PlayerChar->GetActorLocation() + Direction.GetSafeNormal2D() * FMath::FRandRange(Low, High);
	PlaySoundAt((*Sounds)[FMath::RandRange(0, Sounds->Num() - 1)], Location);
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
		MoveComp->MaxWalkSpeed = (bRequireSightOfPlayer && !bSeesPlayer && !bHasSeenPlayer && bSavedInitialSpeed)
			? CachedMaxWalkSpeed : (bRequireSightOfPlayer ? RushSpeed : StalkWalkSpeed);
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
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	ALostWithinCharacter* Player = FindPlayerCharacter();
	const bool bBlocked = bIsFrozen || IsReactingToCat() || UMonsterHearingComponent::IsActorInvestigating(GetOwner())
		|| !Character || !Player || Player->IsDead();

	if (bRequireSightOfPlayer)
	{
		// It goes for a player it sees, else for the place it last saw them; with neither, the roam loop runs.
		const bool bHasTarget = !bBlocked && (bSeesPlayer || bHasSeenPlayer);
		if (bHasTarget && !bSeesPlayer && FVector::Dist2D(Character->GetActorLocation(), LastSeenPlayerLocation) <= 60.0f)
		{
			bHasSeenPlayer = false; // nobody here any more
		}
		const bool bRushNow = bHasTarget && (bSeesPlayer || bHasSeenPlayer);
		if (!bRushNow)
		{
			if (bRushing)
			{
				bRushing = false;
				bRearClickPlayed = false;
				if (!bBlocked)
				{
					if (AAIController* AIC = Character ? Cast<AAIController>(Character->GetController()) : nullptr)
					{
						AIC->StopMovement();
					}
				}
				if (UCharacterMovementComponent* MoveComp = Character ? Character->GetCharacterMovement() : nullptr; MoveComp && bSavedInitialSpeed && !bIsFrozen)
				{
					MoveComp->MaxWalkSpeed = CachedMaxWalkSpeed;
				}
			}
			return;
		}
		if (!bRushing)
		{
			bRushing = true;
			RepathTimer = StalkRepathInterval; // move this tick
		}
		if (!bRearClickPlayed && !bIsObserved && FVector::Dist(Character->GetActorLocation(), Player->GetActorLocation()) <= RearClickDistance)
		{
			bRearClickPlayed = true;
			PlaySoundAt(RearClickSound, Character->GetActorLocation());
		}
	}
	else if (bBlocked)
	{
		return;
	}

	RepathTimer += DeltaTime;
	if (RepathTimer < StalkRepathInterval)
	{
		return;
	}
	RepathTimer = 0.0f;

	// Ensure correct stalking walk speed
	if (UCharacterMovementComponent* MoveComp = Character->GetCharacterMovement())
	{
		if (MoveComp->MovementMode == MOVE_None)
		{
			MoveComp->SetMovementMode(MOVE_Walking);
		}
		MoveComp->MaxWalkSpeed = bRequireSightOfPlayer ? RushSpeed : StalkWalkSpeed;
	}

	const bool bTowardPlayer = !bRequireSightOfPlayer || bSeesPlayer;
	if (AAIController* AIC = Cast<AAIController>(Character->GetController()))
	{
		// No stop-on-overlap: the capsules overlap freely, so the Doll keeps walking until its body touches the player.
		if (bTowardPlayer)
		{
			AIC->MoveToActor(Player, StalkAcceptanceRadius, /*bStopOnOverlap=*/false);
		}
		else
		{
			AIC->MoveToLocation(LastSeenPlayerLocation, StalkAcceptanceRadius, /*bStopOnOverlap=*/false);
		}
	}
	else
	{
		// Fallback direct movement input if running without an AIController
		const FVector Target = bTowardPlayer ? Player->GetActorLocation() : LastSeenPlayerLocation;
		Character->AddMovementInput((Target - Character->GetActorLocation()).GetSafeNormal2D(), 1.0f);
	}
}
