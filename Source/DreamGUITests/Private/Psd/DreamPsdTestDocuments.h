// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CoreMinimal.h"
#include "Psd/DreamPsdDocument.h"
#include "Psd/DreamPsdWriter.h"

/**
 * Layered documents made for the tests, every one original: built layer by layer with the plugin's own writer, so a
 * test says exactly which feature it exercises. And the compositor that says what such a document looks like drawn
 * by DreamGUI into a render target -- the merged image the pixel test holds an import to.
 */
namespace DreamPsdTestDocuments
{
	FDreamPsdImage MakeImage(int32 InWidth, int32 InHeight, TFunctionRef<FColor(int32 X, int32 Y)> InColourAt);
	FDreamPsdGray MakeGray(int32 InWidth, int32 InHeight, TFunctionRef<uint8(int32 X, int32 Y)> InValueAt);

	/** A pixel layer over InRect, its pixels from InColourAt in layer pixels. */
	FDreamPsdWriteLayer PixelLayer(const FString& InName, int32 InId, const FIntRect& InRect, TFunctionRef<FColor(int32 X, int32 Y)> InColourAt);
	/** The two records of a group: the divider that opens it (below its children) and its own record (above them). */
	FDreamPsdWriteLayer GroupDivider(int32 InId);
	FDreamPsdWriteLayer GroupRecord(const FString& InName, int32 InId, const FString& InBlendKey = TEXT("pass"), uint8 InOpacity = 255);

	/** A closed path of a rectangle with rounded corners (cubic arcs), or of an ellipse, in document pixels. */
	FDreamPsdPath RoundedRectPath(const FBox2D& InBox, float InRadius);
	/** Radii: top left, top right, bottom right, bottom left. */
	FDreamPsdPath RoundedRectPath(const FBox2D& InBox, const FVector4& InRadii);
	FDreamPsdPath EllipsePath(const FBox2D& InBox);

	/** A disc of InColour, anti-aliased, InRadius about InCentre, over a layer InSize big. */
	FColor Disc(int32 X, int32 Y, const FVector2D& InCentre, float InRadius, FColor InColour);

	/**
	 * The gallery the pixel test imports: a background, an anti-aliased disc with stripes clipped to it, multiply and
	 * overlay layers, a masked layer, an isolated group at 60 % whose squares overlap, a pass-through group under an
	 * elliptic vector mask, a rounded-rectangle shape, a tab rounded unevenly, a fill layer at half fill under a gradient mask, and a hidden
	 * layer and a `!skip` one. 256 x 192. The merged image is left to CompositeAsDrawn.
	 */
	FDreamPsdWriteDocument MakeGallery(int32 InDepth = 8, bool bInLarge = false, uint16 InCompression = 1);

	/**
	 * What DreamGUI draws of a document into a render target over InBackdrop: normal layers over in linear light (an
	 * sRGB target blends there), the blend hardware's modes as it does them, the modes that read what is behind by
	 * their formula in encoded space, isolated groups drawn apart and put down once, masks and clipping multiplied in.
	 * Only what the gallery uses is modelled.
	 */
	FDreamPsdImage CompositeAsDrawn(const FDreamPsdDocument& InDocument, FColor InBackdrop);

	/** Write InDocument, read it back, put its CompositeAsDrawn in as the merged image, and write it again. */
	TArray<uint8> WriteWithComposite(FDreamPsdWriteDocument& InOutDocument, FColor InBackdrop);
}

#endif
