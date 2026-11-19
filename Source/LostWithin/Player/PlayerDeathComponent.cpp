// Copyright Epic Games, Inc. All Rights Reserved.

#include "Player/PlayerDeathComponent.h"
#include "Player/BodyContactComponent.h"

#include "AI/MonsterAIComponent.h"
#include "AI/MonsterHearingComponent.h"
#include "AIController.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "LostWithin.h"
#include "LostWithinCharacter.h"
#include "LostWithinGameMode.h"
#include "LostWithinPlayerController.h"
#include "Player/DeathCameraActor.h"
#include "TimerManager.h"

UPlayerDeathComponent::UPlayerDeathComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
	DeathCameraClass = ADeathCameraActor::StaticClass();
}

void UPlayerDeathComponent::BeginPlay()
{
	Super::BeginPlay();
	if (UBodyContactComponent* Contact = GetOwner()->FindComponentByClass<UBodyContactComponent>())
	{
		AddTickPrerequisiteComponent(Contact);
	}
}

void UPlayerDeathComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (const ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		if (UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
		{
			Capsule->OnComponentHit.RemoveDynamic(this, &UPlayerDeathComponent::HandleCapsuleHit);
		}
	}
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(FinishTimerHandle);
	}
	if (ADeathCameraActor* Camera = DeathCamera.Get())
	{
		Camera->OnFinished.RemoveAll(this);
	}
	Super::EndPlay(EndPlayReason);
}

bool UPlayerDeathComponent::IsLethalMonster(const AActor* Actor)
{
	const UMonsterAIComponent* MonsterAI = IsValid(Actor) ? Actor->FindComponentByClass<UMonsterAIComponent>() : nullptr;
	return MonsterAI && !MonsterAI->bIsRagdolled;
}

bool UPlayerDeathComponent::AreCapsulesTouching(const UCapsuleComponent& A, const UCapsuleComponent& B, float Tolerance)
{
	float RadiusA = 0.0f;
	float HalfHeightA = 0.0f;
	float RadiusB = 0.0f;
	float HalfHeightB = 0.0f;
	A.GetScaledCapsuleSize(RadiusA, HalfHeightA);
	B.GetScaledCapsuleSize(RadiusB, HalfHeightB);

	const FVector AxisA = A.GetUpVector() * FMath::Max(0.0f, HalfHeightA - RadiusA);
	const FVector AxisB = B.GetUpVector() * FMath::Max(0.0f, HalfHeightB - RadiusB);
	const FVector CenterA = A.GetComponentLocation();
	const FVector CenterB = B.GetComponentLocation();

	FVector ClosestA;
	FVector ClosestB;
	FMath::SegmentDistToSegmentSafe(CenterA - AxisA, CenterA + AxisA, CenterB - AxisB, CenterB + AxisB, ClosestA, ClosestB);
	return FVector::Dist(ClosestA, ClosestB) - RadiusA - RadiusB <= Tolerance;
}

AActor* UPlayerDeathComponent::FindTouchingMonster() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	for (TActorIterator<ACharacter> It(World); It; ++It)
	{
		ACharacter* Candidate = *It;
		if (Candidate == GetOwner() || !IsLethalMonster(Candidate))
		{
			continue;
		}
		if (IsTouchingMonster(Candidate))
		{
			return Candidate;
		}
	}
	return nullptr;
}

bool UPlayerDeathComponent::IsTouchingMonster(const AActor* Monster) const
{
	if (!IsLethalMonster(Monster) || Monster == GetOwner() || Monster->GetWorld() != GetWorld()) { return false; }
	const UBodyContactComponent* PlayerBody = GetOwner()->FindComponentByClass<UBodyContactComponent>();
	const UBodyContactComponent* MonsterBody = Monster->FindComponentByClass<UBodyContactComponent>();
	return PlayerBody && MonsterBody && PlayerBody->HasBodyContactWith(*MonsterBody);
}

void UPlayerDeathComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (bDead)
	{
		SetComponentTickEnabled(false);
		return;
	}

	if (const ALostWithinGameMode* GameMode = GetWorld() ? Cast<ALostWithinGameMode>(GetWorld()->GetAuthGameMode()) : nullptr;
		GameMode && !GameMode->IsRunActive())
	{
		return;
	}

	if (AActor* Monster = FindTouchingMonster())
	{
		BeginDeath(Monster);
	}
}

void UPlayerDeathComponent::HandleCapsuleHit(UPrimitiveComponent* HitComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	if (!bDead && IsLethalMonster(OtherActor))
	{
		BeginDeath(OtherActor);
	}
}

bool UPlayerDeathComponent::BeginDeath(AActor* InKiller)
{
	if (bDead || !IsTouchingMonster(InKiller))
	{
		return false;
	}

	ALostWithinGameMode* GameMode = GetWorld() ? Cast<ALostWithinGameMode>(GetWorld()->GetAuthGameMode()) : nullptr;
	if (GameMode && !GameMode->BeginPlayerDeath())
	{
		return false;
	}

	bDead = true;
	Killer = InKiller;
	SetComponentTickEnabled(false);
	UE_LOG(LogLostWithin, Log, TEXT("%s touched %s: death camera."), *GetNameSafe(GetOwner()), *GetNameSafe(InKiller));

	if (ALostWithinCharacter* Character = Cast<ALostWithinCharacter>(GetOwner()))
	{
		Character->EnterDeathState();
	}
	FreezeKiller(InKiller);
	OnDeathStarted.Broadcast(InKiller);
	StartDeathCamera();
	return true;
}

void UPlayerDeathComponent::FreezeKiller(AActor* InKiller) const
{
	ACharacter* Monster = Cast<ACharacter>(InKiller);
	if (!Monster)
	{
		return;
	}

	if (UMonsterHearingComponent* Hearing = Monster->FindComponentByClass<UMonsterHearingComponent>())
	{
		Hearing->StopInvestigating();
	}
	if (AAIController* Controller = Cast<AAIController>(Monster->GetController()))
	{
		Controller->StopMovement();
	}
	if (UCharacterMovementComponent* Movement = Monster->GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
		Movement->DisableMovement();
	}

	// Face the player so the death camera looks it in the eye.
	if (const AActor* Owner = GetOwner())
	{
		const FVector ToPlayer = (Owner->GetActorLocation() - Monster->GetActorLocation()).GetSafeNormal2D();
		if (!ToPlayer.IsNearlyZero())
		{
			Monster->SetActorRotation(FRotator(0.0f, ToPlayer.Rotation().Yaw, 0.0f));
		}
	}
}

void UPlayerDeathComponent::StartDeathCamera()
{
	APawn* Pawn = Cast<APawn>(GetOwner());
	APlayerController* PlayerController = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	UWorld* World = GetWorld();
	if (!PlayerController || !World)
	{
		FinishDeath();
		return;
	}

	FMinimalViewInfo StartView;
	if (PlayerController->PlayerCameraManager)
	{
		StartView = PlayerController->PlayerCameraManager->GetCameraCacheView();
	}
	else
	{
		StartView.Location = Pawn->GetPawnViewLocation();
		StartView.Rotation = PlayerController->GetControlRotation();
		StartView.FOV = 90.0f;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = Pawn;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	UClass* CameraClass = DeathCameraClass ? DeathCameraClass.Get() : ADeathCameraActor::StaticClass();
	ADeathCameraActor* Camera = World->SpawnActor<ADeathCameraActor>(CameraClass, StartView.Location, StartView.Rotation, SpawnParams);
	if (!Camera)
	{
		UE_LOG(LogLostWithin, Warning, TEXT("Death camera could not spawn; showing the end screen directly."));
		FinishDeath();
		return;
	}

	DeathCamera = Camera;
	Camera->StartSequence(Pawn, Killer.Get(), StartView);
	Camera->OnFinished.AddUObject(this, &UPlayerDeathComponent::FinishDeath);
	PlayerController->SetViewTargetWithBlend(Camera, 0.0f);

	// Safety net in case the camera stops ticking before it finishes.
	World->GetTimerManager().SetTimer(FinishTimerHandle, this, &UPlayerDeathComponent::FinishDeath,
		Camera->GetTotalDuration() + 1.0f, false);
}

void UPlayerDeathComponent::FinishDeath()
{
	if (bFinished)
	{
		return;
	}
	bFinished = true;

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(FinishTimerHandle);
	}

	const APawn* Pawn = Cast<APawn>(GetOwner());
	AController* Controller = Pawn ? Pawn->GetController() : nullptr;
	if (ALostWithinGameMode* GameMode = GetWorld() ? Cast<ALostWithinGameMode>(GetWorld()->GetAuthGameMode()) : nullptr)
	{
		GameMode->HandlePlayerKilled(Controller, Killer.Get());
	}
	else if (ALostWithinPlayerController* PlayerController = Cast<ALostWithinPlayerController>(Controller))
	{
		PlayerController->ShowEndScreen(/*bEscaped=*/false);
	}
}
