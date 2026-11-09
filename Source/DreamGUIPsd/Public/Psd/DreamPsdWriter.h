// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Psd/DreamPsdDocument.h"

/**
 * A small writer of layered image documents, for tests: it writes what the reader reads, in the layout the editor
 * writes it, so a test can make a document that exercises one feature and hold the reader and the importer to it.
 * It is not an exporter -- no image resources beyond the few the reader looks at, no thumbnail, no colour profile.
 */
struct DREAMGUIPSD_API FDreamPsdWriteLayer
{
	FString Name;
	/** Written as 'lyid' when not negative. */
	int32 LayerId = -1;
	/** Document pixels. Pixels must be Rect-sized (or empty for a layer with none). */
	FIntRect Rect;
	FDreamPsdImage Pixels;
	FString BlendKey = TEXT("norm");
	uint8 Opacity = 255;
	/** Written as 'iOpa' when not 255. */
	uint8 FillOpacity = 255;
	bool bClipping = false;
	bool bVisible = true;
	/** 0 a layer, 1 a group's own record (open), 2 closed, 3 the divider at the bottom of a group. */
	int32 SectionType = 0;
	/** 0 raw, 1 run-length, 2 zip, 3 zip with prediction. */
	uint16 Compression = 1;

	/** A pixel mask: Rect-sized in MaskRect. */
	bool bMask = false;
	FIntRect MaskRect;
	FDreamPsdGray MaskPixels;
	uint8 MaskDefault = 0;
	float MaskDensity = 1.0f;
	float MaskFeather = 0.0f;
	bool bMaskDisabled = false;

	/** A vector mask ('vmsk'), in document pixels. */
	bool bVectorMask = false;
	bool bVectorInvert = false;
	FDreamPsdPath VectorPath;
	/** A live shape description ('vogk'); Box and Radii as FDreamPsdLiveShape has them. */
	FDreamPsdLiveShape LiveShape;
	/** A stroke ('vstk'). */
	FDreamPsdStroke Stroke;

	/** A fill ('SoCo' or 'GdFl'). */
	FDreamPsdFill Fill;

	/** A type layer: the text, its transform, its runs. The engine data is made from them. */
	TSharedPtr<FDreamPsdText> Text;

	/** Further blocks, written as they are ('lfx2', an adjustment...). */
	TArray<FDreamPsdTaggedBlock> ExtraBlocks;
};

struct DREAMGUIPSD_API FDreamPsdWriteDocument
{
	int32 Width = 0;
	int32 Height = 0;
	/** 8 or 16. A 16-bit document writes its layers in an 'Lr16' block, as the editor does. */
	int32 Depth = 8;
	/** PSB: version 2, wide lengths. */
	bool bLarge = false;
	EDreamPsdColorMode ColorMode = EDreamPsdColorMode::RGB;
	/** Bottom first, group records explicit. */
	TArray<FDreamPsdWriteLayer> Layers;
	/** The merged image, Width x Height; written with its transparency (the layer count negative). */
	FDreamPsdImage Composite;
	uint16 CompositeCompression = 1;
	float GlobalAngle = 120.0f;
	float GlobalAltitude = 30.0f;
};

namespace DreamPsdWriter
{
	DREAMGUIPSD_API void Write(const FDreamPsdWriteDocument& InDocument, TArray<uint8>& OutBytes);

	/** An 'lfx2' block holding InEffects (a descriptor of the effects, as the editor writes it). */
	DREAMGUIPSD_API FDreamPsdTaggedBlock MakeEffectsBlock(const FDreamPsdDescriptor& InEffects);
	/** A descriptor-versioned block of any key ('CgEd', 'vibA', 'blwh'...). */
	DREAMGUIPSD_API FDreamPsdTaggedBlock MakeDescriptorBlock(const FString& InKey, const FDreamPsdDescriptor& InDescriptor);
	/** A levels block: the composite's input/output black and white and gamma, the channels untouched. */
	DREAMGUIPSD_API FDreamPsdTaggedBlock MakeLevelsBlock(int32 InBlack, int32 InWhite, float InGamma, int32 InOutBlack = 0, int32 InOutWhite = 255);
	/** A curves block (the newer form): one curve per channel index, points (input, output) in 0..255. */
	DREAMGUIPSD_API FDreamPsdTaggedBlock MakeCurvesBlock(const TArray<FDreamPsdAdjustment::FCurve>& InCurves);
	/** A hue/saturation block: the master's hue, saturation and lightness, optionally colorize. */
	DREAMGUIPSD_API FDreamPsdTaggedBlock MakeHueSaturationBlock(int32 InHue, int32 InSaturation, int32 InLightness, bool bInColorize = false);
	/** The engine data a type layer of this text would carry. */
	DREAMGUIPSD_API FDreamPsdEngineValue MakeEngineData(const FDreamPsdText& InText);
}
