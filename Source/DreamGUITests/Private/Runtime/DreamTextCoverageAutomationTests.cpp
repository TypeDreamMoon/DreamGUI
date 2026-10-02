// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Core/DreamUIGeometry.h"
#include "Core/DreamUITextData.h"
#include "Core/Text/DreamTextLayout.h"
#include "Core/Text/DreamTextPainter.h"
#include "Engine/World.h"
#include "DreamTextTestFont.h"
#include "DreamScopedWorld.h"

/*
 * Small text from coverage glyphs, at the painter: where a coverage quad lands on the device pixel grid, which items
 * take one, what an item does while its coverage glyph is still being made, how strokes snap, and that a paint without
 * coverage is what it was. The coverage glyphs are made up (UDreamTextTestFont::MockCoverageGlyph), so every number
 * here follows from the rules on FDreamTextCoverageParams, worked out again in the test, rather than from a rasterizer.
 */
namespace DreamTextCoverageTestLocal
{
	using DreamTests::FScopedGameWorld;

	/** One atlas texel, in UV, of the made-up coverage cells. */
	constexpr float CoverageTexel = 1.0f / 1024.0f;

	/** An emitted glyph item built by hand: its field quad a function of the code point and size, on line 0. */
	FDreamTextGlyphItem MakeGlyph(uint32 Codepoint, int32 ElementIndex, const FVector2f& Pen, float Size)
	{
		FDreamTextGlyphItem Item;
		Item.Kind = EDreamTextItemKind::Glyph;
		Item.Codepoint = Codepoint;
		Item.ElementIndex = ElementIndex;
		Item.SourceIndex = ElementIndex;
		Item.LineIndex = 0;
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
		Item.Glyph.FaceIndex = 0;
		Item.Glyph.GlyphIndex = Codepoint;
		Item.GlyphSize = Size;
		Item.AdvanceWithSpace = Item.Glyph.XAdvance;
		Item.DecorationOffset = 0.0f;
		Item.Style.Size = Size;
		Item.bEmit = true;
		Item.bCountsAsVisible = true;
		return Item;
	}

	/** The coverage box the mock font hands out: a function of the glyph and the raster size, as a rasterizer's would be. */
	FDreamUICoverageGlyph MakeCoverageGlyph(uint32 GlyphIndex, int32 Size26Dot6)
	{
		const int32 Px = FMath::Max(1, (Size26Dot6 + 32) / 64);
		FDreamUICoverageGlyph Glyph;
		Glyph.BitmapLeft = (int32)(GlyphIndex % 3u) - 1;
		Glyph.Width = Px / 2 + 2;
		Glyph.Height = Px * 3 / 4 + 1;
		Glyph.BitmapTop = Px * 3 / 4;
		const FVector2f Corner((float)((GlyphIndex % 16u) * 32u) * CoverageTexel, 64.0f * CoverageTexel);
		Glyph.MinUV = Corner;
		Glyph.MaxUV = Corner + FVector2f(Glyph.Width * CoverageTexel, Glyph.Height * CoverageTexel);
		Glyph.SliceIndex = 3;
		return Glyph;
	}

	struct FCoverageRequest
	{
		int32 FaceIndex = 0;
		uint32 GlyphIndex = 0;
		int32 Size26Dot6 = 0;
		EDreamUICoverageGlyphFlags Flags = EDreamUICoverageGlyphFlags::None;
	};

	/**
	 * Gives the test font coverage glyphs (MakeCoverageGlyph) and records every request. Glyphs in Pending come back still
	 * being made, glyphs in Refused cannot be drawn from coverage at all.
	 */
	void MockCoverage(UDreamTextTestFont* Font, TArray<FCoverageRequest>& Requests, const TSet<uint32>& Pending = TSet<uint32>(),
		const TSet<uint32>& Refused = TSet<uint32>())
	{
		Font->MockCoverageGlyph = [&Requests, Pending, Refused](int32 FaceIndex, uint32 GlyphIndex, int32 Size26Dot6,
			EDreamUICoverageGlyphFlags Flags, FDreamUICoverageGlyph& OutGlyph)
		{
			FCoverageRequest Request;
			Request.FaceIndex = FaceIndex;
			Request.GlyphIndex = GlyphIndex;
			Request.Size26Dot6 = Size26Dot6;
			Request.Flags = Flags;
			Requests.Add(Request);
			if (Refused.Contains(GlyphIndex))return false;
			OutGlyph = MakeCoverageGlyph(GlyphIndex, Size26Dot6);
			OutGlyph.bPending = Pending.Contains(GlyphIndex);
			return true;
		};
	}

	/** A distance-field text with no effects and coverage on: S, the grid's origin, and the project's defaults otherwise. */
	FDreamTextPaintParams MakeCoverageParams(UDreamUIFontData_BaseObject* Font, float DeviceScale, const FVector2f& SnapOrigin,
		FDreamTextCoverageReport* Report)
	{
		FDreamTextPaintParams Params;
		Params.ItalicSlope = 0.25f;
		Params.BaseColor = FColor(20, 30, 40, 255);
		Params.bDistanceField = true;
		Params.EmTexels = 64.0f;
		Params.FieldSpreadTexels = 16.0f;
		Params.QuadMarginTexels = 1.0f;
		Params.TexelToUV = 1.0f / 2048.0f;
		Params.Coverage.bEnabled = true;
		Params.Coverage.Font = Font;
		Params.Coverage.DeviceScale = DeviceScale;
		Params.Coverage.RasterScale = DeviceScale;
		Params.Coverage.MaxPixelSize = 20.0f;
		Params.Coverage.SnapOrigin = SnapOrigin;
		Params.Coverage.Contrast = 1.0f;
		Params.Coverage.bLinearTarget = false;
		Params.Coverage.Report = Report;
		return Params;
	}

	/** One quad as written: corners in local space (vertex 0 bottom-left, 1 bottom-right, 2 top-left, 3 top-right). */
	struct FQuadView
	{
		FVector2f BottomLeft, BottomRight, TopLeft, TopRight;
		/** UV0 of vertex 0 (left, bottom) and of vertex 3 (right, top). */
		FVector2f UVBottomLeft, UVTopRight;
		float Slice = 0.0f;
		float Code = 0.0f;
		float RunX0 = 0.0f;
		float RunX1 = 0.0f;
		FVector2f UV3 = FVector2f::ZeroVector;
		FColor Color;
	};

	FQuadView ReadQuad(const FDreamUIGeometry& Geometry, int32 Quad)
	{
		const int32 V = Quad * 4;
		auto Corner = [&Geometry](int32 Vertex) { return FVector2f(Geometry.OriginVertices[Vertex].Position.Y, Geometry.OriginVertices[Vertex].Position.Z); };
		FQuadView View;
		View.BottomLeft = Corner(V);
		View.BottomRight = Corner(V + 1);
		View.TopLeft = Corner(V + 2);
		View.TopRight = Corner(V + 3);
		View.UVBottomLeft = Geometry.Vertices[V].TextureCoordinate[0];
		View.UVTopRight = Geometry.Vertices[V + 3].TextureCoordinate[0];
		View.Slice = Geometry.Vertices[V].TextureCoordinate[1].Y;
		View.Code = Geometry.Vertices[V].TextureCoordinate[2].X;
		View.RunX0 = Geometry.Vertices[V].TextureCoordinate[2].Y;
		View.RunX1 = Geometry.Vertices[V + 1].TextureCoordinate[2].Y;
		View.UV3 = Geometry.Vertices[V].TextureCoordinate[3];
		View.Color = Geometry.Vertices[V].Color;
		return View;
	}

	/** Whether every vertex of a quad carries the same code, UV3 and colour (the channels a quad has one of). */
	bool QuadIsUniform(const FDreamUIGeometry& Geometry, int32 Quad)
	{
		const FDreamUIMeshVertex& First = Geometry.Vertices[Quad * 4];
		for (int32 i = 1; i < 4; i++)
		{
			const FDreamUIMeshVertex& Vertex = Geometry.Vertices[Quad * 4 + i];
			if (Vertex.TextureCoordinate[2].X != First.TextureCoordinate[2].X || Vertex.TextureCoordinate[3] != First.TextureCoordinate[3]
				|| Vertex.Color != First.Color)
			{
				return false;
			}
		}
		return true;
	}

	/** u or v of a local coordinate: device pixels. */
	double ToDevice(float Local, float DeviceScale, float Origin)
	{
		return (double)DeviceScale * Local + Origin;
	}

	bool IsWholePixel(double Device)
	{
		return FMath::Abs(Device - FMath::RoundToDouble(Device)) < 1e-3;
	}

	/** Where the rules put a coverage glyph for a pen: the baseline row, the column and the phase. */
	struct FGridPlace
	{
		int32 Row = 0;
		int32 Column = 0;
		int32 Phase = 0;
	};

	FGridPlace PlaceOnGrid(const FVector2f& Pen, float DeviceScale, const FVector2f& SnapOrigin)
	{
		FGridPlace Place;
		Place.Row = FMath::FloorToInt32(ToDevice(Pen.Y, DeviceScale, SnapOrigin.Y) + 0.5);
		const int32 Quarters = FMath::FloorToInt32(4.0 * ToDevice(Pen.X, DeviceScale, SnapOrigin.X) + 0.5);
		Place.Column = FMath::FloorToInt32(Quarters / 4.0);
		Place.Phase = Quarters - 4 * Place.Column;
		return Place;
	}

	bool SameGeometry(const FDreamUIGeometry& A, const FDreamUIGeometry& B)
	{
		if (A.OriginVertices.Num() != B.OriginVertices.Num() || A.Vertices.Num() != B.Vertices.Num() || A.Triangles.Num() != B.Triangles.Num())
		{
			return false;
		}
		for (int32 i = 0; i < A.OriginVertices.Num(); i++)
		{
			if (A.OriginVertices[i].Position != B.OriginVertices[i].Position)return false;
			if (A.Vertices[i].Color != B.Vertices[i].Color)return false;
			for (int32 Channel = 0; Channel < 4; Channel++)
			{
				if (A.Vertices[i].TextureCoordinate[Channel] != B.Vertices[i].TextureCoordinate[Channel])return false;
			}
		}
		for (int32 i = 0; i < A.Triangles.Num(); i++)
		{
			if (A.Triangles[i] != B.Triangles[i])return false;
		}
		return true;
	}

	bool SameCharProperties(const TArray<FDreamUITextCharProperty>& A, const TArray<FDreamUITextCharProperty>& B)
	{
		if (A.Num() != B.Num())return false;
		for (int32 i = 0; i < A.Num(); i++)
		{
			if (A[i].CharIndex != B[i].CharIndex || A[i].StartVertIndex != B[i].StartVertIndex || A[i].VertCount != B[i].VertCount
				|| A[i].StartTriangleIndex != B[i].StartTriangleIndex || A[i].IndicesCount != B[i].IndicesCount)
			{
				return false;
			}
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextCoverageQuadsOnPixelsTest,
	"DreamGUI.Text.Coverage.QuadsLandOnWholeDevicePixels",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The device grid of FDreamTextCoverageParams at a fractional scale and origin: every corner of a coverage quad lands on
 * a pixel boundary, the box is the glyph's own pixels from the rounded pen, the UVs are its texels exactly, and UV2.x and
 * UV3.y carry the phase and the contrast. The raster size is the glyph size times the raster scale, not the device scale.
 */
bool FDreamTextCoverageQuadsOnPixelsTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextCoverageTestLocal;
	FScopedGameWorld TestWorld;
	UDreamTextTestFont* Font = NewObject<UDreamTextTestFont>(TestWorld.World);
	TArray<FCoverageRequest> Requests;
	MockCoverage(Font, Requests);

	const float Scale = 1.25f;
	const FVector2f Origin(-3.3f, -47.6f);
	FDreamTextDisplayList DL;
	DL.Items.Add(MakeGlyph('a', 0, FVector2f(10.3f, -20.37f), 12.0f));
	DL.Items.Add(MakeGlyph('b', 1, FVector2f(17.9f, -20.37f), 12.0f));
	DL.Items.Add(MakeGlyph('c', 2, FVector2f(25.15f, -20.37f), 12.0f));

	for (int32 Pass = 0; Pass < 2; Pass++)
	{
		// The second pass rasterizes 0.8% off the device scale, as the component's hysteresis allows, on a linear target.
		const float RasterScale = Pass == 0 ? Scale : Scale * 1.008f;
		FDreamTextCoverageReport Report;
		Report.CoverageItems = Report.PendingItems = 99;
		FDreamTextPaintParams Params = MakeCoverageParams(Font, Scale, Origin, &Report);
		Params.Coverage.RasterScale = RasterScale;
		Params.Coverage.bLinearTarget = Pass == 1;
		Requests.Reset();
		FDreamUIGeometry Geometry;
		TArray<FDreamUITextCharProperty> Chars;
		FDreamTextPainter::Paint(DL, Params, Geometry, Chars);

		const FString Prefix = Pass == 0 ? TEXT("at the device scale") : TEXT("at a raster scale near it");
		TestEqual(*(Prefix + TEXT(": three items drawn from coverage")), Report.CoverageItems, 3);
		TestEqual(*(Prefix + TEXT(": none pending")), Report.PendingItems, 0);
		TestEqual(*(Prefix + TEXT(": one quad per glyph")), Geometry.OriginVertices.Num(), 3 * 4);
		TestEqual(*(Prefix + TEXT(": six indices per glyph")), Geometry.Triangles.Num(), 3 * 6);
		TestEqual(*(Prefix + TEXT(": three characters")), Chars.Num(), 3);
		if (!TestEqual(*(Prefix + TEXT(": the font is asked once per glyph")), Requests.Num(), 3) || Geometry.OriginVertices.Num() != 12)return false;

		const int32 Size26Dot6 = FMath::RoundToInt32(12.0f * RasterScale * 64.0f);
		for (int32 Index = 0; Index < 3; Index++)
		{
			const FDreamTextGlyphItem& Item = DL.Items[Index];
			const FString What = FString::Printf(TEXT("%s: glyph %d"), *Prefix, Index);
			TestEqual(*(What + TEXT(" is asked for by face and glyph index")), Requests[Index].GlyphIndex, Item.Glyph.GlyphIndex);
			TestEqual(*(What + TEXT(" is asked for at the glyph size times the raster scale, in 26.6")), Requests[Index].Size26Dot6, Size26Dot6);
			TestTrue(*(What + TEXT(" has no synthetic style")), Requests[Index].Flags == EDreamUICoverageGlyphFlags::None);

			const FDreamUICoverageGlyph Glyph = MakeCoverageGlyph(Item.Glyph.GlyphIndex, Size26Dot6);
			const FGridPlace Place = PlaceOnGrid(Item.Pen, Scale, Origin);
			const FQuadView Quad = ReadQuad(Geometry, Index);
			const double Left = ToDevice(Quad.BottomLeft.X, Scale, Origin.X);
			const double Right = ToDevice(Quad.BottomRight.X, Scale, Origin.X);
			const double Bottom = ToDevice(Quad.BottomLeft.Y, Scale, Origin.Y);
			const double Top = ToDevice(Quad.TopLeft.Y, Scale, Origin.Y);
			TestTrue(*(What + TEXT(" has every edge on a pixel boundary")), IsWholePixel(Left) && IsWholePixel(Right) && IsWholePixel(Bottom) && IsWholePixel(Top));
			TestEqual(*(What + TEXT(" starts at its column plus the bitmap's left")), Left, (double)(Place.Column + Glyph.BitmapLeft), 1e-3);
			TestEqual(*(What + TEXT(" is the bitmap's width in pixels")), Right - Left, (double)Glyph.Width, 1e-3);
			TestEqual(*(What + TEXT(" tops out at its baseline row plus the bitmap's top")), Top, (double)(Place.Row + Glyph.BitmapTop), 1e-3);
			TestEqual(*(What + TEXT(" is the bitmap's height in pixels")), Top - Bottom, (double)Glyph.Height, 1e-3);
			TestTrue(*(What + TEXT(" is not sheared")), Quad.BottomLeft.X == Quad.TopLeft.X && Quad.BottomRight.X == Quad.TopRight.X);
			TestEqual(*(What + TEXT(" samples the bitmap's left-bottom texel corner")), Quad.UVBottomLeft, FVector2f(Glyph.MinUV.X, Glyph.MaxUV.Y));
			TestEqual(*(What + TEXT(" samples the bitmap's right-top texel corner")), Quad.UVTopRight, FVector2f(Glyph.MaxUV.X, Glyph.MinUV.Y));
			TestEqual(*(What + TEXT(" samples the bitmap's slice")), Quad.Slice, (float)Glyph.SliceIndex);
			TestEqual(*(What + TEXT(" carries the coverage code and its phase")), Quad.Code, DreamTextQuadCode::CoverageBase + (float)Place.Phase);
			TestEqual(*(What + TEXT(" carries the contrast, flagged on a linear target")), Quad.UV3.Y,
				Pass == 0 ? 1.0f : 1.0f + DreamTextQuadCode::CoverageLinearTarget);
			TestTrue(*(What + TEXT(" is in the text's colour")), Quad.Color == Params.BaseColor);
			TestTrue(*(What + TEXT(" has one code, UV3 and colour on all four vertices")), QuadIsUniform(Geometry, Index));
			TestEqual(*(What + TEXT(" is its character's whole vertex range")), Chars[Index].VertCount, 4);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextCoveragePhaseCarryTest,
	"DreamGUI.Text.Coverage.APenSevenEighthsAcrossCarriesToTheNextPixelAtPhaseZero",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The pen's x rounds to the nearest quarter pixel, half up, and the quarter is the phase: x.875 lies halfway between
 * x.75 and the next whole pixel, so it lands on the next pixel unmoved rather than on this one at the 3/4 px phase. The
 * baseline rounds half up too, and a pen left of the canvas's edge still splits into a column and a phase from 0 to 3.
 */
bool FDreamTextCoveragePhaseCarryTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextCoverageTestLocal;
	FScopedGameWorld TestWorld;
	UDreamTextTestFont* Font = NewObject<UDreamTextTestFont>(TestWorld.World);
	TArray<FCoverageRequest> Requests;
	MockCoverage(Font, Requests);

	struct FCase { float PenX; float PenY; int32 Column; int32 Phase; int32 Row; };
	const FCase Cases[] =
	{
		{ 10.875f, -20.5f, 11, 0, -20 },
		{ 10.625f, -20.4f, 10, 3, -20 },
		{ 10.375f, -20.6f, 10, 2, -21 },
		{ 10.125f, -20.0f, 10, 1, -20 },
		{ 10.1f, -19.5f, 10, 0, -19 },
		{ -0.875f, -3.25f, -1, 1, -3 },
	};
	FDreamTextDisplayList DL;
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Cases); Index++)
	{
		DL.Items.Add(MakeGlyph('a' + Index, Index, FVector2f(Cases[Index].PenX, Cases[Index].PenY), 12.0f));
	}

	FDreamTextCoverageReport Report;
	const FDreamTextPaintParams Params = MakeCoverageParams(Font, 1.0f, FVector2f::ZeroVector, &Report);
	FDreamUIGeometry Geometry;
	TArray<FDreamUITextCharProperty> Chars;
	FDreamTextPainter::Paint(DL, Params, Geometry, Chars);
	if (!TestEqual(TEXT("every glyph drawn from coverage"), Report.CoverageItems, (int32)UE_ARRAY_COUNT(Cases)))return false;

	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Cases); Index++)
	{
		const FCase& Case = Cases[Index];
		const FDreamUICoverageGlyph Glyph = MakeCoverageGlyph(DL.Items[Index].Glyph.GlyphIndex, 12 * 64);
		const FQuadView Quad = ReadQuad(Geometry, Index);
		const FString What = FString::Printf(TEXT("pen (%.3f, %.2f)"), Case.PenX, Case.PenY);
		TestEqual(*(What + TEXT(": column")), (double)Quad.BottomLeft.X, (double)(Case.Column + Glyph.BitmapLeft), 1e-4);
		TestEqual(*(What + TEXT(": phase")), Quad.Code, DreamTextQuadCode::CoverageBase + (float)Case.Phase);
		TestEqual(*(What + TEXT(": baseline row")), (double)Quad.TopLeft.Y, (double)(Case.Row + Glyph.BitmapTop), 1e-4);
		// The test's own reading of the rules agrees with the table.
		const FGridPlace Place = PlaceOnGrid(DL.Items[Index].Pen, 1.0f, FVector2f::ZeroVector);
		TestTrue(*(What + TEXT(": the rules give the same place")), Place.Column == Case.Column && Place.Phase == Case.Phase && Place.Row == Case.Row);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextCoveragePerItemThresholdTest,
	"DreamGUI.Text.Coverage.TheThresholdIsDecidedPerItemInARichTextMix",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A 12 px run and a 40 px run in one rich text share one geometry: under MaxPixelSize device pixels per em the items come
 * from coverage, over it from the field, and the font is never asked for the large ones. The scale that counts is the
 * device scale; the raster scale only sets the size the glyph is made at.
 */
bool FDreamTextCoveragePerItemThresholdTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextCoverageTestLocal;
	FScopedGameWorld TestWorld;
	UDreamTextTestFont* Font = NewObject<UDreamTextTestFont>(TestWorld.World);
	TArray<FCoverageRequest> Requests;
	MockCoverage(Font, Requests);

	FDreamTextLayoutInput In;
	In.Content = TEXT("<size=12>ab</size><size=40>cd</size>");
	In.bRichText = true;
	In.Width = 600.0f;
	In.Height = 120.0f;
	In.Pivot = FVector2f(0.5f, 0.5f);
	In.FontSize = 24.0f;
	In.ParagraphHAlign = EDreamUITextParagraphHorizontalAlign::Left;
	In.ParagraphVAlign = EDreamUITextParagraphVerticalAlign::Top;
	In.Font = Font;
	FDreamTextDisplayList DL;
	FDreamTextLayoutEngine::Layout(In, DL);

	// The emitted items in order, which is the order of their quads (one copy each).
	TArray<const FDreamTextGlyphItem*> Emitted;
	for (const FDreamTextGlyphItem& Item : DL.Items)
	{
		if (Item.bEmit)Emitted.Add(&Item);
	}
	if (!TestEqual(TEXT("four glyphs"), Emitted.Num(), 4))return false;
	TestEqual(TEXT("the small run's glyph size"), Emitted[0]->GlyphSize, 12.0f, 0.001f);
	TestEqual(TEXT("the large run's glyph size"), Emitted[3]->GlyphSize, 40.0f, 0.001f);

	{
		FDreamTextCoverageReport Report;
		const FDreamTextPaintParams Params = MakeCoverageParams(Font, 1.0f, FVector2f::ZeroVector, &Report);
		FDreamUIGeometry Geometry;
		TArray<FDreamUITextCharProperty> Chars;
		FDreamTextPainter::Paint(DL, Params, Geometry, Chars);
		if (!TestEqual(TEXT("one quad per glyph"), Geometry.OriginVertices.Num(), 4 * 4))return false;
		TestEqual(TEXT("the 12 px glyphs come from coverage"), Report.CoverageItems, 2);
		TestEqual(TEXT("the font is asked for the 12 px glyphs only"), Requests.Num(), 2);
		for (int32 Index = 0; Index < Requests.Num(); Index++)
		{
			TestEqual(TEXT("asked at 12 px in 26.6"), Requests[Index].Size26Dot6, 12 * 64);
		}
		if (Requests.Num() == 2)
		{
			// The test font's glyph index is the code point.
			TestEqual(TEXT("the first glyph asked for is 'a'"), Requests[0].GlyphIndex, (uint32)'a');
			TestEqual(TEXT("the second glyph asked for is 'b'"), Requests[1].GlyphIndex, (uint32)'b');
		}
		for (int32 Index = 0; Index < 4; Index++)
		{
			const FQuadView Quad = ReadQuad(Geometry, Index);
			const int32 Layer = FMath::FloorToInt32((Quad.Code + 8.0f) / 16.0f);
			const bool bSmall = Emitted[Index]->Style.Size < 20.0f;
			TestEqual(*FString::Printf(TEXT("glyph %d (%.0f px) is drawn from %s"), Index, Emitted[Index]->Style.Size, bSmall ? TEXT("coverage") : TEXT("the field")),
				Layer, bSmall ? 3 : 2);
		}
	}

	// At twice the scale the 12 px run is 24 device pixels per em, over the limit: nothing is asked for, nothing changes.
	{
		Requests.Reset();
		FDreamTextCoverageReport Report;
		const FDreamTextPaintParams Params = MakeCoverageParams(Font, 2.0f, FVector2f::ZeroVector, &Report);
		FDreamUIGeometry Geometry;
		TArray<FDreamUITextCharProperty> Chars;
		FDreamTextPainter::Paint(DL, Params, Geometry, Chars);
		TestEqual(TEXT("at twice the scale nothing comes from coverage"), Report.CoverageItems, 0);
		TestEqual(TEXT("and the font is not asked"), Requests.Num(), 0);
	}

	// A raster scale half a percent off: the glyphs are made at that size, the threshold still reads the device scale.
	{
		Requests.Reset();
		FDreamTextCoverageReport Report;
		FDreamTextPaintParams Params = MakeCoverageParams(Font, 1.0f, FVector2f::ZeroVector, &Report);
		Params.Coverage.RasterScale = 1.005f;
		FDreamUIGeometry Geometry;
		TArray<FDreamUITextCharProperty> Chars;
		FDreamTextPainter::Paint(DL, Params, Geometry, Chars);
		TestEqual(TEXT("both small glyphs again"), Report.CoverageItems, 2);
		const bool bAllAtRasterSize = Requests.Num() == 2 && Requests[0].Size26Dot6 == 772 && Requests[1].Size26Dot6 == 772;
		TestTrue(TEXT("made at 12 * 1.005 px (772 in 26.6)"), bAllAtRasterSize);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextCoveragePendingFallsBackTest,
	"DreamGUI.Text.Coverage.APendingCoverageGlyphFallsBackToTheFieldQuadAndIsReported",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A coverage glyph still being made draws its item from the field this time, exactly the quad a paint without coverage
 * writes, and the report counts it so the text repaints when it lands. A glyph the font cannot draw from coverage at all
 * draws from the field too, and is not waited for.
 */
bool FDreamTextCoveragePendingFallsBackTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextCoverageTestLocal;
	FScopedGameWorld TestWorld;
	UDreamTextTestFont* Font = NewObject<UDreamTextTestFont>(TestWorld.World);
	TArray<FCoverageRequest> Requests;
	MockCoverage(Font, Requests, TSet<uint32>{ (uint32)'b' }, TSet<uint32>{ (uint32)'c' });

	FDreamTextDisplayList DL;
	DL.Items.Add(MakeGlyph('a', 0, FVector2f(0.3f, -15.2f), 12.0f));
	DL.Items.Add(MakeGlyph('b', 1, FVector2f(7.6f, -15.2f), 12.0f));
	DL.Items.Add(MakeGlyph('c', 2, FVector2f(14.9f, -15.2f), 12.0f));
	DL.Items.Add(MakeGlyph('d', 3, FVector2f(22.2f, -15.2f), 12.0f));

	FDreamTextCoverageReport Report;
	const FDreamTextPaintParams Params = MakeCoverageParams(Font, 1.0f, FVector2f(0.5f, -0.25f), &Report);
	FDreamUIGeometry Geometry;
	TArray<FDreamUITextCharProperty> Chars;
	FDreamTextPainter::Paint(DL, Params, Geometry, Chars);

	FDreamTextPaintParams FieldParams = Params;
	FieldParams.Coverage.bEnabled = false;
	// The comparison paint reports to nobody: a paint resets the report it is given, coverage on or off, and sharing this one
	// would leave it saying what the paint without coverage found -- nothing.
	FieldParams.Coverage.Report = nullptr;
	FDreamUIGeometry Field;
	TArray<FDreamUITextCharProperty> FieldChars;
	FDreamTextPainter::Paint(DL, FieldParams, Field, FieldChars);

	if (!TestEqual(TEXT("one quad per glyph either way"), Geometry.OriginVertices.Num(), Field.OriginVertices.Num()) || Geometry.OriginVertices.Num() != 16)return false;
	TestEqual(TEXT("every glyph is asked for"), Requests.Num(), 4);
	TestEqual(TEXT("two glyphs from coverage"), Report.CoverageItems, 2);
	TestEqual(TEXT("one pending: the refused glyph is not waited for"), Report.PendingItems, 1);
	TestTrue(TEXT("'a' is a coverage quad"), ReadQuad(Geometry, 0).Code >= DreamTextQuadCode::CoverageBase);
	TestTrue(TEXT("'d' is a coverage quad"), ReadQuad(Geometry, 3).Code >= DreamTextQuadCode::CoverageBase);
	for (int32 Quad = 1; Quad <= 2; Quad++)
	{
		bool bSame = true;
		for (int32 Vertex = Quad * 4; Vertex < Quad * 4 + 4; Vertex++)
		{
			bSame &= Geometry.OriginVertices[Vertex].Position == Field.OriginVertices[Vertex].Position;
			for (int32 Channel = 0; Channel < 4; Channel++)
			{
				bSame &= Geometry.Vertices[Vertex].TextureCoordinate[Channel] == Field.Vertices[Vertex].TextureCoordinate[Channel];
			}
			bSame &= Geometry.Vertices[Vertex].Color == Field.Vertices[Vertex].Color;
		}
		TestTrue(*FString::Printf(TEXT("'%c' is the field quad a paint without coverage writes"), (TCHAR)('a' + Quad)), bSame);
	}
	TestTrue(TEXT("the characters are where they were"), SameCharProperties(Chars, FieldChars));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextCoverageDrawsPendingFieldGlyphTest,
	"DreamGUI.Text.Coverage.AFieldGlyphStillPendingDrawsFromCoverageOnceItsCoverageGlyphIsReady",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A character whose field glyph is still on the rasterizer's worker has its place but no quad. Small enough, it is drawn
 * from its coverage glyph meanwhile -- in its own place in the vertex buffer, inside its character's range -- and when
 * that is still being made too, it waits, reported, with an empty range as before.
 */
bool FDreamTextCoverageDrawsPendingFieldGlyphTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextCoverageTestLocal;
	FScopedGameWorld TestWorld;
	UDreamTextTestFont* Font = NewObject<UDreamTextTestFont>(TestWorld.World);
	TArray<FCoverageRequest> Requests;

	FDreamTextDisplayList DL;
	DL.Items.Add(MakeGlyph('a', 0, FVector2f(0.0f, -15.0f), 12.0f));
	FDreamTextGlyphItem Waiting = MakeGlyph('b', 1, FVector2f(7.2f, -15.0f), 12.0f);
	// What the layout makes of a glyph still on the worker: the advance and the names, no quad.
	Waiting.Glyph.Width = 0.0f;
	Waiting.Glyph.Height = 0.0f;
	Waiting.Glyph.bPending = true;
	Waiting.bEmit = false;
	Waiting.bCountsAsVisible = true;
	DL.Items.Add(Waiting);
	DL.Items.Add(MakeGlyph('c', 2, FVector2f(14.4f, -15.0f), 12.0f));

	// Its coverage glyph is ready: three quads, the middle one 'b''s, in its character's range.
	{
		MockCoverage(Font, Requests);
		FDreamTextCoverageReport Report;
		const FDreamTextPaintParams Params = MakeCoverageParams(Font, 1.0f, FVector2f::ZeroVector, &Report);
		FDreamUIGeometry Geometry;
		TArray<FDreamUITextCharProperty> Chars;
		FDreamTextPainter::Paint(DL, Params, Geometry, Chars);
		if (!TestEqual(TEXT("three quads"), Geometry.OriginVertices.Num(), 3 * 4) || !TestEqual(TEXT("three characters"), Chars.Num(), 3))return false;
		TestEqual(TEXT("all three from coverage"), Report.CoverageItems, 3);
		TestEqual(TEXT("'b' has its quad"), Chars[1].VertCount, 4);
		TestEqual(TEXT("right after 'a''s"), Chars[1].StartVertIndex, 4);
		const FDreamUICoverageGlyph Glyph = MakeCoverageGlyph('b', 12 * 64);
		const FQuadView Quad = ReadQuad(Geometry, 1);
		TestEqual(TEXT("'b''s quad samples its coverage glyph"), Quad.UVBottomLeft, FVector2f(Glyph.MinUV.X, Glyph.MaxUV.Y));
		TestTrue(TEXT("with the coverage code"), Quad.Code >= DreamTextQuadCode::CoverageBase);
		const FGridPlace Place = PlaceOnGrid(Waiting.Pen, 1.0f, FVector2f::ZeroVector);
		TestEqual(TEXT("at its pen's place on the grid"), (double)Quad.BottomLeft.X, (double)(Place.Column + Glyph.BitmapLeft), 1e-4);
	}

	// Its coverage glyph is still being made too: no quad yet, an empty range, and the report says it is waiting.
	{
		MockCoverage(Font, Requests, TSet<uint32>{ (uint32)'b' });
		FDreamTextCoverageReport Report;
		const FDreamTextPaintParams Params = MakeCoverageParams(Font, 1.0f, FVector2f::ZeroVector, &Report);
		FDreamUIGeometry Geometry;
		TArray<FDreamUITextCharProperty> Chars;
		FDreamTextPainter::Paint(DL, Params, Geometry, Chars);
		TestEqual(TEXT("two quads"), Geometry.OriginVertices.Num(), 2 * 4);
		if (!TestEqual(TEXT("still three characters"), Chars.Num(), 3))return false;
		TestEqual(TEXT("'b' has an empty range"), Chars[1].VertCount, 0);
		TestEqual(TEXT("two from coverage"), Report.CoverageItems, 2);
		TestEqual(TEXT("'b' is reported pending"), Report.PendingItems, 1);
	}

	// Without coverage it waits for its field glyph, as ever.
	{
		FDreamTextPaintParams Params = MakeCoverageParams(Font, 1.0f, FVector2f::ZeroVector, nullptr);
		Params.Coverage.bEnabled = false;
		FDreamUIGeometry Geometry;
		TArray<FDreamUITextCharProperty> Chars;
		FDreamTextPainter::Paint(DL, Params, Geometry, Chars);
		TestEqual(TEXT("without coverage two quads"), Geometry.OriginVertices.Num(), 2 * 4);
		TestTrue(TEXT("and 'b' waits with an empty range"), Chars.Num() == 3 && Chars[1].VertCount == 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextCoverageStrokesSnapTest,
	"DreamGUI.Text.Coverage.StrokesAreSnappedToDeviceRowsAndAtLeastOnePixelThick",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * While coverage is on, an underline or a strikethrough has its top and bottom on device rows: its thickness rounded to
 * whole rows, never under one, about its own centre. Its solid texel and its horizontal extent are untouched, and so are
 * the strokes of a text without coverage. Strips drawn per character snap the same way.
 */
bool FDreamTextCoverageStrokesSnapTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextCoverageTestLocal;
	FScopedGameWorld TestWorld;
	UDreamTextTestFont* Font = NewObject<UDreamTextTestFont>(TestWorld.World);
	TArray<FCoverageRequest> Requests;
	MockCoverage(Font, Requests);

	const float Scale = 1.5f;
	const FVector2f Origin(0.25f, -0.4f);
	// Underline 0.6 thick (0.9 device px: one row), strikethrough 1.7 (2.55 device px: three rows), both solid texels.
	FDreamUICharData Underline;
	Underline.YOffset = -1.13f;
	Underline.Height = 0.6f;
	Underline.MinUV = Underline.MaxUV = FVector2f(0.5f, 0.5f);
	FDreamUICharData Strikethrough;
	Strikethrough.YOffset = 4.37f;
	Strikethrough.Height = 1.7f;
	Strikethrough.MinUV = Strikethrough.MaxUV = FVector2f(0.25f, 0.75f);
	FDreamTextDisplayList DL;
	for (int32 Index = 0; Index < 2; Index++)
	{
		FDreamTextGlyphItem Item = MakeGlyph('a' + Index, Index, FVector2f(3.1f + Index * 7.2f, -20.31f), 12.0f);
		Item.Style.bUnderline = true;
		Item.Style.bStrikethrough = true;
		Item.UnderlineGlyph = Underline;
		Item.StrikethroughGlyph = Strikethrough;
		DL.Items.Add(Item);
	}
	const float Baseline = DL.Items[0].Pen.Y;
	const float NominalLeft = DL.Items[0].Pen.X;
	const float NominalRight = DL.Items[1].Pen.X + DL.Items[1].AdvanceWithSpace;

	auto CheckStroke = [&](const FDreamUIGeometry& Geometry, int32 QuadIndex, const FDreamUICharData& Stroke, const FString& What)
	{
		const FQuadView Quad = ReadQuad(Geometry, QuadIndex);
		const double Bottom = ToDevice(Quad.BottomLeft.Y, Scale, Origin.Y);
		const double Top = ToDevice(Quad.TopLeft.Y, Scale, Origin.Y);
		const double NominalTop = ToDevice(Baseline + Stroke.YOffset, Scale, Origin.Y);
		const double NominalBottom = ToDevice(Baseline + Stroke.YOffset - Stroke.Height, Scale, Origin.Y);
		const int32 Rows = FMath::Max(1, FMath::RoundToInt32((double)Scale * Stroke.Height));
		TestTrue(*(What + TEXT(": top and bottom on device rows")), IsWholePixel(Bottom) && IsWholePixel(Top));
		TestEqual(*(What + TEXT(": its thickness in whole rows, at least one")), Top - Bottom, (double)Rows, 1e-3);
		TestTrue(*(What + TEXT(": about its own centre")), FMath::Abs(0.5 * (Top + Bottom) - 0.5 * (NominalTop + NominalBottom)) <= 0.5 + 1e-3);
		TestTrue(*(What + TEXT(": its solid texel kept")), Quad.UVBottomLeft == Stroke.MinUV && Quad.UVTopRight == Stroke.MinUV);
	};

	// Strips per run, after the glyphs: the underline's, then the strikethrough's.
	{
		FDreamTextCoverageReport Report;
		const FDreamTextPaintParams Params = MakeCoverageParams(Font, Scale, Origin, &Report);
		FDreamUIGeometry Geometry;
		TArray<FDreamUITextCharProperty> Chars;
		FDreamTextPainter::Paint(DL, Params, Geometry, Chars);
		if (!TestEqual(TEXT("two glyphs and two strips"), Geometry.OriginVertices.Num(), 4 * 4))return false;
		TestEqual(TEXT("the glyphs come from coverage"), Report.CoverageItems, 2);
		CheckStroke(Geometry, 2, Underline, TEXT("the underline strip"));
		CheckStroke(Geometry, 3, Strikethrough, TEXT("the strikethrough strip"));
		const FQuadView Strip = ReadQuad(Geometry, 2);
		TestEqual(TEXT("the strip's left edge is the run's"), Strip.BottomLeft.X, NominalLeft, 1e-4f);
		TestEqual(TEXT("the strip's right edge is the run's"), Strip.BottomRight.X, NominalRight, 1e-4f);
	}

	// Strips per character: each glyph's pieces follow it, and snap the same way.
	{
		FDreamTextPaintParams Params = MakeCoverageParams(Font, Scale, Origin, nullptr);
		Params.bStrokesPerCharacter = true;
		FDreamUIGeometry Geometry;
		TArray<FDreamUITextCharProperty> Chars;
		FDreamTextPainter::Paint(DL, Params, Geometry, Chars);
		if (!TestEqual(TEXT("per character: a glyph and two pieces each"), Geometry.OriginVertices.Num(), 6 * 4))return false;
		for (int32 Glyph = 0; Glyph < 2; Glyph++)
		{
			CheckStroke(Geometry, Glyph * 3 + 1, Underline, FString::Printf(TEXT("glyph %d's underline piece"), Glyph));
			CheckStroke(Geometry, Glyph * 3 + 2, Strikethrough, FString::Printf(TEXT("glyph %d's strikethrough piece"), Glyph));
		}
	}

	// Without coverage the strokes stay where the layout put them.
	{
		FDreamTextPaintParams Params = MakeCoverageParams(Font, Scale, Origin, nullptr);
		Params.Coverage.bEnabled = false;
		FDreamUIGeometry Geometry;
		TArray<FDreamUITextCharProperty> Chars;
		FDreamTextPainter::Paint(DL, Params, Geometry, Chars);
		if (!TestEqual(TEXT("without coverage two glyphs and two strips"), Geometry.OriginVertices.Num(), 4 * 4))return false;
		const FQuadView Strip = ReadQuad(Geometry, 2);
		TestEqual(TEXT("the underline's top is the layout's"), Strip.TopLeft.Y, Baseline + Underline.YOffset, 1e-4f);
		TestEqual(TEXT("the underline's bottom is the layout's"), Strip.BottomLeft.Y, Baseline + Underline.YOffset - Underline.Height, 1e-4f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextCoverageSyntheticStylesTest,
	"DreamGUI.Text.Coverage.ItalicAndBoldAreInTheRasterNotInTheQuad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Synthetic italic and bold are baked into a coverage glyph's raster, so the font is asked for them and the quad is
 * neither sheared nor shifted nor dilated -- while a field glyph in the same text, over the threshold, still is.
 */
bool FDreamTextCoverageSyntheticStylesTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextCoverageTestLocal;
	FScopedGameWorld TestWorld;
	UDreamTextTestFont* Font = NewObject<UDreamTextTestFont>(TestWorld.World);
	TArray<FCoverageRequest> Requests;
	MockCoverage(Font, Requests);

	FDreamTextDisplayList DL;
	FDreamTextGlyphItem Italic = MakeGlyph('a', 0, FVector2f(2.3f, -30.6f), 12.0f);
	Italic.Style.bItalic = Italic.Style.bSyntheticItalic = true;
	DL.Items.Add(Italic);
	FDreamTextGlyphItem Bold = MakeGlyph('b', 1, FVector2f(9.7f, -30.6f), 12.0f);
	Bold.Style.bBold = Bold.Style.bSyntheticBold = true;
	DL.Items.Add(Bold);
	FDreamTextGlyphItem LargeItalic = MakeGlyph('c', 2, FVector2f(17.1f, -30.6f), 40.0f);
	LargeItalic.Style.bItalic = LargeItalic.Style.bSyntheticItalic = true;
	DL.Items.Add(LargeItalic);

	FDreamTextCoverageReport Report;
	FDreamTextPaintParams Params = MakeCoverageParams(Font, 1.0f, FVector2f::ZeroVector, &Report);
	Params.BoldDilateEm = 0.05f;
	FDreamUIGeometry Geometry;
	TArray<FDreamUITextCharProperty> Chars;
	FDreamTextPainter::Paint(DL, Params, Geometry, Chars);
	if (!TestEqual(TEXT("three quads"), Geometry.OriginVertices.Num(), 3 * 4))return false;
	TestEqual(TEXT("the two small glyphs come from coverage"), Report.CoverageItems, 2);
	if (!TestEqual(TEXT("and only they are asked for"), Requests.Num(), 2))return false;
	TestTrue(TEXT("the italic one is asked for with synthetic italic"), Requests[0].Flags == EDreamUICoverageGlyphFlags::SyntheticItalic);
	TestTrue(TEXT("the bold one is asked for with synthetic bold"), Requests[1].Flags == EDreamUICoverageGlyphFlags::SyntheticBold);

	const FQuadView ItalicQuad = ReadQuad(Geometry, 0);
	TestTrue(TEXT("the italic coverage quad is not sheared"), ItalicQuad.BottomLeft.X == ItalicQuad.TopLeft.X && ItalicQuad.BottomRight.X == ItalicQuad.TopRight.X);

	const FQuadView BoldQuad = ReadQuad(Geometry, 1);
	const FGridPlace BoldPlace = PlaceOnGrid(Bold.Pen, 1.0f, FVector2f::ZeroVector);
	const FDreamUICoverageGlyph BoldGlyph = MakeCoverageGlyph('b', 12 * 64);
	TestEqual(TEXT("the bold coverage quad is not shifted"), (double)BoldQuad.BottomLeft.X, (double)(BoldPlace.Column + BoldGlyph.BitmapLeft), 1e-4);
	TestEqual(TEXT("and carries no dilation"), BoldQuad.Code, DreamTextQuadCode::CoverageBase + (float)BoldPlace.Phase);

	const FQuadView LargeQuad = ReadQuad(Geometry, 2);
	TestEqual(TEXT("the large italic glyph is a field quad, sheared by its height times the slope"),
		LargeQuad.TopLeft.X - LargeQuad.BottomLeft.X, (LargeQuad.TopLeft.Y - LargeQuad.BottomLeft.Y) * Params.ItalicSlope, 1e-4f);
	TestTrue(TEXT("with a field code"), LargeQuad.Code < DreamTextQuadCode::CoverageBase);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextCoverageOffIsUnchangedTest,
	"DreamGUI.Text.Coverage.WithCoverageOffThePaintIsWhatItWas",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Coverage off paints exactly what a paint that never heard of it does, whatever else the coverage block holds, and resets
 * the report. So does coverage on for a text whose quads come in several copies (it has effects): a coverage glyph is a
 * face alone, and the font is not even asked.
 */
bool FDreamTextCoverageOffIsUnchangedTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextCoverageTestLocal;
	FScopedGameWorld TestWorld;
	UDreamTextTestFont* Font = NewObject<UDreamTextTestFont>(TestWorld.World);
	TArray<FCoverageRequest> Requests;
	MockCoverage(Font, Requests);

	FDreamTextDisplayList DL;
	DL.Lines.AddDefaulted(1);
	DL.Items.Add(MakeGlyph('a', 0, FVector2f(1.1f, -14.2f), 12.0f));
	FDreamTextGlyphItem Italic = MakeGlyph('b', 1, FVector2f(8.3f, -14.2f), 12.0f);
	Italic.Style.bSyntheticItalic = true;
	DL.Items.Add(Italic);
	FDreamTextGlyphItem Bold = MakeGlyph('c', 2, FVector2f(15.4f, -14.2f), 12.0f);
	Bold.Style.bSyntheticBold = true;
	DL.Items.Add(Bold);

	FDreamTextPaintParams Plain;
	Plain.ItalicSlope = 0.25f;
	Plain.BaseColor = FColor(20, 30, 40, 200);
	Plain.bDistanceField = true;
	Plain.EmTexels = 64.0f;
	Plain.FieldSpreadTexels = 16.0f;
	Plain.QuadMarginTexels = 1.0f;
	Plain.TexelToUV = 1.0f / 2048.0f;
	Plain.BoldDilateEm = 0.05f;
	Plain.FaceReachEm = 0.05f;
	Plain.FillProgress = 0.4f;
	FDreamUIGeometry Reference;
	TArray<FDreamUITextCharProperty> ReferenceChars;
	FDreamTextPainter::Paint(DL, Plain, Reference, ReferenceChars);

	{
		FDreamTextCoverageReport Report;
		Report.CoverageItems = Report.PendingItems = 7;
		FDreamTextPaintParams Off = Plain;
		Off.Coverage = MakeCoverageParams(Font, 1.37f, FVector2f(0.31f, -2.9f), &Report).Coverage;
		Off.Coverage.bEnabled = false;
		Off.Coverage.Contrast = 3.0f;
		Off.Coverage.bLinearTarget = true;
		FDreamUIGeometry Geometry;
		TArray<FDreamUITextCharProperty> Chars;
		FDreamTextPainter::Paint(DL, Off, Geometry, Chars);
		TestTrue(TEXT("coverage off: the same quads"), SameGeometry(Geometry, Reference));
		TestTrue(TEXT("coverage off: the same characters"), SameCharProperties(Chars, ReferenceChars));
		TestTrue(TEXT("coverage off: the report is reset"), Report.CoverageItems == 0 && Report.PendingItems == 0);
		TestEqual(TEXT("coverage off: the font is not asked"), Requests.Num(), 0);
	}

	// On, but every item over the limit: nothing changes either.
	{
		FDreamTextCoverageReport Report;
		FDreamTextPaintParams Over = Plain;
		Over.Coverage = MakeCoverageParams(Font, 1.0f, FVector2f::ZeroVector, &Report).Coverage;
		Over.Coverage.MaxPixelSize = 8.0f;
		FDreamUIGeometry Geometry;
		TArray<FDreamUITextCharProperty> Chars;
		FDreamTextPainter::Paint(DL, Over, Geometry, Chars);
		TestTrue(TEXT("over the limit: the same quads"), SameGeometry(Geometry, Reference));
		TestTrue(TEXT("over the limit: nothing reported"), Report.CoverageItems == 0 && Report.PendingItems == 0);
	}

	// On, for a text with effects: its quads come as an effects copy and a face copy, which coverage cannot make.
	{
		FDreamTextPaintParams Effects = Plain;
		Effects.bSeparateEffectLayer = true;
		Effects.EffectReachEm = 0.1f;
		FDreamUIGeometry EffectsReference;
		TArray<FDreamUITextCharProperty> EffectsReferenceChars;
		FDreamTextPainter::Paint(DL, Effects, EffectsReference, EffectsReferenceChars);

		FDreamTextCoverageReport Report;
		FDreamTextPaintParams EffectsOn = Effects;
		EffectsOn.Coverage = MakeCoverageParams(Font, 1.0f, FVector2f::ZeroVector, &Report).Coverage;
		FDreamUIGeometry Geometry;
		TArray<FDreamUITextCharProperty> Chars;
		FDreamTextPainter::Paint(DL, EffectsOn, Geometry, Chars);
		TestTrue(TEXT("with effects: the same quads as without coverage"), SameGeometry(Geometry, EffectsReference));
		TestTrue(TEXT("with effects: the same characters"), SameCharProperties(Chars, EffectsReferenceChars));
		TestEqual(TEXT("with effects: the font is not asked"), Requests.Num(), 0);
		TestEqual(TEXT("with effects: nothing from coverage"), Report.CoverageItems, 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextCoverageFillTest,
	"DreamGUI.Text.Coverage.LyricFillRidesOnCoverageQuads",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Coverage quads carry the lyric fill like field quads: their segment's or line's progress in UV3.x, and in UV2.y their
 * own edges' place across the run, the run measured on the layout's quads -- so the lit edge falls at the same x in either
 * mode. UV3.y is the contrast there, not the glow boost: coverage glyphs have no glow.
 */
bool FDreamTextCoverageFillTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextCoverageTestLocal;
	FScopedGameWorld TestWorld;
	UDreamTextTestFont* Font = NewObject<UDreamTextTestFont>(TestWorld.World);
	TArray<FCoverageRequest> Requests;
	MockCoverage(Font, Requests);

	FDreamTextDisplayList DL;
	DL.Lines.AddDefaulted(1);
	for (int32 Index = 0; Index < 4; Index++)
	{
		DL.Items.Add(MakeGlyph('a' + Index, Index, FVector2f(0.35f + Index * 7.3f, -12.5f), 12.0f));
	}
	TArray<FDreamTextFillSegment> Segments;
	FDreamTextFillSegment Segment;
	Segment.StartCharIndex = 0;
	Segment.EndCharIndex = 1;
	Segment.Progress = 0.4f;
	Segment.GlowBoost = 2.0f;
	Segments.Add(Segment);

	FDreamTextCoverageReport Report;
	FDreamTextPaintParams Params = MakeCoverageParams(Font, 1.0f, FVector2f(0.1f, 0.2f), &Report);
	Params.FillSegments = &Segments;
	Params.FillProgress = 0.7f;
	Params.GlowBoost = 0.5f;
	Params.Coverage.Contrast = 1.5f;
	FDreamUIGeometry Geometry;
	TArray<FDreamUITextCharProperty> Chars;
	FDreamTextPainter::Paint(DL, Params, Geometry, Chars);
	if (!TestEqual(TEXT("four coverage quads"), Report.CoverageItems, 4) || Geometry.OriginVertices.Num() != 16)return false;

	// The runs, measured on the layout's quads: 'a' and 'b' in the segment, 'c' and 'd' on the line.
	auto RunOf = [&DL](int32 First, int32 Last)
	{
		FVector2f Run(FLT_MAX, -FLT_MAX);
		for (int32 Index = First; Index <= Last; Index++)
		{
			const FDreamTextGlyphItem& Item = DL.Items[Index];
			Run.X = FMath::Min(Run.X, Item.Pen.X + Item.Glyph.XOffset);
			Run.Y = FMath::Max(Run.Y, Item.Pen.X + Item.Glyph.XOffset + Item.Glyph.Width);
		}
		return Run;
	};
	const FVector2f SegmentRun = RunOf(0, 1);
	const FVector2f LineRun = RunOf(2, 3);
	for (int32 Index = 0; Index < 4; Index++)
	{
		const bool bInSegment = Index <= 1;
		const FVector2f Run = bInSegment ? SegmentRun : LineRun;
		const FQuadView Quad = ReadQuad(Geometry, Index);
		const FString What = FString::Printf(TEXT("glyph %d"), Index);
		TestTrue(*(What + TEXT(" is a coverage quad")), Quad.Code >= DreamTextQuadCode::CoverageBase);
		TestEqual(*(What + TEXT(" carries its run's progress")), Quad.UV3.X, bInSegment ? 0.4f : 0.7f);
		TestEqual(*(What + TEXT(" carries the contrast, not the glow boost")), Quad.UV3.Y, 1.5f);
		TestEqual(*(What + TEXT(": its left edge's place across the run")), Quad.RunX0, (Quad.BottomLeft.X - Run.X) / (Run.Y - Run.X), 1e-4f);
		TestEqual(*(What + TEXT(": its right edge's place across the run")), Quad.RunX1, (Quad.BottomRight.X - Run.X) / (Run.Y - Run.X), 1e-4f);
		TestEqual(*(What + TEXT(": the top-left vertex sweeps like the bottom-left")), Geometry.Vertices[Index * 4 + 2].TextureCoordinate[2].Y, Quad.RunX0);
	}
	return true;
}

#endif
