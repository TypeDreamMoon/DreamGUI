// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/Text/DreamTextPaint.h"
#include "Rendering/DrawElementTypes.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SLeafWidget.h"

/**
 * A gradient drawn the way it paints a box the shape of this widget: FDreamGradient::Evaluate at a grid of points,
 * so the colour space, the stops' alpha, the angle and the shape are the text's own. Transparent parts show a
 * checkerboard. Sampled again only when the gradient or the widget's size changes.
 */
class SDreamGradientPreview : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SDreamGradientPreview)
		: _DesiredSize(FVector2D(64.0, 16.0))
	{}
		/** The gradient to draw: read every paint, compared with the one last sampled. */
		SLATE_ATTRIBUTE(FDreamGradient, Gradient)
		SLATE_ARGUMENT(FVector2D, DesiredSize)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;

private:
	void Sample(const FDreamGradient& InGradient, const FVector2f& InSize) const;

	TAttribute<FDreamGradient> Gradient;
	/** What the DesiredSize argument asked for (SWidget keeps a DesiredSize of its own). */
	FVector2D PreferredSize = FVector2D(64.0, 16.0);

	mutable FDreamGradient SampledGradient;
	mutable FVector2f SampledSize = FVector2f::ZeroVector;
	/** Rows top to bottom, each a left-to-right gradient of its samples in widget space. */
	mutable TArray<TArray<FSlateGradientStop>> SampledRows;
	mutable bool bSampledTransparency = false;
	mutable bool bHasSamples = false;
};
