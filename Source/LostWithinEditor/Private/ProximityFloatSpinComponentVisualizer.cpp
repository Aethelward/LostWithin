// Copyright Epic Games, Inc. All Rights Reserved.

#include "ProximityFloatSpinComponentVisualizer.h"
#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "Components/ProximityFloatSpinComponent.h"
#include "Editor/UnrealEdEngine.h"
#include "EditorViewportClient.h"
#include "Engine/Engine.h"
#include "GameFramework/Actor.h"
#include "PrimitiveDrawingUtils.h"
#include "SceneManagement.h"
#include "SceneView.h"
#include "ScopedTransaction.h"
#include "UnrealEdGlobals.h"
#include "UObject/UnrealType.h"

IMPLEMENT_HIT_PROXY(HFloatSpinWaypointVisProxy, HComponentVisProxy);

FProximityFloatSpinComponentVisualizer::FProximityFloatSpinComponentVisualizer()
{
	PathWaypointsProperty = FindFProperty<FProperty>(
		UProximityFloatSpinComponent::StaticClass(),
		GET_MEMBER_NAME_CHECKED(UProximityFloatSpinComponent, PathWaypoints));
}

void FProximityFloatSpinComponentVisualizer::DrawVisualization(const UActorComponent* Component, const FSceneView* View, FPrimitiveDrawInterface* PDI)
{
	const UProximityFloatSpinComponent* SpinComp = Cast<UProximityFloatSpinComponent>(Component);
	if (!SpinComp || !PDI || SpinComp->PathWaypoints.IsEmpty())
	{
		return;
	}

	const AActor* Owner = SpinComp->GetOwner();
	if (!Owner)
	{
		return;
	}

	const FTransform AnchorTransform = UProximityFloatSpinComponent::GetDirectedPathAnchorTransform(Owner);
	const bool bIsEditedComp = (SpinComp == GetEditedSpinComponent());
	const FLinearColor ActivePathColor = SpinComp->bEnableDirectedPath ? FLinearColor(0.15f, 0.9f, 1.0f) : FLinearColor(0.55f, 0.65f, 0.75f);
	const FLinearColor UnselectedHandleColor = SpinComp->bEnableDirectedPath ? FLinearColor(1.0f, 0.65f, 0.1f) : FLinearColor(0.6f, 0.6f, 0.6f);
	const FLinearColor SelectedHandleColor = FLinearColor(1.0f, 1.0f, 0.15f);

	FVector PreviousWorldPoint = AnchorTransform.GetLocation();

	for (int32 Index = 0; Index < SpinComp->PathWaypoints.Num(); ++Index)
	{
		const FVector WaypointWorld = AnchorTransform.TransformPosition(SpinComp->PathWaypoints[Index]);

		PDI->SetHitProxy(nullptr);
		PDI->DrawLine(PreviousWorldPoint, WaypointWorld, ActivePathColor, SDPG_Foreground, 2.0f);

		const FVector Segment = WaypointWorld - PreviousWorldPoint;
		const float SegmentLen = Segment.Size();
		if (SegmentLen > 12.0f)
		{
			const FVector Dir = Segment / SegmentLen;
			const float ArrowLen = FMath::Clamp(SegmentLen * 0.3f, 12.0f, 36.0f);
			const FVector ArrowStart = (PreviousWorldPoint + WaypointWorld) * 0.5f - Dir * (ArrowLen * 0.5f);
			const FMatrix ArrowMatrix = FTransform(Dir.Rotation(), ArrowStart).ToMatrixNoScale();
			DrawDirectionalArrow(PDI, ArrowMatrix, ActivePathColor, ArrowLen, 8.0f, SDPG_Foreground, 1.5f);
		}

		const bool bIsSelected = bIsEditedComp && (SelectedWaypointIndex == Index);
		const FLinearColor HandleColor = bIsSelected ? SelectedHandleColor : UnselectedHandleColor;

		PDI->SetHitProxy(new HFloatSpinWaypointVisProxy(SpinComp, Index));
		DrawWireDiamond(PDI, FTranslationMatrix(WaypointWorld), bIsSelected ? 16.0f : 12.0f, HandleColor, SDPG_Foreground, bIsSelected ? 2.5f : 1.5f);
		PDI->DrawPoint(WaypointWorld, HandleColor, bIsSelected ? 12.0f : 8.0f, SDPG_Foreground);
		PDI->SetHitProxy(nullptr);

		PreviousWorldPoint = WaypointWorld;
	}
}

void FProximityFloatSpinComponentVisualizer::DrawVisualizationHUD(const UActorComponent* Component, const FViewport* Viewport, const FSceneView* View, FCanvas* Canvas)
{
	const UProximityFloatSpinComponent* SpinComp = Cast<UProximityFloatSpinComponent>(Component);
	if (!SpinComp || !View || !Canvas || !GEngine || SpinComp->PathWaypoints.IsEmpty())
	{
		return;
	}

	const AActor* Owner = SpinComp->GetOwner();
	if (!Owner)
	{
		return;
	}

	const FTransform AnchorTransform = UProximityFloatSpinComponent::GetDirectedPathAnchorTransform(Owner);
	const float DPIScale = FMath::Max(Canvas->GetDPIScale(), KINDA_SMALL_NUMBER);

	for (int32 Index = 0; Index < SpinComp->PathWaypoints.Num(); ++Index)
	{
		const FVector WaypointWorld = AnchorTransform.TransformPosition(SpinComp->PathWaypoints[Index]);
		FVector2D PixelPos = FVector2D::ZeroVector;
		if (View->WorldToPixel(WaypointWorld + FVector(0.0f, 0.0f, 18.0f), PixelPos))
		{
			const FString Label = FString::Printf(TEXT("[%d]"), Index);
			FCanvasTextItem TextItem(PixelPos / DPIScale, FText::FromString(Label), GEngine->GetSmallFont(), FLinearColor::White);
			TextItem.EnableShadow(FLinearColor::Black);
			Canvas->DrawItem(TextItem);
		}
	}
}

bool FProximityFloatSpinComponentVisualizer::VisProxyHandleClick(FEditorViewportClient* InViewportClient, HComponentVisProxy* VisProxy, const FViewportClick& Click)
{
	if (!VisProxy || !VisProxy->Component.IsValid())
	{
		return false;
	}

	const UProximityFloatSpinComponent* SpinComp = Cast<UProximityFloatSpinComponent>(VisProxy->Component.Get());
	if (!SpinComp)
	{
		return false;
	}

	EditedComponentPath = FComponentPropertyPath(SpinComp);
	if (!EditedComponentPath.IsValid())
	{
		EditedComponentPath.Reset();
		SelectedWaypointIndex = INDEX_NONE;
		return false;
	}

	if (VisProxy->IsA(HFloatSpinWaypointVisProxy::StaticGetType()))
	{
		const HFloatSpinWaypointVisProxy* WaypointProxy = static_cast<const HFloatSpinWaypointVisProxy*>(VisProxy);
		if (SpinComp->PathWaypoints.IsValidIndex(WaypointProxy->WaypointIndex))
		{
			SelectedWaypointIndex = WaypointProxy->WaypointIndex;
			return true;
		}
	}

	SelectedWaypointIndex = INDEX_NONE;
	return false;
}

void FProximityFloatSpinComponentVisualizer::EndEditing()
{
	EditedComponentPath.Reset();
	SelectedWaypointIndex = INDEX_NONE;
}

UActorComponent* FProximityFloatSpinComponentVisualizer::GetEditedComponent() const
{
	return EditedComponentPath.GetComponent();
}

UProximityFloatSpinComponent* FProximityFloatSpinComponentVisualizer::GetEditedSpinComponent() const
{
	return Cast<UProximityFloatSpinComponent>(EditedComponentPath.GetComponent());
}

bool FProximityFloatSpinComponentVisualizer::GetWidgetLocation(const FEditorViewportClient* ViewportClient, FVector& OutLocation) const
{
	const UProximityFloatSpinComponent* SpinComp = GetEditedSpinComponent();
	if (!SpinComp || !SpinComp->PathWaypoints.IsValidIndex(SelectedWaypointIndex))
	{
		return false;
	}

	const AActor* Owner = SpinComp->GetOwner();
	if (!Owner)
	{
		return false;
	}

	const FTransform AnchorTransform = UProximityFloatSpinComponent::GetDirectedPathAnchorTransform(Owner);
	OutLocation = AnchorTransform.TransformPosition(SpinComp->PathWaypoints[SelectedWaypointIndex]);
	return true;
}

bool FProximityFloatSpinComponentVisualizer::HandleInputDelta(FEditorViewportClient* ViewportClient, FViewport* Viewport, FVector& DeltaTranslate, FRotator& DeltaRotate, FVector& DeltaScale)
{
	UProximityFloatSpinComponent* SpinComp = GetEditedSpinComponent();
	if (!SpinComp || !SpinComp->PathWaypoints.IsValidIndex(SelectedWaypointIndex))
	{
		return false;
	}

	const AActor* Owner = SpinComp->GetOwner();
	if (!Owner || DeltaTranslate.IsZero())
	{
		return false;
	}

	const FTransform AnchorTransform = UProximityFloatSpinComponent::GetDirectedPathAnchorTransform(Owner);
	const FVector LocalDelta = AnchorTransform.InverseTransformVectorNoScale(DeltaTranslate);

	SpinComp->Modify();
	SpinComp->PathWaypoints[SelectedWaypointIndex] += LocalDelta;
	NotifyPropertyModified(SpinComp, PathWaypointsProperty, EPropertyChangeType::Interactive);

	if (GUnrealEd)
	{
		GUnrealEd->RedrawLevelEditingViewports(true);
	}
	return true;
}

bool FProximityFloatSpinComponentVisualizer::HandleInputKey(FEditorViewportClient* ViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event)
{
	if (Event == IE_Pressed && (Key == EKeys::Delete || Key == EKeys::BackSpace))
	{
		UProximityFloatSpinComponent* SpinComp = GetEditedSpinComponent();
		if (SpinComp && SpinComp->PathWaypoints.IsValidIndex(SelectedWaypointIndex))
		{
			const FScopedTransaction Transaction(NSLOCTEXT("LostWithinEditor", "DeleteCatPathWaypoint", "Delete Cat Path Waypoint"));
			SpinComp->Modify();
			SpinComp->PathWaypoints.RemoveAt(SelectedWaypointIndex);

			if (SpinComp->PathWaypoints.IsEmpty())
			{
				SelectedWaypointIndex = INDEX_NONE;
			}
			else
			{
				SelectedWaypointIndex = FMath::Clamp(SelectedWaypointIndex, 0, SpinComp->PathWaypoints.Num() - 1);
			}

			NotifyPropertyModified(SpinComp, PathWaypointsProperty, EPropertyChangeType::ValueSet);
			if (GUnrealEd)
			{
				GUnrealEd->RedrawLevelEditingViewports(true);
			}
			return true;
		}
	}

	return false;
}

void FProximityFloatSpinComponentVisualizer::TrackingStopped(FEditorViewportClient* InViewportClient, bool bInDidMove)
{
	if (bInDidMove)
	{
		if (UProximityFloatSpinComponent* SpinComp = GetEditedSpinComponent())
		{
			NotifyPropertyModified(SpinComp, PathWaypointsProperty, EPropertyChangeType::ValueSet);
		}
	}
}

