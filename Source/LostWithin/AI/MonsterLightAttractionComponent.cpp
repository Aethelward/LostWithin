#include "AI/MonsterLightAttractionComponent.h"

#include "AI/MonsterAIComponent.h"
#include "AI/MonsterHearingComponent.h"
#include "AIController.h"
#include "Components/SpotLightComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "LostWithinCharacter.h"

namespace
{
	constexpr float LightAttractionFallbackChaseSpeed = 350.0f;
}

UMonsterLightAttractionComponent::UMonsterLightAttractionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

void UMonsterLightAttractionComponent::RegisterComponentTickFunctions(bool bRegister)
{
	Super::RegisterComponentTickFunctions(bRegister);
	// Tick before the dog's movement, so a toggle starts or stops the run in the same frame.
	if (ACharacter* Character = Cast<ACharacter>(GetOwner()); Character && Character->GetCharacterMovement())
	{
		if (bRegister) { Character->GetCharacterMovement()->AddTickPrerequisiteComponent(this); }
		else { Character->GetCharacterMovement()->RemoveTickPrerequisiteComponent(this); }
	}
}

void UMonsterLightAttractionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Release(false);
	Super::EndPlay(EndPlayReason);
}

bool UMonsterLightAttractionComponent::IsActorAttractedToLight(const AActor* Actor)
{
	const UMonsterLightAttractionComponent* Attraction = IsValid(Actor) ? Actor->FindComponentByClass<UMonsterLightAttractionComponent>() : nullptr;
	return Attraction && Attraction->IsAttracted();
}

void UMonsterLightAttractionComponent::SetupForWorld(UWorld* World)
{
	if (!World) { return; }
	for (TActorIterator<ACharacter> It(World); It; ++It)
	{
		if (!It->FindComponentByClass<UMonsterAIComponent>() || It->FindComponentByClass<UMonsterLightAttractionComponent>()
			|| !It->GetClass()->GetName().Contains(TEXT("BoneDog")))
		{
			continue;
		}
		UMonsterLightAttractionComponent* Attraction = NewObject<UMonsterLightAttractionComponent>(*It, TEXT("LightAttraction"));
		It->AddInstanceComponent(Attraction);
		Attraction->RegisterComponent();
	}
}

bool UMonsterLightAttractionComponent::IsReactingToCat() const
{
	const UMonsterAIComponent* MonsterAI = GetOwner() ? GetOwner()->FindComponentByClass<UMonsterAIComponent>() : nullptr;
	return MonsterAI && (MonsterAI->bCatIsNear || MonsterAI->bIsFleeing || MonsterAI->bIsRagdolled);
}

bool UMonsterLightAttractionComponent::HasClearView(const FVector& From, const FVector& To, const ALostWithinCharacter* PlayerChar) const
{
	const UWorld* World = GetWorld();
	if (!World) { return false; }
	FCollisionQueryParams Params(SCENE_QUERY_STAT(BoneDogLightTrace), false, PlayerChar);
	Params.AddIgnoredActor(GetOwner());
	if (PlayerChar)
	{
		// The held flashlight (and anything else the player carries) must not hide its own lens.
		TArray<AActor*> Carried;
		PlayerChar->GetAttachedActors(Carried, true, true);
		Params.AddIgnoredActors(Carried);
	}
	FHitResult Hit;
	return !World->LineTraceSingleByChannel(Hit, From, To, LightTraceChannel, Params);
}

bool UMonsterLightAttractionComponent::IsDrawnBy(const ALostWithinCharacter* PlayerChar) const
{
	const AActor* Owner = GetOwner();
	if (!IsValid(Owner) || !IsValid(PlayerChar) || PlayerChar->IsDead() || !PlayerChar->IsFlashlightOn())
	{
		return false;
	}
	const USpotLightComponent* Light = PlayerChar->GetFlashlightSpotLight();
	if (!Light || !Light->IsVisible())
	{
		return false;
	}

	const FVector LightPos = Light->GetComponentLocation();
	const FVector Eye = Owner->GetActorLocation() + FVector(0.0f, 0.0f, EyeHeight);
	const FVector ToDog = Eye - LightPos;
	const float Dist = ToDog.Size();
	if (Dist < KINDA_SMALL_NUMBER)
	{
		return true;
	}

	const float CosAngle = FVector::DotProduct(Light->GetForwardVector(), ToDog / Dist);
	const bool bInBeam = Dist <= Light->AttenuationRadius
		&& CosAngle >= FMath::Cos(FMath::DegreesToRadians(Light->OuterConeAngle));
	if (!bInBeam && Dist > AttractionRadius)
	{
		return false;
	}
	return HasClearView(LightPos, Eye, PlayerChar);
}

void UMonsterLightAttractionComponent::Begin(ACharacter* Character)
{
	bAttracted = true;
	RepathTimer = 0.0f;
	if (UMonsterHearingComponent* Hearing = Character->FindComponentByClass<UMonsterHearingComponent>())
	{
		Hearing->StopInvestigating();
	}
	if (UCharacterMovementComponent* Move = Character->GetCharacterMovement())
	{
		SavedMaxWalkSpeed = Move->MaxWalkSpeed;
		const UMonsterAIComponent* MonsterAI = Character->FindComponentByClass<UMonsterAIComponent>();
		const float Speed = ChaseSpeed > 0.0f ? ChaseSpeed
			: (MonsterAI && MonsterAI->PanicGaitSpeed > 0.0f ? MonsterAI->PanicGaitSpeed : LightAttractionFallbackChaseSpeed);
		Move->MaxWalkSpeed = Speed;
	}
}

void UMonsterLightAttractionComponent::Release(bool bStopMove)
{
	if (!bAttracted)
	{
		return;
	}
	bAttracted = false;
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!Character)
	{
		return;
	}
	if (bStopMove)
	{
		if (AAIController* AI = Cast<AAIController>(Character->GetController()))
		{
			AI->StopMovement();
		}
		if (UCharacterMovementComponent* Move = Character->GetCharacterMovement())
		{
			Move->StopActiveMovement();
		}
	}
	if (UCharacterMovementComponent* Move = Character->GetCharacterMovement(); Move && SavedMaxWalkSpeed > 0.0f)
	{
		Move->MaxWalkSpeed = SavedMaxWalkSpeed;
	}
}

void UMonsterLightAttractionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!Character)
	{
		return;
	}
	if (IsReactingToCat())
	{
		// The cat reaction owns movement and speed now; just let go.
		if (bAttracted) { bAttracted = false; }
		return;
	}

	ALostWithinCharacter* PlayerChar = Cast<ALostWithinCharacter>(UGameplayStatics::GetPlayerCharacter(this, 0));
	if (!IsDrawnBy(PlayerChar))
	{
		Release(true);
		return;
	}
	if (!bAttracted)
	{
		Begin(Character);
	}

	if (AAIController* AI = Cast<AAIController>(Character->GetController()))
	{
		RepathTimer -= DeltaTime;
		if (RepathTimer <= 0.0f)
		{
			RepathTimer = RepathInterval;
			AI->MoveToActor(PlayerChar, 5.0f, /*bStopOnOverlap=*/false);
		}
	}
	else
	{
		Character->AddMovementInput((PlayerChar->GetActorLocation() - Character->GetActorLocation()).GetSafeNormal2D(), 1.0f);
	}
}
