// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/ProximityFloatSpinComponent.h"
#include "Animation/AnimationAsset.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "CollisionQueryParams.h"
#include "Components/AudioComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/LightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SphereComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/HitResult.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/RotatingMovementComponent.h"
#include "Interaction/PickupItemActor.h"
#include "Kismet/GameplayStatics.h"
#include "LostWithin.h"
#include "LostWithinCharacter.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundWave.h"
#include "UObject/UObjectIterator.h"

namespace
{
	struct FDiscoShockwaveStage
	{
		float TriggerTime = 0.0f;
		int32 ColorMode = 0;
		FLinearColor ShockwaveColor = FLinearColor::White;
		FLinearColor EnvLightTint = FLinearColor::White;
		FLinearColor EnvSkyTint = FLinearColor::White;
		FLinearColor PostProcessTint = FLinearColor::White;
		float HueMinDeg = 0.0f;
		float HueMaxDeg = 360.0f;
		float RadiusScale = 1.0f;
	};

	const TArray<FDiscoShockwaveStage>& GetDiscoShockwaveStages()
	{
		static const TArray<FDiscoShockwaveStage> Stages = {
			{ 12.46f, 1, FLinearColor(1.00f, 0.14f, 0.85f), FLinearColor(0.95f, 0.14f, 0.85f), FLinearColor(1.00f, 0.20f, 0.92f), FLinearColor(1.00f, 0.72f, 0.96f), 280.0f, 348.0f, 1.0f },
			{ 14.06f, 2, FLinearColor(1.00f, 0.58f, 0.06f), FLinearColor(1.00f, 0.52f, 0.08f), FLinearColor(1.00f, 0.60f, 0.12f), FLinearColor(1.00f, 0.88f, 0.68f),  22.0f,  56.0f, 1.0f },
			{ 15.66f, 3, FLinearColor(0.10f, 1.00f, 0.88f), FLinearColor(0.08f, 0.95f, 0.86f), FLinearColor(0.14f, 1.00f, 0.90f), FLinearColor(0.70f, 1.00f, 0.94f), 145.0f, 205.0f, 1.0f },
			{ 17.26f, 4, FLinearColor(1.00f, 0.08f, 0.20f), FLinearColor(1.00f, 0.08f, 0.18f), FLinearColor(1.00f, 0.12f, 0.24f), FLinearColor(1.00f, 0.66f, 0.70f), 338.0f, 368.0f, 1.0f },
			{ 34.46f, 5, FLinearColor(0.8f, 1.0f, 1.0f), FLinearColor(0.8f, 1.0f, 1.0f), FLinearColor(0.8f, 1.0f, 1.0f), FLinearColor(0.8f, 1.0f, 1.0f), 0.0f, 360.0f, 0.25f },
			{ 37.65f, 6, FLinearColor(0.8f, 1.0f, 1.0f), FLinearColor(0.8f, 1.0f, 1.0f), FLinearColor(0.8f, 1.0f, 1.0f), FLinearColor(0.8f, 1.0f, 1.0f), 0.0f, 360.0f, 0.25f },
			{ 40.86f, 7, FLinearColor(0.8f, 1.0f, 1.0f), FLinearColor(0.8f, 1.0f, 1.0f), FLinearColor(0.8f, 1.0f, 1.0f), FLinearColor(0.8f, 1.0f, 1.0f), 0.0f, 360.0f, 0.25f }
		};
		return Stages;
	}

	const FDiscoShockwaveStage* FindDiscoShockwaveStageByMode(int32 Mode)
	{
		for (const FDiscoShockwaveStage& Stage : GetDiscoShockwaveStages())
		{
			if (Stage.ColorMode == Mode)
			{
				return &Stage;
			}
		}
		return nullptr;
	}
}

UProximityFloatSpinComponent::UProximityFloatSpinComponent()
{
	PrimaryComponentTick.bCanEverTick = true;

	SpinTimeWindows = {
		{ 0.27f, 1.77f, 1.5f },
		{ 3.12f, 5.10f, 1.0f },
		{ 6.06f, 42.89f, 1.5f }
	};

	DropItemClass = APickupItemActor::StaticClass();

	InteractionSoundSearchPaths = {
		FName(TEXT("/Game/NPC/Cat/SFX/Meow")),
		FName(TEXT("/Game/SoundWave/SoundCue/MetaSound/Cat")),
		FName(TEXT("/Game/MetaSound/Cat"))
	};
}

void UProximityFloatSpinComponent::BeginPlay()
{
	Super::BeginPlay();

	AActor* Owner = GetOwner();
	if (!Owner || !Owner->GetRootComponent())
	{
		UE_LOG(LogLostWithin, Warning, TEXT("ProximityFloatSpinComponent on '%s' has no valid owner or root component; disabling tick."), *GetNameSafe(Owner));
		SetComponentTickEnabled(false);
		return;
	}

	if (bAutoSetHierarchyMovable)
	{
		EnsureHierarchyMovable(Owner);
	}

	if (bDisableExistingRotatingMovement)
	{
		DisableConflictingRotatingMovement(Owner);
	}

	for (FSpinTimeWindow& Window : SpinTimeWindows)
	{
		if (Window.EndTime > 42.89f)
		{
			Window.EndTime = 42.89f;
		}
	}

	// Tighten legacy serialized vicinity radii so the check stays close around the player
	if (MorphVicinityRadius >= 750.0f)
	{
		MorphVicinityRadius = 180.0f;
	}
	if (MorphVicinityDeactivationRadius >= 800.0f)
	{
		MorphVicinityDeactivationRadius = 220.0f;
	}

	BaseLocation = Owner->GetActorLocation();
	BaseRotation = Owner->GetActorRotation();
	CachedPathAnchorTransform = FTransform(BaseRotation, BaseLocation, FVector::OneVector);
	bHasCachedPathAnchorTransform = true;
	RuntimeWorldWaypoints.Reset();
	bUsingRuntimeOverrideWaypoints = false;
	CurrentWaypointIndex = 0;
	bIsFollowingPath = false;
	bPathPaused = false;
	bHasCompletedPath = false;
	bHasAutoTriggeredDirectedPath = false;
	BaseActorScale3D = Owner->GetActorScale3D();
	BaseMeshRelativeLocation = FVector::ZeroVector;
	BaseMeshRelativeScale3D = FVector::OneVector;
	MeshPivotHeightAboveBottom = 0.0f;
	ActorPivotHeightAboveBottom = 0.0f;
	bIsActive = false;
	bHasTriggered = false;
	bIsInMorphVicinity = false;
	bSettledAtRest = true;
	bWasSpinningLastTick = false;
	bOwnsRuntimeSkeletalMesh = false;
	CurrentHoverHeight = 0.0f;
	FallVelocity = 0.0f;
	CurrentVicinityZOffset = 0.0f;
	CurrentMorphWeight = bUseMorphVicinityCheck ? ActiveMorphWeight : InactiveMorphWeight;
	BobPhase = 0.0f;
	ElapsedTriggeredTime = 0.0f;
	CurrentSpinOffsetYaw = 0.0f;
	InteractionCount = 0;
	bHasDroppedItem = false;
	CompletedPetSpinRounds = 0;
	bAwaitingPlayerSpin = false;
	LastPetInteractor.Reset();
	ResetPlayerSpinTracking();
	bIsInteractionSqueezing = false;
	InteractionSqueezeElapsed = 0.0f;
	CurrentSqueezeFraction = 0.0f;
	bWorldSkyAndLightsCached = false;
	CurrentSkyDarknessAlpha = 0.0f;
	CachedWorldSkyAndLights.Reset();
	bIsDiscoActive = false;
	DiscoElapsedTime = 0.0f;
	DiscoOrbitYaw = 0.0f;
	ActiveAudioComponent = nullptr;
	RuntimeSkeletalMeshComponent = nullptr;
	InteractionTraceSphere = nullptr;
	DiscoRigPivot = nullptr;
	DiscoSpotlights.Reset();
	DiscoFillPointLight = nullptr;
	DiscoPostProcessComponent = nullptr;
	ShockwaveRingSegments.Reset();
	ShockwaveRingMaterial = nullptr;
	ShockwaveFlashPointLight = nullptr;
	ResetTimelineEffects();

	InitializeVisualMeshes(Owner);

	const FBox OwnerBounds = Owner->GetComponentsBoundingBox(/*bNonColliding=*/true);
	if (OwnerBounds.IsValid)
	{
		ActorPivotHeightAboveBottom = FMath::Max(0.0f, Owner->GetActorLocation().Z - OwnerBounds.Min.Z);
	}

	LoadInteractionSounds();

	ResolveConfiguredWaypointsToWorld(Owner);
	TryTriggerDirectedPath(EDirectedPathTriggerMode::OnBeginPlay);
}

void UProximityFloatSpinComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	DestroyCatShockwaveVisuals();
	SetDiscoEnvironmentActive(GetOwner(), false);
	RestoreWorldSkyAndLights();
	ResetInteractionSqueeze(GetOwner());
	ResetTimelineEffects();
	StopTriggerMusic();

	if (InteractionTraceSphere)
	{
		InteractionTraceSphere->DestroyComponent();
		InteractionTraceSphere = nullptr;
	}

	if (RuntimeSkeletalMeshComponent)
	{
		if (bOwnsRuntimeSkeletalMesh)
		{
			RuntimeSkeletalMeshComponent->DestroyComponent();
		}
		RuntimeSkeletalMeshComponent = nullptr;
		bOwnsRuntimeSkeletalMesh = false;
	}

	Super::EndPlay(EndPlayReason);
}

void UProximityFloatSpinComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	if (bHasDespawnedCat)
	{
		ApplyFloatAndSpinTransform(Owner, DeltaTime);
		return;
	}

	if (const APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0))
	{
		if (bUseMorphVicinityCheck)
		{
			UpdateMorphVicinityState(PlayerPawn);
		}

		// Pet-and-spin ritual: the player's view yaw (controller, else the pawn's own facing) while near the cat.
		if (bAwaitingPlayerSpin)
		{
			const float PlayerViewYaw = PlayerPawn->GetController()
				? PlayerPawn->GetControlRotation().Yaw
				: PlayerPawn->GetActorRotation().Yaw;
			UpdatePlayerSpin(PlayerPawn->GetActorLocation(), PlayerViewYaw);
		}

		if (!bTriggerSpinByInteractionCount && !(bHasTriggered && bStayActiveOnceTriggered))
		{
			UpdateProximityState(Owner, PlayerPawn);
		}
	}

	UpdateDirectedPathMovement(Owner, DeltaTime);

	if (bDrawDebugPath)
	{
		DrawDirectedPathDebug();
	}

	if (bDrawDebugRadius)
	{
		if (const UWorld* World = GetWorld())
		{
			if (!bTriggerSpinByInteractionCount)
			{
				const FColor ActivationColor = bIsActive ? FColor::Green : FColor::Cyan;
				DrawDebugSphere(World, BaseLocation, ActivationRadius, 24, ActivationColor, false, -1.0f, 0, 1.5f);

				const float EffectiveDeactivationRadius = FMath::Max(ActivationRadius, DeactivationRadius);
				if (!bStayActiveOnceTriggered && EffectiveDeactivationRadius > ActivationRadius)
				{
					DrawDebugSphere(World, BaseLocation, EffectiveDeactivationRadius, 24, FColor::Yellow, false, -1.0f, 0, 1.0f);
				}
			}

			if (bUseMorphVicinityCheck)
			{
				const FColor VicinityColor = bIsInMorphVicinity ? FColor::Orange : FColor::Blue;
				DrawDebugSphere(World, BaseLocation, MorphVicinityRadius, 24, VicinityColor, false, -1.0f, 0, 1.0f);
			}
		}
	}

	ApplyFloatAndSpinTransform(Owner, DeltaTime);
}

void UProximityFloatSpinComponent::EnsureHierarchyMovable(AActor* Owner) const
{
	TArray<AActor*> ActorsToUpdate;
	ActorsToUpdate.Add(Owner);
	Owner->GetAttachedActors(ActorsToUpdate, /*bResetArray=*/false, /*bRecursivelyIncludeAttachedActors=*/true);

	int32 UpdatedCount = 0;
	TArray<USceneComponent*> SceneComponents;
	for (AActor* Actor : ActorsToUpdate)
	{
		if (!Actor)
		{
			continue;
		}

		SceneComponents.Reset();
		Actor->GetComponents<USceneComponent>(SceneComponents);
		for (USceneComponent* SceneComp : SceneComponents)
		{
			if (SceneComp && SceneComp->Mobility == EComponentMobility::Static)
			{
				SceneComp->SetMobility(EComponentMobility::Movable);
				++UpdatedCount;
			}
		}
	}

	if (UpdatedCount > 0)
	{
		UE_LOG(LogLostWithin, Warning, TEXT("ProximityFloatSpinComponent on '%s' switched %d Static component(s) to Movable at BeginPlay. Set their Mobility to Movable in the editor for clean builds."), *GetNameSafe(Owner), UpdatedCount);
	}
}

void UProximityFloatSpinComponent::DisableConflictingRotatingMovement(AActor* Owner) const
{
	TArray<URotatingMovementComponent*> RotatingComponents;
	Owner->GetComponents<URotatingMovementComponent>(RotatingComponents);
	for (URotatingMovementComponent* RotatingComp : RotatingComponents)
	{
		if (RotatingComp)
		{
			RotatingComp->RotationRate = FRotator::ZeroRotator;
			RotatingComp->Deactivate();
			RotatingComp->SetComponentTickEnabled(false);
			UE_LOG(LogLostWithin, Log, TEXT("ProximityFloatSpinComponent on '%s' disabled existing RotatingMovementComponent '%s'."), *GetNameSafe(Owner), *RotatingComp->GetName());
		}
	}
}

void UProximityFloatSpinComponent::InitializeVisualMeshes(AActor* Owner)
{
	if (bHideAttachedChildActors)
	{
		TArray<AActor*> AttachedActors;
		Owner->GetAttachedActors(AttachedActors, /*bResetArray=*/true, /*bRecursivelyIncludeAttachedActors=*/true);
		for (AActor* ChildActor : AttachedActors)
		{
			if (ChildActor)
			{
				ChildActor->SetActorHiddenInGame(true);
				ChildActor->SetActorEnableCollision(false);
			}
		}
	}

	USkeletalMeshComponent* ExistingSkelComp = Owner->FindComponentByClass<USkeletalMeshComponent>();
	if (!StandbySkeletalMesh && (!ExistingSkelComp || !ExistingSkelComp->GetSkeletalMeshAsset()))
	{
		StandbySkeletalMesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/NPC/Cat/SkeletalMeshes/oiiaioooooiai_cat.oiiaioooooiai_cat"));
	}

	if (ExistingSkelComp)
	{
		RuntimeSkeletalMeshComponent = ExistingSkelComp;
		bOwnsRuntimeSkeletalMesh = false;
		if (StandbySkeletalMesh)
		{
			ExistingSkelComp->SetSkeletalMesh(StandbySkeletalMesh, true);
		}
	}
	else if (StandbySkeletalMesh)
	{
		USkeletalMeshComponent* SkelComp = NewObject<USkeletalMeshComponent>(Owner);
		if (SkelComp)
		{
			SkelComp->SetMobility(EComponentMobility::Movable);
			SkelComp->SetRelativeTransform(StandbyMeshOffset);
			SkelComp->AttachToComponent(Owner->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
			SkelComp->RegisterComponent();
			SkelComp->SetSkeletalMesh(StandbySkeletalMesh, true);

			RuntimeSkeletalMeshComponent = SkelComp;
			bOwnsRuntimeSkeletalMesh = true;
		}
	}

	if (!StandbyAnimation)
	{
		StandbyAnimation = LoadObject<UAnimationAsset>(nullptr, TEXT("/Game/NPC/Cat/SkeletalMeshes/oiiaioooooiai_cat_Anim.oiiaioooooiai_cat_Anim"));
	}

	if (RuntimeSkeletalMeshComponent)
	{
		// Strip the oversized PhysicsAsset and disable all physics/pawn collision so non-uniform squeeze/unstable-matter
		// scale changes never trigger Chaos depenetration impulses that launch the player out of the world.
		RuntimeSkeletalMeshComponent->SetSimulatePhysics(false);
		RuntimeSkeletalMeshComponent->SetPhysicsAsset(nullptr, /*bForceReInit=*/true);
		RuntimeSkeletalMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		RuntimeSkeletalMeshComponent->SetCollisionResponseToAllChannels(ECR_Ignore);
		RuntimeSkeletalMeshComponent->SetGenerateOverlapEvents(false);
		RuntimeSkeletalMeshComponent->bEnablePerPolyCollision = false;
		RuntimeSkeletalMeshComponent->CanCharacterStepUpOn = ECB_No;

		if (!RuntimeSkeletalMeshComponent->GetMaterial(0))
		{
			if (UMaterialInterface* DefaultCatMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/NPC/Cat/Materials/Muchkin2.Muchkin2")))
			{
				RuntimeSkeletalMeshComponent->SetMaterial(0, DefaultCatMaterial);
			}
		}

		// Apply material instance to all slots for skeletal mesh
		CatMaterialInstances.Reset();
		for (int32 Index = 0; Index < RuntimeSkeletalMeshComponent->GetNumMaterials(); ++Index)
		{
			if (UMaterialInterface* CatMaterial = RuntimeSkeletalMeshComponent->GetMaterial(Index))
			{
				UMaterialInstanceDynamic* DynamicMaterial = UMaterialInstanceDynamic::Create(CatMaterial, this);
				if (DynamicMaterial)
				{
					CatMaterialInstances.Add(DynamicMaterial);
					RuntimeSkeletalMeshComponent->SetMaterial(Index, DynamicMaterial);
				}
			}
		}

		BaseMeshRelativeLocation = RuntimeSkeletalMeshComponent->GetRelativeLocation();
		BaseMeshRelativeScale3D = RuntimeSkeletalMeshComponent->GetRelativeScale3D();
		RuntimeSkeletalMeshComponent->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		if (bDriveSpinMorphTarget)
		{
			RuntimeSkeletalMeshComponent->Stop();
			CurrentMorphWeight = bUseMorphVicinityCheck ? ActiveMorphWeight : InactiveMorphWeight;
			if (!SpinMorphTargetName.IsNone())
			{
				RuntimeSkeletalMeshComponent->SetMorphTarget(SpinMorphTargetName, CurrentMorphWeight, /*bRemoveZeroWeight=*/false);
			}

			CurrentVicinityZOffset = bUseMorphVicinityCheck ? ActiveMorphMeshZOffset : 0.0f;
			if (RuntimeSkeletalMeshComponent != Owner->GetRootComponent())
			{
				RuntimeSkeletalMeshComponent->SetRelativeLocation(BaseMeshRelativeLocation + FVector(0.0f, 0.0f, CurrentVicinityZOffset));
			}
			else if (!FMath::IsNearlyZero(CurrentVicinityZOffset))
			{
				Owner->SetActorLocation(BaseLocation + FVector(0.0f, 0.0f, CurrentVicinityZOffset), false, nullptr, ETeleportType::TeleportPhysics);
			}
		}
	}

	bWasSpinningLastTick = false;
	UpdateSpinAnimationState(false);

	// Disable physics/blocking collision on any other pre-existing primitive components on the cat actor
	TArray<UPrimitiveComponent*> PrimitiveComponents;
	Owner->GetComponents<UPrimitiveComponent>(PrimitiveComponents);
	for (UPrimitiveComponent* PrimComp : PrimitiveComponents)
	{
		if (PrimComp && PrimComp != RuntimeSkeletalMeshComponent)
		{
			PrimComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			PrimComp->SetCollisionResponseToAllChannels(ECR_Ignore);
			PrimComp->SetGenerateOverlapEvents(false);
			PrimComp->CanCharacterStepUpOn = ECB_No;
		}
	}

	// Ensure a reliable query-only hitbox exists solely for [F] interaction and [E] hit traces
	if (USphereComponent* TraceSphere = NewObject<USphereComponent>(Owner))
	{
		TraceSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		TraceSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
		TraceSphere->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
		TraceSphere->SetGenerateOverlapEvents(false);
		TraceSphere->CanCharacterStepUpOn = ECB_No;
		TraceSphere->SetUsingAbsoluteScale(true);
		TraceSphere->SetWorldScale3D(FVector::OneVector);

		const float DesiredWorldRadius = FMath::Clamp(InteractionTraceRadius, 25.0f, 65.0f);
		const FVector SphereWorldOrigin = BaseLocation + FVector(0.0f, 0.0f, DesiredWorldRadius * 0.65f);

		if (RuntimeSkeletalMeshComponent)
		{
			RuntimeSkeletalMeshComponent->UpdateBounds();
			const FBoxSphereBounds MeshBounds = RuntimeSkeletalMeshComponent->Bounds;
			if (MeshBounds.SphereRadius > KINDA_SMALL_NUMBER && MeshBounds.SphereRadius < 300.0f)
			{
				const float BottomWorldZ = MeshBounds.Origin.Z - MeshBounds.BoxExtent.Z;
				const float WorldPivotAboveBottom = FMath::Max(0.0f, RuntimeSkeletalMeshComponent->GetComponentLocation().Z - BottomWorldZ);
				const USceneComponent* AttachParent = RuntimeSkeletalMeshComponent->GetAttachParent();
				const float ParentScaleZ = AttachParent ? FMath::Max(KINDA_SMALL_NUMBER, FMath::Abs(AttachParent->GetComponentScale().Z)) : 1.0f;
				MeshPivotHeightAboveBottom = WorldPivotAboveBottom / ParentScaleZ;
			}
		}

		TraceSphere->InitSphereRadius(DesiredWorldRadius);
		TraceSphere->SetWorldLocation(SphereWorldOrigin);
		TraceSphere->AttachToComponent(Owner->GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);
		TraceSphere->RegisterComponent();
		InteractionTraceSphere = TraceSphere;
	}
}

void UProximityFloatSpinComponent::UpdateSpinAnimationState(bool bIsSpinning)
{
	bWasSpinningLastTick = bIsSpinning;

	if (!RuntimeSkeletalMeshComponent || bDriveSpinMorphTarget)
	{
		return;
	}

	if (bIsSpinning)
	{
		if (bStopAnimationWhileSpinning)
		{
			if (StandbySkeletalMesh)
			{
				RuntimeSkeletalMeshComponent->SetSkeletalMesh(StandbySkeletalMesh, true);
			}
			RuntimeSkeletalMeshComponent->Stop();
		}
	}
	else if (StandbyAnimation)
	{
		RuntimeSkeletalMeshComponent->PlayAnimation(StandbyAnimation, bLoopStandbyAnimation);
	}
}

void UProximityFloatSpinComponent::UpdateSpinMorphTarget(bool bIsSpinning, float DeltaTime)
{
	if (!bDriveSpinMorphTarget || !RuntimeSkeletalMeshComponent || SpinMorphTargetName.IsNone())
	{
		return;
	}

	float TargetWeight = InactiveMorphWeight;
	if (bIsActive || bHasTriggered)
	{
		// Once triggered, use ActiveMorphWeight only during active spin windows and keep
		// InactiveMorphWeight between the floatspins regardless of player vicinity.
		const bool bShowActiveMorph = (bIsSpinning || bTriggeredPopOutEffect) && bActiveMorphWhileSpinning;
		TargetWeight = bShowActiveMorph ? ActiveMorphWeight : InactiveMorphWeight;
	}
	else if (bUseMorphVicinityCheck)
	{
		// Pre-trigger idle: far = ActiveMorphWeight (1.0); inside vicinity = InactiveMorphWeight (0.0).
		TargetWeight = bIsInMorphVicinity ? InactiveMorphWeight : ActiveMorphWeight;
	}

	CurrentMorphWeight = (MorphBlendSpeed > 0.0f)
		? FMath::FInterpTo(CurrentMorphWeight, TargetWeight, DeltaTime, MorphBlendSpeed)
		: TargetWeight;

	RuntimeSkeletalMeshComponent->SetMorphTarget(SpinMorphTargetName, CurrentMorphWeight, /*bRemoveZeroWeight=*/false);
}

void UProximityFloatSpinComponent::UpdateVicinityMeshHeight(bool bIsSpinning, float JumpOffset, float SqueezeFraction, float DeltaTime)
{
	if (!RuntimeSkeletalMeshComponent)
	{
		return;
	}

	const bool bShouldLowerForActiveMorph = bUseMorphVicinityCheck
		&& !bIsActive
		&& !bHasTriggered
		&& !bIsInMorphVicinity
		&& !bIsSpinning
		&& CurrentHoverHeight <= 0.0f
		&& JumpOffset <= 0.0f;
	const float TargetZOffset = bShouldLowerForActiveMorph ? ActiveMorphMeshZOffset : 0.0f;
	const float PreviousVicinityZOffset = CurrentVicinityZOffset;
	CurrentVicinityZOffset = FMath::FInterpTo(CurrentVicinityZOffset, TargetZOffset, DeltaTime, MorphHeightBlendSpeed);
	if (FMath::IsNearlyEqual(CurrentVicinityZOffset, TargetZOffset, KINDA_SMALL_NUMBER))
	{
		CurrentVicinityZOffset = TargetZOffset;
	}
	if (!FMath::IsNearlyEqual(PreviousVicinityZOffset, CurrentVicinityZOffset, KINDA_SMALL_NUMBER))
	{
		bSettledAtRest = false;
	}

	const AActor* Owner = GetOwner();
	if (!Owner || RuntimeSkeletalMeshComponent == Owner->GetRootComponent())
	{
		return;
	}

	const float SqueezeZCompensation = -MeshPivotHeightAboveBottom * (SqueezeFraction * InteractionSqueezeScaleZ);
	const FVector DesiredRelLoc = BaseMeshRelativeLocation + FVector(0.0f, 0.0f, CurrentVicinityZOffset + SqueezeZCompensation);
	if (!RuntimeSkeletalMeshComponent->GetRelativeLocation().Equals(DesiredRelLoc, KINDA_SMALL_NUMBER))
	{
		RuntimeSkeletalMeshComponent->SetRelativeLocation(DesiredRelLoc);
	}
}

float UProximityFloatSpinComponent::ComputePlayerDistanceSq(const APawn* PlayerPawn) const
{
	if (!PlayerPawn)
	{
		return BIG_NUMBER;
	}

	FVector CatWorldCenter = BaseLocation;
	if (RuntimeSkeletalMeshComponent)
	{
		const FBoxSphereBounds MeshBounds = RuntimeSkeletalMeshComponent->Bounds;
		if (MeshBounds.SphereRadius > KINDA_SMALL_NUMBER && MeshBounds.SphereRadius < 300.0f)
		{
			CatWorldCenter = MeshBounds.Origin;
		}
		else
		{
			CatWorldCenter = RuntimeSkeletalMeshComponent->GetComponentLocation();
		}
	}
	else if (const AActor* Owner = GetOwner())
	{
		CatWorldCenter = Owner->GetActorLocation();
	}

	if (const UPrimitiveComponent* PlayerRootPrim = Cast<UPrimitiveComponent>(PlayerPawn->GetRootComponent()))
	{
		FVector ClosestPointOnPlayer = FVector::ZeroVector;
		if (PlayerRootPrim->GetClosestPointOnCollision(CatWorldCenter, ClosestPointOnPlayer) >= 0.0f)
		{
			return FVector::DistSquared(CatWorldCenter, ClosestPointOnPlayer);
		}
	}

	return FVector::DistSquaredXY(CatWorldCenter, PlayerPawn->GetActorLocation());
}

void UProximityFloatSpinComponent::UpdateMorphVicinityState(const APawn* PlayerPawn)
{
	const float DistanceSq = ComputePlayerDistanceSq(PlayerPawn);
	const float EnterRadiusSq = FMath::Square(MorphVicinityRadius);
	const float EffectiveExitRadius = FMath::Max(MorphVicinityRadius, MorphVicinityDeactivationRadius);
	const float ExitRadiusSq = FMath::Square(EffectiveExitRadius);

	if (!bIsInMorphVicinity && DistanceSq <= EnterRadiusSq)
	{
		bIsInMorphVicinity = true;
	}
	else if (bIsInMorphVicinity && DistanceSq > ExitRadiusSq)
	{
		bIsInMorphVicinity = false;
	}
}

void UProximityFloatSpinComponent::UpdateProximityState(AActor* Owner, const APawn* PlayerPawn)
{
	const float DistanceSq = ComputePlayerDistanceSq(PlayerPawn);
	const float ActivationRadiusSq = FMath::Square(ActivationRadius);
	const float EffectiveDeactivationRadius = FMath::Max(ActivationRadius, DeactivationRadius);
	const float DeactivationRadiusSq = FMath::Square(EffectiveDeactivationRadius);

	if (!bIsActive && DistanceSq <= ActivationRadiusSq)
	{
		if (bStayActiveOnceTriggered && bHasTriggered)
		{
			return;
		}

		TriggerSpinSequence(Owner);
	}
	else if (bIsActive && !bStayActiveOnceTriggered && DistanceSq > DeactivationRadiusSq)
	{
		bIsActive = false;
		SetDiscoEnvironmentActive(Owner, false);
		ResetTimelineEffects();
		if (bStopMusicOnDeactivate)
		{
			StopTriggerMusic();
		}
		OnProximityFloatStateChanged.Broadcast(false);
		OnCatSanctuaryStateChanged.Broadcast(false);
	}
}

void UProximityFloatSpinComponent::TriggerSpinSequence(AActor* Owner)
{
	if (!Owner || bIsActive)
	{
		return;
	}

	ResetInteractionSqueeze(Owner);
	ResetTimelineEffects();
	bIsActive = true;
	bHasTriggered = true;
	bSettledAtRest = false;
	ElapsedTriggeredTime = 0.0f;
	StartTriggerMusic(Owner);
	OnProximityFloatStateChanged.Broadcast(true);
	OnCatSanctuaryStateChanged.Broadcast(true);
	TryTriggerDirectedPath(EDirectedPathTriggerMode::OnSpinTriggered);
}

void UProximityFloatSpinComponent::LoadInteractionSounds()
{
	if (InteractionSounds.IsEmpty())
	{
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

#if WITH_EDITOR
		if (!InteractionSoundSearchPaths.IsEmpty())
		{
			TArray<FString> PathsToScan;
			PathsToScan.Reserve(InteractionSoundSearchPaths.Num());
			for (const FName& PathName : InteractionSoundSearchPaths)
			{
				PathsToScan.Add(PathName.ToString());
			}
			AssetRegistry.ScanPathsSynchronous(PathsToScan, false);
		}
#endif

		TArray<FAssetData> FoundAssets;
		if (!InteractionSoundSearchPaths.IsEmpty())
		{
			AssetRegistry.GetAssetsByPaths(InteractionSoundSearchPaths, FoundAssets, /*bRecursive=*/true);
		}

		for (const FAssetData& AssetData : FoundAssets)
		{
			if (USoundBase* LoadedSound = Cast<USoundBase>(AssetData.GetAsset()))
			{
				InteractionSounds.AddUnique(LoadedSound);
			}
		}

		if (InteractionSounds.IsEmpty())
		{
			static constexpr const TCHAR* DefaultCatMeowPaths[] = {
				TEXT("/Game/NPC/Cat/SFX/Meow/Cat_Meow_SFX_01.Cat_Meow_SFX_01"),
				TEXT("/Game/NPC/Cat/SFX/Meow/Cat_Meow_SFX_02.Cat_Meow_SFX_02"),
				TEXT("/Game/NPC/Cat/SFX/Meow/Cat_Meow_SFX_03.Cat_Meow_SFX_03"),
				TEXT("/Game/NPC/Cat/SFX/Meow/Cat_Meow_SFX_04.Cat_Meow_SFX_04"),
				TEXT("/Game/NPC/Cat/SFX/Meow/Cat_Meow_SFX_05.Cat_Meow_SFX_05")
			};

			for (const TCHAR* AssetPath : DefaultCatMeowPaths)
			{
				if (USoundBase* DefaultCatSound = LoadObject<USoundBase>(nullptr, AssetPath))
				{
					InteractionSounds.AddUnique(DefaultCatSound);
				}
			}
		}
	}

	// Ensure one-shot meow SFX never get stuck looping due to imported WAV cue/loop flags
	for (const TObjectPtr<USoundBase>& Sound : InteractionSounds)
	{
		if (USoundWave* SoundWave = Cast<USoundWave>(Sound))
		{
			SoundWave->bLooping = false;
		}
	}
}

void UProximityFloatSpinComponent::PlayRandomInteractionSound(AActor* Owner) const
{
	if (!Owner || InteractionSounds.IsEmpty())
	{
		return;
	}

	const int32 RandomIndex = FMath::RandRange(0, InteractionSounds.Num() - 1);
	USoundBase* SelectedSound = InteractionSounds[RandomIndex];
	if (SelectedSound)
	{
		UGameplayStatics::PlaySoundAtLocation(
			Owner,
			SelectedSound,
			Owner->GetActorLocation(),
			InteractionSoundVolume,
			1.0f,
			0.0f,
			MusicAttenuation);
	}
}

void UProximityFloatSpinComponent::TriggerInteractionSqueeze()
{
	bIsInteractionSqueezing = true;
	InteractionSqueezeElapsed = 0.0f;
	bSettledAtRest = false;
}

float UProximityFloatSpinComponent::UpdateInteractionSqueeze(AActor* Owner, float DeltaTime)
{
	float SqueezeFraction = 0.0f;
	if (bIsInteractionSqueezing)
	{
		InteractionSqueezeElapsed += DeltaTime;
		if (InteractionSqueezeDuration <= 0.0f || InteractionSqueezeElapsed >= InteractionSqueezeDuration)
		{
			bIsInteractionSqueezing = false;
			InteractionSqueezeElapsed = 0.0f;
		}
		else
		{
			const float Alpha = FMath::Clamp(InteractionSqueezeElapsed / InteractionSqueezeDuration, 0.0f, 1.0f);
			SqueezeFraction = 4.0f * Alpha * (1.0f - Alpha);
		}
	}

	const float SqueezeZ = SqueezeFraction * InteractionSqueezeScaleZ;
	const float ExpandXY = SqueezeFraction * InteractionSqueezeHorizontalExpand;
	const FVector SqueezeScale(1.0f + ExpandXY, 1.0f + ExpandXY, FMath::Max(0.25f, 1.0f - SqueezeZ));
	const FVector ScaleMultiplier = SqueezeScale * UnstableScaleMultiplier * UpdatePopOutScale(DeltaTime);

	if (RuntimeSkeletalMeshComponent)
	{
		const FVector DesiredScale = BaseMeshRelativeScale3D * ScaleMultiplier;
		if (!RuntimeSkeletalMeshComponent->GetRelativeScale3D().Equals(DesiredScale, KINDA_SMALL_NUMBER))
		{
			RuntimeSkeletalMeshComponent->SetRelativeScale3D(DesiredScale);
		}
	}
	else if (Owner)
	{
		const FVector DesiredActorScale = BaseActorScale3D * ScaleMultiplier;
		if (!Owner->GetActorScale3D().Equals(DesiredActorScale, KINDA_SMALL_NUMBER))
		{
			Owner->SetActorScale3D(DesiredActorScale);
		}
	}

	return SqueezeFraction;
}

void UProximityFloatSpinComponent::ResetInteractionSqueeze(AActor* Owner)
{
	bIsInteractionSqueezing = false;
	InteractionSqueezeElapsed = 0.0f;
	CurrentSqueezeFraction = 0.0f;
	bIsPopOutActive = false;
	PopOutElapsed = 0.0f;
	UnstableMatterIntensity = 0.0f;
	UnstableScaleMultiplier = FVector::OneVector;

	if (RuntimeSkeletalMeshComponent)
	{
		RuntimeSkeletalMeshComponent->SetRelativeScale3D(BaseMeshRelativeScale3D);
		if (!Owner || RuntimeSkeletalMeshComponent != Owner->GetRootComponent())
		{
			RuntimeSkeletalMeshComponent->SetRelativeLocation(BaseMeshRelativeLocation + FVector(0.0f, 0.0f, CurrentVicinityZOffset));
		}
	}
	else if (Owner)
	{
		Owner->SetActorScale3D(BaseActorScale3D);
	}
}

void UProximityFloatSpinComponent::UpdateSkyDarkening(float DeltaTime)
{
	float TargetAlpha = 0.0f;
	bool bTrackSmoothStepDirect = false;

	if (bDarkenSkyOnSpinWindow && bIsActive && bUseSpinTimeWindows && SpinTimeWindows.IsValidIndex(SkyDarkenStartWindowIndex))
	{
		const FSpinTimeWindow& DarkenWindow = SpinTimeWindows[SkyDarkenStartWindowIndex];
		const float SequenceEndTime = SpinTimeWindows.Last().EndTime;

		if (ElapsedTriggeredTime >= DarkenWindow.StartTime && ElapsedTriggeredTime <= SequenceEndTime)
		{
			bTrackSmoothStepDirect = true;
			if (DarkenWindow.EndTime > DarkenWindow.StartTime && ElapsedTriggeredTime <= DarkenWindow.EndTime)
			{
				TargetAlpha = FMath::SmoothStep(DarkenWindow.StartTime, DarkenWindow.EndTime, ElapsedTriggeredTime);
			}
			else
			{
				TargetAlpha = 1.0f;
			}
		}
	}

	if (bTrackSmoothStepDirect)
	{
		CurrentSkyDarknessAlpha = FMath::Max(CurrentSkyDarknessAlpha, TargetAlpha);
	}
	else if (CurrentSkyDarknessAlpha > 0.0f)
	{
		CurrentSkyDarknessAlpha = FMath::FInterpTo(CurrentSkyDarknessAlpha, 0.0f, DeltaTime, SkyRestoreSpeed);
		if (CurrentSkyDarknessAlpha < 0.002f)
		{
			CurrentSkyDarknessAlpha = 0.0f;
		}
	}

	if (CurrentSkyDarknessAlpha > 0.0f)
	{
		if (!bWorldSkyAndLightsCached)
		{
			CacheWorldSkyAndLights();
		}
		ApplyWorldSkyDarkness(CurrentSkyDarknessAlpha);
	}
	else if (bWorldSkyAndLightsCached)
	{
		RestoreWorldSkyAndLights();
	}
}

void UProximityFloatSpinComponent::CacheWorldSkyAndLights()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	CachedWorldSkyAndLights.Reset();
	bWorldSkyAndLightsCached = true;

	for (TObjectIterator<UDirectionalLightComponent> It; It; ++It)
	{
		UDirectionalLightComponent* DirLight = *It;
		if (DirLight && DirLight->GetWorld() == World && DirLight->IsRegistered() && DirLight->Mobility != EComponentMobility::Static)
		{
			FCachedWorldSkyAndLightState State;
			State.TargetType = ECachedAtmosphereTargetType::DirectionalLight;
			State.LightComponent = DirLight;
			State.OriginalIntensity = DirLight->Intensity;
			State.OriginalPrimaryColor = DirLight->GetLightColor();
			CachedWorldSkyAndLights.Add(State);
		}
	}

	for (TObjectIterator<USkyLightComponent> It; It; ++It)
	{
		USkyLightComponent* SkyLight = *It;
		if (SkyLight && SkyLight->GetWorld() == World && SkyLight->IsRegistered() && SkyLight->Mobility != EComponentMobility::Static)
		{
			FCachedWorldSkyAndLightState State;
			State.TargetType = ECachedAtmosphereTargetType::SkyLight;
			State.SkyLightComponent = SkyLight;
			State.OriginalIntensity = SkyLight->Intensity;
			State.OriginalPrimaryColor = SkyLight->GetLightColor();
			CachedWorldSkyAndLights.Add(State);
		}
	}

	for (TObjectIterator<USkyAtmosphereComponent> It; It; ++It)
	{
		USkyAtmosphereComponent* SkyAtmo = *It;
		if (SkyAtmo && SkyAtmo->GetWorld() == World && SkyAtmo->IsRegistered())
		{
			FCachedWorldSkyAndLightState State;
			State.TargetType = ECachedAtmosphereTargetType::SkyAtmosphere;
			State.SkyAtmosphereComponent = SkyAtmo;
			State.OriginalPrimaryColor = SkyAtmo->SkyLuminanceFactor;
			CachedWorldSkyAndLights.Add(State);
		}
	}

	for (TObjectIterator<UExponentialHeightFogComponent> It; It; ++It)
	{
		UExponentialHeightFogComponent* FogComp = *It;
		if (FogComp && FogComp->GetWorld() == World && FogComp->IsRegistered())
		{
			FCachedWorldSkyAndLightState State;
			State.TargetType = ECachedAtmosphereTargetType::ExponentialHeightFog;
			State.ExponentialFogComponent = FogComp;
			State.OriginalPrimaryColor = FogComp->FogInscatteringLuminance;
			State.OriginalSecondaryColor = FogComp->DirectionalInscatteringLuminance;
			CachedWorldSkyAndLights.Add(State);
		}
	}
}

void UProximityFloatSpinComponent::ApplyWorldSkyDarkness(float Alpha)
{
	const float ClampedAlpha = FMath::Clamp(Alpha, 0.0f, 1.0f);
	FLinearColor TargetLightTint(0.22f, 0.24f, 0.58f);
	FLinearColor TargetSkyTint(0.50f, 0.60f, 1.0f);
	float EnvIntensityBoost = 1.0f;

	if (const FDiscoShockwaveStage* ActiveStage = FindDiscoShockwaveStageByMode(CurrentDiscoColorMode))
	{
		TargetLightTint = ActiveStage->EnvLightTint;
		TargetSkyTint = ActiveStage->EnvSkyTint;
		EnvIntensityBoost = 3.2f;
	}

	const float EffectiveWorldLightMult = FMath::Clamp(SkyDarkenTargetWorldLightMultiplier * EnvIntensityBoost, 0.0f, 0.35f);
	const float EffectiveAtmoMult = FMath::Clamp(SkyDarkenTargetAtmosphereMultiplier * EnvIntensityBoost, 0.0f, 0.12f);
	const float EffectiveFogMult = FMath::Clamp(SkyDarkenTargetFogMultiplier * EnvIntensityBoost, 0.0f, 0.16f);

	for (const FCachedWorldSkyAndLightState& Cached : CachedWorldSkyAndLights)
	{
		switch (Cached.TargetType)
		{
		case ECachedAtmosphereTargetType::DirectionalLight:
			if (ULightComponent* LightComp = Cached.LightComponent.Get())
			{
				LightComp->SetIntensity(FMath::Lerp(Cached.OriginalIntensity, Cached.OriginalIntensity * EffectiveWorldLightMult, ClampedAlpha));
				LightComp->SetLightColor(FMath::Lerp(Cached.OriginalPrimaryColor, TargetLightTint, ClampedAlpha));
			}
			break;

		case ECachedAtmosphereTargetType::SkyLight:
			if (USkyLightComponent* SkyComp = Cached.SkyLightComponent.Get())
			{
				SkyComp->SetIntensity(FMath::Lerp(Cached.OriginalIntensity, Cached.OriginalIntensity * EffectiveWorldLightMult, ClampedAlpha));
				SkyComp->SetLightColor(FMath::Lerp(Cached.OriginalPrimaryColor, TargetLightTint, ClampedAlpha));
			}
			break;

		case ECachedAtmosphereTargetType::SkyAtmosphere:
			if (USkyAtmosphereComponent* SkyAtmo = Cached.SkyAtmosphereComponent.Get())
			{
				const FLinearColor TargetSkyLuminance = Cached.OriginalPrimaryColor * (TargetSkyTint * EffectiveAtmoMult);
				SkyAtmo->SetSkyLuminanceFactor(FMath::Lerp(Cached.OriginalPrimaryColor, TargetSkyLuminance, ClampedAlpha));
			}
			break;

		case ECachedAtmosphereTargetType::ExponentialHeightFog:
			if (UExponentialHeightFogComponent* FogComp = Cached.ExponentialFogComponent.Get())
			{
				const FLinearColor TargetFogColor = Cached.OriginalPrimaryColor * (TargetSkyTint * EffectiveFogMult);
				const FLinearColor TargetDirColor = Cached.OriginalSecondaryColor * (TargetSkyTint * EffectiveFogMult);
				FogComp->SetFogInscatteringColor(FMath::Lerp(Cached.OriginalPrimaryColor, TargetFogColor, ClampedAlpha));
				FogComp->SetDirectionalInscatteringColor(FMath::Lerp(Cached.OriginalSecondaryColor, TargetDirColor, ClampedAlpha));
			}
			break;
		}
	}
}

void UProximityFloatSpinComponent::RestoreWorldSkyAndLights()
{
	for (const FCachedWorldSkyAndLightState& Cached : CachedWorldSkyAndLights)
	{
		switch (Cached.TargetType)
		{
		case ECachedAtmosphereTargetType::DirectionalLight:
			if (ULightComponent* LightComp = Cached.LightComponent.Get())
			{
				LightComp->SetIntensity(Cached.OriginalIntensity);
				LightComp->SetLightColor(Cached.OriginalPrimaryColor);
			}
			break;

		case ECachedAtmosphereTargetType::SkyLight:
			if (USkyLightComponent* SkyComp = Cached.SkyLightComponent.Get())
			{
				SkyComp->SetIntensity(Cached.OriginalIntensity);
				SkyComp->SetLightColor(Cached.OriginalPrimaryColor);
			}
			break;

		case ECachedAtmosphereTargetType::SkyAtmosphere:
			if (USkyAtmosphereComponent* SkyAtmo = Cached.SkyAtmosphereComponent.Get())
			{
				SkyAtmo->SetSkyLuminanceFactor(Cached.OriginalPrimaryColor);
			}
			break;

		case ECachedAtmosphereTargetType::ExponentialHeightFog:
			if (UExponentialHeightFogComponent* FogComp = Cached.ExponentialFogComponent.Get())
			{
				FogComp->SetFogInscatteringColor(Cached.OriginalPrimaryColor);
				FogComp->SetDirectionalInscatteringColor(Cached.OriginalSecondaryColor);
			}
			break;
		}
	}

	CachedWorldSkyAndLights.Reset();
	bWorldSkyAndLightsCached = false;
	CurrentSkyDarknessAlpha = 0.0f;
}

void UProximityFloatSpinComponent::SetDiscoEnvironmentActive(AActor* Owner, bool bActivate)
{
	if (bIsDiscoActive == bActivate)
	{
		return;
	}

	if (!bActivate)
	{
		bIsDiscoActive = false;
		CurrentDiscoColorMode = 0;
		if (!bShockwaveVisualActive)
		{
			HideCatShockwaveVisuals();
		}

		for (USpotLightComponent* SpotComp : DiscoSpotlights)
		{
			if (SpotComp)
			{
				SpotComp->DestroyComponent();
			}
		}
		DiscoSpotlights.Reset();

		if (DiscoFillPointLight)
		{
			DiscoFillPointLight->DestroyComponent();
			DiscoFillPointLight = nullptr;
		}

		if (DiscoPostProcessComponent)
		{
			DiscoPostProcessComponent->bEnabled = false;
			DiscoPostProcessComponent->DestroyComponent();
			DiscoPostProcessComponent = nullptr;
		}

		if (DiscoRigPivot)
		{
			DiscoRigPivot->DestroyComponent();
			DiscoRigPivot = nullptr;
		}

		if (RuntimeSkeletalMeshComponent && !bShockwaveVisualActive && !bHasDespawnedCat)
		{
			RuntimeSkeletalMeshComponent->SetCastShadow(true);
		}

		return;
	}

	UWorld* World = GetWorld();
	if (!Owner || !Owner->GetRootComponent() || !World)
	{
		return;
	}

	bIsDiscoActive = true;
	DiscoElapsedTime = 0.0f;
	DiscoOrbitYaw = 0.0f;

	if (RuntimeSkeletalMeshComponent)
	{
		RuntimeSkeletalMeshComponent->SetCastShadow(false);
	}

	// Center the sweeping disco rig on the floating cat so beams radiate outward around it
	DiscoRigPivot = NewObject<USceneComponent>(Owner);
	if (!DiscoRigPivot)
	{
		SetDiscoEnvironmentActive(Owner, false);
		return;
	}

	const FVector RigWorldLocation = BaseLocation + FVector(0.0f, 0.0f, CurrentHoverHeight + DiscoRigHeightAboveFloat);
	DiscoRigPivot->SetMobility(EComponentMobility::Movable);
	DiscoRigPivot->SetUsingAbsoluteLocation(true);
	DiscoRigPivot->SetUsingAbsoluteRotation(true);
	DiscoRigPivot->SetWorldLocationAndRotation(RigWorldLocation, FRotator::ZeroRotator);
	DiscoRigPivot->AttachToComponent(Owner->GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);
	DiscoRigPivot->RegisterComponent();

	const float InitialBrightnessMultiplier = ComputeDiscoBrightnessMultiplier(ElapsedTriggeredTime);
	const int32 LightCount = FMath::Max(1, DiscoSpotlightCount);
	for (int32 Index = 0; Index < LightCount; ++Index)
	{
		if (USpotLightComponent* SpotComp = NewObject<USpotLightComponent>(Owner))
		{
			const float NormalizedIndex = static_cast<float>(Index) / static_cast<float>(LightCount);
			const float AngleRad = NormalizedIndex * 2.0f * PI;
			SpotComp->SetMobility(EComponentMobility::Movable);
			SpotComp->SetCastShadows(true);
			SpotComp->SetIntensityUnits(ELightUnits::Candelas);
			SpotComp->SetInnerConeAngle(14.0f);
			SpotComp->SetOuterConeAngle(32.0f);
			SpotComp->SetAttenuationRadius(DiscoLightAttenuationRadius);
			SpotComp->SetIntensity(DiscoSpotlightIntensity * InitialBrightnessMultiplier);
			SpotComp->AttachToComponent(DiscoRigPivot, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
			SpotComp->SetRelativeLocation(FVector(FMath::Cos(AngleRad) * 28.0f, FMath::Sin(AngleRad) * 28.0f, 0.0f));
			SpotComp->RegisterComponent();
			DiscoSpotlights.Add(SpotComp);
		}
	}

	if (UPointLightComponent* FillComp = NewObject<UPointLightComponent>(Owner))
	{
		FillComp->SetMobility(EComponentMobility::Movable);
		FillComp->SetCastShadows(true);
		FillComp->SetIntensityUnits(ELightUnits::Candelas);
		FillComp->SetAttenuationRadius(DiscoLightAttenuationRadius * 1.15f);
		FillComp->SetIntensity(DiscoSpotlightIntensity * InitialBrightnessMultiplier * DiscoFillIntensityMultiplier);
		FillComp->SetRelativeLocation(FVector::ZeroVector);
		FillComp->AttachToComponent(DiscoRigPivot, FAttachmentTransformRules::KeepRelativeTransform);
		FillComp->RegisterComponent();
		DiscoFillPointLight = FillComp;
	}

	if (UPostProcessComponent* PPComp = NewObject<UPostProcessComponent>(Owner))
	{
		PPComp->bUnbound = true;
		PPComp->bEnabled = true;
		PPComp->Priority = 100.0f;
		PPComp->BlendWeight = 1.0f;
		PPComp->Settings.bOverride_BloomIntensity = true;
		PPComp->Settings.bOverride_SceneFringeIntensity = true;
		PPComp->Settings.bOverride_VignetteIntensity = true;
		PPComp->Settings.bOverride_ColorSaturation = true;
		PPComp->Settings.bOverride_SceneColorTint = true;
		PPComp->Settings.SceneColorTint = FLinearColor::White;
		PPComp->AttachToComponent(DiscoRigPivot, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
		PPComp->RegisterComponent();
		DiscoPostProcessComponent = PPComp;
	}

	InitializeShockwaveVisuals(Owner);
}

void UProximityFloatSpinComponent::UpdateDiscoEnvironment(AActor* Owner, bool bShouldBeActive, float DeltaTime)
{
	SetDiscoEnvironmentActive(Owner, bShouldBeActive);

	if (!bIsDiscoActive || !DiscoRigPivot)
	{
		return;
	}

	DiscoElapsedTime += DeltaTime;
	DiscoOrbitYaw = FMath::Fmod(DiscoOrbitYaw + DiscoOrbitSpeed * DeltaTime, 360.0f);

	const FVector RigWorldLocation = BaseLocation + FVector(0.0f, 0.0f, CurrentHoverHeight + DiscoRigHeightAboveFloat);
	DiscoRigPivot->SetWorldLocationAndRotation(
		RigWorldLocation,
		FRotator(0.0f, DiscoOrbitYaw, 0.0f));

	const float ShockwavePulseAlpha = LastShockwavePostProcessAlpha;

	const float BrightnessMultiplier = ComputeDiscoBrightnessMultiplier(ElapsedTriggeredTime);
	const float BeatSine = 0.5f + 0.5f * FMath::Sin(DiscoElapsedTime * 2.0f * PI * DiscoPulseFrequency);
	const float BeatPulse = FMath::Square(BeatSine);

	auto ComputeModeHue = [this](float NormalizedIndex, float SpeedScale, float DefaultHue360) -> float
	{
		if (const FDiscoShockwaveStage* ActiveStage = FindDiscoShockwaveStageByMode(CurrentDiscoColorMode))
		{
			const float Phase = 0.5f + 0.5f * FMath::Sin(DiscoElapsedTime * DiscoColorCycleSpeed * SpeedScale + NormalizedIndex * 2.0f * PI);
			return FMath::Fmod(FMath::Lerp(ActiveStage->HueMinDeg, ActiveStage->HueMaxDeg, Phase), 360.0f);
		}
		return DefaultHue360;
	};

	const int32 LightCount = FMath::Max(1, DiscoSpotlights.Num());
	for (int32 Index = 0; Index < DiscoSpotlights.Num(); ++Index)
	{
		if (USpotLightComponent* SpotComp = DiscoSpotlights[Index])
		{
			const float NormalizedIndex = static_cast<float>(Index) / static_cast<float>(LightCount);
			const float DefaultHue = FMath::Fmod((DiscoElapsedTime * DiscoColorCycleSpeed + NormalizedIndex) * 360.0f, 360.0f);
			const float Hue360 = ComputeModeHue(NormalizedIndex, 6.0f, DefaultHue);
			const FLinearColor SpotColor = FLinearColor(Hue360, 0.95f, 1.0f).HSVToLinearRGB();

			const float AngleDeg = NormalizedIndex * 360.0f;
			const float AngleRad = NormalizedIndex * 2.0f * PI;
			const float SweepPitch = -12.0f + 22.0f * FMath::Sin(DiscoElapsedTime * 4.2f + Index * 1.15f);

			SpotComp->SetRelativeLocationAndRotation(
				FVector(FMath::Cos(AngleRad) * 28.0f, FMath::Sin(AngleRad) * 28.0f, 0.0f),
				FRotator(SweepPitch, AngleDeg, 0.0f));
			SpotComp->SetLightColor(SpotColor);
			SpotComp->SetIntensity(DiscoSpotlightIntensity * BrightnessMultiplier * (0.65f + 0.70f * BeatPulse));
		}
	}

	if (DiscoFillPointLight)
	{
		const float DefaultFillHue = FMath::Fmod(DiscoElapsedTime * DiscoColorCycleSpeed * 180.0f, 360.0f);
		const float FillHue = ComputeModeHue(0.0f, 4.0f, DefaultFillHue);
		DiscoFillPointLight->SetLightColor(FLinearColor(FillHue, 0.90f, 1.0f).HSVToLinearRGB());
		DiscoFillPointLight->SetIntensity(DiscoSpotlightIntensity * BrightnessMultiplier * DiscoFillIntensityMultiplier * (0.75f + 0.50f * BeatPulse));
	}

	if (DiscoPostProcessComponent)
	{
		const float PPBrightnessScale = FMath::Clamp(0.45f + 0.30f * BrightnessMultiplier, 0.45f, 1.45f);
		DiscoPostProcessComponent->Settings.BloomIntensity = FMath::Lerp(0.9f, 1.7f, BeatPulse) * PPBrightnessScale + ShockwavePulseAlpha * 2.8f;
		DiscoPostProcessComponent->Settings.SceneFringeIntensity = FMath::Lerp(0.4f, 1.5f, BeatPulse) * PPBrightnessScale + ShockwavePulseAlpha * 4.8f;
		DiscoPostProcessComponent->Settings.VignetteIntensity = FMath::Clamp(0.55f + ShockwavePulseAlpha * 0.35f, 0.0f, 1.0f);
		DiscoPostProcessComponent->Settings.ColorSaturation = (CurrentDiscoColorMode == 0 && BrightnessMultiplier < 0.5f)
			? FVector4(1.05f, 1.05f, 1.05f, 1.0f)
			: FVector4(1.25f, 1.25f, 1.25f, 1.0f);

		const FDiscoShockwaveStage* ActiveStage = FindDiscoShockwaveStageByMode(CurrentDiscoColorMode);
		const FLinearColor EnvTint = ActiveStage ? ActiveStage->PostProcessTint : FLinearColor::White;
		DiscoPostProcessComponent->Settings.SceneColorTint = FMath::Lerp(EnvTint, ActiveShockwaveColor * 1.15f + FLinearColor(0.25f, 0.25f, 0.25f), ShockwavePulseAlpha * 0.45f);
	}
}

void UProximityFloatSpinComponent::ResetTimelineEffects()
{
	NextShockwaveStageIndex = 0;
	CurrentDiscoColorMode = 0;
	bTriggeredPopOutEffect = false;
	bIsPopOutActive = false;
	bHasDespawnedCat = false;
	PopOutElapsed = 0.0f;
	LastShockwavePostProcessAlpha = 0.0f;
	bShockwaveVisualActive = false;
	ShockwaveElapsed = 0.0f;
	ShockwaveOrigin = BaseLocation;
	ActiveShockwaveColor = FLinearColor::White;
	UnstableMatterIntensity = 0.0f;
	UnstableScaleMultiplier = FVector::OneVector;
	HideCatShockwaveVisuals();
}

void UProximityFloatSpinComponent::EvaluateTimelineKeyframes(AActor* Owner, bool bDiscoShouldRun)
{
	if (bIsActive && !bTriggeredPopOutEffect && ElapsedTriggeredTime >= 42.89f)
	{
		bTriggeredPopOutEffect = true;
		bIsPopOutActive = true;
		PopOutElapsed = 0.0f;
		CurrentDiscoColorMode = 0;
		SetDiscoEnvironmentActive(Owner, false);
		RestoreWorldSkyAndLights();
		TriggerCatShockwave(Owner, FLinearColor(0.85f, 0.96f, 1.0f));
	}

	if (!bDiscoShouldRun)
	{
		CurrentDiscoColorMode = 0;
		return;
	}

	const TArray<FDiscoShockwaveStage>& Stages = GetDiscoShockwaveStages();
	const FDiscoShockwaveStage* NewlyTriggeredStage = nullptr;
	while (NextShockwaveStageIndex < Stages.Num() && ElapsedTriggeredTime >= Stages[NextShockwaveStageIndex].TriggerTime)
	{
		NewlyTriggeredStage = &Stages[NextShockwaveStageIndex];
		++NextShockwaveStageIndex;
	}

	if (NewlyTriggeredStage)
	{
		CurrentDiscoColorMode = NewlyTriggeredStage->ColorMode;
		TriggerCatShockwave(Owner, NewlyTriggeredStage->ShockwaveColor, NewlyTriggeredStage->RadiusScale);
	}

	if (ElapsedTriggeredTime >= 18.86f)
	{
		// Restore default darkened night environment color & normal rainbow disco palette
		CurrentDiscoColorMode = 0;
	}
}

float UProximityFloatSpinComponent::ComputeDiscoBrightnessMultiplier(float TimeSeconds) const
{
	// Relative to the 1.25% (312.5 cd) base (half of previous 625 cd): 0.2x = 62.5 cd, 1.0x = 312.5 cd, 1.5x = 468.75 cd, 2.0x = 625 cd, 3.0x = 937.5 cd
	if (TimeSeconds >= 31.66f)
	{
		return 3.0f;
	}
	if (TimeSeconds >= 31.26f)
	{
		return 1.5f;
	}
	if (TimeSeconds >= 30.06f)
	{
		return 0.2f;
	}
	if (TimeSeconds >= 18.86f)
	{
		return 2.0f;
	}
	return 1.0f;
}

float UProximityFloatSpinComponent::ComputeUnstableMatterIntensity(float TimeSeconds, bool bIsSpinning) const
{
	if (!bIsActive || !bIsSpinning || TimeSeconds < 17.26f || TimeSeconds >= 42.89f)
	{
		return 0.0f;
	}

	if (TimeSeconds < 18.86f)
	{
		// 17.26s -> 18.86s: slowly ramp up the unstable matter movement effect
		return FMath::SmoothStep(17.26f, 18.86f, TimeSeconds) * 0.55f;
	}

	if (TimeSeconds < 30.06f)
	{
		// 18.86s -> 30.06s: slight unstable matter movement effect
		return 0.25f;
	}

	if (TimeSeconds < 31.66f)
	{
		// 30.06s -> 31.66s: unstable matter effect stopped
		return 0.0f;
	}

	// 31.66s -> 42.89s: fully crazy unstable matter effect
	return 1.0f;
}

void UProximityFloatSpinComponent::ComputeUnstableMatterOffsets(float Intensity, float TimeSeconds, FVector& OutPositionOffset, FRotator& OutRotationOffset, FVector& OutScaleMultiplier) const
{
	if (Intensity <= KINDA_SMALL_NUMBER)
	{
		OutPositionOffset = FVector::ZeroVector;
		OutRotationOffset = FRotator::ZeroRotator;
		OutScaleMultiplier = FVector::OneVector;
		return;
	}

	// Blend smoothly between slight shimmer (0.25) and fully crazy matter destabilization (1.0)
	const float RampIn = FMath::Clamp(Intensity / 0.25f, 0.0f, 1.0f);
	const float CrazyBlend = FMath::Clamp((Intensity - 0.25f) / 0.75f, 0.0f, 1.0f);

	const float PosAmplitude = FMath::Lerp(6.0f, 42.0f, CrazyBlend) * RampIn;
	const float ScaleWarpAmp = FMath::Lerp(0.11f, 0.52f, CrazyBlend) * RampIn;

	// High-frequency quantized glitch steps mixed with irrational-ratio sine oscillation
	const float GlitchRate = FMath::Lerp(16.0f, 36.0f, CrazyBlend);
	const float GlitchStep = FMath::FloorToFloat(TimeSeconds * GlitchRate);
	auto StepHash = [GlitchStep](float SeedOffset) -> float
	{
		return FMath::Sin(GlitchStep * 12.9898f + SeedOffset * 78.233f);
	};

	const float SnapWeight = FMath::Lerp(0.22f, 0.58f, CrazyBlend);
	const float ContWeight = 1.0f - SnapWeight;

	const float OffsetX = (ContWeight * FMath::Sin(TimeSeconds * 37.3f + 0.7f * FMath::Sin(TimeSeconds * 19.1f)) + SnapWeight * StepHash(1.1f)) * PosAmplitude;
	const float OffsetY = (ContWeight * FMath::Sin(TimeSeconds * 43.7f + 0.8f * FMath::Cos(TimeSeconds * 23.3f)) + SnapWeight * StepHash(2.7f)) * PosAmplitude;
	const float OffsetZ = (ContWeight * FMath::Sin(TimeSeconds * 53.1f + 0.6f * FMath::Sin(TimeSeconds * 29.9f)) + SnapWeight * StepHash(4.3f)) * (PosAmplitude * 0.65f);
	OutPositionOffset = FVector(OffsetX, OffsetY, OffsetZ);

	OutRotationOffset = FRotator::ZeroRotator;

	const float ScaleWaveX = ContWeight * FMath::Sin(TimeSeconds * 31.7f) + SnapWeight * StepHash(13.7f);
	const float ScaleWaveY = ContWeight * FMath::Sin(TimeSeconds * 47.9f + 2.1f) + SnapWeight * StepHash(17.5f);
	const float ScaleWaveZ = ContWeight * FMath::Cos(TimeSeconds * 67.3f + 4.2f) + SnapWeight * StepHash(21.9f);

	OutScaleMultiplier = FVector(
		FMath::Clamp(1.0f + ScaleWaveX * ScaleWarpAmp, 0.42f, 1.65f),
		FMath::Clamp(1.0f + ScaleWaveY * ScaleWarpAmp, 0.42f, 1.65f),
		FMath::Clamp(1.0f + ScaleWaveZ * ScaleWarpAmp, 0.42f, 1.65f));
}

FVector UProximityFloatSpinComponent::UpdatePopOutScale(float DeltaTime)
{
	if (bHasDespawnedCat)
	{
		return FVector(0.01f, 0.01f, 0.01f);
	}

	if (!bIsPopOutActive)
	{
		return FVector::OneVector;
	}

	PopOutElapsed += DeltaTime;
	if (PopOutDuration <= 0.0f || PopOutElapsed >= PopOutDuration)
	{
		bIsPopOutActive = false;
		bHasDespawnedCat = true;
		PopOutElapsed = 0.0f;
		return FVector(0.01f, 0.01f, 0.01f);
	}

	const float Alpha = FMath::Clamp(PopOutElapsed / PopOutDuration, 0.0f, 1.0f);
	float PopScale = 1.0f;
	if (Alpha < 0.45f)
	{
		const float ExpandAlpha = FMath::Clamp(Alpha / 0.45f, 0.0f, 1.0f);
		PopScale = FMath::Lerp(1.0f, 1.38f, FMath::Sin(ExpandAlpha * PI * 0.5f));
	}
	else
	{
		const float ImplodeAlpha = FMath::Clamp((Alpha - 0.45f) / 0.55f, 0.0f, 1.0f);
		PopScale = FMath::Lerp(1.38f, 0.01f, ImplodeAlpha * ImplodeAlpha);
	}
	return FVector(PopScale, PopScale, PopScale);
}

void UProximityFloatSpinComponent::InitializeShockwaveVisuals(AActor* Owner)
{
	if (!Owner || !Owner->GetRootComponent() || !ShockwaveRingSegments.IsEmpty())
	{
		return;
	}

	UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UMaterialInterface* BaseMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineMaterials/EmissiveMeshMaterial.EmissiveMeshMaterial"));
	if (!BaseMaterial)
	{
		BaseMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	}

	if (CubeMesh && BaseMaterial)
	{
		ShockwaveRingMaterial = UMaterialInstanceDynamic::Create(BaseMaterial, this);
		if (ShockwaveRingMaterial)
		{
			ShockwaveRingMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor::Black);
		}

		static constexpr int32 NumRingSegments = 24;
		ShockwaveRingSegments.Reserve(NumRingSegments);
		for (int32 Index = 0; Index < NumRingSegments; ++Index)
		{
			if (UStaticMeshComponent* SegComp = NewObject<UStaticMeshComponent>(Owner))
			{
				SegComp->SetStaticMesh(CubeMesh);
				SegComp->SetMobility(EComponentMobility::Movable);
				SegComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				SegComp->SetCastShadow(false);
				SegComp->SetGenerateOverlapEvents(false);
				SegComp->SetUsingAbsoluteLocation(true);
				SegComp->SetUsingAbsoluteRotation(true);
				SegComp->SetUsingAbsoluteScale(true);
				if (ShockwaveRingMaterial)
				{
					SegComp->SetMaterial(0, ShockwaveRingMaterial);
				}
				SegComp->SetVisibility(false);
				SegComp->SetHiddenInGame(true);
				SegComp->AttachToComponent(Owner->GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);
				SegComp->RegisterComponent();
				ShockwaveRingSegments.Add(SegComp);
			}
		}
	}

	if (!ShockwaveFlashPointLight)
	{
		if (UPointLightComponent* FlashLight = NewObject<UPointLightComponent>(Owner))
		{
			FlashLight->SetMobility(EComponentMobility::Movable);
			FlashLight->SetCastShadows(true);
			FlashLight->SetIntensityUnits(ELightUnits::Candelas);
			FlashLight->SetAttenuationRadius(1200.0f);
			FlashLight->SetIntensity(0.0f);
			FlashLight->SetUsingAbsoluteLocation(true);
			FlashLight->SetVisibility(false);
			FlashLight->SetHiddenInGame(true);
			FlashLight->AttachToComponent(Owner->GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);
			FlashLight->RegisterComponent();
			ShockwaveFlashPointLight = FlashLight;
		}
	}
}

void UProximityFloatSpinComponent::TriggerCatShockwave(AActor* Owner, const FLinearColor& WaveColor, float RadiusScale)
{
	if (!Owner)
	{
		return;
	}

	if (ShockwaveRingSegments.IsEmpty())
	{
		InitializeShockwaveVisuals(Owner);
	}

	ShockwaveOrigin = Owner->GetActorLocation();
	ActiveShockwaveColor = WaveColor;
	ActiveShockwaveRadiusScale = RadiusScale;
	ShockwaveElapsed = 0.0f;
	bShockwaveVisualActive = true;

	if (RuntimeSkeletalMeshComponent)
	{
		RuntimeSkeletalMeshComponent->SetCastShadow(false);
	}

	for (UStaticMeshComponent* SegComp : ShockwaveRingSegments)
	{
		if (SegComp)
		{
			SegComp->SetHiddenInGame(false);
			SegComp->SetVisibility(true);
		}
	}

	if (ShockwaveFlashPointLight)
	{
		ShockwaveFlashPointLight->SetWorldLocation(ShockwaveOrigin);
		ShockwaveFlashPointLight->SetLightColor(ActiveShockwaveColor);
		ShockwaveFlashPointLight->SetHiddenInGame(false);
		ShockwaveFlashPointLight->SetVisibility(true);
	}
}

void UProximityFloatSpinComponent::UpdateCatShockwave(float DeltaTime, float& OutPostProcessPulseAlpha)
{
	OutPostProcessPulseAlpha = 0.0f;
	if (!bShockwaveVisualActive)
	{
		return;
	}

	ShockwaveElapsed += DeltaTime;
	if (ShockwaveDuration <= 0.0f || ShockwaveElapsed >= ShockwaveDuration)
	{
		HideCatShockwaveVisuals();
		return;
	}

	const float WaveProgress = FMath::Clamp(ShockwaveElapsed / ShockwaveDuration, 0.0f, 1.0f);
	const float RadiusProgress = 1.0f - FMath::Pow(1.0f - WaveProgress, 2.6f);
	const float CurrentRadius = FMath::Lerp(35.0f, ShockwaveMaxRadius * ActiveShockwaveRadiusScale, RadiusProgress);
	const float IntensityFade = FMath::Pow(1.0f - WaveProgress, 1.5f);
	OutPostProcessPulseAlpha = FMath::Pow(1.0f - WaveProgress, 2.0f) * 0.0625f;

	if (ShockwaveRingMaterial)
	{
		ShockwaveRingMaterial->SetVectorParameterValue(TEXT("Color"), ActiveShockwaveColor * (1.125f * IntensityFade));
	}

	const int32 SegCount = ShockwaveRingSegments.Num();
	if (SegCount > 0)
	{
		const float ChordLength = (2.0f * PI * CurrentRadius / static_cast<float>(SegCount)) * 1.08f;
		const float RadialThickness = FMath::Lerp(16.0f, 44.0f, RadiusProgress) * FMath::Lerp(0.35f, 1.0f, IntensityFade);
		const float VerticalHeight = FMath::Lerp(14.0f, 26.0f, IntensityFade);
		const FVector SegmentScale(RadialThickness / 100.0f, ChordLength / 100.0f, VerticalHeight / 100.0f);

		for (int32 Index = 0; Index < SegCount; ++Index)
		{
			if (UStaticMeshComponent* SegComp = ShockwaveRingSegments[Index])
			{
				const float AngleRad = (2.0f * PI * static_cast<float>(Index)) / static_cast<float>(SegCount);
				const FVector SegmentLoc = ShockwaveOrigin + FVector(FMath::Cos(AngleRad) * CurrentRadius, FMath::Sin(AngleRad) * CurrentRadius, 0.0f);
				const FRotator SegmentRot(0.0f, FMath::RadiansToDegrees(AngleRad), 0.0f);
				SegComp->SetWorldLocationAndRotation(SegmentLoc, SegmentRot);
				SegComp->SetWorldScale3D(SegmentScale);
			}
		}
	}

	if (ShockwaveFlashPointLight)
	{
		ShockwaveFlashPointLight->SetWorldLocation(ShockwaveOrigin);
		ShockwaveFlashPointLight->SetLightColor(ActiveShockwaveColor);
		ShockwaveFlashPointLight->SetAttenuationRadius(FMath::Max(600.0f, CurrentRadius * 1.25f));
		ShockwaveFlashPointLight->SetIntensity(4062.5f * IntensityFade);
	}
}

void UProximityFloatSpinComponent::HideCatShockwaveVisuals()
{
	bShockwaveVisualActive = false;
	ShockwaveElapsed = 0.0f;

	if (RuntimeSkeletalMeshComponent && !bIsDiscoActive && !bHasDespawnedCat)
	{
		RuntimeSkeletalMeshComponent->SetCastShadow(true);
	}

	for (UStaticMeshComponent* SegComp : ShockwaveRingSegments)
	{
		if (SegComp)
		{
			SegComp->SetVisibility(false);
			SegComp->SetHiddenInGame(true);
		}
	}

	if (ShockwaveFlashPointLight)
	{
		ShockwaveFlashPointLight->SetIntensity(0.0f);
		ShockwaveFlashPointLight->SetVisibility(false);
		ShockwaveFlashPointLight->SetHiddenInGame(true);
	}
}

void UProximityFloatSpinComponent::DestroyCatShockwaveVisuals()
{
	bShockwaveVisualActive = false;
	ShockwaveElapsed = 0.0f;

	for (UStaticMeshComponent* SegComp : ShockwaveRingSegments)
	{
		if (SegComp)
		{
			SegComp->DestroyComponent();
		}
	}
	ShockwaveRingSegments.Reset();
	ShockwaveRingMaterial = nullptr;

	if (ShockwaveFlashPointLight)
	{
		ShockwaveFlashPointLight->DestroyComponent();
		ShockwaveFlashPointLight = nullptr;
	}
}

void UProximityFloatSpinComponent::SpawnDroppedItem(AActor* Owner, AActor* Interactor)
{
	UWorld* World = GetWorld();
	if (!World || !Owner)
	{
		return;
	}

	UClass* SpawnClass = DropItemClass ? DropItemClass.Get() : APickupItemActor::StaticClass();

	FVector TossDirection = Interactor
		? (Interactor->GetActorLocation() - Owner->GetActorLocation()).GetSafeNormal2D()
		: Owner->GetActorForwardVector().GetSafeNormal2D();
	if (TossDirection.IsNearlyZero())
	{
		TossDirection = Owner->GetActorForwardVector().GetSafeNormal2D();
	}
	if (TossDirection.IsNearlyZero())
	{
		TossDirection = FVector::ForwardVector;
	}

	const FVector CatCenter = Owner->GetActorLocation() + FVector(0.0f, 0.0f, 55.0f);
	float ForwardOffset = 42.0f;
	FCollisionQueryParams WallQueryParams(SCENE_QUERY_STAT(CatDropTrace), false, Owner);
	if (Interactor)
	{
		WallQueryParams.AddIgnoredActor(Interactor);
	}
	FHitResult WallHit;
	if (World->LineTraceSingleByChannel(WallHit, CatCenter, CatCenter + TossDirection * 60.0f, ECC_Visibility, WallQueryParams))
	{
		ForwardOffset = FMath::Max(12.0f, WallHit.Distance - 20.0f);
	}

	const FVector SpawnOrigin = CatCenter + TossDirection * ForwardOffset;

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = Owner;
	SpawnParams.Instigator = Cast<APawn>(Interactor);
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	if (APickupItemActor* DroppedItem = World->SpawnActor<APickupItemActor>(SpawnClass, SpawnOrigin, FRotator::ZeroRotator, SpawnParams))
	{
		bHasDroppedItem = true;
		DroppedItem->ConfigureAsFairyTaleKittyKey();
		const FVector LaunchVelocity = TossDirection * 180.0f + FVector(0.0f, 0.0f, 220.0f);
		DroppedItem->DropWithPhysics(SpawnOrigin, LaunchVelocity, { Owner });
		OnCatKeyDropped.Broadcast(DroppedItem);

		if (ALostWithinCharacter* Char = Cast<ALostWithinCharacter>(Interactor))
		{
			Char->ShowStatusMessage(NSLOCTEXT("LostWithin", "CatDroppedKeyStatusMessage", "The cat dropped the Fairy Tale Kitty Key!"));
		}
	}
}

void UProximityFloatSpinComponent::OnInteract_Implementation(AActor* Interactor)
{
	AActor* Owner = GetOwner();
	if (!Owner || bIsActive || bHasTriggered)
	{
		return;
	}

	++InteractionCount;
	TriggerInteractionSqueeze();
	PlayRandomInteractionSound(Owner);
	OnCatInteracted.Broadcast(InteractionCount);
	TryTriggerDirectedPath(EDirectedPathTriggerMode::OnFirstInteract);

	if (InteractionCount >= ItemDropInteractionThreshold && !bHasDroppedItem)
	{
		SpawnDroppedItem(Owner, Interactor);
		if (bHasDroppedItem)
		{
			TryTriggerDirectedPath(EDirectedPathTriggerMode::OnItemDropped);
		}
	}

	if (!bTriggerSpinByInteractionCount)
	{
		return;
	}

	if (bRequirePetSpinSequence)
	{
		// Each pet opens a round that a full spin closes. Petting again before spinning starts the ritual over,
		// with this pet as its first.
		if (bAwaitingPlayerSpin)
		{
			CompletedPetSpinRounds = 0;
		}
		bAwaitingPlayerSpin = true;
		LastPetInteractor = Interactor;
		ResetPlayerSpinTracking();
		return;
	}

	if (InteractionCount >= SpinTriggerInteractionThreshold && !bIsActive)
	{
		StartChaosFromPetting(Owner, Interactor);
	}
}

void UProximityFloatSpinComponent::ResetPlayerSpinTracking()
{
	AccumulatedPlayerSpinYaw = 0.0f;
	LastPlayerSpinYaw = 0.0f;
	bHasLastPlayerSpinYaw = false;
}

bool UProximityFloatSpinComponent::UpdatePlayerSpin(const FVector& PlayerLocation, float PlayerViewYaw)
{
	AActor* Owner = GetOwner();
	if (!Owner || !bAwaitingPlayerSpin || bIsActive || bHasTriggered)
	{
		ResetPlayerSpinTracking();
		return false;
	}

	// Turning only counts near the cat; a partial turn made farther away is dropped.
	if (FVector::DistSquared2D(PlayerLocation, Owner->GetActorLocation()) > FMath::Square(static_cast<double>(PlayerSpinRadius)))
	{
		ResetPlayerSpinTracking();
		return false;
	}

	if (!bHasLastPlayerSpinYaw)
	{
		LastPlayerSpinYaw = PlayerViewYaw;
		bHasLastPlayerSpinYaw = true;
		return false;
	}

	// Signed and unwrapped, so turning back cancels and a full circle in one direction adds up to 360 degrees.
	AccumulatedPlayerSpinYaw += FRotator::NormalizeAxis(PlayerViewYaw - LastPlayerSpinYaw);
	LastPlayerSpinYaw = PlayerViewYaw;
	if (FMath::Abs(AccumulatedPlayerSpinYaw) < PlayerSpinDegrees)
	{
		return false;
	}

	bAwaitingPlayerSpin = false;
	ResetPlayerSpinTracking();
	++CompletedPetSpinRounds;
	if (CompletedPetSpinRounds < PetSpinRounds)
	{
		return false;
	}

	CompletedPetSpinRounds = 0;
	StartChaosFromPetting(Owner, LastPetInteractor.Get());
	return bIsActive;
}

void UProximityFloatSpinComponent::StartChaosFromPetting(AActor* Owner, AActor* Interactor)
{
	if (!Owner || bIsActive)
	{
		return;
	}

	// Chaos mode stops petting, so a key the cat still holds must drop now or it could never be obtained.
	if (!bHasDroppedItem)
	{
		SpawnDroppedItem(Owner, Interactor);
		if (bHasDroppedItem)
		{
			TryTriggerDirectedPath(EDirectedPathTriggerMode::OnItemDropped);
		}
	}

	TriggerSpinSequence(Owner);
	if (ALostWithinCharacter* Char = Cast<ALostWithinCharacter>(Interactor))
	{
		Char->ShowStatusMessage(NSLOCTEXT("LostWithin", "CatSanctuaryStatusMessage", "Oiiai Chaos Sanctuary Activated!"));
	}
}

FText UProximityFloatSpinComponent::GetInteractionPrompt_Implementation() const
{
	if (IsChaosSanctuaryActive())
	{
		return NSLOCTEXT("LostWithin", "CatSanctuaryActivePrompt", "Chaos Sanctuary Active");
	}

	if (!bHasDroppedItem && ItemDropInteractionThreshold > 1)
	{
		return FText::Format(
			NSLOCTEXT("LostWithin", "CatPetKeyProgressPrompt", "[E] Pet Cat ({0}/{1})"),
			FText::AsNumber(InteractionCount),
			FText::AsNumber(ItemDropInteractionThreshold));
	}

	// Chaos progress is deliberately not shown.
	return NSLOCTEXT("LostWithin", "CatInteractionPrompt", "[E] Pet Cat");
}

bool UProximityFloatSpinComponent::IsPlayerInChaosSanctuary(const APawn* PlayerPawn) const
{
	if (!IsChaosSanctuaryActive() || !PlayerPawn)
	{
		return false;
	}

	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return false;
	}

	const double DistSq = FVector::DistSquared(Owner->GetActorLocation(), PlayerPawn->GetActorLocation());
	return DistSq <= FMath::Square(static_cast<double>(ChaosSanctuaryRadius));
}

void UProximityFloatSpinComponent::UpdateCatGlow(float Intensity, float ElapsedTime)
{
	if (CatMaterialInstances.IsEmpty())
	{
		UE_LOG(LogLostWithin, Warning, TEXT("UpdateCatGlow: No CatMaterialInstances."));
		return;
	}

	// Cyan-whitish star color
	// Only apply glow when unstable matter effect is active
	float FinalIntensity = Intensity;
	
	FLinearColor GlowColor = FLinearColor(0.8f, 1.0f, 1.0f) * FinalIntensity;
	
	for (UMaterialInstanceDynamic* MatInst : CatMaterialInstances)
	{
		if (MatInst)
		{
			// Try both variants (with and without space) to be safe
			MatInst->SetVectorParameterValue(TEXT("EmissiveColor"), GlowColor);
			MatInst->SetVectorParameterValue(TEXT("Emissive Color"), GlowColor);
			MatInst->SetVectorParameterValue(TEXT("EmissiveFactor"), GlowColor);
			MatInst->SetVectorParameterValue(TEXT("BaseColor"), GlowColor);
			MatInst->SetVectorParameterValue(TEXT("Base Color"), GlowColor);
			MatInst->SetScalarParameterValue(TEXT("EmissiveMultiplier"), FinalIntensity * 5.0f);
			MatInst->SetScalarParameterValue(TEXT("GlowMultiplier"), FinalIntensity * 5.0f);
		}
	}
}

bool UProximityFloatSpinComponent::CanInteract_Implementation(AActor* Interactor) const
{
	return !bIsActive && !bHasTriggered && !bHasDespawnedCat;
}

float UProximityFloatSpinComponent::GetActiveSpinSpeedMultiplier(float TimeSeconds, bool& bOutIsSpinning, int32& OutWindowIndex) const
{
	for (int32 Index = 0; Index < SpinTimeWindows.Num(); ++Index)
	{
		const FSpinTimeWindow& Window = SpinTimeWindows[Index];
		if (TimeSeconds >= Window.StartTime && TimeSeconds <= Window.EndTime)
		{
			bOutIsSpinning = true;
			OutWindowIndex = Index;
			return Window.SpinSpeedMultiplier;
		}
	}

	bOutIsSpinning = false;
	OutWindowIndex = INDEX_NONE;
	return 0.0f;
}

bool UProximityFloatSpinComponent::EvaluateShouldSpin(int32& OutActiveWindowIndex, float& OutActiveSpinMultiplier) const
{
	bool bShouldSpin = bIsActive;
	OutActiveWindowIndex = INDEX_NONE;
	OutActiveSpinMultiplier = 1.0f;

	if (bIsActive && bUseSpinTimeWindows)
	{
		OutActiveSpinMultiplier = GetActiveSpinSpeedMultiplier(ElapsedTriggeredTime, bShouldSpin, OutActiveWindowIndex);
	}

	if (bTriggeredPopOutEffect || (bIsActive && ElapsedTriggeredTime >= 42.89f))
	{
		bShouldSpin = false;
		OutActiveWindowIndex = INDEX_NONE;
	}

	return bShouldSpin;
}

bool UProximityFloatSpinComponent::IsSpinWindowActive(int32 WindowIndex) const
{
	if (!IsComponentTickEnabled() || !bUseSpinTimeWindows || !SpinTimeWindows.IsValidIndex(WindowIndex) || bHasDespawnedCat)
	{
		return false;
	}
	int32 ActiveWindowIndex = INDEX_NONE;
	float ActiveSpinMultiplier = 1.0f;
	return EvaluateShouldSpin(ActiveWindowIndex, ActiveSpinMultiplier) && ActiveWindowIndex == WindowIndex;
}

bool UProximityFloatSpinComponent::IsWithinSpinWindow(float TimeSeconds) const
{
	bool bIsSpinning = false;
	int32 UnusedWindowIndex = INDEX_NONE;
	GetActiveSpinSpeedMultiplier(TimeSeconds, bIsSpinning, UnusedWindowIndex);
	return bIsSpinning;
}

bool UProximityFloatSpinComponent::IsSpinMusicActive() const
{
	if (ActiveAudioComponent && ActiveAudioComponent->IsPlaying())
	{
		return true;
	}

	if (!bIsActive || bTriggeredPopOutEffect || bHasDespawnedCat)
	{
		return false;
	}

	if (bUseSpinTimeWindows && !SpinTimeWindows.IsEmpty())
	{
		return ElapsedTriggeredTime <= SpinTimeWindows.Last().EndTime;
	}

	return true;
}

void UProximityFloatSpinComponent::StartTriggerMusic(AActor* Owner)
{
	if (!Owner || !Owner->GetRootComponent())
	{
		return;
	}

	if (!TriggerMusic)
	{
		TriggerMusic = LoadObject<USoundBase>(nullptr, TEXT("/Game/NPC/Cat/SFX/Oiiai_Psytrance.Oiiai_Psytrance"));
	}

	if (!TriggerMusic)
	{
		return;
	}

	if (USoundWave* TriggerWave = Cast<USoundWave>(TriggerMusic))
	{
		TriggerWave->bLooping = bLoopMusic;
	}

	StopTriggerMusic();

	UAudioComponent* AudioComp = NewObject<UAudioComponent>(Owner);
	if (!AudioComp)
	{
		return;
	}

	AudioComp->bAutoActivate = false;
	AudioComp->bAutoDestroy = false;
	AudioComp->bStopWhenOwnerDestroyed = true;
	AudioComp->bAllowSpatialization = true;
	AudioComp->SetSound(TriggerMusic);
	AudioComp->SetVolumeMultiplier(MusicVolume);

	if (MusicAttenuation)
	{
		AudioComp->SetAttenuationSettings(MusicAttenuation);
	}
	else
	{
		AudioComp->SetOverrideAttenuation(true);
		FSoundAttenuationSettings AttenuationSettings;
		AttenuationSettings.bAttenuate = true;
		AttenuationSettings.bSpatialize = true;
		AttenuationSettings.AttenuationShape = EAttenuationShape::Sphere;
		AttenuationSettings.AttenuationShapeExtents = FVector(MusicInnerRadius, 0.0f, 0.0f);
		AttenuationSettings.FalloffDistance = MusicFalloffDistance;
		AudioComp->SetAttenuationOverrides(AttenuationSettings);
	}

	AudioComp->AttachToComponent(Owner->GetRootComponent(), FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	AudioComp->RegisterComponent();
	AudioComp->OnAudioFinished.AddDynamic(this, &UProximityFloatSpinComponent::HandleMusicFinished);

	ActiveAudioComponent = AudioComp;

	if (MusicFadeInDuration > 0.0f)
	{
		AudioComp->FadeIn(MusicFadeInDuration, 1.0f, 0.0f);
	}
	else
	{
		AudioComp->Play(0.0f);
	}
}

void UProximityFloatSpinComponent::StopTriggerMusic()
{
	if (ActiveAudioComponent)
	{
		ActiveAudioComponent->OnAudioFinished.RemoveDynamic(this, &UProximityFloatSpinComponent::HandleMusicFinished);
		ActiveAudioComponent->Stop();
		ActiveAudioComponent->DestroyComponent();
		ActiveAudioComponent = nullptr;
	}
}

void UProximityFloatSpinComponent::HandleMusicFinished()
{
	if (bHasDespawnedCat)
	{
		StopTriggerMusic();
		if (!bShockwaveVisualActive)
		{
			if (AActor* Owner = GetOwner())
			{
				Owner->Destroy();
			}
		}
		return;
	}

	if (bLoopMusic && bIsActive && ActiveAudioComponent)
	{
		ElapsedTriggeredTime = 0.0f;
		ResetTimelineEffects();
		ActiveAudioComponent->Play(0.0f);
	}
}

float UProximityFloatSpinComponent::ComputeJumpOffset(float TimeSeconds) const
{
	if (!bJumpBeforeSpin || !bIsActive || JumpHeight <= 0.0f || JumpDuration <= 0.0f)
	{
		return 0.0f;
	}

	auto EvaluateParabolicJump = [this](float LocalElapsed) -> float
	{
		if (LocalElapsed < 0.0f || LocalElapsed > JumpDuration)
		{
			return 0.0f;
		}
		const float Alpha = FMath::Clamp(LocalElapsed / JumpDuration, 0.0f, 1.0f);
		return 4.0f * Alpha * (1.0f - Alpha) * JumpHeight;
	};

	if (!bJumpBeforeEverySpinWindow)
	{
		return EvaluateParabolicJump(TimeSeconds);
	}

	float MaxJumpOffset = 0.0f;
	for (const FSpinTimeWindow& Window : SpinTimeWindows)
	{
		const float JumpStart = FMath::Max(0.0f, Window.StartTime - JumpDuration);
		MaxJumpOffset = FMath::Max(MaxJumpOffset, EvaluateParabolicJump(TimeSeconds - JumpStart));
	}
	return MaxJumpOffset;
}

void UProximityFloatSpinComponent::ApplyFloatAndSpinTransform(AActor* Owner, float DeltaTime)
{
	if (bHasDespawnedCat)
	{
		UpdateCatShockwave(DeltaTime, LastShockwavePostProcessAlpha);
		const bool bMusicStillPlaying = ActiveAudioComponent && ActiveAudioComponent->IsPlaying();
		if (!bShockwaveVisualActive && !bMusicStillPlaying && Owner)
		{
			Owner->Destroy();
		}
		return;
	}

	if (bIsActive)
	{
		ElapsedTriggeredTime += DeltaTime;
	}

	int32 ActiveWindowIndex = INDEX_NONE;
	float ActiveSpinMultiplier = 1.0f;
	const bool bShouldSpin = EvaluateShouldSpin(ActiveWindowIndex, ActiveSpinMultiplier);

	if (bIsActive
		&& bUseSpinTimeWindows
		&& !bTriggeredPopOutEffect
		&& SpinTimeWindows.IsValidIndex(DirectedPathSpinWindowIndex)
		&& ElapsedTriggeredTime >= SpinTimeWindows[DirectedPathSpinWindowIndex].StartTime)
	{
		TryTriggerDirectedPath(EDirectedPathTriggerMode::OnSpinWindow);
	}

	const bool bDiscoShouldRun = bEnableDiscoEnvironmentOnSpinWindow && IsSpinWindowActive(DiscoSpinWindowIndex);

	UnstableMatterIntensity = ComputeUnstableMatterIntensity(ElapsedTriggeredTime, bShouldSpin);
	UpdateCatGlow(UnstableMatterIntensity, ElapsedTriggeredTime);
	FVector UnstablePositionOffset = FVector::ZeroVector;
	FRotator UnstableRotationOffset = FRotator::ZeroRotator;
	ComputeUnstableMatterOffsets(UnstableMatterIntensity, ElapsedTriggeredTime, UnstablePositionOffset, UnstableRotationOffset, UnstableScaleMultiplier);

	EvaluateTimelineKeyframes(Owner, bDiscoShouldRun);
	UpdateCatShockwave(DeltaTime, LastShockwavePostProcessAlpha);
	UpdateSkyDarkening(DeltaTime);
	UpdateDiscoEnvironment(Owner, bDiscoShouldRun && !bTriggeredPopOutEffect, DeltaTime);

	// Float only while spinning; hold hover height while popping out to despawn; otherwise fall rapidly to ground.
	if (bShouldSpin)
	{
		FallVelocity = 0.0f;
		BobPhase = FMath::Fmod(BobPhase + DeltaTime * 2.0f * PI * BobFrequency, 2.0f * PI);
		const float TargetHoverZ = FloatHeight + FMath::Sin(BobPhase) * BobAmplitude;
		CurrentHoverHeight = FMath::FInterpTo(CurrentHoverHeight, TargetHoverZ, DeltaTime, BlendSpeed);
	}
	else if (bTriggeredPopOutEffect)
	{
		FallVelocity = 0.0f;
	}
	else
	{
		BobPhase = 0.0f;
		if (CurrentHoverHeight > 0.0f)
		{
			FallVelocity += FallGravity * DeltaTime;
			CurrentHoverHeight = FMath::Max(0.0f, CurrentHoverHeight - FallVelocity * DeltaTime);
			if (CurrentHoverHeight <= 0.0f)
			{
				FallVelocity = 0.0f;
			}
		}
		else
		{
			CurrentHoverHeight = 0.0f;
			FallVelocity = 0.0f;
		}
	}

	const float TotalJumpOffset = ComputeJumpOffset(ElapsedTriggeredTime);

	if (bShouldSpin)
	{
		const float YawStep = FMath::Min(FMath::Abs(SpinSpeed) * ActiveSpinMultiplier * DeltaTime, 150.0f);
		CurrentSpinOffsetYaw = FMath::Fmod(CurrentSpinOffsetYaw + YawStep, 360.0f);
	}
	else if (bResetRotationWhenSpinStops && !bTriggeredPopOutEffect)
	{
		CurrentSpinOffsetYaw = 0.0f;
	}

	if (bShouldSpin != bWasSpinningLastTick)
	{
		UpdateSpinAnimationState(bShouldSpin);
	}
	CurrentSqueezeFraction = UpdateInteractionSqueeze(Owner, DeltaTime);
	if (bHasDespawnedCat)
	{
		TArray<UPrimitiveComponent*> Primitives;
		Owner->GetComponents<UPrimitiveComponent>(Primitives);
		for (UPrimitiveComponent* PrimComp : Primitives)
		{
			if (PrimComp && !ShockwaveRingSegments.Contains(Cast<UStaticMeshComponent>(PrimComp)))
			{
				PrimComp->SetVisibility(false, false);
				PrimComp->SetHiddenInGame(true);
			}
		}
		if (InteractionTraceSphere)
		{
			InteractionTraceSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			InteractionTraceSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
		}
		bIsActive = false;

		const bool bMusicStillPlaying = ActiveAudioComponent && ActiveAudioComponent->IsPlaying();
		if (!bShockwaveVisualActive && !bMusicStillPlaying)
		{
			Owner->Destroy();
		}
		return;
	}
	UpdateSpinMorphTarget(bShouldSpin, DeltaTime);
	UpdateVicinityMeshHeight(bShouldSpin, TotalJumpOffset, CurrentSqueezeFraction, DeltaTime);

	const bool bSkelMeshIsRoot = RuntimeSkeletalMeshComponent && (RuntimeSkeletalMeshComponent == Owner->GetRootComponent());
	const float RootSqueezeOffsetZ = bSkelMeshIsRoot
		? (-MeshPivotHeightAboveBottom * (CurrentSqueezeFraction * InteractionSqueezeScaleZ))
		: 0.0f;
	const float RootVicinityOffsetZ = bSkelMeshIsRoot ? CurrentVicinityZOffset : 0.0f;
	const float ActorSqueezeOffsetZ = (!RuntimeSkeletalMeshComponent)
		? (-ActorPivotHeightAboveBottom * (CurrentSqueezeFraction * InteractionSqueezeScaleZ))
		: 0.0f;

	const bool bAtGroundRest = !bShouldSpin
		&& CurrentHoverHeight <= 0.0f
		&& TotalJumpOffset <= 0.0f
		&& !bIsInteractionSqueezing
		&& FMath::IsNearlyZero(CurrentSqueezeFraction)
		&& !bIsPopOutActive
		&& !bShockwaveVisualActive
		&& UnstableMatterIntensity <= KINDA_SMALL_NUMBER;
	if (bAtGroundRest)
	{
		if (bSettledAtRest)
		{
			return;
		}

		bSettledAtRest = true;
		const FVector RestLocation = BaseLocation + FVector(0.0f, 0.0f, RootVicinityOffsetZ);
		const FQuat YawSpinQuat(FVector::UpVector, FMath::DegreesToRadians(CurrentSpinOffsetYaw));
		const FRotator RestRotation = (YawSpinQuat * BaseRotation.Quaternion()).Rotator();
		Owner->SetActorLocationAndRotation(RestLocation, RestRotation, false, nullptr, ETeleportType::TeleportPhysics);
		return;
	}

	bSettledAtRest = false;
	const FVector NewLocation = BaseLocation
		+ FVector(0.0f, 0.0f, CurrentHoverHeight + TotalJumpOffset + RootSqueezeOffsetZ + RootVicinityOffsetZ + ActorSqueezeOffsetZ)
		+ UnstablePositionOffset;
	const FQuat YawSpinQuat(FVector::UpVector, FMath::DegreesToRadians(CurrentSpinOffsetYaw));
	const FRotator NewRotation = (UnstableRotationOffset.Quaternion() * YawSpinQuat * BaseRotation.Quaternion()).Rotator();
	Owner->SetActorLocationAndRotation(NewLocation, NewRotation, false, nullptr, ETeleportType::TeleportPhysics);
}

FTransform UProximityFloatSpinComponent::GetDirectedPathAnchorTransform(const AActor* Owner)
{
	if (!Owner)
	{
		return FTransform::Identity;
	}

	return FTransform(Owner->GetActorRotation(), Owner->GetActorLocation(), FVector::OneVector);
}

void UProximityFloatSpinComponent::StartDirectedPath(bool bRestartFromBeginning)
{
	if (!bUsingRuntimeOverrideWaypoints)
	{
		ResolveConfiguredWaypointsToWorld(GetOwner());
	}

	if (bRestartFromBeginning)
	{
		CurrentWaypointIndex = 0;
		bHasCompletedPath = false;
	}

	if (RuntimeWorldWaypoints.IsValidIndex(CurrentWaypointIndex) && !bHasCompletedPath)
	{
		bIsFollowingPath = true;
		bPathPaused = false;
		bSettledAtRest = false;
	}
	else
	{
		bIsFollowingPath = false;
	}
}

void UProximityFloatSpinComponent::StartDirectedPathWithWaypoints(const TArray<FVector>& WorldWaypoints)
{
	RuntimeWorldWaypoints = WorldWaypoints;
	bUsingRuntimeOverrideWaypoints = true;
	CurrentWaypointIndex = 0;
	bHasCompletedPath = false;
	bPathPaused = false;
	bIsFollowingPath = !RuntimeWorldWaypoints.IsEmpty();
	if (bIsFollowingPath)
	{
		bSettledAtRest = false;
	}
}

void UProximityFloatSpinComponent::MoveToDirectedLocation(const FVector& WorldDestination)
{
	StartDirectedPathWithWaypoints({ WorldDestination });
}

void UProximityFloatSpinComponent::PauseDirectedPath()
{
	if (bIsFollowingPath)
	{
		bPathPaused = true;
	}
}

void UProximityFloatSpinComponent::ResumeDirectedPath()
{
	if (bIsFollowingPath && !bHasCompletedPath && RuntimeWorldWaypoints.IsValidIndex(CurrentWaypointIndex))
	{
		bPathPaused = false;
		bSettledAtRest = false;
	}
}

void UProximityFloatSpinComponent::StopDirectedPath(bool bResetWaypointIndex)
{
	bIsFollowingPath = false;
	bPathPaused = false;
	if (bResetWaypointIndex)
	{
		CurrentWaypointIndex = 0;
		bHasCompletedPath = false;
		bUsingRuntimeOverrideWaypoints = false;
	}
}

void UProximityFloatSpinComponent::ResolveConfiguredWaypointsToWorld(const AActor* Owner)
{
	RuntimeWorldWaypoints.Reset();
	if (PathWaypoints.IsEmpty())
	{
		return;
	}

	const FTransform AnchorTransform = bHasCachedPathAnchorTransform
		? CachedPathAnchorTransform
		: GetDirectedPathAnchorTransform(Owner);

	RuntimeWorldWaypoints.Reserve(PathWaypoints.Num());
	for (const FVector& LocalWaypoint : PathWaypoints)
	{
		RuntimeWorldWaypoints.Add(AnchorTransform.TransformPosition(LocalWaypoint));
	}
}

void UProximityFloatSpinComponent::TryTriggerDirectedPath(EDirectedPathTriggerMode EventMode)
{
	if (bEnableDirectedPath
		&& !bHasAutoTriggeredDirectedPath
		&& DirectedPathTriggerMode == EventMode
		&& !bIsFollowingPath
		&& !bHasCompletedPath)
	{
		bHasAutoTriggeredDirectedPath = true;
		StartDirectedPath(/*bRestartFromBeginning=*/true);
	}
}

bool UProximityFloatSpinComponent::SnapLocationToGround(const AActor* Owner, FVector& InOutWorldLocation) const
{
	const UWorld* World = GetWorld();
	if (!World || !Owner)
	{
		return false;
	}

	const float HalfExtent = FMath::Max(10.0f, GroundTraceHalfHeight);
	const FVector TraceStart(InOutWorldLocation.X, InOutWorldLocation.Y, InOutWorldLocation.Z + HalfExtent);
	const FVector TraceEnd(InOutWorldLocation.X, InOutWorldLocation.Y, InOutWorldLocation.Z - HalfExtent);

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(CatDirectedPathGroundTrace), /*bTraceComplex=*/false, Owner);
	TArray<AActor*> AttachedActors;
	Owner->GetAttachedActors(AttachedActors, /*bResetArray=*/true, /*bRecursivelyIncludeAttachedActors=*/true);
	QueryParams.AddIgnoredActors(AttachedActors);
	for (TActorIterator<APickupItemActor> It(World); It; ++It)
	{
		if (IsValid(*It))
		{
			QueryParams.AddIgnoredActor(*It);
		}
	}

	FHitResult HitResult;
	if (World->LineTraceSingleByChannel(HitResult, TraceStart, TraceEnd, GroundTraceChannel, QueryParams))
	{
		InOutWorldLocation.Z = HitResult.ImpactPoint.Z + GroundHeightOffset;
		return true;
	}

	return false;
}

void UProximityFloatSpinComponent::UpdateDirectedPathMovement(AActor* Owner, float DeltaTime)
{
	if (!Owner || DeltaTime <= 0.0f || !bIsFollowingPath || bPathPaused || bHasCompletedPath || bTriggeredPopOutEffect || bIsPopOutActive)
	{
		return;
	}

	if (bPausePathWhileSpinning)
	{
		int32 UnusedWindowIndex = INDEX_NONE;
		float UnusedSpinMultiplier = 1.0f;
		const bool bSpinningNow = EvaluateShouldSpin(UnusedWindowIndex, UnusedSpinMultiplier);
		if (bSpinningNow || CurrentHoverHeight > KINDA_SMALL_NUMBER || ComputeJumpOffset(ElapsedTriggeredTime) > KINDA_SMALL_NUMBER)
		{
			return;
		}
	}

	if (!RuntimeWorldWaypoints.IsValidIndex(CurrentWaypointIndex))
	{
		if (!RuntimeWorldWaypoints.IsEmpty())
		{
			bIsFollowingPath = false;
			bHasCompletedPath = true;
			bSettledAtRest = false;
			OnDirectedPathCompleted.Broadcast();
		}
		return;
	}

	if (PathMoveSpeed <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	const FVector TargetPoint = RuntimeWorldWaypoints[CurrentWaypointIndex];
	const FVector StepTarget = bSnapPathToGround
		? FVector(TargetPoint.X, TargetPoint.Y, BaseLocation.Z)
		: TargetPoint;

	FVector NextLocation = FMath::VInterpConstantTo(BaseLocation, StepTarget, DeltaTime, PathMoveSpeed);
	if (bSnapPathToGround)
	{
		SnapLocationToGround(Owner, NextLocation);
	}

	if (bFacePathDirection)
	{
		const FVector DeltaXY(TargetPoint.X - BaseLocation.X, TargetPoint.Y - BaseLocation.Y, 0.0f);
		if (!DeltaXY.IsNearlyZero(1.0f))
		{
			const FRotator TargetRotation(BaseRotation.Pitch, DeltaXY.Rotation().Yaw + PathFacingYawOffset, BaseRotation.Roll);
			BaseRotation = FMath::RInterpTo(BaseRotation, TargetRotation, DeltaTime, FMath::Max(0.1f, PathTurnSpeed));
		}
	}

	const float AcceptanceRadiusSq = FMath::Square(FMath::Max(0.1f, WaypointAcceptanceRadius));
	const float DistToTargetSq = bSnapPathToGround
		? FVector::DistSquaredXY(NextLocation, TargetPoint)
		: FVector::DistSquared(NextLocation, TargetPoint);

	if (DistToTargetSq <= AcceptanceRadiusSq)
	{
		NextLocation = bSnapPathToGround
			? FVector(TargetPoint.X, TargetPoint.Y, NextLocation.Z)
			: TargetPoint;
		if (bSnapPathToGround)
		{
			SnapLocationToGround(Owner, NextLocation);
		}

		BaseLocation = NextLocation;
		bSettledAtRest = false;

		const int32 ReachedIndex = CurrentWaypointIndex;
		++CurrentWaypointIndex;
		OnDirectedPathWaypointReached.Broadcast(ReachedIndex);

		if (CurrentWaypointIndex >= RuntimeWorldWaypoints.Num())
		{
			bIsFollowingPath = false;
			bHasCompletedPath = true;
			OnDirectedPathCompleted.Broadcast();
		}
		return;
	}

	BaseLocation = NextLocation;
	bSettledAtRest = false;
}

void UProximityFloatSpinComponent::DrawDirectedPathDebug() const
{
	const UWorld* World = GetWorld();
	if (!World || RuntimeWorldWaypoints.IsEmpty())
	{
		return;
	}

	FVector PreviousPoint = BaseLocation;
	for (int32 Index = 0; Index < RuntimeWorldWaypoints.Num(); ++Index)
	{
		const FVector& Waypoint = RuntimeWorldWaypoints[Index];
		const bool bReached = Index < CurrentWaypointIndex || bHasCompletedPath;
		const bool bActiveTarget = !bHasCompletedPath && (Index == CurrentWaypointIndex);

		if (Index >= CurrentWaypointIndex && !bHasCompletedPath)
		{
			DrawDebugLine(World, PreviousPoint, Waypoint, FColor::Cyan, false, -1.0f, 0, 2.0f);
			PreviousPoint = Waypoint;
		}
		else if (Index > 0)
		{
			DrawDebugLine(World, RuntimeWorldWaypoints[Index - 1], Waypoint, FColor::Silver, false, -1.0f, 0, 1.0f);
		}

		const FColor PointColor = bActiveTarget ? FColor::Green : (bReached ? FColor::Silver : FColor::Yellow);
		DrawDebugSphere(World, Waypoint, 12.0f, 12, PointColor, false, -1.0f, 0, 1.5f);
	}
}
