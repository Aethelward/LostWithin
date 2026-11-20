// Copyright Epic Games, Inc. All Rights Reserved.

#include "AI/MonsterSenseProfile.h"

#include "AI/MonsterAIComponent.h"
#include "AI/MonsterHearingComponent.h"
#include "AI/MonsterLightAttractionComponent.h"
#include "AI/MonsterLightAverseComponent.h"
#include "AI/MonsterScentComponent.h"
#include "AI/MonsterSightFreezeComponent.h"
#include "AI/MonsterVibrationComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "LostWithin.h"

namespace
{
	template <typename TComponent>
	TComponent* FindOrAddSenseComponent(ACharacter* Monster, const TCHAR* Name)
	{
		if (TComponent* Existing = Monster->FindComponentByClass<TComponent>())
		{
			return Existing;
		}
		TComponent* Added = NewObject<TComponent>(Monster, MakeUniqueObjectName(Monster, TComponent::StaticClass(), Name));
		Monster->AddInstanceComponent(Added);
		Added->RegisterComponent();
		return Added;
	}

	void SilenceHearing(ACharacter* Monster)
	{
		if (UMonsterHearingComponent* Hearing = Monster->FindComponentByClass<UMonsterHearingComponent>())
		{
			Hearing->StopInvestigating();
			Hearing->Deactivate(); // CanHear rejects an inactive component, so no noise reaches it
		}
	}
}

EMonsterSense UMonsterSenseProfile::GetSenseForActor(const AActor* Actor)
{
	if (!IsValid(Actor))
	{
		return EMonsterSense::None;
	}
	const FString ClassName = Actor->GetClass()->GetName();
	if (ClassName.Contains(TEXT("BoneDog")))
	{
		return EMonsterSense::Smell;
	}
	if (ClassName.Contains(TEXT("Doll")))
	{
		return EMonsterSense::Sight;
	}
	if (ClassName.Contains(TEXT("Ghoul")))
	{
		return EMonsterSense::Hearing;
	}
	if (ClassName.Contains(TEXT("Lunger")) || ClassName.Contains(TEXT("Monster")))
	{
		return EMonsterSense::Touch;
	}
	return EMonsterSense::None;
}

EMonsterSense UMonsterSenseProfile::ApplyToActor(ACharacter* Monster)
{
	const EMonsterSense Sense = GetSenseForActor(Monster);
	ApplySenseToActor(Monster, Sense);
	return Sense;
}

void UMonsterSenseProfile::ApplySenseToActor(ACharacter* Monster, EMonsterSense Sense)
{
	if (!IsValid(Monster))
	{
		return;
	}
	switch (Sense)
	{
	case EMonsterSense::Smell:
		SilenceHearing(Monster);
		FindOrAddSenseComponent<UMonsterLightAttractionComponent>(Monster, TEXT("LightAttraction"));
		FindOrAddSenseComponent<UMonsterScentComponent>(Monster, TEXT("ScentSense"));
		break;
	case EMonsterSense::Sight:
		SilenceHearing(Monster);
		break;
	case EMonsterSense::Hearing:
		if (UMonsterHearingComponent* Hearing = Monster->FindComponentByClass<UMonsterHearingComponent>())
		{
			Hearing->HearingRangeMultiplier = GhoulHearingRangeMultiplier;
			Hearing->PinpointErrorInLight = 0.0f;
			Hearing->PinpointErrorInDark = 0.0f;
			if (!Hearing->IsActive())
			{
				Hearing->Activate();
			}
		}
		break;
	default:
		break;
	}
}

int32 UMonsterSenseProfile::ApplyForWorld(UObject* WorldContextObject)
{
	UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	if (!World)
	{
		return 0;
	}
	int32 Count = 0;
	for (TActorIterator<ACharacter> It(World); It; ++It)
	{
		if (It->FindComponentByClass<UMonsterAIComponent>() && ApplyToActor(*It) != EMonsterSense::None)
		{
			++Count;
		}
	}
	UE_LOG(LogLostWithin, Log, TEXT("MonsterSenseProfile: applied to %d monsters"), Count);
	return Count;
}

bool UMonsterSenseProfile::IsActorHunting(const AActor* Actor)
{
	if (!IsValid(Actor))
	{
		return false;
	}
	if (UMonsterHearingComponent::IsActorInvestigating(Actor) || UMonsterScentComponent::IsActorTrackingScent(Actor)
		|| UMonsterLightAttractionComponent::IsActorAttractedToLight(Actor)
		|| UMonsterLightAverseComponent::IsActorDarkStalkingPlayer(Actor)
		|| UMonsterSightFreezeComponent::IsActorRushingAtPlayer(Actor))
	{
		return true;
	}
	const UMonsterVibrationComponent* Vibration = Actor->FindComponentByClass<UMonsterVibrationComponent>();
	return Vibration && Vibration->IsInvestigating();
}
