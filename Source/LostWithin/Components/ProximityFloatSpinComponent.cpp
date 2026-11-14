// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/ProximityFloatSpinComponent.h"
#include "Animation/AnimationAsset.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/AudioComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/LightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SphereComponent.h"
#include "Components/SpotLightComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/RotatingMovementComponent.h"
#include "Interaction/PickupItemActor.h"
#include "Kismet/GameplayStatics.h"
#include "LostWithin.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "UObject/UObjectIterator.h"

UProximityFloatSpinComponent::UProximityFloatSpinComponent()
{
	PrimaryComponentTick.bCanEverTick = true;

	SpinTimeWindows = {
		{ 0.27f, 1.77f, 1.5f },
		{ 3.12f, 5.10f, 1.0f },
		{ 6.06f, 43.84f, 1.5f }
	};

	DropItemClass = APickupItemActor::StaticClass();

	InteractionSoundSearchPaths = {
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

	BaseLocation = Owner->GetActorLocation();
	BaseRotation = Owner->GetActorRotation();
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
	bIsInteractionBouncing = false;
	InteractionBounceElapsed = 0.0f;
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

	InitializeVisualMeshes(Owner);

	const FBox OwnerBounds = Owner->GetComponentsBoundingBox(/*bNonColliding=*/true);
	if (OwnerBounds.IsValid)
	{
		ActorPivotHeightAboveBottom = FMath::Max(0.0f, Owner->GetActorLocation().Z - OwnerBounds.Min.Z);
	}

	LoadInteractionSounds();
}

void UProximityFloatSpinComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	SetDiscoEnvironmentActive(GetOwner(), false);
	RestoreWorldSkyAndLights();
	ResetInteractionSqueeze(GetOwner());
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

	if (const APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0))
	{
		if (bUseMorphVicinityCheck)
		{
			UpdateMorphVicinityState(PlayerPawn);
		}

		if (!bTriggerSpinByInteractionCount && !(bHasTriggered && bStayActiveOnceTriggered))
		{
			UpdateProximityState(Owner, PlayerPawn);
		}
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
			}
		}
	}

	if (USkeletalMeshComponent* ExistingSkelComp = Owner->FindComponentByClass<USkeletalMeshComponent>())
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
			SkelComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			SkelComp->SetRelativeTransform(StandbyMeshOffset);
			SkelComp->AttachToComponent(Owner->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
			SkelComp->RegisterComponent();
			SkelComp->SetSkeletalMesh(StandbySkeletalMesh, true);

			RuntimeSkeletalMeshComponent = SkelComp;
			bOwnsRuntimeSkeletalMesh = true;
		}
	}

	if (RuntimeSkeletalMeshComponent)
	{
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
			RuntimeSkeletalMeshComponent->SetRelativeLocation(BaseMeshRelativeLocation + FVector(0.0f, 0.0f, CurrentVicinityZOffset));
		}
	}

	bWasSpinningLastTick = false;
	UpdateSpinAnimationState(false);

	// Ensure a reliable query-only trace volume exists for [F] interaction and [LMB] hit traces
	if (USphereComponent* TraceSphere = NewObject<USphereComponent>(Owner))
	{
		TraceSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		TraceSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
		TraceSphere->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
		TraceSphere->SetGenerateOverlapEvents(false);
		TraceSphere->SetUsingAbsoluteScale(true);
		TraceSphere->SetWorldScale3D(FVector::OneVector);

		FVector SphereWorldOrigin = Owner->GetActorLocation() + FVector(0.0f, 0.0f, 25.0f);
		float DesiredWorldRadius = InteractionTraceRadius;

		if (RuntimeSkeletalMeshComponent)
		{
			RuntimeSkeletalMeshComponent->UpdateBounds();
			const FBoxSphereBounds MeshBounds = RuntimeSkeletalMeshComponent->Bounds;
			if (MeshBounds.SphereRadius > KINDA_SMALL_NUMBER)
			{
				SphereWorldOrigin = MeshBounds.Origin;
				DesiredWorldRadius = FMath::Clamp(MeshBounds.SphereRadius * 1.15f, InteractionTraceRadius, 100.0f);

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
	if (bUseMorphVicinityCheck)
	{
		// Default/far = ActiveMorphWeight (1.0); inside vicinity = InactiveMorphWeight (0.0), optionally overridden while spinning.
		const bool bShowActiveMorph = !bIsInMorphVicinity || (bIsSpinning && bActiveMorphWhileSpinning);
		TargetWeight = bShowActiveMorph ? ActiveMorphWeight : InactiveMorphWeight;
	}
	else
	{
		TargetWeight = bIsSpinning ? ActiveMorphWeight : InactiveMorphWeight;
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
		&& !bIsInMorphVicinity
		&& !bIsSpinning
		&& CurrentHoverHeight <= 0.0f
		&& JumpOffset <= 0.0f;
	const float TargetZOffset = bShouldLowerForActiveMorph ? ActiveMorphMeshZOffset : 0.0f;
	CurrentVicinityZOffset = FMath::FInterpTo(CurrentVicinityZOffset, TargetZOffset, DeltaTime, MorphHeightBlendSpeed);
	if (FMath::IsNearlyEqual(CurrentVicinityZOffset, TargetZOffset, KINDA_SMALL_NUMBER))
	{
		CurrentVicinityZOffset = TargetZOffset;
	}

	const float SqueezeZCompensation = -MeshPivotHeightAboveBottom * (SqueezeFraction * InteractionSqueezeScaleZ);
	const FVector DesiredRelLoc = BaseMeshRelativeLocation + FVector(0.0f, 0.0f, CurrentVicinityZOffset + SqueezeZCompensation);
	if (!RuntimeSkeletalMeshComponent->GetRelativeLocation().Equals(DesiredRelLoc, KINDA_SMALL_NUMBER))
	{
		RuntimeSkeletalMeshComponent->SetRelativeLocation(DesiredRelLoc);
	}
}

void UProximityFloatSpinComponent::UpdateMorphVicinityState(const APawn* PlayerPawn)
{
	const float DistanceSq = FVector::DistSquared(BaseLocation, PlayerPawn->GetActorLocation());
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
	const float DistanceSq = FVector::DistSquared(BaseLocation, PlayerPawn->GetActorLocation());
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
		if (bStopMusicOnDeactivate)
		{
			StopTriggerMusic();
		}
		OnProximityFloatStateChanged.Broadcast(false);
	}
}

void UProximityFloatSpinComponent::TriggerSpinSequence(AActor* Owner)
{
	if (!Owner || bIsActive)
	{
		return;
	}

	ResetInteractionSqueeze(Owner);
	bIsActive = true;
	bHasTriggered = true;
	bSettledAtRest = false;
	ElapsedTriggeredTime = 0.0f;
	StartTriggerMusic(Owner);
	OnProximityFloatStateChanged.Broadcast(true);
}

void UProximityFloatSpinComponent::LoadInteractionSounds()
{
	if (!InteractionSounds.IsEmpty())
	{
		return;
	}

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
		if (USoundBase* DefaultCatSound = LoadObject<USoundBase>(nullptr, TEXT("/Game/SoundWave/SoundCue/MetaSound/Cat/Cat_Meow_SFX_01.Cat_Meow_SFX_01")))
		{
			InteractionSounds.Add(DefaultCatSound);
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

void UProximityFloatSpinComponent::TriggerInteractionBounce()
{
	bIsInteractionBouncing = true;
	InteractionBounceElapsed = 0.0f;
	bSettledAtRest = false;
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
	const FVector ScaleMultiplier(1.0f + ExpandXY, 1.0f + ExpandXY, FMath::Max(0.25f, 1.0f - SqueezeZ));

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

	if (RuntimeSkeletalMeshComponent)
	{
		RuntimeSkeletalMeshComponent->SetRelativeScale3D(BaseMeshRelativeScale3D);
		RuntimeSkeletalMeshComponent->SetRelativeLocation(BaseMeshRelativeLocation + FVector(0.0f, 0.0f, CurrentVicinityZOffset));
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
	const FLinearColor NightLightTint(0.22f, 0.24f, 0.58f);
	const FLinearColor NightSkyTint(0.50f, 0.60f, 1.0f);

	for (const FCachedWorldSkyAndLightState& Cached : CachedWorldSkyAndLights)
	{
		switch (Cached.TargetType)
		{
		case ECachedAtmosphereTargetType::DirectionalLight:
			if (ULightComponent* LightComp = Cached.LightComponent.Get())
			{
				LightComp->SetIntensity(FMath::Lerp(Cached.OriginalIntensity, Cached.OriginalIntensity * SkyDarkenTargetWorldLightMultiplier, ClampedAlpha));
				LightComp->SetLightColor(FMath::Lerp(Cached.OriginalPrimaryColor, NightLightTint, ClampedAlpha));
			}
			break;

		case ECachedAtmosphereTargetType::SkyLight:
			if (USkyLightComponent* SkyComp = Cached.SkyLightComponent.Get())
			{
				SkyComp->SetIntensity(FMath::Lerp(Cached.OriginalIntensity, Cached.OriginalIntensity * SkyDarkenTargetWorldLightMultiplier, ClampedAlpha));
				SkyComp->SetLightColor(FMath::Lerp(Cached.OriginalPrimaryColor, NightLightTint, ClampedAlpha));
			}
			break;

		case ECachedAtmosphereTargetType::SkyAtmosphere:
			if (USkyAtmosphereComponent* SkyAtmo = Cached.SkyAtmosphereComponent.Get())
			{
				const FLinearColor TargetSkyLuminance = Cached.OriginalPrimaryColor * (NightSkyTint * SkyDarkenTargetAtmosphereMultiplier);
				SkyAtmo->SetSkyLuminanceFactor(FMath::Lerp(Cached.OriginalPrimaryColor, TargetSkyLuminance, ClampedAlpha));
			}
			break;

		case ECachedAtmosphereTargetType::ExponentialHeightFog:
			if (UExponentialHeightFogComponent* FogComp = Cached.ExponentialFogComponent.Get())
			{
				const FLinearColor TargetFogColor = Cached.OriginalPrimaryColor * (NightSkyTint * SkyDarkenTargetFogMultiplier);
				const FLinearColor TargetDirColor = Cached.OriginalSecondaryColor * (NightSkyTint * SkyDarkenTargetFogMultiplier);
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

	// Suspend the sweeping disco rig high above the floating cat so illumination projects downward from overhead
	DiscoRigPivot = NewObject<USceneComponent>(Owner);
	if (!DiscoRigPivot)
	{
		SetDiscoEnvironmentActive(Owner, false);
		return;
	}

	const FVector RigWorldLocation = BaseLocation + FVector(0.0f, 0.0f, FloatHeight + DiscoRigHeightAboveFloat);
	DiscoRigPivot->SetMobility(EComponentMobility::Movable);
	DiscoRigPivot->SetUsingAbsoluteLocation(true);
	DiscoRigPivot->SetUsingAbsoluteRotation(true);
	DiscoRigPivot->SetWorldLocationAndRotation(RigWorldLocation, FRotator::ZeroRotator);
	DiscoRigPivot->AttachToComponent(Owner->GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);
	DiscoRigPivot->RegisterComponent();

	const int32 LightCount = FMath::Max(1, DiscoSpotlightCount);
	for (int32 Index = 0; Index < LightCount; ++Index)
	{
		if (USpotLightComponent* SpotComp = NewObject<USpotLightComponent>(Owner))
		{
			SpotComp->SetMobility(EComponentMobility::Movable);
			SpotComp->SetCastShadows(false);
			SpotComp->SetIntensityUnits(ELightUnits::Candelas);
			SpotComp->SetInnerConeAngle(14.0f);
			SpotComp->SetOuterConeAngle(32.0f);
			SpotComp->SetAttenuationRadius(DiscoLightAttenuationRadius);
			SpotComp->SetIntensity(DiscoSpotlightIntensity);
			SpotComp->AttachToComponent(DiscoRigPivot, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
			SpotComp->RegisterComponent();
			DiscoSpotlights.Add(SpotComp);
		}
	}

	if (UPointLightComponent* FillComp = NewObject<UPointLightComponent>(Owner))
	{
		FillComp->SetMobility(EComponentMobility::Movable);
		FillComp->SetCastShadows(false);
		FillComp->SetIntensityUnits(ELightUnits::Candelas);
		FillComp->SetAttenuationRadius(DiscoLightAttenuationRadius * 1.15f);
		FillComp->SetIntensity(DiscoSpotlightIntensity * DiscoFillIntensityMultiplier);
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
		PPComp->AttachToComponent(DiscoRigPivot, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
		PPComp->RegisterComponent();
		DiscoPostProcessComponent = PPComp;
	}
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

	const FVector RigWorldLocation = BaseLocation + FVector(0.0f, 0.0f, FloatHeight + DiscoRigHeightAboveFloat);
	DiscoRigPivot->SetWorldLocationAndRotation(
		RigWorldLocation,
		FRotator(0.0f, DiscoOrbitYaw, 0.0f));

	const float BeatSine = 0.5f + 0.5f * FMath::Sin(DiscoElapsedTime * 2.0f * PI * DiscoPulseFrequency);
	const float BeatPulse = FMath::Square(BeatSine);

	const int32 LightCount = FMath::Max(1, DiscoSpotlights.Num());
	for (int32 Index = 0; Index < DiscoSpotlights.Num(); ++Index)
	{
		if (USpotLightComponent* SpotComp = DiscoSpotlights[Index])
		{
			const float NormalizedIndex = static_cast<float>(Index) / static_cast<float>(LightCount);
			const float Hue360 = FMath::Fmod((DiscoElapsedTime * DiscoColorCycleSpeed + NormalizedIndex) * 360.0f, 360.0f);
			const FLinearColor SpotColor = FLinearColor(Hue360, 0.95f, 1.0f).HSVToLinearRGB();

			const float AngleDeg = NormalizedIndex * 360.0f;
			const float SweepPitch = -40.0f + 12.0f * FMath::Sin(DiscoElapsedTime * 4.2f + Index * 1.15f);

			SpotComp->SetRelativeRotation(FRotator(SweepPitch, AngleDeg, 0.0f));
			SpotComp->SetLightColor(SpotColor);
			SpotComp->SetIntensity(DiscoSpotlightIntensity * (0.65f + 0.70f * BeatPulse));
		}
	}

	if (DiscoFillPointLight)
	{
		const float FillHue = FMath::Fmod(DiscoElapsedTime * DiscoColorCycleSpeed * 180.0f, 360.0f);
		DiscoFillPointLight->SetLightColor(FLinearColor(FillHue, 0.90f, 1.0f).HSVToLinearRGB());
		DiscoFillPointLight->SetIntensity(DiscoSpotlightIntensity * DiscoFillIntensityMultiplier * (0.75f + 0.50f * BeatPulse));
	}

	if (DiscoPostProcessComponent)
	{
		DiscoPostProcessComponent->Settings.BloomIntensity = FMath::Lerp(0.9f, 1.7f, BeatPulse);
		DiscoPostProcessComponent->Settings.SceneFringeIntensity = FMath::Lerp(0.4f, 1.5f, BeatPulse);
		DiscoPostProcessComponent->Settings.VignetteIntensity = 0.55f;
		DiscoPostProcessComponent->Settings.ColorSaturation = FVector4(1.25f, 1.25f, 1.25f, 1.0f);
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
	const FVector SpawnOrigin = Owner->GetActorLocation() + FVector(0.0f, 0.0f, 50.0f);

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

	const FVector TargetDropLocation = Owner->GetActorLocation() + TossDirection * 95.0f;

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = Owner;
	SpawnParams.Instigator = Cast<APawn>(Interactor);
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	if (APickupItemActor* DroppedItem = World->SpawnActor<APickupItemActor>(SpawnClass, SpawnOrigin, FRotator::ZeroRotator, SpawnParams))
	{
		DroppedItem->StartDropArc(SpawnOrigin, TargetDropLocation);
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

	if (InteractionCount >= ItemDropInteractionThreshold && !bHasDroppedItem)
	{
		bHasDroppedItem = true;
		SpawnDroppedItem(Owner, Interactor);
	}

	if (bTriggerSpinByInteractionCount && InteractionCount >= SpinTriggerInteractionThreshold && !bIsActive)
	{
		TriggerSpinSequence(Owner);
	}
}

void UProximityFloatSpinComponent::OnHit_Implementation(AActor* Attacker, const FHitResult& HitResult)
{
	if (AActor* Owner = GetOwner())
	{
		TriggerInteractionBounce();
		PlayRandomInteractionSound(Owner);
	}
}

FText UProximityFloatSpinComponent::GetInteractionPrompt_Implementation() const
{
	return NSLOCTEXT("LostWithin", "CatInteractionPrompt", "[F] Pet Cat");
}

bool UProximityFloatSpinComponent::CanInteract_Implementation(AActor* Interactor) const
{
	return !bIsActive && !bHasTriggered;
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

bool UProximityFloatSpinComponent::IsWithinSpinWindow(float TimeSeconds) const
{
	bool bIsSpinning = false;
	int32 UnusedWindowIndex = INDEX_NONE;
	GetActiveSpinSpeedMultiplier(TimeSeconds, bIsSpinning, UnusedWindowIndex);
	return bIsSpinning;
}

void UProximityFloatSpinComponent::StartTriggerMusic(AActor* Owner)
{
	if (!TriggerMusic || !Owner || !Owner->GetRootComponent())
	{
		return;
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
	if (bLoopMusic && bIsActive && ActiveAudioComponent)
	{
		ElapsedTriggeredTime = 0.0f;
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
	if (bIsActive)
	{
		ElapsedTriggeredTime += DeltaTime;
	}

	bool bShouldSpin = bIsActive;
	int32 ActiveWindowIndex = INDEX_NONE;
	float ActiveSpinMultiplier = 1.0f;
	if (bIsActive && bUseSpinTimeWindows)
	{
		ActiveSpinMultiplier = GetActiveSpinSpeedMultiplier(ElapsedTriggeredTime, bShouldSpin, ActiveWindowIndex);
	}

	const bool bDiscoShouldRun = bEnableDiscoEnvironmentOnSpinWindow
		&& bIsActive
		&& bShouldSpin
		&& bUseSpinTimeWindows
		&& ActiveWindowIndex == DiscoSpinWindowIndex;
	UpdateSkyDarkening(DeltaTime);
	UpdateDiscoEnvironment(Owner, bDiscoShouldRun, DeltaTime);

	// Float only while spinning; fall rapidly to ground using real gravity acceleration when spinning stops.
	if (bShouldSpin)
	{
		FallVelocity = 0.0f;
		BobPhase = FMath::Fmod(BobPhase + DeltaTime * 2.0f * PI * BobFrequency, 2.0f * PI);
		const float TargetHoverZ = FloatHeight + FMath::Sin(BobPhase) * BobAmplitude;
		CurrentHoverHeight = FMath::FInterpTo(CurrentHoverHeight, TargetHoverZ, DeltaTime, BlendSpeed);
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

	const float JumpOffset = ComputeJumpOffset(ElapsedTriggeredTime);

	float InteractionBounceOffset = 0.0f;
	if (bIsInteractionBouncing)
	{
		InteractionBounceElapsed += DeltaTime;
		if (InteractionBounceElapsed >= InteractionBounceDuration || InteractionBounceDuration <= 0.0f)
		{
			bIsInteractionBouncing = false;
			InteractionBounceElapsed = 0.0f;
		}
		else
		{
			const float Alpha = FMath::Clamp(InteractionBounceElapsed / InteractionBounceDuration, 0.0f, 1.0f);
			InteractionBounceOffset = 4.0f * Alpha * (1.0f - Alpha) * InteractionBounceHeight;
		}
	}

	const float TotalJumpOffset = JumpOffset + InteractionBounceOffset;

	if (bShouldSpin)
	{
		CurrentSpinOffsetYaw = FMath::Fmod(CurrentSpinOffsetYaw + SpinSpeed * ActiveSpinMultiplier * DeltaTime, 360.0f);
	}
	else if (bResetRotationWhenSpinStops)
	{
		CurrentSpinOffsetYaw = 0.0f;
	}

	if (bShouldSpin != bWasSpinningLastTick)
	{
		UpdateSpinAnimationState(bShouldSpin);
	}
	CurrentSqueezeFraction = UpdateInteractionSqueeze(Owner, DeltaTime);
	UpdateSpinMorphTarget(bShouldSpin, DeltaTime);
	UpdateVicinityMeshHeight(bShouldSpin, TotalJumpOffset, CurrentSqueezeFraction, DeltaTime);

	const float ActorSqueezeOffsetZ = (!RuntimeSkeletalMeshComponent)
		? (-ActorPivotHeightAboveBottom * (CurrentSqueezeFraction * InteractionSqueezeScaleZ))
		: 0.0f;

	const bool bAtGroundRest = !bShouldSpin
		&& CurrentHoverHeight <= 0.0f
		&& TotalJumpOffset <= 0.0f
		&& !bIsInteractionSqueezing
		&& FMath::IsNearlyZero(CurrentSqueezeFraction);
	if (bAtGroundRest)
	{
		if (bSettledAtRest)
		{
			return;
		}

		bSettledAtRest = true;
		const FQuat YawSpinQuat(FVector::UpVector, FMath::DegreesToRadians(CurrentSpinOffsetYaw));
		const FRotator RestRotation = (YawSpinQuat * BaseRotation.Quaternion()).Rotator();
		Owner->SetActorLocationAndRotation(BaseLocation, RestRotation);
		return;
	}

	bSettledAtRest = false;
	const FVector NewLocation = BaseLocation + FVector(0.0f, 0.0f, CurrentHoverHeight + TotalJumpOffset + ActorSqueezeOffsetZ);
	const FQuat YawSpinQuat(FVector::UpVector, FMath::DegreesToRadians(CurrentSpinOffsetYaw));
	const FRotator NewRotation = (YawSpinQuat * BaseRotation.Quaternion()).Rotator();
	Owner->SetActorLocationAndRotation(NewLocation, NewRotation);
}
