// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.
// Portions derived from DreamGUI, Copyright 2026-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamLayout.h"
#include "DreamLayoutFragment.h"
#include "DreamPanelSlot.h"
#include "DreamScrollTypes.h"
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

/** Slate's EStretchDirection: which way a scale box is allowed to take its content. */
UENUM(BlueprintType)
enum class EDreamScaleBoxStretchDirection : uint8
{
	/** Scale up or down as the stretch mode asks. */
	Both,
	/** Never scale above 1: art authored at its intended size shrinks to fit and is never blown up. */
	DownOnly,
	/** Never scale below 1: content may grow into the space but is never squeezed. */
	UpOnly,
};

UCLASS(Abstract, BlueprintType)
class DREAMGUI_API UDreamPanelLayoutBase : public UDreamLayoutContainer
{
	GENERATED_BODY()

public:
	/**
	 * Where a child lands when an author ADDS one to this panel by hand -- UMG's per-slot-class
	 * defaults, which differ by panel for a reason: an overlay stacks things at their own size in its
	 * corner, a box gives each child its band, a scale box centres what it scales.
	 *
	 * Asked by authoring gestures only (a palette drop in the designer), which then write the answer
	 * into the slot as an authored value. It is NOT what a slot minted anywhere else starts with, and
	 * deliberately so: the slot's own default is Fill, every `.dui` file and every control that builds
	 * its own tree was written against that, and a child that names no alignment there has to go on
	 * meaning Fill.
	 */
	virtual void GetNewChildSlotAlignment(EDreamPanelHorizontalAlignment& OutHorizontal, EDreamPanelVerticalAlignment& OutVertical) const;

	/**
	 * Authored/intrinsic size a child wants: fitter -> container preferred -> visual intrinsic ->
	 * content children -> authored rect. Never reads a rect a panel pass has written (layout output
	 * must not feed back into measurement). Public so the prefab compiler and tests can diagnose
	 * children with no intrinsic size source.
	 */
	FVector2D GetDesiredSize(UDreamWidget* Child) const;
	/**
	 * The same question, asked inside a constraint.
	 *
	 * A child whose answer depends on the space it is given -- a wrap box, a scale box set to fit, a
	 * scroll box -- cannot answer "how big do you want to be" on its own. It used to answer by reading
	 * its OWN current width, which is the size its parent gave it on the PREVIOUS pass: measurement
	 * reading layout output, one frame stale, and the reason a wrap box inside a vertical box needed a
	 * second pass to agree with itself. The constraint now arrives as an argument instead.
	 *
	 * Specs flow DOWN from the one place real numbers exist: a panel's arrange, which knows the rect its
	 * own parent gave it. Each panel narrows what it received by the space it is about to spend
	 * (FDreamMeasureSpec::ForChild) and hands the rest on. An unconstrained ask stays unconstrained all
	 * the way down and every panel answers with its natural size, which is what the no-argument overload
	 * above means.
	 */
	FVector2D GetDesiredSize(UDreamWidget* Child, const FDreamMeasureSpec& InWidthSpec, const FDreamMeasureSpec& InHeightSpec) const;

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
	/**
	 * Place a child inside an area, honouring its slot's alignment.
	 *
	 * The two alignment overrides exist for UMG's Border, whose HorizontalAlignment and
	 * VerticalAlignment belong to the BORDER and describe where it puts its content -- unlike every
	 * other panel here, where alignment is the slot's. Passing them unset is the ordinary behaviour.
	 */
	void ApplyChildRect(UDreamWidget* Child, const FVector2D& Position, const FVector2D& Size, bool bForceFill = false,
		TOptional<EDreamPanelHorizontalAlignment> InHorizontalOverride = TOptional<EDreamPanelHorizontalAlignment>(),
		TOptional<EDreamPanelVerticalAlignment> InVerticalOverride = TOptional<EDreamPanelVerticalAlignment>()) const;
	/**
	 * The last step every placement shares: apply the slot's nudge, turn a top-left rect in the panel's
	 * content space into the child's anchored position, and record it. ApplyChildRect is this with the
	 * alignment arithmetic in front of it; a panel that has already decided a size for its own reasons
	 * (a size box under an aspect ratio) hands that answer straight here rather than restating the
	 * conversion and quietly losing the nudge along the way.
	 *
	 * It is also where layout mirroring happens; see IsRightToLeft.
	 */
	void CommitChildRect(UDreamWidget* Child, const FVector2D& InTopLeft, const FVector2D& InSize) const;
	/**
	 * Whether this panel lays its children out right to left, asked of the widget that owns it.
	 *
	 * Mirroring is done in one place -- CommitChildRect reflects the finished rect about the panel's
	 * width -- rather than inside each panel's arrangement, and that is a claim worth stating: one
	 * reflection IS the whole of what mirroring a horizontal layout means. It reverses the order of
	 * anything laid out along x (a horizontal box, a wrap box's line, a grid's columns), swaps Left
	 * and Right alignment, and swaps the left and right halves of both the panel's padding and the
	 * slot's -- because all four are statements about where in the rect the child ends up, and
	 * reflecting the rect restates every one of them at once. Nothing vertical moves.
	 *
	 * Measurement is untouched: a mirrored layout wants exactly the size the unmirrored one wanted.
	 *
	 * A canvas child positions itself through its own anchors and never reaches CommitChildRect, so a
	 * canvas is not mirrored -- which is also what Slate does with SConstraintCanvas.
	 */
	bool IsRightToLeft() const;
	/** Record a rect a panel computed itself, for the paths that do not go through ApplyChildRect. */
	void RecordChildRect(const FDreamPanelChildRect& Rect) const;
	bool BeginLayoutPass();
	/**
	 * How big this panel wants to be inside the given constraint. Both specs may be Undefined, which
	 * means "no constraint, report your natural size" -- every panel whose answer does not depend on the
	 * space available simply ignores them.
	 */
	virtual FVector2f MeasureLayout(const FDreamMeasureSpec& InWidthSpec, const FDreamMeasureSpec& InHeightSpec) const;
	/**
	 * The natural size, with no constraint at all. This is what a panel's own arrange publishes as its
	 * preferred size: the fragment states what the panel WANTS, and bounding that by the rect it was
	 * just given would be the same feedback loop the specs exist to break.
	 */
	FVector2f MeasureUnconstrained() const
	{
		return MeasureLayout(FDreamMeasureSpec::Undefined(), FDreamMeasureSpec::Undefined());
	}
	virtual FDreamLayoutControlAnchorData GetLayoutControlAnchor(const UDreamWidget* TargetWidget) const override;

	/**
	 * Each panel's arrangement algorithm. Calls ApplyChildRect / RecordChildRect and writes no child
	 * geometry itself - the base class commits the recorded fragment afterwards, in one pass.
	 */
	virtual void ArrangeChildren() {}

	/** Where the current arrange pass is recording. Null outside a pass. */
	mutable FDreamFragment* RecordingFragment = nullptr;

private:
	/**
	 * One measured widget under one constraint. The spec has to be part of the key: the same widget
	 * genuinely has different answers under different constraints, and keying on the widget alone would
	 * hand a constrained caller whatever the unconstrained one happened to ask for first.
	 *
	 * The hash deliberately ignores the spec VALUES and folds in only the two modes, because
	 * FDreamMeasureSpec's equality is a tolerance comparison and a hash built from a float would put
	 * two equal keys in different buckets. Equal keys therefore always hash equal; unequal keys with the
	 * same modes share a bucket, which costs a comparison and nothing else.
	 */
	struct FDesiredSizeKey
	{
		const UDreamWidget* Widget = nullptr;
		FDreamMeasureSpec WidthSpec;
		FDreamMeasureSpec HeightSpec;

		bool operator==(const FDesiredSizeKey& Other) const
		{
			return Widget == Other.Widget && WidthSpec == Other.WidthSpec && HeightSpec == Other.HeightSpec;
		}
		friend uint32 GetTypeHash(const FDesiredSizeKey& Key)
		{
			return HashCombine(::GetTypeHash(Key.Widget),
				static_cast<uint32>(Key.WidthSpec.Mode) * 3u + static_cast<uint32>(Key.HeightSpec.Mode));
		}
	};

	/** One pass is single-threaded, so the memo is shared across every panel taking part in it. */
	static TMap<FDesiredSizeKey, FVector2D> DesiredSizeMemo;
	static int32 DesiredSizeMemoDepth;
	static int64 DesiredSizeComputeCount;

public:
	/**
	 * For tests: how many desired-size memo scopes are open right now. Zero between passes; a test
	 * rig asserts it is zero once the rig is gone, because a scope left open keeps the shared memo
	 * answering measurements from a pass that is over. A read; it changes nothing.
	 */
	static int32 GetDesiredSizeMemoDepthForTesting() { return DesiredSizeMemoDepth; }

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
	virtual FVector2f GetLayoutPreferredSize(const FDreamMeasureSpec& InWidthSpec, const FDreamMeasureSpec& InHeightSpec) const override;
	virtual bool GetLayoutDebugInfo(const UDreamWidget* TargetWidget, FDreamLayoutDebugInfo& OutInfo) const override;
	UFUNCTION(BlueprintCallable, Category = "Panel")
	void RequestLayoutRefresh();

	/**
	 * UMG's UPanelWidget child API, forwarded to the widget that owns this container.
	 *
	 * The hierarchy here belongs to UDreamWidget and not to the panel -- a widget can take children with
	 * no container at all -- so these are deliberately forwarders and not a second list. What they buy is
	 * that a Blueprint holding the container (which is what GetLayoutContainer hands back, and what a
	 * variable typed to a specific panel holds) can ask the questions UMG lets you ask a panel, without
	 * first having to know to hop back to the widget. Every panel gets the same set from here, so no
	 * panel can grow its own dialect of it.
	 */
	UFUNCTION(BlueprintPure, Category = "Panel")
	int32 GetChildrenCount() const;
	UFUNCTION(BlueprintPure, Category = "Panel")
	UDreamWidget* GetChildAt(int32 InIndex) const;
	UFUNCTION(BlueprintPure, Category = "Panel")
	TArray<UDreamWidget*> GetAllChildren() const;
	UFUNCTION(BlueprintPure, Category = "Panel")
	int32 GetChildIndex(const UDreamWidget* InChild) const;
	UFUNCTION(BlueprintPure, Category = "Panel")
	bool HasChild(const UDreamWidget* InChild) const;
	UFUNCTION(BlueprintPure, Category = "Panel")
	bool HasAnyChildren() const;
	/** Returns the child's slot, or null -- see UDreamWidget::AddChild for what null can mean. */
	UFUNCTION(BlueprintCallable, Category = "Panel", meta = (AdvancedDisplay = "InSiblingIndex"))
	UDreamPanelSlot* AddChild(UDreamWidget* InChild, int32 InSiblingIndex = -1);
	UFUNCTION(BlueprintCallable, Category = "Panel")
	bool RemoveChild(UDreamWidget* InChild);
	UFUNCTION(BlueprintCallable, Category = "Panel")
	bool RemoveChildAt(int32 InIndex);
	/**
	 * UMG's ClearChildren under the name this plugin already gave it: UMG merely detaches and lets GC
	 * take the pieces, which here would leave live registered widgets with nobody holding them.
	 */
	UFUNCTION(BlueprintCallable, Category = "Panel")
	void DestroyAllChildren();
	/**
	 * UOverlay::ReplaceOverlayChildAt and UStackBox::ReplaceStackBoxChildAt, once, for every panel.
	 *
	 * The widget that was there is detached and left alive -- RemoveChild's contract, so the caller owns
	 * it afterwards and can put it somewhere or destroy it. The replacement lands at the same index, so
	 * sibling order (and with it paint order, for the panels that use it) is unchanged.
	 */
	UFUNCTION(BlueprintCallable, Category = "Panel")
	bool ReplaceChildAt(int32 InIndex, UDreamWidget* InContent);
};

UCLASS(BlueprintType, DisplayName = "UMG Canvas Panel")
class DREAMGUI_API UDreamLayoutContainerCanvasPanel : public UDreamPanelLayoutBase
{
	GENERATED_BODY()
protected:
	virtual FVector2f MeasureLayout(const FDreamMeasureSpec& InWidthSpec, const FDreamMeasureSpec& InHeightSpec) const override;
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
	virtual FVector2f MeasureLayout(const FDreamMeasureSpec& InWidthSpec, const FDreamMeasureSpec& InHeightSpec) const override;
public:
	/** Top-left, at the child's own size -- UOverlaySlot's default. A button dropped on an overlay is a button, not a wall. */
	virtual void GetNewChildSlotAlignment(EDreamPanelHorizontalAlignment& OutHorizontal, EDreamPanelVerticalAlignment& OutVertical) const override;
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
	virtual FVector2f MeasureLayout(const FDreamMeasureSpec& InWidthSpec, const FDreamMeasureSpec& InHeightSpec) const override;
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
	virtual FVector2f MeasureLayout(const FDreamMeasureSpec& InWidthSpec, const FDreamMeasureSpec& InHeightSpec) const override;
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetPadding, Category = "WrapBox")
	FMargin Padding;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetSpacing, Category = "WrapBox")
	FVector2D Spacing = FVector2D::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetWrapSize, Category = "WrapBox", meta = (ClampMin = "0.0"))
	float WrapSize = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetExplicitWrapSize, Category = "WrapBox")
	bool bExplicitWrapSize = false;
	/**
	 * Which way the lines run. Horizontal fills a row and wraps downwards -- the classic wrap box, and
	 * what this panel has always done; Vertical fills a column and wraps to the right.
	 *
	 * Everything else in the panel is stated in terms of the line's own axis: WrapSize is the length of
	 * a line whichever way it runs, and Spacing's x is the gap ALONG a line with y the gap between them,
	 * so a horizontal box keeps exactly the meaning it had.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetOrientation, Category = "WrapBox")
	EDreamPanelOrientation Orientation = EDreamPanelOrientation::Horizontal;
	/**
	 * Where a line that did not fill the wrap width sits inside it. UMG enables this for a horizontal
	 * box only, and so does this: in a vertical box the lines are columns and there is no horizontal
	 * line length for the value to describe.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetHorizontalAlignment, Category = "WrapBox", meta = (EditCondition = "Orientation == EDreamPanelOrientation::Horizontal"))
	EDreamPanelHorizontalAlignment HorizontalAlignment = EDreamPanelHorizontalAlignment::Left;
	UFUNCTION(BlueprintSetter) void SetPadding(FMargin Value);
	UFUNCTION(BlueprintSetter) void SetSpacing(FVector2D Value);
	UFUNCTION(BlueprintSetter) void SetWrapSize(float Value);
	UFUNCTION(BlueprintSetter) void SetExplicitWrapSize(bool Value);
	UFUNCTION(BlueprintSetter) void SetOrientation(EDreamPanelOrientation Value);
	UFUNCTION(BlueprintSetter) void SetHorizontalAlignment(EDreamPanelHorizontalAlignment Value);
	/** UMG's UWrapBox::InnerSlotPadding, which is this panel's Spacing: the gap it leaves between slots. */
	UFUNCTION(BlueprintPure, Category = "WrapBox")
	FVector2D GetInnerSlotPadding() const { return Spacing; }
	UFUNCTION(BlueprintCallable, Category = "WrapBox")
	void SetInnerSlotPadding(FVector2D InPadding) { SetSpacing(InPadding); }
	virtual void ArrangeChildren() override;
};

UCLASS(BlueprintType, DisplayName = "UMG Grid Panel")
class DREAMGUI_API UDreamLayoutContainerGridPanel : public UDreamPanelLayoutBase
{
	GENERATED_BODY()
protected:
	virtual FVector2f MeasureLayout(const FDreamMeasureSpec& InWidthSpec, const FDreamMeasureSpec& InHeightSpec) const override;
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
	/**
	 * UMG's UGridPanel::SetColumnFill(Index, Coefficient), under a name that says it addresses one track.
	 *
	 * The plain name is already the whole-array BlueprintSetter for ColumnFill, and two reflected
	 * functions cannot share a name, so the per-track form gets the suffix. The array grows to reach the
	 * index, the way UMG's does.
	 */
	UFUNCTION(BlueprintCallable, Category = "GridPanel")
	void SetColumnFillAt(int32 InColumnIndex, float InCoefficient);
	UFUNCTION(BlueprintCallable, Category = "GridPanel")
	void SetRowFillAt(int32 InRowIndex, float InCoefficient);
	/** Drop every fill coefficient, so every track goes back to sizing itself from its children. */
	UFUNCTION(BlueprintCallable, Category = "GridPanel")
	void ClearFill();
	virtual void ArrangeChildren() override;
};

UCLASS(BlueprintType, DisplayName = "UMG Uniform Grid Panel")
class DREAMGUI_API UDreamLayoutContainerUniformGridPanel : public UDreamPanelLayoutBase
{
	GENERATED_BODY()
protected:
	virtual FVector2f MeasureLayout(const FDreamMeasureSpec& InWidthSpec, const FDreamMeasureSpec& InHeightSpec) const override;
public:
	/** Top-left of its cell -- UUniformGridSlot's default. */
	virtual void GetNewChildSlotAlignment(EDreamPanelHorizontalAlignment& OutHorizontal, EDreamPanelVerticalAlignment& OutVertical) const override;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetPadding, Category = "UniformGridPanel")
	FMargin Padding;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetSpacing, Category = "UniformGridPanel")
	FVector2D Spacing = FVector2D::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetMinDesiredSlotWidth, Category = "UniformGridPanel", meta = (ClampMin = "0.0"))
	float MinDesiredSlotWidth = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetMinDesiredSlotHeight, Category = "UniformGridPanel", meta = (ClampMin = "0.0"))
	float MinDesiredSlotHeight = 0.0f;
	/**
	 * UMG's SlotPadding: breathing room INSIDE every cell, around whatever that cell holds, and part of
	 * how big a cell has to be. Spacing next to it is the gap BETWEEN cells and no part of a cell -- the
	 * two look alike on screen and differ the moment a cell has a background.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetSlotPadding, Category = "UniformGridPanel")
	FMargin SlotPadding;
	UFUNCTION(BlueprintSetter) void SetPadding(FMargin Value);
	UFUNCTION(BlueprintSetter) void SetSpacing(FVector2D Value);
	UFUNCTION(BlueprintSetter) void SetMinDesiredSlotWidth(float Value);
	UFUNCTION(BlueprintSetter) void SetMinDesiredSlotHeight(float Value);
	UFUNCTION(BlueprintSetter) void SetSlotPadding(FMargin Value);
	virtual void ArrangeChildren() override;
};

UCLASS(BlueprintType, DisplayName = "UMG Size Box")
class DREAMGUI_API UDreamLayoutContainerSizeBox : public UDreamPanelLayoutBase
{
	GENERATED_BODY()
protected:
	virtual FVector2f MeasureLayout(const FDreamMeasureSpec& InWidthSpec, const FDreamMeasureSpec& InHeightSpec) const override;
	virtual FDreamLayoutControlAnchorData GetLayoutControlAnchor(const UDreamWidget* TargetWidget) const override;
public:
	virtual int32 GetMaxChildren() const override { return 1; }
	virtual void GetRequiredBehaviourClasses(TArray<TSubclassOf<UDreamUIBehaviour>>& OutClasses) const override;
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
	/**
	 * Width-over-height bounds on the CONTENT's rect, as SBox applies them: the box itself is sized by
	 * the rules above, and a content rect whose ratio falls outside these is shrunk on one axis until it
	 * does not, then re-aligned inside the space it was offered.
	 *
	 * Zero means "not set", which is the convention MinDesiredSize and MaxDesiredSize already use here;
	 * UMG spends a separate bOverride_ bit on each instead. A pair that crosses resolves to the maximum,
	 * because the maximum is tested first, and a content rect with a zero side is left alone -- there is
	 * no ratio to correct.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetMinAspectRatio, Category = "SizeBox", meta = (ClampMin = "0.0"))
	float MinAspectRatio = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetMaxAspectRatio, Category = "SizeBox", meta = (ClampMin = "0.0"))
	float MaxAspectRatio = 0.0f;
	UFUNCTION(BlueprintSetter) void SetPadding(FMargin Value);
	UFUNCTION(BlueprintSetter) void SetOverrideWidth(bool Value);
	UFUNCTION(BlueprintSetter) void SetWidthOverride(float Value);
	UFUNCTION(BlueprintSetter) void SetOverrideHeight(bool Value);
	UFUNCTION(BlueprintSetter) void SetHeightOverride(float Value);
	UFUNCTION(BlueprintSetter) void SetMinDesiredSize(FVector2D Value);
	UFUNCTION(BlueprintSetter) void SetMaxDesiredSize(FVector2D Value);
	UFUNCTION(BlueprintSetter) void SetMinAspectRatio(float Value);
	UFUNCTION(BlueprintSetter) void SetMaxAspectRatio(float Value);

	/**
	 * USizeBox's per-axis accessors. The two bounds are stored as one FVector2D apiece here, which is
	 * the same four numbers with half the fields; these name the axis the way UMG does, and Clear is
	 * "put that axis back to zero", which is how this class already spells "no opinion".
	 */
	UFUNCTION(BlueprintCallable, Category = "SizeBox")
	void ClearWidthOverride();
	UFUNCTION(BlueprintCallable, Category = "SizeBox")
	void ClearHeightOverride();
	UFUNCTION(BlueprintPure, Category = "SizeBox")
	float GetMinDesiredWidth() const { return static_cast<float>(MinDesiredSize.X); }
	UFUNCTION(BlueprintCallable, Category = "SizeBox")
	void SetMinDesiredWidth(float InMinDesiredWidth);
	UFUNCTION(BlueprintCallable, Category = "SizeBox")
	void ClearMinDesiredWidth();
	UFUNCTION(BlueprintPure, Category = "SizeBox")
	float GetMinDesiredHeight() const { return static_cast<float>(MinDesiredSize.Y); }
	UFUNCTION(BlueprintCallable, Category = "SizeBox")
	void SetMinDesiredHeight(float InMinDesiredHeight);
	UFUNCTION(BlueprintCallable, Category = "SizeBox")
	void ClearMinDesiredHeight();
	UFUNCTION(BlueprintPure, Category = "SizeBox")
	float GetMaxDesiredWidth() const { return static_cast<float>(MaxDesiredSize.X); }
	UFUNCTION(BlueprintCallable, Category = "SizeBox")
	void SetMaxDesiredWidth(float InMaxDesiredWidth);
	UFUNCTION(BlueprintCallable, Category = "SizeBox")
	void ClearMaxDesiredWidth();
	UFUNCTION(BlueprintPure, Category = "SizeBox")
	float GetMaxDesiredHeight() const { return static_cast<float>(MaxDesiredSize.Y); }
	UFUNCTION(BlueprintCallable, Category = "SizeBox")
	void SetMaxDesiredHeight(float InMaxDesiredHeight);
	UFUNCTION(BlueprintCallable, Category = "SizeBox")
	void ClearMaxDesiredHeight();
	UFUNCTION(BlueprintCallable, Category = "SizeBox")
	void ClearMinAspectRatio();
	UFUNCTION(BlueprintCallable, Category = "SizeBox")
	void ClearMaxAspectRatio();
	UFUNCTION(BlueprintPure, Category = "SizeBox")
	bool IsMinAspectRatioOverride() const { return MinAspectRatio > 0.0f; }
	UFUNCTION(BlueprintPure, Category = "SizeBox")
	bool IsMaxAspectRatioOverride() const { return MaxAspectRatio > 0.0f; }

	/**
	 * Shrink Size to the nearest rect inside MaxSize that satisfies the ratio bounds, a pure function
	 * so the arithmetic can be checked without a panel. The two alignment flags say which axis leads
	 * when the correction has a choice, exactly as SBox's do.
	 */
	static FVector2D ConstrainToAspectRatio(const FVector2D& InSize, const FVector2D& InMaxSize,
		float InMinAspectRatio, float InMaxAspectRatio, bool bInFillHorizontally, bool bInFillVertically);
	virtual void ArrangeChildren() override;

private:
	/** The placement path taken only when a ratio bound is set; see the note on the implementation. */
	void ArrangeContentWithinAspectRatio(UDreamWidget* InContent, const FVector2D& InBoxPosition, const FVector2D& InBoxSize);
};

UCLASS(BlueprintType, DisplayName = "UMG Scale Box")
class DREAMGUI_API UDreamLayoutContainerScaleBox : public UDreamPanelLayoutBase
{
	GENERATED_BODY()
public:
	/** Centred -- UScaleBoxSlot's default, and what a slot minted at registration under this panel already gets. */
	virtual void GetNewChildSlotAlignment(EDreamPanelHorizontalAlignment& OutHorizontal, EDreamPanelVerticalAlignment& OutVertical) const override;
protected:
	virtual FVector2f MeasureLayout(const FDreamMeasureSpec& InWidthSpec, const FDreamMeasureSpec& InHeightSpec) const override;
	virtual FDreamLayoutControlAnchorData GetLayoutControlAnchor(const UDreamWidget* TargetWidget) const override;
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	void UpdateClippingOverride();
	TWeakObjectPtr<UDreamWidget> ScaledChild;
	bool bAppliedDefaultClipping = false;
public:
	virtual int32 GetMaxChildren() const override { return 1; }
	virtual void GetRequiredBehaviourClasses(TArray<TSubclassOf<UDreamUIBehaviour>>& OutClasses) const override;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetPadding, Category = "ScaleBox")
	FMargin Padding;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetStretch, Category = "ScaleBox")
	EDreamScaleBoxStretch Stretch = EDreamScaleBoxStretch::ScaleToFit;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetUserSpecifiedScale, Category = "ScaleBox", meta = (ClampMin = "0.0"))
	float UserSpecifiedScale = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetIgnoreInheritedScale, Category = "ScaleBox")
	bool bIgnoreInheritedScale = false;
	/**
	 * Which way the box is allowed to scale, matching Slate's EStretchDirection. DownOnly is the one
	 * that gets used: art authored at its intended size should shrink to fit a small screen and never
	 * be blown up past it, which is a rule the Stretch modes cannot state on their own.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetStretchDirection, Category = "ScaleBox")
	EDreamScaleBoxStretchDirection StretchDirection = EDreamScaleBoxStretchDirection::Both;
	UFUNCTION(BlueprintSetter) void SetPadding(FMargin Value);
	UFUNCTION(BlueprintSetter) void SetStretch(EDreamScaleBoxStretch Value);
	UFUNCTION(BlueprintSetter) void SetUserSpecifiedScale(float Value);
	UFUNCTION(BlueprintSetter) void SetIgnoreInheritedScale(bool Value);
	UFUNCTION(BlueprintSetter) void SetStretchDirection(EDreamScaleBoxStretchDirection Value);
	virtual void ArrangeChildren() override;
};

UCLASS(BlueprintType, DisplayName = "UMG Safe Zone")
class DREAMGUI_API UDreamLayoutContainerSafeZone : public UDreamPanelLayoutBase
{
	GENERATED_BODY()
protected:
	FMargin GetCombinedSafePadding() const;
	virtual FVector2f MeasureLayout(const FDreamMeasureSpec& InWidthSpec, const FDreamMeasureSpec& InHeightSpec) const override;
	virtual FDreamLayoutControlAnchorData GetLayoutControlAnchor(const UDreamWidget* TargetWidget) const override;
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	void HandleSafeFrameChanged();
	FDelegateHandle SafeFrameChangedHandle;
public:
	virtual int32 GetMaxChildren() const override { return 1; }
	virtual void GetRequiredBehaviourClasses(TArray<TSubclassOf<UDreamUIBehaviour>>& OutClasses) const override;
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
	/**
	 * Per-side multiplier over the padding the PLATFORM reports, matching SSafeZone's SafeAreaScale:
	 * one is the whole reported inset, zero ignores that side entirely. SafePadding is added on top of
	 * the scaled result and is not touched by it, as in Slate.
	 *
	 * The four bPad* switches are the same idea at its extremes; this is the dial between them, for the
	 * phone whose reported bottom inset is a gesture bar you only want to half-clear.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetSafeAreaScale, Category = "SafeZone", meta = (ClampMin = "0.0"))
	FMargin SafeAreaScale = FMargin(1.0f);
	UFUNCTION(BlueprintSetter) void SetSafeAreaScale(FMargin Value);
	UFUNCTION(BlueprintSetter) void SetUsePlatformSafeZone(bool Value);
	UFUNCTION(BlueprintSetter) void SetPadLeft(bool Value);
	UFUNCTION(BlueprintSetter) void SetPadTop(bool Value);
	UFUNCTION(BlueprintSetter) void SetPadRight(bool Value);
	UFUNCTION(BlueprintSetter) void SetPadBottom(bool Value);
	UFUNCTION(BlueprintSetter) void SetSafePadding(FMargin Value);
	UFUNCTION(BlueprintSetter) void SetNormalizedSafePadding(FMargin Value);
	/**
	 * USafeZone::SetSidesToPad: the four side switches in one call, in UMG's argument order
	 * (left, right, top, bottom -- not the order an FMargin lists them in).
	 */
	UFUNCTION(BlueprintCallable, Category = "SafeZone")
	void SetSidesToPad(bool bInPadLeft, bool bInPadRight, bool bInPadTop, bool bInPadBottom);
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
// EDreamScrollBoxConsumeMouseWheel used to be declared here. It moved to DreamScrollTypes.h (included
// at the top of this file, so every reader of this header still sees it) once UUIScrollView needed to
// answer the same question: a behaviour cannot include a layout container, and the wheel-consumption
// question belongs to neither of them.

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
	virtual FVector2f MeasureLayout(const FDreamMeasureSpec& InWidthSpec, const FDreamMeasureSpec& InHeightSpec) const override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
public:
	UDreamLayoutContainerScrollBox();
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void BeginPlay() override;
	virtual void EndPlay() override;

	/**
	 * Local units scrolled per wheel notch.
	 *
	 * BlueprintSetter, like every writable knob below it: a plain Blueprint write onto the variable
	 * skips whatever the setter does about it, and on this box several of these have to re-measure,
	 * re-sync the bar or re-arm the physics before the number means anything.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetScrollSensitivity", BlueprintSetter = "SetScrollSensitivity", Category = "ScrollBox", meta = (ClampMin = "0.0"))
	float ScrollSensitivity = 40.0f;

	/** Whether the wheel is swallowed here or handed to an outer scroll box once this one is at a limit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetConsumeMouseWheel", BlueprintSetter = "SetConsumeMouseWheel", Category = "ScrollBox")
	EDreamScrollBoxConsumeMouseWheel ConsumeMouseWheel = EDreamScrollBoxConsumeMouseWheel::WhenScrollingPossible;

	/**
	 * A plain multiplier on whatever one wheel notch already travels -- UMG's WheelScrollMultiplier.
	 * ScrollSensitivity is the distance; this scales it, so "the same feel, twice as fast" is one
	 * number and not a re-tuned one.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetWheelScrollMultiplier", BlueprintSetter = "SetWheelScrollMultiplier", Category = "ScrollBox", meta = (ClampMin = "0.0"))
	float WheelScrollMultiplier = 1.0f;

	/** Where a widget revealed by ScrollWidgetIntoView ends up -- UMG's NavigationDestination. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetNavigationDestination", BlueprintSetter = "SetNavigationDestination", Category = "ScrollBox", meta = (InvalidEnumValues = "Configured"))
	EDreamUIScrollDestination NavigationDestination = EDreamUIScrollDestination::IntoView;

	/**
	 * How much of the window to keep clear around a revealed widget, in local units -- UMG's
	 * NavigationScrollPadding. Stops a row landing flush against the edge with its neighbour cut in
	 * half beside it, which is the only cue a player has that the list continues.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetNavigationScrollPadding", BlueprintSetter = "SetNavigationScrollPadding", Category = "ScrollBox", meta = (ClampMin = "0.0"))
	float NavigationScrollPadding = 0.0f;

	/**
	 * What this box does when user focus lands inside it -- UMG's ScrollWhenFocusChanges. Read by
	 * FDreamUINavigationScroll, which is the only thing that reveals a widget because focus moved.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetScrollWhenFocusChanges", BlueprintSetter = "SetScrollWhenFocusChanges", Category = "ScrollBox")
	EDreamUIScrollWhenFocusChanges ScrollWhenFocusChanges = EDreamUIScrollWhenFocusChanges::AnimatedScroll;

	/**
	 * The analog key that acts as this box's mouse wheel -- UMG's AnalogMouseWheelKey. Invalid (the
	 * default) means "whatever the input preset already routes here", which is the right stick and
	 * therefore exactly today's behaviour.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetAnalogMouseWheelKey", BlueprintSetter = "SetAnalogMouseWheelKey", Category = "ScrollBox")
	FKey AnalogMouseWheelKey;

	UPROPERTY(BlueprintAssignable, Category = "ScrollBox")
	FDreamScrollBoxUserScrolledEvent OnUserScrolled;

	/**
	 * A UUIScrollbar to keep in step with this box, both ways. Assign the component from a
	 * scrollbar prefab (/DreamGUI/Prefabs/VerticalScrollbar or HorizontalScrollbar) placed anywhere in
	 * the hierarchy -- it does not have to be a child of this box. Which end of the bar means zero
	 * is the BAR's business: this box always feeds the raw 0..1 fraction and the bar's own
	 * DirectionType decides the mapping.
	 *
	 * The one knob on this box with no BlueprintSetter, and the reason is the TYPE: a BlueprintSetter
	 * has to take the property's own type, and a weak pointer is not something a UFUNCTION parameter
	 * can be. SetScrollbar below takes the raw pointer a caller actually has, and is the Blueprint
	 * road; the exception is registered rather than left to be noticed.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ScrollBox")
	TWeakObjectPtr<class UUIScrollbar> Scrollbar;
	/** Whether that bar hides itself when the content already fits. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetScrollbarVisibility", BlueprintSetter = "SetScrollbarVisibility", Category = "ScrollBox")
	EDreamScrollBoxScrollbarVisibility ScrollbarVisibility = EDreamScrollBoxScrollbarVisibility::AutoHide;

	/** Ease to the wheel's new position over a few frames instead of jumping there. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetAnimateWheelScrolling", BlueprintSetter = "SetAnimateWheelScrolling", Category = "ScrollBox")
	bool bAnimateWheelScrolling = false;
	/** Which curve an eased scroll follows. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetScrollAnimationMode", BlueprintSetter = "SetScrollAnimationMode", Category = "ScrollBox")
	EDreamScrollAnimationMode ScrollAnimationMode = EDreamScrollAnimationMode::InterpToSpeed;
	/** FInterpTo speed, used by InterpToSpeed only. Larger arrives sooner; UMG's default is 15. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetScrollAnimationInterpolationSpeed", BlueprintSetter = "SetScrollAnimationInterpolationSpeed", Category = "ScrollBox", meta = (ClampMin = "0.0", EditCondition = "ScrollAnimationMode == EDreamScrollAnimationMode::InterpToSpeed"))
	float ScrollAnimationInterpolationSpeed = 15.0f;
	/** Curve shape, used by EaseCurve only. Any of DreamTween's easings, overshooting ones included. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetScrollAnimationEase", BlueprintSetter = "SetScrollAnimationEase", Category = "ScrollBox", meta = (EditCondition = "ScrollAnimationMode == EDreamScrollAnimationMode::EaseCurve"))
	EDreamTweenEase ScrollAnimationEase = EDreamTweenEase::OutCubic;
	/** Seconds an eased scroll takes, used by EaseCurve only. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetScrollAnimationDuration", BlueprintSetter = "SetScrollAnimationDuration", Category = "ScrollBox", meta = (ClampMin = "0.0", EditCondition = "ScrollAnimationMode == EDreamScrollAnimationMode::EaseCurve"))
	float ScrollAnimationDuration = 0.25f;

	/** Keep moving under momentum after the finger or mouse lets go. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetEnableInertia", BlueprintSetter = "SetEnableInertia", Category = "ScrollBox")
	bool bEnableInertia = true;
	/**
	 * How quickly momentum dies. 0 never slows down; larger values stop sooner. Same units and
	 * default as the legacy scroll view, so a value tuned there carries over.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetDecelerationRate", BlueprintSetter = "SetDecelerationRate", Category = "ScrollBox", meta = (ClampMin = "0.0"))
	float DecelerationRate = 0.135f;
	/** Let the content rubber-band past an end while dragging, then spring back on release. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetAllowOverscroll", BlueprintSetter = "SetAllowOverscroll", Category = "ScrollBox")
	bool bAllowOverscroll = true;
	/**
	 * How far past an end the content can be pulled, in local units. The pull saturates towards this
	 * rather than stopping at it, so the resistance grows the further out you drag and the content
	 * can never be dragged away indefinitely -- which the legacy view's flat damping allowed.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetOverscrollLimit", BlueprintSetter = "SetOverscrollLimit", Category = "ScrollBox", meta = (ClampMin = "0.0"))
	float OverscrollLimit = 120.0f;

	/**
	 * The plain readers and writers for every knob above.
	 *
	 * Most of them are one line, and that is the point: a Blueprint write has to land on the setter
	 * rather than on the variable, because several of them (the pads, the bar, the wheel) have work
	 * to do before the number is true. Spelling all of them the same way is what makes that a rule
	 * rather than a thing to remember per property.
	 */
	UFUNCTION(BlueprintPure, Category = "ScrollBox")
	float GetScrollSensitivity() const { return ScrollSensitivity; }
	UFUNCTION(BlueprintCallable, Category = "ScrollBox")
	void SetScrollSensitivity(float Value);
	UFUNCTION(BlueprintPure, Category = "ScrollBox")
	EDreamScrollBoxConsumeMouseWheel GetConsumeMouseWheel() const { return ConsumeMouseWheel; }
	UFUNCTION(BlueprintCallable, Category = "ScrollBox")
	void SetConsumeMouseWheel(EDreamScrollBoxConsumeMouseWheel Value) { ConsumeMouseWheel = Value; }
	UFUNCTION(BlueprintPure, Category = "ScrollBox")
	float GetWheelScrollMultiplier() const { return WheelScrollMultiplier; }
	UFUNCTION(BlueprintCallable, Category = "ScrollBox")
	void SetWheelScrollMultiplier(float Value);
	UFUNCTION(BlueprintPure, Category = "ScrollBox")
	EDreamUIScrollDestination GetNavigationDestination() const { return NavigationDestination; }
	UFUNCTION(BlueprintCallable, Category = "ScrollBox")
	void SetNavigationDestination(EDreamUIScrollDestination Value);
	UFUNCTION(BlueprintPure, Category = "ScrollBox")
	float GetNavigationScrollPadding() const { return NavigationScrollPadding; }
	UFUNCTION(BlueprintCallable, Category = "ScrollBox")
	void SetNavigationScrollPadding(float Value);
	UFUNCTION(BlueprintPure, Category = "ScrollBox")
	EDreamUIScrollWhenFocusChanges GetScrollWhenFocusChanges() const { return ScrollWhenFocusChanges; }
	UFUNCTION(BlueprintCallable, Category = "ScrollBox")
	void SetScrollWhenFocusChanges(EDreamUIScrollWhenFocusChanges Value) { ScrollWhenFocusChanges = Value; }
	UFUNCTION(BlueprintPure, Category = "ScrollBox")
	FKey GetAnalogMouseWheelKey() const { return AnalogMouseWheelKey; }
	UFUNCTION(BlueprintCallable, Category = "ScrollBox")
	void SetAnalogMouseWheelKey(FKey Value) { AnalogMouseWheelKey = Value; }
	/** The bar this box drives. Takes the raw pointer, because the stored form is a weak one. */
	UFUNCTION(BlueprintPure, Category = "ScrollBox")
	UUIScrollbar* GetScrollbar() const { return Scrollbar.Get(); }
	UFUNCTION(BlueprintCallable, Category = "ScrollBox")
	void SetScrollbar(UUIScrollbar* Value);
	UFUNCTION(BlueprintPure, Category = "ScrollBox")
	EDreamScrollBoxScrollbarVisibility GetScrollbarVisibility() const { return ScrollbarVisibility; }
	UFUNCTION(BlueprintCallable, Category = "ScrollBox")
	void SetScrollbarVisibility(EDreamScrollBoxScrollbarVisibility Value);
	UFUNCTION(BlueprintPure, Category = "ScrollBox")
	bool GetAnimateWheelScrolling() const { return bAnimateWheelScrolling; }
	UFUNCTION(BlueprintCallable, Category = "ScrollBox")
	void SetAnimateWheelScrolling(bool Value) { bAnimateWheelScrolling = Value; }
	UFUNCTION(BlueprintPure, Category = "ScrollBox")
	EDreamScrollAnimationMode GetScrollAnimationMode() const { return ScrollAnimationMode; }
	UFUNCTION(BlueprintCallable, Category = "ScrollBox")
	void SetScrollAnimationMode(EDreamScrollAnimationMode Value) { ScrollAnimationMode = Value; }
	UFUNCTION(BlueprintPure, Category = "ScrollBox")
	float GetScrollAnimationInterpolationSpeed() const { return ScrollAnimationInterpolationSpeed; }
	UFUNCTION(BlueprintCallable, Category = "ScrollBox")
	void SetScrollAnimationInterpolationSpeed(float Value);
	UFUNCTION(BlueprintPure, Category = "ScrollBox")
	EDreamTweenEase GetScrollAnimationEase() const { return ScrollAnimationEase; }
	UFUNCTION(BlueprintCallable, Category = "ScrollBox")
	void SetScrollAnimationEase(EDreamTweenEase Value) { ScrollAnimationEase = Value; }
	UFUNCTION(BlueprintPure, Category = "ScrollBox")
	float GetScrollAnimationDuration() const { return ScrollAnimationDuration; }
	UFUNCTION(BlueprintCallable, Category = "ScrollBox")
	void SetScrollAnimationDuration(float Value);
	UFUNCTION(BlueprintPure, Category = "ScrollBox")
	bool GetEnableInertia() const { return bEnableInertia; }
	UFUNCTION(BlueprintCallable, Category = "ScrollBox")
	void SetEnableInertia(bool Value);
	UFUNCTION(BlueprintPure, Category = "ScrollBox")
	float GetDecelerationRate() const { return DecelerationRate; }
	UFUNCTION(BlueprintCallable, Category = "ScrollBox")
	void SetDecelerationRate(float Value);
	UFUNCTION(BlueprintPure, Category = "ScrollBox")
	bool GetAllowOverscroll() const { return bAllowOverscroll; }
	UFUNCTION(BlueprintCallable, Category = "ScrollBox")
	void SetAllowOverscroll(bool Value);
	UFUNCTION(BlueprintPure, Category = "ScrollBox")
	float GetOverscrollLimit() const { return OverscrollLimit; }
	UFUNCTION(BlueprintCallable, Category = "ScrollBox")
	void SetOverscrollLimit(float Value);

	/**
	 * True when a GESTURE on this box has to be read backwards -- a horizontal box in a right-to-left
	 * layout, and nothing else.
	 *
	 * The offset itself needs no flipping, and that is worth spelling out because it looks like it
	 * should: the arrangement walks the children in content space and subtracts the offset there, and
	 * CommitChildRect reflects the finished rect afterwards. So offset still means "how far from the
	 * content's START edge", the start edge is simply the right one now, and a larger offset still
	 * reveals later items. Overscroll rides the same arithmetic and follows for free.
	 *
	 * What does NOT follow is the pointer: a drag delta and a wheel axis arrive in the panel's own
	 * space and are never reflected, so without this the content would run away from the finger in a
	 * mirrored layout. One sign, in the one place gestures become offsets.
	 */
	UFUNCTION(BlueprintPure, Category = "ScrollBox")
	bool IsScrollGestureMirrored() const;

	/** Signed displacement past an end, damped; zero while in range. Layout adds it to the offset. */
	UFUNCTION(BlueprintCallable, Category = "ScrollBox")
	float GetOverscroll() const;
	/** The same displacement as a PERCENTAGE of the window -- UMG's GetOverscrollPercentage. */
	UFUNCTION(BlueprintPure, Category = "ScrollBox")
	float GetOverscrollPercentage() const;
	/** Drop the fling, keeping the position and whatever rubber band is open -- UMG's name for it. */
	UFUNCTION(BlueprintCallable, Category = "ScrollBox")
	void EndInertialScrolling();
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
	 *
	 * @param InDestination Where the widget ends up. Configured (the default) leaves it to this box's
	 *                      own NavigationDestination, which is what every caller meant before the
	 *                      parameter existed.
	 * @param InPadding     How much of the window to keep clear around it. Negative leaves it to this
	 *                      box's NavigationScrollPadding, for the same reason.
	 */
	UFUNCTION(BlueprintCallable, Category = "ScrollBox")
	bool ScrollWidgetIntoView(UDreamWidget* InWidget, bool bAnimateScroll = true,
		EDreamUIScrollDestination InDestination = EDreamUIScrollDestination::Configured, float InPadding = -1.0f);
	/**
	 * True when InWidget lives in this box and a scroll would bring more of it into sight. The question
	 * directional navigation has to answer before it commits to a candidate it cannot currently see.
	 */
	UFUNCTION(BlueprintCallable, Category = "ScrollBox")
	bool CanScrollWidgetIntoView(UDreamWidget* InWidget);
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
	/**
	 * Offset that brings InWidget into view with the least movement. False when InWidget is not in this
	 * box or is already fully visible, so the query and the scroll answer from the same arithmetic
	 * rather than from two copies of it that can drift apart.
	 */
	bool CalculateOffsetToReveal(UDreamWidget* InWidget, float& OutTarget,
		EDreamUIScrollDestination InDestination = EDreamUIScrollDestination::Configured, float InPadding = -1.0f);

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
	virtual FVector2f MeasureLayout(const FDreamMeasureSpec& InWidthSpec, const FDreamMeasureSpec& InHeightSpec) const override;
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
	/**
	 * UMG's UWidgetSwitcher::SetActiveWidget: show a page by identity rather than by position.
	 *
	 * The index stays the single source of truth (GetActiveWidget resolves through it), so this is a
	 * lookup followed by SetActiveWidgetIndex. Anything that is not a child of this panel is ignored:
	 * silently switching to page 0 is the failure mode that made the index setter clamp in the first
	 * place. Returns whether the widget was found.
	 */
	UFUNCTION(BlueprintCallable, Category = "WidgetSwitcher")
	bool SetActiveWidget(UDreamWidget* Value);
	UFUNCTION(BlueprintSetter) void SetPadding(FMargin Value);
	UFUNCTION(BlueprintPure, Category = "WidgetSwitcher")
	UDreamWidget* GetActiveWidget()const;
	UFUNCTION(BlueprintPure, Category = "WidgetSwitcher")
	int32 GetActiveWidgetIndex() const { return ActiveWidgetIndex; }
	/**
	 * How many pages this switcher has. UMG names it for the panel rather than for the children because
	 * that is what a switcher's children are; it answers the same number GetChildrenCount does.
	 */
	UFUNCTION(BlueprintPure, Category = "WidgetSwitcher")
	int32 GetNumWidgets() const { return GetChildrenCount(); }
	UFUNCTION(BlueprintPure, Category = "WidgetSwitcher")
	UDreamWidget* GetWidgetAtIndex(int32 InIndex) const { return GetChildAt(InIndex); }
};

/**
 * UMG's UBorder: one child, padded, aligned by the BORDER rather than by the child's slot.
 *
 * That last part is the only thing here that is not already expressible as an Overlay with a rect-block
 * visual, and it is the reason this is a class. Everywhere else in this plugin alignment belongs to the
 * slot -- "where do I sit in what I was given" -- and a Border inverts it: the alignment is the
 * container's statement about where it puts its content. Authors coming from UMG set HorizontalAlignment
 * on the Border, and before this that property had nowhere to live.
 *
 * The background is the owning widget's own visual, which is how every other drawn thing works here;
 * BrushColor writes through to it so UBorder::SetBrushColor has a counterpart. There is no Background
 * FSlateBrush: a DreamGUI widget's art comes from its visual (sprite, rect block, image), and adding a
 * second source of it would be a fork of the render path rather than a port of a panel.
 */
UCLASS(BlueprintType, DisplayName = "UMG Border")
class DREAMGUI_API UDreamLayoutContainerBorder : public UDreamPanelLayoutBase
{
	GENERATED_BODY()
protected:
	virtual FVector2f MeasureLayout(const FDreamMeasureSpec& InWidthSpec, const FDreamMeasureSpec& InHeightSpec) const override;
	virtual FDreamLayoutControlAnchorData GetLayoutControlAnchor(const UDreamWidget* TargetWidget) const override;
	virtual void OnRegister() override;
	/** Push BrushColor onto the owning widget's visual, which is what actually draws the background. */
	void ApplyBrushColorToVisual() const;
public:
	virtual int32 GetMaxChildren() const override { return 1; }
	/** A content widget, like every other single-child panel here. */
	virtual void GetRequiredBehaviourClasses(TArray<TSubclassOf<UDreamUIBehaviour>>& OutClasses) const override;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetPadding, Category = "Border")
	FMargin Padding;
	/** Where the border puts its content. Overrides the content slot's own alignment, as UMG's does. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetHorizontalAlignment, Category = "Border")
	EDreamPanelHorizontalAlignment HorizontalAlignment = EDreamPanelHorizontalAlignment::Fill;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetVerticalAlignment, Category = "Border")
	EDreamPanelVerticalAlignment VerticalAlignment = EDreamPanelVerticalAlignment::Fill;
	/** UMG's UBorder::DesiredSizeScale: scales what this border REPORTS, not what it arranges. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetDesiredSizeScale, Category = "Border")
	FVector2D DesiredSizeScale = FVector2D(1.0, 1.0);
	/** Written through to the owning widget's visual, which is what actually draws the background. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetBrushColor, Category = "Border")
	FLinearColor BrushColor = FLinearColor::White;

	UFUNCTION(BlueprintSetter) void SetPadding(FMargin Value);
	UFUNCTION(BlueprintSetter) void SetHorizontalAlignment(EDreamPanelHorizontalAlignment Value);
	UFUNCTION(BlueprintSetter) void SetVerticalAlignment(EDreamPanelVerticalAlignment Value);
	UFUNCTION(BlueprintSetter) void SetDesiredSizeScale(FVector2D Value);
	UFUNCTION(BlueprintSetter) void SetBrushColor(FLinearColor Value);
	virtual void ArrangeChildren() override;
};

/** UMG's EMenuPlacement, for the placements a rect-against-rect layout can actually express. */
UENUM(BlueprintType)
enum class EDreamMenuPlacement : uint8
{
	/** Directly below the anchor, left edges aligned. UMG's MenuPlacement_BelowAnchor. */
	BelowAnchor,
	/** Below the anchor, horizontally centred on it. */
	CenteredBelowAnchor,
	/** Below the anchor, right edges aligned. */
	BelowRightAnchor,
	/** Below the anchor and forced to the anchor's width. UMG's MenuPlacement_ComboBox. */
	ComboBox,
	/** Below the anchor, right-aligned, forced to the anchor's width. */
	ComboBoxRight,
	/** Directly above the anchor, left edges aligned. */
	AboveAnchor,
	/** Above the anchor, horizontally centred on it. */
	CenteredAboveAnchor,
	/** Above the anchor, right edges aligned. */
	AboveRightAnchor,
	/** To the right of the anchor, top edges aligned. UMG's MenuPlacement_MenuRight. */
	MenuRight,
	/** To the left of the anchor, top edges aligned. */
	MenuLeft,
	/** Centred over the anchor on both axes. */
	Center,
};

/**
 * UMG's UMenuAnchor, layout side.
 *
 * The first child is the anchor -- the button, the combo box face -- and is filled like a Border's
 * content. An optional second child is the menu: it does NOT contribute to this panel's measured size
 * (a menu that grew its own button would be unusable) and is placed against the anchor's rect by
 * Placement, then clamped into the root widget when bFitInWindow is set.
 *
 * What is deliberately NOT here is the popup's lifetime: creating menu content from a class, owning it
 * on a popup layer, dismissing it on a click elsewhere. UIDropdown and DreamUIModal already each carry
 * a version of that, and a third would be a fork rather than a port. This is the placement arithmetic
 * and the open/closed state those two can be expressed in terms of.
 */
UCLASS(BlueprintType, DisplayName = "UMG Menu Anchor")
class DREAMGUI_API UDreamLayoutContainerMenuAnchor : public UDreamPanelLayoutBase
{
	GENERATED_BODY()
protected:
	virtual FVector2f MeasureLayout(const FDreamMeasureSpec& InWidthSpec, const FDreamMeasureSpec& InHeightSpec) const override;
	virtual FDreamLayoutControlAnchorData GetLayoutControlAnchor(const UDreamWidget* TargetWidget) const override;
	virtual void OnUnregister() override;
public:
	/** The anchor plus at most one menu. */
	virtual int32 GetMaxChildren() const override { return 2; }
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetPlacement, Category = "MenuAnchor")
	EDreamMenuPlacement Placement = EDreamMenuPlacement::ComboBox;
	/** Keep the placed menu inside the root widget's rect, shifting it rather than letting it overhang. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetFitInWindow, Category = "MenuAnchor")
	bool bFitInWindow = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetIsOpen, Category = "MenuAnchor")
	bool bIsOpen = false;

	UFUNCTION(BlueprintSetter) void SetPlacement(EDreamMenuPlacement Value);
	UFUNCTION(BlueprintSetter) void SetFitInWindow(bool Value);
	/** Show or hide the menu child. Collapsed when closed, so it costs no layout and no hit test. */
	UFUNCTION(BlueprintSetter, BlueprintCallable, Category = "MenuAnchor")
	void SetIsOpen(bool Value);
	UFUNCTION(BlueprintPure, Category = "MenuAnchor")
	bool IsOpen() const { return bIsOpen; }
	UFUNCTION(BlueprintCallable, Category = "MenuAnchor")
	void ToggleOpen() { SetIsOpen(!bIsOpen); }
	/** The first child: the thing the menu is anchored to. Null when the panel is empty. */
	UFUNCTION(BlueprintPure, Category = "MenuAnchor")
	UDreamWidget* GetAnchorContent() const;
	/** The second child, if there is one. Null when this anchor has no menu authored under it. */
	UFUNCTION(BlueprintPure, Category = "MenuAnchor")
	UDreamWidget* GetMenuContent() const;

	/**
	 * Where a menu of MenuSize goes against an anchor of AnchorSize placed at AnchorPosition, in the
	 * panel's top-left content space. Static and pure so the arithmetic can be tested on its own, which
	 * is the half of UMenuAnchor that has a right answer.
	 */
	static FVector2D CalculateMenuPosition(EDreamMenuPlacement InPlacement, const FVector2D& AnchorPosition,
		const FVector2D& AnchorSize, const FVector2D& MenuSize);
	/** UMG's bFitInWindow: shift (never resize) the menu so it lies inside WindowSize. */
	static FVector2D FitMenuInWindow(const FVector2D& MenuPosition, const FVector2D& MenuSize, const FVector2D& WindowSize);
	/**
	 * FitMenuInWindow for a rect given in the PANEL's space, which is the space the arrange pass works
	 * in. PanelOffset is the panel's top-left corner inside the window.
	 *
	 * bMirrored says the rect is about to be reflected across PanelWidth when it is committed, as every
	 * child rect is under a right-to-left flow. The rect that has to end up inside the window is then
	 * the reflected one, so it is reflected, fitted, and the answer reflected back: what this returns is
	 * still what to COMMIT, and committing it lands the menu where the fit put it.
	 */
	static FVector2D FitMenuInWindowFromPanelSpace(const FVector2D& MenuPosition, const FVector2D& MenuSize,
		const FVector2D& PanelOffset, float PanelWidth, const FVector2D& WindowSize, bool bMirrored);
	/** True when Placement forces the menu to the anchor's width, as UMG's ComboBox placements do. */
	static bool PlacementMatchesAnchorWidth(EDreamMenuPlacement InPlacement);

	virtual void ArrangeChildren() override;
};
