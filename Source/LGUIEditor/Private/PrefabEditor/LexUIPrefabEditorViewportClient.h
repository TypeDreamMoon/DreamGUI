// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PreviewScene.h"
#include "EditorViewportClient.h"
#include "Animation/AnimInstance.h"
#include "LevelEditorViewport.h"

class ULexVisual;
class ULexWidget;
class FLexUIPrefabEditor;
class ULexUIPrefab;

/** Viewport client for editor viewports. Contains common functionality for camera movement, rendering debug information, etc. */
class FLexUIPrefabEditorViewportClient : public FEditorViewportClient
{
public:
	FLexUIPrefabEditorViewportClient(TWeakPtr<FLexUIPrefabEditor> InPrefabEditorPtr, const TSharedRef<class SLexUIPrefabEditorViewport>& InEditorViewportPtr);

	virtual ~FLexUIPrefabEditorViewportClient()override;

	// FViewElementDrawer interface
	virtual void Draw(const FSceneView* View, FPrimitiveDrawInterface* PDI) override;
	// End of FViewElementDrawer interface

	// FEditorViewportClient interface
	virtual void DrawCanvas(FViewport& InViewport, FSceneView& View, FCanvas& Canvas)override;
	virtual void ReceivedFocus(FViewport* InViewport) override;
	virtual void LostFocus(FViewport* InViewport) override;
	virtual void ProcessClick(FSceneView& View, HHitProxy* HitProxy, FKey Key, EInputEvent Event, uint32 HitX, uint32 HitY) override;
	virtual bool InputKey(const FInputKeyEventArgs& EventArgs) override;
	virtual void TrackingStarted(const struct FInputEventState& InInputState, bool bIsDragging, bool bNudge) override;
	virtual void TrackingStopped() override;
	virtual void AbortTracking() override;

	virtual void CapturedMouseMove(FViewport* InViewport, int32 InMouseX, int32 InMouseY) override;
	virtual void MouseEnter(FViewport* Viewport, int32 x, int32 y) override;
	virtual void MouseMove(FViewport* InViewport, int32 x, int32 y) override;
	virtual void MouseLeave(FViewport* Viewport) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual bool InputWidgetDelta(FViewport* InViewport, EAxisList::Type InCurrentAxis, FVector& Drag, FRotator& Rot, FVector& Scale) override;

	virtual UE::Widget::EWidgetMode GetWidgetMode() const override;
	virtual FVector GetWidgetLocation() const override;
	virtual FMatrix GetWidgetCoordSystem() const override;
 
	virtual void SetViewportType(ELevelViewportType InViewportType) override;
	// End of FEditorViewportClient interface

	/**
	 * Get the elements (from the current selection set) that this viewport can manipulate (eg, via the transform gizmo).
	 */
	FTypedElementListConstRef GetElementsToManipulate(const bool bForceRefresh = false);
	/** Cache the list of elements to manipulate based on the current selection set. */
	void CacheElementsToManipulate(const bool bForceRefresh = false);
	/** Reset the list of elements to manipulate */
	void ResetElementsToManipulate(const bool bClearList = true);

	/** Reset the list of elements to manipulate, because the selection set they were cached from has changed */
	void ResetElementsToManipulateFromSelectionChange(const UTypedElementSelectionSet* InSelectionSet);

	/** Reset the list of elements to manipulate, because the typed element registry is about to process deferred deletion */
	void ResetElementsToManipulateFromProcessingDeferredElementsToDestroy();

	/** Get the selection set that associated with our level editor. */
	const UTypedElementSelectionSet* GetSelectionSet() const;
	UTypedElementSelectionSet* GetMutableSelectionSet() const;

	/**
	 * Returns the horizontal axis for this viewport.
	 */
	EAxisList::Type GetHorizAxis() const;

	/**
	 * Returns the vertical axis for this viewport.
	 */
	EAxisList::Type GetVertAxis() const;

	virtual void NudgeSelectedObjects(const struct FInputEventState& InputState) override;

	void ApplyDeltaToActors(const FVector& InDrag, const FRotator& InRot, const FVector& InScale);
	void ApplyDeltaToActor(AActor* InActor, const FVector& InDeltaDrag, const FRotator& InDeltaRot, const FVector& InDeltaScale);
	void ApplyDeltaToComponent(USceneComponent* InComponent, const FVector& InDeltaDrag, const FRotator& InDeltaRot, const FVector& InDeltaScale);

	void ApplyDeltaToSelectedElements(const FTransform& InDeltaTransform);
	void ApplyDeltaToElement(const FTypedElementHandle& InElementHandle, const FTransform& InDeltaTransform);

	void TickWorld(float DeltaSeconds);

	bool FocusViewportToTargets();
	TSharedPtr<FLexUIPrefabEditor> GetPrefabEditor() const { return PrefabEditorPtr.Pin(); }
	ULexWidget* GetWidgetUnderCursor(int32 PixelX, int32 PixelY, bool bRespectDesignerLock = true);
	bool GetDropWorldPosition(int32 PixelX, int32 PixelY, ULexWidget* ParentWidget, FVector& OutWorldPosition);
	void SetPaletteDropPreview(ULexWidget* Widget);
	void ClearPaletteDropPreview();

	// Begin override because PreviewScene is nullptr
	virtual UWorld* GetWorld()const override;
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual void DrawPreviewLightVisualization(const FSceneView* View, FPrimitiveDrawInterface* PDI)override;
	virtual FLinearColor GetBackgroundColor() const override;
	virtual bool Internal_InputAxis(FViewport* InViewport, FInputDeviceId DeviceID, FKey Key, float Delta, float DeltaTime, int32 NumSamples, bool bGamepad) override;
	// End
private:
	enum class EDesignerHandle : uint8
	{
		None,
		Move,
		Left,
		Right,
		Top,
		Bottom,
		TopLeft,
		TopRight,
		BottomLeft,
		BottomRight,
		Pivot,
	};
	struct FDesignerWidgetSnapshot
	{
		TWeakObjectPtr<ULexWidget> Widget;
		FVector2D AnchoredPosition = FVector2D::ZeroVector;
		FVector2D Pivot = FVector2D(0.5f);
		float Width = 0.0f;
		float Height = 0.0f;
		FTransform WorldTransform = FTransform::Identity;
		FTransform PlaneTransform = FTransform::Identity;
		FVector StartPlanePoint = FVector::ZeroVector;
	};
	bool HandleDesignerInputKey(const FInputKeyEventArgs& EventArgs);
	void TrackRightMouseMovement(int32 MouseX, int32 MouseY);
	void UpdateDesignerDrag();
	void FinishDesignerDrag(bool bCancel);
	void DrawDesignerOverlay(FViewport& InViewport, FSceneView& View, FCanvas& Canvas);
	bool UpdateDesignerScreenGeometry(FSceneView& View);
	EDesignerHandle HitTestDesignerHandle(const FVector2D& PixelPosition) const;
	bool IntersectDesignerPlane(const FVector2D& PixelPosition, const FTransform& PlaneTransform, FVector& OutPoint) const;
	void DrawWidgetScreenOutline(ULexWidget* InWidget, FSceneView& View, FCanvas& Canvas, const FLinearColor& Color, float Thickness = 1.0f) const;
	void DrawDesignerCanvasBoundary(FViewport& InViewport, FSceneView& View, FCanvas& Canvas) const;
	TArray<FVector2D> DesignerScreenCorners;
	TMap<EDesignerHandle, FVector2D> DesignerHandlePositions;
	FBox2D DesignerScreenBounds = FBox2D(EForceInit::ForceInit);
	EDesignerHandle ActiveDesignerHandle = EDesignerHandle::None;
	bool bDesignerDragging = false;
	bool bDesignerChanged = false;
	FVector2D DesignerDragStartPixel = FVector2D::ZeroVector;
	TArray<FDesignerWidgetSnapshot> DesignerSnapshots;
	TUniquePtr<class FScopedTransaction> DesignerTransaction;
	TOptional<float> DesignerGuideX;
	TOptional<float> DesignerGuideY;

	int PrevMouseX = 0, PrevMouseY = 0;
	int IndexOfClickSelectUI = INDEX_NONE;
	TUniquePtr<class FLexUITransformWidget> TransformWidget = nullptr;
	TWeakObjectPtr<ULexWidget> PaletteDropPreviewWidget;
	FDelegateHandle OnSelectionChangedDelegateHandle;

	TWeakPtr<FLexUIPrefabEditor> PrefabEditorPtr;
	TWeakPtr<class SLexUIPrefabEditorViewport> EditorViewportPtr;
	FIntPoint RightMouseDownPosition = FIntPoint::ZeroValue;
	bool bRightMouseButtonDown = false;
	bool bRightMouseMoved = false;
	// Are we currently manipulating something?
	bool bManipulating = false;
	FTrackingTransaction TrackingTransaction;
	/**
	 * true when a brush is being transformed by its Widget
	 */
	bool					bIsTrackingBrushModification;
	/** true if gizmo manipulation was started from a tracking event */
	bool					bHasBegunGizmoManipulation;
	/** Whether this viewport recently received focus. Used to determine whether component selection is permissible. */
	bool bReceivedFocusRecently;

	/** The elements (from the current selection set) that this viewport can manipulate (eg, via the transform gizmo) */
	bool bHasCachedElementsToManipulate = false;
	FTypedElementListRef CachedElementsToManipulate;

	ULexUIPrefab* GetPrefabBeingEdited()const;
	/**
	 * Collects the set of components and actors on which to apply move operations during or after drag operations.
	 */
	void GetSelectedActorsAndComponentsForMove(TArray<AActor*>& OutActorsToMove, TArray<USceneComponent*>& OutComponentsToMove) const;
	/**
	 * Determines if it is valid to move an actor in this viewport.
	 *
	 * @param InActor - the actor that the viewport may be interested in moving.
	 * @returns true if it is valid for this viewport to update the given actor's transform.
	 */
	bool CanMoveActorInViewport(const AActor* InActor) const;

	/** @return	Returns true if the delta tracker was used to modify any selected actors or BSP.  Must be called before EndTracking(). */
	bool HaveSelectedObjectsBeenChanged() const;

	/** Handle to a timer event raised in ::ReceivedFocus*/
	FTimerHandle			FocusTimerHandle;
};
