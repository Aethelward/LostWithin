// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MonsterSenseProfile.generated.h"

class ACharacter;
class AActor;
class UWorld;

/** The one sense a monster hunts with. */
UENUM(BlueprintType)
enum class EMonsterSense : uint8
{
	None,
	/** Doll: sees only what light touches; deaf. */
	Sight,
	/** Ghoul: blind, exact hearing. */
	Hearing,
	/** BoneDog: follows the player's scent; deaf, near-sighted. */
	Smell,
	/** Lunger: floor vibration and air turbulence; blind and deaf. */
	Touch
};

/**
 *  Gives each placed monster its specialty sense at run start (called once by LostWithinGameMode, like the BoneDog's
 *  light attraction before it), without editing the Blueprint assets:
 *  - BoneDog: hearing deactivated, near-sight (UMonsterLightAttractionComponent) and nose (UMonsterScentComponent) added.
 *  - Doll: hearing deactivated; its sight component's dormancy and lit-sight defaults apply.
 *  - Ghoul: hearing range x1.5 and exact pinpoint (0 cm error in light and dark).
 *  - Lunger: vibration only (unchanged).
 *  The creature is identified by its class name. Cat reactions keep priority over every sense.
 */
UCLASS()
class LOSTWITHIN_API UMonsterSenseProfile : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/** The Ghoul's hearing range multiplier (walk 6 m, run 13.5 m, sprint 24 m, item 45 m). */
	static constexpr float GhoulHearingRangeMultiplier = 1.5f;

	/** Which sense the creature class of Actor hunts with (None when it is not a known monster). */
	UFUNCTION(BlueprintPure, Category = "Monster Senses")
	static EMonsterSense GetSenseForActor(const AActor* Actor);

	/** Applies the profile to one monster by its class; returns the sense it now hunts with. Safe to call again. */
	UFUNCTION(BlueprintCallable, Category = "Monster Senses")
	static EMonsterSense ApplyToActor(ACharacter* Monster);

	/** Applies the given sense's setup to Monster whatever its class (tests, custom creatures). Safe to call again. */
	UFUNCTION(BlueprintCallable, Category = "Monster Senses")
	static void ApplySenseToActor(ACharacter* Monster, EMonsterSense Sense);

	/** Applies the profile to every placed monster (characters with a MonsterAIComponent); returns how many. */
	UFUNCTION(BlueprintCallable, Category = "Monster Senses", meta = (WorldContext = "WorldContextObject"))
	static int32 ApplyForWorld(UObject* WorldContextObject);

	/** True while any of Actor's senses is tracking the player or a lure (hearing, scent, sight, ear chase, tremor). */
	UFUNCTION(BlueprintPure, Category = "Monster Senses")
	static bool IsActorHunting(const AActor* Actor);
};
