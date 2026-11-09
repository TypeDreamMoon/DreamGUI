// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"

/** A point of a curve the editor edits: X and Y both 0..1, and whether the curve bends at it. */
struct FDreamCurvePoint
{
	float X = 0.0f;
	float Y = 0.0f;
	bool bCorner = false;

	FDreamCurvePoint() = default;
	FDreamCurvePoint(float InX, float InY, bool bInCorner = false) : X(InX), Y(InY), bCorner(bInCorner) {}
	bool operator==(const FDreamCurvePoint& Other) const { return X == Other.X && Y == Other.Y && bCorner == Other.bCorner; }
};

/**
 * A curve from 0 to 1 across and up, drawn in a box with a grid: a contour, an ease, any falloff. It draws whatever its
 * Evaluate gives, and when it is editable, Points are its handles:
 *
 * - drag a point to move it, between its neighbours -- the first and last stay at 0 and 1 across;
 * - click empty space to add a point there and drag it at once;
 * - drag a point well out of the box and let go to remove it (never the first or the last);
 * - Alt+click or double-click a point to make it a corner, or smooth again.
 *
 * It holds no curve of its own: every change is reported, and what binds it decides what the curve is.
 */
class DREAMGUIEDITOR_API SDreamCurveEditor : public SLeafWidget
{
public:
	DECLARE_DELEGATE_TwoParams(FOnPointsChanged, const TArray<FDreamCurvePoint>& /*Points*/, bool /*bInteractive*/);

	SLATE_BEGIN_ARGS(SDreamCurveEditor)
		: _DesiredSize(FVector2D(160.0, 160.0))
		, _bEditable(false)
		, _bShowGrid(true)
	{}
		SLATE_ARGUMENT(FVector2D, DesiredSize)
		SLATE_ARGUMENT(bool, bEditable)
		SLATE_ARGUMENT(bool, bShowGrid)
		/** The curve, 0..1 in and out; what is drawn. */
		SLATE_ARGUMENT(TFunction<float(float)>, Evaluate)
		/** The handles, by X; only drawn and edited when editable. */
		SLATE_ATTRIBUTE(TArray<FDreamCurvePoint>, Points)
		SLATE_EVENT(FSimpleDelegate, OnBeginEdit)
		SLATE_EVENT(FOnPointsChanged, OnPointsChanged)
		SLATE_EVENT(FSimpleDelegate, OnEndEdit)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	void SetEvaluate(TFunction<float(float)> InEvaluate) { Evaluate = MoveTemp(InEvaluate); }

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float) const override { return DesiredSize; }
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

	/** The edits a pointer makes, apart from Slate, so that they can be held to: each returns the points after it. */
	static TArray<FDreamCurvePoint> AddPoint(const TArray<FDreamCurvePoint>& InPoints, FVector2f InAt, int32& OutIndex);
	static TArray<FDreamCurvePoint> MovePoint(const TArray<FDreamCurvePoint>& InPoints, int32 InIndex, FVector2f InTo);
	static TArray<FDreamCurvePoint> RemovePoint(const TArray<FDreamCurvePoint>& InPoints, int32 InIndex);
	static TArray<FDreamCurvePoint> ToggleCorner(const TArray<FDreamCurvePoint>& InPoints, int32 InIndex);

private:
	/** The box the curve is drawn in, inside a margin for the handles. */
	FSlateRect CurveRect(const FGeometry& InGeometry) const;
	FVector2f ToCurve(const FGeometry& InGeometry, const FVector2D& InScreen) const;
	FVector2D ToLocal(const FGeometry& InGeometry, const FDreamCurvePoint& InPoint) const;
	int32 HitPoint(const FGeometry& InGeometry, const FVector2D& InScreen) const;
	void Report(const TArray<FDreamCurvePoint>& InPoints, bool bInteractive);

	FVector2D DesiredSize;
	bool bEditable = false;
	bool bShowGrid = true;
	TFunction<float(float)> Evaluate;
	TAttribute<TArray<FDreamCurvePoint>> Points;
	FSimpleDelegate OnBeginEdit;
	FOnPointsChanged OnPointsChanged;
	FSimpleDelegate OnEndEdit;

	int32 Dragging = INDEX_NONE;
	bool bOutside = false;
	TArray<FDreamCurvePoint> Working;
};
