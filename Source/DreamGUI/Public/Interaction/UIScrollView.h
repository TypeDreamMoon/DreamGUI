// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once

#include "InputCoreTypes.h"
#include "Event/DreamDelegateDeclaration.h"
#include "Event/DreamUIEventDelegate.h"
#include "Event/Interface/DreamPointerDragInterface.h"
#include "Event/Interface/DreamPointerScrollInterface.h"
#include "Core/DreamUIBehaviour.h"
// The questions this behaviour shares with the scroll box control and the scroll box layout: where a
// revealed widget lands, what the wheel event does, what focus landing inside means. One spelling of
// each, in a header none of the three implementations has to include the others to reach.
#include "Core/Components/DreamScrollTypes.h"
#include "UIScrollView.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FUIScrollViewValueChangedEvent, FVector2D, InVector2);

// EDreamUIScrollDestination and EDreamUIScrollWhenFocusChanges used to be declared here, and moved to
// DreamScrollTypes.h when the layout container needed to speak them too: a behaviour and a layout
// container cannot include each other, and the question is neither one's property.

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

/** Which end of a drag gesture a broadcast is about. */
enum class EDreamScrollDragPhase : uint8 { Begin, Move, End };
/**
 * The three ends of a drag gesture, with whether it came from a finger.
 *
 * The view itself has no use for the distinction -- it acts on the delta either way -- but a list
 * wants to announce touch start, move and end, and re-deriving them from a value-changed broadcast
 * is impossible: a change says nothing about whether a finger is still down. Native only, like
 * OnValueChangedCPP, because a raw enum is not a reflected parameter type.
 */
DECLARE_MULTICAST_DELEGATE_TwoParams(FDreamScrollDragGesture, EDreamScrollDragPhase /*InPhase*/, bool /*bInTouch*/);
/**
 * User focus landed on something INSIDE this view's content.
 *
 * Raised by the navigation reveal, which is the one place that already knows both halves: it walks
 * from the newly focused widget out to its scrolling ancestors, so "which view" and "what got
 * focus" arrive together. A control cannot work this out on its own -- a descendant's focus is not
 * its own, and polling HasFocusedDescendants every frame would be a tick for an edge.
 */
DECLARE_MULTICAST_DELEGATE_OneParam(FDreamScrollContentFocusMoved, UDreamWidget* /*InFocusedWidget*/);

UCLASS(ClassGroup = (DreamGUI), Blueprintable, meta = (BlueprintSpawnableComponent))
class DREAMGUI_API UUIScrollView : public UDreamUIBehaviour, public IDreamPointerDragInterface, public IDreamPointerScrollInterface
{
	GENERATED_BODY()

protected:
	virtual void Awake() override;
	virtual void Tick(float DeltaTime) override;
	virtual void OnUnregister() override;
	virtual void OnDestroy() override;
	/**
	 * Says out loud what the ScrollSensitivity semantic change left silent.
	 *
	 * The number used to be documented as a MULTIPLIER on the raw wheel axis and shipped at 1; it is
	 * LOCAL UNITS per notch now and ships at 40. The arithmetic never changed, so an asset carrying a
	 * multiplier-era value still loads it and still travels that many units a notch -- one unit, on
	 * the old default -- which reads as a wheel that does nothing rather than as a setting. Nothing
	 * migrates it (a value equal to the old default was never serialized in the first place, so those
	 * assets pick the new one up for free) and nothing said so, which is the part worth fixing.
	 */
	virtual void PostLoad() override;

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
	/**
	 * A plain multiplier on whatever one wheel notch already travels -- UMG's WheelScrollMultiplier,
	 * and the reason it is a second number beside ScrollSensitivity rather than folded into it:
	 * ScrollSensitivity is the DISTANCE a notch covers (local units, or a progress fraction when
	 * WheelProgressStep is set), and this scales it. A project that wants "the same feel, twice as
	 * fast" writes 2 here and leaves the distance alone, which is what the list controls already mean
	 * by the same name.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ScrollView", meta = (ClampMin = "0.0"))
		float WheelScrollMultiplier = 1.0f;
	/**
	 * Whether a wheel event this view acted on is swallowed here or handed to an outer scrolling
	 * ancestor -- UMG's ConsumeMouseWheel.
	 *
	 * WhenScrollingPossible is what this component has always done: it scrolls and consumes while
	 * there is somewhere left to go, and hands the event on once it is at a limit, which is what makes
	 * a list inside a scrolling page behave. Never stops the wheel driving this view at all; Always
	 * keeps the event even at a limit, so the page behind never moves.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ScrollView")
		EDreamScrollBoxConsumeMouseWheel ConsumeMouseWheel = EDreamScrollBoxConsumeMouseWheel::WhenScrollingPossible;
	/**
	 * Whether the content may be pulled past an end and spring back -- UMG's AllowOverscroll.
	 *
	 * Off does NOT mean OutOfRangeDamper is zero for good: the damper stays the author's number and
	 * this is the switch in front of it, so turning overscroll back on restores whatever feel was
	 * tuned. Everything that reads the damper goes through GetEffectiveOutOfRangeDamper.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ScrollView")
		bool bAllowOverscroll = true;
	/** Whether a TOUCH drag scrolls this view -- UMG's bEnableTouchScrolling. A mouse drag is unaffected. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ScrollView")
		bool bEnableTouchScrolling = true;
	/**
	 * Whether a drag with the RIGHT button scrolls this view -- UMG's bAllowRightClickDragScrolling.
	 * Off leaves the right button to whatever wants it for a context menu.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ScrollView")
		bool bAllowRightClickDragScrolling = true;
	/**
	 * The master switch over POINTER scrolling -- UMG's bIsPointerScrollingEnabled.
	 *
	 * The wheel AND the drag together, because they are one gesture set: a view that answers the
	 * wheel but not the drag is one whose scroll bar is the only way down. Asked in front of the
	 * per-gesture switches, which are about WHICH pointer gesture rather than whether any of them.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ScrollView")
		bool bIsPointerScrollingEnabled = true;
	/**
	 * The same switch for the analog stick -- UMG's bIsGamepadScrollingEnabled.
	 *
	 * Read by FDreamUINavigationScroll beside AnalogMouseWheelKey: the key says which axis reaches
	 * this view, and this says whether any does.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ScrollView")
		bool bIsGamepadScrollingEnabled = true;
	/**
	 * Whether a TOUCH drag eases to a stop like the wheel does -- UMG's bInEnableTouchAnimatedScrolling.
	 *
	 * Separate from bAnimateWheelScrolling because a finger already carries its own momentum: gliding
	 * a gesture that was never discrete is a second ease on top of the one the player made.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ScrollView")
		bool bAnimateTouchScrolling = false;
	/**
	 * A whole window of empty space BEFORE the content, so the first item can be scrolled all the way
	 * to the trailing edge -- UMG's BackPadScrolling, and Slate's arithmetic (a full view, not half).
	 *
	 * Both pads move the point the offset is measured from, which is why GetStartAlignedPosition owns
	 * them rather than each caller adding its own term: the ranges, the progress and the reveal maths
	 * all read that one function, so they cannot disagree about where zero is.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ScrollView")
		bool bBackPadScrolling = false;
	/** A whole window of empty space AFTER the content -- UMG's FrontPadScrolling. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ScrollView")
		bool bFrontPadScrolling = false;
	/**
	 * The analog key that acts as this view's mouse wheel -- UMG's AnalogMouseWheelKey.
	 *
	 * Invalid (the default) means "whatever the input preset already routes here", which is the right
	 * stick on both axes and therefore exactly today's behaviour. Naming a key narrows it to that key:
	 * the preset only binds the right stick's two axes, so a view that names one of them scrolls from
	 * that axis alone, and a view that names anything else scrolls from a binding the project adds.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ScrollView")
		FKey AnalogMouseWheelKey;
	/**
	 * What this view does when user focus lands inside it -- UMG's ScrollWhenFocusChanges. Read by
	 * FDreamUINavigationScroll, which is the only thing that reveals a widget because focus moved.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ScrollView")
		EDreamUIScrollWhenFocusChanges ScrollWhenFocusChanges = EDreamUIScrollWhenFocusChanges::AnimatedScroll;
	/**
	 * A wheel notch GLIDES to its destination instead of teleporting -- UMG's AnimateWheelScrolling.
	 *
	 * Off by default, which is the behaviour every existing view already has: turning it on for
	 * everyone would change how every list in every project reads under the wheel. It uses the same
	 * easing ScrollTo does, so a notch and a programmatic scroll move the content one way.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ScrollView")
		bool bAnimateWheelScrolling = false;
	/** How long one animated notch takes. Ignored while bAnimateWheelScrolling is off. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ScrollView", meta = (ClampMin = "0.0", EditCondition = "bAnimateWheelScrolling"))
		float WheelScrollAnimationDuration = 0.15f;
	/**
	 * Where a revealed widget is meant to END UP -- UMG's NavigationDestination, consulted by
	 * ScrollWidgetIntoView and therefore by every navigation move (FDreamUINavigationScroll).
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ScrollView", meta = (InvalidEnumValues = "Configured"))
		EDreamUIScrollDestination NavigationDestination = EDreamUIScrollDestination::IntoView;
	/**
	 * How much of the window to keep clear around a revealed widget, in local units -- UMG's
	 * NavigationScrollPadding. Stops a row landing flush against the edge with its neighbour cut in
	 * half beside it, which is the only cue a player has that the list continues.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ScrollView", meta = (ClampMin = "0.0"))
		float NavigationScrollPadding = 0.0f;
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
public:
	/** Whether this drag gesture is one the author left switched on -- the right button, and touch. */
	bool AcceptsDragGesture(UDreamPointerEventData* InEventData) const;

	/** Subscribe to the three ends of a drag gesture. See FDreamScrollDragGesture. */
	FDreamScrollDragGesture& GetOnDragGestureEvent() { return OnDragGestureCPP; }

	/** Subscribe to focus landing inside this view's content. See FDreamScrollContentFocusMoved. */
	FDreamScrollContentFocusMoved& GetOnContentFocusMovedEvent() { return OnContentFocusMovedCPP; }

	/**
	 * Told by the navigation reveal that focus went to InWidget inside this view.
	 *
	 * A notifier rather than something this component works out: the reveal already walks from the
	 * focused widget out to its scrolling ancestors, so it has both halves of the answer and this
	 * has neither.
	 */
	void NotifyContentFocusMoved(UDreamWidget* InWidget) { OnContentFocusMovedCPP.Broadcast(InWidget); }
protected:
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
	/** Native only, like OnValueChangedCPP: the phase enum is not a reflected type. */
	FDreamScrollDragGesture OnDragGestureCPP;
	FDreamScrollContentFocusMoved OnContentFocusMovedCPP;
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
		float GetWheelScrollMultiplier()const { return WheelScrollMultiplier; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		void SetWheelScrollMultiplier(float value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		EDreamScrollBoxConsumeMouseWheel GetConsumeMouseWheel()const { return ConsumeMouseWheel; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		void SetConsumeMouseWheel(EDreamScrollBoxConsumeMouseWheel value) { ConsumeMouseWheel = value; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		bool GetAllowOverscroll()const { return bAllowOverscroll; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		void SetAllowOverscroll(bool value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		bool GetEnableTouchScrolling()const { return bEnableTouchScrolling; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		void SetEnableTouchScrolling(bool value) { bEnableTouchScrolling = value; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		bool GetPointerScrollingEnabled()const { return bIsPointerScrollingEnabled; }
	/** True while the player's last input came from a finger. The gate AcceptsDragGesture uses. */
	bool IsTouchInput(UDreamPointerEventData* InEventData) const;
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		void SetPointerScrollingEnabled(bool value) { bIsPointerScrollingEnabled = value; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		bool GetGamepadScrollingEnabled()const { return bIsGamepadScrollingEnabled; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		void SetGamepadScrollingEnabled(bool value) { bIsGamepadScrollingEnabled = value; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		bool GetAnimateTouchScrolling()const { return bAnimateTouchScrolling; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		void SetAnimateTouchScrolling(bool value) { bAnimateTouchScrolling = value; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		bool GetAllowRightClickDragScrolling()const { return bAllowRightClickDragScrolling; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		void SetAllowRightClickDragScrolling(bool value) { bAllowRightClickDragScrolling = value; }
	/**
	 * Whether a pointer event this view handled is swallowed -- UMG's bConsumePointerInput, which is
	 * the inverse of the AllowEventBubbleUp this component was written with. One stored bit, asked
	 * both ways round, rather than two that can disagree.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		bool GetConsumePointerInput()const { return !AllowEventBubbleUp; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		void SetConsumePointerInput(bool value) { AllowEventBubbleUp = !value; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		bool GetBackPadScrolling()const { return bBackPadScrolling; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		void SetBackPadScrolling(bool value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		bool GetFrontPadScrolling()const { return bFrontPadScrolling; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		void SetFrontPadScrolling(bool value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		FKey GetAnalogMouseWheelKey()const { return AnalogMouseWheelKey; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		void SetAnalogMouseWheelKey(FKey value) { AnalogMouseWheelKey = value; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		EDreamUIScrollWhenFocusChanges GetScrollWhenFocusChanges()const { return ScrollWhenFocusChanges; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		void SetScrollWhenFocusChanges(EDreamUIScrollWhenFocusChanges value) { ScrollWhenFocusChanges = value; }
	/**
	 * Signed distance past an end, per axis, in the reading direction: negative before the start,
	 * positive past the end, zero in range -- SScrollBox::GetOverscrollOffset's answer. Always zero
	 * while overscroll is off, because there is then nothing that could be out there.
	 */
	UFUNCTION(BlueprintPure, Category = "DreamGUI-ScrollView")
		FVector2D GetOverscrollOffset()const;
	/** The same distance as a PERCENTAGE of the window on that axis, which is what UMG reports. */
	UFUNCTION(BlueprintPure, Category = "DreamGUI-ScrollView")
		FVector2D GetOverscrollPercentage()const;
	/** True while momentum or a spring-back still has the content moving -- UMG's GetIsScrolling. */
	UFUNCTION(BlueprintPure, Category = "DreamGUI-ScrollView")
		bool IsScrolling()const;
	/**
	 * Drop the fling, keeping the position -- UMG's EndInertialScrolling.
	 *
	 * The spring is deliberately left armed when the content is out of range: a rubber band pulled
	 * open is not inertia, and abandoning it there would leave the content parked outside its own
	 * range with nothing left to walk it back.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		void EndInertialScrolling();
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		bool GetAnimateWheelScrolling()const { return bAnimateWheelScrolling; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		void SetAnimateWheelScrolling(bool value) { bAnimateWheelScrolling = value; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		float GetWheelScrollAnimationDuration()const { return WheelScrollAnimationDuration; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		void SetWheelScrollAnimationDuration(float value) { WheelScrollAnimationDuration = FMath::Max(0.0f, value); }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		EDreamUIScrollDestination GetNavigationDestination()const { return NavigationDestination; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		void SetNavigationDestination(EDreamUIScrollDestination value) { NavigationDestination = value; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		float GetNavigationScrollPadding()const { return NavigationScrollPadding; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		void SetNavigationScrollPadding(float value) { NavigationScrollPadding = FMath::Max(0.0f, value); }
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
	 *
	 * @param InDestination Where the child ends up. Configured (the default) leaves it to this view's
	 *                      own NavigationDestination, which is what every caller meant before the
	 *                      parameter existed.
	 * @param InPadding     How much of the window to keep clear around it. Negative leaves it to this
	 *                      view's NavigationScrollPadding, for the same reason.
	 * @return true when the content position actually moved.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		bool ScrollWidgetIntoView(UDreamWidget* InChild, bool InEaseAnimation = true, float InAnimationDuration = 0.25f,
			EDreamUIScrollDestination InDestination = EDreamUIScrollDestination::Configured, float InPadding = -1.0f);
	/** True when InChild sits inside this view and a scroll would bring more of it into sight. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		bool CanScrollWidgetIntoView(UDreamWidget* InChild);
protected:
	/**
	 * Content position that reveals InChild with the least movement, clamped to the scroll range and
	 * restricted to the axes this view scrolls on. Returns false when nothing needs to move -- either
	 * the child is already visible, or the clamp leaves the position where it was.
	 */
	bool CalculateRevealContentPosition(UDreamWidget* InChild, FVector2D& OutPosition,
		EDreamUIScrollDestination InDestination = EDreamUIScrollDestination::Configured, float InPadding = -1.0f);

	/** The damper in force right now: the author's number, or zero while overscroll is switched off. */
	float GetEffectiveOutOfRangeDamper() const { return bAllowOverscroll ? OutOfRangeDamper : 0.0f; }
	/**
	 * The empty space BEFORE the content, per axis -- a whole window while bBackPadScrolling is on.
	 *
	 * This is the one that moves where offset zero IS, so GetStartAlignedPosition folds it in and
	 * every reader of that function (the ranges, the progress, the reveal maths, the wheel) follows
	 * without a term of its own.
	 */
	FVector2D GetLeadingScrollPad() const;
	/** The empty space AFTER the content -- a whole window while bFrontPadScrolling is on. */
	FVector2D GetTrailingScrollPad() const;

	/** The content position clamped into both ranges, on the axes this view scrolls. */
	FVector2D ClampToRange(const FVector2D& InPosition) const;

	/** Glide the content to a position with no physics running underneath. Shared by both ScrollTo forms. */
	void GlideContentTo(const FVector2D& InTargetPosition, bool InEaseAnimation, float InAnimationDuration);

	/** Which axes this gesture drives, given the first movement it made. Writes the two gesture bits. */
	void ResolveGestureAxes(const FVector2D& InFirstDelta);
};
