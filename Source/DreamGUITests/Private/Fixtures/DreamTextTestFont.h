// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DreamUIFontData_DistanceField.h"
#include "DreamTextTestFont.generated.h"

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
	virtual void AddUIText(UDreamText* InText) override {}
	virtual void RemoveUIText(UDreamText* InText) override {}
	/** No FreeType face, so no shaping: layout takes the one-glyph-per-code-point path. */
	virtual int32 GetFaceCount() override { return 1; }
	virtual bool FaceHasCodepoint(int32 FaceIndex, uint32 Codepoint) override { return true; }
	virtual void* GetShapingFont(int32 FaceIndex, float FontSize) override { return nullptr; }
};
