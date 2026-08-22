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

#endif
