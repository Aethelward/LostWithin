// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "LostWithinLightSubsystem.generated.h"

class AActor;
class ALostWithinCharacter;
class UPointLightComponent;

/**
 *  Answers "is this point lit?" for monster senses: by a wall torch (a visible point light whose component name starts
 *  with TorchLightNamePrefix, within TorchLitRadius and with a clear line) or by the player's flashlight beam.
 *  Torch lights are cached and re-collected at most once per second.
 */
UCLASS()
class LOSTWITHIN_API ULostWithinLightSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** A point this close to a torch light (and in its line of sight) counts as torch-lit. The lights reach 900 cm, but
	 *  only the bright pool counts: 400 cm lights about a fifth of the maze floor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Light", meta = (ClampMin = "0.0", Units = "cm"))
	float TorchLitRadius = 400.0f;

	/** Point light components whose name starts with this are torches. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Light")
	FString TorchLightNamePrefix = TEXT("TorchPointLight");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Light")
	TEnumAsByte<ECollisionChannel> LightTraceChannel = ECC_Visibility;

	static ULostWithinLightSubsystem* Get(const UObject* WorldContext);

	/** True when a torch lights Point; OutLightLocation receives the nearest such torch light. */
	bool IsPointTorchLit(const FVector& Point, const AActor* IgnoreA = nullptr, const AActor* IgnoreB = nullptr,
		FVector* OutLightLocation = nullptr) const;

	/** True when the player's lit flashlight beam reaches Point (range, outer cone and a clear line). */
	static bool IsPointInFlashlightBeam(const ALostWithinCharacter* Player, const FVector& Point, const AActor* Ignore = nullptr);

	/** Torch-lit or in the beam of the first local player's flashlight. */
	bool IsPointLit(const FVector& Point, const AActor* Ignore = nullptr, FVector* OutLightLocation = nullptr) const;

	/** Re-collects the torch lights now (queries also do this lazily, at most once per second). */
	void RefreshTorches();

	int32 GetNumTorchLights() const { return TorchLights.Num(); }

private:
	void RefreshIfStale() const;
	bool HasClearLine(const FVector& From, const FVector& To, const AActor* LightOwner, const AActor* IgnoreA, const AActor* IgnoreB) const;

	TArray<TWeakObjectPtr<UPointLightComponent>> TorchLights;
	double LastRefreshSeconds = -1.0;
};
