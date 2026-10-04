// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Core/DreamUIGeometry.h"
#include "Core/DreamUITextData.h"
#include "Core/Text/DreamTextPaint.h"
#include "Core/Text/DreamTextPainter.h"

/*
 * Paints at the painter (FDreamTextPaints): the slot each quad's code carries, where each vertex sits in its paint's boxes
 * (UV4), the colours a painted face and the effects are written in, strokes cut where their paint changes, colour glyphs and
 * a bitmap font's copies left unpainted, the vertex-colour fallback, and a text that paints nothing writing exactly what it
 * wrote without paints. The display lists and their boxes are built by hand, every glyph's quad exactly its pen box, so
 * every number here follows from the rules on FDreamTextPaintParams, worked out again in the test; nothing needs a font.
 */
namespace DreamTextPainterPaintTestLocal
{
	FDreamTextBox MakeBox(float Left, float Right, float Bottom, float Top)
	{
		FDreamTextBox Box;
		Box.Left = Left;
		Box.Right = Right;
		Box.Bottom = Bottom;
		Box.Top = Top;
		return Box;
	}

	/** A glyph whose quad is its pen box: Advance wide from Pen.X, from Descent below its baseline to Ascent above it. */
	FDreamTextGlyphItem MakeBoxGlyph(uint32 Codepoint, int32 ElementIndex, int32 LineIndex, const FVector2f& Pen, float Advance)
	{
		const float Ascent = 9.0f;
		const float Descent = 3.0f;
		FDreamTextGlyphItem Item;
		Item.Kind = EDreamTextItemKind::Glyph;
		Item.Codepoint = Codepoint;
		Item.ElementIndex = ElementIndex;
		Item.SourceIndex = ElementIndex;
		Item.LineIndex = LineIndex;
		Item.Pen = Pen;
		Item.Glyph.Width = Advance;
		Item.Glyph.Height = Ascent + Descent;
		Item.Glyph.XOffset = 0.0f;
		Item.Glyph.YOffset = Ascent;
		Item.Glyph.XAdvance = Advance;
		const float CellU = (float)(Codepoint % 32u) / 32.0f;
		Item.Glyph.MinUV = FVector2f(CellU, 0.5f);
		Item.Glyph.MaxUV = FVector2f(CellU + 1.0f / 64.0f, 0.5f + 1.0f / 64.0f);
		Item.Glyph.SliceIndex = 1;
		Item.Glyph.GlyphIndex = Codepoint;
		Item.GlyphSize = 12.0f;
		Item.AdvanceWithSpace = Advance;
		Item.DecorationOffset = 0.0f;
		Item.Ascent = Ascent;
		Item.Descent = Descent;
		Item.Style.Size = 12.0f;
		Item.bEmit = true;
		Item.bCountsAsVisible = true;
		return Item;
	}

	/**
	 * "abc" on line 0, its baseline at 0, and "de" on line 1, at -15, each glyph 8 wide from x = 10. Each line's box spans its
	 * glyphs' pen boxes across and their ascent to descent down; the block runs from the content box's left (10) to its
	 * right (34) and from the first line's top to the last one's bottom; the content box is larger.
	 */
	FDreamTextDisplayList MakeTwoLines()
	{
		FDreamTextDisplayList DL;
		DL.Lines.AddDefaulted(2);
		for (int32 Index = 0; Index < 3; Index++)
		{
			DL.Items.Add(MakeBoxGlyph('a' + Index, Index, 0, FVector2f(10.0f + 8.0f * Index, 0.0f), 8.0f));
		}
		for (int32 Index = 0; Index < 2; Index++)
		{
			DL.Items.Add(MakeBoxGlyph('d' + Index, 4 + Index, 1, FVector2f(10.0f + 8.0f * Index, -15.0f), 8.0f));
		}
		DL.LineBoxes.Add(MakeBox(10.0f, 34.0f, -3.0f, 9.0f));
		DL.LineBoxes.Add(MakeBox(10.0f, 26.0f, -18.0f, -6.0f));
		DL.TextBlockBox = MakeBox(10.0f, 34.0f, -18.0f, 9.0f);
		DL.ContentBox = MakeBox(0.0f, 50.0f, -30.0f, 12.0f);
		return DL;
	}

	/** A multi-channel field font with no effects and no growth: every glyph's face quad is its pen box. */
	FDreamTextPaintParams MakeParams()
	{
		FDreamTextPaintParams Params;
		Params.ItalicSlope = 0.25f;
		Params.BaseColor = FColor(20, 30, 40, 255);
		Params.bDistanceField = true;
		Params.EmTexels = 64.0f;
		Params.FieldSpreadTexels = 16.0f;
		Params.QuadMarginTexels = 1.0f;
		Params.TexelToUV = 1.0f / 2048.0f;
		Params.PaintBaseColor = FColor(250, 240, 230, 255);
		return Params;
	}

	/** Red on the left to blue on the right. */
	FDreamGradient MakeGradient()
	{
		FDreamGradient Gradient;
		Gradient.Type = EDreamPaintType::Linear;
		Gradient.Angle = 90.0f;
		Gradient.Stops.Add(FDreamGradientStop(0.0f, FColor(255, 0, 0, 255)));
		Gradient.Stops.Add(FDreamGradientStop(1.0f, FColor(0, 0, 255, 255)));
		return Gradient;
	}

	/** The text's own slot paints its face with Gradient, measured across InAcross and down InDown. */
	void PaintOwnFace(FDreamTextPaintParams& Params, const FDreamGradient& Gradient, EDreamTextPaintBox InAcross, EDreamTextPaintBox InDown)
	{
		FDreamTextPaintSlot& Slot = Params.Paints.Slots[DreamTextQuadCode::TextSlot];
		Slot.Face = &Gradient;
		Slot.HorizontalBox = InAcross;
		Slot.VerticalBox = InDown;
	}

	FVector2f Corner(const FDreamUIGeometry& Geometry, int32 Vertex)
	{
		return FVector2f(Geometry.OriginVertices[Vertex].Position.Y, Geometry.OriginVertices[Vertex].Position.Z);
	}

	/** Where a local point sits in a box as UV4 measures it: u from its left edge, v from its top. */
	FVector2f InBox(const FDreamTextBox& Box, const FVector2f& Point)
	{
		return FVector2f((Point.X - Box.Left) / Box.GetWidth(), (Box.Top - Point.Y) / Box.GetHeight());
	}

	int32 SlotOf(const FDreamUIGeometry& Geometry, int32 Quad)
	{
		return DreamTextQuadCode::GetSlot(Geometry.Vertices[Quad * 4].TextureCoordinate[2].X);
	}

	float CodeOf(const FDreamUIGeometry& Geometry, int32 Quad)
	{
		return DreamTextQuadCode::StripSlot(Geometry.Vertices[Quad * 4].TextureCoordinate[2].X);
	}

	/** Every vertex of the quad carries Slot on its code and the UV4 Box puts its corner at (within Tolerance). */
	bool QuadIsMeasuredIn(const FDreamUIGeometry& Geometry, int32 Quad, int32 Slot, const FDreamTextBox& Box, float Tolerance = 1e-5f)
	{
		for (int32 Vertex = Quad * 4; Vertex < Quad * 4 + 4; Vertex++)
		{
			if (DreamTextQuadCode::GetSlot(Geometry.Vertices[Vertex].TextureCoordinate[2].X) != Slot)return false;
			if (!Geometry.Vertices[Vertex].UV4.Equals(InBox(Box, Corner(Geometry, Vertex)), Tolerance))return false;
		}
		return true;
	}

	/** The quad paints nothing: slot 0 on every vertex and UV4 (0, 0). */
	bool QuadPaintsNothing(const FDreamUIGeometry& Geometry, int32 Quad)
	{
		for (int32 Vertex = Quad * 4; Vertex < Quad * 4 + 4; Vertex++)
		{
			if (DreamTextQuadCode::GetSlot(Geometry.Vertices[Vertex].TextureCoordinate[2].X) != 0)return false;
			if (Geometry.Vertices[Vertex].UV4 != FVector2f::ZeroVector)return false;
		}
		return true;
	}

	/** Every byte the painter is responsible for, vertex for vertex and index for index. */
	bool SameBytes(const FDreamUIGeometry& A, const FDreamUIGeometry& B)
	{
		if (A.OriginVertices.Num() != B.OriginVertices.Num() || A.Vertices.Num() != B.Vertices.Num() || A.Triangles.Num() != B.Triangles.Num())
		{
			return false;
		}
		return FMemory::Memcmp(A.OriginVertices.GetData(), B.OriginVertices.GetData(), A.OriginVertices.Num() * sizeof(FDreamUIOriginVertexData)) == 0
			&& FMemory::Memcmp(A.Vertices.GetData(), B.Vertices.GetData(), A.Vertices.Num() * sizeof(FDreamUIMeshVertex)) == 0
			&& FMemory::Memcmp(A.Triangles.GetData(), B.Triangles.GetData(), A.Triangles.Num() * sizeof(FDreamUIMeshIndex)) == 0;
	}

	/** A solid underline under the item: half a unit thick, its top a unit under the baseline, one texel of the atlas. */
	void Underline(FDreamTextGlyphItem& Item)
	{
		Item.Style.bUnderline = true;
		Item.UnderlineGlyph.YOffset = -1.0f;
		Item.UnderlineGlyph.Height = 0.5f;
		Item.UnderlineGlyph.MinUV = Item.UnderlineGlyph.MaxUV = FVector2f(0.5f, 0.5f);
	}

	/** A colour glyph (an emoji from a colour face) on the pen: its quad its whole padded cell, 12 units square. */
	FDreamTextGlyphItem MakeColorGlyph(int32 ElementIndex, const FVector2f& Pen)
	{
		FDreamTextGlyphItem Item = MakeBoxGlyph(0x1F600, ElementIndex, 0, Pen, 12.0f);
		Item.Glyph.bColor = true;
		Item.Glyph.FaceIndex = 1;
		Item.Glyph.ColorTexelsPerEm = 48.0f;
		Item.Glyph.MinUV = FVector2f(0.5f, 0.25f);
		Item.Glyph.MaxUV = Item.Glyph.MinUV + FVector2f(40.0f / 2048.0f, 40.0f / 2048.0f);
		return Item;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextPainterUV4BoxesTest,
	"DreamGUI.Text.Painter.UV4RunsFromZeroToOneAcrossEachLineAndAcrossTheBlock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A painted face quad's UV4 is each of its vertices' place in its slot's boxes: across a Line box a line's first glyph
 * starts at 0 and its last ends at 1, and every glyph's top is at 0 and its bottom at 1; across the TextBlock the same
 * holds for the block's edges, the first line's top and the last line's bottom; a ContentBox and Glyph boxes measure by
 * their own edges. Every painted quad carries slot 1 on its code, the code without it unchanged.
 */
bool FDreamTextPainterUV4BoxesTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextPainterPaintTestLocal;

	const FDreamTextDisplayList DL = MakeTwoLines();
	const FDreamGradient Gradient = MakeGradient();
	auto PaintIn = [&DL, &Gradient](EDreamTextPaintBox InPaintBox, FDreamUIGeometry& OutGeometry)
	{
		FDreamTextPaintParams Params = MakeParams();
		PaintOwnFace(Params, Gradient, InPaintBox, InPaintBox);
		TArray<FDreamUITextCharProperty> Chars;
		FDreamTextPainter::Paint(DL, Params, OutGeometry, Chars);
	};

	{
		FDreamUIGeometry Geometry;
		PaintIn(EDreamTextPaintBox::Line, Geometry);
		if (!TestEqual(TEXT("line boxes: one quad per glyph"), Geometry.Vertices.Num(), 5 * 4))return false;
		for (int32 Quad = 0; Quad < 5; Quad++)
		{
			TestTrue(*FString::Printf(TEXT("line boxes: glyph %d is measured in its line's box, in slot 1"), Quad),
				QuadIsMeasuredIn(Geometry, Quad, DreamTextQuadCode::TextSlot, DL.LineBoxes[DL.Items[Quad].LineIndex]));
			TestEqual(*FString::Printf(TEXT("line boxes: glyph %d keeps its code under the slot (both layers, no dilation)"), Quad),
				CodeOf(Geometry, Quad), 2.0f * DreamTextQuadCode::FieldLayerStride);
		}
		// Vertices: 0 bottom-left, 1 bottom-right, 2 top-left, 3 top-right.
		TestEqual(TEXT("line 0's first glyph starts at u 0"), Geometry.Vertices[0].UV4.X, 0.0f);
		TestEqual(TEXT("line 0's last glyph ends at u 1"), Geometry.Vertices[2 * 4 + 1].UV4.X, 1.0f);
		TestEqual(TEXT("line 1's first glyph starts at u 0"), Geometry.Vertices[3 * 4].UV4.X, 0.0f);
		TestEqual(TEXT("line 1's last glyph ends at u 1"), Geometry.Vertices[4 * 4 + 3].UV4.X, 1.0f);
		TestEqual(TEXT("a glyph's top is at v 0"), Geometry.Vertices[4 * 4 + 2].UV4.Y, 0.0f);
		TestEqual(TEXT("and its bottom at v 1"), Geometry.Vertices[4 * 4].UV4.Y, 1.0f);
	}

	{
		FDreamUIGeometry Geometry;
		PaintIn(EDreamTextPaintBox::TextBlock, Geometry);
		if (!TestEqual(TEXT("text block: one quad per glyph"), Geometry.Vertices.Num(), 5 * 4))return false;
		for (int32 Quad = 0; Quad < 5; Quad++)
		{
			TestTrue(*FString::Printf(TEXT("text block: glyph %d is measured in the block"), Quad),
				QuadIsMeasuredIn(Geometry, Quad, DreamTextQuadCode::TextSlot, DL.TextBlockBox));
		}
		TestEqual(TEXT("the block's left edge is at u 0"), Geometry.Vertices[0].UV4.X, 0.0f);
		TestEqual(TEXT("its right edge, the end of the longer line, at u 1"), Geometry.Vertices[2 * 4 + 1].UV4.X, 1.0f);
		TestEqual(TEXT("the shorter line ends two thirds across"), Geometry.Vertices[4 * 4 + 1].UV4.X, 16.0f / 24.0f, 1e-6f);
		TestEqual(TEXT("the first line's top is at v 0"), Geometry.Vertices[2].UV4.Y, 0.0f);
		TestEqual(TEXT("the last line's bottom at v 1"), Geometry.Vertices[3 * 4].UV4.Y, 1.0f);
		TestEqual(TEXT("the first line's bottom 12 of the block's 27 down"), Geometry.Vertices[0].UV4.Y, 12.0f / 27.0f, 1e-6f);
	}

	{
		FDreamUIGeometry Geometry;
		PaintIn(EDreamTextPaintBox::ContentBox, Geometry);
		bool bAll = Geometry.Vertices.Num() == 5 * 4;
		for (int32 Quad = 0; Quad < 5 && bAll; Quad++)
		{
			bAll &= QuadIsMeasuredIn(Geometry, Quad, DreamTextQuadCode::TextSlot, DL.ContentBox);
		}
		TestTrue(TEXT("content box: every glyph is measured in the content box"), bAll);
		TestEqual(TEXT("content box: the first glyph starts a fifth across"), Geometry.Vertices[0].UV4.X, 0.2f, 1e-6f);
	}

	{
		FDreamUIGeometry Geometry;
		PaintIn(EDreamTextPaintBox::Glyph, Geometry);
		bool bAll = Geometry.Vertices.Num() == 5 * 4;
		for (int32 Quad = 0; Quad < 5 && bAll; Quad++)
		{
			const FDreamTextGlyphItem& Item = DL.Items[Quad];
			const FDreamTextBox GlyphBox = MakeBox(Item.Pen.X, Item.Pen.X + Item.AdvanceWithSpace, Item.Pen.Y - Item.Descent, Item.Pen.Y + Item.Ascent);
			bAll &= QuadIsMeasuredIn(Geometry, Quad, DreamTextQuadCode::TextSlot, GlyphBox);
			bAll &= Geometry.Vertices[Quad * 4].UV4 == FVector2f(0.0f, 1.0f) && Geometry.Vertices[Quad * 4 + 3].UV4 == FVector2f(1.0f, 0.0f);
		}
		TestTrue(TEXT("glyph boxes: every glyph runs from (0, 1) at its bottom left to (1, 0) at its top right"), bAll);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextPainterItalicUV4Test,
	"DreamGUI.Text.Painter.AnItalicQuadIsMeasuredWhereItsShearPutsIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * UV4 comes from a quad's final corners: an italic glyph's are sheared about its baseline, so its top corners sit further
 * right in the gradient than its bottom ones -- the gradient itself is not sheared, as in CSS.
 */
bool FDreamTextPainterItalicUV4Test::RunTest(const FString& Parameters)
{
	using namespace DreamTextPainterPaintTestLocal;

	FDreamTextDisplayList DL = MakeTwoLines();
	DL.Items[1].Style.bItalic = DL.Items[1].Style.bSyntheticItalic = true;
	const FDreamGradient Gradient = MakeGradient();
	FDreamTextPaintParams Params = MakeParams();
	PaintOwnFace(Params, Gradient, EDreamTextPaintBox::TextBlock, EDreamTextPaintBox::TextBlock);
	FDreamUIGeometry Geometry;
	TArray<FDreamUITextCharProperty> Chars;
	FDreamTextPainter::Paint(DL, Params, Geometry, Chars);
	if (!TestEqual(TEXT("one quad per glyph"), Geometry.Vertices.Num(), 5 * 4))return false;

	// 'b' at x 18: its top 9 above the baseline, its bottom 3 below.
	TestEqual(TEXT("the top edge leans right by its height times the slope"), Corner(Geometry, 6).X, 18.0f + 9.0f * Params.ItalicSlope, 1e-5f);
	TestEqual(TEXT("the bottom edge leans left by its depth times the slope"), Corner(Geometry, 4).X, 18.0f - 3.0f * Params.ItalicSlope, 1e-5f);
	TestTrue(TEXT("every vertex is measured at its sheared corner"), QuadIsMeasuredIn(Geometry, 1, DreamTextQuadCode::TextSlot, DL.TextBlockBox));
	TestEqual(TEXT("so the top-left corner sits the shear further across than the bottom-left"),
		Geometry.Vertices[6].UV4.X - Geometry.Vertices[4].UV4.X, 12.0f * Params.ItalicSlope / 24.0f, 1e-5f);
	TestEqual(TEXT("an upright glyph's left edge stays at one u"), Geometry.Vertices[2].UV4.X, Geometry.Vertices[0].UV4.X);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextPainterSlotRuleTest,
	"DreamGUI.Text.Painter.APaintedFaceIsWrittenInThePaintColourAndASolidOneInItsOwn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The per-item rule of FDreamTextPaintParams::Paints and the colours that go with it: a face its slot paints is written in
 * PaintBaseColor, times a custom style's pending Multiply -- never in the text's own colour; a <color> run, a hovered link
 * and an item a custom style took the paints off are solid in their own colours at slot 0; a tag paint whose name resolved
 * paints with its slot, one that did not falls back to the text's own paints (or to the item's <color>). A text whose
 * slot paints only the overlay keeps every face in its own colour, in slot 1.
 */
bool FDreamTextPainterSlotRuleTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextPainterPaintTestLocal;

	FDreamTextDisplayList DL;
	DL.Lines.AddDefaulted(1);
	for (int32 Index = 0; Index < 8; Index++)
	{
		DL.Items.Add(MakeBoxGlyph('a' + Index, Index, 0, FVector2f(8.0f * Index, 0.0f), 8.0f));
	}
	DL.LineBoxes.Add(MakeBox(0.0f, 64.0f, -3.0f, 9.0f));
	DL.TextBlockBox = DL.LineBoxes[0];
	// 'b' meets a custom style's Multiply, 'c' has a <color>, 'd' is a link, 'e' lost its paints to a custom style, 'f' and
	// 'g' are tag paints -- 'f''s name resolved to slot 2, 'g''s to nothing -- and 'h' has a tag paint that did not resolve
	// inside a <color>.
	DL.Items[1].Style.bHasMultiplyColor = true;
	DL.Items[1].Style.MultiplyColor = FColor(128, 255, 255, 255);
	DL.Items[2].Style.bHasColor = true;
	DL.Items[2].Style.Color = FColor(10, 20, 30, 200);
	DL.CustomTagElementRanges.Add(FIntPoint(3, 3));
	DL.Items[4].Style.bPaintRemoved = true;
	DL.PaintNames.Add(TEXT("Gold"));
	DL.PaintNames.Add(TEXT("Nowhere"));
	DL.Items[5].Style.PaintIndex = 0;
	DL.Items[6].Style.PaintIndex = 1;
	DL.Items[7].Style.PaintIndex = 1;
	DL.Items[7].Style.bHasColor = true;
	DL.Items[7].Style.Color = FColor(60, 70, 80, 255);

	const FDreamGradient Gradient = MakeGradient();
	const FDreamGradient TagGradient = MakeGradient();
	const TArray<uint8> NameSlots = { (uint8)DreamTextQuadCode::FirstTagSlot, 0 };
	const TArray<TPair<int32, FColor>> Overrides = { TPair<int32, FColor>(0, FColor(90, 100, 110, 255)) };
	FDreamTextPaintParams Params = MakeParams();
	Params.RichTextTagOpacity = 0.5f;
	Params.TagColorOverrides = &Overrides;
	PaintOwnFace(Params, Gradient, EDreamTextPaintBox::TextBlock, EDreamTextPaintBox::TextBlock);
	FDreamTextPaintSlot& TagSlot = Params.Paints.Slots[DreamTextQuadCode::FirstTagSlot];
	TagSlot.Face = &TagGradient;
	TagSlot.HorizontalBox = TagSlot.VerticalBox = EDreamTextPaintBox::Line;
	Params.Paints.NameSlots = &NameSlots;

	FDreamUIGeometry Geometry;
	TArray<FDreamUITextCharProperty> Chars;
	FDreamTextPainter::Paint(DL, Params, Geometry, Chars);
	if (!TestEqual(TEXT("one quad per glyph"), Geometry.Vertices.Num(), 8 * 4))return false;

	struct FExpect { int32 Slot; FColor Color; const TCHAR* What; };
	const FExpect Expected[] =
	{
		{ 1, Params.PaintBaseColor, TEXT("a plain glyph paints with the text's slot, in the paint colour") },
		{ 1, FColor(125, 240, 230, 255), TEXT("a custom style's Multiply multiplies the paint colour") },
		{ 0, FColor(10, 20, 30, 100), TEXT("a <color> run is solid, in its colour at the faded alpha") },
		{ 0, FColor(90, 100, 110, 128), TEXT("a hovered link is solid, in its override colour") },
		{ 0, Params.BaseColor, TEXT("an item whose paints a custom style took off is solid, in the text's colour") },
		{ DreamTextQuadCode::FirstTagSlot, Params.PaintBaseColor, TEXT("a tag paint that resolved paints with its own slot") },
		{ 1, Params.PaintBaseColor, TEXT("a tag paint that resolved to nothing takes the text's own paints") },
		{ 0, FColor(60, 70, 80, 128), TEXT("inside a <color>, a tag paint that resolved to nothing is the <color>") },
	};
	for (int32 Quad = 0; Quad < 8; Quad++)
	{
		const FString What = FString::Printf(TEXT("'%c': %s"), (TCHAR)('a' + Quad), Expected[Quad].What);
		TestEqual(*(What + TEXT(" (slot)")), SlotOf(Geometry, Quad), Expected[Quad].Slot);
		TestTrue(*(What + TEXT(" (colour)")), Geometry.Vertices[Quad * 4].Color == Expected[Quad].Color);
		if (Expected[Quad].Slot == 0)
		{
			TestTrue(*(What + TEXT(" (no UV4)")), QuadPaintsNothing(Geometry, Quad));
		}
		else
		{
			const FDreamTextBox& Box = Expected[Quad].Slot == DreamTextQuadCode::TextSlot ? DL.TextBlockBox : DL.LineBoxes[0];
			TestTrue(*(What + TEXT(" (UV4 in its slot's boxes)")), QuadIsMeasuredIn(Geometry, Quad, Expected[Quad].Slot, Box));
		}
	}

	// The overlay alone: faces keep their own colours, and still take the slot the overlay is drawn from.
	{
		FDreamTextPaintParams Overlay = MakeParams();
		Overlay.Paints.Slots[DreamTextQuadCode::TextSlot].Overlay = &Gradient;
		FDreamUIGeometry OverlayGeometry;
		TArray<FDreamUITextCharProperty> OverlayChars;
		FDreamTextPainter::Paint(DL, Overlay, OverlayGeometry, OverlayChars);
		if (!TestEqual(TEXT("overlay: one quad per glyph"), OverlayGeometry.Vertices.Num(), 8 * 4))return false;
		TestEqual(TEXT("overlay: a plain glyph takes slot 1"), SlotOf(OverlayGeometry, 0), DreamTextQuadCode::TextSlot);
		TestTrue(TEXT("overlay: in the text's own colour"), OverlayGeometry.Vertices[0].Color == Overlay.BaseColor);
		TestTrue(TEXT("overlay: measured in the text's boxes"), QuadIsMeasuredIn(OverlayGeometry, 0, DreamTextQuadCode::TextSlot, DL.TextBlockBox));
		TestEqual(TEXT("overlay: a <color> run is still solid"), SlotOf(OverlayGeometry, 2), 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextPainterEffectsAlphaTest,
	"DreamGUI.Text.Painter.EffectsFadeWithTheEffectOpacityNotWithTheTextColour",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Every effects copy -- a field's effects layer, a colour glyph's underlay copy, a bitmap font's shadow and outline copies --
 * is written at EffectOpacity (the bitmap copies at their own colour's alpha times it), never at the alpha of the text's or
 * a tag's colour: a text with a clear face still draws its outline, and a faded one fades its effects with its face.
 */
bool FDreamTextPainterEffectsAlphaTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextPainterPaintTestLocal;

	// A field with effects and an underlay, its face clear: 'a', a colour glyph, 'b' in an opaque <color>.
	{
		FDreamTextDisplayList DL;
		DL.Lines.AddDefaulted(1);
		DL.Items.Add(MakeBoxGlyph('a', 0, 0, FVector2f(0.0f, 0.0f), 8.0f));
		DL.Items.Add(MakeColorGlyph(1, FVector2f(8.0f, 0.0f)));
		FDreamTextGlyphItem Tagged = MakeBoxGlyph('b', 2, 0, FVector2f(20.0f, 0.0f), 8.0f);
		Tagged.Style.bHasColor = true;
		Tagged.Style.Color = FColor(200, 100, 50, 255);
		DL.Items.Add(Tagged);

		FDreamTextPaintParams Params = MakeParams();
		Params.BaseColor = FColor(20, 30, 40, 0);
		Params.bSeparateEffectLayer = true;
		Params.EffectReachEm = 0.1f;
		Params.bHasUnderlay = true;
		Params.EffectOpacity = 0.5f;
		FDreamUIGeometry Geometry;
		TArray<FDreamUITextCharProperty> Chars;
		FDreamTextPainter::Paint(DL, Params, Geometry, Chars);
		if (!TestEqual(TEXT("field: two quads a glyph"), Geometry.Vertices.Num(), 6 * 4))return false;
		TestEqual(TEXT("field: the clear glyph's effects copy is at the effect opacity"), Geometry.Vertices[0].Color.A, (uint8)128);
		TestEqual(TEXT("field: its face is clear"), Geometry.Vertices[4].Color.A, (uint8)0);
		TestEqual(TEXT("field: the colour glyph's underlay copy is at the effect opacity"), Geometry.Vertices[8].Color.A, (uint8)128);
		TestTrue(TEXT("field: and white"), Geometry.Vertices[8].Color == FColor(255, 255, 255, 128));
		TestEqual(TEXT("field: the colour glyph's face keeps the item's alpha"), Geometry.Vertices[12].Color.A, (uint8)0);
		TestEqual(TEXT("field: the <color> glyph's effects copy is at the effect opacity too"), Geometry.Vertices[16].Color.A, (uint8)128);
		TestEqual(TEXT("field: while its face is the tag's"), Geometry.Vertices[20].Color.A, (uint8)255);
		TestEqual(TEXT("field: an effects copy keeps its face's colour"), Geometry.Vertices[16].Color.R, (uint8)200);
	}

	// A bitmap font: the shadow and the outline at their own alpha times the effect opacity, the glyph's alpha left out.
	{
		FDreamTextDisplayList DL;
		DL.Lines.AddDefaulted(1);
		DL.Items.Add(MakeBoxGlyph('a', 0, 0, FVector2f(0.0f, 0.0f), 8.0f));
		FDreamTextPaintParams Params;
		Params.BaseColor = FColor(255, 255, 255, 30);
		Params.BitmapShadowColor = FColor(0, 0, 0, 200);
		Params.BitmapShadowOffsetEm = FVector2f(0.1f, 0.1f);
		Params.BitmapOutlineColor = FColor(255, 0, 0, 100);
		Params.BitmapOutlineWidthEm = 0.05f;
		Params.EffectOpacity = 0.5f;
		FDreamUIGeometry Geometry;
		TArray<FDreamUITextCharProperty> Chars;
		FDreamTextPainter::Paint(DL, Params, Geometry, Chars);
		if (!TestEqual(TEXT("bitmap: a shadow, eight taps and a face"), Geometry.Vertices.Num(), 10 * 4))return false;
		TestTrue(TEXT("bitmap: the shadow at its alpha times the effect opacity"), Geometry.Vertices[0].Color == FColor(0, 0, 0, 100));
		bool bTaps = true;
		for (int32 Quad = 1; Quad <= 8; Quad++)
		{
			bTaps &= Geometry.Vertices[Quad * 4].Color == FColor(255, 0, 0, 50);
		}
		TestTrue(TEXT("bitmap: every outline tap at its alpha times the effect opacity"), bTaps);
		TestEqual(TEXT("bitmap: the face at the text's alpha"), Geometry.Vertices[9 * 4].Color.A, (uint8)30);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextPainterUnpaintedCopiesTest,
	"DreamGUI.Text.Painter.ColourGlyphsAndABitmapFontsCopiesPaintNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Slot 0 always: a colour glyph's copies keep their codes (-17 for the underlay, -1 for the face) and UV4 (0, 0) in a painted
 * text, while the field glyphs around them carry slot 1 on their face and -- the outline being painted -- on their effects
 * copy; a bitmap font's shadow and outline copies paint nothing either, only its faces do.
 */
bool FDreamTextPainterUnpaintedCopiesTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextPainterPaintTestLocal;

	const FDreamGradient Gradient = MakeGradient();
	FDreamTextDisplayList DL;
	DL.Lines.AddDefaulted(1);
	DL.Items.Add(MakeBoxGlyph('a', 0, 0, FVector2f(0.0f, 0.0f), 8.0f));
	DL.Items.Add(MakeColorGlyph(1, FVector2f(8.0f, 0.0f)));
	DL.Items.Add(MakeBoxGlyph('b', 2, 0, FVector2f(20.0f, 0.0f), 8.0f));
	DL.LineBoxes.Add(MakeBox(0.0f, 28.0f, -3.0f, 9.0f));
	DL.TextBlockBox = DL.LineBoxes[0];

	{
		FDreamTextPaintParams Params = MakeParams();
		Params.bSeparateEffectLayer = true;
		Params.EffectReachEm = 0.1f;
		Params.bHasUnderlay = true;
		PaintOwnFace(Params, Gradient, EDreamTextPaintBox::TextBlock, EDreamTextPaintBox::TextBlock);
		Params.Paints.Slots[DreamTextQuadCode::TextSlot].Outline = &Gradient;
		FDreamUIGeometry Geometry;
		TArray<FDreamUITextCharProperty> Chars;
		FDreamTextPainter::Paint(DL, Params, Geometry, Chars);
		if (!TestEqual(TEXT("field: two quads a glyph"), Geometry.Vertices.Num(), 6 * 4))return false;
		for (int32 Glyph = 0; Glyph < 3; Glyph += 2)
		{
			const FString What = FString::Printf(TEXT("field: '%c'"), Glyph == 0 ? TEXT('a') : TEXT('b'));
			TestTrue(*(What + TEXT("'s effects copy paints with slot 1")), QuadIsMeasuredIn(Geometry, Glyph * 2, DreamTextQuadCode::TextSlot, DL.TextBlockBox));
			TestEqual(*(What + TEXT("'s effects copy keeps its effects code")), CodeOf(Geometry, Glyph * 2), DreamTextQuadCode::FieldLayerStride);
			TestTrue(*(What + TEXT("'s face paints with slot 1")), QuadIsMeasuredIn(Geometry, Glyph * 2 + 1, DreamTextQuadCode::TextSlot, DL.TextBlockBox));
			TestEqual(*(What + TEXT("'s face keeps its face code")), CodeOf(Geometry, Glyph * 2 + 1), 0.0f);
		}
		TestTrue(TEXT("field: the colour glyph's underlay copy paints nothing"), QuadPaintsNothing(Geometry, 2));
		TestEqual(TEXT("field: and is the colour effects code"), Geometry.Vertices[8].TextureCoordinate[2].X, DreamTextQuadCode::ColorEffects);
		TestTrue(TEXT("field: the colour glyph's face paints nothing"), QuadPaintsNothing(Geometry, 3));
		TestEqual(TEXT("field: and is the colour face code"), Geometry.Vertices[12].TextureCoordinate[2].X, DreamTextQuadCode::ColorFace);
	}

	{
		FDreamTextDisplayList Plain = DL;
		Plain.Items.RemoveAt(1);
		FDreamTextPaintParams Params;
		Params.BitmapShadowColor = FColor(0, 0, 0, 255);
		Params.BitmapShadowOffsetEm = FVector2f(0.1f, 0.1f);
		Params.BitmapOutlineColor = FColor(255, 0, 0, 255);
		Params.BitmapOutlineWidthEm = 0.05f;
		Params.PaintBaseColor = FColor(250, 240, 230, 255);
		PaintOwnFace(Params, Gradient, EDreamTextPaintBox::TextBlock, EDreamTextPaintBox::TextBlock);
		FDreamUIGeometry Geometry;
		TArray<FDreamUITextCharProperty> Chars;
		FDreamTextPainter::Paint(Plain, Params, Geometry, Chars);
		if (!TestEqual(TEXT("bitmap: ten copies a glyph"), Geometry.Vertices.Num(), 2 * 10 * 4))return false;
		bool bCopies = true;
		bool bFaces = true;
		for (int32 Quad = 0; Quad < 20; Quad++)
		{
			// A glyph's quads are its shadow, its eight taps, then its face.
			if (Quad % 10 == 9)
			{
				bFaces &= QuadIsMeasuredIn(Geometry, Quad, DreamTextQuadCode::TextSlot, Plain.TextBlockBox) && CodeOf(Geometry, Quad) == 0.0f;
			}
			else
			{
				bCopies &= QuadPaintsNothing(Geometry, Quad) && Geometry.Vertices[Quad * 4].TextureCoordinate[2].X == 0.0f;
			}
		}
		TestTrue(TEXT("bitmap: every shadow and outline copy paints nothing"), bCopies);
		TestTrue(TEXT("bitmap: every face paints with slot 1"), bFaces);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextPainterStrokeSplitTest,
	"DreamGUI.Text.Painter.AStrokeIsCutWhereItsPaintChanges",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * An underline is one strip per run of matching decoration, and a painted one is measured in one box: a strip is cut where
 * its items' slot changes (an item a custom style took the paints off, in the same colour as the paint's), into a piece per
 * character under a Glyph box, and where a tag run's piece changes under a Run box. Without paints the same underline is
 * one strip.
 */
bool FDreamTextPainterStrokeSplitTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextPainterPaintTestLocal;

	FDreamTextDisplayList DL;
	DL.Lines.AddDefaulted(1);
	for (int32 Index = 0; Index < 4; Index++)
	{
		FDreamTextGlyphItem Item = MakeBoxGlyph('a' + Index, Index, 0, FVector2f(10.0f + 8.0f * Index, 0.0f), 8.0f);
		Underline(Item);
		DL.Items.Add(Item);
	}
	DL.LineBoxes.Add(MakeBox(10.0f, 42.0f, -3.0f, 9.0f));
	DL.TextBlockBox = DL.LineBoxes[0];
	const FDreamGradient Gradient = MakeGradient();
	FDreamTextPaintParams Base = MakeParams();
	// The paint's colour is the text's: only the slot can tell the strips apart.
	Base.BaseColor = Base.PaintBaseColor;

	auto StripAt = [](const FDreamUIGeometry& Geometry, int32 Quad)
	{
		return FVector2f(Geometry.OriginVertices[Quad * 4].Position.Y, Geometry.OriginVertices[Quad * 4 + 1].Position.Y);
	};

	{
		FDreamUIGeometry Geometry;
		TArray<FDreamUITextCharProperty> Chars;
		FDreamTextPainter::Paint(DL, Base, Geometry, Chars);
		TestEqual(TEXT("without paints: four glyphs and one strip"), Geometry.Vertices.Num(), 5 * 4);
	}

	// 'c' lost its paints: the strip is cut around it.
	{
		FDreamTextDisplayList Removed = DL;
		Removed.Items[2].Style.bPaintRemoved = true;
		FDreamTextPaintParams Params = Base;
		PaintOwnFace(Params, Gradient, EDreamTextPaintBox::TextBlock, EDreamTextPaintBox::TextBlock);
		FDreamUIGeometry Geometry;
		TArray<FDreamUITextCharProperty> Chars;
		FDreamTextPainter::Paint(Removed, Params, Geometry, Chars);
		if (!TestEqual(TEXT("slot change: four glyphs and three strips"), Geometry.Vertices.Num(), 7 * 4))return false;
		TestTrue(TEXT("slot change: 'a' and 'b''s strip, painted"), StripAt(Geometry, 4) == FVector2f(10.0f, 26.0f)
			&& QuadIsMeasuredIn(Geometry, 4, DreamTextQuadCode::TextSlot, Removed.TextBlockBox));
		TestTrue(TEXT("slot change: 'c''s strip, solid"), StripAt(Geometry, 5) == FVector2f(26.0f, 34.0f) && QuadPaintsNothing(Geometry, 5));
		TestTrue(TEXT("slot change: 'd''s strip, painted"), StripAt(Geometry, 6) == FVector2f(34.0f, 42.0f)
			&& QuadIsMeasuredIn(Geometry, 6, DreamTextQuadCode::TextSlot, Removed.TextBlockBox));
	}

	// Glyph boxes: a piece per character, each measured in its own character's box.
	{
		FDreamTextPaintParams Params = Base;
		PaintOwnFace(Params, Gradient, EDreamTextPaintBox::Glyph, EDreamTextPaintBox::Glyph);
		FDreamUIGeometry Geometry;
		TArray<FDreamUITextCharProperty> Chars;
		FDreamTextPainter::Paint(DL, Params, Geometry, Chars);
		if (!TestEqual(TEXT("glyph boxes: four glyphs and four pieces"), Geometry.Vertices.Num(), 8 * 4))return false;
		for (int32 Index = 0; Index < 4; Index++)
		{
			const FDreamTextGlyphItem& Item = DL.Items[Index];
			const FDreamTextBox GlyphBox = MakeBox(Item.Pen.X, Item.Pen.X + Item.AdvanceWithSpace, Item.Pen.Y - Item.Descent, Item.Pen.Y + Item.Ascent);
			TestTrue(*FString::Printf(TEXT("glyph boxes: piece %d is under its character, measured in its box"), Index),
				StripAt(Geometry, 4 + Index) == FVector2f(Item.Pen.X, Item.Pen.X + 8.0f) && QuadIsMeasuredIn(Geometry, 4 + Index, DreamTextQuadCode::TextSlot, GlyphBox));
		}
		TestEqual(TEXT("glyph boxes: a piece runs from u 0"), Geometry.Vertices[4 * 4].UV4.X, 0.0f);
		TestEqual(TEXT("glyph boxes: to u 1"), Geometry.Vertices[4 * 4 + 1].UV4.X, 1.0f);
		TestEqual(TEXT("glyph boxes: its top a unit under the baseline, 10 of the glyph's 12 down"), Geometry.Vertices[4 * 4 + 2].UV4.Y, 10.0f / 12.0f, 1e-6f);
	}

	// Two runs of one tag paint, side by side: one strip each, measured in its own run.
	{
		FDreamTextDisplayList Runs = DL;
		Runs.PaintNames.Add(TEXT("Gold"));
		for (int32 Index = 0; Index < 4; Index++)
		{
			Runs.Items[Index].Style.PaintIndex = 0;
			Runs.Items[Index].PaintFragment = Index / 2;
		}
		for (int32 Run = 0; Run < 2; Run++)
		{
			FDreamTextPaintFragment& Piece = Runs.PaintFragments.AddDefaulted_GetRef();
			Piece.PaintIndex = 0;
			Piece.LineIndex = 0;
			Piece.Box = MakeBox(10.0f + 16.0f * Run, 26.0f + 16.0f * Run, -3.0f, 9.0f);
			Piece.RunOffset = 0.0f;
			Piece.RunWidth = 16.0f;
		}
		const TArray<uint8> NameSlots = { (uint8)DreamTextQuadCode::FirstTagSlot };
		FDreamTextPaintParams Params = Base;
		FDreamTextPaintSlot& TagSlot = Params.Paints.Slots[DreamTextQuadCode::FirstTagSlot];
		TagSlot.Face = &Gradient;
		TagSlot.HorizontalBox = TagSlot.VerticalBox = EDreamTextPaintBox::Run;
		Params.Paints.NameSlots = &NameSlots;
		FDreamUIGeometry Geometry;
		TArray<FDreamUITextCharProperty> Chars;
		FDreamTextPainter::Paint(Runs, Params, Geometry, Chars);
		if (!TestEqual(TEXT("runs: four glyphs and two strips"), Geometry.Vertices.Num(), 6 * 4))return false;
		for (int32 Run = 0; Run < 2; Run++)
		{
			TestTrue(*FString::Printf(TEXT("runs: run %d's strip is measured in its own run"), Run),
				StripAt(Geometry, 4 + Run) == FVector2f(10.0f + 16.0f * Run, 26.0f + 16.0f * Run)
				&& QuadIsMeasuredIn(Geometry, 4 + Run, DreamTextQuadCode::FirstTagSlot, Runs.PaintFragments[Run].Box));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextPainterRunBoxTest,
	"DreamGUI.Text.Painter.ARunBoxLaysTheRunsPiecesEndToEnd",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A Run box is CSS's slice: a tag run broken over two lines is measured as if its pieces were laid end to end in line
 * order -- a piece's u starts at its RunOffset over the RunWidth -- and down each piece's own box. The text's own paints
 * measured across Runs take the whole text as one run, its line boxes laid end to end.
 */
bool FDreamTextPainterRunBoxTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextPainterPaintTestLocal;

	FDreamTextDisplayList DL = MakeTwoLines();
	// 'b' and 'c' on line 0 and 'd' on line 1 are one <gradient> run: its pieces 16 and 8 wide.
	DL.PaintNames.Add(TEXT("Gold"));
	const int32 InRun[] = { 1, 2, 3 };
	for (int32 Item : InRun)
	{
		DL.Items[Item].Style.PaintIndex = 0;
		DL.Items[Item].PaintFragment = DL.Items[Item].LineIndex;
	}
	FDreamTextPaintFragment& First = DL.PaintFragments.AddDefaulted_GetRef();
	First.LineIndex = 0;
	First.Box = MakeBox(18.0f, 34.0f, -3.0f, 9.0f);
	First.RunOffset = 0.0f;
	First.RunWidth = 24.0f;
	FDreamTextPaintFragment& Second = DL.PaintFragments.AddDefaulted_GetRef();
	Second.LineIndex = 1;
	Second.Box = MakeBox(10.0f, 18.0f, -18.0f, -6.0f);
	Second.RunOffset = 16.0f;
	Second.RunWidth = 24.0f;

	const FDreamGradient Gradient = MakeGradient();
	const TArray<uint8> NameSlots = { (uint8)DreamTextQuadCode::FirstTagSlot };
	FDreamTextPaintParams Params = MakeParams();
	PaintOwnFace(Params, Gradient, EDreamTextPaintBox::Run, EDreamTextPaintBox::Run);
	FDreamTextPaintSlot& TagSlot = Params.Paints.Slots[DreamTextQuadCode::FirstTagSlot];
	TagSlot.Face = &Gradient;
	TagSlot.HorizontalBox = TagSlot.VerticalBox = EDreamTextPaintBox::Run;
	Params.Paints.NameSlots = &NameSlots;
	FDreamUIGeometry Geometry;
	TArray<FDreamUITextCharProperty> Chars;
	FDreamTextPainter::Paint(DL, Params, Geometry, Chars);
	if (!TestEqual(TEXT("one quad per glyph"), Geometry.Vertices.Num(), 5 * 4))return false;

	// The tag run: 'b' from 0 to a third, 'c' from a third to two thirds, 'd' on the next line from two thirds to 1.
	const FVector2f TagRun[] = { FVector2f(0.0f, 8.0f / 24.0f), FVector2f(8.0f / 24.0f, 16.0f / 24.0f), FVector2f(16.0f / 24.0f, 1.0f) };
	for (int32 Index = 0; Index < 3; Index++)
	{
		const int32 Quad = InRun[Index];
		const FString What = FString::Printf(TEXT("'%c'"), (TCHAR)('a' + Quad));
		TestEqual(*(What + TEXT(" paints with the tag's slot")), SlotOf(Geometry, Quad), DreamTextQuadCode::FirstTagSlot);
		TestEqual(*(What + TEXT(": its left edge's place in the run")), Geometry.Vertices[Quad * 4].UV4.X, TagRun[Index].X, 1e-6f);
		TestEqual(*(What + TEXT(": its right edge's")), Geometry.Vertices[Quad * 4 + 1].UV4.X, TagRun[Index].Y, 1e-6f);
		TestEqual(*(What + TEXT(": its top at the top of its piece")), Geometry.Vertices[Quad * 4 + 2].UV4.Y, 0.0f, 1e-6f);
		TestEqual(*(What + TEXT(": its bottom at the bottom")), Geometry.Vertices[Quad * 4].UV4.Y, 1.0f, 1e-6f);
	}
	// The text's own paints: 'a' at the start of line 0 (24 wide), 'e' on line 1 (16 wide) after all of line 0.
	TestEqual(TEXT("'a' paints with the text's slot"), SlotOf(Geometry, 0), DreamTextQuadCode::TextSlot);
	TestEqual(TEXT("'a' starts the whole text's run"), Geometry.Vertices[0].UV4.X, 0.0f, 1e-6f);
	TestEqual(TEXT("'a' ends a fifth into it"), Geometry.Vertices[1].UV4.X, 8.0f / 40.0f, 1e-6f);
	TestEqual(TEXT("'e' starts after line 0 and its own line's first glyph"), Geometry.Vertices[4 * 4].UV4.X, 32.0f / 40.0f, 1e-6f);
	TestEqual(TEXT("'e' ends the whole run"), Geometry.Vertices[4 * 4 + 1].UV4.X, 1.0f, 1e-6f);
	TestEqual(TEXT("'e' is measured down its line's box"), Geometry.Vertices[4 * 4 + 2].UV4.Y, 0.0f, 1e-6f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextPainterNothingPaintedTest,
	"DreamGUI.Text.Painter.ATextThatPaintsNothingWritesTheBytesItWritesWithoutPaints",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A text with paints none of its quads take -- every glyph solid in a <color> -- writes exactly the bytes it writes with no
 * paint at all; and a text painted with no paint into a geometry a painted text used before writes exactly what it writes
 * into a fresh one: UV4 and the codes are written on every vertex, so nothing of the last paint's survives.
 */
bool FDreamTextPainterNothingPaintedTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextPainterPaintTestLocal;

	FDreamTextDisplayList DL = MakeTwoLines();
	Underline(DL.Items[0]);
	Underline(DL.Items[1]);
	DL.Items[3].Style.bSyntheticItalic = true;
	const FDreamGradient Gradient = MakeGradient();
	FDreamTextPaintParams Plain = MakeParams();
	Plain.bSeparateEffectLayer = true;
	Plain.EffectReachEm = 0.1f;
	FDreamTextPaintParams Painted = Plain;
	PaintOwnFace(Painted, Gradient, EDreamTextPaintBox::Line, EDreamTextPaintBox::TextBlock);

	FDreamUIGeometry Reference;
	TArray<FDreamUITextCharProperty> ReferenceChars;
	FDreamTextPainter::Paint(DL, Plain, Reference, ReferenceChars);
	if (!TestEqual(TEXT("two quads a glyph, two for the strip"), Reference.Vertices.Num(), 12 * 4))return false;
	bool bReferenceUnpainted = true;
	for (int32 Quad = 0; Quad < 12; Quad++)
	{
		bReferenceUnpainted &= QuadPaintsNothing(Reference, Quad);
	}
	TestTrue(TEXT("without paints every quad is slot 0 with UV4 (0, 0)"), bReferenceUnpainted);

	// Every glyph solid: the face paint reaches no quad.
	{
		FDreamTextDisplayList Solid = DL;
		for (FDreamTextGlyphItem& Item : Solid.Items)
		{
			Item.Style.bHasColor = true;
			Item.Style.Color = Plain.BaseColor;
		}
		FDreamUIGeometry WithPaints;
		TArray<FDreamUITextCharProperty> WithPaintsChars;
		FDreamTextPainter::Paint(Solid, Painted, WithPaints, WithPaintsChars);
		FDreamUIGeometry WithoutPaints;
		TArray<FDreamUITextCharProperty> WithoutPaintsChars;
		FDreamTextPainter::Paint(Solid, Plain, WithoutPaints, WithoutPaintsChars);
		TestTrue(TEXT("paints no quad takes: the same bytes as no paints"), SameBytes(WithPaints, WithoutPaints));
	}

	// The painted text first, into the geometry the unpainted one then reuses.
	{
		FDreamUIGeometry Reused;
		TArray<FDreamUITextCharProperty> Chars;
		FDreamTextPainter::Paint(DL, Painted, Reused, Chars);
		if (!TestEqual(TEXT("painted: the same quads"), Reused.Vertices.Num(), 12 * 4))return false;
		TestEqual(TEXT("painted: a face carries slot 1"), SlotOf(Reused, 1), DreamTextQuadCode::TextSlot);
		TestTrue(TEXT("painted: and a UV4"), Reused.Vertices[1 * 4 + 1].UV4 != FVector2f::ZeroVector);
		FDreamTextPainter::Paint(DL, Plain, Reused, Chars);
		TestTrue(TEXT("unpainted after painted: the same bytes as into a fresh geometry"), SameBytes(Reused, Reference));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextPainterScratchTest,
	"DreamGUI.Text.Painter.APaintIsTheSameWhateverWasPaintedBeforeIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The painter keeps its working arrays from one paint to the next, so a burst of paints allocates nothing once they have
 * grown: none of it may leak into the next paint. A text painted, then a longer and busier one -- more lines, a lyric fill,
 * a colour glyph, strikethroughs, another paint -- then the first again, writes the first exactly as it did the first time.
 */
bool FDreamTextPainterScratchTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextPainterPaintTestLocal;

	FDreamTextDisplayList First = MakeTwoLines();
	Underline(First.Items[0]);
	Underline(First.Items[1]);
	const FDreamGradient Gradient = MakeGradient();
	FDreamTextPaintParams FirstParams = MakeParams();
	PaintOwnFace(FirstParams, Gradient, EDreamTextPaintBox::Line, EDreamTextPaintBox::TextBlock);

	FDreamTextDisplayList Busy;
	for (int32 Line = 0; Line < 4; Line++)
	{
		Busy.Lines.AddDefaulted();
		Busy.LineBoxes.Add(MakeBox(0.0f, 96.0f, -15.0f * Line - 3.0f, -15.0f * Line + 9.0f));
		for (int32 Index = 0; Index < 12; Index++)
		{
			FDreamTextGlyphItem Item = MakeBoxGlyph('a' + Index, Line * 13 + Index, Line, FVector2f(8.0f * Index, -15.0f * Line), 8.0f);
			Item.Style.bStrikethrough = true;
			Item.StrikethroughGlyph.YOffset = 4.0f;
			Item.StrikethroughGlyph.Height = 0.75f;
			Item.StrikethroughGlyph.MinUV = Item.StrikethroughGlyph.MaxUV = FVector2f(0.25f, 0.75f);
			Busy.Items.Add(Item);
		}
	}
	Busy.Items[5] = MakeColorGlyph(5, Busy.Items[5].Pen);
	Busy.TextBlockBox = MakeBox(0.0f, 96.0f, -48.0f, 9.0f);
	TArray<FDreamTextFillSegment> Segments;
	FDreamTextFillSegment Segment;
	Segment.StartCharIndex = 2;
	Segment.EndCharIndex = 20;
	Segment.Progress = 0.5f;
	Segments.Add(Segment);
	FDreamTextPaintParams BusyParams = MakeParams();
	BusyParams.bSeparateEffectLayer = true;
	BusyParams.EffectReachEm = 0.1f;
	BusyParams.FillSegments = &Segments;
	PaintOwnFace(BusyParams, Gradient, EDreamTextPaintBox::Glyph, EDreamTextPaintBox::Glyph);

	FDreamUIGeometry Before;
	TArray<FDreamUITextCharProperty> BeforeChars;
	FDreamTextPainter::Paint(First, FirstParams, Before, BeforeChars);
	FDreamUIGeometry BusyGeometry;
	TArray<FDreamUITextCharProperty> BusyChars;
	FDreamTextPainter::Paint(Busy, BusyParams, BusyGeometry, BusyChars);
	TestTrue(TEXT("the busier text has more quads than the first"), BusyGeometry.Vertices.Num() > Before.Vertices.Num());
	FDreamUIGeometry After;
	TArray<FDreamUITextCharProperty> AfterChars;
	FDreamTextPainter::Paint(First, FirstParams, After, AfterChars);
	TestTrue(TEXT("the first text is written byte for byte as before"), SameBytes(Before, After));
	bool bSameChars = BeforeChars.Num() == AfterChars.Num();
	for (int32 Index = 0; Index < BeforeChars.Num() && bSameChars; Index++)
	{
		bSameChars &= BeforeChars[Index].CharIndex == AfterChars[Index].CharIndex && BeforeChars[Index].StartVertIndex == AfterChars[Index].StartVertIndex
			&& BeforeChars[Index].VertCount == AfterChars[Index].VertCount && BeforeChars[Index].StartTriangleIndex == AfterChars[Index].StartTriangleIndex
			&& BeforeChars[Index].IndicesCount == AfterChars[Index].IndicesCount;
	}
	TestTrue(TEXT("and its characters are where they were"), bSameChars);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextPainterVertexColorFallbackTest,
	"DreamGUI.Text.Painter.TheVertexColourFallbackPutsTheGradientIntoTheFaceColoursAtSlotZero",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A material that does not shade through MF_DreamUI_Shade cannot read a slot: under bVertexColorFallback no quad takes one
 * and none gets a UV4, and each face vertex's colour is the paint colour times the face gradient evaluated where the
 * vertex sits in its boxes (FDreamGradient::Evaluate, with the slot's aspect and the text's animation), in linear light as
 * the shader would multiply them. The outline paint is not drawn: an effects copy keeps its colour.
 */
bool FDreamTextPainterVertexColorFallbackTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextPainterPaintTestLocal;

	const FDreamTextDisplayList DL = MakeTwoLines();
	const FDreamGradient Gradient = MakeGradient();
	FDreamTextPaintParams Params = MakeParams();
	Params.bSeparateEffectLayer = true;
	Params.EffectReachEm = 0.1f;
	PaintOwnFace(Params, Gradient, EDreamTextPaintBox::TextBlock, EDreamTextPaintBox::TextBlock);
	Params.Paints.Slots[DreamTextQuadCode::TextSlot].Outline = &Gradient;
	Params.Paints.Slots[DreamTextQuadCode::TextSlot].BoxAspect = 24.0f / 27.0f;
	Params.Paints.bVertexColorFallback = true;
	Params.Paints.FaceAnimation.Phase = 0.1f;
	FDreamUIGeometry Geometry;
	TArray<FDreamUITextCharProperty> Chars;
	FDreamTextPainter::Paint(DL, Params, Geometry, Chars);
	if (!TestEqual(TEXT("two quads a glyph"), Geometry.Vertices.Num(), 10 * 4))return false;

	bool bNothingPainted = true;
	bool bFaces = true;
	bool bEffects = true;
	for (int32 Glyph = 0; Glyph < 5; Glyph++)
	{
		bNothingPainted &= QuadPaintsNothing(Geometry, Glyph * 2) && QuadPaintsNothing(Geometry, Glyph * 2 + 1);
		for (int32 Vertex = (Glyph * 2 + 1) * 4; Vertex < (Glyph * 2 + 2) * 4; Vertex++)
		{
			const FLinearColor Paint = Gradient.Evaluate(InBox(DL.TextBlockBox, Corner(Geometry, Vertex)), 24.0f / 27.0f, Params.Paints.FaceAnimation);
			bFaces &= Geometry.Vertices[Vertex].Color == (FLinearColor(Params.PaintBaseColor) * Paint).ToFColor(true);
		}
		for (int32 Vertex = Glyph * 2 * 4; Vertex < (Glyph * 2 + 1) * 4; Vertex++)
		{
			bEffects &= Geometry.Vertices[Vertex].Color == FColor(Params.PaintBaseColor.R, Params.PaintBaseColor.G, Params.PaintBaseColor.B, 255);
		}
	}
	TestTrue(TEXT("no quad takes a slot or a UV4"), bNothingPainted);
	TestTrue(TEXT("every face vertex is the paint colour times the gradient there"), bFaces);
	TestTrue(TEXT("every effects copy keeps its colour, the outline paint undrawn"), bEffects);
	// Red on the left, blue on the right: the gradient did reach the colours.
	const FColor Left = Geometry.Vertices[1 * 4].Color;
	const FColor Right = Geometry.Vertices[5 * 4 + 1].Color;
	TestTrue(TEXT("the block's left is redder than its right"), Left.R > Right.R && Left.B < Right.B);
	return true;
}

#endif
