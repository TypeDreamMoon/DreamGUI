// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Tests/DreamTextTestFont.h"

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
