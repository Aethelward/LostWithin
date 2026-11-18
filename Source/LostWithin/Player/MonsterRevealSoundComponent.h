// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"
#include "MonsterRevealSoundComponent.generated.h"

class ACharacter;
class APlayerController;
class UAudioComponent;
class USoundBase;

/** Plays a one-shot when a nearby monster suddenly enters the living player's view. */
UCLASS(ClassGroup=(Audio), meta=(BlueprintSpawnableComponent))
class LOSTWITHIN_API UMonsterRevealSoundComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMonsterRevealSoundComponent();

	/** Sound variants for a sudden sighting, played once and without spatial attenuation. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Monster Reveal")
	TArray<TObjectPtr<USoundBase>> RevealSounds;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Monster Reveal", meta=(ClampMin="0.0", Units="cm"))
	float CloseRange = 420.0f;

	/** A brief occlusion or crossing the screen edge must not rearm a sighting. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Monster Reveal", meta=(ClampMin="0.0", Units="s"))
	float MinimumHiddenTime = 0.75f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Monster Reveal", meta=(ClampMin="0.0", Units="s"))
	float Cooldown = 10.0f;

	void StopReveal();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	struct FSighting
	{
		double HiddenSince = 0.0;
		bool bVisible = false;
	};

	bool IsMonsterInView(const APlayerController& Controller, const ACharacter& Monster,
		const FVector& CameraLocation, const FIntPoint& ViewportSize) const;
	void PlayReveal(const ACharacter& Monster, double Now);

	TMap<TWeakObjectPtr<ACharacter>, FSighting> Sightings;
	TWeakObjectPtr<UAudioComponent> PlayingReveal;
	FRandomStream Random;
	double NextRevealTime = 0.0;
	int32 LastSoundIndex = INDEX_NONE;
	bool bViewPrimed = false;
};
