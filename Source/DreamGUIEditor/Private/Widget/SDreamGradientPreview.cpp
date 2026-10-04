// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Widget/SDreamGradientPreview.h"

#include "Layout/Geometry.h"
#include "Rendering/DrawElements.h"
#include "Styling/AppStyle.h"

namespace SDreamGradientPreviewLocal
{
	/** Slate units between two samples along a row, and the most samples a row takes. */
	constexpr float SampleSpacing = 2.0f;
	constexpr int32 MaxSamplesPerRow = 256;
	/** Rows: enough for a radial or an angled gradient to read as one in a short strip. */
	constexpr float RowHeight = 2.0f;
	constexpr int32 MaxRows = 12;
}

void SDreamGradientPreview::Construct(const FArguments& InArgs)
{
	Gradient = InArgs._Gradient;
	PreferredSize = InArgs._DesiredSize;
}

FVector2D SDreamGradientPreview::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	return PreferredSize;
}

void SDreamGradientPreview::Sample(const FDreamGradient& InGradient, const FVector2f& InSize) const
{
	using namespace SDreamGradientPreviewLocal;
	const int32 Columns = FMath::Clamp(FMath::CeilToInt(InSize.X / SampleSpacing) + 1, 2, MaxSamplesPerRow);
	const int32 Rows = FMath::Clamp(FMath::RoundToInt(InSize.Y / RowHeight), 1, MaxRows);
	// The widget as the box: its own width over height, as a text block's would be.
	const float Aspect = InSize.X / InSize.Y;
	SampledRows.Reset();
	SampledRows.SetNum(Rows);
	bSampledTransparency = false;
	for (int32 Row = 0; Row < Rows; ++Row)
	{
		const float V = (Row + 0.5f) / Rows;
		TArray<FSlateGradientStop>& Stops = SampledRows[Row];
		Stops.Reserve(Columns);
		for (int32 Column = 0; Column < Columns; ++Column)
		{
			const float U = (float)Column / (float)(Columns - 1);
			const FLinearColor Color = InGradient.Evaluate(FVector2f(U, V), Aspect);
			bSampledTransparency |= Color.A < 1.0f;
			Stops.Emplace(FVector2f(U * InSize.X, 0.0f), Color);
		}
	}
	SampledGradient = InGradient;
	SampledSize = InSize;
	bHasSamples = true;
}

int32 SDreamGradientPreview::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const FVector2f Size = AllottedGeometry.GetLocalSize();
	if (Size.X < 1.0f || Size.Y < 1.0f)
	{
		return LayerId;
	}
	const FDreamGradient Current = Gradient.Get();
	if (!bHasSamples || Current != SampledGradient || !Size.Equals(SampledSize, 0.5f))
	{
		Sample(Current, Size);
	}

	const ESlateDrawEffect DrawEffects = ShouldBeEnabled(bParentEnabled) ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;
	if (bSampledTransparency)
	{
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), FAppStyle::GetBrush("Checkerboard"), DrawEffects);
	}
	const float RowSize = Size.Y / SampledRows.Num();
	for (int32 Row = 0; Row < SampledRows.Num(); ++Row)
	{
		// Vertical lines of colour, so each row's stops run left to right.
		FSlateDrawElement::MakeGradient(OutDrawElements, LayerId + 1,
			AllottedGeometry.ToPaintGeometry(FVector2f(Size.X, RowSize), FSlateLayoutTransform(FVector2f(0.0f, Row * RowSize))),
			SampledRows[Row], Orient_Vertical, DrawEffects);
	}
	return LayerId + 1;
}
