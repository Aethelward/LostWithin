#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "MonsterLightAttractionComponent.generated.h"

class ACharacter;
class ALostWithinCharacter;
class UMonsterAIComponent;

/**
 * BoneDog's flashlight attraction. While the player's flashlight is on and the dog is in its beam (or near enough to
 * see the lit lens), the dog runs at the light in the same frame; the frame the flashlight turns off, or the dog loses
 * sight of it, the chase stops. While attracted, MonsterAI::CanRunNormalAI is false, so the Blueprint roam loop waits
 * and the Animation Blueprint plays the fastest gait. Cat reactions keep priority.
 */
UCLASS(ClassGroup=(AI), meta=(BlueprintSpawnableComponent))
class LOSTWITHIN_API UMonsterLightAttractionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMonsterLightAttractionComponent();

	/** The dog also notices a lit flashlight this close even when the beam is not on it (needs a clear view of the lens). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Light Attraction", meta=(ClampMin="0.0", Units="cm"))
	float AttractionRadius = 1500.0f;

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

	/** True when the given player's lit flashlight draws this dog right now (beam or nearby with a clear view). */
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
