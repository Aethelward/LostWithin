// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "LostWithinNoiseSubsystem.generated.h"

class AActor;
class UMonsterHearingComponent;

/** What made a noise; hearing components can ignore some kinds. */
UENUM(BlueprintType)
enum class ELostWithinNoiseKind : uint8
{
	Footstep,
	ItemImpact,
	Other
};

/** One noise: where it happened and how far it carries. */
USTRUCT(BlueprintType)
struct LOSTWITHIN_API FLostWithinNoiseEvent
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Noise")
	FVector Location = FVector::ZeroVector;

	/** Distance at which the noise can still be heard. */
	UPROPERTY(BlueprintReadOnly, Category = "Noise", meta = (Units = "cm"))
	float Radius = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Noise")
	ELostWithinNoiseKind Kind = ELostWithinNoiseKind::Other;

	/** The actor whose action made the noise (the player, a dropped item). */
	UPROPERTY(BlueprintReadOnly, Category = "Noise")
	TObjectPtr<AActor> NoiseMaker = nullptr;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FOnLostWithinNoiseBroadcast, const FLostWithinNoiseEvent&);

/**
 *  Delivers player-made noises (footsteps, item impacts) to every monster hearing component whose owner is inside
 *  the noise radius. Delivery is synchronous, so a noise is either heard this frame or not at all.
 */
UCLASS()
class LOSTWITHIN_API ULostWithinNoiseSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:

	/**
	 *  Reports a noise to the hearing monsters of the world and to engine perception (MakeNoise with MaxRange).
	 *  Returns how many monsters reacted to it.
	 */
	UFUNCTION(BlueprintCallable, Category = "Noise", meta = (WorldContext = "WorldContextObject"))
	static int32 ReportNoise(const UObject* WorldContextObject, FVector Location, float Radius,
		ELostWithinNoiseKind Kind, AActor* NoiseMaker);

	/** Delivers Noise to registered listeners in range; returns how many reacted. */
	int32 Broadcast(const FLostWithinNoiseEvent& Noise);

	void RegisterListener(UMonsterHearingComponent* Listener);
	void UnregisterListener(UMonsterHearingComponent* Listener);

	/** Multicast delegate fired on every noise broadcast. Allows vibration components and sensors to listen without hearing component registration. */
	FOnLostWithinNoiseBroadcast OnNoiseBroadcast;

	/** The most recent reported noise, for debugging and automation. */
	const FLostWithinNoiseEvent& GetLastNoise() const { return LastNoise; }
	int32 GetReportedNoiseCount() const { return ReportedNoiseCount; }

private:

	TArray<TWeakObjectPtr<UMonsterHearingComponent>> Listeners;
	FLostWithinNoiseEvent LastNoise;
	int32 ReportedNoiseCount = 0;
};
