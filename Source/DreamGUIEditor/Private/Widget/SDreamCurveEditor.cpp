// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Widget/SDreamCurveEditor.h"

#include "Rendering/DrawElements.h"
#include "Styling/AppStyle.h"

namespace DreamCurveEditorLocal
{
	/** Room round the box for a handle on its edge. */
	constexpr float Margin = 5.0f;
	/** How near a pointer has to come to a handle to take it. */
	constexpr float HitRadius = 6.0f;
	/** How far out of the box a dragged handle has to go to be removed when let go. */
	constexpr float RemoveDistance = 18.0f;
	/** The least room kept between neighbouring handles, across. */
	constexpr float Gap = 0.01f;
}

void SDreamCurveEditor::Construct(const FArguments& InArgs)
{
	DesiredSize = InArgs._DesiredSize;
	bEditable = InArgs._bEditable;
	bShowGrid = InArgs._bShowGrid;
	Evaluate = InArgs._Evaluate;
	Points = InArgs._Points;
	OnBeginEdit = InArgs._OnBeginEdit;
	OnPointsChanged = InArgs._OnPointsChanged;
	OnEndEdit = InArgs._OnEndEdit;
}

FSlateRect SDreamCurveEditor::CurveRect(const FGeometry& InGeometry) const
{
	const float M = bEditable ? DreamCurveEditorLocal::Margin : 1.0f;
	const FVector2D Size = InGeometry.GetLocalSize();
	return FSlateRect(M, M, FMath::Max(M + 1.0f, static_cast<float>(Size.X) - M), FMath::Max(M + 1.0f, static_cast<float>(Size.Y) - M));
}

FVector2f SDreamCurveEditor::ToCurve(const FGeometry& InGeometry, const FVector2D& InScreen) const
{
	const FSlateRect Rect = CurveRect(InGeometry);
	const FVector2D Local = InGeometry.AbsoluteToLocal(InScreen);
	return FVector2f(
		static_cast<float>((Local.X - Rect.Left) / FMath::Max(1.0, Rect.GetSize().X)),
		static_cast<float>(1.0 - (Local.Y - Rect.Top) / FMath::Max(1.0, Rect.GetSize().Y)));
}

FVector2D SDreamCurveEditor::ToLocal(const FGeometry& InGeometry, const FDreamCurvePoint& InPoint) const
{
	const FSlateRect Rect = CurveRect(InGeometry);
	return FVector2D(Rect.Left + InPoint.X * Rect.GetSize().X, Rect.Top + (1.0f - InPoint.Y) * Rect.GetSize().Y);
}

int32 SDreamCurveEditor::HitPoint(const FGeometry& InGeometry, const FVector2D& InScreen) const
{
	const FVector2D Local = InGeometry.AbsoluteToLocal(InScreen);
	const TArray<FDreamCurvePoint> Current = Points.Get();
	int32 Best = INDEX_NONE;
	double BestDistance = DreamCurveEditorLocal::HitRadius;
	for (int32 Index = 0; Index < Current.Num(); ++Index)
	{
		const double Distance = FVector2D::Distance(Local, ToLocal(InGeometry, Current[Index]));
		if (Distance <= BestDistance)
		{
			Best = Index;
			BestDistance = Distance;
		}
	}
	return Best;
}

int32 SDreamCurveEditor::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const bool bEnabled = ShouldBeEnabled(bParentEnabled);
	const FSlateRect Rect = CurveRect(AllottedGeometry);
	const FVector2D TopLeft(Rect.Left, Rect.Top);
	const FVector2D Size = Rect.GetSize();
	FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(Size, FSlateLayoutTransform(TopLeft)),
		FAppStyle::GetBrush("WhiteBrush"), ESlateDrawEffect::None, FLinearColor(0.0f, 0.0f, 0.0f, 0.35f));
	const FLinearColor Grid(1.0f, 1.0f, 1.0f, 0.08f);
	if (bShowGrid)
	{
		for (int32 Line = 1; Line < 4; ++Line)
		{
			const double T = Line / 4.0;
			FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 1, AllottedGeometry.ToPaintGeometry(),
				{ TopLeft + FVector2D(Size.X * T, 0.0), TopLeft + FVector2D(Size.X * T, Size.Y) }, ESlateDrawEffect::None, Grid);
			FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 1, AllottedGeometry.ToPaintGeometry(),
				{ TopLeft + FVector2D(0.0, Size.Y * T), TopLeft + FVector2D(Size.X, Size.Y * T) }, ESlateDrawEffect::None, Grid);
		}
	}
	const FLinearColor Line = bEnabled ? FLinearColor(0.95f, 0.95f, 0.95f, 1.0f) : FLinearColor(0.6f, 0.6f, 0.6f, 0.6f);
	if (Evaluate)
	{
		TArray<FVector2D> Curve;
		const int32 Samples = FMath::Clamp(static_cast<int32>(Size.X), 16, 160);
		for (int32 Index = 0; Index <= Samples; ++Index)
		{
			const float X = static_cast<float>(Index) / Samples;
			const float Y = FMath::Clamp(Evaluate(X), -0.25f, 1.25f);
			Curve.Add(TopLeft + FVector2D(X * Size.X, (1.0f - Y) * Size.Y));
		}
		FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 2, AllottedGeometry.ToPaintGeometry(), Curve, ESlateDrawEffect::None, Line, true, bEditable ? 1.5f : 1.0f);
	}
	if (bEditable)
	{
		const TArray<FDreamCurvePoint> Current = Points.Get();
		const FLinearColor Handle = FAppStyle::GetSlateColor("SelectionColor").GetSpecifiedColor();
		for (int32 Index = 0; Index < Current.Num(); ++Index)
		{
			const FVector2D At = ToLocal(AllottedGeometry, Current[Index]);
			const FVector2D Knob(7.0, 7.0);
			const bool bTaken = Index == Dragging;
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 3, AllottedGeometry.ToPaintGeometry(Knob, FSlateLayoutTransform(At - Knob * 0.5)),
				FAppStyle::GetBrush(Current[Index].bCorner ? "WhiteBrush" : "Icons.FilledCircle"), ESlateDrawEffect::None,
				bTaken && bOutside ? FLinearColor(1.0f, 0.3f, 0.3f, 0.8f) : bTaken ? Handle : Line);
		}
	}
	return LayerId + 3;
}

TArray<FDreamCurvePoint> SDreamCurveEditor::AddPoint(const TArray<FDreamCurvePoint>& InPoints, FVector2f InAt, int32& OutIndex)
{
	using namespace DreamCurveEditorLocal;
	TArray<FDreamCurvePoint> Out = InPoints;
	const float X = FMath::Clamp(InAt.X, Gap, 1.0f - Gap);
	const float Y = FMath::Clamp(InAt.Y, 0.0f, 1.0f);
	OutIndex = 0;
	while (OutIndex < Out.Num() && Out[OutIndex].X < X)
	{
		++OutIndex;
	}
	Out.Insert(FDreamCurvePoint(X, Y), OutIndex);
	return Out;
}

TArray<FDreamCurvePoint> SDreamCurveEditor::MovePoint(const TArray<FDreamCurvePoint>& InPoints, int32 InIndex, FVector2f InTo)
{
	using namespace DreamCurveEditorLocal;
	TArray<FDreamCurvePoint> Out = InPoints;
	if (!Out.IsValidIndex(InIndex))
	{
		return Out;
	}
	float X = InTo.X;
	if (InIndex == 0)
	{
		X = 0.0f;
	}
	else if (InIndex == Out.Num() - 1)
	{
		X = 1.0f;
	}
	else
	{
		X = FMath::Clamp(X, Out[InIndex - 1].X + Gap, Out[InIndex + 1].X - Gap);
	}
	Out[InIndex].X = X;
	Out[InIndex].Y = FMath::Clamp(InTo.Y, 0.0f, 1.0f);
	return Out;
}

TArray<FDreamCurvePoint> SDreamCurveEditor::RemovePoint(const TArray<FDreamCurvePoint>& InPoints, int32 InIndex)
{
	TArray<FDreamCurvePoint> Out = InPoints;
	if (InIndex > 0 && InIndex < Out.Num() - 1)
	{
		Out.RemoveAt(InIndex);
	}
	return Out;
}

TArray<FDreamCurvePoint> SDreamCurveEditor::ToggleCorner(const TArray<FDreamCurvePoint>& InPoints, int32 InIndex)
{
	TArray<FDreamCurvePoint> Out = InPoints;
	if (Out.IsValidIndex(InIndex))
	{
		Out[InIndex].bCorner = !Out[InIndex].bCorner;
	}
	return Out;
}

void SDreamCurveEditor::Report(const TArray<FDreamCurvePoint>& InPoints, bool bInteractive)
{
	OnPointsChanged.ExecuteIfBound(InPoints, bInteractive);
}

FReply SDreamCurveEditor::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!bEditable || !IsEnabled() || MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}
	TArray<FDreamCurvePoint> Current = Points.Get();
	int32 Index = HitPoint(MyGeometry, MouseEvent.GetScreenSpacePosition());
	if (Index != INDEX_NONE && MouseEvent.IsAltDown())
	{
		OnBeginEdit.ExecuteIfBound();
		Report(ToggleCorner(Current, Index), false);
		OnEndEdit.ExecuteIfBound();
		return FReply::Handled();
	}
	OnBeginEdit.ExecuteIfBound();
	if (Index == INDEX_NONE)
	{
		const FVector2f At = ToCurve(MyGeometry, MouseEvent.GetScreenSpacePosition());
		if (At.X < 0.0f || At.X > 1.0f || At.Y < 0.0f || At.Y > 1.0f)
		{
			OnEndEdit.ExecuteIfBound();
			return FReply::Unhandled();
		}
		Current = AddPoint(Current, At, Index);
		Report(Current, true);
	}
	Dragging = Index;
	bOutside = false;
	Working = Current;
	return FReply::Handled().CaptureMouse(SharedThis(this));
}

FReply SDreamCurveEditor::OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!bEditable || !IsEnabled())
	{
		return FReply::Unhandled();
	}
	const int32 Index = HitPoint(MyGeometry, MouseEvent.GetScreenSpacePosition());
	if (Index == INDEX_NONE)
	{
		return FReply::Unhandled();
	}
	OnBeginEdit.ExecuteIfBound();
	Report(ToggleCorner(Points.Get(), Index), false);
	OnEndEdit.ExecuteIfBound();
	return FReply::Handled();
}

FReply SDreamCurveEditor::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (Dragging == INDEX_NONE || !HasMouseCapture())
	{
		return FReply::Unhandled();
	}
	const FSlateRect Rect = CurveRect(MyGeometry);
	const FVector2D Local = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	const bool bEnd = Dragging == 0 || Dragging == Working.Num() - 1;
	bOutside = !bEnd && (Local.X < Rect.Left - DreamCurveEditorLocal::RemoveDistance || Local.X > Rect.Right + DreamCurveEditorLocal::RemoveDistance
		|| Local.Y < Rect.Top - DreamCurveEditorLocal::RemoveDistance || Local.Y > Rect.Bottom + DreamCurveEditorLocal::RemoveDistance);
	Working = MovePoint(Working, Dragging, ToCurve(MyGeometry, MouseEvent.GetScreenSpacePosition()));
	Report(bOutside ? RemovePoint(Working, Dragging) : Working, true);
	return FReply::Handled();
}

FReply SDreamCurveEditor::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (Dragging == INDEX_NONE)
	{
		return FReply::Unhandled();
	}
	Report(bOutside ? RemovePoint(Working, Dragging) : Working, false);
	Dragging = INDEX_NONE;
	bOutside = false;
	OnEndEdit.ExecuteIfBound();
	return FReply::Handled().ReleaseMouseCapture();
}
