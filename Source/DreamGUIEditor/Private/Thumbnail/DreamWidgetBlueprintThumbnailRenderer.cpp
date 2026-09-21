// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Thumbnail/DreamWidgetBlueprintThumbnailRenderer.h"

#include "DreamWidgetBlueprint.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamWidget.h"
#include "Core/Components/DreamWidgetPlacement.h"

#include "CanvasItem.h"
#include "CanvasTypes.h"

bool UDreamWidgetBlueprintThumbnailRenderer::CanVisualizeAsset(UObject* Object)
{
	const UDreamWidgetBlueprint* Blueprint = Cast<UDreamWidgetBlueprint>(Object);
	// Nothing authored means nothing to diagram, and an empty frame says less than the generic
	// Blueprint icon the browser falls back to.
	return Blueprint != nullptr && IsValid(Blueprint->WidgetTree) && IsValid(Blueprint->WidgetTree->RootWidget);
}

FBox2D UDreamWidgetBlueprintThumbnailRenderer::ResolveAnchoredRect(const FBox2D& InParentRect, const FDreamUIAnchorData& InAnchors)
{
	const FVector2D ParentSize = InParentRect.Max - InParentRect.Min;
	// The rect the anchors alone describe: a fraction of the parent on each axis.
	const FVector2D AnchorRefMin = InParentRect.Min + InAnchors.AnchorMin * ParentSize;
	const FVector2D AnchorRefMax = InParentRect.Min + InAnchors.AnchorMax * ParentSize;
	// SizeDelta grows the anchored span. With min == max (a point anchor) the span is zero and
	// SizeDelta IS the size, which is the case an author meets most often.
	const FVector2D Size = (AnchorRefMax - AnchorRefMin) + InAnchors.SizeDelta;
	// AnchoredPosition moves the PIVOT, and the pivot decides which part of the rect that names --
	// which is why the two are always read together (see FinishDesignerDrag's snapshot restore).
	const FVector2D PivotPosition = AnchorRefMin + (AnchorRefMax - AnchorRefMin) * InAnchors.Pivot + InAnchors.AnchoredPosition;
	const FVector2D Min = PivotPosition - InAnchors.Pivot * Size;
	FBox2D Result(Min, Min + Size);
	// A negative size is authored data the designer permits and a rect nobody can draw; normalising
	// it here keeps the wireframe readable rather than inside out.
	if (Result.Min.X > Result.Max.X) { Swap(Result.Min.X, Result.Max.X); }
	if (Result.Min.Y > Result.Max.Y) { Swap(Result.Min.Y, Result.Max.Y); }
	Result.bIsValid = true;
	return Result;
}

FBox2D UDreamWidgetBlueprintThumbnailRenderer::FitCanvasIntoThumbnail(const FBox2D& InThumbnailRect, FIntPoint InCanvasSize)
{
	const FVector2D Available = InThumbnailRect.Max - InThumbnailRect.Min;
	if (Available.X <= 0.0 || Available.Y <= 0.0 || InCanvasSize.X <= 0 || InCanvasSize.Y <= 0)
	{
		return InThumbnailRect;
	}
	// Letterboxed rather than stretched: the aspect ratio is half of what makes one screen
	// recognisable from another at tile size, and a 16:9 HUD squeezed into a square tile looks like
	// the 4:3 one next to it.
	const double CanvasAspect = (double)InCanvasSize.X / (double)InCanvasSize.Y;
	const double AvailableAspect = Available.X / Available.Y;
	FVector2D Size = Available;
	if (CanvasAspect > AvailableAspect)
	{
		Size.Y = Available.X / CanvasAspect;
	}
	else
	{
		Size.X = Available.Y * CanvasAspect;
	}
	const FVector2D Min = InThumbnailRect.Min + (Available - Size) * 0.5;
	FBox2D Result(Min, Min + Size);
	Result.bIsValid = true;
	return Result;
}

void UDreamWidgetBlueprintThumbnailRenderer::CollectWidgetRects(const UDreamWidget* InWidget, const FBox2D& InParentRect,
	int32 InDepth, TArray<TPair<FBox2D, int32>>& OutRects)
{
	if (!IsValid(InWidget))
	{
		return;
	}
	// Four levels is what a 64-pixel tile can still show apart; below that the lines merge into a
	// smudge and each one costs a draw call per thumbnail the browser scrolls past.
	constexpr int32 MaxDepth = 4;
	constexpr int32 MaxRects = 64;
	if (InDepth > MaxDepth || OutRects.Num() >= MaxRects)
	{
		return;
	}
	const FBox2D Rect = ResolveAnchoredRect(InParentRect, InWidget->GetAnchorData());
	OutRects.Emplace(Rect, InDepth);
	for (const UDreamWidget* Child : InWidget->GetChildren())
	{
		CollectWidgetRects(Child, Rect, InDepth + 1, OutRects);
	}
}

void UDreamWidgetBlueprintThumbnailRenderer::Draw(UObject* Object, int32 X, int32 Y, uint32 Width, uint32 Height,
	FRenderTarget* RenderTarget, FCanvas* Canvas, bool bAdditionalViewFamily)
{
	UDreamWidgetBlueprint* Blueprint = Cast<UDreamWidgetBlueprint>(Object);
	if (Blueprint == nullptr || Canvas == nullptr || Width == 0 || Height == 0)
	{
		return;
	}
	const FBox2D ThumbnailRect(FVector2D(X, Y), FVector2D(X + (double)Width, Y + (double)Height));
	FIntPoint CanvasSize = Blueprint->DesignerData.CanvasSize;
	if (CanvasSize.X <= 0 || CanvasSize.Y <= 0)
	{
		// Never authored, or authored as zero. 16:9 rather than the tile's own shape, because an
		// unsaved designer state should not make a screen look like a different screen.
		CanvasSize = FIntPoint(1920, 1080);
	}
	const FBox2D CanvasRect = FitCanvasIntoThumbnail(ThumbnailRect, CanvasSize);

	// The canvas itself, filled, so the tile reads as a screen rather than as a floating diagram.
	Canvas->DrawTile(CanvasRect.Min.X, CanvasRect.Min.Y,
		CanvasRect.Max.X - CanvasRect.Min.X, CanvasRect.Max.Y - CanvasRect.Min.Y,
		0.0f, 0.0f, 1.0f, 1.0f, FLinearColor(0.07f, 0.08f, 0.10f, 1.0f));

	TArray<TPair<FBox2D, int32>> Rects;
	if (IsValid(Blueprint->WidgetTree))
	{
		CollectWidgetRects(Blueprint->WidgetTree->RootWidget, CanvasRect, 0, Rects);
	}
	for (const TPair<FBox2D, int32>& Entry : Rects)
	{
		const FBox2D& Rect = Entry.Key;
		// Clipped to the canvas: a widget authored outside its screen is legitimate (an off-screen
		// panel waiting to slide in) and would otherwise draw over the neighbouring tile.
		const FVector2D Min(FMath::Max(Rect.Min.X, CanvasRect.Min.X), FMath::Max(Rect.Min.Y, CanvasRect.Min.Y));
		const FVector2D Max(FMath::Min(Rect.Max.X, CanvasRect.Max.X), FMath::Min(Rect.Max.Y, CanvasRect.Max.Y));
		if (Max.X - Min.X < 1.0 || Max.Y - Min.Y < 1.0)
		{
			continue;
		}
		// Deeper is dimmer, so nesting is visible at a glance instead of every rect competing.
		const float Fade = FMath::Clamp(1.0f - Entry.Value * 0.18f, 0.35f, 1.0f);
		const FLinearColor LineColor(0.35f * Fade, 0.62f * Fade, 0.95f * Fade, 1.0f);
		const FVector2D Corners[4] = { Min, FVector2D(Max.X, Min.Y), Max, FVector2D(Min.X, Max.Y) };
		for (int32 Corner = 0; Corner < 4; Corner++)
		{
			FCanvasLineItem Line(Corners[Corner], Corners[(Corner + 1) % 4]);
			Line.SetColor(LineColor);
			Line.LineThickness = 1.0f;
			Canvas->DrawItem(Line);
		}
	}
}
