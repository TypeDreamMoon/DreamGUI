// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Extensions/2DLineRenderer/Dream2DLineRaw.h"
#include "DreamGUI.h"
#include "Core/DreamUIGeometry.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/DreamUIWidgetRegistry.h"

UDream2DLineRaw::UDream2DLineRaw(const FObjectInitializer& ObjectInitializer):Super(ObjectInitializer)
{
}

void UDream2DLineRaw::BeginPlay()
{
	Super::BeginPlay();
}

#if WITH_EDITOR
void UDream2DLineRaw::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

void UDream2DLineRaw::SetPoints(const TArray<FVector2D>& InPoints)
{
	if (InPoints.Num() != PointArray.Num())
	{
		PointArray = InPoints;
		MarkVerticesDirty(true, true, true, true);
	}
	else
	{
		PointArray = InPoints;
		MarkVertexPositionDirty();
	}
}

/*
 * The one line renderer that CAN answer, and the reason is worth naming: its points are authored,
 * in the widget's own local space, and nothing in CalculatePoints touches the rect -- the override
 * is empty, PointArray is simply what the author typed. Every sibling scales its points by
 * Widget->GetWidth(), which is what makes them unable to answer; this one is independent of the
 * rect, so the space its points occupy is genuine content and measuring it is not circular.
 *
 * The measurement is the EXTENT of the points, not their distance from the origin. A layout places
 * a rect and a pivot places the drawing inside it; those are separate decisions, and a measure that
 * folded the pivot in would be answering a question nobody asked. This matches how UDreamText
 * measures -- the width of the paragraph, not where the paragraph sits.
 *
 * The stroke is added because a line of width W straddles its path: LineWidthOffset slides the
 * split between the two sides but the total across the path is always W, so half on each side is
 * exact perpendicular to the path, and the cap quads at the ends are about that big too.
 */
FVector2f UDream2DLineRaw::MeasureStrokeBounds() const
{
	// One point draws nothing, and no points draw nothing: neither is a claim about size.
	if (PointArray.Num() < 2)
	{
		return FVector2f(-1.0f, -1.0f);
	}

	FVector2D Min = PointArray[0];
	FVector2D Max = PointArray[0];
	for (const FVector2D& Point : PointArray)
	{
		Min = FVector2D::Min(Min, Point);
		Max = FVector2D::Max(Max, Point);
	}

	// A straight horizontal run has zero extent vertically and is still LineWidth tall, so the
	// stroke has to be added before the zero test rather than after it.
	const float Width = static_cast<float>(Max.X - Min.X) + LineWidth;
	const float Height = static_cast<float>(Max.Y - Min.Y) + LineWidth;
	return FVector2f(Width > 0.0f ? Width : -1.0f, Height > 0.0f ? Height : -1.0f);
}

float UDream2DLineRaw::GetPreferredWidth() const
{
	return MeasureStrokeBounds().X;
}

float UDream2DLineRaw::GetPreferredHeight() const
{
	return MeasureStrokeBounds().Y;
}

DECLARE_DREAM_GUI_VISUAL("Line2DRaw", UDream2DLineRaw)
