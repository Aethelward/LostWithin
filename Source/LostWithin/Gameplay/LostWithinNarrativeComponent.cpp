#include "Gameplay/LostWithinNarrativeComponent.h"

#include "AI/MonsterAIComponent.h"
#include "AI/MonsterHearingComponent.h"
#include "AI/MonsterVibrationComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Gameplay/LostWithinStory.h"
#include "Inventory/InventoryComponent.h"
#include "LostWithinCharacter.h"
#include "LostWithinGameMode.h"
#include "UI/LostWithinStoryHUD.h"

ULostWithinNarrativeComponent::ULostWithinNarrativeComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.1f;
}

void ULostWithinNarrativeComponent::BeginPlay()
{
	Super::BeginPlay();
	const APawn* Pawn = Cast<APawn>(GetOwner());
	APlayerController* Controller = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	if (Controller && Controller->IsLocalController())
	{
		HUD = CreateWidget<ULostWithinStoryHUD>(Controller);
		if (HUD) { HUD->AddToViewport(15); }
	}
	ShowTutorial(NSLOCTEXT("LostWithinStory", "MovementTutorial", "[W][A][S][D] Move  |  [SPACE] Jump  |  [CTRL] Sprint  |  [SHIFT] Walk"));
	QueueSubtitle(NSLOCTEXT("LostWithinStory", "Awakening", "My head... Where am I? Cold concrete. I can't remember my name... or how I got down here."), 5, 7.0f);
	QueueSubtitle(NSLOCTEXT("LostWithinStory", "TableIntro", "A flashlight... and a note on the table. Someone left instructions for me."), 5, 5.5f);
	UpdateHUD();
}

void ULostWithinNarrativeComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	if (HUD) { HUD->RemoveFromParent(); HUD = nullptr; }
	Super::EndPlay(Reason);
}

int32 ULostWithinNarrativeComponent::GetKnownDigitCount() const
{
	return static_cast<int32>(ReadNotes.Contains(LostWithinStory::Warning))
		+ static_cast<int32>(ReadNotes.Contains(LostWithinStory::Report))
		+ static_cast<int32>(ReadNotes.Contains(LostWithinStory::FinalEntry));
}

FText ULostWithinNarrativeComponent::GetObjective() const
{
	if (bEnding) { return FText::GetEmpty(); }
	const UInventoryComponent* Inventory = GetOwner() ? GetOwner()->FindComponentByClass<UInventoryComponent>() : nullptr;
	const bool bHasLight = Inventory && Inventory->HasItem(FName(TEXT("Flashlight")));
	if (!bHasLight && !ReadNotes.Contains(LostWithinStory::Orientation))
	{
		return NSLOCTEXT("LostWithinStory", "InitialGoal", "Grab the flashlight and read the note on the table.");
	}
	if (!bHasLight) { return NSLOCTEXT("LostWithinStory", "LightGoal", "Take the flashlight from the starting table."); }
	if (!ReadNotes.Contains(LostWithinStory::Orientation)) { return NSLOCTEXT("LostWithinStory", "OrientationGoal", "Read the orientation note on the starting table."); }
	const int32 Known = GetKnownDigitCount();
	return Known == 3
		? NSLOCTEXT("LostWithinStory", "ExitGoal", "Reach the southeastern exit and enter the three digits in order.")
		: FText::Format(NSLOCTEXT("LostWithinStory", "ClueGoal", "Search toolboxes and scattered notes. Exit digits found: {0}/3."), FText::AsNumber(Known));
}

void ULostWithinNarrativeComponent::NoteOpened(FName ID)
{
	if (bEnding || ID.IsNone() || LostWithinStory::FormatNote(ID, {}).Title.IsEmpty()) { return; }
	ReadNotes.Add(ID);
	UpdateHUD();
}

void ULostWithinNarrativeComponent::NoteClosed(FName ID)
{
	if (bEnding || !ReadNotes.Contains(ID) || ReactedNotes.Contains(ID)) { return; }
	ReactedNotes.Add(ID);
	const ALostWithinGameMode* Mode = GetWorld() ? Cast<ALostWithinGameMode>(GetWorld()->GetAuthGameMode()) : nullptr;
	const TArray<int32> Digits = Mode ? Mode->GetCombinationDigits() : TArray<int32>();
	FText Reaction = LostWithinStory::FormatNote(ID, Digits).Reaction;
	if (ID == LostWithinStory::FinalEntry && GetKnownDigitCount() == 3)
	{
		Reaction = NSLOCTEXT("LostWithinStory", "AllDigitsReaction", "I have all three digits now. Time to reach the southeastern exit and get out of here!");
	}
	// A newly read note takes priority over the remaining awakening monologue.
	SubtitleQueue.RemoveAll([](const FQueuedLine& Line) { return Line.Priority < 10; });
	QueueSubtitle(Reaction, 15, 8.0f);
}

void ULostWithinNarrativeComponent::ShowTutorial(const FText& Text)
{
	Tutorial = Text;
	TutorialRemaining = 7.0f;
}

void ULostWithinNarrativeComponent::NotifyTableApproach()
{
	if (!bTableTutorialShown && !bEnding)
	{
		bTableTutorialShown = true;
		ShowTutorial(NSLOCTEXT("LostWithinStory", "InteractionTutorial", "[E] Interact / Read / Search / Collect  |  [TAB] Inventory  |  [ESC] Pause"));
	}
}

void ULostWithinNarrativeComponent::QueueSubtitle(const FText& Text, int32 Priority, float Seconds)
{
	if (Text.IsEmpty() || (bEnding && Priority < 100)) { return; }
	FQueuedLine Line{Text, Priority, FMath::Max(1.0f, Seconds)};
	if (CurrentSubtitle.IsEmpty() || Priority > CurrentPriority)
	{
		CurrentSubtitle = Line.Text;
		CurrentPriority = Line.Priority;
		SubtitleRemaining = Line.Seconds;
	}
	else if (!CurrentSubtitle.EqualTo(Text) && SubtitleQueue.Num() < 12)
	{
		SubtitleQueue.Add(MoveTemp(Line));
	}
}

void ULostWithinNarrativeComponent::ObserveBattery(bool bOwned, float Percent)
{
	if (!bOwned || bEnding) { return; }
	if (!bFlashlightTutorialShown)
	{
		bFlashlightTutorialShown = true;
		ShowTutorial(NSLOCTEXT("LostWithinStory", "FlashlightTutorial", "[F] Flashlight ON / OFF  |  [R] Load a spare battery  |  Two batteries power the light"));
	}
	if (Percent >= 20.0f) { bLowBatteryWarned = false; }
	if (Percent > 0.0f) { bEmptyBatteryWarned = false; }
	if (Percent <= 0.0f && !bEmptyBatteryWarned)
	{
		bEmptyBatteryWarned = true;
		bLowBatteryWarned = true;
		QueueSubtitle(NSLOCTEXT("LostWithinStory", "BatteryEmpty", "The light is dead... I need to load a battery."), 35, 6.0f);
	}
	else if (Percent > 0.0f && Percent < 20.0f && !bLowBatteryWarned)
	{
		bLowBatteryWarned = true;
		QueueSubtitle(NSLOCTEXT("LostWithinStory", "BatteryLow", "No, not now - the light is dying! I need a battery."), 30, 6.0f);
	}
}

void ULostWithinNarrativeComponent::NotifyEnding(bool bEscaped)
{
	if (bEnding) { return; }
	bEnding = true;
	SubtitleQueue.Empty();
	Tutorial = FText::GetEmpty();
	QueueSubtitle(bEscaped
		? NSLOCTEXT("LostWithinStory", "EscapeReaction", "I'm out... I survived Dr. Vance's sick experiment. Now I'm going to burn this place to the ground.")
		: NSLOCTEXT("LostWithinStory", "DeathReaction", "Dr. Vance's experiment claims another victim... Subject 09 terminated."), 100, 9.0f);
	UpdateHUD();
}

void ULostWithinNarrativeComponent::ObserveMonsters()
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	APlayerController* Controller = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	if (!Controller || !Controller->PlayerCameraManager || !GetWorld()) { return; }
	bool bThreatened = false;
	for (TActorIterator<ACharacter> It(GetWorld()); It; ++It)
	{
		if (!It->FindComponentByClass<UMonsterAIComponent>()) { continue; }
		bThreatened |= UMonsterHearingComponent::IsActorInvestigating(*It) || UMonsterVibrationComponent::IsActorInvestigating(*It);
		if (!bSawCreature)
		{
			FVector2D Screen;
			int32 Width = 0, Height = 0;
			Controller->GetViewportSize(Width, Height);
			const FVector Target = It->GetActorLocation();
			const FVector View = Controller->PlayerCameraManager->GetCameraLocation();
			if (FVector::DistSquared(Target, View) < FMath::Square(1800.0f)
				&& Controller->ProjectWorldLocationToScreen(Target, Screen)
				&& Screen.X >= 0 && Screen.X <= Width && Screen.Y >= 0 && Screen.Y <= Height)
			{
				FHitResult Hit;
				FCollisionQueryParams Params(SCENE_QUERY_STAT(StoryCreatureSighting), false, GetOwner());
				Params.AddIgnoredActor(*It);
				if (!GetWorld()->LineTraceSingleByChannel(Hit, View, Target, ECC_Visibility, Params))
				{
					bSawCreature = true;
					QueueSubtitle(NSLOCTEXT("LostWithinStory", "FirstCreature", "What the hell is that thing?! Stay back... I need to learn how it hunts."), 25, 7.0f);
				}
			}
		}
	}
	if (bThreatened && !bWasThreatened)
	{
		QueueSubtitle(NSLOCTEXT("LostWithinStory", "CreatureAlert", "Something is searching the halls... Stay alert. Each creature hunts differently."), 25, 6.0f);
	}
	else if (!bThreatened && bWasThreatened)
	{
		QueueSubtitle(NSLOCTEXT("LostWithinStory", "AreaQuiet", "It's quiet for now... Keep moving."), 20, 5.0f);
	}
	bWasThreatened = bThreatened;
}

void ULostWithinNarrativeComponent::UpdateHUD()
{
	// Possession can complete after component BeginPlay when a pawn is spawned during travel.
	if (!HUD)
	{
		const APawn* Pawn = Cast<APawn>(GetOwner());
		APlayerController* Controller = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
		if (Controller && Controller->IsLocalController())
		{
			HUD = CreateWidget<ULostWithinStoryHUD>(Controller);
			if (HUD) { HUD->AddToViewport(15); }
		}
	}
	if (HUD) { HUD->UpdateStory(GetObjective(), Tutorial, CurrentSubtitle); }
}

void ULostWithinNarrativeComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
	Super::TickComponent(DeltaTime, TickType, TickFunction);
	SubtitleRemaining -= DeltaTime;
	TutorialRemaining -= DeltaTime;
	if (TutorialRemaining <= 0.0f) { Tutorial = FText::GetEmpty(); }
	if (SubtitleRemaining <= 0.0f)
	{
		CurrentSubtitle = FText::GetEmpty();
		CurrentPriority = 0;
		if (!SubtitleQueue.IsEmpty())
		{
			const FQueuedLine Line = SubtitleQueue[0];
			SubtitleQueue.RemoveAt(0);
			QueueSubtitle(Line.Text, Line.Priority, Line.Seconds);
		}
	}
	if (!bEnding)
	{
		if (const ALostWithinCharacter* Character = Cast<ALostWithinCharacter>(GetOwner()))
		{
			ObserveBattery(Character->HasFlashlight(), Character->GetFlashlightBatteryPercent());
		}
		ObserveMonsters();
	}
	UpdateHUD();
}
