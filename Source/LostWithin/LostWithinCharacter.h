// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Logging/LogMacros.h"
#include "LostWithinCharacter.generated.h"

class UCameraComponent;
class UInputAction;
class UInputComponent;
class UInputMappingContext;
class UInventoryComponent;
class UInventoryWidget;
class USkeletalMeshComponent;
class USpringArmComponent;
struct FHitResult;
struct FInputActionValue;

DECLARE_LOG_CATEGORY_EXTERN(LogTemplateCharacter, Log, All);

/**
 *  A basic first/third person character with movement modes, interaction/hitting, inventory UI, and POV switching.
 */
UCLASS(abstract)
class ALostWithinCharacter : public ACharacter
{
	GENERATED_BODY()

	/** Pawn mesh: first person view (arms; seen only by self) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	USkeletalMeshComponent* FirstPersonMesh;

	/** First person camera */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	UCameraComponent* FirstPersonCameraComponent;

	/** Camera boom for third-person perspective ([F5] toggle) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	USpringArmComponent* ThirdPersonCameraBoom;

	/** Third person camera ([F5] toggle) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	UCameraComponent* ThirdPersonCameraComponent;

	/** Player inventory component */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	UInventoryComponent* InventoryComponent;

protected:

	/** Jump Input Action */
	UPROPERTY(EditAnywhere, Category ="Input")
	UInputAction* JumpAction;

	/** Move Input Action */
	UPROPERTY(EditAnywhere, Category ="Input")
	UInputAction* MoveAction;

	/** Look Input Action */
	UPROPERTY(EditAnywhere, Category ="Input")
	UInputAction* LookAction;

	/** Mouse Look Input Action */
	UPROPERTY(EditAnywhere, Category ="Input")
	UInputAction* MouseLookAction;

	/** Interact Input Action ([F]) */
	UPROPERTY(EditAnywhere, Category ="Input")
	TObjectPtr<UInputAction> InteractAction = nullptr;

	/** Hit Input Action ([LMB]) */
	UPROPERTY(EditAnywhere, Category ="Input")
	TObjectPtr<UInputAction> HitAction = nullptr;

	/** Toggle Inventory UI Input Action ([Tab]) */
	UPROPERTY(EditAnywhere, Category ="Input")
	TObjectPtr<UInputAction> ToggleInventoryAction = nullptr;

	/** Switch 1st/3rd Person POV Input Action ([F5]) */
	UPROPERTY(EditAnywhere, Category ="Input")
	TObjectPtr<UInputAction> SwitchPOVAction = nullptr;

	/** Hold-to-Walk Input Action ([Left Shift]) */
	UPROPERTY(EditAnywhere, Category ="Input")
	TObjectPtr<UInputAction> WalkModeAction = nullptr;

	/** Hold-to-Sprint Input Action ([Left Ctrl]) */
	UPROPERTY(EditAnywhere, Category ="Input")
	TObjectPtr<UInputAction> SprintModeAction = nullptr;

	/** Toggle Crouch Input Action ([C]) */
	UPROPERTY(EditAnywhere, Category ="Input")
	TObjectPtr<UInputAction> CrouchAction = nullptr;

	/** Default running speed when moving normally */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Movement|Speeds", meta = (ClampMin = "0.0", Units = "cm/s"))
	float DefaultRunSpeed = 500.0f;

	/** Walking speed while holding [Left Shift] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Movement|Speeds", meta = (ClampMin = "0.0", Units = "cm/s"))
	float SlowWalkSpeed = 250.0f;

	/** Sprinting speed while holding [Left Ctrl] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Movement|Speeds", meta = (ClampMin = "0.0", Units = "cm/s"))
	float FastSprintSpeed = 800.0f;

	/** Movement speed while crouched ([C]) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Movement|Speeds", meta = (ClampMin = "0.0", Units = "cm/s"))
	float CrouchMoveSpeed = 200.0f;

	/** Vertical camera offset applied while crouched so first-person & third-person views drop smoothly */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Movement|Crouch", meta = (Units = "cm"))
	float CrouchCameraZOffset = -45.0f;

	/** Interpolation speed for crouch camera height transitions */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Movement|Crouch", meta = (ClampMin = "0.1"))
	float CrouchCameraInterpSpeed = 12.0f;

	/** If true, this character manages default Run / Walk / Sprint / Crouch speeds */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Movement|Speeds")
	bool bUseCustomMovementSpeeds = true;

	/** Maximum trace distance in front of the player for [F] interaction */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Interaction", meta = (ClampMin = "10.0", Units = "cm"))
	float InteractionDistance = 350.0f;

	/** Maximum trace distance in front of the player for [LMB] hit */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Interaction", meta = (ClampMin = "10.0", Units = "cm"))
	float HitDistance = 250.0f;

	/** Sphere sweep radius used to assist aiming at small interactables or hits */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Interaction", meta = (ClampMin = "1.0", Units = "cm"))
	float InteractionTraceRadius = 18.0f;

	/** Damage dealt by [LMB] hit */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Interaction", meta = (ClampMin = "0.0"))
	float HitDamage = 15.0f;

	/** Physics impulse applied to simulating primitives on [LMB] hit */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Interaction", meta = (ClampMin = "0.0"))
	float HitImpulseStrength = 30000.0f;

	/** Widget class spawned for the Placeholder Inventory & crosshair HUD */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="UI")
	TSubclassOf<UInventoryWidget> InventoryWidgetClass;

public:

	ALostWithinCharacter();

protected:

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void PawnClientRestart() override;

public:

	virtual void Tick(float DeltaTime) override;

protected:

	/** Called from Input Actions for movement input */
	void MoveInput(const FInputActionValue& Value);

	/** Called from Input Actions for looking input */
	void LookInput(const FInputActionValue& Value);

	/** Handles aim inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoAim(float Yaw, float Pitch);

	/** Handles move inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoMove(float Right, float Forward);

	/** Handles jump start inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoJumpStart();

	/** Handles jump end inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoJumpEnd();

	/** Interacts with the object currently in view ([F]) */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoInteract();

	/** Performs a melee hit trace in view ([LMB]) */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoHit();

	/** Toggles the Placeholder Inventory UI ([Tab]) */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoToggleInventory();

	/** Switches between 1st-person and 3rd-person POV ([F5]) */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoTogglePOV();

	/** Starts holding walk ([Left Shift]) */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoStartWalk();

	/** Stops holding walk ([Left Shift]) */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoEndWalk();

	/** Starts holding sprint ([Left Ctrl]) */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoStartFastSprint();

	/** Stops holding sprint ([Left Ctrl]) */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoEndFastSprint();

	/** Toggles crouch state ([C]) */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoToggleCrouch();

	/** Set up input action bindings */
	virtual void SetupPlayerInputComponent(UInputComponent* InputComponent) override;

public:

	/** Returns the first person mesh */
	USkeletalMeshComponent* GetFirstPersonMesh() const { return FirstPersonMesh; }

	/** Returns first person camera component */
	UCameraComponent* GetFirstPersonCameraComponent() const { return FirstPersonCameraComponent; }

	/** Returns third person camera component */
	UCameraComponent* GetThirdPersonCameraComponent() const { return ThirdPersonCameraComponent; }

	/** Returns the currently active gameplay camera */
	UFUNCTION(BlueprintPure, Category="Camera")
	UCameraComponent* GetActiveGameplayCamera() const;

	/** Returns true when currently in 3rd person view */
	UFUNCTION(BlueprintPure, Category="Camera")
	bool IsThirdPersonPOV() const { return bIsThirdPerson; }

	/** Returns the player's inventory component */
	UFUNCTION(BlueprintPure, Category="Inventory")
	UInventoryComponent* GetInventoryComponent() const { return InventoryComponent; }

private:

	/** Ensures default Enhanced Input actions and key mappings exist even without Blueprint wiring */
	void EnsureDefaultInputActionsAndMapping();

	/** Registers the runtime key mappings and removes debug F5 exec binding on the local player controller */
	void RegisterRuntimeInputMappingContext();

	/** Applies current 1st/3rd-person camera & mesh visibility state */
	void ApplyPOVState();

	/** Updates CharacterMovement->MaxWalkSpeed based on Walk/Run/Sprint/Crouch state */
	void UpdateMovementSpeed();

	/** Traces along the active camera view for an actor or component implementing IInteractableInterface */
	UObject* FindInteractableInView(FHitResult& OutHit, float MaxTraceDistance) const;

	/** Updates the HUD prompt text for the currently looked-at interactable */
	void UpdateInteractionPrompt();

	/** Applies mouse cursor and input mode matching bIsInventoryOpen */
	void ApplyInventoryInputState();

	/** Closes Placeholder Inventory when triggered from UI keyboard focus ([Tab]) */
	UFUNCTION()
	void HandleInventoryCloseRequested();

	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> RuntimeExtraMappingContext = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UInventoryWidget> InventoryWidget = nullptr;

	FVector DefaultFirstPersonMeshRelativeLocation = FVector::ZeroVector;
	FVector DefaultThirdPersonBoomRelativeLocation = FVector::ZeroVector;
	FVector CurrentHitPunchOffset = FVector::ZeroVector;

	float CurrentCrouchCameraOffset = 0.0f;
	bool bIsThirdPerson = false;
	bool bIsInventoryOpen = false;
	bool bIsWalking = false;
	bool bIsSprintingHeld = false;
};
