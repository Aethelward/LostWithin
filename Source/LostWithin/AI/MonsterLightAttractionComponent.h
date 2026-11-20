#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "MonsterLightAttractionComponent.generated.h"

class ACharacter;
class ALostWithinCharacter;
class UMonsterAIComponent;

/**
 * BoneDog's eyes. By default the dog is near-sighted: it sees the player only within AttractionRadius (400 cm) with a
 * clear line from its eyes to the player's body, lit or not, and runs at them in the same frame; out of that range it
 * relies on its nose (UMonsterScentComponent). With bRequireFlashlight, the older flashlight lure applies instead: the
 * lit beam on the dog, or the lit lens within AttractionRadius, draws it. While attracted, MonsterAI::CanRunNormalAI is
 * false, so the Blueprint roam loop waits and the Animation Blueprint plays the fastest gait. Cat reactions keep
 * priority.
 */
UCLASS(ClassGroup=(AI), meta=(BlueprintSpawnableComponent))
class LOSTWITHIN_API UMonsterLightAttractionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMonsterLightAttractionComponent();

	/** How far the dog sees the player (needs a clear line). With bRequireFlashlight: how close a lit lens draws it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Light Attraction", meta=(ClampMin="0.0", Units="cm"))
	float AttractionRadius = 400.0f;

	/** False (default): near-sighted sight of the player. True: only the lit flashlight draws it (older behaviour). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Light Attraction")
	bool bRequireFlashlight = false;

	/** Chase speed; 0 uses MonsterAI's PanicGaitSpeed (the fastest gait clip), else 350 cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Light Attraction", meta=(ClampMin="0.0", Units="cm/s"))
	float ChaseSpeed = 0.0f;

	/** Time between move requests toward the player while attracted. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Light Attraction", meta=(ClampMin="0.05", Units="s"))
	float RepathInterval = 0.35f;

	/** Height above the actor origin used as the dog's eyes and beam target. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Light Attraction", meta=(Units="cm"))
	float EyeHeight = 40.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Light Attraction")
	TEnumAsByte<ECollisionChannel> LightTraceChannel = ECC_Visibility;

	UFUNCTION(BlueprintPure, Category="Light Attraction")
	bool IsAttracted() const { return bAttracted; }

	static bool IsActorAttractedToLight(const AActor* Actor);

	/** Adds the component to every placed BoneDog that lacks one (called once per run by the game mode). */
	static void SetupForWorld(UWorld* World);

	/** True when the dog sees the player right now (near sight), or with bRequireFlashlight, when the lit flashlight draws it. */
	bool IsDrawnBy(const ALostWithinCharacter* PlayerChar) const;

	/** Lets go before a cat reaction caches the movement settings: restores the walk speed, keeps the flee move. */
	void ReleaseForCatReaction() { Release(false); }

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void RegisterComponentTickFunctions(bool bRegister) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void Begin(ACharacter* Character);
	void Release(bool bStopMove);
	bool IsReactingToCat() const;
	bool HasClearView(const FVector& From, const FVector& To, const ALostWithinCharacter* PlayerChar) const;

	bool bAttracted = false;
	float SavedMaxWalkSpeed = 0.0f;
	float RepathTimer = 0.0f;
};
