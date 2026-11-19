#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "LostWithinNarrativeComponent.generated.h"

class ULostWithinStoryHUD;

/** Per-player/run narrative; reads monster state, never drives it. */
UCLASS(ClassGroup=(Gameplay), meta=(BlueprintSpawnableComponent))
class LOSTWITHIN_API ULostWithinNarrativeComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	ULostWithinNarrativeComponent();
	UFUNCTION(BlueprintCallable, Category="Story") void NoteOpened(FName ID);
	UFUNCTION(BlueprintCallable, Category="Story") void NoteClosed(FName ID);
	UFUNCTION(BlueprintCallable, Category="Story") void NotifyTableApproach();
	UFUNCTION(BlueprintCallable, Category="Story") void NotifyEnding(bool bEscaped);
	UFUNCTION(BlueprintPure, Category="Story") int32 GetKnownDigitCount() const;
	UFUNCTION(BlueprintPure, Category="Story") bool HasReadNote(FName ID) const { return ReadNotes.Contains(ID); }
	UFUNCTION(BlueprintPure, Category="Story") FText GetObjective() const;
	UFUNCTION(BlueprintPure, Category="Story") FText GetSubtitle() const { return CurrentSubtitle; }
	UFUNCTION(BlueprintPure, Category="Story") FText GetTutorial() const { return Tutorial; }
	void QueueSubtitle(const FText& Text, int32 Priority = 10, float Seconds = 7.0f);
	/** Exposed as a deterministic state transition for battery regression coverage. */
	void ObserveBattery(bool bOwned, float Percent);
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
	struct FQueuedLine { FText Text; int32 Priority = 0; float Seconds = 0.0f; };
	void ObserveMonsters();
	void ShowTutorial(const FText& Text);
	void UpdateHUD();
	UPROPERTY(Transient) TObjectPtr<ULostWithinStoryHUD> HUD;
	TSet<FName> ReadNotes;
	TSet<FName> ReactedNotes;
	TArray<FQueuedLine> SubtitleQueue;
	FText CurrentSubtitle;
	FText Tutorial;
	float SubtitleRemaining = 0.0f;
	float TutorialRemaining = 0.0f;
	int32 CurrentPriority = 0;
	bool bTableTutorialShown = false;
	bool bFlashlightTutorialShown = false;
	bool bLowBatteryWarned = false;
	bool bEmptyBatteryWarned = false;
	bool bSawCreature = false;
	bool bWasThreatened = false;
	bool bEnding = false;
};
