// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UDreamWidget;

/**
 * How a measurement constraint should be read, in the sense Android's MeasureSpec and Yoga's
 * YGMeasureMode use: the number alone does not say whether it is a ceiling or an instruction.
 *
 * Undefined - no constraint, report the natural size.
 * AtMost    - report the natural size, but no larger than Value.
 * Exactly   - the answer is Value regardless of content.
 */
enum class EDreamMeasureMode : uint8
{
	Undefined,
	AtMost,
	Exactly,
};

/**
 * One axis of a measurement request.
 *
 * The layout that measures without one of these cannot answer questions whose answer depends on the
 * space available - "how tall is this wrapping text at 300 wide" being the one that matters most. Slate
 * has no equivalent and pays for it with a frame of latency (STextBlock::OnPaint re-wraps and then
 * invalidates); DreamGUI had no equivalent and paid for it by iterating until the numbers stopped moving.
 */
struct FDreamMeasureSpec
{
	float Value = 0.0f;
	EDreamMeasureMode Mode = EDreamMeasureMode::Undefined;

	FDreamMeasureSpec() = default;
	FDreamMeasureSpec(float InValue, EDreamMeasureMode InMode) : Value(InValue), Mode(InMode) {}

	static FDreamMeasureSpec Undefined() { return FDreamMeasureSpec(0.0f, EDreamMeasureMode::Undefined); }
	static FDreamMeasureSpec AtMost(float InValue) { return FDreamMeasureSpec(InValue, EDreamMeasureMode::AtMost); }
	static FDreamMeasureSpec Exactly(float InValue) { return FDreamMeasureSpec(InValue, EDreamMeasureMode::Exactly); }

	/** Apply this constraint to a natural content size. */
	float Resolve(float ContentSize) const
	{
		switch (Mode)
		{
		case EDreamMeasureMode::Exactly: return FMath::Max(0.0f, Value);
		case EDreamMeasureMode::AtMost:  return FMath::Clamp(ContentSize, 0.0f, FMath::Max(0.0f, Value));
		default:                       return FMath::Max(0.0f, ContentSize);
		}
	}

	bool operator==(const FDreamMeasureSpec& Other) const
	{
		if (Mode != Other.Mode)
		{
			return false;
		}
		return Mode == EDreamMeasureMode::Undefined || FMath::IsNearlyEqual(Value, Other.Value, 0.001f);
	}
	bool operator!=(const FDreamMeasureSpec& Other) const { return !(*this == Other); }
};

/**
 * One child's arranged rect, as the panel decided it, before any of it reaches the widget.
 *
 * Child is a bare pointer on purpose: a fragment never outlives the arrange/commit pair that produced
 * it, so there is nothing for the GC to keep alive.
 */
struct FDreamPanelChildRect
{
	UDreamWidget* Child = nullptr;
	FVector2D AnchoredPosition = FVector2D::ZeroVector;
	FVector2f Size = FVector2f::ZeroVector;
	/**
	 * ScaleBox is the only panel that scales what it arranges. Carried behind a flag rather than inferred
	 * from "is it unit": a ScaleBox set to None writes a unit scale deliberately, and skipping that write
	 * would strand whatever scale a previous stretch mode left behind.
	 */
	bool bApplyScale = false;
	FVector2f LayoutScale = FVector2f::UnitVector;
	/** Collapse the child's anchors to centre before writing. CanvasPanel's auto-size path does not. */
	bool bCollapseAnchors = true;
	/** Size only; the child keeps the anchored position its own anchor data produced. */
	bool bSizeOnly = false;
};

/**
 * What one panel's arrange pass decided: its own size, and a rect per participating child.
 *
 * The point of the type is that it exists at all. A layout result used to have nowhere to live except
 * the widgets themselves, so arranging *was* writing, and every write re-entered invalidation while
 * the pass that caused it was still running. Blink hit the same wall and answered it the same way -
 * NGLayoutAlgorithm returns an immutable NGPhysicalFragment rather than mutating the LayoutObject
 * tree in place.
 */
struct FDreamFragment
{
	FVector2f Size = FVector2f::ZeroVector;
	TArray<FDreamPanelChildRect> Children;

	void Reset()
	{
		Size = FVector2f::ZeroVector;
		Children.Reset();
	}
};
