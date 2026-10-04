// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DreamUIFontData_DistanceField.h"
#include "DreamTextTestFont.generated.h"

/** One face of the test font's family: what it has, what its fallback entry says, and how it measures. */
struct FDreamTextTestFace
{
	/** Code points the face has, inclusive ranges; empty means every code point. */
	TArray<FInt32Interval> Has;
	/** Its fallback entry's ranges (FDreamFontFaceInfo::Ranges); empty means every code point. Not read for face 0. */
	TArray<FInt32Interval> Ranges;
	/** Its fallback entry's cultures, already split ("ja", "zh-Hans"); empty means any language. */
	TArray<FString> Cultures;
	/** Its fallback entry's scale. The layout asks for its glyphs at the style size times this. */
	float Scale = 1.0f;
	bool bPreferOverPrimary = false;
	/** IsColorFace answers this. */
	bool bColor = false;
	/** Multiplies the width and advance of every glyph the face gives, so a measurement tells which face drew a character. */
	float WidthScale = 1.0f;
};

/**
 * A font with made-up metrics and no FreeType behind it, so text layout can be exercised headlessly
 * and its numbers asserted on. Every metric is a deterministic function of the code point, so two
 * pipelines fed the same string see the same glyphs. Derives from the SDF font so the old, font-owned
 * quad emission can still be driven for the golden comparison while it exists.
 */
UCLASS(NotBlueprintable, NotBlueprintType, Transient, HideDropdown)
class UDreamTextTestFont : public UDreamUIFontData_DistanceField
{
	GENERATED_BODY()
public:
	/** Kerning is on by default so the kerning path is covered; flip it to cover the other one. */
	bool bMockHasKerning = true;
	/** Rasterize at DynamicPixelsPerUnit, as a bitmap font does; off by default, as for every distance field. */
	bool bMockSupportsDynamicPixelsPerUnit = false;
	/**
	 * Every advance rounded to a whole number of these units at the size it is asked for, as a hinted font rounds to whole
	 * pixels; 0, the default, leaves advances as they are. Under a scaled canvas a glyph is asked for at the device size and
	 * measured back, so its advance then differs from the one at the text's own size.
	 */
	float MockAdvanceGrid = 0.0f;

	virtual void InitFont() override {}
	virtual UTexture2DArray* GetFontTexture() override { return nullptr; }
	virtual FDreamUICharData GetCharData(uint32 CharCode, float CharSize, bool IsBold) override;
	virtual bool HasKerning() override { return bMockHasKerning; }
	virtual float GetKerning(uint32 LeftCharCode, uint32 RightCharCode, float CharSize) override;
	virtual float GetLineHeight(float FontSize) override { return FontSize * 1.25f; }
	virtual float GetVerticalOffset(float FontSize) override { return -FontSize * 0.325f; }
	virtual float GetAscent(float FontSize) override { return FontSize * 0.95f; }
	virtual float GetDescent(float FontSize) override { return FontSize * 0.3f; }
	virtual float GetFontSizeLimit() override { return 200.0f; }
	virtual bool GetShouldAffectByPixelPerfect() override { return false; }
	virtual bool GetSupportDynamicPixelsPerUnit() override { return bMockSupportsDynamicPixelsPerUnit; }
	virtual void AddUIText(UDreamText* InText) override {}
	virtual void RemoveUIText(UDreamText* InText) override {}
	/**
	 * The family's faces, face 0 first, as the layout's resolver sees them through GetFaceTable, FaceHasCodepoint,
	 * IsColorFace and GetFaceCharData. Empty -- every test written before faces -- is one face with every code point.
	 */
	TArray<FDreamTextTestFace> MockFaces;
	/**
	 * Coverage glyphs for the painter's tests: GetCoverageGlyph answers through this when it is set, and
	 * SupportsCoverageGlyphs says whether it is. Unset, the font has no coverage support.
	 */
	TFunction<bool(int32 FaceIndex, uint32 GlyphIndex, int32 Size26Dot6, EDreamUICoverageGlyphFlags Flags, FDreamUICoverageGlyph& OutGlyph)> MockCoverageGlyph;
	/** What GetCoverageMaxPixelSize answers. */
	float MockCoverageMaxPixelSize = 20.0f;

	/** No FreeType face, so no shaping: layout takes the one-glyph-per-code-point path, resolving faces itself. */
	virtual int32 GetFaceCount() override { return FMath::Max(1, MockFaces.Num()); }
	/** MockFaces[FaceIndex].Has; every code point without MockFaces; false for a face index past them. */
	virtual bool FaceHasCodepoint(int32 FaceIndex, uint32 Codepoint) override;
	virtual void* GetShapingFont(int32 FaceIndex, float FontSize) override { return nullptr; }
	/** Built from MockFaces each call (Ranges, Cultures, Scale, bPreferOverPrimary); the empty table without them. */
	virtual const FDreamFontFaceTable& GetFaceTable() override;
	virtual bool IsColorFace(int32 FaceIndex) override;
	/** GetCharData's metrics with width and advance times the face's WidthScale, FaceIndex set, GlyphIndex the code point. */
	virtual FDreamUICharData GetFaceCharData(int32 FaceIndex, uint32 CharCode, float CharSize, bool IsBold) override;
	virtual bool SupportsCoverageGlyphs() const override { return (bool)MockCoverageGlyph; }
	virtual float GetCoverageMaxPixelSize() const override { return MockCoverageMaxPixelSize; }
	virtual bool GetCoverageGlyph(int32 FaceIndex, uint32 GlyphIndex, int32 Size26Dot6, EDreamUICoverageGlyphFlags Flags, FDreamUICoverageGlyph& OutGlyph) override;
	/** No face to read a post or OS/2 table from: underlines and strikethroughs are placed from the '_' and '-' glyphs. */
	virtual bool GetDecorationMetrics(int32 FaceIndex, float FontSize, float& OutUnderlinePosition, float& OutUnderlineThickness, float& OutStrikethroughPosition, float& OutStrikethroughThickness) override { return false; }
private:
	/** What GetFaceTable answers with, rebuilt from MockFaces on every call. */
	FDreamFontFaceTable MockFaceTable;
};
