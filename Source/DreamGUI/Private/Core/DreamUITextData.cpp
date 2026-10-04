// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/DreamUITextData.h"
#include "Core/DreamUIGeometry.h"
#include "Core/Text/DreamTextLayout.h"
#include "Core/Text/DreamTextPainter.h"
#include "Hash/CityHash.h"
#include "Math/Float16.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

// ICU is linked into this module wherever HarfBuzz is (DreamGUI.Build.cs): every target but a dedicated server. Its
// emoji properties need ICU 62 (Extended_Pictographic); a platform on an older ICU answers from the tables.
#if UE_ENABLE_ICU && !UE_SERVER
THIRD_PARTY_INCLUDES_START
#include <unicode/uchar.h>
THIRD_PARTY_INCLUDES_END
#if U_ICU_VERSION_MAJOR_NUM >= 62
#define DREAMUITEXTDATA_EMOJI_FROM_ICU 1
#endif
#endif
#ifndef DREAMUITEXTDATA_EMOJI_FROM_ICU
#define DREAMUITEXTDATA_EMOJI_FROM_ICU 0
#endif

namespace DreamUITextDataEmojiLocal
{
	/** An inclusive range of code points. */
	struct FCodepointRange
	{
		uint32 First;
		uint32 Last;
	};

	/** Emoji_Presentation=Yes, from emoji-data.txt of Emoji 16.0. */
	constexpr FCodepointRange EmojiPresentationRanges[] =
	{
		{0x231A, 0x231B}, {0x23E9, 0x23EC}, {0x23F0, 0x23F0}, {0x23F3, 0x23F3}, {0x25FD, 0x25FE}, {0x2614, 0x2615},
		{0x2648, 0x2653}, {0x267F, 0x267F}, {0x2693, 0x2693}, {0x26A1, 0x26A1}, {0x26AA, 0x26AB}, {0x26BD, 0x26BE},
		{0x26C4, 0x26C5}, {0x26CE, 0x26CE}, {0x26D4, 0x26D4}, {0x26EA, 0x26EA}, {0x26F2, 0x26F3}, {0x26F5, 0x26F5},
		{0x26FA, 0x26FA}, {0x26FD, 0x26FD}, {0x2705, 0x2705}, {0x270A, 0x270B}, {0x2728, 0x2728}, {0x274C, 0x274C},
		{0x274E, 0x274E}, {0x2753, 0x2755}, {0x2757, 0x2757}, {0x2795, 0x2797}, {0x27B0, 0x27B0}, {0x27BF, 0x27BF},
		{0x2B1B, 0x2B1C}, {0x2B50, 0x2B50}, {0x2B55, 0x2B55},
		{0x1F004, 0x1F004}, {0x1F0CF, 0x1F0CF}, {0x1F18E, 0x1F18E}, {0x1F191, 0x1F19A}, {0x1F1E6, 0x1F1FF},
		{0x1F201, 0x1F201}, {0x1F21A, 0x1F21A}, {0x1F22F, 0x1F22F}, {0x1F232, 0x1F236}, {0x1F238, 0x1F23A},
		{0x1F250, 0x1F251}, {0x1F300, 0x1F320}, {0x1F32D, 0x1F335}, {0x1F337, 0x1F37C}, {0x1F37E, 0x1F393},
		{0x1F3A0, 0x1F3CA}, {0x1F3CF, 0x1F3D3}, {0x1F3E0, 0x1F3F0}, {0x1F3F4, 0x1F3F4}, {0x1F3F8, 0x1F43E},
		{0x1F440, 0x1F440}, {0x1F442, 0x1F4FC}, {0x1F4FF, 0x1F53D}, {0x1F54B, 0x1F54E}, {0x1F550, 0x1F567},
		{0x1F57A, 0x1F57A}, {0x1F595, 0x1F596}, {0x1F5A4, 0x1F5A4}, {0x1F5FB, 0x1F64F}, {0x1F680, 0x1F6C5},
		{0x1F6CC, 0x1F6CC}, {0x1F6D0, 0x1F6D2}, {0x1F6D5, 0x1F6D7}, {0x1F6DC, 0x1F6DF}, {0x1F6EB, 0x1F6EC},
		{0x1F6F4, 0x1F6FC}, {0x1F7E0, 0x1F7EB}, {0x1F7F0, 0x1F7F0}, {0x1F90C, 0x1F93A}, {0x1F93C, 0x1F945},
		{0x1F947, 0x1F9FF}, {0x1FA70, 0x1FA7C}, {0x1FA80, 0x1FA89}, {0x1FA8F, 0x1FAC6}, {0x1FACE, 0x1FADC},
		{0x1FADF, 0x1FAE9}, {0x1FAF0, 0x1FAF8},
	};

	/**
	 * Extended_Pictographic, from emoji-data.txt of Emoji 16.0, which like every version since 11.0 also covers the
	 * unassigned code points of the pictographic blocks: an emoji encoded later is a pictograph here already.
	 */
	constexpr FCodepointRange ExtendedPictographicRanges[] =
	{
		{0x00A9, 0x00A9}, {0x00AE, 0x00AE}, {0x203C, 0x203C}, {0x2049, 0x2049}, {0x2122, 0x2122}, {0x2139, 0x2139},
		{0x2194, 0x2199}, {0x21A9, 0x21AA}, {0x231A, 0x231B}, {0x2328, 0x2328}, {0x2388, 0x2388}, {0x23CF, 0x23CF},
		{0x23E9, 0x23F3}, {0x23F8, 0x23FA}, {0x24C2, 0x24C2}, {0x25AA, 0x25AB}, {0x25B6, 0x25B6}, {0x25C0, 0x25C0},
		{0x25FB, 0x25FE}, {0x2600, 0x2605}, {0x2607, 0x2612}, {0x2614, 0x2685}, {0x2690, 0x2705}, {0x2708, 0x2712},
		{0x2714, 0x2714}, {0x2716, 0x2716}, {0x271D, 0x271D}, {0x2721, 0x2721}, {0x2728, 0x2728}, {0x2733, 0x2734},
		{0x2744, 0x2744}, {0x2747, 0x2747}, {0x274C, 0x274C}, {0x274E, 0x274E}, {0x2753, 0x2755}, {0x2757, 0x2757},
		{0x2763, 0x2767}, {0x2795, 0x2797}, {0x27A1, 0x27A1}, {0x27B0, 0x27B0}, {0x27BF, 0x27BF}, {0x2934, 0x2935},
		{0x2B05, 0x2B07}, {0x2B1B, 0x2B1C}, {0x2B50, 0x2B50}, {0x2B55, 0x2B55}, {0x3030, 0x3030}, {0x303D, 0x303D},
		{0x3297, 0x3297}, {0x3299, 0x3299},
		{0x1F000, 0x1F0FF}, {0x1F10D, 0x1F10F}, {0x1F12F, 0x1F12F}, {0x1F16C, 0x1F171}, {0x1F17E, 0x1F17F},
		{0x1F18E, 0x1F18E}, {0x1F191, 0x1F19A}, {0x1F1AD, 0x1F1E5}, {0x1F201, 0x1F20F}, {0x1F21A, 0x1F21A},
		{0x1F22F, 0x1F22F}, {0x1F232, 0x1F23A}, {0x1F23C, 0x1F23F}, {0x1F249, 0x1F3FA}, {0x1F400, 0x1F53D},
		{0x1F546, 0x1F64F}, {0x1F680, 0x1F6FF}, {0x1F774, 0x1F77F}, {0x1F7D5, 0x1F7FF}, {0x1F80C, 0x1F80F},
		{0x1F848, 0x1F84F}, {0x1F85A, 0x1F85F}, {0x1F888, 0x1F88F}, {0x1F8AE, 0x1F8FF}, {0x1F90C, 0x1F93A},
		{0x1F93C, 0x1F945}, {0x1F947, 0x1FAFF}, {0x1FC00, 0x1FFFD},
	};

	/** Whether a code point is in a table of ranges sorted by code point: a binary search. */
	bool IsInRanges(const FCodepointRange* Ranges, int32 Count, uint32 Codepoint)
	{
		int32 Low = 0;
		int32 High = Count - 1;
		while (Low <= High)
		{
			const int32 Middle = (Low + High) / 2;
			if (Codepoint < Ranges[Middle].First)
			{
				High = Middle - 1;
			}
			else if (Codepoint > Ranges[Middle].Last)
			{
				Low = Middle + 1;
			}
			else
			{
				return true;
			}
		}
		return false;
	}
}

bool FDreamUIText_CodePoint::HasEmojiPresentationFromTable(uint32 Codepoint)
{
	using namespace DreamUITextDataEmojiLocal;
	return Codepoint >= 0x231A && IsInRanges(EmojiPresentationRanges, (int32)UE_ARRAY_COUNT(EmojiPresentationRanges), Codepoint);
}

bool FDreamUIText_CodePoint::IsExtendedPictographicFromTable(uint32 Codepoint)
{
	using namespace DreamUITextDataEmojiLocal;
	return Codepoint >= 0x00A9 && IsInRanges(ExtendedPictographicRanges, (int32)UE_ARRAY_COUNT(ExtendedPictographicRanges), Codepoint);
}

bool FDreamUIText_CodePoint::HasEmojiPresentation(uint32 Codepoint)
{
	if (Codepoint < 0x231A)
	{
		return false;
	}
#if DREAMUITEXTDATA_EMOJI_FROM_ICU
	if (Codepoint <= 0x10FFFF)
	{
		if (u_hasBinaryProperty((UChar32)Codepoint, UCHAR_EMOJI_PRESENTATION))
		{
			return true;
		}
		// ICU's data stops at Unicode 12: a code point it knows keeps its answer, one encoded since then asks the table.
		if (u_charType((UChar32)Codepoint) != U_UNASSIGNED)
		{
			return false;
		}
	}
#endif
	return HasEmojiPresentationFromTable(Codepoint);
}

bool FDreamUIText_CodePoint::IsExtendedPictographic(uint32 Codepoint)
{
	if (Codepoint < 0x00A9)
	{
		return false;
	}
#if DREAMUITEXTDATA_EMOJI_FROM_ICU
	// ICU's answer for a code point its data has. One it does not have asks the table: Unicode 12 held U+1FA96-1FFFD for
	// pictographs to come, and Unicode 13 gave U+1FB00-1FBFF of them to Symbols for Legacy Computing, which are none.
	if (Codepoint <= 0x10FFFF && u_charType((UChar32)Codepoint) != U_UNASSIGNED)
	{
		return u_hasBinaryProperty((UChar32)Codepoint, UCHAR_EXTENDED_PICTOGRAPHIC) != 0;
	}
#endif
	return IsExtendedPictographicFromTable(Codepoint);
}

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

bool FDreamTextStyle::HasPaints() const
{
	return FacePaint.IsPainting() || OutlinePaint.IsPainting() || OverlayPaint.IsPainting();
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
		&& FillFadeWidth == Other.FillFadeWidth
		&& FacePaint == Other.FacePaint
		&& OutlinePaint == Other.OutlinePaint
		&& OverlayPaint == Other.OverlayPaint
		&& OverlayBlend == Other.OverlayBlend
		&& PaintBoxHorizontal == Other.PaintBoxHorizontal
		&& PaintBoxVertical == Other.PaintBoxVertical;
}

/*
 * Every style pixel is a 32-bit pattern the shader loads as a FLOAT before it takes the bits apart (asuint), as every
 * pixel of the widget record is (UDreamVisual::PackWidgetMarks). A pattern whose float exponent is all zeros and whose
 * mantissa is not is a denormal, which a GPU that flushes denormals -- most mobile ones -- loads as 0, and the pixel's
 * values are gone. The exponent is bits 23..30: for two halves, the first half's exponent and its three top mantissa
 * bits; for a colour, alpha's low seven bits and red's top bit. So the packing keeps them off zero where it matters.
 */
namespace DreamTextStyleLocal
{
	/** 2^-14, the smallest normal half: what a first half too small to give the pixel an exponent is nudged to. */
	constexpr uint32 SmallestNormalHalf = 0x0400;

	// The shader decodes x from the high 16 bits and y from the low 16.
	static uint32 PackHalf2(float X, float Y)
	{
		uint32 XBits = uint32(FFloat16(X).Encoded);
		const uint32 YBits = uint32(FFloat16(Y).Encoded);
		// A zero (or a value under 2^-17) first with anything second -- no softness with a dilation, an underlay offset
		// straight down, no fill dim with a fade width -- would be a denormal: the first half becomes the smallest normal
		// one, 0.00006 em or alpha, with its sign, and the second is kept exactly.
		const bool bNoExponent = ((XBits >> 7) & 0xFF) == 0;
		const bool bAnyMantissa = (XBits & 0x7F) != 0 || YBits != 0;
		if (bNoExponent && bAnyMantissa)
		{
			XBits = (XBits & 0x8000) | SmallestNormalHalf;
		}
		return (XBits << 16) | YBits;
	}
	// r = bits 16..23, g = 8..15, b = 0..7, a = 24..31
	static uint32 PackColor(const FColor& C)
	{
		// Alpha 128 leaves the exponent to red's top bit alone, so a colour of alpha 128 and red under 128 would be a
		// denormal and lose its RGB: 129 is indistinguishable and never is. (Alpha 0 can be one as well, and reads back as
		// the transparent colour it was.)
		const uint32 Alpha = C.A == 128 ? 129u : uint32(C.A);
		return (Alpha << 24) | (uint32(C.R) << 16) | (uint32(C.G) << 8) | uint32(C.B);
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

namespace DreamUITextGeometryCacheLocal
{
	/** Elements a text needs before its cache keeps its layout for edits without being asked to. */
	constexpr int32 AutomaticIncrementalElements = 256;
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
	else
	{
		// Colour is outside equality on purpose (it is a paint input), so it would otherwise go stale
		// here. Nothing reads it back for painting today, but a stored input that disagrees with the
		// widget would be a trap for whoever does.
		Input->Color = InInput.Color;
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
	// What changed underneath an unchanged input -- glyphs that landed, an atlas refilled, a style asset edited in place --
	// may be in what the last layout kept: it goes, and the next layout starts from nothing.
	if (IncrementalState.IsValid())
	{
		IncrementalState->Reset();
	}
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
	TRACE_CPUPROFILER_EVENT_SCOPE(DreamUI_TextLayout);
	FDreamTextLayoutEngine::Layout(*Input, *DisplayList, PrepareIncrementalState());
	LastElementCount = DisplayList->ElementCount;
	return true;
}

FDreamTextLayoutState* FDreamUITextGeometryCache::PrepareIncrementalState()
{
	using namespace DreamUITextGeometryCacheLocal;
	if (!FDreamTextLayoutEngine::IsIncrementalLayoutEnabled())
	{
		IncrementalState.Reset();
		bAutoIncrementalLayout = false;
		ContentChangeStreak = 0;
		bHasContentHash = false;
		return nullptr;
	}
	// A long text whose content changed in this layout and the one before it is being edited, or streamed into: it keeps
	// its layout from then on, until it falls under the threshold. A text of fewer code units than that has fewer elements
	// too, and is not even hashed.
	const FString& Content = Input->Content;
	if (Content.Len() >= AutomaticIncrementalElements)
	{
		const uint64 Hash = CityHash64(reinterpret_cast<const char*>(*Content), (uint32)(Content.Len() * sizeof(TCHAR)));
		ContentChangeStreak = bHasContentHash && Hash != LastContentHash ? ContentChangeStreak + 1 : 0;
		LastContentHash = Hash;
		bHasContentHash = true;
		bAutoIncrementalLayout = (bAutoIncrementalLayout || ContentChangeStreak >= 2) && LastElementCount >= AutomaticIncrementalElements;
	}
	else
	{
		ContentChangeStreak = 0;
		bHasContentHash = false;
		bAutoIncrementalLayout = false;
	}
	if (!bIncrementalLayoutRequested && !bAutoIncrementalLayout)
	{
		IncrementalState.Reset();
		return nullptr;
	}
	if (!IncrementalState.IsValid())
	{
		IncrementalState = MakeUnique<FDreamTextLayoutState>();
	}
	return IncrementalState.Get();
}

bool FDreamUITextGeometryCache::IsIncrementalLayoutActive() const
{
	return IncrementalState.IsValid();
}

void FDreamUITextGeometryCache::Paint(FDreamUIGeometry& Geometry, const FDreamTextPaintParams& Params)
{
	EnsureLayout();
	TRACE_CPUPROFILER_EVENT_SCOPE(DreamUI_TextPaint);
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
