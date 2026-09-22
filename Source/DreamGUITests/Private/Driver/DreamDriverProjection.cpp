// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Driver/DreamDriverProjection.h"

#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamWidget.h"

bool FDreamDriverProjection::IsProjectableCanvas(const UDreamCanvas* InRootCanvas)
{
	if (!IsValid(InRootCanvas))
	{
		return false;
	}
	const EDreamRenderMode RenderMode = InRootCanvas->GetActualRenderMode();
	return RenderMode == EDreamRenderMode::ScreenSpaceOverlay || RenderMode == EDreamRenderMode::RenderTarget;
}

TOptional<FVector2D> FDreamDriverProjection::WorldPointToPixel(const UDreamCanvas* InRootCanvas, const FVector& InWorldPoint)
{
	if (!IsProjectableCanvas(InRootCanvas))
	{
		return TOptional<FVector2D>();
	}
	const FIntPoint ViewportSize = InRootCanvas->GetViewportSize();
	if (ViewportSize.X <= 0 || ViewportSize.Y <= 0)
	{
		return TOptional<FVector2D>();
	}

	const FVector4 Clip = InRootCanvas->GetViewProjectionMatrix().TransformFVector4(FVector4(InWorldPoint, 1.0));
	// At or behind the eye there is no pixel, only a mirrored one on the far side of the origin. The
	// same guard UDreamCanvas::ProjectWorldPointOntoCanvasPlane uses, for the same reason.
	if (Clip.W <= UE_KINDA_SMALL_NUMBER)
	{
		return TOptional<FVector2D>();
	}
	const FVector2D NDC(Clip.X / Clip.W, Clip.Y / Clip.W);
	const FVector2D ViewPoint01(NDC.X * 0.5 + 0.5, NDC.Y * 0.5 + 0.5);
	// The Y flip that GenerateRay applies on the way in, applied here on the way out. See the header.
	return FVector2D(ViewPoint01.X * ViewportSize.X, (1.0 - ViewPoint01.Y) * ViewportSize.Y);
}

TOptional<FVector2D> FDreamDriverProjection::WidgetLocalPointToPixel(const UDreamWidget* InWidget, const FVector2D& InLocalPoint)
{
	if (!IsValid(InWidget))
	{
		return TOptional<FVector2D>();
	}
	const UDreamCanvas* RootCanvas = InWidget->GetRootCanvas();
	if (!IsProjectableCanvas(RootCanvas))
	{
		return TOptional<FVector2D>();
	}
	// A widget's rect lies on its own local X = 0 plane, with the 2D X axis running along local Y and
	// the 2D Y axis along local Z. That is the same mapping UDreamVisual::LineTraceUIRect tests a ray
	// against and the same one UDreamCanvas::ProjectWorldPointOntoCanvasPlane builds its local point
	// with, so a point placed this way is a point the hit test agrees exists.
	const FVector WorldPoint = InWidget->GetWorldTransform().TransformPosition(
		FVector(0.0, InLocalPoint.X, InLocalPoint.Y));
	return WorldPointToPixel(RootCanvas, WorldPoint);
}

TOptional<FBox2D> FDreamDriverProjection::WidgetToPixelRect(const UDreamWidget* InWidget)
{
	if (!IsValid(InWidget))
	{
		return TOptional<FBox2D>();
	}
	const float Left = InWidget->GetLocalSpaceLeft();
	const float Right = InWidget->GetLocalSpaceRight();
	const float Bottom = InWidget->GetLocalSpaceBottom();
	const float Top = InWidget->GetLocalSpaceTop();

	const FVector2D LocalCorners[4] =
	{
		FVector2D(Left, Bottom),
		FVector2D(Right, Bottom),
		FVector2D(Left, Top),
		FVector2D(Right, Top),
	};

	FBox2D PixelBox(ForceInit);
	for (const FVector2D& LocalCorner : LocalCorners)
	{
		const TOptional<FVector2D> CornerPixel = WidgetLocalPointToPixel(InWidget, LocalCorner);
		// All four or none: a box built from the corners that happened to be in front of the eye
		// would be a smaller box than the widget, which is worse than no answer.
		if (!CornerPixel.IsSet())
		{
			return TOptional<FBox2D>();
		}
		PixelBox += CornerPixel.GetValue();
	}
	return PixelBox;
}

TOptional<FVector2D> FDreamDriverProjection::WidgetCentrePixel(const UDreamWidget* InWidget)
{
	if (!IsValid(InWidget))
	{
		return TOptional<FVector2D>();
	}
	return WidgetLocalPointToPixel(InWidget, InWidget->GetLocalSpaceCenter());
}
