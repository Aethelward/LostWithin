// Copyright Epic Games, Inc. All Rights Reserved.

#include "LostWithinCharacter.h"
#include "Animation/AnimInstance.h"
#include "Animation/LostWithinFlashlightAnimInstance.h"
#include "Camera/CameraComponent.h"
#include "Components/AudioComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/ProximityFloatSpinComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StaticMesh.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Gameplay/LostWithinNoiseSubsystem.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "Interaction/InteractableInterface.h"
#include "Interaction/PickupItemActor.h"
#include "Inventory/InventoryComponent.h"
#include "Kismet/GameplayStatics.h"
#include "LostWithin.h"
#include "Player/MonsterRevealSoundComponent.h"
#include "Player/PlayerDeathComponent.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundWave.h"
#include "UI/InventoryWidget.h"
#include "UObject/UObjectIterator.h"

namespace
{
	void PlayFlashlightSwitchSound(AActor* WorldContext, bool bIsOn)
	{
		if (!WorldContext) return;
		
		USoundBase* SwitchOnSound = LoadObject<USoundBase>(nullptr, TEXT("/Game/Item/Flashlight/SFX/Switch/Switch_ON.Switch_ON"));
		USoundBase* SwitchOffSound = LoadObject<USoundBase>(nullptr, TEXT("/Game/Item/Flashlight/SFX/Switch/Switch_OFF.Switch_OFF"));
		
		USoundBase* SoundToPlay = bIsOn ? SwitchOnSound : SwitchOffSound;
		if (SoundToPlay)
		{
			UGameplayStatics::PlaySoundAtLocation(WorldContext, SoundToPlay, WorldContext->GetActorLocation());
		}
	}

	void EnsureSoundArrayLoaded(TArray<TObjectPtr<USoundBase>>& OutSounds, TConstArrayView<const TCHAR*> AssetPaths)
	{
		if (OutSounds.IsEmpty())
		{
			for (const TCHAR* AssetPath : AssetPaths)
			{
				if (USoundBase* LoadedSound = LoadObject<USoundBase>(nullptr, AssetPath))
				{
					OutSounds.AddUnique(LoadedSound);
				}
			}
		}

		// Clear looping flags on loaded waves so one-shot SFX stop cleanly and BGM tracks trigger OnAudioFinished
		for (const TObjectPtr<USoundBase>& Sound : OutSounds)
		{
			if (USoundWave* SoundWave = Cast<USoundWave>(Sound))
			{
				SoundWave->bLooping = false;
			}
		}
	}

	USoundBase* PickRandomSoundNoRepeat(const TArray<TObjectPtr<USoundBase>>& Sounds, int32& InOutLastIndex)
	{
		if (Sounds.IsEmpty())
		{
			return nullptr;
		}

		int32 ChosenIndex = FMath::RandRange(0, Sounds.Num() - 1);
		if (Sounds.Num() > 1 && ChosenIndex == InOutLastIndex)
		{
			ChosenIndex = (ChosenIndex + 1) % Sounds.Num();
		}
		InOutLastIndex = ChosenIndex;
		return Sounds[ChosenIndex];
	}

	UObject* ResolveInteractableOnActor(AActor* Actor)
	{
		if (Actor->GetClass()->ImplementsInterface(UInteractableInterface::StaticClass()))
		{
			return Actor;
		}

		for (UActorComponent* Comp : Actor->GetComponents())
		{
			if (Comp && Comp->GetClass()->ImplementsInterface(UInteractableInterface::StaticClass()))
			{
				return Comp;
			}
		}
		return nullptr;
	}

	UObject* ResolveInteractableFromHit(const FHitResult& Hit)
	{
		if (UPrimitiveComponent* HitComp = Hit.GetComponent())
		{
			if (HitComp->GetClass()->ImplementsInterface(UInteractableInterface::StaticClass()))
			{
				return HitComp;
			}
		}

		// Walk up from child actors (the ScaryDoor's padlock) and attached actors to the actor that owns the interaction.
		constexpr int32 MaxParentDepth = 4;
		AActor* Actor = Hit.GetActor();
		for (int32 Depth = 0; Actor && Depth <= MaxParentDepth; ++Depth)
		{
			if (UObject* Interactable = ResolveInteractableOnActor(Actor))
			{
				return Interactable;
			}
			Actor = Actor->GetParentActor() ? Actor->GetParentActor() : Actor->GetAttachParentActor();
		}

		return nullptr;
	}
}

ALostWithinCharacter::ALostWithinCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(55.f, 96.0f);

	// Create third-person boom and camera
	ThirdPersonCameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("Third Person Camera Boom"));
	ThirdPersonCameraBoom->SetupAttachment(GetCapsuleComponent());
	ThirdPersonCameraBoom->SetRelativeLocation(FVector(0.0f, 0.0f, 60.0f));
	ThirdPersonCameraBoom->TargetArmLength = 200.0f;
	ThirdPersonCameraBoom->SocketOffset = FVector(0.0f, 45.0f, 15.0f);
	ThirdPersonCameraBoom->bUsePawnControlRotation = true;
	ThirdPersonCameraBoom->bDoCollisionTest = true;

	ThirdPersonCameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("Third Person Camera"));
	ThirdPersonCameraComponent->SetupAttachment(ThirdPersonCameraBoom, USpringArmComponent::SocketName);
	ThirdPersonCameraComponent->bUsePawnControlRotation = false;
	ThirdPersonCameraComponent->bAutoActivate = true;

	// Create player inventory component
	InventoryComponent = CreateDefaultSubobject<UInventoryComponent>(TEXT("Inventory Component"));

	// Create held flashlight visual mesh attached to the character's right hand
	FlashlightThirdPersonMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Flashlight Third Person Mesh"));
	FlashlightThirdPersonMesh->SetupAttachment(GetMesh(), FlashlightHandSocket);
	FlashlightThirdPersonMesh->SetRelativeLocationAndRotation(FlashlightAttachRelativeLocation, FlashlightAttachRelativeRotation);
	FlashlightThirdPersonMesh->SetRelativeScale3D(FVector(FlashlightHeldScale));
	FlashlightThirdPersonMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FlashlightThirdPersonMesh->SetOwnerNoSee(false);
	FlashlightThirdPersonMesh->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::None;
	FlashlightThirdPersonMesh->SetCastShadow(true);
	FlashlightThirdPersonMesh->SetVisibility(false);

	// Create tactical flashlight lighting components in world space
	FlashlightSpotLight = CreateDefaultSubobject<USpotLightComponent>(TEXT("Flashlight SpotLight"));
	FlashlightSpotLight->SetupAttachment(GetCapsuleComponent());
	FlashlightSpotLight->SetUsingAbsoluteLocation(true);
	FlashlightSpotLight->SetUsingAbsoluteRotation(true);
	FlashlightSpotLight->SetCastShadows(true);
	FlashlightSpotLight->SetVisibility(false);

	FlashlightFillLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("Flashlight Fill Light"));
	FlashlightFillLight->SetupAttachment(GetCapsuleComponent());
	FlashlightFillLight->SetUsingAbsoluteLocation(true);
	FlashlightFillLight->SetCastShadows(false);
	FlashlightFillLight->SetVisibility(false);

	ApplyFlashlightLightingSettings();

	// Configure the character comps for 3rd-person visibility
	GetMesh()->SetOwnerNoSee(false);
	GetMesh()->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::None;

	GetCapsuleComponent()->SetCapsuleSize(34.0f, 96.0f);

	// Configure character movement (default movement is Run)
	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	MoveComp->BrakingDecelerationFalling = 1500.0f;
	MoveComp->AirControl = 0.5f;
	MoveComp->MaxWalkSpeed = DefaultRunSpeed;
	MoveComp->GetNavAgentPropertiesRef().bCanCrouch = false;

	DeathComponent = CreateDefaultSubobject<UPlayerDeathComponent>(TEXT("Death Component"));
	MonsterRevealSound = CreateDefaultSubobject<UMonsterRevealSoundComponent>(TEXT("Monster Reveal Sound"));

	InventoryWidgetClass = UInventoryWidget::StaticClass();
}

void ALostWithinCharacter::BeginPlay()
{
	Super::BeginPlay();

	if (ThirdPersonCameraComponent)
	{
		ThirdPersonCameraComponent->SetActive(true);
	}

	if (USkeletalMeshComponent* ThirdPersonMesh = GetMesh())
	{
		ThirdPersonMesh->SetOwnerNoSee(false);
		ThirdPersonMesh->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::None;
		ThirdPersonMesh->MarkRenderStateDirty();
	}

	InitializeFlashlightSetup();
	SetupFlashlightAnimationWrapper();

	if (InventoryComponent)
	{
		InventoryComponent->OnInventoryUpdated.AddDynamic(this, &ALostWithinCharacter::HandleInventoryUpdated);
	}

	HandleInventoryUpdated();
	UpdateMovementSpeed();
	EnsureDefaultAudioAssets();
	RegisterRuntimeInputMappingContext();

	if (IsLocallyControlled())
	{
		if (APlayerController* PC = Cast<APlayerController>(GetController()))
		{
			UClass* WidgetClass = InventoryWidgetClass ? InventoryWidgetClass.Get() : UInventoryWidget::StaticClass();
			InventoryWidget = CreateWidget<UInventoryWidget>(PC, WidgetClass);
			if (InventoryWidget)
			{
				InventoryWidget->BindInventory(InventoryComponent);
				InventoryWidget->OnCloseRequested.AddDynamic(this, &ALostWithinCharacter::HandleInventoryCloseRequested);
				InventoryWidget->SetInventoryOpen(bIsInventoryOpen);
				InventoryWidget->AddToViewport(10);
				ApplyInventoryInputState();
				UpdateFlashlightBatteryHUD();
				if (bIsInCinematic)
				{
					InventoryWidget->SetVisibility(ESlateVisibility::Collapsed);
				}
			}
		}

		if (bEnableExplorationBGM && !bIsInCinematic)
		{
			StartExplorationBGM();
		}
	}
	GetWorldTimerManager().SetTimer(FlickerTimerHandle, this, &ALostWithinCharacter::CheckFlashlightFlicker, 0.2f, true);
}

void ALostWithinCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopExplorationBGM();

	if (InventoryComponent)
	{
		InventoryComponent->OnInventoryUpdated.RemoveDynamic(this, &ALostWithinCharacter::HandleInventoryUpdated);
	}

	if (bIsInventoryOpen)
	{
		bIsInventoryOpen = false;
		ApplyInventoryInputState();
	}

	if (InventoryWidget)
	{
		InventoryWidget->OnCloseRequested.RemoveDynamic(this, &ALostWithinCharacter::HandleInventoryCloseRequested);
		InventoryWidget->RemoveFromParent();
		InventoryWidget = nullptr;
	}

	Super::EndPlay(EndPlayReason);
}

void ALostWithinCharacter::PawnClientRestart()
{
	Super::PawnClientRestart();
	RegisterRuntimeInputMappingContext();
}

void ALostWithinCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	const float CurrentAimPitch = FMath::Clamp(FRotator::NormalizeAxis(GetBaseAimRotation().Pitch), -89.0f, 89.0f);
	if (FlashlightAnimInstance)
	{
		FlashlightAnimInstance->SetAimPitch(CurrentAimPitch);
	}

	if (bHasFlashlight && bIsFlashlightOn && InventoryComponent)
	{
		bool bBatteryDepleted = false;
		InventoryComponent->ConsumeFlashlightBattery(DeltaTime, FlashlightBatteryDurationSeconds, bBatteryDepleted);
		if (bBatteryDepleted)
		{
			bIsFlashlightOn = false;
			bFlashlightAimInitialized = false;
			RefreshFlashlightState();
		}
		else
		{
			ApplyFlashlightLightingSettings();
			UpdateFlashlightBatteryHUD();
		}
	}

	UpdateFlashlightTransformAndLighting(DeltaTime);

	if (IsLocallyControlled())
	{
		if (InventoryWidget)
		{
			UpdateInteractionPrompt();
		}
		UpdateExplorationBGM(DeltaTime);
	}

	UpdateFootstepAudio(DeltaTime);
}

void ALostWithinCharacter::Landed(const FHitResult& Hit)
{
	Super::Landed(Hit);
	FootstepTimer = 0.0f;
	PlayRandomSoundAtFeet(LandingSounds, LastLandingSoundIndex, LandingSoundVolume, 0.96f, 1.04f,
		FMath::Max(GetCurrentGaitNoiseRadius(), RunNoiseRadius));
}

void ALostWithinCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	EnsureDefaultInputActionsAndMapping();

	// Set up action bindings
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		// Jumping
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Started, this, &ALostWithinCharacter::DoJumpStart);
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Completed, this, &ALostWithinCharacter::DoJumpEnd);

		// Moving
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ALostWithinCharacter::MoveInput);

		// Looking/Aiming
		EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &ALostWithinCharacter::LookInput);
		EnhancedInputComponent->BindAction(MouseLookAction, ETriggerEvent::Triggered, this, &ALostWithinCharacter::LookInput);

		// Interaction ([LMB]) & Flashlight Toggle ([F])
		if (InteractAction)
		{
			EnhancedInputComponent->BindAction(InteractAction, ETriggerEvent::Started, this, &ALostWithinCharacter::DoInteract);
		}
		if (ToggleFlashlightAction)
		{
			EnhancedInputComponent->BindAction(ToggleFlashlightAction, ETriggerEvent::Started, this, &ALostWithinCharacter::DoToggleFlashlight);
		}
		if (ReloadFlashlightAction)
		{
			EnhancedInputComponent->BindAction(ReloadFlashlightAction, ETriggerEvent::Started, this, &ALostWithinCharacter::DoReloadFlashlight);
		}

		// Inventory UI ([Tab])
		if (ToggleInventoryAction)
		{
			EnhancedInputComponent->BindAction(ToggleInventoryAction, ETriggerEvent::Started, this, &ALostWithinCharacter::DoToggleInventory);
		}

		// Movement states: Walk ([Left Shift]), Sprint ([Left Ctrl])
		if (WalkModeAction)
		{
			EnhancedInputComponent->BindAction(WalkModeAction, ETriggerEvent::Started, this, &ALostWithinCharacter::DoStartWalk);
			EnhancedInputComponent->BindAction(WalkModeAction, ETriggerEvent::Completed, this, &ALostWithinCharacter::DoEndWalk);
		}
		if (SprintModeAction)
		{
			EnhancedInputComponent->BindAction(SprintModeAction, ETriggerEvent::Started, this, &ALostWithinCharacter::DoStartFastSprint);
			EnhancedInputComponent->BindAction(SprintModeAction, ETriggerEvent::Completed, this, &ALostWithinCharacter::DoEndFastSprint);
		}
	}
	else
	{
		UE_LOG(LogLostWithin, Error, TEXT("'%s' Failed to find an Enhanced Input Component! This template is built to use the Enhanced Input system. If you intend to use the legacy system, then you will need to update this C++ file."), *GetNameSafe(this));
	}
}

void ALostWithinCharacter::MoveInput(const FInputActionValue& Value)
{
	// get the Vector2D move axis
	FVector2D MovementVector = Value.Get<FVector2D>();

	// pass the axis values to the move input
	DoMove(MovementVector.X, MovementVector.Y);
}

void ALostWithinCharacter::LookInput(const FInputActionValue& Value)
{
	// get the Vector2D look axis
	FVector2D LookAxisVector = Value.Get<FVector2D>();

	// pass the axis values to the aim input
	DoAim(LookAxisVector.X, LookAxisVector.Y);
}

void ALostWithinCharacter::DoAim(float Yaw, float Pitch)
{
	if (bIsInventoryOpen || IsGameplayInputBlocked())
	{
		return;
	}

	if (GetController())
	{
		// pass the rotation inputs
		AddControllerYawInput(Yaw);
		AddControllerPitchInput(Pitch);
	}
}

void ALostWithinCharacter::DoMove(float Right, float Forward)
{
	if (IsGameplayInputBlocked())
	{
		return;
	}

	if (GetController())
	{
		// pass the move inputs
		AddMovementInput(GetActorRightVector(), Right);
		AddMovementInput(GetActorForwardVector(), Forward);
	}
}

void ALostWithinCharacter::DoJumpStart()
{
	if (IsGameplayInputBlocked())
	{
		return;
	}

	// pass Jump to the character
	Jump();
}

void ALostWithinCharacter::DoJumpEnd()
{
	// pass StopJumping to the character
	StopJumping();
}

void ALostWithinCharacter::DoInteract()
{
	if (bIsInventoryOpen || IsGameplayInputBlocked())
	{
		return;
	}

	// [LMB] first puts away a note that is being read.
	if (IsNoteOpen())
	{
		HideNote();
		return;
	}

	FHitResult HitResult;
	if (UObject* InteractableTarget = FindInteractableInView(HitResult, InteractionDistance))
	{
		if (IInteractableInterface::Execute_CanInteract(InteractableTarget, this))
		{
			IInteractableInterface::Execute_OnInteract(InteractableTarget, this);
		}
	}
}

void ALostWithinCharacter::DoToggleFlashlight()
{
	if (bIsInventoryOpen || bIsDead || IsGameplayInputBlocked() || !bHasFlashlight)
	{
		return;
	}

	const bool bDesiredOn = !bIsFlashlightOn;
	if (bDesiredOn && (!InventoryComponent || !InventoryComponent->CanOperateFlashlight()))
	{
		bIsFlashlightOn = false;
		bFlashlightAimInitialized = false;
		if (InventoryComponent)
		{
			InventoryComponent->SetFlashlightOnState(false);
		}
		RefreshFlashlightState();
		return;
	}

	bIsFlashlightOn = bDesiredOn;
	if (!bIsFlashlightOn)
	{
		bFlashlightAimInitialized = false;
	}
	if (InventoryComponent)
	{
		InventoryComponent->SetFlashlightOnState(bIsFlashlightOn);
	}
	
	if (IsLocallyControlled())
	{
		PlayFlashlightSwitchSound(this, bIsFlashlightOn);
	}
	
	RefreshFlashlightState();
}

void ALostWithinCharacter::DoReloadFlashlight()
{
	if (bIsInventoryOpen || bIsDead || IsGameplayInputBlocked() || !bHasFlashlight || !InventoryComponent)
	{
		return;
	}

	if (InventoryComponent->ReloadBestBatteryIntoFlashlight())
	{
		PlayPickupAnimation();
	}
}

void ALostWithinCharacter::PlayPickupAnimation()
{
	if (FlashlightAnimInstance)
	{
		FlashlightAnimInstance->PlayPickupAnimation();
	}
}

void ALostWithinCharacter::DoToggleInventory()
{
	if (!bIsInventoryOpen && IsGameplayInputBlocked())
	{
		return;
	}

	bIsInventoryOpen = !bIsInventoryOpen;
	if (InventoryWidget)
	{
		InventoryWidget->SetInventoryOpen(bIsInventoryOpen);
	}
	ApplyInventoryInputState();
}

void ALostWithinCharacter::HandleInventoryCloseRequested()
{
	if (bIsInventoryOpen)
	{
		DoToggleInventory();
	}
}

void ALostWithinCharacter::ApplyInventoryInputState()
{
	if (!IsLocallyControlled())
	{
		return;
	}

	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC)
	{
		return;
	}

	if (bIsInventoryOpen)
	{
		PC->SetShowMouseCursor(true);

		FInputModeGameAndUI InputMode;
		if (InventoryWidget)
		{
			InputMode.SetWidgetToFocus(InventoryWidget->TakeWidget());
		}
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		InputMode.SetHideCursorDuringCapture(false);
		PC->SetInputMode(InputMode);
	}
	else
	{
		PC->SetShowMouseCursor(false);
		PC->SetInputMode(FInputModeGameOnly());
	}
}

void ALostWithinCharacter::DoStartWalk()
{
	if (IsGameplayInputBlocked())
	{
		return;
	}
	bIsWalking = true;
	UpdateMovementSpeed();
}

void ALostWithinCharacter::DoEndWalk()
{
	bIsWalking = false;
	UpdateMovementSpeed();
}

void ALostWithinCharacter::DoStartFastSprint()
{
	if (IsGameplayInputBlocked())
	{
		return;
	}
	bIsSprintingHeld = true;
	UpdateMovementSpeed();
}

void ALostWithinCharacter::DoEndFastSprint()
{
	bIsSprintingHeld = false;
	UpdateMovementSpeed();
}

UCameraComponent* ALostWithinCharacter::GetActiveGameplayCamera() const
{
	return ThirdPersonCameraComponent;
}

void ALostWithinCharacter::EnsureDefaultInputActionsAndMapping()
{
	if (!RuntimeExtraMappingContext)
	{
		RuntimeExtraMappingContext = NewObject<UInputMappingContext>(this, TEXT("RuntimeExtraMappingContext"));
	}

	auto EnsureActionAndKey = [this](TObjectPtr<UInputAction>& ActionPtr, const FName ActionName, const FKey& Key)
	{
		if (!ActionPtr)
		{
			UInputAction* NewAction = NewObject<UInputAction>(this, ActionName);
			NewAction->ValueType = EInputActionValueType::Boolean;
			ActionPtr = NewAction;
			RuntimeExtraMappingContext->MapKey(NewAction, Key);
		}
	};

	EnsureActionAndKey(InteractAction, TEXT("IA_RuntimeInteract"), EKeys::LeftMouseButton);
	EnsureActionAndKey(ToggleFlashlightAction, TEXT("IA_RuntimeToggleFlashlight"), EKeys::F);
	EnsureActionAndKey(ReloadFlashlightAction, TEXT("IA_RuntimeReloadFlashlight"), EKeys::R);
	EnsureActionAndKey(ToggleInventoryAction, TEXT("IA_RuntimeToggleInventory"), EKeys::Tab);
	EnsureActionAndKey(WalkModeAction, TEXT("IA_RuntimeWalk"), EKeys::LeftShift);
	EnsureActionAndKey(SprintModeAction, TEXT("IA_RuntimeSprint"), EKeys::LeftControl);
}

void ALostWithinCharacter::RegisterRuntimeInputMappingContext()
{
	if (!IsLocallyControlled())
	{
		return;
	}

	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC)
	{
		return;
	}

	EnsureDefaultInputActionsAndMapping();

	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
	{
		if (RuntimeExtraMappingContext && !Subsystem->HasMappingContext(RuntimeExtraMappingContext))
		{
			// Priority -1 lets variant-specific mapping contexts (like IMC_Horror / IMC_Weapons at priority 0) take precedence when present
			Subsystem->AddMappingContext(RuntimeExtraMappingContext, -1);
		}
	}
}

void ALostWithinCharacter::SetupFlashlightAnimationWrapper()
{
	USkeletalMeshComponent* ThirdPersonMesh = GetMesh();
	if (!ThirdPersonMesh)
	{
		return;
	}

	ThirdPersonMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;

	UClass* UnarmedBaseClass = LoadClass<UAnimInstance>(nullptr, TEXT("/Game/Characters/Mannequins/Anims/Unarmed/ABP_Unarmed.ABP_Unarmed_C"));
	UClass* CurrentAnimClass = ThirdPersonMesh->GetAnimClass();
	UClass* BaseAnimClass = (CurrentAnimClass && CurrentAnimClass != ULostWithinFlashlightAnimInstance::StaticClass())
		? CurrentAnimClass
		: UnarmedBaseClass;

	if (!BaseAnimClass || BaseAnimClass == ULostWithinFlashlightAnimInstance::StaticClass())
	{
		FlashlightAnimInstance = Cast<ULostWithinFlashlightAnimInstance>(ThirdPersonMesh->GetAnimInstance());
		return;
	}

	const float InitialAimPitch = FMath::Clamp(FRotator::NormalizeAxis(GetBaseAimRotation().Pitch), -89.0f, 89.0f);
	ThirdPersonMesh->SetAnimInstanceClass(ULostWithinFlashlightAnimInstance::StaticClass());
	FlashlightAnimInstance = Cast<ULostWithinFlashlightAnimInstance>(ThirdPersonMesh->GetAnimInstance());
	if (FlashlightAnimInstance)
	{
		FlashlightAnimInstance->SetBaseAnimClass(BaseAnimClass);
		FlashlightAnimInstance->SetHasFlashlight(bHasFlashlight);
		FlashlightAnimInstance->SetAimPitch(InitialAimPitch);
	}
}

FVector ALostWithinCharacter::GetHeldFlashlightLensLocation() const
{
	// Clearance between the lens and the beam origin, so the spotlight is not buried in the lens geometry.
	constexpr float HeldFlashlightLensClearanceCm = 4.0f;

	if (!FlashlightThirdPersonMesh)
	{
		return GetPawnViewLocation();
	}

	// The lens is the mesh's +X end (the pickup uses the same axis), measured from its bounds at the current scale.
	FVector LensLocal = FVector::ZeroVector;
	if (const UStaticMesh* HeldMesh = FlashlightThirdPersonMesh->GetStaticMesh())
	{
		const FBox LocalBounds = HeldMesh->GetBoundingBox();
		LensLocal = FVector(LocalBounds.Max.X, LocalBounds.GetCenter().Y, LocalBounds.GetCenter().Z);
	}
	return FlashlightThirdPersonMesh->GetComponentTransform().TransformPosition(LensLocal)
		+ FlashlightThirdPersonMesh->GetForwardVector() * HeldFlashlightLensClearanceCm;
}

void ALostWithinCharacter::InitializeFlashlightSetup()
{
	UStaticMesh* FlashlightMesh = LoadObject<UStaticMesh>(nullptr, APickupItemActor::FlashlightMeshAssetPath);

	if (FlashlightThirdPersonMesh)
	{
		if (USkeletalMeshComponent* CharacterMesh = GetMesh())
		{
			FlashlightThirdPersonMesh->AttachToComponent(CharacterMesh, FAttachmentTransformRules::SnapToTargetNotIncludingScale, FlashlightHandSocket);
		}

		if (FlashlightMesh)
		{
			FlashlightThirdPersonMesh->SetStaticMesh(FlashlightMesh);
		}

		FlashlightThirdPersonMesh->SetRelativeLocationAndRotation(FlashlightAttachRelativeLocation, FlashlightAttachRelativeRotation);
		FlashlightThirdPersonMesh->SetRelativeScale3D(FVector(FlashlightHeldScale));
	}

	ApplyFlashlightLightingSettings();
}

float ALostWithinCharacter::GetFlashlightBatteryPercent() const
{
	if (!bHasFlashlight || !InventoryComponent)
	{
		return 0.0f;
	}

	int32 InstalledCount = 0;
	float CombinedPercent = 0.0f;
	float Slot1 = 0.0f;
	float Slot2 = 0.0f;
	InventoryComponent->GetFlashlightBatteryState(InstalledCount, CombinedPercent, Slot1, Slot2);
	return CombinedPercent;
}

int32 ALostWithinCharacter::GetFlashlightBatteryCount() const
{
	if (!bHasFlashlight || !InventoryComponent)
	{
		return 0;
	}

	int32 InstalledCount = 0;
	float CombinedPercent = 0.0f;
	float Slot1 = 0.0f;
	float Slot2 = 0.0f;
	InventoryComponent->GetFlashlightBatteryState(InstalledCount, CombinedPercent, Slot1, Slot2);
	return InstalledCount;
}

void ALostWithinCharacter::ApplyFlashlightLightingSettings()
{
	FLostWithinFlashlightLightParams BaseParams;
	BaseParams.SpotIntensity = FlashlightSpotIntensity;
	BaseParams.FillIntensity = FlashlightFillIntensity;
	BaseParams.AttenuationRadius = FlashlightAttenuationRadius;
	BaseParams.InnerConeAngle = FlashlightInnerConeAngle;
	BaseParams.OuterConeAngle = FlashlightOuterConeAngle;
	BaseParams.LightColor = FlashlightLightColor;

	float EffectivePercent = 25.0f;
	if (bHasFlashlight && InventoryComponent)
	{
		int32 InstalledCount = 0;
		float CombinedPercent = 0.0f;
		float Slot1 = 0.0f;
		float Slot2 = 0.0f;
		InventoryComponent->GetFlashlightBatteryState(InstalledCount, CombinedPercent, Slot1, Slot2);
		EffectivePercent = (InstalledCount == 2) ? CombinedPercent : 0.0f;
	}

	const FLostWithinFlashlightLightParams ScaledParams = APickupItemActor::ComputeEffectiveFlashlightLightParams(
		BaseParams,
		EffectivePercent);
	APickupItemActor::ApplyFlashlightLightingParams(FlashlightSpotLight, FlashlightFillLight, ScaledParams);
}

#if WITH_EDITOR
void ALostWithinCharacter::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	ApplyFlashlightLightingSettings();
}
#endif

void ALostWithinCharacter::HandleInventoryUpdated()
{
	bHasFlashlight = InventoryComponent && InventoryComponent->HasItem(APickupItemActor::FlashlightItemID);

	if (!bHasFlashlight || !InventoryComponent || !InventoryComponent->CanOperateFlashlight())
	{
		bIsFlashlightOn = false;
		bFlashlightAimInitialized = false;
		if (bHasFlashlight && InventoryComponent)
		{
			InventoryComponent->SetFlashlightOnState(false);
		}
	}
	else
	{
		bIsFlashlightOn = InventoryComponent->GetFlashlightOnState();
		if (!bIsFlashlightOn)
		{
			bFlashlightAimInitialized = false;
		}
	}

	if (FlashlightAnimInstance)
	{
		FlashlightAnimInstance->SetHasFlashlight(bHasFlashlight);
	}

	RefreshFlashlightState();
}

void ALostWithinCharacter::RefreshFlashlightState()
{
	ApplyFlashlightLightingSettings();

	if (FlashlightThirdPersonMesh)
	{
		FlashlightThirdPersonMesh->SetOwnerNoSee(false);
		FlashlightThirdPersonMesh->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::None;
		FlashlightThirdPersonMesh->SetVisibility(bHasFlashlight, false);
		FlashlightThirdPersonMesh->MarkRenderStateDirty();
	}

	const bool bLightActive = bHasFlashlight && bIsFlashlightOn && InventoryComponent && InventoryComponent->CanOperateFlashlight();
	if (FlashlightSpotLight)
	{
		FlashlightSpotLight->SetVisibility(bLightActive);
	}
	if (FlashlightFillLight)
	{
		FlashlightFillLight->SetVisibility(bLightActive);
	}

	if (bLightActive)
	{
		UpdateFlashlightTransformAndLighting(0.0f);
	}

	UpdateFlashlightBatteryHUD();
}

void ALostWithinCharacter::UpdateFlashlightBatteryHUD()
{
	if (!InventoryWidget)
	{
		return;
	}

	int32 InstalledCount = 0;
	float CombinedPercent = 0.0f;
	float Slot1Percent = FInventoryItem::EmptyBatterySlotValue;
	float Slot2Percent = FInventoryItem::EmptyBatterySlotValue;
	if (bHasFlashlight && InventoryComponent)
	{
		InventoryComponent->GetFlashlightBatteryState(InstalledCount, CombinedPercent, Slot1Percent, Slot2Percent);
	}

	InventoryWidget->UpdateFlashlightBatteryHUD(
		bHasFlashlight,
		bIsFlashlightOn,
		InstalledCount,
		CombinedPercent,
		Slot1Percent,
		Slot2Percent);
}

void ALostWithinCharacter::UpdateFlashlightTransformAndLighting(float DeltaTime)
{
	if (!bHasFlashlight || !bIsFlashlightOn || !FlashlightSpotLight)
	{
		return;
	}

	const UCameraComponent* ActiveCamera = GetActiveGameplayCamera();
	if (!ActiveCamera)
	{
		return;
	}

	const FVector CameraLocation = ActiveCamera->GetComponentLocation();
	const FVector CameraForward = ActiveCamera->GetForwardVector().GetSafeNormal();

	FVector TargetPoint = CameraLocation + CameraForward * FlashlightConvergenceDistance;
	if (UWorld* World = GetWorld())
	{
		FHitResult AimHit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(FlashlightAimTrace), false, this);
		if (World->LineTraceSingleByChannel(AimHit, CameraLocation, TargetPoint, ECC_Visibility, Params) && AimHit.Distance > 80.0f)
		{
			TargetPoint = AimHit.ImpactPoint;
		}
	}

	FVector MuzzleOrigin;
	if (FlashlightThirdPersonMesh)
	{
		MuzzleOrigin = GetHeldFlashlightLensLocation();
	}
	else if (GetMesh() && GetMesh()->DoesSocketExist(FlashlightHandSocket))
	{
		MuzzleOrigin = GetMesh()->GetSocketLocation(FlashlightHandSocket) + CameraForward * 22.0f;
	}
	else
	{
		MuzzleOrigin = GetPawnViewLocation() + CameraForward * 30.0f;
	}

	const FVector DesiredDirection = (TargetPoint - MuzzleOrigin).GetSafeNormal();
	const FRotator DesiredRotation = DesiredDirection.IsNearlyZero()
		? ActiveCamera->GetComponentRotation()
		: DesiredDirection.Rotation();

	FRotator FinalRotation = DesiredRotation;
	if (bFlashlightAimInitialized && DeltaTime > 0.0f && FlashlightAimInterpSpeed > 0.0f)
	{
		FinalRotation = FMath::RInterpTo(FlashlightSpotLight->GetComponentRotation(), DesiredRotation, DeltaTime, FlashlightAimInterpSpeed);
	}
	else
	{
		bFlashlightAimInitialized = true;
	}

	FlashlightSpotLight->SetWorldLocationAndRotation(MuzzleOrigin, FinalRotation);

	if (FlashlightFillLight)
	{
		FlashlightFillLight->SetWorldLocation(MuzzleOrigin + FinalRotation.Vector() * 6.0f);
	}
}

void ALostWithinCharacter::CheckFlashlightFlicker()
{
	if (!bIsFlashlightOn || !FlashlightSpotLight)
	{
		return;
	}

	const float Percent = GetFlashlightBatteryPercent();
	if (Percent <= 25.0f)
	{
		const float Chance = FMath::Clamp((25.0f - Percent) * 4.0f, 0.0f, 100.0f);
		if (FMath::FRand() * 100.0f < Chance)
		{
			StartFlickerSequence();
		}
	}
}

void ALostWithinCharacter::StartFlickerSequence()
{
	const int32 NumFlickers = FMath::RandRange(3, 8);
	float Delay = 0.0f;
	for (int32 i = 0; i < NumFlickers; ++i)
	{
		FTimerHandle UnusedHandle;
		GetWorldTimerManager().SetTimer(UnusedHandle, [this]()
		{
			if (FlashlightSpotLight)
			{
				const bool bIsVisible = FlashlightSpotLight->IsVisible();
				FlashlightSpotLight->SetVisibility(!bIsVisible);
				if (FlashlightFillLight)
				{
					FlashlightFillLight->SetVisibility(!bIsVisible);
				}
			}
		}, Delay, false);
		Delay += FMath::FRandRange(0.03f, 0.07f);
	}

	// Ensure light is back on at the end
	FTimerHandle EndHandle;
	GetWorldTimerManager().SetTimer(EndHandle, [this]()
	{
		if (bIsFlashlightOn && FlashlightSpotLight)
		{
			FlashlightSpotLight->SetVisibility(true);
			if (FlashlightFillLight)
			{
				FlashlightFillLight->SetVisibility(true);
			}
		}
	}, Delay, false);
}

void ALostWithinCharacter::UpdateMovementSpeed()
{
	if (!bUseCustomMovementSpeeds)
	{
		return;
	}

	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	if (!MoveComp)
	{
		return;
	}

	if (bIsWalking)
	{
		MoveComp->MaxWalkSpeed = SlowWalkSpeed;
	}
	else if (bIsSprintingHeld)
	{
		MoveComp->MaxWalkSpeed = FastSprintSpeed;
	}
	else
	{
		MoveComp->MaxWalkSpeed = DefaultRunSpeed;
	}
}

UObject* ALostWithinCharacter::FindInteractableInView(FHitResult& OutHit, float MaxTraceDistance) const
{
	UWorld* World = GetWorld();
	const UCameraComponent* ActiveCamera = GetActiveGameplayCamera();
	if (!World || !ActiveCamera)
	{
		return nullptr;
	}

	const FVector CameraLocation = ActiveCamera->GetComponentLocation();
	const FVector CameraForward = ActiveCamera->GetForwardVector();
	const float ForwardOffset = FMath::Max(0.0f, FVector::DotProduct(GetPawnViewLocation() - CameraLocation, CameraForward));

	const FVector TraceStart = CameraLocation + CameraForward * ForwardOffset;
	const FVector TraceEnd = TraceStart + CameraForward * MaxTraceDistance;

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(LostWithinInteractionTrace), false, this);

	// 1. Exact line trace first for pinpoint accuracy
	FHitResult LineHit;
	const bool bLineBlocked = World->LineTraceSingleByChannel(LineHit, TraceStart, TraceEnd, ECC_Visibility, QueryParams);
	if (bLineBlocked)
	{
		if (UObject* DirectTarget = ResolveInteractableFromHit(LineHit))
		{
			OutHit = LineHit;
			return DirectTarget;
		}
	}

	// 2. Assisted sphere sweep capped by any blocking surface from the line trace so we never reach through walls
	const float SweepDistance = bLineBlocked ? LineHit.Distance : MaxTraceDistance;
	const FVector SweepEnd = TraceStart + CameraForward * SweepDistance;
	FHitResult SweepHit;
	if (World->SweepSingleByChannel(
		SweepHit,
		TraceStart,
		SweepEnd,
		FQuat::Identity,
		ECC_Visibility,
		FCollisionShape::MakeSphere(InteractionTraceRadius),
		QueryParams))
	{
		OutHit = SweepHit;
		return ResolveInteractableFromHit(SweepHit);
	}

	if (bLineBlocked)
	{
		OutHit = LineHit;
	}
	return nullptr;
}

void ALostWithinCharacter::UpdateInteractionPrompt()
{
	if (!InventoryWidget)
	{
		return;
	}

	if (bIsInventoryOpen || bIsDead || bIsInCinematic)
	{
		InventoryWidget->SetInteractionPrompt(FText::GetEmpty());
		return;
	}

	if (IsGameplayInputBlocked())
	{
		// The ScaryDoor padlock has the camera; its own input turns the dials.
		InventoryWidget->SetInteractionPrompt(NSLOCTEXT("LostWithin", "PadlockControlsPrompt",
			"Point at a dial  |  [Mouse Wheel] Turn it  |  [E] Step back"));
		return;
	}

	if (IsNoteOpen())
	{
		InventoryWidget->SetInteractionPrompt(NSLOCTEXT("LostWithin", "CloseNotePrompt", "[LMB] Put the note away"));
		return;
	}

	FHitResult HitResult;
	UObject* InteractableTarget = FindInteractableInView(HitResult, InteractionDistance);
	if (InteractableTarget && IInteractableInterface::Execute_CanInteract(InteractableTarget, this))
	{
		InventoryWidget->SetInteractionPrompt(IInteractableInterface::Execute_GetInteractionPrompt(InteractableTarget));
	}
	else
	{
		InventoryWidget->SetInteractionPrompt(FText::GetEmpty());
	}
}

void ALostWithinCharacter::EnsureDefaultAudioAssets()
{
	static constexpr const TCHAR* DefaultStepPaths[] = {
		TEXT("/Game/Audio/SFX/Character/Steps/Step_01.Step_01"),
		TEXT("/Game/Audio/SFX/Character/Steps/Step_02.Step_02"),
		TEXT("/Game/Audio/SFX/Character/Steps/Step_03.Step_03"),
		TEXT("/Game/Audio/SFX/Character/Steps/Step_04.Step_04")
	};

	static constexpr const TCHAR* DefaultLandingPaths[] = {
		TEXT("/Game/Audio/SFX/Character/Landing/Landing_01.Landing_01"),
		TEXT("/Game/Audio/SFX/Character/Landing/Landing_02.Landing_02"),
		TEXT("/Game/Audio/SFX/Character/Landing/Landing_03.Landing_03"),
		TEXT("/Game/Audio/SFX/Character/Landing/Landing_04.Landing_04")
	};

	static constexpr const TCHAR* DefaultBGMPaths[] = {
		TEXT("/Game/Audio/BGM/01_Exploration/BGM_01.BGM_01"),
		TEXT("/Game/Audio/BGM/01_Exploration/BGM_02.BGM_02"),
		TEXT("/Game/Audio/BGM/01_Exploration/BGM_03.BGM_03")
	};

	EnsureSoundArrayLoaded(FootstepSounds, DefaultStepPaths);
	EnsureSoundArrayLoaded(LandingSounds, DefaultLandingPaths);
	EnsureSoundArrayLoaded(ExplorationBGMTracks, DefaultBGMPaths);
}

void ALostWithinCharacter::UpdateFootstepAudio(float DeltaTime)
{
	const UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	if (!MoveComp || !MoveComp->IsMovingOnGround() || FootstepSounds.IsEmpty())
	{
		FootstepTimer = 0.0f;
		return;
	}

	const float Speed2D = GetVelocity().Size2D();
	if (Speed2D <= 30.0f)
	{
		FootstepTimer = 0.0f;
		return;
	}

	float StepInterval = RunStepInterval;
	float StepVolume = RunStepVolume;

	const int32 Gait = ResolveFootstepGait();
	if (Gait == 2)
	{
		StepInterval = SprintStepInterval;
		StepVolume = SprintStepVolume;
	}
	else if (Gait == 0)
	{
		StepInterval = WalkStepInterval;
		StepVolume = WalkStepVolume;
	}
	const float NoiseRadius = GetCurrentGaitNoiseRadius();

	const float SafeInterval = FMath::Max(0.05f, StepInterval);
	FootstepTimer += DeltaTime;
	if (FootstepTimer >= SafeInterval)
	{
		FootstepTimer = FMath::Fmod(FootstepTimer, SafeInterval);
		PlayRandomSoundAtFeet(FootstepSounds, LastFootstepSoundIndex, StepVolume, 0.95f, 1.05f, NoiseRadius);
	}
}

void ALostWithinCharacter::PlayRandomSoundAtFeet(const TArray<TObjectPtr<USoundBase>>& Sounds, int32& InOutLastIndex, float VolumeMultiplier, float MinPitch, float MaxPitch, float NoiseRadius)
{
	if (VolumeMultiplier <= 0.0f)
	{
		return;
	}

	if (USoundBase* SelectedSound = PickRandomSoundNoRepeat(Sounds, InOutLastIndex))
	{
		const float HalfHeight = GetCapsuleComponent() ? GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 88.0f;
		const FVector FeetLocation = GetActorLocation() - FVector(0.0f, 0.0f, HalfHeight);
		UGameplayStatics::PlaySoundAtLocation(
			this,
			SelectedSound,
			FeetLocation,
			VolumeMultiplier,
			FMath::FRandRange(MinPitch, MaxPitch));

		ULostWithinNoiseSubsystem::ReportNoise(this, FeetLocation, NoiseRadius, ELostWithinNoiseKind::Footstep, this);
	}
}

float ALostWithinCharacter::GetEffectiveExplorationBGMVolume() const
{
	return (ExplorationBGMVolume > 0.0f)
		? FMath::Max(0.001f, ExplorationBGMVolume * CurrentBGMVolumeFactor)
		: 0.0f;
}

void ALostWithinCharacter::StartExplorationBGM()
{
	EnsureDefaultAudioAssets();
	if (!bEnableExplorationBGM || ExplorationBGMTracks.IsEmpty())
	{
		return;
	}

	StopExplorationBGM();

	UAudioComponent* BGMComp = NewObject<UAudioComponent>(this);
	if (!BGMComp)
	{
		return;
	}

	BGMComp->bAutoActivate = false;
	BGMComp->bAutoDestroy = false;
	BGMComp->bStopWhenOwnerDestroyed = true;
	BGMComp->bAllowSpatialization = false;
	BGMComp->bIsUISound = true;
	BGMComp->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
	BGMComp->RegisterComponent();
	BGMComp->OnAudioFinished.AddDynamic(this, &ALostWithinCharacter::HandleExplorationBGMFinished);

	ExplorationBGMComponent = BGMComp;
	CurrentBGMTrackIndex = 0;
	CurrentBGMVolumeFactor = 1.0f;
	bBGMPausedForCatSpin = false;
	bSuppressBGMFinishedAdvance = false;

	PlayCurrentExplorationBGMTrack();
}

void ALostWithinCharacter::StopExplorationBGM()
{
	bSuppressBGMFinishedAdvance = true;
	bBGMPausedForCatSpin = false;

	if (ExplorationBGMComponent)
	{
		ExplorationBGMComponent->OnAudioFinished.RemoveDynamic(this, &ALostWithinCharacter::HandleExplorationBGMFinished);
		ExplorationBGMComponent->Stop();
		ExplorationBGMComponent->DestroyComponent();
		ExplorationBGMComponent = nullptr;
	}
}

void ALostWithinCharacter::UpdateExplorationBGM(float DeltaTime)
{
	const UWorld* World = GetWorld();
	if (!ExplorationBGMComponent || !bEnableExplorationBGM || !World)
	{
		return;
	}

	bool bAnySpinMusicActive = false;
	for (TObjectIterator<UProximityFloatSpinComponent> It; It; ++It)
	{
		const UProximityFloatSpinComponent* SpinComp = *It;
		if (SpinComp && SpinComp->GetWorld() == World && SpinComp->IsSpinMusicActive())
		{
			bAnySpinMusicActive = true;
			break;
		}
	}

	const float TargetFactor = bAnySpinMusicActive ? 0.0f : 1.0f;
	const float PreviousFactor = CurrentBGMVolumeFactor;
	CurrentBGMVolumeFactor = FMath::FInterpTo(CurrentBGMVolumeFactor, TargetFactor, DeltaTime, ExplorationBGMFadeSpeed);
	if (FMath::IsNearlyEqual(CurrentBGMVolumeFactor, TargetFactor, 0.002f))
	{
		CurrentBGMVolumeFactor = TargetFactor;
	}

	if (bAnySpinMusicActive)
	{
		if (CurrentBGMVolumeFactor <= 0.01f)
		{
			CurrentBGMVolumeFactor = 0.0f;
			if (!bBGMPausedForCatSpin)
			{
				ExplorationBGMComponent->SetPaused(true);
				bBGMPausedForCatSpin = true;
			}
		}
	}
	else if (bBGMPausedForCatSpin)
	{
		bBGMPausedForCatSpin = false;
		if (ExplorationBGMComponent->IsPlaying())
		{
			ExplorationBGMComponent->SetPaused(false);
		}
		else
		{
			PlayCurrentExplorationBGMTrack();
		}
	}

	if (!FMath::IsNearlyEqual(CurrentBGMVolumeFactor, PreviousFactor, KINDA_SMALL_NUMBER))
	{
		ExplorationBGMComponent->SetVolumeMultiplier(GetEffectiveExplorationBGMVolume());
	}
}

void ALostWithinCharacter::PlayCurrentExplorationBGMTrack()
{
	if (!ExplorationBGMComponent || ExplorationBGMTracks.IsEmpty())
	{
		return;
	}

	const int32 TrackCount = ExplorationBGMTracks.Num();
	USoundBase* SelectedTrack = nullptr;
	for (int32 Offset = 0; Offset < TrackCount; ++Offset)
	{
		const int32 CandidateIndex = (CurrentBGMTrackIndex + Offset) % TrackCount;
		if (USoundBase* Candidate = ExplorationBGMTracks[CandidateIndex])
		{
			CurrentBGMTrackIndex = CandidateIndex;
			SelectedTrack = Candidate;
			break;
		}
	}

	if (!SelectedTrack)
	{
		return;
	}

	bSuppressBGMFinishedAdvance = true;
	ExplorationBGMComponent->SetSound(SelectedTrack);
	ExplorationBGMComponent->SetVolumeMultiplier(GetEffectiveExplorationBGMVolume());
	bSuppressBGMFinishedAdvance = false;

	ExplorationBGMComponent->Play(0.0f);
	if (bBGMPausedForCatSpin)
	{
		ExplorationBGMComponent->SetPaused(true);
	}
}

void ALostWithinCharacter::HandleExplorationBGMFinished()
{
	if (bSuppressBGMFinishedAdvance || !bEnableExplorationBGM || bBGMPausedForCatSpin || ExplorationBGMTracks.IsEmpty())
	{
		return;
	}

	CurrentBGMTrackIndex = (CurrentBGMTrackIndex + 1) % ExplorationBGMTracks.Num();
	PlayCurrentExplorationBGMTrack();
}

int32 ALostWithinCharacter::ResolveFootstepGait() const
{
	if (bIsCrouched)
	{
		return 0;
	}
	const float Speed2D = GetVelocity().Size2D();
	const bool bIsSprintGait = (bUseCustomMovementSpeeds && bIsSprintingHeld && !bIsWalking)
		|| (!bUseCustomMovementSpeeds && Speed2D > 400.0f);
	if (bIsSprintGait)
	{
		return 2;
	}
	if (bIsWalking || Speed2D <= (SlowWalkSpeed + 25.0f))
	{
		return 0;
	}
	return 1;
}

float ALostWithinCharacter::GetCurrentGaitNoiseRadius() const
{
	if (bIsCrouched)
	{
		return CrouchNoiseRadius;
	}
	switch (ResolveFootstepGait())
	{
	case 0:
		return WalkNoiseRadius;
	case 2:
		return SprintNoiseRadius;
	default:
		return RunNoiseRadius;
	}
}

bool ALostWithinCharacter::IsGameplayInputBlocked() const
{
	if (bIsDead || bIsInCinematic)
	{
		return true;
	}
	const APlayerController* PC = Cast<APlayerController>(GetController());
	return PC && PC->GetViewTarget() && PC->GetViewTarget() != this;
}

void ALostWithinCharacter::EnterCinematicState()
{
	if (bIsInCinematic)
	{
		return;
	}
	bIsInCinematic = true;

	// Temporarily hide the character model and all attached parts so the player is not visible standing beside the bed
	SetActorHiddenInGame(true);

	if (bIsInventoryOpen)
	{
		bIsInventoryOpen = false;
		if (InventoryWidget)
		{
			InventoryWidget->SetInventoryOpen(false);
		}
		ApplyInventoryInputState();
	}
	if (InventoryWidget)
	{
		InventoryWidget->HideNote();
		InventoryWidget->SetInteractionPrompt(FText::GetEmpty());
	}
	SetHUDVisible(false);

	bIsWalking = false;
	bIsSprintingHeld = false;
	StopJumping();
	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->StopMovementImmediately();
	}
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		PC->SetIgnoreMoveInput(true);
		PC->SetIgnoreLookInput(true);
	}
}

void ALostWithinCharacter::ExitCinematicState()
{
	if (!bIsInCinematic)
	{
		return;
	}
	bIsInCinematic = false;

	// Restore character model visibility now that the cutscene has ended
	SetActorHiddenInGame(false);

	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		PC->ResetIgnoreMoveInput();
		PC->ResetIgnoreLookInput();
	}

	SetHUDVisible(true);

	if (bEnableExplorationBGM && !ExplorationBGMComponent && IsLocallyControlled())
	{
		StartExplorationBGM();
	}
}

void ALostWithinCharacter::SetHUDVisible(bool bVisible)
{
	bIsHUDVisible = bVisible;
	if (InventoryWidget)
	{
		InventoryWidget->SetVisibility(bVisible ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}
}

bool ALostWithinCharacter::IsHUDVisible() const
{
	if (InventoryWidget)
	{
		return InventoryWidget->GetVisibility() == ESlateVisibility::SelfHitTestInvisible
			|| InventoryWidget->GetVisibility() == ESlateVisibility::Visible;
	}
	return bIsHUDVisible;
}

void ALostWithinCharacter::EnterDeathState()
{
	if (bIsDead)
	{
		return;
	}
	bIsDead = true;
	if (MonsterRevealSound)
	{
		MonsterRevealSound->StopReveal();
	}
	StopExplorationBGM();

	if (bIsInventoryOpen)
	{
		bIsInventoryOpen = false;
		if (InventoryWidget)
		{
			InventoryWidget->SetInventoryOpen(false);
		}
		ApplyInventoryInputState();
	}
	if (InventoryWidget)
	{
		InventoryWidget->HideNote();
		InventoryWidget->SetInteractionPrompt(FText::GetEmpty());
	}
	SetHUDVisible(false);

	bIsWalking = false;
	bIsSprintingHeld = false;
	StopJumping();
	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->StopMovementImmediately();
		MoveComp->DisableMovement();
	}
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		PC->SetIgnoreMoveInput(true);
		PC->SetIgnoreLookInput(true);
	}
}

void ALostWithinCharacter::ShowNote(const FText& Title, const FText& Body)
{
	if (InventoryWidget && !bIsDead)
	{
		InventoryWidget->ShowNote(Title, Body);
	}
}

void ALostWithinCharacter::HideNote()
{
	if (InventoryWidget)
	{
		InventoryWidget->HideNote();
	}
}

bool ALostWithinCharacter::IsNoteOpen() const
{
	return InventoryWidget && InventoryWidget->IsNoteVisible();
}

void ALostWithinCharacter::ShowStatusMessage(const FText& Message)
{
	if (InventoryWidget && !bIsDead)
	{
		InventoryWidget->ShowStatusMessage(Message, 3.0f);
	}
}
