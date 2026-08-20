// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DreamUIAnchorData.h"
#include "DreamWidgetSubObjectBehaviour.h"
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
