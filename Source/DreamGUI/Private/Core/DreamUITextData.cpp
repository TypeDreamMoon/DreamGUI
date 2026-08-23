// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/DreamUITextData.h"
#include "Core/DreamUIGeometry.h"
#include "Core/Text/DreamTextLayout.h"
#include "Core/Text/DreamTextPainter.h"
#include "Math/Float16.h"

bool FDreamTextStyle::HasEffects() const
{
	return (OutlineColor.A > 0 && OutlineWidth > 0.0f)
		|| (GlowColor.A > 0 && GlowWidth > 0.0f)
		|| UnderlayColor.A > 0;
}

float FDreamTextStyle::GetFaceReachEm(float ExtraDilateEm) const
{
	return FMath::Max(0.0f, FaceDilate + ExtraDilateEm + FaceSoftness * 0.5f);
}

float FDreamTextStyle::GetEffectReachEm(float ExtraDilateEm, float MaxGlowBoost) const
{
	// Mirrors DreamUIText_ShadeField: the outline sits on the dilated edge, the glow and the underlay's
	// edge sit outside the outline, the underlay is also shifted by its offset.
	const float Dilate = FaceDilate + ExtraDilateEm;
	float Reach = 0.0f;
	if (OutlineColor.A > 0 && OutlineWidth > 0.0f)
	{
		Reach = FMath::Max(Reach, Dilate + OutlineWidth + OutlineSoftness * 0.5f);
	}
	if (GlowColor.A > 0 && GlowWidth > 0.0f)
	{
		Reach = FMath::Max(Reach, Dilate + GlowWidth * (1.0f + FMath::Max(MaxGlowBoost, 0.0f)));
	}
	if (UnderlayColor.A > 0)
	{
		Reach = FMath::Max(Reach, UnderlayOffset.Size() + Dilate + OutlineWidth + UnderlayDilate + UnderlaySoftness * 0.5f);
	}
	return FMath::Max(Reach, 0.0f);
}

bool FDreamTextStyle::operator==(const FDreamTextStyle& Other) const
{
	return FaceSoftness == Other.FaceSoftness
		&& FaceDilate == Other.FaceDilate
		&& OutlineColor == Other.OutlineColor
		&& OutlineWidth == Other.OutlineWidth
		&& OutlineSoftness == Other.OutlineSoftness
		&& UnderlayColor == Other.UnderlayColor
		&& UnderlayOffset == Other.UnderlayOffset
		&& UnderlaySoftness == Other.UnderlaySoftness
		&& UnderlayDilate == Other.UnderlayDilate
		&& GlowColor == Other.GlowColor
		&& GlowWidth == Other.GlowWidth
		&& GlowPower == Other.GlowPower
		&& FillDimAlpha == Other.FillDimAlpha
		&& FillFadeWidth == Other.FillFadeWidth;
}

namespace DreamTextStyleLocal
{
	// The shader decodes x from the high 16 bits and y from the low 16.
	static uint32 PackHalf2(float X, float Y)
	{
		return (uint32(FFloat16(X).Encoded) << 16) | uint32(FFloat16(Y).Encoded);
	}
	// r = bits 16..23, g = 8..15, b = 0..7, a = 24..31
	static uint32 PackColor(const FColor& C)
	{
		return (uint32(C.A) << 24) | (uint32(C.R) << 16) | (uint32(C.G) << 8) | uint32(C.B);
	}
}

void FDreamTextStyle::Pack(TArray<uint8>& OutBytes) const
{
	using namespace DreamTextStyleLocal;
	uint32 Pixels[PackedPixelCount];
	Pixels[0] = PackHalf2(FaceSoftness, FaceDilate);
	Pixels[1] = PackColor(OutlineColor);
	Pixels[2] = PackHalf2(OutlineWidth, OutlineSoftness);
	Pixels[3] = PackColor(UnderlayColor);
	Pixels[4] = PackHalf2(UnderlayOffset.X, UnderlayOffset.Y);
	Pixels[5] = PackHalf2(UnderlaySoftness, UnderlayDilate);
	Pixels[6] = PackColor(GlowColor);
	Pixels[7] = PackHalf2(GlowWidth, GlowPower);
	Pixels[8] = PackHalf2(FillDimAlpha, FillFadeWidth);
	OutBytes.SetNumUninitialized(sizeof(Pixels));
	FMemory::Memcpy(OutBytes.GetData(), Pixels, sizeof(Pixels));
}

FDreamUITextGeometryCache::FDreamUITextGeometryCache()
	: Input(MakeUnique<FDreamTextLayoutInput>())
	, DisplayList(MakeUnique<FDreamTextDisplayList>())
{
}

FDreamUITextGeometryCache::~FDreamUITextGeometryCache() = default;

bool FDreamUITextGeometryCache::SetLayoutInput(const FDreamTextLayoutInput& InInput)
{
	if (*Input != InInput)
	{
		*Input = InInput;
		bIsDirty = true;
	}
	return bIsDirty;
}

const FDreamTextLayoutInput& FDreamUITextGeometryCache::GetLayoutInput() const
{
	return *Input;
}

void FDreamUITextGeometryCache::MarkDirty()
{
	bIsDirty = true;
	BestFitKey.Reset();
}

bool FDreamUITextGeometryCache::TryGetBestFit(const FDreamTextLayoutInput& InCeilingInput, float& OutSize) const
{
	if (BestFitKey.IsValid() && *BestFitKey == InCeilingInput)
	{
		OutSize = BestFitSize;
		return true;
	}
	return false;
}

void FDreamUITextGeometryCache::SetBestFit(const FDreamTextLayoutInput& InCeilingInput, float InSize)
{
	if (!BestFitKey.IsValid())
	{
		BestFitKey = MakeUnique<FDreamTextLayoutInput>();
	}
	*BestFitKey = InCeilingInput;
	BestFitSize = InSize;
}

bool FDreamUITextGeometryCache::EnsureLayout()
{
	if (!bIsDirty)return false;
	if (!Input->Font.IsValid())return false;
	bIsDirty = false;
	LayoutRunCount++;
	FDreamTextLayoutEngine::Layout(*Input, *DisplayList);
	return true;
}

void FDreamUITextGeometryCache::Paint(FDreamUIGeometry& Geometry, const FDreamTextPaintParams& Params)
{
	EnsureLayout();
	FDreamTextPainter::Paint(*DisplayList, Params, Geometry, CharPropertyArray);
}

const FDreamTextDisplayList& FDreamUITextGeometryCache::GetDisplayList() const
{
	return *DisplayList;
}

bool FDreamUITextGeometryCache::IsTextTruncated() const
{
	return DisplayList->bTruncated;
}

FVector2f FDreamUITextGeometryCache::GetPreferredSize() const
{
	return DisplayList->PreferredSize;
}

const TArray<FDreamUITextLineProperty>& FDreamUITextGeometryCache::GetLines() const
{
	return DisplayList->Lines;
}

const TArray<FDreamUIText_RichTextCustomTag>& FDreamUITextGeometryCache::GetCustomTags() const
{
	return DisplayList->CustomTags;
}

const TArray<FDreamUIText_RichTextImageTag>& FDreamUITextGeometryCache::GetImageTags() const
{
	return DisplayList->Images;
}

const TArray<FDreamUIText_Emoji>& FDreamUITextGeometryCache::GetEmojis() const
{
	return DisplayList->Emojis;
}
