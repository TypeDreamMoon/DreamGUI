// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once

#include "Event/DreamDelegateDeclaration.h"
#include "Event/DreamUIEventDelegate.h"
#include "Event/Interface/DreamPointerDragInterface.h"
#include "Event/Interface/DreamPointerScrollInterface.h"
#include "Core/DreamUIBehaviour.h"
#include "UIScrollView.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FUIScrollViewValueChangedEvent, FVector2D, InVector2);

UENUM(BlueprintType)
enum class EDreamScrollCoordinateMode : uint8
{
	/** Backward-compatible raw component location used by legacy DreamGUI prefabs. */
	RelativeLocation,
	/** RectTransform-style offset that remains stable across anchors and pivots. */
	AnchoredPosition,
};

UCLASS(ClassGroup=(DreamGUI), Transient)
class DREAMGUI_API UUIScrollViewHelper :public UDreamUIBehaviour
{
	GENERATED_BODY()
private:
	virtual void Awake()override;
	virtual void OnDimensionsChanged(bool PivotChanged, bool WidthChanged, bool HeightChanged)override;
	virtual void OnChildDimensionsChanged(UDreamWidget* Child, bool PivotChanged, bool WidthChanged, bool HeightChanged)override;
	friend class UUIScrollView;
	UPROPERTY(Transient)
		TWeakObjectPtr<class UUIScrollView> TargetComp;
};

/**
 * A window onto content larger than itself, in the idiom UMG and Slate use: an OFFSET from the start.
 *
 * THE MODEL
 * ---------
 * Three numbers describe everything this component does, and two of them are measurements rather
 * than state:
 *
 *   Viewport = the content's PARENT rect -- the window. The view never stores one of its own.
 *   Content  = the content widget's rect, which is what the author says there is to scroll.
 *   Extent   = max(0, Content - Viewport), per axis. How far there is to go.
 *
 * and the one piece of state is the OFFSET: how far the window has travelled from the content's
 * start edge, in local units, X growing rightward and Y growing DOWNWARD -- the reading direction,
 * not the engine's +Z. Progress is that offset over the extent; the content's position is that
 * offset applied to the position at which the two start edges coincide. Every other quantity in
 * this class is derived from those, on demand, from the widgets themselves.
 *
 * That last part is deliberate and it is the reason this rewrite exists. The offset is a VIEW of
 * the content's position, not a second copy of it: the content's rect is the observable truth, so
 * a subclass or a caller that moves the content directly (UUIRecyclableScrollView does, every time
 * it recycles a cell) cannot leave this component holding a stale number it would later restore.
 *
 * WHAT THE PIVOT MATHS BECAME
 * ---------------------------
 * The two scroll ranges used to be written out per axis as four pivot-weighted terms with a
 * coordinate-mode correction added afterwards. They are now one measurement -- "how far must the
 * content move for its start edge to meet the viewport's" -- taken in the coordinate the setter
 * actually writes. The correction disappears because nothing is being translated between two
 * frames any more: both AnchoredPosition and RelativeLocation change one-for-one with the content's
 * position, so a DELTA measured in either is the same delta in the other, whatever the anchors are.
 * HorizontalRange and VerticalRange survive as derived values (min, max content position) because
 * the recycler reads them.
 *
 * AXES: CAPABILITY AND GESTURE ARE TWO QUESTIONS
 * ----------------------------------------------
 * bAllowHorizontalScroll / bAllowVerticalScroll used to answer both "does this view scroll
 * sideways" and "is THIS drag a sideways one", and one drag settled the other's answer for good:
 * after a single vertical flick with OnlyOneDirection on, the horizontal flag stayed false, so
 * UpdateProgress stopped maintaining Progress.X and the physics stopped settling X back into range
 * for the rest of the component's life. They are now the CAPABILITY (resolved from Horizontal and
 * Vertical whenever the range is), and the gesture keeps its own private pair.
 */
UCLASS(ClassGroup = (DreamGUI), Blueprintable, meta = (BlueprintSpawnableComponent))
class DREAMGUI_API UUIScrollView : public UDreamUIBehaviour, public IDreamPointerDragInterface, public IDreamPointerScrollInterface
{
	GENERATED_BODY()

protected:
	virtual void Awake() override;
	virtual void Tick(float DeltaTime) override;
	virtual void OnUnregister() override;
	virtual void OnDestroy() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	virtual void OnEnable() override;
	virtual void OnTransformChanged() override;
	virtual void OnDimensionsChanged(bool PivotChanged, bool WidthChanged, bool HeightChanged)override;
	virtual void RecalculateRange();
protected:
	friend class UUIScrollViewHelper;
	/** Content can move inside it's parent area. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ScrollView")
		TWeakObjectPtr<UDreamWidget> Content;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ScrollView")
		bool Horizontal = true;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ScrollView")
		bool Vertical = true;
	/** If allow Horizontal and Vertical both, then only allow one direction drag at the same time. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ScrollView")
		bool OnlyOneDirection = true;
	/**
	 * Local units travelled per mouse-wheel notch. 40 is what UDreamLayoutContainerScrollBox already
	 * uses for the same gesture; the 1.0 this shipped with was a MULTIPLIER on the raw axis value,
	 * which meant one unit per notch and a wheel that visibly did nothing.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ScrollView")
		float ScrollSensitivity = 40.0f;
	/** If greater than zero, mouse wheel input advances by this normalized progress instead of local units. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ScrollView", meta = (ClampMin = "0.0", ClampMax = "1.0"))
		float WheelProgressStep = 0.0f;
	/** Coordinate contract used to move Content. AnchoredPosition is recommended for layout-managed content. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ScrollView")
		EDreamScrollCoordinateMode CoordinateMode = EDreamScrollCoordinateMode::RelativeLocation;
	/** When Content size is smaller than Content's parent size, can we still drag it (and have it spring back)? */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ScrollView")
		bool CanScrollInSmallSize = true;
	/** When Content size is smaller than Content's parent size, rest it against the END edge instead of the start. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ScrollView")
		bool FlipDirectionInSmallSize = false;
	/** Determines how quickly the contents stop moving. A value of 0 means the movement will never slow down, larger value will stop the movement faster. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ScrollView", meta = (ClampMin = "0.0"))
		float DecelerateRate = 0.135f;
	/** Limit Content inside Viewport's rect area, if out-of-range then move it back. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ScrollView", meta = (ClampMin = "0.0", ClampMax = "1.0"))
		bool RestrictRectArea = true;
	/** Decrease movement value when drag content out of range. A value of 0 means not allowed out of range. A value of 1 means no damp effect. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ScrollView", meta = (ClampMin = "0.0", ClampMax = "1.0"))
		float OutOfRangeDamper = 0.5f;

	/** inherited events of this component can bubble up? */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ScrollView")
		bool AllowEventBubbleUp = false;

	/**
	 * Keep progress value when content position and size change.
	 * true- keep progress value and change content's position and size to fit progress.
	 * false- keep the offset, and let the progress follow from it.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ScrollView")
		bool KeepProgress = false;
	//progress, 0--1, x for horizontal, y for vertical
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ScrollView", meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition="KeepProgress"))
		FVector2D Progress = FVector2D(0, 0);

	/**
	 * CAPABILITY, not gesture: whether this view scrolls on that axis at all. Resolved from
	 * Horizontal / Vertical every time the range is, so it is never left false by a past drag.
	 * UUIScrollViewWithScrollbar reads both to decide which of its bars to keep in step.
	 */
	uint8 bAllowHorizontalScroll: 1, bAllowVerticalScroll: 1;
	/** Which axis the drag or wheel gesture in progress drives. See the class comment. */
	uint8 bGestureHorizontal: 1, bGestureVertical: 1;
	/** True while the inertia-and-spring pass has something left to do. */
	uint8 bCanUpdateAfterDrag: 1;
	uint8 bRangeCalculated: 1;

	virtual void CalculateHorizontalRange();
	virtual void CalculateVerticalRange();
	bool CheckParameters();
	virtual bool CheckValidHit(UDreamWidget* InHitComp);
	UPROPERTY(Transient)TWeakObjectPtr<UDreamWidget> ContentParent = nullptr;//Content's parent
	UPROPERTY(Transient)TWeakObjectPtr<UUIScrollViewHelper> RangeHelper = nullptr;
	virtual void UpdateProgress(bool InFireEvent = true);
	FVector2D Velocity = FVector2D(0, 0);//drag speed
	FVector2D HorizontalRange;//horizontal content-position range, x--min, y--max
	FVector2D VerticalRange;//vertical content-position range, x--min, y--max
	FVector PrevPointerPosition;//prev frame pointer hit position in world

	void UpdateAfterDrag(float deltaTime);
	virtual void ApplyContentPositionWithProgress();
	FVector2D GetContentPosition() const;
	void SetContentPosition(const FVector2D& Value) const;
	void ReleaseRangeHelper();
	float GetSafeDeltaTime() const;

	/**
	 * The content position at which the content's START edges meet the viewport's, in whichever
	 * coordinate mode is active. The fixed point every other number here is measured from: the
	 * ranges are this plus or minus the extent, and the offset is the distance from it.
	 *
	 * Measured rather than derived from pivots, and measured as a DELTA against the content's
	 * CURRENT position -- which is what makes it independent of anchors, of pivot, and of which of
	 * the two coordinate modes the setter writes.
	 */
	FVector2D GetStartAlignedPosition() const;

	/** Move the content and update progress, with no physics and no clamping. One writer, one place. */
	void ApplyContentPosition(const FVector2D& InPosition, bool bInFireEvent = true);

	FDreamUIMulticastDelegateVector2 OnValueChangedCPP;
	UPROPERTY(BlueprintAssignable, Category = "DreamGUI-ScrollView", DisplayName="OnValueChanged")
	FUIScrollViewValueChangedEvent OnValueChangedBP;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ScrollView")
	FDreamUIEventDelegate OnValueChanged = FDreamUIEventDelegate(EDreamUIEventDelegateParameterType::Vector2);
public:
	FDreamUIMulticastDelegateVector2& GetOnValueChangedEvent(){return OnValueChangedCPP;}

	//scroll range change(eg content or content's parent size change), use this to recalculate range
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		void RectRangeChanged();

	virtual bool OnPointerBeginDrag_Implementation(UDreamPointerEventData* EventData)override;
	virtual bool OnPointerDrag_Implementation(UDreamPointerEventData* EventData)override;
	virtual bool OnPointerEndDrag_Implementation(UDreamPointerEventData* EventData)override;

	virtual bool OnPointerScroll_Implementation(UDreamPointerEventData* EventData)override;

	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		UDreamWidget* GetContent()const { return Content.Get(); }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		void SetContent(UDreamWidget* Value);
	/**
	 * The window: the content's PARENT. This component never stores a viewport of its own -- which
	 * is also why the name is not GetViewport: UUIScrollViewWithScrollbar has one of those, and it
	 * means an authored widget the bars shrink rather than the rect the content slides inside.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		UDreamWidget* GetContentViewport()const { return ContentParent.Get(); }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		bool GetHorizontal()const { return Horizontal; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		bool GetVertical()const { return Vertical; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		bool GetOnlyOneDirection()const { return OnlyOneDirection; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		float GetScrollSensitivity()const { return ScrollSensitivity; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		float GetWheelProgressStep()const { return WheelProgressStep; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		EDreamScrollCoordinateMode GetCoordinateMode()const { return CoordinateMode; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		bool GetCanScrollInSmallSize()const { return CanScrollInSmallSize; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		FVector2D GetVelocity()const { return Velocity; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		float GetDecelerateRate()const { return DecelerateRate; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		bool GetRestrictRectArea()const { return RestrictRectArea; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		float GetOutOfRangeDamper()const { return OutOfRangeDamper; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		FVector2D GetScrollProgress()const { return Progress; }
	/** Get Content's position range in horizontal. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		FVector2D GetHorizontalRange()const { return HorizontalRange; }
	/** Get Content's position range in vertical. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		FVector2D GetVerticalRange()const { return VerticalRange; }

	/** The window's own size -- the content's parent rect. Zero when this view is not wired up yet. */
	UFUNCTION(BlueprintPure, Category = "DreamGUI-ScrollView")
		FVector2D GetViewportSize()const;
	/** The scrolled content's size. Zero when this view is not wired up yet. */
	UFUNCTION(BlueprintPure, Category = "DreamGUI-ScrollView")
		FVector2D GetContentSize()const;
	/** How far there is to go, per axis: max(0, content - viewport). Zero on an axis that fits. */
	UFUNCTION(BlueprintPure, Category = "DreamGUI-ScrollView")
		FVector2D GetScrollableExtent()const;
	/**
	 * How far the window has travelled from the content's start edge, in local units: X rightward,
	 * Y DOWNWARD. The reading direction on both axes, which is why neither one is inverted here and
	 * the two progress values mean the same thing as each other.
	 */
	UFUNCTION(BlueprintPure, Category = "DreamGUI-ScrollView")
		FVector2D GetScrollOffset()const;
	/** Clamped to the extent on every axis this view scrolls; axes it does not scroll are left alone. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		void SetScrollOffset(FVector2D InOffset);
	/** SetScrollOffset(GetScrollOffset() + InDelta), which is what a wheel notch and a key press are. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		void ScrollBy(FVector2D InDelta);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		void ScrollToStart();
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		void ScrollToEnd();
	/** True when this view scrolls on that axis AND there is something to scroll. */
	UFUNCTION(BlueprintPure, Category = "DreamGUI-ScrollView")
		bool CanScrollOnAxis(bool bInHorizontalAxis)const;

	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		void SetScrollSensitivity(float value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		void SetWheelProgressStep(float value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		void SetCoordinateMode(EDreamScrollCoordinateMode value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		void SetKeepProgress(bool value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		void SetHorizontal(bool value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		void SetVertical(bool value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		void SetOnlyOneDirection(bool value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		void SetCanScrollInSmallSize(bool value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		void SetVelocity(const FVector2D& value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		void SetDecelerateRate(float value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		void SetRestrictRectArea(bool value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		void SetOutOfRangeDamper(float value);

	/** Manually scroll it with delta value, in CONTENT-POSITION units (Y up, the engine's sign). */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		void SetScrollDelta(FVector2D value);
	/** Manually scroll it with absolute value. The value will be applyed to Content's position. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		void SetScrollValue(FVector2D value);
	/** Manually scroll it with progress value (from 0 to 1). */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		void SetScrollProgress(FVector2D value);

	/**
	 * Try to scroll the scrollview so the child can sit at center. Will clamp it in valid range.
	 * @param InChild Target child actor.
	 * @param InEaseAnimation true-use tween animation to make smooth scroll, false-immediate set.
	 * @param InAnimationDuration Animation duration if InEaseAnimation = true.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		void ScrollTo(UDreamWidget* InChild, bool InEaseAnimation = true, float InAnimationDuration = 0.5f);

	/**
	 * Scroll the least distance that brings InChild fully inside the viewport, and nothing at all when
	 * it is already there. ScrollTo always centres, which reads badly under directional navigation:
	 * stepping one row down would heave the whole list to put that row in the middle.
	 * @return true when the content position actually moved.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		bool ScrollWidgetIntoView(UDreamWidget* InChild, bool InEaseAnimation = true, float InAnimationDuration = 0.25f);
	/** True when InChild sits inside this view and a scroll would bring more of it into sight. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		bool CanScrollWidgetIntoView(UDreamWidget* InChild);
protected:
	/**
	 * Content position that reveals InChild with the least movement, clamped to the scroll range and
	 * restricted to the axes this view scrolls on. Returns false when nothing needs to move -- either
	 * the child is already visible, or the clamp leaves the position where it was.
	 */
	bool CalculateRevealContentPosition(UDreamWidget* InChild, FVector2D& OutPosition);

	/** The content position clamped into both ranges, on the axes this view scrolls. */
	FVector2D ClampToRange(const FVector2D& InPosition) const;

	/** Glide the content to a position with no physics running underneath. Shared by both ScrollTo forms. */
	void GlideContentTo(const FVector2D& InTargetPosition, bool InEaseAnimation, float InAnimationDuration);

	/** Which axes this gesture drives, given the first movement it made. Writes the two gesture bits. */
	void ResolveGestureAxes(const FVector2D& InFirstDelta);
};
