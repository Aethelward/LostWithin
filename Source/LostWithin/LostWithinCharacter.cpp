// Copyright Epic Games, Inc. All Rights Reserved.

#include "LostWithinCharacter.h"
#include "Animation/AnimInstance.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/DamageType.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "Interaction/InteractableInterface.h"
#include "Inventory/InventoryComponent.h"
#include "Kismet/GameplayStatics.h"
#include "LostWithin.h"
#include "UI/InventoryWidget.h"

namespace
{
	UObject* ResolveInteractableFromHit(const FHitResult& Hit)
	{
		if (UPrimitiveComponent* HitComp = Hit.GetComponent())
		{
			if (HitComp->GetClass()->ImplementsInterface(UInteractableInterface::StaticClass()))
			{
				return HitComp;
			}
		}

		if (AActor* HitActor = Hit.GetActor())
		{
			if (HitActor->GetClass()->ImplementsInterface(UInteractableInterface::StaticClass()))
			{
				return HitActor;
			}

			for (UActorComponent* Comp : HitActor->GetComponents())
			{
				if (Comp && Comp->GetClass()->ImplementsInterface(UInteractableInterface::StaticClass()))
				{
					return Comp;
				}
			}
		}

		return nullptr;
	}
}

ALostWithinCharacter::ALostWithinCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(55.f, 96.0f);

	// Create the first person mesh that will be viewed only by this character's owner
	FirstPersonMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("First Person Mesh"));

	FirstPersonMesh->SetupAttachment(GetMesh());
	FirstPersonMesh->SetOnlyOwnerSee(true);
	FirstPersonMesh->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::FirstPerson;
	FirstPersonMesh->SetCollisionProfileName(FName("NoCollision"));

	// Create the Camera Component
	FirstPersonCameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("First Person Camera"));
	FirstPersonCameraComponent->SetupAttachment(FirstPersonMesh, FName("head"));
	FirstPersonCameraComponent->SetRelativeLocationAndRotation(FVector(-2.8f, 5.89f, 0.0f), FRotator(0.0f, 90.0f, -90.0f));
	FirstPersonCameraComponent->bUsePawnControlRotation = true;
	FirstPersonCameraComponent->bEnableFirstPersonFieldOfView = true;
	FirstPersonCameraComponent->bEnableFirstPersonScale = true;
	FirstPersonCameraComponent->FirstPersonFieldOfView = 70.0f;
	FirstPersonCameraComponent->FirstPersonScale = 0.6f;

	// Create third-person boom and camera ([F5] POV switch)
	ThirdPersonCameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("Third Person Camera Boom"));
	ThirdPersonCameraBoom->SetupAttachment(GetCapsuleComponent());
	ThirdPersonCameraBoom->SetRelativeLocation(FVector(0.0f, 0.0f, 60.0f));
	ThirdPersonCameraBoom->TargetArmLength = 300.0f;
	ThirdPersonCameraBoom->SocketOffset = FVector(0.0f, 45.0f, 15.0f);
	ThirdPersonCameraBoom->bUsePawnControlRotation = true;
	ThirdPersonCameraBoom->bDoCollisionTest = true;

	ThirdPersonCameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("Third Person Camera"));
	ThirdPersonCameraComponent->SetupAttachment(ThirdPersonCameraBoom, USpringArmComponent::SocketName);
	ThirdPersonCameraComponent->bUsePawnControlRotation = false;
	ThirdPersonCameraComponent->bAutoActivate = false;

	// Create player inventory component
	InventoryComponent = CreateDefaultSubobject<UInventoryComponent>(TEXT("Inventory Component"));

	// Configure the character comps
	GetMesh()->SetOwnerNoSee(true);
	GetMesh()->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::WorldSpaceRepresentation;

	GetCapsuleComponent()->SetCapsuleSize(34.0f, 96.0f);

	// Configure character movement (default movement is Run)
	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	MoveComp->BrakingDecelerationFalling = 1500.0f;
	MoveComp->AirControl = 0.5f;
	MoveComp->MaxWalkSpeed = DefaultRunSpeed;
	MoveComp->MaxWalkSpeedCrouched = CrouchMoveSpeed;
	MoveComp->GetNavAgentPropertiesRef().bCanCrouch = true;
	MoveComp->SetCrouchedHalfHeight(55.0f);

	InventoryWidgetClass = UInventoryWidget::StaticClass();
}

void ALostWithinCharacter::BeginPlay()
{
	Super::BeginPlay();

	if (FirstPersonMesh)
	{
		DefaultFirstPersonMeshRelativeLocation = FirstPersonMesh->GetRelativeLocation();
	}

	if (ThirdPersonCameraBoom)
	{
		DefaultThirdPersonBoomRelativeLocation = ThirdPersonCameraBoom->GetRelativeLocation();
	}

	ApplyPOVState();
	UpdateMovementSpeed();
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
			}
		}
	}
}

void ALostWithinCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
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

	const float TargetCrouchOffset = bIsCrouched ? CrouchCameraZOffset : 0.0f;
	CurrentCrouchCameraOffset = FMath::FInterpTo(CurrentCrouchCameraOffset, TargetCrouchOffset, DeltaTime, CrouchCameraInterpSpeed);
	CurrentHitPunchOffset = FMath::VInterpTo(CurrentHitPunchOffset, FVector::ZeroVector, DeltaTime, 16.0f);

	if (FirstPersonMesh)
	{
		const FVector DesiredFPLocation = DefaultFirstPersonMeshRelativeLocation
			+ FVector(0.0f, 0.0f, CurrentCrouchCameraOffset)
			+ CurrentHitPunchOffset;
		FirstPersonMesh->SetRelativeLocation(DesiredFPLocation);
	}

	if (ThirdPersonCameraBoom)
	{
		const FVector DesiredBoomLocation = DefaultThirdPersonBoomRelativeLocation
			+ FVector(0.0f, 0.0f, CurrentCrouchCameraOffset);
		ThirdPersonCameraBoom->SetRelativeLocation(DesiredBoomLocation);
	}

	if (IsLocallyControlled() && InventoryWidget)
	{
		UpdateInteractionPrompt();
	}
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

		// Interaction & Hit
		if (InteractAction)
		{
			EnhancedInputComponent->BindAction(InteractAction, ETriggerEvent::Started, this, &ALostWithinCharacter::DoInteract);
		}
		if (HitAction)
		{
			EnhancedInputComponent->BindAction(HitAction, ETriggerEvent::Started, this, &ALostWithinCharacter::DoHit);
		}

		// Inventory UI ([Tab]) & POV Toggle ([F5])
		if (ToggleInventoryAction)
		{
			EnhancedInputComponent->BindAction(ToggleInventoryAction, ETriggerEvent::Started, this, &ALostWithinCharacter::DoToggleInventory);
		}
		if (SwitchPOVAction)
		{
			EnhancedInputComponent->BindAction(SwitchPOVAction, ETriggerEvent::Started, this, &ALostWithinCharacter::DoTogglePOV);
		}

		// Movement states: Walk ([Left Shift]), Sprint ([Left Ctrl]), Crouch ([C])
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
		if (CrouchAction)
		{
			EnhancedInputComponent->BindAction(CrouchAction, ETriggerEvent::Started, this, &ALostWithinCharacter::DoToggleCrouch);
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
	if (bIsInventoryOpen)
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
	if (GetController())
	{
		// pass the move inputs
		AddMovementInput(GetActorRightVector(), Right);
		AddMovementInput(GetActorForwardVector(), Forward);
	}
}

void ALostWithinCharacter::DoJumpStart()
{
	if (bIsCrouched)
	{
		UnCrouch();
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
	if (bIsInventoryOpen)
	{
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

void ALostWithinCharacter::DoHit()
{
	if (bIsInventoryOpen)
	{
		return;
	}

	const UCameraComponent* ActiveCamera = GetActiveGameplayCamera();
	const FVector CameraForward = ActiveCamera ? ActiveCamera->GetForwardVector() : GetActorForwardVector();

	if (GetMesh())
	{
		const FVector WorldPunchDelta = CameraForward * 14.0f + FVector(0.0f, 0.0f, -3.0f);
		CurrentHitPunchOffset = GetMesh()->GetComponentTransform().InverseTransformVectorNoScale(WorldPunchDelta);
	}

	FHitResult HitResult;
	UObject* InteractableTarget = FindInteractableInView(HitResult, HitDistance);
	if (HitResult.bBlockingHit)
	{
		if (UPrimitiveComponent* HitComp = HitResult.GetComponent())
		{
			if (HitComp->IsSimulatingPhysics())
			{
				HitComp->AddImpulseAtLocation(CameraForward * HitImpulseStrength, HitResult.ImpactPoint);
			}
		}

		if (AActor* HitActor = HitResult.GetActor())
		{
			UGameplayStatics::ApplyPointDamage(
				HitActor,
				HitDamage,
				CameraForward,
				HitResult,
				GetController(),
				this,
				UDamageType::StaticClass());
		}

		if (InteractableTarget)
		{
			IInteractableInterface::Execute_OnHit(InteractableTarget, this, HitResult);
		}
	}
}

void ALostWithinCharacter::DoToggleInventory()
{
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

void ALostWithinCharacter::DoTogglePOV()
{
	bIsThirdPerson = !bIsThirdPerson;
	ApplyPOVState();
}

void ALostWithinCharacter::DoStartWalk()
{
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
	bIsSprintingHeld = true;
	UpdateMovementSpeed();
}

void ALostWithinCharacter::DoEndFastSprint()
{
	bIsSprintingHeld = false;
	UpdateMovementSpeed();
}

void ALostWithinCharacter::DoToggleCrouch()
{
	if (bIsCrouched)
	{
		UnCrouch();
	}
	else if (CanCrouch())
	{
		Crouch();
	}

	UpdateMovementSpeed();
}

UCameraComponent* ALostWithinCharacter::GetActiveGameplayCamera() const
{
	return (bIsThirdPerson && ThirdPersonCameraComponent) ? ThirdPersonCameraComponent : FirstPersonCameraComponent;
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

	EnsureActionAndKey(InteractAction, TEXT("IA_RuntimeInteract"), EKeys::F);
	EnsureActionAndKey(HitAction, TEXT("IA_RuntimeHit"), EKeys::LeftMouseButton);
	EnsureActionAndKey(ToggleInventoryAction, TEXT("IA_RuntimeToggleInventory"), EKeys::Tab);
	EnsureActionAndKey(SwitchPOVAction, TEXT("IA_RuntimeSwitchPOV"), EKeys::F5);
	EnsureActionAndKey(WalkModeAction, TEXT("IA_RuntimeWalk"), EKeys::LeftShift);
	EnsureActionAndKey(SprintModeAction, TEXT("IA_RuntimeSprint"), EKeys::LeftControl);
	EnsureActionAndKey(CrouchAction, TEXT("IA_RuntimeCrouch"), EKeys::C);
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

	// Prevent Unreal's default F5 debug command ("viewmode shadercomplexity") from overriding POV toggle
	if (PC->PlayerInput)
	{
		PC->PlayerInput->DebugExecBindings.RemoveAll([](const FKeyBind& Bind)
		{
			return Bind.Key == EKeys::F5;
		});
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

void ALostWithinCharacter::ApplyPOVState()
{
	if (FirstPersonCameraComponent)
	{
		FirstPersonCameraComponent->SetActive(!bIsThirdPerson);
	}

	if (ThirdPersonCameraComponent)
	{
		ThirdPersonCameraComponent->SetActive(bIsThirdPerson);
	}

	if (FirstPersonMesh)
	{
		FirstPersonMesh->SetVisibility(!bIsThirdPerson, true);
	}

	if (USkeletalMeshComponent* ThirdPersonMesh = GetMesh())
	{
		ThirdPersonMesh->SetOwnerNoSee(!bIsThirdPerson);
		ThirdPersonMesh->FirstPersonPrimitiveType = bIsThirdPerson
			? EFirstPersonPrimitiveType::None
			: EFirstPersonPrimitiveType::WorldSpaceRepresentation;
		ThirdPersonMesh->MarkRenderStateDirty();
	}
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

	MoveComp->MaxWalkSpeedCrouched = CrouchMoveSpeed;

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
	const float ForwardOffset = bIsThirdPerson
		? FMath::Max(0.0f, FVector::DotProduct(GetPawnViewLocation() - CameraLocation, CameraForward))
		: 0.0f;

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

	if (bIsInventoryOpen)
	{
		InventoryWidget->SetInteractionPrompt(FText::GetEmpty());
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
