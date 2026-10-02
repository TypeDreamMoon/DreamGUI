// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Core/DreamUIGeometry.h"
#include "Core/DreamUITextData.h"
#include "Core/Text/DreamTextPainter.h"

/*
 * Colour glyphs (emoji from a colour face) at the painter: the quad is the padded cell the layout placed, never grown,
 * dilated or shifted; the code in UV2.x tells the shader to sample it as a colour bitmap, UV3.y carries the cell's texels
 * per em, the vertex is white with the item's alpha; an outline is never drawn around one, and its underlay gets a copy of
 * its own only when the text draws an underlay. The display lists are built by hand: nothing here needs a font.
 */
namespace DreamTextColorGlyphTestLocal
{
	/** One texel of the made-up atlas, in UV. */
	constexpr float AtlasTexel = 1.0f / 1024.0f;
	/** The colour cell: 40 texels square at 48 texels per em, drawn 30 units square. */
	constexpr float CellTexels = 40.0f;
	constexpr float CellTexelsPerEm = 48.0f;
	constexpr float CellSize = 30.0f;

	/** A plain glyph item, its field quad a function of the code point. */
	FDreamTextGlyphItem MakeGlyph(uint32 Codepoint, int32 ElementIndex, const FVector2f& Pen, float Size)
	{
		FDreamTextGlyphItem Item;
		Item.Kind = EDreamTextItemKind::Glyph;
		Item.Codepoint = Codepoint;
		Item.ElementIndex = ElementIndex;
		Item.SourceIndex = ElementIndex;
		Item.Pen = Pen;
		Item.Glyph.Width = Size * 0.5f;
		Item.Glyph.Height = Size * 0.7f;
		Item.Glyph.XOffset = Size * 0.05f;
		Item.Glyph.YOffset = Size * 0.6f;
		Item.Glyph.XAdvance = Size * 0.6f;
		const float CellU = (float)(Codepoint % 32u) / 32.0f;
		Item.Glyph.MinUV = FVector2f(CellU, 0.5f);
		Item.Glyph.MaxUV = FVector2f(CellU + 1.0f / 64.0f, 0.5f + 1.0f / 64.0f);
		Item.Glyph.SliceIndex = 1;
		Item.Glyph.GlyphIndex = Codepoint;
		Item.GlyphSize = Size;
		Item.AdvanceWithSpace = Item.Glyph.XAdvance;
		Item.Style.Size = Size;
		Item.bEmit = true;
		Item.bCountsAsVisible = true;
		return Item;
	}

	/** A colour glyph item: its quad the whole padded cell, as the font hands it out (FDreamUICharData::bColor). */
	FDreamTextGlyphItem MakeColorGlyph(int32 ElementIndex, const FVector2f& Pen, float Size)
	{
		FDreamTextGlyphItem Item = MakeGlyph(0x1F600, ElementIndex, Pen, Size);
		Item.Kind = EDreamTextItemKind::Glyph;
		Item.Glyph.bColor = true;
		Item.Glyph.ColorTexelsPerEm = CellTexelsPerEm;
		Item.Glyph.FaceIndex = 1;
		Item.Glyph.Width = CellSize;
		Item.Glyph.Height = CellSize;
		Item.Glyph.XOffset = -2.0f;
		Item.Glyph.YOffset = 26.0f;
		Item.Glyph.XAdvance = 27.0f;
		Item.Glyph.MinUV = FVector2f(0.5f, 0.25f);
		Item.Glyph.MaxUV = Item.Glyph.MinUV + FVector2f(CellTexels * AtlasTexel, CellTexels * AtlasTexel);
		Item.Glyph.SliceIndex = 2;
		Item.AdvanceWithSpace = Item.Glyph.XAdvance;
		return Item;
	}

	/** a, the colour glyph, b on one line. */
	FDreamTextDisplayList MakeLine(bool bColorItalic = false, bool bColorBold = false)
	{
		FDreamTextDisplayList DL;
		DL.Lines.AddDefaulted(1);
		DL.Items.Add(MakeGlyph('a', 0, FVector2f(0.0f, -30.0f), 24.0f));
		FDreamTextGlyphItem Color = MakeColorGlyph(1, FVector2f(14.4f, -30.0f), 24.0f);
		Color.Style.bItalic = Color.Style.bSyntheticItalic = bColorItalic;
		Color.Style.bBold = Color.Style.bSyntheticBold = bColorBold;
		DL.Items.Add(Color);
		DL.Items.Add(MakeGlyph('b', 2, FVector2f(41.4f, -30.0f), 24.0f));
		return DL;
	}

	/** A multi-channel distance-field font at 64 texels per em with a 16 texel spread, no effects. */
	FDreamTextPaintParams MakeFieldParams()
	{
		FDreamTextPaintParams Params;
		Params.ItalicSlope = 0.25f;
		Params.BaseColor = FColor(200, 100, 50, 128);
		Params.bDistanceField = true;
		Params.EmTexels = 64.0f;
		Params.FieldSpreadTexels = 16.0f;
		Params.QuadMarginTexels = 1.0f;
		Params.TexelToUV = AtlasTexel;
		Params.BoldDilateEm = 0.05f;
		Params.FaceReachEm = 0.04f;
		return Params;
	}

	FVector2f Corner(const FDreamUIGeometry& Geometry, int32 Vertex)
	{
		return FVector2f(Geometry.OriginVertices[Vertex].Position.Y, Geometry.OriginVertices[Vertex].Position.Z);
	}

	/** Every vertex of the quad at Quad carries this code and this UV3.y, in white at this alpha. */
	bool IsColorCopy(const FDreamUIGeometry& Geometry, int32 Quad, float Code, uint8 Alpha)
	{
		for (int32 Vertex = Quad * 4; Vertex < Quad * 4 + 4; Vertex++)
		{
			const FDreamUIMeshVertex& V = Geometry.Vertices[Vertex];
			if (V.TextureCoordinate[2].X != Code || V.TextureCoordinate[3].Y != CellTexelsPerEm)return false;
			if (V.Color != FColor(255, 255, 255, Alpha))return false;
		}
		return true;
	}

	/** Layer of a field quad's code (DreamUIText_UnpackGlyphChannel). */
	int32 FieldLayer(float Code)
	{
		return FMath::FloorToInt32((Code + 8.0f) / 16.0f);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextColorGlyphCellTest,
	"DreamGUI.Text.ColorGlyph.AColourGlyphIsItsPaddedCellInWhiteKeepingItsAlpha",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The face of a colour glyph: its padded cell exactly where the layout put it, its texels exactly, the colour-face code,
 * the cell's texels per em in UV3.y, white at the item's alpha. Bold neither shifts nor dilates it; italic shears it like
 * any glyph; a tag's colour reaches it only as alpha. The plain glyphs beside it paint as ever.
 */
bool FDreamTextColorGlyphCellTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextColorGlyphTestLocal;

	const FDreamTextDisplayList DL = MakeLine();
	const FDreamTextPaintParams Params = MakeFieldParams();
	FDreamUIGeometry Geometry;
	TArray<FDreamUITextCharProperty> Chars;
	FDreamTextPainter::Paint(DL, Params, Geometry, Chars);
	if (!TestEqual(TEXT("one quad per glyph"), Geometry.OriginVertices.Num(), 3 * 4) || !TestEqual(TEXT("three characters"), Chars.Num(), 3))return false;

	const FDreamTextGlyphItem& Item = DL.Items[1];
	const FDreamUICharData& Cell = Item.Glyph;
	const float Left = Item.Pen.X + Cell.XOffset;
	const float Top = Item.Pen.Y + Cell.YOffset;
	TestEqual(TEXT("left-bottom corner is the cell's"), Corner(Geometry, 4), FVector2f(Left, Top - CellSize));
	TestEqual(TEXT("right-bottom corner is the cell's"), Corner(Geometry, 5), FVector2f(Left + CellSize, Top - CellSize));
	TestEqual(TEXT("left-top corner is the cell's"), Corner(Geometry, 6), FVector2f(Left, Top));
	TestEqual(TEXT("right-top corner is the cell's"), Corner(Geometry, 7), FVector2f(Left + CellSize, Top));
	TestEqual(TEXT("UVs are the cell's: left-bottom"), Geometry.Vertices[4].TextureCoordinate[0], Cell.GetUV0());
	TestEqual(TEXT("UVs are the cell's: right-bottom"), Geometry.Vertices[5].TextureCoordinate[0], Cell.GetUV1());
	TestEqual(TEXT("UVs are the cell's: left-top"), Geometry.Vertices[6].TextureCoordinate[0], Cell.GetUV2());
	TestEqual(TEXT("UVs are the cell's: right-top"), Geometry.Vertices[7].TextureCoordinate[0], Cell.GetUV3());
	TestEqual(TEXT("the cell's slice"), Geometry.Vertices[4].TextureCoordinate[1].Y, 2.0f);
	TestTrue(TEXT("the colour-face code, texels per em in UV3.y, white at the text's alpha"),
		IsColorCopy(Geometry, 1, DreamTextQuadCode::ColorFace, 128));
	TestEqual(TEXT("the character's range is its one quad"), Chars[1].VertCount, 4);

	// The plain glyph beside it: grown into the field by the face's reach, the field code, the text's colour.
	{
		const FDreamTextGlyphItem& Plain = DL.Items[0];
		TestTrue(TEXT("a plain glyph still grows into the field"), Corner(Geometry, 0).X < Plain.Pen.X + Plain.Glyph.XOffset);
		TestEqual(TEXT("and carries the field code (both layers, no dilation)"), Geometry.Vertices[0].TextureCoordinate[2].X, 2.0f * DreamTextQuadCode::FieldLayerStride);
		TestTrue(TEXT("in the text's colour"), Geometry.Vertices[0].Color == Params.BaseColor);
	}

	// Bold: no shift, no dilation in the code.
	{
		const FDreamTextDisplayList BoldLine = MakeLine(false, true);
		FDreamUIGeometry Bold;
		TArray<FDreamUITextCharProperty> BoldChars;
		FDreamTextPainter::Paint(BoldLine, Params, Bold, BoldChars);
		TestEqual(TEXT("bold does not move a colour glyph"), Corner(Bold, 4), Corner(Geometry, 4));
		TestTrue(TEXT("nor dilate it"), IsColorCopy(Bold, 1, DreamTextQuadCode::ColorFace, 128));
	}

	// Italic: sheared about the baseline like any glyph.
	{
		const FDreamTextDisplayList ItalicLine = MakeLine(true, false);
		FDreamUIGeometry Italic;
		TArray<FDreamUITextCharProperty> ItalicChars;
		FDreamTextPainter::Paint(ItalicLine, Params, Italic, ItalicChars);
		const float Baseline = Item.Pen.Y;
		TestEqual(TEXT("italic leans the bottom edge left by its depth below the baseline"), Corner(Italic, 4).X,
			Left - (Baseline - (Top - CellSize)) * Params.ItalicSlope, 1e-4f);
		TestEqual(TEXT("and the top edge right by its height above it"), Corner(Italic, 6).X, Left + (Top - Baseline) * Params.ItalicSlope, 1e-4f);
	}

	// A <color> tag's colour: only its alpha reaches the glyph, with the hierarchy's fade.
	{
		FDreamTextDisplayList Tagged = MakeLine();
		Tagged.Items[1].Style.bHasColor = true;
		Tagged.Items[1].Style.Color = FColor(10, 20, 30, 255);
		FDreamTextPaintParams Faded = Params;
		Faded.RichTextTagOpacity = 0.5f;
		FDreamUIGeometry TaggedGeometry;
		TArray<FDreamUITextCharProperty> TaggedChars;
		FDreamTextPainter::Paint(Tagged, Faded, TaggedGeometry, TaggedChars);
		TestTrue(TEXT("a tagged colour glyph stays white, at the tag's faded alpha"), IsColorCopy(TaggedGeometry, 1, DreamTextQuadCode::ColorFace, 128));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextColorGlyphEffectsCopyTest,
	"DreamGUI.Text.ColorGlyph.AColourGlyphGetsAnEffectsCopyOnlyUnderAnUnderlay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A text with effects draws every field glyph twice, effects then face. A colour glyph has no outline or glow to draw:
 * without an underlay it is its face alone; with one it gets an effects copy in the effects block, drawn before every
 * face, inset by half the field's spread so the shader's offset sample never leaves its cell.
 */
bool FDreamTextColorGlyphEffectsCopyTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextColorGlyphTestLocal;

	const FDreamTextDisplayList DL = MakeLine();
	FDreamTextPaintParams Params = MakeFieldParams();
	Params.bSeparateEffectLayer = true;
	Params.EffectReachEm = 0.2f;

	// An outline but no underlay: the colour glyph is one quad among pairs.
	{
		FDreamUIGeometry Geometry;
		TArray<FDreamUITextCharProperty> Chars;
		FDreamTextPainter::Paint(DL, Params, Geometry, Chars);
		if (!TestEqual(TEXT("two quads, one, two"), Geometry.OriginVertices.Num(), 5 * 4) || !TestEqual(TEXT("three characters"), Chars.Num(), 3))return false;
		TestEqual(TEXT("indices for five quads"), Geometry.Triangles.Num(), 5 * 6);
		TestTrue(TEXT("the colour glyph's range is its face alone"), Chars[1].StartVertIndex == 8 && Chars[1].VertCount == 4 && Chars[1].IndicesCount == 6);
		TestTrue(TEXT("which is a colour face"), IsColorCopy(Geometry, 2, DreamTextQuadCode::ColorFace, 128));
		// The effects block holds the two plain glyphs' effects quads and nothing else; then every face.
		bool bBlocks = true;
		for (int32 Index = 0; Index < Geometry.Triangles.Num(); Index++)
		{
			const float Code = Geometry.Vertices[Geometry.Triangles[Index]].TextureCoordinate[2].X;
			const bool bEffects = Code > DreamTextQuadCode::ColorThreshold && FieldLayer(Code) == 1;
			bBlocks &= Index < 2 * 6 ? bEffects : !bEffects;
		}
		TestTrue(TEXT("the plain glyphs' effects draw first, then every face"), bBlocks);
	}

	// An underlay: the colour glyph's effects copy joins the effects block.
	{
		Params.bHasUnderlay = true;
		FDreamUIGeometry Geometry;
		TArray<FDreamUITextCharProperty> Chars;
		FDreamTextPainter::Paint(DL, Params, Geometry, Chars);
		if (!TestEqual(TEXT("two quads each"), Geometry.OriginVertices.Num(), 6 * 4) || !TestEqual(TEXT("still three characters"), Chars.Num(), 3))return false;
		TestTrue(TEXT("the colour glyph's range covers both its copies"), Chars[1].StartVertIndex == 8 && Chars[1].VertCount == 8 && Chars[1].IndicesCount == 6);
		TestTrue(TEXT("its first copy is the effects copy, white at the text's alpha"), IsColorCopy(Geometry, 2, DreamTextQuadCode::ColorEffects, 128));
		TestTrue(TEXT("its second the face"), IsColorCopy(Geometry, 3, DreamTextQuadCode::ColorFace, 128));
		bool bBlocks = true;
		for (int32 Index = 0; Index < Geometry.Triangles.Num(); Index++)
		{
			const float Code = Geometry.Vertices[Geometry.Triangles[Index]].TextureCoordinate[2].X;
			const bool bEffects = Code < DreamTextQuadCode::ColorEffectsThreshold
				|| (Code > DreamTextQuadCode::ColorThreshold && FieldLayer(Code) == 1);
			bBlocks &= Index < 3 * 6 ? bEffects : !bEffects;
		}
		TestTrue(TEXT("every effects copy, the colour glyph's too, draws before any face"), bBlocks);

		// Inset by half the spread: 0.5 * 16 / 64 = 0.125 em, 6 of the cell's 48 texels per em, 4.5 of its 30 units.
		const FVector2f FaceLeftBottom = Corner(Geometry, 12);
		const FVector2f FaceRightTop = Corner(Geometry, 15);
		TestTrue(TEXT("the effects copy's left-bottom corner is inset"), Corner(Geometry, 8).Equals(FaceLeftBottom + FVector2f(4.5f, 4.5f), 1e-4f));
		TestTrue(TEXT("its right-top corner too"), Corner(Geometry, 11).Equals(FaceRightTop - FVector2f(4.5f, 4.5f), 1e-4f));
		const FVector2f FaceUV = Geometry.Vertices[12].TextureCoordinate[0];
		TestTrue(TEXT("and its texels with it"),
			Geometry.Vertices[8].TextureCoordinate[0].Equals(FaceUV + FVector2f(6.0f * AtlasTexel, -6.0f * AtlasTexel), 1e-6f));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextColorGlyphBitmapFontTest,
	"DreamGUI.Text.ColorGlyph.ABitmapFontDrawsNoOutlineAroundAColourGlyph",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A bitmap font's shadow and outline are copies of the glyph. Around a colour glyph the outline's eight taps are left
 * out; its shadow is one copy moved by the shadow's offset, coded as the colour glyph's effects copy -- which the shader
 * draws as the glyph's alpha in the shadow colour -- and white at the item's alpha, in the shadow block with the rest.
 */
bool FDreamTextColorGlyphBitmapFontTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextColorGlyphTestLocal;

	FDreamTextDisplayList DL;
	DL.Items.Add(MakeGlyph('a', 0, FVector2f(0.0f, -30.0f), 24.0f));
	DL.Items.Add(MakeColorGlyph(1, FVector2f(14.4f, -30.0f), 24.0f));

	FDreamTextPaintParams Params;
	Params.BaseColor = FColor::White;
	Params.BitmapShadowColor = FColor(0, 0, 0, 255);
	Params.BitmapShadowOffsetEm = FVector2f(0.1f, 0.1f);
	Params.BitmapOutlineColor = FColor(255, 0, 0, 255);
	Params.BitmapOutlineWidthEm = 0.05f;
	// What a text component may say of its style whatever the font; a bitmap font goes by its shadow.
	Params.bHasUnderlay = true;

	{
		FDreamUIGeometry Geometry;
		TArray<FDreamUITextCharProperty> Chars;
		FDreamTextPainter::Paint(DL, Params, Geometry, Chars);
		if (!TestEqual(TEXT("ten copies of the plain glyph, two of the colour glyph"), Geometry.OriginVertices.Num(), 12 * 4)
			|| !TestEqual(TEXT("two characters"), Chars.Num(), 2))
		{
			return false;
		}
		TestTrue(TEXT("the colour glyph's range is its two copies"), Chars[1].StartVertIndex == 40 && Chars[1].VertCount == 8);
		TestTrue(TEXT("its shadow copy carries the effects code, white at the item's alpha"), IsColorCopy(Geometry, 10, DreamTextQuadCode::ColorEffects, 255));
		TestTrue(TEXT("its face the face code"), IsColorCopy(Geometry, 11, DreamTextQuadCode::ColorFace, 255));
		TestTrue(TEXT("the shadow copy is moved by the offset (+y down in the style)"), Corner(Geometry, 40).Equals(Corner(Geometry, 44) + FVector2f(2.4f, -2.4f), 1e-4f));
		bool bPlainCodes = true;
		for (int32 Vertex = 0; Vertex < 40; Vertex++)
		{
			bPlainCodes &= Geometry.Vertices[Vertex].TextureCoordinate[2].X == 0.0f;
		}
		TestTrue(TEXT("the plain glyph's copies keep a bitmap font's zero code"), bPlainCodes);

		// Shadows (the plain glyph's and the colour glyph's), then the plain glyph's eight outline taps, then the faces.
		auto QuadAt = [&Geometry](int32 Index) { return (int32)Geometry.Triangles[Index] / 4; };
		bool bOrder = Geometry.Triangles.Num() == 12 * 6;
		for (int32 Block = 0; Block < 12 && bOrder; Block++)
		{
			const int32 Quad = QuadAt(Block * 6);
			if (Block == 0)bOrder &= Quad == 0;
			else if (Block == 1)bOrder &= Quad == 10;
			else if (Block < 10)bOrder &= Quad >= 1 && Quad <= 8;
			else if (Block == 10)bOrder &= Quad == 9;
			else bOrder &= Quad == 11;
		}
		TestTrue(TEXT("both shadows first, then the outline taps, then the faces"), bOrder);
	}

	// An outline and no shadow: the colour glyph is its face alone.
	{
		FDreamTextPaintParams OutlineOnly = Params;
		OutlineOnly.BitmapShadowColor = FColor(0, 0, 0, 0);
		FDreamUIGeometry Geometry;
		TArray<FDreamUITextCharProperty> Chars;
		FDreamTextPainter::Paint(DL, OutlineOnly, Geometry, Chars);
		TestEqual(TEXT("nine copies of the plain glyph, one of the colour glyph"), Geometry.OriginVertices.Num(), 10 * 4);
		TestTrue(TEXT("which is its face"), Geometry.OriginVertices.Num() == 40 && IsColorCopy(Geometry, 9, DreamTextQuadCode::ColorFace, 255));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextColorGlyphFillTest,
	"DreamGUI.Text.ColorGlyph.LyricFillRidesOnColourQuads",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Colour quads carry the lyric fill like every quad: the run's progress in UV3.x and their edges' place across the run in
 * UV2.y, the effects copy measured on its own inset edges. UV3.y is the cell's texels per em there, never the glow boost.
 */
bool FDreamTextColorGlyphFillTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextColorGlyphTestLocal;

	const FDreamTextDisplayList DL = MakeLine();
	TArray<FDreamTextFillSegment> Segments;
	FDreamTextFillSegment Segment;
	Segment.StartCharIndex = 0;
	Segment.EndCharIndex = 1;
	Segment.Progress = 0.3f;
	Segment.GlowBoost = 2.0f;
	Segments.Add(Segment);

	FDreamTextPaintParams Params = MakeFieldParams();
	Params.FaceReachEm = 0.0f;
	Params.FillSegments = &Segments;
	Params.FillProgress = 0.8f;
	Params.GlowBoost = 0.5f;
	Params.bSeparateEffectLayer = true;
	Params.bHasUnderlay = true;
	FDreamUIGeometry Geometry;
	TArray<FDreamUITextCharProperty> Chars;
	FDreamTextPainter::Paint(DL, Params, Geometry, Chars);
	if (!TestEqual(TEXT("two copies of every glyph"), Geometry.OriginVertices.Num(), 6 * 4))return false;

	// The segment's run: 'a' and the colour glyph's whole cell, measured on the layout's quads.
	const FDreamTextGlyphItem& A = DL.Items[0];
	const FDreamTextGlyphItem& Emoji = DL.Items[1];
	const float RunLeft = FMath::Min(A.Pen.X + A.Glyph.XOffset, Emoji.Pen.X + Emoji.Glyph.XOffset);
	const float RunRight = FMath::Max(A.Pen.X + A.Glyph.XOffset + A.Glyph.Width, Emoji.Pen.X + Emoji.Glyph.XOffset + Emoji.Glyph.Width);
	for (int32 Quad = 2; Quad <= 3; Quad++)
	{
		const FString What = Quad == 2 ? TEXT("the colour glyph's effects copy") : TEXT("the colour glyph's face");
		const FDreamUIMeshVertex& LeftVertex = Geometry.Vertices[Quad * 4];
		const FDreamUIMeshVertex& RightVertex = Geometry.Vertices[Quad * 4 + 1];
		TestEqual(*(What + TEXT(" carries the segment's progress")), LeftVertex.TextureCoordinate[3].X, 0.3f);
		TestEqual(*(What + TEXT(" carries texels per em, not the glow boost")), LeftVertex.TextureCoordinate[3].Y, CellTexelsPerEm);
		TestEqual(*(What + TEXT(": its left edge's place across the run")), LeftVertex.TextureCoordinate[2].Y,
			(Corner(Geometry, Quad * 4).X - RunLeft) / (RunRight - RunLeft), 1e-4f);
		TestEqual(*(What + TEXT(": its right edge's place across the run")), RightVertex.TextureCoordinate[2].Y,
			(Corner(Geometry, Quad * 4 + 1).X - RunLeft) / (RunRight - RunLeft), 1e-4f);
	}
	// The plain glyphs keep their run's progress and glow boost.
	TestEqual(TEXT("'a' carries the segment's glow boost"), Geometry.Vertices[0].TextureCoordinate[3].Y, 2.0f);
	TestEqual(TEXT("'b' carries the line's progress"), Geometry.Vertices[16].TextureCoordinate[3].X, 0.8f);
	TestEqual(TEXT("and the line's glow boost"), Geometry.Vertices[16].TextureCoordinate[3].Y, 0.5f);
	return true;
}

#endif
