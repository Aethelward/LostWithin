// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Logging/LogMacros.h"
#include "LostWithinCharacter.generated.h"

class UAudioComponent;
class UCameraComponent;
class UInputAction;
class UInputComponent;
class UInputMappingContext;
class UInventoryComponent;
class UInventoryWidget;
class ULostWithinFlashlightAnimInstance;
class UPointLightComponent;
class USkeletalMeshComponent;
class USoundBase;
class USpotLightComponent;
class USpringArmComponent;
class UStaticMeshComponent;
struct FHitResult;
struct FInputActionValue;

DECLARE_LOG_CATEGORY_EXTERN(LogTemplateCharacter, Log, All);

/**
 *  A third-person character with movement modes, interaction, inventory UI, and flashlight lighting.
 */
UCLASS(abstract)
class ALostWithinCharacter : public ACharacter
{
	GENERATED_BODY()

	/** Camera boom for over-the-shoulder third-person perspective */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	USpringArmComponent* ThirdPersonCameraBoom;

	/** Third-person camera */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	UCameraComponent* ThirdPersonCameraComponent;

	/** Player inventory component */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	UInventoryComponent* InventoryComponent;

	/** Held flashlight mesh attached to the character's right hand */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> FlashlightThirdPersonMesh;

	/** Primary beam spotlight for the tactical flashlight ([F] toggle) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USpotLightComponent> FlashlightSpotLight;

	/** Subtle close-range fill light near the flashlight lens */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UPointLightComponent> FlashlightFillLight;

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

	/** Interact Input Action ([LMB]) */
	UPROPERTY(EditAnywhere, Category ="Input")
	TObjectPtr<UInputAction> InteractAction = nullptr;

	/** Toggle Flashlight Input Action ([F]) */
	UPROPERTY(EditAnywhere, Category ="Input")
	TObjectPtr<UInputAction> ToggleFlashlightAction = nullptr;

	/** Reload / Change Flashlight Battery Input Action ([R]) */
	UPROPERTY(EditAnywhere, Category ="Input")
	TObjectPtr<UInputAction> ReloadFlashlightAction = nullptr;

	/** Toggle Inventory UI Input Action ([Tab]) */
	UPROPERTY(EditAnywhere, Category ="Input")
	TObjectPtr<UInputAction> ToggleInventoryAction = nullptr;

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

	/** Vertical camera offset applied while crouched so the third-person camera drops smoothly */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Movement|Crouch", meta = (Units = "cm"))
	float CrouchCameraZOffset = -45.0f;

	/** Interpolation speed for crouch camera height transitions */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Movement|Crouch", meta = (ClampMin = "0.1"))
	float CrouchCameraInterpSpeed = 12.0f;

	/** If true, this character manages default Run / Walk / Sprint / Crouch speeds */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Movement|Speeds")
	bool bUseCustomMovementSpeeds = true;

	/** Maximum trace distance in front of the player for [LMB] interaction */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Interaction", meta = (ClampMin = "10.0", Units = "cm"))
	float InteractionDistance = 350.0f;

	/** Sphere sweep radius used to assist aiming at small interactables */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Interaction", meta = (ClampMin = "1.0", Units = "cm"))
	float InteractionTraceRadius = 18.0f;

	/** Hand attachment socket used for the held flashlight mesh */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flashlight")
	FName FlashlightHandSocket = TEXT("hand_r");

	/** Relative offset of the held flashlight inside hand_r */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flashlight")
	FVector FlashlightAttachRelativeLocation = FVector(-8.1f, 1.8f, -0.3f);

	/** Relative rotation of the held flashlight inside hand_r */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flashlight")
	FRotator FlashlightAttachRelativeRotation = FRotator(58.4f, 163.3f, 0.0f);

	/** Uniform scale applied to the held flashlight mesh (/Game/Item/Flashlight/StaticMeshes/defaultMaterial) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flashlight", meta = (ClampMin = "0.01"))
	float FlashlightHeldScale = 0.15f;

	/** Seconds of continuous ON time per 100% combined flashlight battery charge */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flashlight|Battery", meta = (ClampMin = "1.0", Units = "s"))
	float FlashlightBatteryDurationSeconds = 180.0f;

	/** Flashlight spotlight intensity in candelas */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flashlight|Lighting", meta = (ClampMin = "0.0"))
	float FlashlightSpotIntensity = 25.0f;

	/** Close-range fill point light intensity in candelas */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flashlight|Lighting", meta = (ClampMin = "0.0"))
	float FlashlightFillIntensity = 0.5f;

	/** Maximum range of the flashlight spotlight beam */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flashlight|Lighting", meta = (ClampMin = "100.0", Units = "cm"))
	float FlashlightAttenuationRadius = 1500.0f;

	/** Inner beam cone angle in degrees */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flashlight|Lighting", meta = (ClampMin = "1.0", ClampMax = "80.0", Units = "deg"))
	float FlashlightInnerConeAngle = 8.0f;

	/** Outer beam cone angle in degrees */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flashlight|Lighting", meta = (ClampMin = "2.0", ClampMax = "89.0", Units = "deg"))
	float FlashlightOuterConeAngle = 18.0f;

	/** Beam tint color */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flashlight|Lighting")
	FLinearColor FlashlightLightColor = FLinearColor(1.0f, 0.95f, 0.86f, 1.0f);

	/** Smoothing speed for aiming the spotlight toward the active camera target */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flashlight|Lighting", meta = (ClampMin = "0.0"))
	float FlashlightAimInterpSpeed = 20.0f;

	/** Max distance used to converge the flashlight beam onto the player's crosshair point */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flashlight|Lighting", meta = (ClampMin = "100.0", Units = "cm"))
	float FlashlightConvergenceDistance = 2400.0f;

	/** Widget class spawned for the Placeholder Inventory & crosshair HUD */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="UI")
	TSubclassOf<UInventoryWidget> InventoryWidgetClass;

	/** Footstep sound variations played while walking, running, or sprinting */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Audio|Character")
	TArray<TObjectPtr<USoundBase>> FootstepSounds;

	/** Landing sound variations played when touching down from a jump or fall */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Audio|Character")
	TArray<TObjectPtr<USoundBase>> LandingSounds;

	/** Quietest footstep loudness when walking ([Left Shift] / crouched) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Audio|Character", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float WalkStepVolume = 0.175f;

	/** Medium footstep loudness during normal running */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Audio|Character", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float RunStepVolume = 0.325f;

	/** Loudest footstep loudness while sprinting ([Left Ctrl]) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Audio|Character", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float SprintStepVolume = 0.50f;

	/** Time between footstep sounds while walking */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Audio|Character", meta = (ClampMin = "0.05", Units = "s"))
	float WalkStepInterval = 0.58f;

	/** Time between footstep sounds during normal running */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Audio|Character", meta = (ClampMin = "0.05", Units = "s"))
	float RunStepInterval = 0.42f;

	/** Time between footstep sounds while sprinting */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Audio|Character", meta = (ClampMin = "0.05", Units = "s"))
	float SprintStepInterval = 0.30f;

	/** Volume multiplier for landing SFX */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Audio|Character", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float LandingSoundVolume = 0.85f;

	/** If true, plays ExplorationBGMTracks sequentially in a rotating loop */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Audio|BGM")
	bool bEnableExplorationBGM = true;

	/** Exploration BGM tracks played sequentially in a rotating loop */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Audio|BGM")
	TArray<TObjectPtr<USoundBase>> ExplorationBGMTracks;

	/** Base volume multiplier for exploration BGM */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Audio|BGM", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float ExplorationBGMVolume = 0.35f;

	/** Fade speed when pausing/resuming exploration BGM for the Oiiai cat spin sequence */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Audio|BGM", meta = (ClampMin = "0.1"))
	float ExplorationBGMFadeSpeed = 2.0f;

public:

	ALostWithinCharacter();

protected:

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void PawnClientRestart() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

public:

	virtual void Tick(float DeltaTime) override;
	virtual void Landed(const FHitResult& Hit) override;

	/** Plays A_ItemPickup_fromIdle_RH_100cm on the right arm/upper body overlay */
	UFUNCTION(BlueprintCallable, Category="Animation|Flashlight")
	void PlayPickupAnimation();

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

	/** Interacts with the object currently in view ([LMB]) */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoInteract();

	/** Toggles the held tactical flashlight on or off ([F]) */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoToggleFlashlight();

	/** Installs or swaps the highest-charged spare battery into the held tactical flashlight ([R]) */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoReloadFlashlight();

	/** Toggles the Placeholder Inventory UI ([Tab]) */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoToggleInventory();

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

	/** Returns third person camera component */
	UCameraComponent* GetThirdPersonCameraComponent() const { return ThirdPersonCameraComponent; }

	/** Returns the currently active gameplay camera */
	UFUNCTION(BlueprintPure, Category="Camera")
	UCameraComponent* GetActiveGameplayCamera() const;

	/** Returns the player's inventory component */
	UFUNCTION(BlueprintPure, Category="Inventory")
	UInventoryComponent* GetInventoryComponent() const { return InventoryComponent; }

	/** Returns true when the player owns at least one tactical flashlight */
	UFUNCTION(BlueprintPure, Category="Flashlight")
	bool HasFlashlight() const { return bHasFlashlight; }

	/** Returns true when the player's held tactical flashlight is currently powered ON */
	UFUNCTION(BlueprintPure, Category="Flashlight")
	bool IsFlashlightOn() const { return bIsFlashlightOn; }

	/** Returns the combined battery charge (%) of the held flashlight (0..100) */
	UFUNCTION(BlueprintPure, Category="Flashlight|Battery")
	float GetFlashlightBatteryPercent() const;

	/** Returns the number of physical batteries currently installed in the held flashlight (0..2) */
	UFUNCTION(BlueprintPure, Category="Flashlight|Battery")
	int32 GetFlashlightBatteryCount() const;

	/** Sets the full battery duration (seconds) while powered ON */
	UFUNCTION(BlueprintCallable, Category="Flashlight|Battery")
	void SetFlashlightBatteryDurationSeconds(float InSeconds) { FlashlightBatteryDurationSeconds = FMath::Max(1.0f, InSeconds); }

	/** Returns the vertical camera offset applied while crouched */
	UFUNCTION(BlueprintPure, Category="Movement|Crouch")
	float GetCrouchCameraZOffset() const { return CrouchCameraZOffset; }

private:

	/** Ensures default Enhanced Input actions and key mappings exist even without Blueprint wiring */
	void EnsureDefaultInputActionsAndMapping();

	/** Registers the runtime key mappings on the local player controller */
	void RegisterRuntimeInputMappingContext();

	/** Updates CharacterMovement->MaxWalkSpeed based on Walk/Run/Sprint/Crouch state */
	void UpdateMovementSpeed();

	/** Traces along the active camera view for an actor or component implementing IInteractableInterface */
	UObject* FindInteractableInView(FHitResult& OutHit, float MaxTraceDistance) const;

	/** Updates the HUD prompt text for the currently looked-at interactable */
	void UpdateInteractionPrompt();

	/** Applies mouse cursor and input mode matching bIsInventoryOpen */
	void ApplyInventoryInputState();

	/** Configures the character mesh with ULostWithinFlashlightAnimInstance over base locomotion */
	void SetupFlashlightAnimationWrapper();

	/** Loads the flashlight mesh onto the held component and applies lighting parameters */
	void InitializeFlashlightSetup();

	/** Applies Flashlight|Lighting intensity, cone angles, attenuation, and color to SpotLight and FillLight */
	void ApplyFlashlightLightingSettings();

	/** Syncs held flashlight visibility, active pose, and lighting state with InventoryComponent */
	UFUNCTION()
	void HandleInventoryUpdated();

	/** Applies held mesh visibility and light enablement matching bHasFlashlight / bIsFlashlightOn */
	void RefreshFlashlightState();

	/** Updates the HUD widget's flashlight battery & cell breakdown display */
	void UpdateFlashlightBatteryHUD();

	/** Updates world position and camera-converged aim rotation of the flashlight lighting components */
	void UpdateFlashlightTransformAndLighting(float DeltaTime);

	/** Closes Placeholder Inventory when triggered from UI keyboard focus ([Tab]) */
	UFUNCTION()
	void HandleInventoryCloseRequested();

	/** Ensures default footstep, landing, and exploration BGM assets are loaded with looping disabled */
	void EnsureDefaultAudioAssets();

	/** Drives periodic footstep SFX scaled cleanly across Walk, Run, and Sprint gaits */
	void UpdateFootstepAudio(float DeltaTime);

	/** Plays a non-repeating random sound at the character's feet with randomized pitch */
	void PlayRandomSoundAtFeet(const TArray<TObjectPtr<USoundBase>>& Sounds, int32& InOutLastIndex, float VolumeMultiplier, float MinPitch, float MaxPitch);

	/** Computes the effective BGM volume multiplier from base volume and active fade factor */
	float GetEffectiveExplorationBGMVolume() const;

	/** Initializes and starts sequential exploration BGM playback for the local player */
	void StartExplorationBGM();

	/** Stops and cleans up the exploration BGM audio component */
	void StopExplorationBGM();

	/** Fades and pauses exploration BGM while the Oiiai cat spin music is playing */
	void UpdateExplorationBGM(float DeltaTime);

	/** Starts playing ExplorationBGMTracks[CurrentBGMTrackIndex] */
	void PlayCurrentExplorationBGMTrack();

	/** Advances to the next exploration BGM track in sequence when the current track ends */
	UFUNCTION()
	void HandleExplorationBGMFinished();

	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> RuntimeExtraMappingContext = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UInventoryWidget> InventoryWidget = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> ExplorationBGMComponent = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<ULostWithinFlashlightAnimInstance> FlashlightAnimInstance = nullptr;

	FVector DefaultThirdPersonBoomRelativeLocation = FVector::ZeroVector;

	float CurrentCrouchCameraOffset = 0.0f;
	float FootstepTimer = 0.0f;
	int32 LastFootstepSoundIndex = INDEX_NONE;
	int32 LastLandingSoundIndex = INDEX_NONE;
	int32 CurrentBGMTrackIndex = 0;
	float CurrentBGMVolumeFactor = 1.0f;
	bool bIsInventoryOpen = false;
	bool bIsWalking = false;
	bool bIsSprintingHeld = false;
	bool bHasFlashlight = false;
	bool bIsFlashlightOn = false;
	bool bFlashlightAimInitialized = false;
	bool bBGMPausedForCatSpin = false;
	bool bSuppressBGMFinishedAdvance = false;
};
