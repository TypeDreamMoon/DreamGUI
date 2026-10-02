// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "DreamTextTestFont.h"

FDreamUICharData UDreamTextTestFont::GetCharData(uint32 CharCode, float CharSize, bool IsBold)
{
	FDreamUICharData Data;
	if (CharSize <= 0.0f)return Data;

	const float Em = CharSize;
	if (CharCode == ' ' || CharCode == '\t')
	{
		Data.XAdvance = Em * 0.3f;
		return Data;
	}
	// Widths and heights vary with the code point so alignment, wrapping and truncation all see
	// different numbers per glyph; bold adds a little width, as real emboldening would.
	const float WidthUnit = (float)((CharCode * 7u) % 10u) * 0.1f;
	const float HeightUnit = (float)((CharCode * 13u) % 10u) * 0.1f;
	Data.Width = Em * (0.4f + 0.3f * WidthUnit) + (IsBold ? Em * 0.05f : 0.0f);
	Data.Height = Em * (0.5f + 0.4f * HeightUnit);
	Data.XOffset = Em * 0.05f * (float)(CharCode % 3u);
	Data.YOffset = Data.Height * 0.8f;
	Data.XAdvance = Data.Width + Em * 0.1f;
	const float CellU = (float)(CharCode % 64u) / 64.0f;
	const float CellV = (float)((CharCode / 64u) % 64u) / 64.0f;
	Data.MinUV = FVector2f(CellU, CellV);
	Data.MaxUV = FVector2f(CellU + 1.0f / 64.0f, CellV + 1.0f / 64.0f);
	Data.SliceIndex = 0;
	return Data;
}

float UDreamTextTestFont::GetKerning(uint32 LeftCharCode, uint32 RightCharCode, float CharSize)
{
	const int32 Bucket = (int32)((LeftCharCode * 31u + RightCharCode * 17u) % 7u) - 3;
	return (float)Bucket * 0.01f * CharSize;
}

bool UDreamTextTestFont::FaceHasCodepoint(int32 FaceIndex, uint32 Codepoint)
{
	if (MockFaces.Num() == 0)
	{
		return true;
	}
	if (!MockFaces.IsValidIndex(FaceIndex))
	{
		return false;
	}
	const TArray<FInt32Interval>& Has = MockFaces[FaceIndex].Has;
	if (Has.Num() == 0)
	{
		return true;
	}
	for (const FInt32Interval& Interval : Has)
	{
		if (Interval.Contains((int32)Codepoint))
		{
			return true;
		}
	}
	return false;
}

const FDreamFontFaceTable& UDreamTextTestFont::GetFaceTable()
{
	MockFaceTable.Faces.Reset();
	MockFaceTable.bPreferColorEmoji = true;
	for (const FDreamTextTestFace& MockFace : MockFaces)
	{
		FDreamFontFaceInfo& Info = MockFaceTable.Faces.AddDefaulted_GetRef();
		Info.Ranges = MockFace.Ranges;
		Info.Cultures = MockFace.Cultures;
		Info.Scale = MockFace.Scale;
		Info.bPreferOverPrimary = MockFace.bPreferOverPrimary;
	}
	return MockFaceTable;
}

bool UDreamTextTestFont::IsColorFace(int32 FaceIndex)
{
	return MockFaces.IsValidIndex(FaceIndex) && MockFaces[FaceIndex].bColor;
}

FDreamUICharData UDreamTextTestFont::GetFaceCharData(int32 FaceIndex, uint32 CharCode, float CharSize, bool IsBold)
{
	FDreamUICharData Data = GetCharData(CharCode, CharSize, IsBold);
	if (MockFaces.IsValidIndex(FaceIndex))
	{
		const FDreamTextTestFace& MockFace = MockFaces[FaceIndex];
		Data.Width *= MockFace.WidthScale;
		Data.XAdvance *= MockFace.WidthScale;
		// A colour face hands out colour glyphs, as a real one does: its quad samples a colour cell of 64 texels per em.
		Data.bColor = MockFace.bColor;
		Data.ColorTexelsPerEm = MockFace.bColor ? 64.0f : 0.0f;
	}
	Data.FaceIndex = FaceIndex;
	Data.GlyphIndex = CharCode;
	return Data;
}

bool UDreamTextTestFont::GetCoverageGlyph(int32 FaceIndex, uint32 GlyphIndex, int32 Size26Dot6, EDreamUICoverageGlyphFlags Flags, FDreamUICoverageGlyph& OutGlyph)
{
	return MockCoverageGlyph ? MockCoverageGlyph(FaceIndex, GlyphIndex, Size26Dot6, Flags, OutGlyph) : false;
}
