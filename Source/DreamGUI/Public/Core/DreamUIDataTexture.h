// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/Texture.h"
#include "DreamUIDataTexture.generated.h"

/** A run of pixels on one row of a data texture, or the same run on each of Rows rows, for UDreamUIDataTexture::Upload. */
struct FDreamUIDataTextureUpdate
{
	int32 X = 0;
	int32 Y = 0;
	int32 PixelCount = 0;
	/** How many rows, from Y down, the run is written on: each row's PixelCount pixels follow the one above's in Data. */
	int32 Rows = 1;
	/** PixelCount pixels of the texture's format for each row; a longer buffer is read only that far. */
	TArray<uint8> Data;
};

/**
 * The texture a canvas's widget data, clip data and rect block data live in: rows of values the shaders read with
 * Load, one row -- or one block of pixels on a row -- per widget, clip or rect block.
 *
 * What it has that the UTexture2DDynamic before it did not is that it grows in place. A texture that has run out of
 * rows is replaced on the render thread by a taller one holding the same rows, and the texture's reference -- what a
 * material, a material instance and the built-in shader all bind -- is pointed at the new one in the same command.
 * Everything that samples this texture follows it without being told: no parameter to set again, no draw call to
 * rebuild, no flush of the RHI. The UTexture2DDynamic was swapped for a new object instead, so every material instance,
 * pooled or not, and every render-thread copy of its resource had to hear of it, and one that did not drew through a
 * texture the collector had taken.
 *
 * The render thread keeps a copy of the pixels. A frame's writes land in the copy in the order they were made, and go
 * up as one texture update per run of rows they touched rather than one per write; growing uploads the copy into the
 * taller texture rather than copying texture to texture.
 *
 * Its size lives outside its properties on purpose: made at run time in the transient package and never saved,
 * duplicated or copied, a copy that reached it anyway has no size, and makes no resource.
 */
UCLASS(Transient, NotBlueprintType)
class DREAMGUI_API UDreamUIDataTexture : public UTexture
{
	GENERATED_BODY()

public:
	/** InWidth x InHeight pixels of InFormat, InBytesPerPixel bytes each, all zero. Once: a second call does nothing. */
	void Initialize(int32 InWidth, int32 InHeight, EPixelFormat InFormat, int32 InBytesPerPixel);
	/** InHeight rows, keeping every row there is and zeroing the new ones. It never shrinks. */
	void Grow(int32 InHeight);
	/** Write InUpdates, in the order given. */
	void Upload(TArray<FDreamUIDataTextureUpdate>&& InUpdates);

	int32 GetWidth() const { return Width; }
	int32 GetHeight() const { return Height; }
	EPixelFormat GetDataFormat() const { return DataFormat; }
	int32 GetBytesPerPixel() const { return BytesPerPixel; }

	//~ Begin UTexture
	virtual FTextureResource* CreateResource() override;
	virtual EMaterialValueType GetMaterialType() const override { return MCT_Texture2D; }
	virtual ETextureClass GetTextureClass() const override { return ETextureClass::TwoDDynamic; }
	virtual float GetSurfaceWidth() const override { return static_cast<float>(Width); }
	virtual float GetSurfaceHeight() const override { return static_cast<float>(Height); }
	virtual float GetSurfaceDepth() const override { return 0.0f; }
	virtual uint32 GetSurfaceArraySize() const override { return 0; }
	//~ End UTexture

private:
	int32 Width = 0;
	int32 Height = 0;
	int32 BytesPerPixel = 0;
	EPixelFormat DataFormat = PF_Unknown;
};
