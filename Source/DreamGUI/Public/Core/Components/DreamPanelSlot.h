// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DreamUIAnchorData.h"
#include "DreamWidgetSubObjectBehaviour.h"
#include "Widgets/Layout/Anchors.h"
#include "DreamPanelSlot.generated.h"

// Defined with the widget; forward declared here so this header stays off the big one.
enum class EDreamLayoutInvalidation : uint8;

UENUM(BlueprintType)
enum class EDreamPanelHorizontalAlignment : uint8
{
	Fill,
	Left,
	Center,
	Right,
};

UENUM(BlueprintType)
enum class EDreamPanelVerticalAlignment : uint8
{
	Fill,
	Top,
	Center,
	Bottom,
};

UENUM(BlueprintType)
enum class EDreamPanelSizeRule : uint8
{
	Auto,
	Fill,
};

/** UMG-style per-child layout data owned by the child widget. */
UCLASS(BlueprintType, DefaultToInstanced, EditInlineNew, DisplayName = "Panel Slot")
class DREAMGUI_API UDreamPanelSlot : public UDreamWidgetSubObjectBehaviour
{
	GENERATED_BODY()

private:
	/** Authored rect preserved independently from the serialized rect currently produced by a panel pass. */
	UPROPERTY()
	FDreamUIAnchorData AuthoredAnchorData;
	/** Actual authored size is stored separately because stretched anchors depend on the parent size. */
	UPROPERTY()
	FVector2f AuthoredDesiredSizeFallback = FVector2f::ZeroVector;
	UPROPERTY()
	bool bHasAuthoredGeometry = false;
	/** Persists so an asset reloaded after Apply still knows that AnchorData contains arranged geometry. */
	UPROPERTY()
	bool bLayoutGeometryApplied = false;
	/** Axes touched by the active layout pass. Zero on an applied legacy slot means all axes. */
	UPROPERTY()
	uint8 LayoutGeometryControlMask = 0;
	/** Swaps arranged geometry out of the persistent fields around prefab serialization. */
	friend class FDreamUIAuthoredGeometrySaveScope;

protected:
	virtual void OnRegister() override;

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetPadding, Category = "Slot")
	FMargin Padding;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetHorizontalAlignment, Category = "Slot")
	EDreamPanelHorizontalAlignment HorizontalAlignment = EDreamPanelHorizontalAlignment::Fill;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetVerticalAlignment, Category = "Slot")
	EDreamPanelVerticalAlignment VerticalAlignment = EDreamPanelVerticalAlignment::Fill;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetSizeRule, Category = "Slot")
	EDreamPanelSizeRule SizeRule = EDreamPanelSizeRule::Auto;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetFillWeight, Category = "Slot", meta = (ClampMin = "0.0"))
	float FillWeight = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetRow, Category = "Slot", meta = (ClampMin = "0"))
	int32 Row = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetColumn, Category = "Slot", meta = (ClampMin = "0"))
	int32 Column = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetRowSpan, Category = "Slot", meta = (ClampMin = "1"))
	int32 RowSpan = 1;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetColumnSpan, Category = "Slot", meta = (ClampMin = "1"))
	int32 ColumnSpan = 1;
	/**
	 * Paint order among siblings, applied as a stable reorder (equal values keep sibling order).
	 * Consumed by Overlay and GridPanel always, and by CanvasPanel when SortChildrenByZOrder is on;
	 * every other panel arranges strictly by sibling order and ignores this value.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetZOrder, Category = "Slot")
	int32 ZOrder = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetAutoSize, Category = "Slot")
	bool bAutoSize = false;
	/**
	 * Floor and ceiling applied to whatever this child measures, per axis, zero meaning "no opinion".
	 *
	 * SizeBox has carried MinDesiredSize/MaxDesiredSize since the beginning, but SizeBox takes one
	 * child, so constraining one item of a StackBox or one cell of a GridPanel meant wrapping it in a
	 * whole extra panel. These say the same thing on the slot, where UMG's own per-slot minimums live.
	 * Applied at the single point every panel measures through (UDreamPanelLayoutBase::GetDesiredSize),
	 * so no panel needs to know about them and none can forget: min wins over max when they cross,
	 * matching SizeBox, and neither one forces a size the way SizeBox's overrides do -- they bound the
	 * measurement, and the panel's own alignment and fill rules then do what they always did.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetMinDesiredSize, Category = "Slot", meta = (ClampMin = "0.0"))
	FVector2D MinDesiredSize = FVector2D::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetMaxDesiredSize, Category = "Slot", meta = (ClampMin = "0.0"))
	FVector2D MaxDesiredSize = FVector2D::ZeroVector;
	/**
	 * WrapBox only, matching UMG's UWrapBoxSlot: share out whatever room is left over on this child's
	 * line among the children on it that asked for it. Read by no other panel.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetFillEmptySpace, Category = "Slot")
	bool bFillEmptySpace = false;
	/**
	 * WrapBox only, matching UMG's UWrapBoxSlot: when the box's wrap width drops below this, give this
	 * child a line to itself. Zero disables it. The classic use is a responsive list of cards that stops
	 * sharing rows once the panel is narrow enough that sharing would make everything unreadable.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetFillSpanWhenLessThan, Category = "Slot", meta = (ClampMin = "0.0"))
	float FillSpanWhenLessThan = 0.0f;
	/**
	 * WrapBox only, matching UMG's UWrapBoxSlot::bForceNewLine: begin a line at this child however much
	 * room is left on the current one. The unconditional twin of FillSpanWhenLessThan, which only breaks
	 * once the box is narrow; this is how a section heading stays at the head of its own row.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetNewLine, Category = "Slot")
	bool bForceNewLine = false;
	/**
	 * A fixed offset added to wherever the panel decided to put this child, in the panel's content space
	 * (x right, y down -- the same sense as Padding).
	 *
	 * UMG carries this on UGridSlot alone. Here it is applied at the single point every panel places a
	 * child through (UDreamPanelLayoutBase::ApplyChildRect), so a stack, a wrap box and an overlay all
	 * honour it without knowing it exists. It is deliberately invisible to MEASUREMENT: nudging a child
	 * must not resize the panel around it, which is precisely why UMG calls it a nudge and not a position.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetNudge, Category = "Slot")
	FVector2D Nudge = FVector2D::ZeroVector;

	UFUNCTION(BlueprintSetter) void SetPadding(FMargin Value);
	UFUNCTION(BlueprintSetter) void SetHorizontalAlignment(EDreamPanelHorizontalAlignment Value);
	UFUNCTION(BlueprintSetter) void SetVerticalAlignment(EDreamPanelVerticalAlignment Value);
	UFUNCTION(BlueprintSetter) void SetSizeRule(EDreamPanelSizeRule Value);
	UFUNCTION(BlueprintSetter) void SetFillWeight(float Value);
	UFUNCTION(BlueprintSetter) void SetRow(int32 Value);
	UFUNCTION(BlueprintSetter) void SetColumn(int32 Value);
	UFUNCTION(BlueprintSetter) void SetRowSpan(int32 Value);
	UFUNCTION(BlueprintSetter) void SetColumnSpan(int32 Value);
	UFUNCTION(BlueprintSetter) void SetZOrder(int32 Value);
	UFUNCTION(BlueprintSetter) void SetAutoSize(bool Value);
	UFUNCTION(BlueprintSetter) void SetMinDesiredSize(FVector2D Value);
	UFUNCTION(BlueprintSetter) void SetMaxDesiredSize(FVector2D Value);
	UFUNCTION(BlueprintSetter) void SetFillEmptySpace(bool Value);
	UFUNCTION(BlueprintSetter) void SetFillSpanWhenLessThan(float Value);
	UFUNCTION(BlueprintSetter) void SetNewLine(bool Value);
	UFUNCTION(BlueprintSetter) void SetNudge(FVector2D Value);

	/**
	 * UMG's UGridSlot::Layer, which is this slot's ZOrder under its UMG name rather than a second number.
	 *
	 * A grid's layer and a slot's z-order state the same fact -- which children of the panel draw over
	 * which -- and UDreamLayoutContainerGridPanel already reorders its children by ZOrder, stably. Two
	 * stored numbers for one fact would only raise the question of which one wins.
	 */
	UFUNCTION(BlueprintPure, Category = "Slot")
	int32 GetLayer() const { return ZOrder; }
	UFUNCTION(BlueprintCallable, Category = "Slot")
	void SetLayer(int32 InLayer) { SetZOrder(InLayer); }
	UFUNCTION(BlueprintPure, Category = "Slot")
	int32 GetZOrder() const { return ZOrder; }
	UFUNCTION(BlueprintPure, Category = "Slot")
	bool GetAutoSize() const { return bAutoSize; }

	/**
	 * UMG's UCanvasPanelSlot rect family, reading and writing the widget's own FDreamUIAnchorData.
	 *
	 * A canvas child here states its rect through its anchors, exactly as one does in UMG through
	 * FAnchorData -- so these are a second NAME for that data, never a second copy of it. Everything
	 * below goes straight to the widget's setters, which raise the right invalidation and keep the
	 * authored-geometry snapshot in step; a slot that stored its own position would be the one thing
	 * guaranteed to disagree with the transform shown in the details panel.
	 *
	 * The axis convention is this plugin's throughout: y points UP, anchors are measured from the
	 * bottom, and Alignment IS the pivot. UMG measures y downwards, so a literal port would have to
	 * negate it -- and then SetPosition and SetAnchoredPosition would describe the same widget in two
	 * opposite languages. The names and the shapes are UMG's; the axes are the ones the details panel,
	 * the .dui files and every other setter here already use.
	 *
	 * Offsets are the four edge insets from the anchor rect (UDreamWidget::GetAnchorOffset), which is
	 * UMG's meaning on a STRETCHED axis. On a point anchor UMG reuses Offsets.Right/Bottom to mean the
	 * size instead; that reading lives on GetSize/SetSize here, and Offsets stays insets on both.
	 */
	UFUNCTION(BlueprintPure, Category = "Slot|Canvas")
	FDreamUIAnchorData GetLayout() const;
	UFUNCTION(BlueprintCallable, Category = "Slot|Canvas")
	void SetLayout(const FDreamUIAnchorData& InLayout);
	/** The offset of the widget's alignment point from its anchor -- UMG's Position on a point anchor. */
	UFUNCTION(BlueprintPure, Category = "Slot|Canvas")
	FVector2D GetPosition() const;
	UFUNCTION(BlueprintCallable, Category = "Slot|Canvas")
	void SetPosition(FVector2D InPosition);
	/** The size a point anchor resolves to, and the size BEYOND the anchor span on a stretched one. */
	UFUNCTION(BlueprintPure, Category = "Slot|Canvas")
	FVector2D GetSize() const;
	UFUNCTION(BlueprintCallable, Category = "Slot|Canvas")
	void SetSize(FVector2D InSize);
	UFUNCTION(BlueprintPure, Category = "Slot|Canvas")
	FMargin GetOffsets() const;
	UFUNCTION(BlueprintCallable, Category = "Slot|Canvas")
	void SetOffsets(FMargin InOffsets);
	UFUNCTION(BlueprintPure, Category = "Slot|Canvas")
	FAnchors GetAnchors() const;
	UFUNCTION(BlueprintCallable, Category = "Slot|Canvas")
	void SetAnchors(FAnchors InAnchors);
	UFUNCTION(BlueprintCallable, Category = "Slot|Canvas")
	void SetMinimum(FVector2D InMinimumAnchors);
	UFUNCTION(BlueprintCallable, Category = "Slot|Canvas")
	void SetMaximum(FVector2D InMaximumAnchors);
	/** UMG's Alignment: where inside its own rect the widget's position refers to. This plugin's Pivot. */
	UFUNCTION(BlueprintPure, Category = "Slot|Canvas")
	FVector2D GetAlignment() const;
	UFUNCTION(BlueprintCallable, Category = "Slot|Canvas")
	void SetAlignment(FVector2D InAlignment);

	/** Apply this slot's Min/Max to a measured size. Zero on an axis means that bound is not set. */
	FVector2D ConstrainDesiredSize(const FVector2D& InDesiredSize) const;
	UFUNCTION(BlueprintCallable, Category = "Slot")
	/**
	 * Reason defaults to Measure, the safe answer. Alignment, size rule, fill weight and z-order pass
	 * Arrange instead: none of them appear in any panel's MeasureLayout - checked one by one - so they
	 * cannot move a preferred size, only where this slot's widget ends up inside its own parent.
	 * Padding, the grid coordinates and bAutoSize all do appear there, and keep the default.
	 */
	void NotifySlotChanged(EDreamLayoutInvalidation Reason);

	void CaptureAuthoredGeometry(bool bForce = false);
	bool RestoreAuthoredGeometry(bool bForce = false);
	void InvalidateAuthoredGeometry();
	void MarkLayoutGeometryApplied(bool bHorizontalPosition = true, bool bVerticalPosition = true,
		bool bHorizontalSize = true, bool bVerticalSize = true);
	bool HasAuthoredGeometry() const { return bHasAuthoredGeometry; }
	bool HasLayoutGeometryApplied() const { return bLayoutGeometryApplied; }
	FVector2f GetAuthoredDesiredSizeFallback() const { return AuthoredDesiredSizeFallback; }
	/**
	 * The widget's size just changed outside a layout pass, so that size is the new authored intent and
	 * measurement has to follow it. Updates only the desired-size fallback: AuthoredAnchorData is the
	 * restore target, and the widget's current anchors may still be holding layout output.
	 */
	void SyncAuthoredDesiredSizeFromWidget();
	uint8 GetLayoutGeometryControlMask() const { return LayoutGeometryControlMask; }
	/**
	 * Merge the authored values over Current on every axis the active layout pass controls (all axes when
	 * the mask is empty). Only meaningful while HasAuthoredGeometry(); pure query, applies nothing.
	 */
	FDreamUIAnchorData ComposeAuthoredAnchorData(const FDreamUIAnchorData& Current) const;

#if WITH_EDITOR
	/** Keep the cached authored rect in sync after a user edits the widget transform. */
	void SyncAuthoredGeometryAfterUserEdit();
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditUndo() override;
#endif
};
