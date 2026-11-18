// Copyright Epic Games, Inc. All Rights Reserved.

#include "Interaction/ItemSpawnPoint.h"
#include "Components/ArrowComponent.h"
#include "Components/BillboardComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/Texture2D.h"
#include "UObject/ConstructorHelpers.h"

AItemSpawnPoint::AItemSpawnPoint()
{
	PrimaryActorTick.bCanEverTick = false;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Static);
	SetRootComponent(Root);

#if WITH_EDITORONLY_DATA
	bIsSpatiallyLoaded = false;

	SpriteComponent = CreateEditorOnlyDefaultSubobject<UBillboardComponent>(TEXT("Sprite"));
	if (SpriteComponent)
	{
		static ConstructorHelpers::FObjectFinder<UTexture2D> TargetIcon(TEXT("/Engine/EditorResources/S_TargetPoint.S_TargetPoint"));
		SpriteComponent->Sprite = TargetIcon.Object;
		SpriteComponent->SetupAttachment(Root);
		SpriteComponent->bIsScreenSizeScaled = true;
		SpriteComponent->SetRelativeScale3D(FVector(0.5f));
	}

	ArrowComponent = CreateEditorOnlyDefaultSubobject<UArrowComponent>(TEXT("Arrow"));
	if (ArrowComponent)
	{
		ArrowComponent->SetupAttachment(Root);
		ArrowComponent->ArrowSize = 0.5f;
		ArrowComponent->bIsScreenSizeScaled = true;
	}
#endif
}

void AItemSpawnPoint::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

#if WITH_EDITORONLY_DATA
	if (ArrowComponent)
	{
		// Gold for keys, pale paper for notes, so the two sets are easy to tell apart in the level.
		ArrowComponent->SetArrowColor(Kind == ELostWithinSpawnPointKind::Key
			? FLinearColor(1.0f, 0.78f, 0.15f)
			: FLinearColor(0.92f, 0.90f, 0.82f));
	}
#endif
}
