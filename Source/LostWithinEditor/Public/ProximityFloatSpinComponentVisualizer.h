// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComponentVisualizer.h"

class UProximityFloatSpinComponent;

/** Hit proxy representing a clickable waypoint handle in the Level Editor viewport */
struct HFloatSpinWaypointVisProxy : public HComponentVisProxy
{
	DECLARE_HIT_PROXY();

	HFloatSpinWaypointVisProxy(const UActorComponent* InComponent, int32 InWaypointIndex)
		: HComponentVisProxy(InComponent, HPP_Foreground)
		, WaypointIndex(InWaypointIndex)
	{}

	int32 WaypointIndex = INDEX_NONE;
};

/**
 *  Level Editor visualizer that draws editable 3D diamond gizmos, directional path segments,
 *  and index labels for UProximityFloatSpinComponent::PathWaypoints.
 */
class FProximityFloatSpinComponentVisualizer : public FComponentVisualizer
{
public:
	FProximityFloatSpinComponentVisualizer();
	virtual ~FProximityFloatSpinComponentVisualizer() override = default;

	virtual void DrawVisualization(const UActorComponent* Component, const FSceneView* View, FPrimitiveDrawInterface* PDI) override;
	virtual void DrawVisualizationHUD(const UActorComponent* Component, const FViewport* Viewport, const FSceneView* View, FCanvas* Canvas) override;
	virtual bool VisProxyHandleClick(FEditorViewportClient* InViewportClient, HComponentVisProxy* VisProxy, const FViewportClick& Click) override;
	virtual void EndEditing() override;
	virtual bool GetWidgetLocation(const FEditorViewportClient* ViewportClient, FVector& OutLocation) const override;
	virtual bool HandleInputDelta(FEditorViewportClient* ViewportClient, FViewport* Viewport, FVector& DeltaTranslate, FRotator& DeltaRotate, FVector& DeltaScale) override;
	virtual bool HandleInputKey(FEditorViewportClient* ViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event) override;
	virtual void TrackingStopped(FEditorViewportClient* InViewportClient, bool bInDidMove) override;
	virtual UActorComponent* GetEditedComponent() const override;

private:
	UProximityFloatSpinComponent* GetEditedSpinComponent() const;

	FComponentPropertyPath EditedComponentPath;
	int32 SelectedWaypointIndex = INDEX_NONE;
	FProperty* PathWaypointsProperty = nullptr;
};

