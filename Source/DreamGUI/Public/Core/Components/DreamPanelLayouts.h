// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.
// Portions derived from DreamGUI, Copyright 2026-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamLayout.h"
#include "DreamLayoutFragment.h"
#include "DreamPanelSlot.h"
#include "DreamTweener.h"
#include "DreamPanelLayouts.generated.h"

UENUM(BlueprintType)
enum class EDreamPanelOrientation : uint8
{
	Horizontal,
	Vertical,
};

UENUM(BlueprintType)
enum class EDreamScaleBoxStretch : uint8
{
	None,
	Fill,
	ScaleToFit,
	ScaleToFill,
	ScaleToFitX,
	ScaleToFitY,
	UserSpecified,
};

UCLASS(Abstract, BlueprintType)
class DREAMGUI_API UDreamPanelLayoutBase : public UDreamLayoutContainer
{
	GENERATED_BODY()

public:
	/**
	 * Authored/intrinsic size a child wants: fitter -> container preferred -> visual intrinsic ->
	 * content children -> authored rect. Never reads a rect a panel pass has written (layout output
	 * must not feed back into measurement). Public so the prefab compiler and tests can diagnose
	 * children with no intrinsic size source.
	 */
	FVector2D GetDesiredSize(UDreamWidget* Child) const;

	/**
	 * Memoises GetDesiredSize for the duration of one arrange or measure.
	 *
	 * Measuring a panel asks its container for a preferred size, which measures every child, each of
	 * which may be a panel - so one question costs O(children^depth). A StackBox then asks it four times
	 * per child in a single pass: once to total the fixed extent, once to place, once inside
	 * ApplyChildRect, and once more from MeasureLayout. Nothing pruned any of that.
	 *
	 * The memo is only sound because an arrange pass no longer writes: a child's desired size cannot
	 * change while the pass that would change it is still recording into a fragment. Entries are dropped
	 * explicitly on the two paths that do write mid-arrange (authored-geometry restore, and layout
	 * visibility suppression), and the whole memo dies when the outermost scope closes.
	 */
	struct DREAMGUI_API FDesiredSizeMemoScope
	{
		FDesiredSizeMemoScope();
		~FDesiredSizeMemoScope();
		FDesiredSizeMemoScope(const FDesiredSizeMemoScope&) = delete;
		FDesiredSizeMemoScope& operator=(const FDesiredSizeMemoScope&) = delete;
	};

	/** Drop one entry, for a caller that is about to write the widget's geometry mid-pass. */
	static void ForgetDesiredSize(const UDreamWidget* Widget);
	/** Drop everything, for a change that alters which widgets participate at all. */
	static void ForgetAllDesiredSizes();

	/** Times GetDesiredSize walked the tree instead of answering from the memo. Test instrumentation. */
	static int64 GetDesiredSizeComputeCount() { return DesiredSizeComputeCount; }
	static void ResetDesiredSizeComputeCount() { DesiredSizeComputeCount = 0; }

protected:
	FVector2f PreferredSize = FVector2f::ZeroVector;
	virtual void OnUnregister() override;
	UDreamPanelSlot* EnsureSlot(UDreamWidget* Child) const;
	const UDreamPanelSlot* GetSlot(const UDreamWidget* Child) const;
	TArray<UDreamWidget*> CollectLayoutChildren(bool bEnsureSlots = true) const;
	void ApplyChildRect(UDreamWidget* Child, const FVector2D& Position, const FVector2D& Size, bool bForceFill = false) const;
	/** Record a rect a panel computed itself, for the paths that do not go through ApplyChildRect. */
	void RecordChildRect(const FDreamPanelChildRect& Rect) const;
	bool BeginLayoutPass();
	virtual FVector2f MeasureLayout() const;
	virtual FDreamLayoutControlAnchorData GetLayoutControlAnchor(const UDreamWidget* TargetWidget) const override;

	/**
	 * Each panel's arrangement algorithm. Calls ApplyChildRect / RecordChildRect and writes no child
	 * geometry itself - the base class commits the recorded fragment afterwards, in one pass.
	 */
	virtual void ArrangeChildren() {}

	/** Where the current arrange pass is recording. Null outside a pass. */
	mutable FDreamFragment* RecordingFragment = nullptr;

private:
	/** One pass is single-threaded, so the memo is shared across every panel taking part in it. */
	static TMap<const UDreamWidget*, FVector2D> DesiredSizeMemo;
	static int32 DesiredSizeMemoDepth;
	static int64 DesiredSizeComputeCount;

protected:

	/** Write a recorded fragment onto the widgets. The one place a panel's result reaches the tree. */
	void CommitFragment(const FDreamFragment& Fragment) const;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditUndo() override;
#endif

public:
	/**
	 * Gate, arrange into a fragment, commit the fragment. Panels override ArrangeChildren, not this:
	 * the split is the whole point, so it is not a per-panel choice.
	 */
	virtual void CalculateLayout() override final;

	/** Run the arrangement and hand back what it decided, writing nothing. */
	FDreamFragment Arrange();

	virtual FVector2f GetLayoutPreferredSize() const override;
	virtual bool GetLayoutDebugInfo(const UDreamWidget* TargetWidget, FDreamLayoutDebugInfo& OutInfo) const override;
	UFUNCTION(BlueprintCallable, Category = "Panel")
	void RequestLayoutRefresh();
};

UCLASS(BlueprintType, DisplayName = "UMG Canvas Panel")
class DREAMGUI_API UDreamLayoutContainerCanvasPanel : public UDreamPanelLayoutBase
{
	GENERATED_BODY()
protected:
	virtual FVector2f MeasureLayout() const override;
	virtual FDreamLayoutControlAnchorData GetLayoutControlAnchor(const UDreamWidget* TargetWidget) const override;
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetSortChildrenByZOrder, Category = "CanvasPanel")
	bool bSortChildrenByZOrder = true;
	UFUNCTION(BlueprintSetter) void SetSortChildrenByZOrder(bool Value);
	virtual void ArrangeChildren() override;
};

UCLASS(BlueprintType, DisplayName = "UMG Overlay")
class DREAMGUI_API UDreamLayoutContainerOverlay : public UDreamPanelLayoutBase
{
	GENERATED_BODY()
protected:
	virtual FVector2f MeasureLayout() const override;
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetPadding, Category = "Overlay")
	FMargin Padding;
	UFUNCTION(BlueprintSetter) void SetPadding(FMargin Value);
	virtual void ArrangeChildren() override;
};

UCLASS(BlueprintType, DisplayName = "UMG Stack Box")
class DREAMGUI_API UDreamLayoutContainerStackBox : public UDreamPanelLayoutBase
{
	GENERATED_BODY()
protected:
	virtual FVector2f MeasureLayout() const override;
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetOrientation, Category = "StackBox")
	EDreamPanelOrientation Orientation = EDreamPanelOrientation::Vertical;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetPadding, Category = "StackBox")
	FMargin Padding;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetSpacing, Category = "StackBox", meta = (ClampMin = "0.0"))
	float Spacing = 0.0f;
	UFUNCTION(BlueprintSetter) void SetOrientation(EDreamPanelOrientation Value);
	UFUNCTION(BlueprintSetter) void SetPadding(FMargin Value);
	UFUNCTION(BlueprintSetter) void SetSpacing(float Value);
	virtual void ArrangeChildren() override;
};

UCLASS(BlueprintType, DisplayName = "UMG Horizontal Box")
class DREAMGUI_API UDreamLayoutContainerHorizontalBox : public UDreamLayoutContainerStackBox
{
	GENERATED_BODY()
public:
	UDreamLayoutContainerHorizontalBox();
};

UCLASS(BlueprintType, DisplayName = "UMG Vertical Box")
class DREAMGUI_API UDreamLayoutContainerVerticalBox : public UDreamLayoutContainerStackBox
{
	GENERATED_BODY()
public:
	UDreamLayoutContainerVerticalBox();
};

UCLASS(BlueprintType, DisplayName = "UMG Wrap Box")
class DREAMGUI_API UDreamLayoutContainerWrapBox : public UDreamPanelLayoutBase
{
	GENERATED_BODY()
protected:
	virtual FVector2f MeasureLayout() const override;
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetPadding, Category = "WrapBox")
	FMargin Padding;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetSpacing, Category = "WrapBox")
	FVector2D Spacing = FVector2D::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetWrapSize, Category = "WrapBox", meta = (ClampMin = "0.0"))
	float WrapSize = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetExplicitWrapSize, Category = "WrapBox")
	bool bExplicitWrapSize = false;
	UFUNCTION(BlueprintSetter) void SetPadding(FMargin Value);
	UFUNCTION(BlueprintSetter) void SetSpacing(FVector2D Value);
	UFUNCTION(BlueprintSetter) void SetWrapSize(float Value);
	UFUNCTION(BlueprintSetter) void SetExplicitWrapSize(bool Value);
	virtual void ArrangeChildren() override;
};

UCLASS(BlueprintType, DisplayName = "UMG Grid Panel")
class DREAMGUI_API UDreamLayoutContainerGridPanel : public UDreamPanelLayoutBase
{
	GENERATED_BODY()
protected:
	virtual FVector2f MeasureLayout() const override;
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetPadding, Category = "GridPanel")
	FMargin Padding;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetSpacing, Category = "GridPanel")
	FVector2D Spacing = FVector2D::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetColumnFill, Category = "GridPanel")
	TArray<float> ColumnFill;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetRowFill, Category = "GridPanel")
	TArray<float> RowFill;
	UFUNCTION(BlueprintSetter) void SetPadding(FMargin Value);
	UFUNCTION(BlueprintSetter) void SetSpacing(FVector2D Value);
	UFUNCTION(BlueprintSetter) void SetColumnFill(const TArray<float>& Value);
	UFUNCTION(BlueprintSetter) void SetRowFill(const TArray<float>& Value);
	virtual void ArrangeChildren() override;
};

UCLASS(BlueprintType, DisplayName = "UMG Uniform Grid Panel")
class DREAMGUI_API UDreamLayoutContainerUniformGridPanel : public UDreamPanelLayoutBase
{
	GENERATED_BODY()
protected:
	virtual FVector2f MeasureLayout() const override;
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetPadding, Category = "UniformGridPanel")
	FMargin Padding;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetSpacing, Category = "UniformGridPanel")
	FVector2D Spacing = FVector2D::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetMinDesiredSlotWidth, Category = "UniformGridPanel", meta = (ClampMin = "0.0"))
	float MinDesiredSlotWidth = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetMinDesiredSlotHeight, Category = "UniformGridPanel", meta = (ClampMin = "0.0"))
	float MinDesiredSlotHeight = 0.0f;
	UFUNCTION(BlueprintSetter) void SetPadding(FMargin Value);
	UFUNCTION(BlueprintSetter) void SetSpacing(FVector2D Value);
	UFUNCTION(BlueprintSetter) void SetMinDesiredSlotWidth(float Value);
	UFUNCTION(BlueprintSetter) void SetMinDesiredSlotHeight(float Value);
	virtual void ArrangeChildren() override;
};

UCLASS(BlueprintType, DisplayName = "UMG Size Box")
class DREAMGUI_API UDreamLayoutContainerSizeBox : public UDreamPanelLayoutBase
{
	GENERATED_BODY()
protected:
	virtual FVector2f MeasureLayout() const override;
	virtual FDreamLayoutControlAnchorData GetLayoutControlAnchor(const UDreamWidget* TargetWidget) const override;
public:
	virtual int32 GetMaxChildren() const override { return 1; }
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetPadding, Category = "SizeBox")
	FMargin Padding;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetOverrideWidth, Category = "SizeBox")
	bool bOverrideWidth = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetWidthOverride, Category = "SizeBox", meta = (EditCondition = "bOverrideWidth", ClampMin = "0.0"))
	float WidthOverride = 100.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetOverrideHeight, Category = "SizeBox")
	bool bOverrideHeight = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetHeightOverride, Category = "SizeBox", meta = (EditCondition = "bOverrideHeight", ClampMin = "0.0"))
	float HeightOverride = 100.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetMinDesiredSize, Category = "SizeBox", meta = (ClampMin = "0.0"))
	FVector2D MinDesiredSize = FVector2D::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetMaxDesiredSize, Category = "SizeBox", meta = (ClampMin = "0.0"))
	FVector2D MaxDesiredSize = FVector2D::ZeroVector;
	UFUNCTION(BlueprintSetter) void SetPadding(FMargin Value);
	UFUNCTION(BlueprintSetter) void SetOverrideWidth(bool Value);
	UFUNCTION(BlueprintSetter) void SetWidthOverride(float Value);
	UFUNCTION(BlueprintSetter) void SetOverrideHeight(bool Value);
	UFUNCTION(BlueprintSetter) void SetHeightOverride(float Value);
	UFUNCTION(BlueprintSetter) void SetMinDesiredSize(FVector2D Value);
	UFUNCTION(BlueprintSetter) void SetMaxDesiredSize(FVector2D Value);
	virtual void ArrangeChildren() override;
};

UCLASS(BlueprintType, DisplayName = "UMG Scale Box")
class DREAMGUI_API UDreamLayoutContainerScaleBox : public UDreamPanelLayoutBase
{
	GENERATED_BODY()
protected:
	virtual FVector2f MeasureLayout() const override;
	virtual FDreamLayoutControlAnchorData GetLayoutControlAnchor(const UDreamWidget* TargetWidget) const override;
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	void UpdateClippingOverride();
	TWeakObjectPtr<UDreamWidget> ScaledChild;
	bool bAppliedDefaultClipping = false;
public:
	virtual int32 GetMaxChildren() const override { return 1; }
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetPadding, Category = "ScaleBox")
	FMargin Padding;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetStretch, Category = "ScaleBox")
	EDreamScaleBoxStretch Stretch = EDreamScaleBoxStretch::ScaleToFit;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetUserSpecifiedScale, Category = "ScaleBox", meta = (ClampMin = "0.0"))
	float UserSpecifiedScale = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetIgnoreInheritedScale, Category = "ScaleBox")
	bool bIgnoreInheritedScale = false;
	UFUNCTION(BlueprintSetter) void SetPadding(FMargin Value);
	UFUNCTION(BlueprintSetter) void SetStretch(EDreamScaleBoxStretch Value);
	UFUNCTION(BlueprintSetter) void SetUserSpecifiedScale(float Value);
	UFUNCTION(BlueprintSetter) void SetIgnoreInheritedScale(bool Value);
	virtual void ArrangeChildren() override;
};

UCLASS(BlueprintType, DisplayName = "UMG Safe Zone")
class DREAMGUI_API UDreamLayoutContainerSafeZone : public UDreamPanelLayoutBase
{
	GENERATED_BODY()
protected:
	FMargin GetCombinedSafePadding() const;
	virtual FVector2f MeasureLayout() const override;
	virtual FDreamLayoutControlAnchorData GetLayoutControlAnchor(const UDreamWidget* TargetWidget) const override;
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	void HandleSafeFrameChanged();
	FDelegateHandle SafeFrameChangedHandle;
public:
	virtual int32 GetMaxChildren() const override { return 1; }
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetUsePlatformSafeZone, Category = "SafeZone")
	bool bUsePlatformSafeZone = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetPadLeft, Category = "SafeZone")
	bool bPadLeft = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetPadTop, Category = "SafeZone")
	bool bPadTop = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetPadRight, Category = "SafeZone")
	bool bPadRight = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetPadBottom, Category = "SafeZone")
	bool bPadBottom = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetSafePadding, Category = "SafeZone")
	FMargin SafePadding;
	/** Per-side fraction of this widget's size, useful for device profiles and previewing notches. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetNormalizedSafePadding, Category = "SafeZone", meta = (ClampMin = "0.0", ClampMax = "0.499"))
	FMargin NormalizedSafePadding;
	UFUNCTION(BlueprintSetter) void SetUsePlatformSafeZone(bool Value);
	UFUNCTION(BlueprintSetter) void SetPadLeft(bool Value);
	UFUNCTION(BlueprintSetter) void SetPadTop(bool Value);
	UFUNCTION(BlueprintSetter) void SetPadRight(bool Value);
	UFUNCTION(BlueprintSetter) void SetPadBottom(bool Value);
	UFUNCTION(BlueprintSetter) void SetSafePadding(FMargin Value);
	UFUNCTION(BlueprintSetter) void SetNormalizedSafePadding(FMargin Value);
	virtual void ArrangeChildren() override;
};

/**
 * A stack box that clips to its own bounds and scrolls its children — the whole scroll view in one panel.
 *
 * Unlike the UUIScrollView component, there is no separate viewport/content pair to wire up: children are
 * arranged at their desired size along the scroll axis (Fill is meaningless here, since a scroll box exists
 * precisely because content may exceed the viewport), the scrollable extent is derived from that arrangement,
 * and clipping is applied automatically. Drop one in, add children, done.
 */
/** When a wheel event is swallowed by the box instead of being handed to whatever is behind it. */
UENUM(BlueprintType)
enum class EDreamScrollBoxConsumeMouseWheel : uint8
{
	/** Never scroll on the wheel; the event always passes through. */
	Never,
	/** Scroll and consume only while there is somewhere left to scroll -- the nesting-friendly default. */
	WhenScrollingPossible,
	/** Always consume, even at a limit, so the wheel never reaches an outer box. */
	Always,
};

/** Broadcast when the USER scrolls the box; code-driven SetScrollOffset does not fire it. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDreamScrollBoxUserScrolledEvent, float, CurrentOffset);

/** Whether a linked scrollbar stays put or disappears when everything already fits. */
UENUM(BlueprintType)
enum class EDreamScrollBoxScrollbarVisibility : uint8
{
	/** Always shown, even with nothing to scroll. */
	Permanent,
	/** Hidden while the content fits the viewport, which is what UMG's default does. */
	AutoHide,
};

/** How an eased scroll gets from where it is to where it was asked to go. */
UENUM(BlueprintType)
enum class EDreamScrollAnimationMode : uint8
{
	/**
	 * UMG's own: FInterpTo towards the target at a fixed speed. Never overshoots and has no fixed
	 * arrival time -- a longer distance simply takes longer.
	 */
	InterpToSpeed,
	/**
	 * An DreamTween easing curve evaluated over a fixed duration, so the scroll always arrives on time
	 * whatever the distance. Overshooting curves (Back, Elastic, Bounce) are available, but note the
	 * offset still clamps to the scrollable range, so an overshoot at either END is flattened.
	 */
	EaseCurve,
};

UCLASS(BlueprintType, DisplayName = "UMG Scroll Box")
class DREAMGUI_API UDreamLayoutContainerScrollBox : public UDreamLayoutContainerStackBox
{
	GENERATED_BODY()
protected:
	bool bAppliedDefaultClipping = false;
	virtual void ArrangeChildren() override;
	virtual FVector2f MeasureLayout() const override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
public:
	UDreamLayoutContainerScrollBox();
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void BeginPlay() override;
	virtual void EndPlay() override;

	/** Local units scrolled per wheel notch. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ScrollBox", meta = (ClampMin = "0.0"))
	float ScrollSensitivity = 40.0f;

	/** Whether the wheel is swallowed here or handed to an outer scroll box once this one is at a limit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ScrollBox")
	EDreamScrollBoxConsumeMouseWheel ConsumeMouseWheel = EDreamScrollBoxConsumeMouseWheel::WhenScrollingPossible;

	UPROPERTY(BlueprintAssignable, Category = "ScrollBox")
	FDreamScrollBoxUserScrolledEvent OnUserScrolled;

	/**
	 * A UUIScrollbar to keep in step with this box, both ways. Assign the component from a
	 * scrollbar prefab (/DreamGUI/Prefabs/VerticalScrollbar or HorizontalScrollbar) placed anywhere in
	 * the hierarchy -- it does not have to be a child of this box. Which end of the bar means zero
	 * is the BAR's business: this box always feeds the raw 0..1 fraction and the bar's own
	 * DirectionType decides the mapping.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ScrollBox")
	TWeakObjectPtr<class UUIScrollbar> Scrollbar;
	/** Whether that bar hides itself when the content already fits. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ScrollBox")
	EDreamScrollBoxScrollbarVisibility ScrollbarVisibility = EDreamScrollBoxScrollbarVisibility::AutoHide;

	/** Ease to the wheel's new position over a few frames instead of jumping there. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ScrollBox")
	bool bAnimateWheelScrolling = false;
	/** Which curve an eased scroll follows. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ScrollBox")
	EDreamScrollAnimationMode ScrollAnimationMode = EDreamScrollAnimationMode::InterpToSpeed;
	/** FInterpTo speed, used by InterpToSpeed only. Larger arrives sooner; UMG's default is 15. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ScrollBox", meta = (ClampMin = "0.0", EditCondition = "ScrollAnimationMode == EDreamScrollAnimationMode::InterpToSpeed"))
	float ScrollAnimationInterpolationSpeed = 15.0f;
	/** Curve shape, used by EaseCurve only. Any of DreamTween's easings, overshooting ones included. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ScrollBox", meta = (EditCondition = "ScrollAnimationMode == EDreamScrollAnimationMode::EaseCurve"))
	EDreamTweenEase ScrollAnimationEase = EDreamTweenEase::OutCubic;
	/** Seconds an eased scroll takes, used by EaseCurve only. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ScrollBox", meta = (ClampMin = "0.0", EditCondition = "ScrollAnimationMode == EDreamScrollAnimationMode::EaseCurve"))
	float ScrollAnimationDuration = 0.25f;

	/** Keep moving under momentum after the finger or mouse lets go. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ScrollBox")
	bool bEnableInertia = true;
	/**
	 * How quickly momentum dies. 0 never slows down; larger values stop sooner. Same units and
	 * default as the legacy scroll view, so a value tuned there carries over.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ScrollBox", meta = (ClampMin = "0.0"))
	float DecelerationRate = 0.135f;
	/** Let the content rubber-band past an end while dragging, then spring back on release. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ScrollBox")
	bool bAllowOverscroll = true;
	/**
	 * How far past an end the content can be pulled, in local units. The pull saturates towards this
	 * rather than stopping at it, so the resistance grows the further out you drag and the content
	 * can never be dragged away indefinitely -- which the legacy view's flat damping allowed.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ScrollBox", meta = (ClampMin = "0.0"))
	float OverscrollLimit = 120.0f;

	/** Signed displacement past an end, damped; zero while in range. Layout adds it to the offset. */
	UFUNCTION(BlueprintCallable, Category = "ScrollBox")
	float GetOverscroll() const;
	UFUNCTION(BlueprintCallable, Category = "ScrollBox")
	float GetScrollVelocity() const { return ScrollVelocity; }
	UFUNCTION(BlueprintCallable, Category = "ScrollBox")
	void SetScrollVelocity(float Value);
	/** True while momentum or a spring-back is still moving the content. */
	UFUNCTION(BlueprintCallable, Category = "ScrollBox")
	bool IsScrolling() const;
	/** Drop momentum and any rubber-band displacement immediately. */
	UFUNCTION(BlueprintCallable, Category = "ScrollBox")
	void StopScrolling();
	/**
	 * Advance momentum and spring-back by DeltaTime. Deliberately free of input plumbing: the physics
	 * can then be stepped directly by a test, which is the only way any of this gets verified without
	 * a real pointer.
	 */
	UFUNCTION(BlueprintCallable, Category = "ScrollBox")
	void TickScrollPhysics(float DeltaTime);
	/** Apply a drag delta, rubber-banding whatever part of it would push past an end. */
	void ApplyDragDelta(float Delta);
	/**
	 * Tell the box a pointer is holding its content. While it is, momentum and the spring-back stay
	 * out of the way: a hand on the content owns the offset, and letting the spring run underneath
	 * means the band is pulled shut in the same frames the drag is stretching it -- which stops the
	 * content advancing and looks like it snapped back mid-gesture.
	 */
	void SetDragging(bool bInDragging);
	bool IsDragging() const { return bDragging; }

	/** Distance scrolled from the start, in local units. Always within [0, GetMaxScrollOffset()]. */
	UFUNCTION(BlueprintCallable, Category = "ScrollBox")
	float GetScrollOffset() const { return ScrollOffset; }
	UFUNCTION(BlueprintCallable, Category = "ScrollBox")
	void SetScrollOffset(float Value);
	/** How far this box can scroll: content extent minus viewport extent, or 0 when everything fits. */
	UFUNCTION(BlueprintCallable, Category = "ScrollBox")
	float GetMaxScrollOffset() const { return MaxScrollOffset; }
	/** Scroll by a signed delta. Returns true when the offset actually changed, false when already at a limit. */
	UFUNCTION(BlueprintCallable, Category = "ScrollBox")
	bool ScrollBy(float Delta);
	/** As ScrollBy, but reports the move as user-driven so OnUserScrolled fires. */
	bool ScrollByFromUser(float Delta);

	/** Jump to the start of the content. */
	UFUNCTION(BlueprintCallable, Category = "ScrollBox")
	void ScrollToStart();
	/** Jump to the end of the content. */
	UFUNCTION(BlueprintCallable, Category = "ScrollBox")
	void ScrollToEnd();
	/** The offset at which the end of the content is in view -- UMG's name for GetMaxScrollOffset. */
	UFUNCTION(BlueprintCallable, Category = "ScrollBox")
	float GetScrollOffsetOfEnd() const { return MaxScrollOffset; }
	/** Fraction of the content currently visible, 0..1. 1 when everything fits. */
	UFUNCTION(BlueprintCallable, Category = "ScrollBox")
	float GetViewFraction() const;
	/** How far through the scrollable range the view sits, 0..1. 0 when nothing can scroll. */
	UFUNCTION(BlueprintCallable, Category = "ScrollBox")
	float GetViewOffsetFraction() const;
	/**
	 * Scroll the minimum distance that brings InWidget into view. Accepts any descendant, not just a
	 * direct child. Returns false when the widget is not inside this box or nothing needed to move.
	 * Eased by default, as UMG's is -- a jump loses the reader's place on a long list.
	 */
	UFUNCTION(BlueprintCallable, Category = "ScrollBox")
	bool ScrollWidgetIntoView(UDreamWidget* InWidget, bool bAnimateScroll = true);
	/** Ease towards Value over the coming frames instead of moving now. Cancels any momentum. */
	UFUNCTION(BlueprintCallable, Category = "ScrollBox")
	void SetScrollOffsetAnimated(float Value);
	/** True while an eased scroll is still running. */
	UFUNCTION(BlueprintCallable, Category = "ScrollBox")
	bool IsAnimatingScroll() const { return bAnimatingScroll; }
	/** Where an eased scroll is heading; the current offset when nothing is animating. */
	UFUNCTION(BlueprintCallable, Category = "ScrollBox")
	float GetAnimatedScrollTarget() const { return bAnimatingScroll ? AnimatedTargetOffset : ScrollOffset; }

private:
	/** Content extent along the scroll axis, measured by the last layout pass. */
	float MeasuredContentPrimary = 0.0f;
	/** Viewport extent along the scroll axis, measured by the last layout pass. */
	float MeasuredViewportPrimary = 0.0f;
	/**
	 * False until CalculateLayout has run once. MaxScrollOffset is zero before that, so clamping a
	 * requested offset against it would silently swallow every SetScrollOffset issued during
	 * construction or BeginPlay; UMG dodges the same trap by deferring its clamp to Slate.
	 */
	bool bLayoutMetricsValid = false;
	/**
	 * The offset last asked for, before any clamp. Seeded from the serialized ScrollOffset on the first pass.
	 *
	 * Clamping ScrollOffset in place against MaxScrollOffset is destructive, and MaxScrollOffset is only as
	 * good as the measurement behind it. A pass that measures the content too small - narrowing a vertical
	 * box re-measures wrapping text at its pre-arrangement width, for one - then permanently truncated the
	 * user's position, and the corrected larger range on the pass right after could not give it back.
	 * Re-deriving the clamp from the request each pass makes a transient underestimate transient too.
	 */
	float RequestedScrollOffset = 0.0f;
	/** Content-space start and extent of the direct child that contains InWidget. */
	bool GetChildContentExtent(UDreamWidget* InWidget, float& OutStart, float& OutExtent);

	/** Local units per second the content is still travelling under momentum. */
	float ScrollVelocity = 0.0f;
	/** Push this box's position into the linked bar, and hide the bar when nothing can scroll. */
	void SyncScrollbar();
	/** Bind to the bar's value changes. Lazy and idempotent: the reference may not resolve until the prefab has finished loading. */
	void EnsureScrollbarBound();
	UFUNCTION()
	void HandleScrollbarValueChanged(float InValue);
	/** Set while a bar-driven change is being applied, so the push side does not answer its own pull. */
	bool bSyncingFromScrollbar = false;
	FDelegateHandle ScrollbarChangedHandle;

	/** A pointer is currently holding the content; momentum and spring-back are suspended. */
	bool bDragging = false;
	/** An eased scroll is in flight; mutually exclusive with momentum, which it cancels. */
	bool bAnimatingScroll = false;
	float AnimatedTargetOffset = 0.0f;
	/** Where an EaseCurve scroll started and how far through its duration it is. */
	float AnimatedStartOffset = 0.0f;
	float AnimatedElapsed = 0.0f;
	/** Close enough to the target to stop interpolating and land exactly on it. */
	static constexpr float ScrollAnimationSnapThreshold = 0.05f;
	/**
	 * Signed displacement past an end, already damped. Stored directly rather than derived from an
	 * undamped pull: with a saturating map the raw figure runs far ahead of what is on screen, so
	 * dragging back had to unwind hundreds of units before the content responded at all.
	 */
	float Overscroll = 0.0f;
	/** Opposing impulse per unit of overscroll, ported from the legacy view's dragForceMulitply. */
	static constexpr float OverscrollSpringStiffness = 500.0f;
	/** Positional lerp rate once the spring has killed the outward velocity. */
	static constexpr float OverscrollReturnRate = 10.0f;
	/** Below this the rubber band is snapped shut, so it does not creep towards zero forever. */
	static constexpr float OverscrollSnapThreshold = 0.1f;
	/**
	 * Below this a band value is residue, not a band: spring-back decay a grab interrupted, or
	 * float dust from the remainder arithmetic. Treating residue as "at an end" routed whole
	 * mid-range gestures into the band with the offset frozen -- caught by the live frame trace,
	 * a drag stuck at offset 74 of 4198 feeding a +0.02 leftover.
	 */
	static constexpr float OverscrollResidueThreshold = 0.5f;
	/**
	 * Distance scrolled from the start. Editable so the designer can scroll the content in the prefab
	 * editor (where no pointer input exists) to reach and edit off-screen children; the authored value
	 * is also the initial scroll position at runtime. Clamped to [0, GetMaxScrollOffset()] by layout.
	 */
	UPROPERTY(EditAnywhere, Category = "ScrollBox", meta = (ClampMin = "0.0", AllowPrivateAccess = true))
	float ScrollOffset = 0.0f;
	/** Recomputed by CalculateLayout from the measured content extent; not authored. */
	float MaxScrollOffset = 0.0f;
	UPROPERTY(Transient)
	TWeakObjectPtr<class UDreamScrollBoxInputHandler> InputHandler;
};

UCLASS(BlueprintType, DisplayName = "UMG Widget Switcher")
class DREAMGUI_API UDreamLayoutContainerWidgetSwitcher : public UDreamPanelLayoutBase
{
	GENERATED_BODY()
protected:
	virtual FVector2f MeasureLayout() const override;
	virtual void OnUnregister() override;
	TWeakObjectPtr<UDreamWidget> ActiveWidget;
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetActiveWidgetIndex, Category = "WidgetSwitcher", meta = (AllowPrivateAccess = true, ClampMin = "0"))
	int32 ActiveWidgetIndex = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetPadding, Category = "WidgetSwitcher")
	FMargin Padding;
	virtual void ArrangeChildren() override;
	UFUNCTION(BlueprintSetter, BlueprintCallable, Category = "WidgetSwitcher")
	void SetActiveWidgetIndex(int32 Value);
	UFUNCTION(BlueprintSetter) void SetPadding(FMargin Value);
	UFUNCTION(BlueprintPure, Category = "WidgetSwitcher")
	UDreamWidget* GetActiveWidget()const;
};
