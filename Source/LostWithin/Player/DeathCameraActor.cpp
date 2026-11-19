// Copyright Epic Games, Inc. All Rights Reserved.

#include "Player/DeathCameraActor.h"

#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "LostWithin.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	float EaseOutCubic(float T)
	{
		const float Inverse = 1.0f - FMath::Clamp(T, 0.0f, 1.0f);
		return 1.0f - Inverse * Inverse * Inverse;
	}

	float SmoothStep01(float T)
	{
		const float X = FMath::Clamp(T, 0.0f, 1.0f);
		return X * X * (3.0f - 2.0f * X);
	}
}

ADeathCameraActor::ADeathCameraActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	SetActorEnableCollision(false);

	if (UCameraComponent* Camera = GetCameraComponent())
	{
		Camera->bConstrainAspectRatio = false;
		Camera->SetFieldOfView(90.0f);
	}

	static ConstructorHelpers::FObjectFinder<USoundBase> DeathImpact(
		TEXT("/Game/Audio/SFX/Player/Death/SFX_DeathImpact_01.SFX_DeathImpact_01"));
	StingSound = DeathImpact.Object;

	// Generated with ElevenLabs (Tools/SFX/sfx-manifest.json, id death_jumpscare); plays together with the impact.
	static ConstructorHelpers::FObjectFinder<USoundBase> DeathJumpscare(
		TEXT("/Game/Audio/SFX/Player/Death/SFX_DeathJumpscare_01.SFX_DeathJumpscare_01"));
	JumpscareSound = DeathJumpscare.Object;
}

FVector ADeathCameraActor::FindLookTarget(const AActor* InKiller)
{
	if (!IsValid(InKiller))
	{
		return FVector::ZeroVector;
	}

	if (const ACharacter* Character = Cast<ACharacter>(InKiller))
	{
		if (const USkeletalMeshComponent* Mesh = Character->GetMesh(); Mesh && Mesh->GetSkeletalMeshAsset())
		{
			// FName comparison ignores case, so "head" also finds "Head" and "HEAD".
			static const FName HeadBones[] = {
				TEXT("head"), TEXT("mixamorig:Head"), TEXT("Bip001-Head"), TEXT("Bip01-Head"), TEXT("neck_01"), TEXT("neck")
			};
			for (const FName& Bone : HeadBones)
			{
				if (Mesh->DoesSocketExist(Bone))
				{
					return Mesh->GetSocketLocation(Bone);
				}
			}
		}
		if (const UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
		{
			return Capsule->GetComponentLocation() + Capsule->GetUpVector() * Capsule->GetScaledCapsuleHalfHeight() * 0.6f;
		}
	}
	return InKiller->GetActorLocation();
}

void ADeathCameraActor::StartSequence(APawn* InVictim, AActor* InKiller, const FMinimalViewInfo& StartView)
{
	Victim = InVictim;
	Killer = InKiller;
	StartLocation = StartView.Location;
	StartRotation = StartView.Rotation.Quaternion();
	StartFOV = StartView.FOV > 0.0f ? StartView.FOV : 90.0f;
	EyeLocation = InVictim ? InVictim->GetPawnViewLocation() : StartLocation;

	// Keep the killer's head from filling (or clipping) the view at contact range.
	const FVector Target = FindLookTarget(InKiller);
	if (IsValid(InKiller))
	{
		const FVector ToTarget = Target - EyeLocation;
		if (ToTarget.Size() < MinDistanceToKiller)
		{
			const FVector Away = ToTarget.IsNearlyZero()
				? (InVictim ? -InVictim->GetActorForwardVector() : FVector::BackwardVector)
				: -ToTarget.GetSafeNormal();
			EyeLocation = Target + Away * MinDistanceToKiller;
		}
	}
	StareEndLocation = EyeLocation;

	FloorZ = EyeLocation.Z - 160.0;
	if (UWorld* World = GetWorld())
	{
		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(LostWithinDeathCameraFloor), /*bTraceComplex=*/false, this);
		QueryParams.AddIgnoredActor(InVictim);
		QueryParams.AddIgnoredActor(InKiller);
		FHitResult FloorHit;
		if (World->LineTraceSingleByObjectType(FloorHit, EyeLocation, EyeLocation - FVector(0.0f, 0.0f, 400.0f),
			FCollisionObjectQueryParams(ECC_WorldStatic), QueryParams))
		{
			FloorZ = FloorHit.ImpactPoint.Z;
		}
	}

	Elapsed = 0.0f;
	bStarted = true;
	bFadeStarted = false;
	bFinished = false;
	ApplyView(StartLocation, StartRotation.Rotator(), StartFOV);

	// The jumpscare and the impact start together. Both are UI sounds, so the end screen's pause does not cut them off.
	if (JumpscareSound)
	{
		UGameplayStatics::PlaySound2D(this, JumpscareSound, 1.0f, 1.0f, 0.0f, nullptr, nullptr, /*bIsUISound=*/true);
		UE_LOG(LogLostWithin, Log, TEXT("Death camera: death jumpscare %s"), *JumpscareSound->GetName());
	}
	if (StingSound)
	{
		UGameplayStatics::PlaySound2D(this, StingSound, 1.0f, 1.0f, 0.0f, nullptr, nullptr, /*bIsUISound=*/true);
		UE_LOG(LogLostWithin, Log, TEXT("Death camera: death impact %s"), *StingSound->GetName());
	}
}

void ADeathCameraActor::ApplyView(const FVector& Location, const FRotator& Rotation, float FieldOfView)
{
	SetActorLocationAndRotation(Location, Rotation);
	if (UCameraComponent* Camera = GetCameraComponent())
	{
		Camera->SetFieldOfView(FieldOfView);
	}
}

void ADeathCameraActor::StartFade()
{
	bFadeStarted = true;
	const APawn* VictimPawn = Victim.Get();
	const APlayerController* PlayerController = VictimPawn ? Cast<APlayerController>(VictimPawn->GetController()) : nullptr;
	if (PlayerController && PlayerController->PlayerCameraManager)
	{
		PlayerController->PlayerCameraManager->StartCameraFade(0.0f, 1.0f, CollapseDuration, FLinearColor::Black,
			/*bShouldFadeAudio=*/false, /*bHoldWhenFinished=*/true);
	}
}

void ADeathCameraActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bStarted || bFinished)
	{
		return;
	}

	Elapsed += DeltaSeconds;
	const FVector Target = Killer.IsValid()
		? FindLookTarget(Killer.Get())
		: EyeLocation + StartRotation.GetForwardVector() * 100.0f;

	const float StareStart = TurnDuration;
	const float CollapseStart = TurnDuration + StareDuration;

	if (Elapsed < StareStart)
	{
		const float Alpha = EaseOutCubic(Elapsed / TurnDuration);
		const FVector Location = FMath::Lerp(StartLocation, EyeLocation, Alpha);
		const FQuat LookAt = (Target - Location).ToOrientationQuat();
		ApplyView(Location, FQuat::Slerp(StartRotation, LookAt, Alpha).Rotator(), FMath::Lerp(StartFOV, TurnFOV, Alpha));
		return;
	}

	if (Elapsed < CollapseStart)
	{
		const float StareAlpha = (Elapsed - StareStart) / FMath::Max(StareDuration, UE_KINDA_SMALL_NUMBER);
		const float Ease = SmoothStep01(StareAlpha);
		const FVector ToTarget = Target - EyeLocation;
		const float Push = FMath::Min(PushInDistance, FMath::Max(0.0f, static_cast<float>(ToTarget.Size()) - MinDistanceToKiller));
		const FVector Location = EyeLocation + ToTarget.GetSafeNormal() * Push * Ease;
		StareEndLocation = Location;

		// Shake grows as the stare goes on.
		const float Strength = ShakeAmplitude * (0.5f + StareAlpha);
		FRotator Rotation = (Target - Location).Rotation();
		Rotation.Pitch += FMath::PerlinNoise1D(Elapsed * 9.0f) * Strength;
		Rotation.Yaw += FMath::PerlinNoise1D(Elapsed * 7.3f + 13.1f) * Strength;
		ApplyView(Location, Rotation, FMath::Lerp(TurnFOV, StareFOV, Ease));
		return;
	}

	if (Elapsed < GetTotalDuration())
	{
		if (!bFadeStarted)
		{
			StartFade();
		}
		const float CollapseAlpha = (Elapsed - CollapseStart) / CollapseDuration;
		const float Ease = CollapseAlpha * CollapseAlpha;
		const FVector Back = -(Target - StareEndLocation).GetSafeNormal2D() * 25.0f;
		const FVector Floor(StareEndLocation.X + Back.X, StareEndLocation.Y + Back.Y, FloorZ + CollapsedEyeHeight);
		const FVector Location = FMath::Lerp(StareEndLocation, Floor, Ease);

		FRotator Rotation = (Target - Location).Rotation();
		Rotation.Roll = CollapseRoll * Ease;
		Rotation.Pitch += FMath::PerlinNoise1D(Elapsed * 11.0f) * ShakeAmplitude * 0.5f;
		ApplyView(Location, Rotation, StareFOV);
		return;
	}

	bFinished = true;
	OnFinished.Broadcast();
}
