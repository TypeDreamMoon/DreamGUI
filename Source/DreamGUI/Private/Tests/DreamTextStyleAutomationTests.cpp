// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Core/DreamUIGeometry.h"
#include "Core/DreamUITextData.h"
#include "Core/Text/DreamTextLayout.h"
#include "Core/Text/DreamTextPainter.h"
#include "Engine/World.h"
#include "Math/Float16.h"
#include "Tests/DreamTextTestFont.h"

/*
 * The text style record and the painter's fill channels: the two halves of the built-in shader's
 * text effects that can be checked without a GPU. The record is decoded here exactly the way
 * DreamUIText.ush decodes it, so a layout change on either side fails the test.
 */
namespace DreamTextStyleTestLocal
{
	struct FScopedGameWorld
	{
		UWorld* World = nullptr;
		FScopedGameWorld() { World = UWorld::CreateWorld(EWorldType::Game, false); }
		~FScopedGameWorld() { if (World) { World->DestroyWorld(false); } }
	};

	// DreamUIText_UnpackHalf2: x from the high 16 bits, y from the low 16.
	FVector2f UnpackHalf2(uint32 Packed)
	{
		FFloat16 X, Y;
		X.Encoded = (Packed >> 16) & 0xffff;
		Y.Encoded = Packed & 0xffff;
		return FVector2f(X.GetFloat(), Y.GetFloat());
	}
	// DreamUIText_UnpackColor: r = bits 16..23, g = 8..15, b = 0..7, a = 24..31.
	FColor UnpackColor(uint32 Packed)
	{
		return FColor((Packed >> 16) & 0xff, (Packed >> 8) & 0xff, Packed & 0xff, (Packed >> 24) & 0xff);
	}

	FDreamTextLayoutInput MakeInput(UDreamUIFontData_BaseObject* Font, const FString& Content)
	{
		FDreamTextLayoutInput In;
		In.Content = Content;
		In.Width = 600.0f;
		In.Height = 120.0f;
		In.Pivot = FVector2f(0.5f, 0.5f);
		In.FontSize = 24.0f;
		In.ParagraphHAlign = EDreamUITextParagraphHorizontalAlign::Left;
		In.ParagraphVAlign = EDreamUITextParagraphVerticalAlign::Top;
		In.Font = Font;
		return In;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextStylePackRoundTripTest,
	"DreamGUI.Text.Style.PackMatchesShaderLayout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextStylePackRoundTripTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextStyleTestLocal;

	FDreamTextStyle Style;
	Style.FaceSoftness = 0.125f;
	Style.FaceDilate = -0.0625f;
	Style.OutlineColor = FColor(10, 20, 30, 200);
	Style.OutlineWidth = 0.25f;
	Style.OutlineSoftness = 0.5f;
	Style.UnderlayColor = FColor(255, 0, 128, 64);
	Style.UnderlayOffset = FVector2f(0.75f, -0.25f);
	Style.UnderlaySoftness = 1.0f;
	Style.UnderlayDilate = 0.375f;
	Style.GlowColor = FColor(1, 2, 3, 4);
	Style.GlowWidth = 2.0f;
	Style.GlowPower = 3.0f;
	Style.FillDimAlpha = 0.5f;
	Style.FillFadeWidth = 0.0625f;

	TArray<uint8> Bytes;
	Style.Pack(Bytes);
	TestEqual(TEXT("packed size"), Bytes.Num(), FDreamTextStyle::PackedPixelCount * 4);
	if (Bytes.Num() != FDreamTextStyle::PackedPixelCount * 4)return false;
	uint32 Pixels[FDreamTextStyle::PackedPixelCount];
	FMemory::Memcpy(Pixels, Bytes.GetData(), sizeof(Pixels));

	// Every value above is exactly representable in half precision, so equality is exact.
	TestEqual(TEXT("face"), UnpackHalf2(Pixels[0]), FVector2f(0.125f, -0.0625f));
	TestEqual(TEXT("outline colour"), UnpackColor(Pixels[1]), FColor(10, 20, 30, 200));
	TestEqual(TEXT("outline"), UnpackHalf2(Pixels[2]), FVector2f(0.25f, 0.5f));
	TestEqual(TEXT("underlay colour"), UnpackColor(Pixels[3]), FColor(255, 0, 128, 64));
	TestEqual(TEXT("underlay offset"), UnpackHalf2(Pixels[4]), FVector2f(0.75f, -0.25f));
	TestEqual(TEXT("underlay"), UnpackHalf2(Pixels[5]), FVector2f(1.0f, 0.375f));
	TestEqual(TEXT("glow colour"), UnpackColor(Pixels[6]), FColor(1, 2, 3, 4));
	TestEqual(TEXT("glow"), UnpackHalf2(Pixels[7]), FVector2f(2.0f, 3.0f));
	TestEqual(TEXT("fill"), UnpackHalf2(Pixels[8]), FVector2f(0.5f, 0.0625f));

	// The record slots the style occupies must be the ones the shader reads (pixels 4..12).
	TestEqual(TEXT("record start"), FDreamTextStyle::PackedPixelStart, 4);
	TestEqual(TEXT("record length"), FDreamTextStyle::PackedPixelStart + FDreamTextStyle::PackedPixelCount, 13);

	FDreamTextStyle Same = Style;
	TestTrue(TEXT("equality"), Same == Style);
	Same.GlowPower = 4.0f;
	TestTrue(TEXT("inequality"), Same != Style);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextPainterFillChannelsTest,
	"DreamGUI.Text.Painter.FillChannelsSweepEachRun",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextPainterFillChannelsTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextStyleTestLocal;
	FScopedGameWorld TestWorld;
	UDreamTextTestFont* Font = NewObject<UDreamTextTestFont>(TestWorld.World);

	FDreamTextDisplayList DL;
	FDreamTextLayoutInput In = MakeInput(Font, TEXT("Hello world"));
	FDreamTextLayoutEngine::Layout(In, DL);

	// No segments: the whole line is one run, fully lit, and UV2.y sweeps 0..1 across it.
	{
		FDreamUIGeometry Geometry;
		TArray<FDreamUITextCharProperty> Chars;
		FDreamTextPaintParams Params;
		FDreamTextPainter::Paint(DL, Params, Geometry, Chars);
		if (!TestTrue(TEXT("painted something"), Geometry.Vertices.Num() >= 8))return false;

		float PrevRight = -1.0f;
		bool bMonotonic = true, bAllLit = true;
		for (int32 Quad = 0; Quad < Geometry.Vertices.Num() / 4; Quad++)
		{
			const FVector2f& UV2Left = Geometry.Vertices[Quad * 4].TextureCoordinate[2];
			const FVector2f& UV2Right = Geometry.Vertices[Quad * 4 + 1].TextureCoordinate[2];
			bMonotonic &= UV2Left.Y >= PrevRight - KINDA_SMALL_NUMBER && UV2Right.Y >= UV2Left.Y;
			PrevRight = UV2Right.Y;
			for (int32 i = 0; i < 4; i++)
			{
				const FVector2f& UV3 = Geometry.Vertices[Quad * 4 + i].TextureCoordinate[3];
				bAllLit &= UV3.X == 1.0f && UV3.Y == 0.0f;
			}
		}
		TestTrue(TEXT("run position increases left to right"), bMonotonic);
		TestTrue(TEXT("default fill is lit with no glow boost"), bAllLit);
		TestEqual(TEXT("first glyph starts the run"), Geometry.Vertices[0].TextureCoordinate[2].Y, 0.0f, 0.001f);
		TestEqual(TEXT("last glyph ends the run"), Geometry.Vertices[Geometry.Vertices.Num() - 3].TextureCoordinate[2].Y, 1.0f, 0.001f);
	}

	// A segment over "Hello" (chars 0..4) carries its own progress and boost and its own 0..1 sweep;
	// "world" stays on the line's default.
	{
		TArray<FDreamTextFillSegment> Segments;
		FDreamTextFillSegment Seg;
		Seg.StartCharIndex = 0;
		Seg.EndCharIndex = 4;
		Seg.Progress = 0.5f;
		Seg.GlowBoost = 2.0f;
		Segments.Add(Seg);

		FDreamUIGeometry Geometry;
		TArray<FDreamUITextCharProperty> Chars;
		FDreamTextPaintParams Params;
		Params.FillSegments = &Segments;
		Params.FillProgress = 0.25f;
		FDreamTextPainter::Paint(DL, Params, Geometry, Chars);

		// Map the emitted quads back to characters through the char properties.
		int32 HelloQuads = 0, WorldQuads = 0;
		bool bHelloOk = true, bWorldOk = true;
		float HelloMin = 2.0f, HelloMax = -1.0f, WorldMin = 2.0f, WorldMax = -1.0f;
		for (const auto& Char : Chars)
		{
			for (int32 V = Char.StartVertIndex; V < Char.StartVertIndex + Char.VertCount; V++)
			{
				const FVector2f& UV2 = Geometry.Vertices[V].TextureCoordinate[2];
				const FVector2f& UV3 = Geometry.Vertices[V].TextureCoordinate[3];
				if (Char.CharIndex <= 4)
				{
					bHelloOk &= UV3.X == 0.5f && UV3.Y == 2.0f;
					HelloMin = FMath::Min(HelloMin, UV2.Y);
					HelloMax = FMath::Max(HelloMax, UV2.Y);
				}
				else if (Char.CharIndex >= 6)
				{
					bWorldOk &= UV3.X == 0.25f && UV3.Y == 0.0f;
					WorldMin = FMath::Min(WorldMin, UV2.Y);
					WorldMax = FMath::Max(WorldMax, UV2.Y);
				}
			}
			if (Char.CharIndex <= 4)HelloQuads++; else if (Char.CharIndex >= 6)WorldQuads++;
		}
		TestEqual(TEXT("five glyphs in Hello"), HelloQuads, 5);
		TestEqual(TEXT("five glyphs in world"), WorldQuads, 5);
		TestTrue(TEXT("segment glyphs carry the segment's progress and boost"), bHelloOk);
		TestTrue(TEXT("glyphs outside the segment carry the text default"), bWorldOk);
		TestEqual(TEXT("segment sweep starts at 0"), HelloMin, 0.0f, 0.001f);
		TestEqual(TEXT("segment sweep ends at 1"), HelloMax, 1.0f, 0.001f);
		// "world" is the remainder of the line's run: its sweep starts past where the line's own
		// run would start, because the line run only spans the glyphs outside every segment.
		TestEqual(TEXT("line-run sweep starts at 0"), WorldMin, 0.0f, 0.001f);
		TestEqual(TEXT("line-run sweep ends at 1"), WorldMax, 1.0f, 0.001f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextPainterEffectLayerTest,
	"DreamGUI.Text.Painter.EffectLayerDrawsBeforeFaces",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextPainterEffectLayerTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextStyleTestLocal;
	FScopedGameWorld TestWorld;
	UDreamTextTestFont* Font = NewObject<UDreamTextTestFont>(TestWorld.World);

	FDreamTextDisplayList DL;
	FDreamTextLayoutInput In = MakeInput(Font, TEXT("<b>ab</b>c"));
	In.bRichText = true;
	FDreamTextLayoutEngine::Layout(In, DL);

	// A multi-channel font at 48 texels per em with a 16 texel spread; the layout's quads keep 1 texel of it.
	FDreamTextPaintParams Params;
	Params.bMultiChannelField = true;
	Params.EmTexels = 48.0f;
	Params.FieldSpreadTexels = 16.0f;
	Params.QuadMarginTexels = 1.0f;
	Params.TexelToUV = 1.0f / 2048.0f;
	Params.BoldDilateEm = 0.04f;
	Params.FaceReachEm = 0.04f;

	// Reference: the same text painted as one layer with no growth.
	FDreamUIGeometry Plain;
	TArray<FDreamUITextCharProperty> PlainChars;
	{
		FDreamTextPaintParams PlainParams = Params;
		PlainParams.bMultiChannelField = false;
		PlainParams.BoldDilateEm = 0.0f;
		FDreamTextPainter::Paint(DL, PlainParams, Plain, PlainChars);
	}
	const int32 GlyphCount = Plain.Vertices.Num() / 4;
	if (!TestEqual(TEXT("three glyphs"), GlyphCount, 3))return false;

	// With effects: every glyph gets an effect quad and a face quad; the effect indices all come first.
	Params.bSeparateEffectLayer = true;
	Params.EffectReachEm = 0.25f;// 0.25 em = 12 texels + 1.5 margin = 13.5 > the 16 - 1 available? no: 13.5 - 1 margin = 12.5 grown
	FDreamUIGeometry Geometry;
	TArray<FDreamUITextCharProperty> Chars;
	FDreamTextPainter::Paint(DL, Params, Geometry, Chars);
	TestEqual(TEXT("two quads per glyph"), Geometry.Vertices.Num(), GlyphCount * 8);
	TestEqual(TEXT("two quads' indices per glyph"), Geometry.Triangles.Num(), GlyphCount * 12);
	if (Geometry.Vertices.Num() != GlyphCount * 8)return false;

	// Layer codes: vertex quads alternate effect / face per glyph, written in that order.
	bool bLayersOk = true;
	for (int32 Glyph = 0; Glyph < GlyphCount; Glyph++)
	{
		const float EffectUV2 = Geometry.Vertices[Glyph * 8].TextureCoordinate[2].X;
		const float FaceUV2 = Geometry.Vertices[Glyph * 8 + 4].TextureCoordinate[2].X;
		const int32 EffectLayer = FMath::FloorToInt((EffectUV2 + 8.0f) / 16.0f);
		const int32 FaceLayer = FMath::FloorToInt((FaceUV2 + 8.0f) / 16.0f);
		bLayersOk &= EffectLayer == 1 && FaceLayer == 0;
	}
	TestTrue(TEXT("effect quads are layer 1, face quads layer 0"), bLayersOk);

	// Index order: the first half of the index buffer only references effect quads, the second only faces.
	bool bOrderOk = true;
	const int32 Half = Geometry.Triangles.Num() / 2;
	for (int32 i = 0; i < Geometry.Triangles.Num(); i++)
	{
		const int32 QuadOfVertex = Geometry.Triangles[i] / 4;// quads alternate: even = effect, odd = face
		const bool bEffectQuad = (QuadOfVertex % 2) == 0;
		bOrderOk &= (i < Half) ? bEffectQuad : !bEffectQuad;
	}
	TestTrue(TEXT("all effect triangles precede all face triangles"), bOrderOk);

	// Characters stay contiguous in vertices (both quads) and their triangle range is the face block's.
	TestEqual(TEXT("three characters"), Chars.Num(), 3);
	bool bCharsOk = Chars.Num() == 3;
	for (int32 i = 0; i < Chars.Num() && bCharsOk; i++)
	{
		bCharsOk &= Chars[i].StartVertIndex == i * 8 && Chars[i].VertCount == 8;
		bCharsOk &= Chars[i].StartTriangleIndex == Half + i * 6 && Chars[i].IndicesCount == 6;
	}
	TestTrue(TEXT("char properties span both quads and point at the face triangles"), bCharsOk);

	// Growth: the effect quad of 'c' (plain, not bold) is wider than the reference quad by the clamped
	// reach on each side: need 0.25 em * 48 + 1.5 = 13.5 texels, 1 already there, 15 available -> 12.5 texels.
	{
		const float Size = 24.0f;
		const float Expected = 12.5f * Size / 48.0f;
		const FVector3f& RefLeft = Plain.OriginVertices[2 * 4].Position;
		const FVector3f& RefRight = Plain.OriginVertices[2 * 4 + 1].Position;
		const FVector3f& GrownLeft = Geometry.OriginVertices[2 * 8].Position;
		const FVector3f& GrownRight = Geometry.OriginVertices[2 * 8 + 1].Position;
		TestEqual(TEXT("effect quad grows left by the reach"), RefLeft.Y - GrownLeft.Y, Expected, 0.01f);
		TestEqual(TEXT("effect quad grows right by the reach"), GrownRight.Y - RefRight.Y, Expected, 0.01f);
		// UVs grow by the same number of texels.
		const float RefU = Plain.Vertices[2 * 4].TextureCoordinate[0].X;
		const float GrownU = Geometry.Vertices[2 * 8].TextureCoordinate[0].X;
		TestEqual(TEXT("effect quad UV grows with it"), RefU - GrownU, 12.5f / 2048.0f, 0.00001f);
	}
	// Clamp: a reach the field cannot hold stops at the spread.
	{
		FDreamTextPaintParams Wide = Params;
		Wide.EffectReachEm = 2.0f;
		FDreamUIGeometry WideGeometry;
		TArray<FDreamUITextCharProperty> WideChars;
		FDreamTextPainter::Paint(DL, Wide, WideGeometry, WideChars);
		const float Expected = 15.0f * 24.0f / 48.0f;
		const float RefLeft = Plain.OriginVertices[2 * 4].Position.Y;
		const float GrownLeft = WideGeometry.OriginVertices[2 * 8].Position.Y;
		TestEqual(TEXT("growth is clamped to the spread"), RefLeft - GrownLeft, Expected, 0.01f);
	}

	// Shader bold: 'a' is bold, so its quads carry the dilate and sit BoldDilateEm * size further right;
	// 'c' is not, so its dilate is zero and it sits where the plain paint put it.
	{
		const float BoldUV2 = Geometry.Vertices[0 * 8 + 4].TextureCoordinate[2].X;
		const float PlainUV2 = Geometry.Vertices[2 * 8 + 4].TextureCoordinate[2].X;
		const float BoldDilate = BoldUV2 - 16.0f * FMath::FloorToInt((BoldUV2 + 8.0f) / 16.0f);
		const float PlainDilate = PlainUV2 - 16.0f * FMath::FloorToInt((PlainUV2 + 8.0f) / 16.0f);
		TestEqual(TEXT("bold glyph packs its dilate"), BoldDilate, 0.04f, 0.0001f);
		TestEqual(TEXT("plain glyph packs no dilate"), PlainDilate, 0.0f, 0.0001f);
		// Face quads of 'a': grown by FaceReachEm (0.04 em * 48 + 1.5 - 1 = 2.42 texels) and shifted by 0.04 em.
		const float Size = 24.0f;
		const float Grow = 2.42f * Size / 48.0f;
		const float Shift = 0.04f * Size;
		const float RefLeft = Plain.OriginVertices[0].Position.Y;
		const float FaceLeft = Geometry.OriginVertices[4].Position.Y;
		TestEqual(TEXT("bold glyph shifts right by one side's dilation"), FaceLeft - RefLeft, Shift - Grow, 0.01f);
	}
	return true;
}

#endif
