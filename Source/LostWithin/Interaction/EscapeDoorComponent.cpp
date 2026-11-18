// Copyright Epic Games, Inc. All Rights Reserved.

#include "Interaction/EscapeDoorComponent.h"

#include "Components/ChildActorComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "LostWithin.h"
#include "LostWithinCharacter.h"
#include "LostWithinGameMode.h"
#include "LostWithinPlayerController.h"
#include "TimerManager.h"
#include "UObject/UnrealType.h"

UEscapeDoorComponent::UEscapeDoorComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

bool UEscapeDoorComponent::CallBlueprintFunction(UObject* Target, FName FunctionName)
{
	UFunction* Function = IsValid(Target) ? Target->FindFunction(FunctionName) : nullptr;
	if (!Function)
	{
		return false;
	}
	if (Function->NumParms != 0)
	{
		UE_LOG(LogLostWithin, Warning, TEXT("%s::%s takes parameters; the escape door only calls parameterless functions."),
			*GetNameSafe(Target->GetClass()), *FunctionName.ToString());
		return false;
	}
	Target->ProcessEvent(Function, nullptr);
	return true;
}

bool UEscapeDoorComponent::SetStringProperty(UObject* Target, FName PropertyName, const FString& Value)
{
	FStrProperty* Property = IsValid(Target) ? FindFProperty<FStrProperty>(Target->GetClass(), PropertyName) : nullptr;
	if (!Property)
	{
		return false;
	}
	Property->SetPropertyValue_InContainer(Target, Value);
	return true;
}

bool UEscapeDoorComponent::SetObjectProperty(UObject* Target, FName PropertyName, UObject* Value)
{
	FObjectPropertyBase* Property = IsValid(Target) ? FindFProperty<FObjectPropertyBase>(Target->GetClass(), PropertyName) : nullptr;
	if (!Property || (Value && !Value->IsA(Property->PropertyClass)))
	{
		return false;
	}
	Property->SetObjectPropertyValue_InContainer(Target, Value);
	return true;
}

bool UEscapeDoorComponent::GetBoolProperty(const UObject* Target, FName PropertyName, bool& OutValue)
{
	const FBoolProperty* Property = IsValid(Target) ? FindFProperty<FBoolProperty>(Target->GetClass(), PropertyName) : nullptr;
	if (!Property)
	{
		return false;
	}
	OutValue = Property->GetPropertyValue_InContainer(Target);
	return true;
}

bool UEscapeDoorComponent::HasStringProperty(const UObject* Target, FName PropertyName)
{
	return IsValid(Target) && FindFProperty<FStrProperty>(Target->GetClass(), PropertyName) != nullptr;
}

void UEscapeDoorComponent::BeginPlay()
{
	Super::BeginPlay();

	AActor* Owner = GetOwner();
	Padlock = FindPadlock();
	AActor* Lock = Padlock.Get();
	if (!Lock)
	{
		UE_LOG(LogLostWithin, Warning, TEXT("%s has no combination padlock; the door opens only if its Blueprint unlocks it."),
			*GetNameSafe(Owner));
		return;
	}

	if (!SetObjectProperty(Lock, PadlockDoorPropertyName, Owner))
	{
		UE_LOG(LogLostWithin, Warning, TEXT("%s: could not point padlock %s at the door (property %s)."),
			*GetNameSafe(Owner), *GetNameSafe(Lock), *PadlockDoorPropertyName.ToString());
	}

	if (bApplyRunCombination)
	{
		const ALostWithinGameMode* GameMode = GetWorld() ? Cast<ALostWithinGameMode>(GetWorld()->GetAuthGameMode()) : nullptr;
		const FString Combination = GameMode ? GameMode->GetDoorCombination() : FString();
		if (!Combination.IsEmpty())
		{
			ApplyCombination(Combination);
		}
	}
}

void UEscapeDoorComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(EscapeTimerHandle);
	}
	Super::EndPlay(EndPlayReason);
}

AActor* UEscapeDoorComponent::FindPadlock() const
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return nullptr;
	}

	TArray<UChildActorComponent*> ChildActorComponents;
	Owner->GetComponents(ChildActorComponents);
	for (const UChildActorComponent* ChildComponent : ChildActorComponents)
	{
		AActor* Child = ChildComponent ? ChildComponent->GetChildActor() : nullptr;
		if (HasStringProperty(Child, PadlockCombinationPropertyName))
		{
			return Child;
		}
	}

	TArray<AActor*> AttachedActors;
	Owner->GetAttachedActors(AttachedActors, /*bResetArray=*/true, /*bRecursivelyIncludeAttachedActors=*/true);
	for (AActor* Attached : AttachedActors)
	{
		if (HasStringProperty(Attached, PadlockCombinationPropertyName))
		{
			return Attached;
		}
	}

	// A padlock placed separately in the level, pointing at this door (the pack's own setup).
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Candidate = *It;
			if (!HasStringProperty(Candidate, PadlockCombinationPropertyName))
			{
				continue;
			}
			const FObjectPropertyBase* DoorProperty = FindFProperty<FObjectPropertyBase>(Candidate->GetClass(), PadlockDoorPropertyName);
			if (DoorProperty && DoorProperty->GetObjectPropertyValue_InContainer(Candidate) == Owner)
			{
				return Candidate;
			}
		}
	}
	return nullptr;
}

bool UEscapeDoorComponent::ApplyCombination(const FString& Combination)
{
	AActor* Lock = Padlock.Get();
	if (!Lock || !SetStringProperty(Lock, PadlockCombinationPropertyName, Combination))
	{
		return false;
	}
	UE_LOG(LogLostWithin, Log, TEXT("%s: padlock combination set for this run."), *GetNameSafe(GetOwner()));
	return true;
}

bool UEscapeDoorComponent::IsDoorLocked() const
{
	bool bLocked = false;
	return GetBoolProperty(GetOwner(), DoorLockedPropertyName, bLocked) && bLocked;
}

void UEscapeDoorComponent::OnInteract_Implementation(AActor* Interactor)
{
	if (!CanInteract_Implementation(Interactor))
	{
		return;
	}

	if (IsDoorLocked())
	{
		AActor* Lock = Padlock.Get();
		if (!Lock || !CallBlueprintFunction(Lock, InteractFunctionName))
		{
			if (ALostWithinCharacter* Character = Cast<ALostWithinCharacter>(Interactor))
			{
				Character->ShowStatusMessage(NSLOCTEXT("LostWithin", "EscapeDoorLocked", "It's locked."));
			}
		}
		return;
	}

	ALostWithinGameMode* GameMode = GetWorld() ? Cast<ALostWithinGameMode>(GetWorld()->GetAuthGameMode()) : nullptr;
	if (GameMode && !GameMode->BeginEscape())
	{
		return;
	}

	if (!CallBlueprintFunction(GetOwner(), InteractFunctionName))
	{
		UE_LOG(LogLostWithin, Warning, TEXT("%s has no %s function; ending the run without the door animation."),
			*GetNameSafe(GetOwner()), *InteractFunctionName.ToString());
	}

	bEscaping = true;
	const APawn* Pawn = Cast<APawn>(Interactor);
	EscapingController = Pawn ? Pawn->GetController() : nullptr;

	UWorld* World = GetWorld();
	if (!World || EscapeDelay <= 0.0f)
	{
		CompleteEscape();
		return;
	}
	World->GetTimerManager().SetTimer(EscapeTimerHandle, this, &UEscapeDoorComponent::CompleteEscape, EscapeDelay, false);
}

void UEscapeDoorComponent::CompleteEscape()
{
	AController* Controller = EscapingController.Get();
	if (ALostWithinGameMode* GameMode = GetWorld() ? Cast<ALostWithinGameMode>(GetWorld()->GetAuthGameMode()) : nullptr)
	{
		GameMode->HandlePlayerEscaped(Controller);
	}
	else if (ALostWithinPlayerController* PlayerController = Cast<ALostWithinPlayerController>(Controller))
	{
		PlayerController->ShowEndScreen(/*bEscaped=*/true);
	}
}

FText UEscapeDoorComponent::GetInteractionPrompt_Implementation() const
{
	if (IsDoorLocked())
	{
		return Padlock.IsValid()
			? NSLOCTEXT("LostWithin", "EscapeDoorPadlockPrompt", "[LMB] Try the skull padlock")
			: NSLOCTEXT("LostWithin", "EscapeDoorLockedPrompt", "Locked");
	}
	return NSLOCTEXT("LostWithin", "EscapeDoorOpenPrompt", "[LMB] Open the door");
}

bool UEscapeDoorComponent::CanInteract_Implementation(AActor* Interactor) const
{
	if (bEscaping)
	{
		return false;
	}
	const ALostWithinGameMode* GameMode = GetWorld() ? Cast<ALostWithinGameMode>(GetWorld()->GetAuthGameMode()) : nullptr;
	return !GameMode || GameMode->IsRunActive();
}
