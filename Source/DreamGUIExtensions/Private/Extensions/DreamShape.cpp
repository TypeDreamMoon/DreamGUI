// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Extensions/DreamShape.h"
#include "DreamGUI.h"
#include "Core/DreamUIGeometry.h"
#include "Core/DreamUISpriteData_BaseObject.h"
#include "Core/DreamUIWidgetRegistry.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamWidget.h"
#include "DreamTweenManager.h"

namespace DreamShapeLocal
{
	/** Twice the signed area: positive when the outline runs counter-clockwise in the y-up rect space. */
	double SignedArea2(TConstArrayView<FVector2D> P)
	{
		double Area = 0.0;
		for (int32 I = 0, N = P.Num(); I < N; ++I)
		{
			const FVector2D& A = P[I];
			const FVector2D& B = P[(I + 1) % N];
			Area += A.X * B.Y - B.X * A.Y;
		}
		return Area;
	}

	double Cross(const FVector2D& O, const FVector2D& A, const FVector2D& B)
	{
		return (A.X - O.X) * (B.Y - O.Y) - (A.Y - O.Y) * (B.X - O.X);
	}

	/** Strictly inside the counter-clockwise triangle ABC: a point on an edge or a corner does not block an ear. */
	bool StrictlyInside(const FVector2D& P, const FVector2D& A, const FVector2D& B, const FVector2D& C, double Eps)
	{
		return Cross(A, B, P) > Eps && Cross(B, C, P) > Eps && Cross(C, A, P) > Eps;
	}
}

UDreamShape::UDreamShape(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	// A right triangle until told otherwise, so a Shape dropped in with no points still shows where it is.
	Points = { FVector2D(0.0, 0.0), FVector2D(1.0, 0.0), FVector2D(0.0, 1.0) };
}

bool UDreamShape::Triangulate(TConstArrayView<FVector2D> InOutline, TArray<int32>& OutIndices)
{
	using namespace DreamShapeLocal;

	const int32 N = InOutline.Num();
	if (N < 3)
	{
		return false;
	}
	const double Area2 = SignedArea2(InOutline);
	// Scale-aware tolerance: the same outline must fill whether it is written in 0..1 or in pixels.
	double Extent = 0.0;
	for (const FVector2D& P : InOutline)
	{
		Extent = FMath::Max(Extent, FMath::Max(FMath::Abs(P.X), FMath::Abs(P.Y)));
	}
	const double Eps = FMath::Max(Extent * Extent, 1e-12) * 1e-9;
	if (FMath::Abs(Area2) <= Eps)
	{
		return false;
	}

	// Work counter-clockwise whatever order the author wrote, so every emitted triangle has the same winding as the
	// fans Polygon draws.
	TArray<int32> Ring;
	Ring.Reserve(N);
	for (int32 I = 0; I < N; ++I)
	{
		Ring.Add(Area2 > 0.0 ? I : N - 1 - I);
	}

	const int32 Start = OutIndices.Num();
	while (Ring.Num() > 3)
	{
		const int32 Count = Ring.Num();
		bool bClipped = false;
		for (int32 K = 0; K < Count && !bClipped; ++K)
		{
			const int32 A = Ring[(K + Count - 1) % Count];
			const int32 B = Ring[K];
			const int32 C = Ring[(K + 1) % Count];
			if (Cross(InOutline[A], InOutline[B], InOutline[C]) <= Eps)
			{
				continue; // reflex or flat corner: not an ear
			}
			bool bEmpty = true;
			for (const int32 M : Ring)
			{
				if (M != A && M != B && M != C && StrictlyInside(InOutline[M], InOutline[A], InOutline[B], InOutline[C], Eps))
				{
					bEmpty = false;
					break;
				}
			}
			if (bEmpty)
			{
				OutIndices.Append({ A, B, C });
				Ring.RemoveAt(K);
				bClipped = true;
			}
		}
		if (!bClipped)
		{
			// No ear left. A flat corner (three points on a line) is not one but adds no area either, so drop it and
			// carry on; anything else means the outline crosses itself.
			bool bDropped = false;
			for (int32 K = 0; K < Count; ++K)
			{
				const int32 A = Ring[(K + Count - 1) % Count];
				const int32 B = Ring[K];
				const int32 C = Ring[(K + 1) % Count];
				if (FMath::Abs(Cross(InOutline[A], InOutline[B], InOutline[C])) <= Eps)
				{
					Ring.RemoveAt(K);
					bDropped = true;
					break;
				}
			}
			if (!bDropped)
			{
				OutIndices.SetNum(Start);
				return false;
			}
		}
	}
	if (Cross(InOutline[Ring[0]], InOutline[Ring[1]], InOutline[Ring[2]]) > Eps)
	{
		OutIndices.Append({ Ring[0], Ring[1], Ring[2] });
	}
	return OutIndices.Num() > Start;
}

void UDreamShape::OnUpdateGeometry(FDreamUIGeometry& InGeo, bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged)
{
	UDreamWidget* Widget = GetWidget();
	const int32 PointCount = Points.Num();
	// Fewer than three points still gets three (collapsed) vertices and one empty triangle: the batch expects a mesh.
	const int32 VertexCount = FMath::Max(PointCount, 3);

	if (InTriangleChanged || BuiltPointCount != PointCount)
	{
		CachedIndices.Reset();
		if (PointCount >= 3 && !Triangulate(Points, CachedIndices))
		{
			// Self-intersecting: fill it as a fan so the author sees something to fix rather than nothing.
			UE_LOG(DreamGUI, Warning, TEXT("[%s].%d '%s': the shape's outline crosses itself; drawn as a fan."),
				ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *GetNameSafe(Widget));
			for (int32 I = 1; I + 1 < PointCount; ++I)
			{
				CachedIndices.Append({ 0, I, I + 1 });
			}
		}
		if (CachedIndices.IsEmpty())
		{
			CachedIndices = { 0, 1, 2 };
		}
		BuiltPointCount = PointCount;
		InTriangleChanged = true;
		InVertexPositionChanged = InVertexUVChanged = InVertexColorChanged = true;
	}

	auto& Triangles = InGeo.Triangles;
	FDreamUIGeometry::DreamUIGeometrySetArrayNum(Triangles, CachedIndices.Num());
	if (InTriangleChanged)
	{
		for (int32 I = 0; I < CachedIndices.Num(); ++I)
		{
			Triangles[I] = CachedIndices[I];
		}
	}

	auto& Vertices = InGeo.Vertices;
	auto& OriginVertices = InGeo.OriginVertices;
	FDreamUIGeometry::DreamUIGeometrySetArrayNum(Vertices, VertexCount);
	FDreamUIGeometry::DreamUIGeometrySetArrayNum(OriginVertices, VertexCount);
	if (!(InVertexUVChanged || InVertexPositionChanged || InVertexColorChanged))
	{
		return;
	}

	const float Width = Widget != nullptr ? Widget->GetWidth() : 0.0f;
	const float Height = Widget != nullptr ? Widget->GetHeight() : 0.0f;
	// Each point as a 0..1 position in the rect, which both the position and the UV are read from.
	auto RectFraction = [this, Width, Height](int32 Index) -> FVector2f
	{
		if (!Points.IsValidIndex(Index))
		{
			return FVector2f::ZeroVector;
		}
		const FVector2D& P = Points[Index];
		if (PointUnit == EDreamShapePointUnit::Pixels)
		{
			return FVector2f(Width > 0.0f ? P.X / Width : 0.0f, Height > 0.0f ? P.Y / Height : 0.0f);
		}
		return FVector2f(P);
	};

	if (InVertexPositionChanged)
	{
		const FVector2f Pivot = Widget != nullptr ? FVector2f(Widget->GetPivot()) : FVector2f(0.5f, 0.5f);
		float PivotOffsetX = 0.0f, PivotOffsetY = 0.0f;
		FDreamUIGeometry::CalculatePivotOffset(Width, Height, Pivot, PivotOffsetX, PivotOffsetY);
		for (int32 I = 0; I < VertexCount; ++I)
		{
			const FVector2f F = RectFraction(I);
			const float X = PivotOffsetX + (F.X - 0.5f) * Width;
			const float Y = PivotOffsetY + (F.Y - 0.5f) * Height;
			OriginVertices[I].Position = FVector3f(0, X, Y);
		}
	}

	if (InVertexUVChanged)
	{
		// The same convention Polygon uses: MinUV.Y is the rect's top, MaxUV.Y its bottom.
		FVector2f MinUV, MaxUV;
		if (bHasAddToSprite)
		{
			auto* Sprite = (UDreamUISpriteData_BaseObject*)Brush.GetResourceObject();
			const auto& SpriteInfo = Sprite->GetSpriteInfo();
			MinUV = FVector2f(SpriteInfo.MinUV.X, SpriteInfo.MaxUV.Y);
			MaxUV = FVector2f(SpriteInfo.MaxUV.X, SpriteInfo.MinUV.Y);
		}
		else
		{
			MinUV = FVector2f(Brush.UVRegion.X, Brush.UVRegion.Y);
			MaxUV = FVector2f(Brush.UVRegion.Z, Brush.UVRegion.W);
		}
		for (int32 I = 0; I < VertexCount; ++I)
		{
			const FVector2f F = RectFraction(I);
			Vertices[I].TextureCoordinate[0] = FVector2f(
				FMath::Lerp(MinUV.X, MaxUV.X, F.X),
				FMath::Lerp(MaxUV.Y, MinUV.Y, F.Y));
		}
	}

	if (InVertexColorChanged)
	{
		FDreamUIGeometry::UpdateUIColor(&InGeo, GetFinalColor());
	}

	const UDreamCanvas* RenderCanvas = Widget != nullptr ? Widget->GetRenderCanvas() : nullptr;
	if (RenderCanvas != nullptr && RenderCanvas->GetActualRequireNormalAndTangent())
	{
		for (int32 I = 0; I < VertexCount; ++I)
		{
			OriginVertices[I].Normal = FVector3f(-1, 0, 0);
			OriginVertices[I].Tangent = FVector3f(0, 1, 0);
		}
	}
}

void UDreamShape::SetPoints(const TArray<FVector2D>& InPoints)
{
	if (Points != InPoints)
	{
		const bool bCountChanged = Points.Num() != InPoints.Num();
		Points = InPoints;
		// A concave outline can need different triangles after a point moves, so the triangle list is always redone.
		MarkVerticesDirty(true, true, true, bCountChanged);
	}
}

void UDreamShape::SetPointUnit(EDreamShapePointUnit InUnit)
{
	if (PointUnit != InUnit)
	{
		PointUnit = InUnit;
		MarkVerticesDirty(false, true, true, false);
	}
}

void UDreamShape::SetMorphAlpha(float InAlpha)
{
	MorphAlpha = InAlpha;
	if (MorphFrom.Num() != MorphTo.Num())
	{
		return;
	}
	TArray<FVector2D> Blend;
	Blend.SetNumUninitialized(MorphTo.Num());
	for (int32 I = 0; I < MorphTo.Num(); ++I)
	{
		Blend[I] = FMath::Lerp(MorphFrom[I], MorphTo[I], static_cast<double>(InAlpha));
	}
	SetPoints(Blend);
}

UDreamTweener* UDreamShape::PointsTo(const TArray<FVector2D>& InPoints, float Duration, float Delay, EDreamTweenEase EaseType)
{
	if (InPoints.Num() != Points.Num() || Duration <= 0.0f)
	{
		SetPoints(InPoints);
		return nullptr;
	}
	MorphFrom = Points;
	MorphTo = InPoints;
	MorphAlpha = 0.0f;
	UDreamTweener* Tweener = UDreamTweenManager::To(this,
		FDreamTweenFloatGetterFunction::CreateWeakLambda(this, [this]() { return MorphAlpha; }),
		FDreamTweenFloatSetterFunction::CreateWeakLambda(this, [this](float Value) { SetMorphAlpha(Value); }),
		1.0f, Duration);
	if (Tweener != nullptr)
	{
		Tweener->SetEase(EaseType)->SetDelay(Delay);
		UDreamWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(GetWidget(), Tweener);
	}
	return Tweener;
}

/* Like Polygon: the outline is placed in whatever rect the shape is given, so it has no size of its own to report. */
float UDreamShape::GetPreferredWidth() const
{
	return -1;
}

float UDreamShape::GetPreferredHeight() const
{
	return -1;
}

DECLARE_DREAM_GUI_VISUAL("Shape", UDreamShape)
