// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once

#include "CoreMinimal.h"
#include "SEditorViewport.h"
#include "EditorViewportClient.h"

class FDreamUISequenceEditorToolkit;
class SDreamUISequencePreviewViewport;
class UDreamWidget;

/**
 * The viewport client for the DreamUI Animation editor's preview: renders the toolkit's private
 * preview world, ticks it (nothing else does -- the world belongs to no world context), picks
 * widgets by rect, and drives the engine transform gizmo so dragging a widget auto-keys the
 * animation instead of editing the prefab.
 */
class FDreamUISequencePreviewViewportClient : public FEditorViewportClient
{
public:
	FDreamUISequencePreviewViewportClient(TWeakPtr<FDreamUISequenceEditorToolkit> InToolkit, const TSharedRef<SDreamUISequencePreviewViewport>& InViewport);

	//~ FEditorViewportClient interface
	virtual UWorld* GetWorld() const override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void Draw(const FSceneView* View, FPrimitiveDrawInterface* PDI) override;
	virtual void ProcessClick(FSceneView& View, HHitProxy* HitProxy, FKey Key, EInputEvent Event, uint32 HitX, uint32 HitY) override;
	virtual UE::Widget::EWidgetMode GetWidgetMode() const override;
	virtual FVector GetWidgetLocation() const override;
	virtual FMatrix GetWidgetCoordSystem() const override;
	virtual ECoordSystem GetWidgetCoordSystemSpace() const override { return COORD_Local; }
	virtual bool InputWidgetDelta(FViewport* InViewport, EAxisList::Type InCurrentAxis, FVector& Drag, FRotator& Rot, FVector& Scale) override;
	virtual void TrackingStarted(const struct FInputEventState& InInputState, bool bIsDragging, bool bNudge) override;
	virtual void TrackingStopped() override;

	/** Frame the camera on the loaded preview tree. Called by the toolkit after each rebuild. */
	void FocusOnPreview();

private:
	/** The widgets the gizmo acts on: the toolkit's viewport selection, still alive. */
	void GetGizmoWidgets(TArray<UDreamWidget*>& OutWidgets) const;
	void ApplyDeltaToWidgets(const FVector& Drag, const FRotator& Rot, const FVector& Scale);

	TWeakPtr<FDreamUISequenceEditorToolkit> ToolkitPtr;

	/** Click-through cycling state, same scheme as the designer viewport. */
	int32 ClickCycleIndex = INDEX_NONE;
	FIntPoint LastClickPixel = FIntPoint(-1, -1);

	/** Which transform properties the current drag touched, so TrackingStopped keys exactly those. */
	bool bDragTouchedRenderTranslation = false;
	bool bDragTouchedRelativeLocation = false;
	bool bDragTouchedRotation = false;
	bool bDragTouchedScale = false;
	bool bIsTrackingDrag = false;
};

/** The Slate viewport tab content for the DreamUI Animation editor. */
class SDreamUISequencePreviewViewport : public SEditorViewport
{
public:
	SLATE_BEGIN_ARGS(SDreamUISequencePreviewViewport) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedPtr<FDreamUISequenceEditorToolkit> InToolkit);

	TSharedPtr<FDreamUISequencePreviewViewportClient> GetPreviewClient() const { return PreviewClient; }

protected:
	//~ SEditorViewport interface
	virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override;
	virtual void OnFocusViewportToSelection() override;

private:
	TWeakPtr<FDreamUISequenceEditorToolkit> ToolkitPtr;
	TSharedPtr<FDreamUISequencePreviewViewportClient> PreviewClient;
};
