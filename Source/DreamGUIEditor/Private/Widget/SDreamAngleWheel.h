// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"

/** Which way an angle runs, and where its zero is. */
enum class EDreamAngleConvention : uint8
{
	/** 0 at three o'clock, counter-clockwise: an image editor's light and gradient angles. */
	RightCounterClockwise,
	/** 0 at three o'clock, clockwise: a rotation in screen space, +Y down. */
	RightClockwise,
	/** 0 at twelve o'clock, clockwise: a clock's. */
	TopClockwise,
};

namespace DreamAngleWheel
{
	/** The convention named after DreamGUI_Editor="AngleWheel:": "" or "CCW", "CW", "TopCW". */
	DREAMGUIEDITOR_API EDreamAngleConvention ParseConvention(const FString& InMeta);
	/** The angle, degrees, of a point InOffset from the wheel's centre (screen space, +Y down). */
	DREAMGUIEDITOR_API float AngleOf(const FVector2D& InOffset, EDreamAngleConvention InConvention);
	/** The unit direction, screen space, of InDegrees. */
	DREAMGUIEDITOR_API FVector2D DirectionOf(float InDegrees, EDreamAngleConvention InConvention);
	/** InDegrees in (-180, 180] when bInSigned -- as DreamLayerStyleLight keeps a light's angle -- else in [0, 360). */
	DREAMGUIEDITOR_API float Wrap(float InDegrees, bool bInSigned);
}

/**
 * A small dial for an angle: a circle, a hand from its centre and a knob at the hand's end. Dragging anywhere on it turns
 * the hand to the pointer; Shift snaps to 15 degrees. It only draws and reports -- the value lives in whatever binds it.
 */
class DREAMGUIEDITOR_API SDreamAngleWheel : public SLeafWidget
{
public:
	DECLARE_DELEGATE_OneParam(FOnAngle, float);

	SLATE_BEGIN_ARGS(SDreamAngleWheel)
		: _Diameter(30.0f)
		, _Convention(EDreamAngleConvention::RightCounterClockwise)
		, _bSigned(true)
	{}
		/** Degrees; unset draws no hand (several values). */
		SLATE_ATTRIBUTE(TOptional<float>, Angle)
		SLATE_ARGUMENT(float, Diameter)
		SLATE_ARGUMENT(EDreamAngleConvention, Convention)
		/** Report angles in [-180, 180) rather than [0, 360). */
		SLATE_ARGUMENT(bool, bSigned)
		SLATE_EVENT(FSimpleDelegate, OnBeginDrag)
		SLATE_EVENT(FOnAngle, OnAngleChanged)
		SLATE_EVENT(FOnAngle, OnEndDrag)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float) const override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FCursorReply OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const override;

	/** The angle a pointer at InScreenPosition gives, as a drag reports it (no snapping). */
	float AngleAt(const FGeometry& InGeometry, const FVector2D& InScreenPosition) const;

private:
	float AngleFromEvent(const FGeometry& InGeometry, const FPointerEvent& InEvent) const;

	TAttribute<TOptional<float>> Angle;
	float Diameter = 30.0f;
	EDreamAngleConvention Convention = EDreamAngleConvention::RightCounterClockwise;
	bool bSigned = true;
	FSimpleDelegate OnBeginDrag;
	FOnAngle OnAngleChanged;
	FOnAngle OnEndDrag;
	bool bDragging = false;
	float LastAngle = 0.0f;
};
