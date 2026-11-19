// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraTypes.h"
#include "DeathCameraActor.generated.h"

class APawn;
class USoundBase;

/**
 *  The "death cam": the view leaves the over-the-shoulder camera, whips to the player's eyes facing the monster's
 *  head, pushes in with a shaking stare, then drops and rolls as the player collapses while the screen fades to black.
 */
UCLASS()
class LOSTWITHIN_API ADeathCameraActor : public ACameraActor
{
	GENERATED_BODY()

public:

	ADeathCameraActor();

	/** Turn from the gameplay view to the killer */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death Camera", meta = (ClampMin = "0.01", Units = "s"))
	float TurnDuration = 0.25f;

	/** Shaking stare at the killer */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death Camera", meta = (ClampMin = "0.0", Units = "s"))
	float StareDuration = 1.5f;

	/** Fall to the floor and fade to black */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death Camera", meta = (ClampMin = "0.01", Units = "s"))
	float CollapseDuration = 0.85f;

	/** How far the stare drifts toward the killer */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death Camera", meta = (ClampMin = "0.0", Units = "cm"))
	float PushInDistance = 60.0f;

	/** Never push closer than this to the killer's head */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death Camera", meta = (ClampMin = "0.0", Units = "cm"))
	float MinDistanceToKiller = 45.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death Camera", meta = (ClampMin = "0.0", Units = "deg"))
	float ShakeAmplitude = 1.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death Camera", meta = (ClampMin = "5.0", ClampMax = "170.0", Units = "deg"))
	float TurnFOV = 85.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death Camera", meta = (ClampMin = "5.0", ClampMax = "170.0", Units = "deg"))
	float StareFOV = 70.0f;

	/** Eye height above the floor at the end of the collapse */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death Camera", meta = (ClampMin = "5.0", Units = "cm"))
	float CollapsedEyeHeight = 45.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death Camera", meta = (ClampMin = "-90.0", ClampMax = "90.0", Units = "deg"))
	float CollapseRoll = 28.0f;

	/** Short death impact played when the death camera starts, together with JumpscareSound (optional) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death Camera")
	TObjectPtr<USoundBase> StingSound;

	/** Death jumpscare played when the death camera starts, together with StingSound (optional) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death Camera")
	TObjectPtr<USoundBase> JumpscareSound;

	/** Begins the sequence from StartView (the gameplay camera at the moment of contact) */
	void StartSequence(APawn* InVictim, AActor* InKiller, const FMinimalViewInfo& StartView);

	/** Total length of the three phases */
	UFUNCTION(BlueprintPure, Category = "Death Camera")
	float GetTotalDuration() const { return TurnDuration + StareDuration + CollapseDuration; }

	UFUNCTION(BlueprintPure, Category = "Death Camera")
	bool IsFinished() const { return bFinished; }

	/** Point the camera stares at: the killer's head bone, else the upper part of its capsule */
	static FVector FindLookTarget(const AActor* Killer);

	/** Fired once when the collapse has finished */
	FSimpleMulticastDelegate OnFinished;

	virtual void Tick(float DeltaSeconds) override;

private:

	void ApplyView(const FVector& Location, const FRotator& Rotation, float FieldOfView);
	void StartFade();

	TWeakObjectPtr<APawn> Victim;
	TWeakObjectPtr<AActor> Killer;
	FVector StartLocation = FVector::ZeroVector;
	FQuat StartRotation = FQuat::Identity;
	float StartFOV = 90.0f;
	FVector EyeLocation = FVector::ZeroVector;
	FVector StareEndLocation = FVector::ZeroVector;
	double FloorZ = 0.0;
	float Elapsed = 0.0f;
	bool bStarted = false;
	bool bFadeStarted = false;
	bool bFinished = false;
};
