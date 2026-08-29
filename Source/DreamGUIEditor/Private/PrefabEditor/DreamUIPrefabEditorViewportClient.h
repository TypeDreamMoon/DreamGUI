// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once

#include "CoreMinimal.h"
#include "PreviewScene.h"
#include "EditorViewportClient.h"
#include "Animation/AnimInstance.h"
#include "LevelEditorViewport.h"

class UDreamVisual;
class UDreamWidget;
class FDreamWidgetBlueprintEditor;
class UDreamWidgetBlueprint;
class UDreamUIPrefab;
struct FDreamLayoutControlAnchorData;

/** Viewport client for editor viewports. Contains common functionality for camera movement, rendering debug information, etc. */
class FDreamUIPrefabEditorViewportClient : public FEditorViewportClient
{
public:
	FDreamUIPrefabEditorViewportClient(TWeakPtr<FDreamWidgetBlueprintEditor> InPrefabEditorPtr, const TSharedRef<class SDreamUIPrefabEditorViewport>& InEditorViewportPtr);

	virtual ~FDreamUIPrefabEditorViewportClient()override;

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
	/** The selection the transform gizmo acts on: selected, not locked, not hidden in the designer. */
	void GetGizmoWidgets(TArray<UDreamWidget*>& OutWidgets) const;
	/** Apply one frame of the engine widget's delta to that selection. */
	void ApplyDeltaToSelectedWidgets(const FVector& Drag, const FRotator& Rot, const FVector& Scale);

	virtual UE::Widget::EWidgetMode GetWidgetMode() const override;
	virtual FVector GetWidgetLocation() const override;
	virtual FMatrix GetWidgetCoordSystem() const override;
 
	virtual void SetViewportType(ELevelViewportType InViewportType) override;
	virtual FSceneView* CalcSceneView(FSceneViewFamily* ViewFamily, const int32 StereoViewIndex = INDEX_NONE) override;
	// End of FEditorViewportClient interface

	/**
	 * Whether the 2D view should project through the canvas's own virtual camera instead of its
	 * orthographic one. False for the 3D view (which has a real camera the author is steering), for
	 * canvases with no virtual camera to borrow, and for canvases that cannot see their own plane.
	 */
	bool ShouldUseCanvasView()const;

	/**
	 * Stand the editor camera exactly where the canvas's own virtual camera stands, with its lens.
	 * Perspective bakes geometry for that eye, so this is the only pose from which the editor shows
	 * the foreshortening play shows. Switches to the 3D view first -- an ortho view has no eye.
	 */
	void FrameFromCanvasEye();
	bool CanFrameFromCanvasEye()const;
	/** The preview's root canvas, or null if it or its widget is not in a state safe to ask. */
	class UDreamCanvas* GetPreviewRootCanvas()const;
	/** Keep the editor lens equal to the canvas lens, so the 3D view is at least calibrated. */
	void SyncViewFOVToCanvas();

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
	TSharedPtr<FDreamWidgetBlueprintEditor> GetPrefabEditor() const { return PrefabEditorPtr.Pin(); }
	/**
	 * Which of a widget's own axes something else is deciding: its parent's container, plus its own
	 * layout-self. Free of any viewport state so the handle policy can be tested directly.
	 */
	static FDreamLayoutControlAnchorData GetEffectiveLayoutControl(const UDreamWidget* InWidget);
	/**
	 * Whether the author may still set this widget's anchors, per axis. An anchor only says where in
	 * the parent a rect is measured from, so on an axis whose position or size is already being
	 * decided for the child there is nothing left for one to say.
	 */
	static void GetAnchorEditableAxes(const UDreamWidget* InWidget, bool& bOutHorizontal, bool& bOutVertical);
	/**
	 * Whether a Move gesture over this selection can write anything the next arrange will keep. A
	 * position an arranger owns is re-decided immediately -- SetAnchoredPosition re-dirties the
	 * parent -- so a gesture that can only touch owned axes buys an undo step, a dirty asset and
	 * possibly an animation key for a move that never appears on screen.
	 */
	static bool CanMoveSelection(TConstArrayView<UDreamWidget*> InWidgets);
	/** The part of a move that survives the next arrange: zeroed on every axis an arranger decides. */
	static FVector2D FilterMoveDelta(const FVector2D& InDelta, const FDreamLayoutControlAnchorData& InControl);

	/** One widget under a Move drag, with the pointer ray already intersected against its own plane. */
	struct FMoveDragTarget
	{
		FTransform PlaneTransform = FTransform::Identity;
		FVector StartPlanePoint = FVector::ZeroVector;
		FVector CurrentPlanePoint = FVector::ZeroVector;
		FVector2D StartPosition = FVector2D::ZeroVector;
		bool bHorizontalFree = true;
		bool bVerticalFree = true;
	};
	struct FMoveDragResult
	{
		FVector2D Position = FVector2D::ZeroVector;
		/** The grid moved this axis, which is the only thing a guide line has to say. */
		bool bSnappedHorizontal = false;
		bool bSnappedVertical = false;
	};
	/**
	 * Where a Move drag puts each target. Every target is MEASURED in its own parent's space, because
	 * the same numbers are a different distance in parents of differing scale or rotation; the grid's
	 * correction is SNAPPED once per axis, off the first target free to be written on that axis,
	 * because a selection is dragged as one shape. A target an arranger owns on an axis never
	 * receives that axis, so a correction read off it would bend the rest of the selection towards a
	 * number nothing was ever going to land on. InGridSize <= 0 means no snapping.
	 */
	static void ResolveMoveDrag(TConstArrayView<FMoveDragTarget> InTargets, float InGridSize, TArray<FMoveDragResult>& OutResults);

	/**
	 * Whether dropping this selection into InNewParent is a reparent the hierarchy would accept. The
	 * refusals are the tree's: a widget onto itself, onto its own descendant, or into a parent with
	 * no room. A widget already sitting in InNewParent is not a reparent at all, so it refuses that
	 * too and the drag stays the plain move it has always been.
	 */
	static bool CanReparentSelectionUnder(TConstArrayView<UDreamWidget*> InWidgets, const UDreamWidget* InNewParent);

	/**
	 * Where a drag hovering over InHit would drop InDragged, or null when nothing there would take
	 * it. The resolve walks up from the hit to the nearest ancestor holding a container, so the lock
	 * is asked of that ancestor as well as of the hit -- otherwise a locked panel is reachable
	 * through any unlocked child it happens to hold. A lock refuses the drop where it is rather than
	 * sending the selection somewhere else; only a chain with no container at all falls back to
	 * InRoot, which is the container-less prefab's answer and the one the palette drop already gives.
	 */
	static UDreamWidget* ResolveDragDropContainer(UDreamWidget* InHit, UDreamWidget* InRoot, TConstArrayView<UDreamWidget*> InDragged, TFunctionRef<bool(const UDreamWidget*)> InIsLocked);

	/**
	 * The cycle index a click at this pixel should walk from. Click-through only means anything while
	 * the pick ray stays put, so a click anywhere else starts its own stack at the top. The
	 * mouse-move reset cannot stand alone: it fires on moves, and a click can land on a pixel no move
	 * event was delivered for.
	 */
	static int32 ResolveClickCycleIndex(const FIntPoint& InLastClickPixel, const FIntPoint& InClickPixel, int32 InCurrentIndex);

	/**
	 * The rect a device's title-safe area leaves inside a canvas this size, in the canvas's own local
	 * space (origin at its pivot, X right, Y up). InSafePadding is (Left, Top, Right, Bottom) in
	 * design units -- the order FMargin and FDisplayMetrics::TitleSafePaddingSize both use.
	 */
	static FBox2D GetSafeZoneLocalRect(const FVector2D& InCanvasSize, const FVector2D& InCanvasPivot, const FVector4& InSafePadding);
	/** An anchor fraction pulled onto the quarter gridline it is within InTolerance of. */
	static double SnapAnchorFraction(double InFraction, double InTolerance);
	/**
	 * Write new anchors while the widget's rect stays exactly where it is. Anchors and the offsets
	 * describe that one rect between them, so moving an anchor line without re-measuring the offsets
	 * against it drags the rect along by however far the line travelled.
	 */
	static void SetAnchorsPreservingRect(UDreamWidget* InWidget, const FVector2D& InAnchorMin, const FVector2D& InAnchorMax);

	/** What a finished marquee does to the selection it was dragged over. */
	enum class EMarqueeMode : uint8
	{
		Replace,
		Add,
		Remove,
	};
	/** Does a widget's projected rect meet the marquee box? InQuad is the four corners, in ring order. */
	static bool DoesMarqueeMeetQuad(const FBox2D& InMarquee, TConstArrayView<FVector2D> InQuad);
	/** Fold what a marquee caught into what was already selected. */
	static void CombineMarqueeSelection(EMarqueeMode InMode, TConstArrayView<UDreamWidget*> InCurrent, TConstArrayView<UDreamWidget*> InCaught, TSet<UDreamWidget*>& OutSelection);

	/** World-space pick ray through a viewport pixel. */
	bool ComputePickRay(int32 PixelX, int32 PixelY, FVector& OutLineStart, FVector& OutLineEnd);
	UDreamWidget* GetWidgetUnderCursor(int32 PixelX, int32 PixelY, bool bRespectDesignerLock = true);
	/** Where a drop at this pixel should be parented: nearest container under it, else the prefab root. */
	UDreamWidget* GetDropContainerUnderCursor(int32 PixelX, int32 PixelY);
	bool GetDropWorldPosition(int32 PixelX, int32 PixelY, UDreamWidget* ParentWidget, FVector& OutWorldPosition);
	void SetPaletteDropPreview(UDreamWidget* Widget);
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
		AnchorBottomLeft,
		AnchorBottomRight,
		AnchorTopRight,
		AnchorTopLeft,
	};
	struct FDesignerWidgetSnapshot
	{
		TWeakObjectPtr<UDreamWidget> Widget;
		FVector2D AnchoredPosition = FVector2D::ZeroVector;
		FVector2D AnchorMin = FVector2D(0.5f);
		FVector2D AnchorMax = FVector2D(0.5f);
		FVector2D SizeDelta = FVector2D::ZeroVector;
		FVector2D Pivot = FVector2D(0.5f);
		float Width = 0.0f;
		float Height = 0.0f;
		FTransform WorldTransform = FTransform::Identity;
		FTransform PlaneTransform = FTransform::Identity;
		FVector StartPlanePoint = FVector::ZeroVector;
		/** Read once at press: what arranges a widget cannot change while the pointer is down. */
		bool bHorizontalPositionFree = true;
		bool bVerticalPositionFree = true;
	};
	bool HandleDesignerInputKey(const FInputKeyEventArgs& EventArgs);
	void TrackRightMouseMovement(int32 MouseX, int32 MouseY);
	/** Would this handle drag anything? Checked at press time so a dead press is not swallowed. */
	bool CanBeginDesignerDrag(EDesignerHandle InHandle) const;
	/** Snapshot the selection and enter the drag, anchored at the pixel the button went down on. */
	void BeginDesignerDrag(EDesignerHandle InHandle, const FVector2D& InPressPixel);
	/** A held press becomes a drag only once it has travelled; until then it is still a click. */
	void TryPromoteDesignerDrag();
	/** Pick and select whatever is at this pixel, the way ProcessClick would have. */
	void SelectWidgetAtPixel(const FVector2D& InPixel, bool bIsControlDown);
	void UpdateDesignerDrag();
	/**
	 * Where a Move drag is currently hovering, when dropping there would move the selection: the
	 * container under the cursor, or the prefab root when nothing above the cursor holds one. Held as
	 * pending state and shown as the drop preview, because a reparent that happened mid-drag would
	 * fight the arrange the new parent runs on every pointer move.
	 */
	void UpdateDesignerReparentTarget(const FVector2D& InPixel);
	/** Carry out the pending reparent, inside the transaction the move already opened. */
	bool ApplyPendingReparent();
	/** Every widget the running drag snapshotted, arranged axes included: a reparent moves them all. */
	void GetDraggedWidgets(TArray<UDreamWidget*>& OutWidgets) const;
	void FinishDesignerDrag(bool bCancel);
	void DrawDesignerOverlay(FViewport& InViewport, FSceneView& View, FCanvas& Canvas);
	void DrawLayoutDebugOverlay(FViewport& InViewport, FCanvas& Canvas) const;
	bool UpdateDesignerScreenGeometry(FSceneView& View);
	/** An anchor handle rides the parent's rect rather than the selection's, and writes anchors. */
	static bool IsAnchorHandle(EDesignerHandle InHandle);
	/** Project the anchor space -- the parent's rect -- and place the four anchor markers on it. */
	void UpdateAnchorScreenGeometry(FSceneView& View, UDreamWidget* InWidget);
	void DrawAnchorMedallion(FCanvas& Canvas) const;
	/** A press on empty space is still the backdrop click until it travels; then it is a marquee. */
	void TryPromoteDesignerMarquee();
	void FinishDesignerMarquee();
	void DrawDesignerMarquee(FCanvas& Canvas) const;
	FBox2D GetDesignerMarqueeBox() const;
	EDesignerHandle HitTestDesignerHandle(const FVector2D& PixelPosition) const;
	bool IntersectDesignerPlane(const FVector2D& PixelPosition, const FTransform& PlaneTransform, FVector& OutPoint) const;
	void DrawWidgetScreenOutline(UDreamWidget* InWidget, FSceneView& View, FCanvas& Canvas, const FLinearColor& Color, float Thickness = 1.0f) const;
	/** Outline + name of whatever the cursor is over, so a click's target is knowable before it happens. */
	void DrawHoverOutline(FSceneView& View, FCanvas& Canvas) const;
	/** Resolve the hovered widget from the last mouse pixel. Once per frame, not once per move event. */
	void UpdateHoveredWidget();
	void DrawDesignerCanvasBoundary(FViewport& InViewport, FSceneView& View, FCanvas& Canvas) const;
	/**
	 * Draw where the selection actually lands in the shipped image, computed with the canvas's own
	 * view-projection, without moving the camera or leaving the layout surface. Silent for anything
	 * that ships where it was laid out, which is most things.
	 */
	void DrawShippedImageOutline(UDreamWidget* InWidget, FSceneView& View, FCanvas& Canvas) const;
	/** Overlay common device resolutions anchored at the design canvas top-left, like UMG's designer. */
	void DrawResolutionGuides(FViewport& InViewport, FSceneView& View, FCanvas& Canvas) const;
	/** The title-safe rect of the design canvas. Silent on platforms that declare no safe area. */
	void DrawSafeZoneGuide(FSceneView& View, FCanvas& Canvas) const;
	/** The cursor's place on the design canvas, in the units the details panel is written in. */
	void DrawCursorReadout(FViewport& InViewport, FSceneView& View, FCanvas& Canvas) const;
	/** Rulers along the top and left edges, measured in the design canvas's own units. */
	void DrawDesignerRulers(FViewport& InViewport, FSceneView& View, FCanvas& Canvas) const;
	void DrawAnimationModeIndicator(FViewport& InViewport, FCanvas& Canvas) const;
	/** Where the animation chip's close button was last drawn, in raw viewport pixels; invalid outside animation mode. */
	mutable FBox2D AnimationChipCloseRect = FBox2D(ForceInit);
	/** Key the given widgets' transform into the animation being edited, if there is one. */
	void AutoKeyAnimatedTransform(const TArray<UDreamWidget*>& InWidgets, bool bLocation, bool bRotation, bool bScale) const;
	TArray<FVector2D> DesignerScreenCorners;
	TMap<EDesignerHandle, FVector2D> DesignerHandlePositions;
	/** Kept apart from the selection's own handles so a resize handle still wins an overlapping pixel. */
	TArray<FVector2D> DesignerAnchorSpaceCorners;
	TMap<EDesignerHandle, FVector2D> DesignerAnchorHandlePositions;
	FBox2D DesignerScreenBounds = FBox2D(EForceInit::ForceInit);
	/** Refreshed with the handle positions, because it is an answer about the same selection. */
	bool bDesignerMoveAvailable = false;
	EDesignerHandle ActiveDesignerHandle = EDesignerHandle::None;
	bool bDesignerDragging = false;
	bool bDesignerChanged = false;
	/** A left press that landed on a handle but has not travelled far enough to be a drag yet. */
	bool bDesignerDragPending = false;
	EDesignerHandle PendingDesignerHandle = EDesignerHandle::None;
	FVector2D DesignerPressPixel = FVector2D::ZeroVector;
	FVector2D DesignerDragStartPixel = FVector2D::ZeroVector;
	/** A left press that landed on empty space; a marquee only once it has travelled. */
	bool bDesignerMarqueePending = false;
	bool bDesignerMarqueeActive = false;
	FVector2D DesignerMarqueePressPixel = FVector2D::ZeroVector;
	FVector2D DesignerMarqueeCurrentPixel = FVector2D::ZeroVector;
	TArray<FDesignerWidgetSnapshot> DesignerSnapshots;
	/** Where a Move drag would drop the selection, when that is somewhere other than where it is. */
	TWeakObjectPtr<UDreamWidget> PendingReparentTarget;
	TUniquePtr<class FScopedTransaction> DesignerTransaction;
	TOptional<float> DesignerGuideX;
	TOptional<float> DesignerGuideY;

	/**
	 * Whether the arrow-key nudge has a transaction of its own open. The editor's transaction stack
	 * is global, so a release that ends one it never began finalises whatever else is running -- the
	 * designer drag's own transaction, which can then no longer be cancelled.
	 */
	bool bNudgeTransactionOpen = false;

	int PrevMouseX = 0, PrevMouseY = 0;
	int IndexOfClickSelectUI = INDEX_NONE;
	FIntPoint LastClickPixel = FIntPoint(-1, -1);
	TWeakObjectPtr<UDreamWidget> PaletteDropPreviewWidget;
	TWeakObjectPtr<UDreamWidget> HoveredWidget;
	FIntPoint HoverPixel = FIntPoint::ZeroValue;
	bool bHoverPixelDirty = false;
	/** The cursor readout has to go quiet when the cursor is not over this viewport at all. */
	bool bCursorInViewport = false;
	FDelegateHandle OnSelectionChangedDelegateHandle;

	TWeakPtr<FDreamWidgetBlueprintEditor> PrefabEditorPtr;
	TWeakPtr<class SDreamUIPrefabEditorViewport> EditorViewportPtr;
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

	UDreamWidgetBlueprint* GetWidgetBlueprint()const;
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
