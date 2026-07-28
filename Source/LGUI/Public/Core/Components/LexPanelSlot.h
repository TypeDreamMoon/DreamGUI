// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/LexUIAnchorData.h"
#include "LexWidgetSubObjectBehaviour.h"
#include "LexPanelSlot.generated.h"

UENUM(BlueprintType)
enum class ELexPanelHorizontalAlignment : uint8
{
	Fill,
	Left,
	Center,
	Right,
};

UENUM(BlueprintType)
enum class ELexPanelVerticalAlignment : uint8
{
	Fill,
	Top,
	Center,
	Bottom,
};

UENUM(BlueprintType)
enum class ELexPanelSizeRule : uint8
{
	Auto,
	Fill,
};

/** UMG-style per-child layout data owned by the child widget. */
UCLASS(BlueprintType, DefaultToInstanced, EditInlineNew, DisplayName = "Panel Slot")
class LGUI_API ULexPanelSlot : public ULexWidgetSubObjectBehaviour
{
	GENERATED_BODY()

private:
	/** Authored rect preserved independently from the serialized rect currently produced by a panel pass. */
	UPROPERTY()
	FLexUIAnchorData AuthoredAnchorData;
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
	friend class FLexUIAuthoredGeometrySaveScope;

protected:
	virtual void OnRegister() override;

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetPadding, Category = "Slot")
	FMargin Padding;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetHorizontalAlignment, Category = "Slot")
	ELexPanelHorizontalAlignment HorizontalAlignment = ELexPanelHorizontalAlignment::Fill;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetVerticalAlignment, Category = "Slot")
	ELexPanelVerticalAlignment VerticalAlignment = ELexPanelVerticalAlignment::Fill;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetSizeRule, Category = "Slot")
	ELexPanelSizeRule SizeRule = ELexPanelSizeRule::Auto;
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
	UFUNCTION(BlueprintSetter) void SetHorizontalAlignment(ELexPanelHorizontalAlignment Value);
	UFUNCTION(BlueprintSetter) void SetVerticalAlignment(ELexPanelVerticalAlignment Value);
	UFUNCTION(BlueprintSetter) void SetSizeRule(ELexPanelSizeRule Value);
	UFUNCTION(BlueprintSetter) void SetFillWeight(float Value);
	UFUNCTION(BlueprintSetter) void SetRow(int32 Value);
	UFUNCTION(BlueprintSetter) void SetColumn(int32 Value);
	UFUNCTION(BlueprintSetter) void SetRowSpan(int32 Value);
	UFUNCTION(BlueprintSetter) void SetColumnSpan(int32 Value);
	UFUNCTION(BlueprintSetter) void SetZOrder(int32 Value);
	UFUNCTION(BlueprintSetter) void SetAutoSize(bool Value);
	UFUNCTION(BlueprintCallable, Category = "Slot")
	void NotifySlotChanged();

	void CaptureAuthoredGeometry(bool bForce = false);
	bool RestoreAuthoredGeometry(bool bForce = false);
	void InvalidateAuthoredGeometry();
	void MarkLayoutGeometryApplied(bool bHorizontalPosition = true, bool bVerticalPosition = true,
		bool bHorizontalSize = true, bool bVerticalSize = true);
	bool HasAuthoredGeometry() const { return bHasAuthoredGeometry; }
	bool HasLayoutGeometryApplied() const { return bLayoutGeometryApplied; }
	FVector2f GetAuthoredDesiredSizeFallback() const { return AuthoredDesiredSizeFallback; }
	uint8 GetLayoutGeometryControlMask() const { return LayoutGeometryControlMask; }
	/**
	 * Merge the authored values over Current on every axis the active layout pass controls (all axes when
	 * the mask is empty). Only meaningful while HasAuthoredGeometry(); pure query, applies nothing.
	 */
	FLexUIAnchorData ComposeAuthoredAnchorData(const FLexUIAnchorData& Current) const;

#if WITH_EDITOR
	/** Keep the cached authored rect in sync after a user edits the widget transform. */
	void SyncAuthoredGeometryAfterUserEdit();
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditUndo() override;
#endif
};
