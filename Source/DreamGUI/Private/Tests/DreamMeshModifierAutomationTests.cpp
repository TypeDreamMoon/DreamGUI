// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/DreamUIGeometry.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamImage.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamVisualBatchMesh.h"
#include "Core/Components/DreamWidget.h"
#include "MeshModifier/DreamMeshModifierBase.h"
#include "MeshModifier/DreamMeshModifierGradientColor.h"
#include "MeshModifier/DreamMeshModifierLongShadow.h"
#include "MeshModifier/DreamMeshModifierOutline.h"
#include "MeshModifier/DreamMeshModifierPositionAsUV.h"
#include "MeshModifier/DreamMeshModifierShadow.h"
#include "MeshModifier/DreamMeshModifierTextAnimation.h"
#include "MeshModifier/TextAnimation/DreamMeshModifierTextAnimation_PropertyWithEase.h"
#include "MeshModifier/TextAnimation/DreamMeshModifierTextAnimation_PropertyWithWave.h"
#include "MeshModifier/TextAnimation/DreamMeshModifierTextAnimation_Selector.h"
#include "UObject/Package.h"

/*
 * Mesh modifiers: what a test can hold onto, and what it cannot.
 *
 * A modifier is a filter over geometry that has ALREADY been built. Its whole job is one function --
 * ModifyUIGeometry(FDreamUIGeometry&, ...) -- which is public, takes its input by reference, and for
 * every native modifier except PositionAsUV and TextAnimation reads nothing off the widget at all.
 * So the interesting half of this family can be driven directly against a geometry built by hand: no
 * canvas, no world, no render thread. That is deliberate, because the suite runs -nullrhi and a test
 * claiming something about the picture would be measuring its own arithmetic.
 *
 * What IS testable is the part with a decision in it, and in this family the decisions are counts and
 * ordering. Outline and Shadow do not tint anything -- they DUPLICATE the mesh, one copy per
 * direction, and lay the copies out in the index buffer so the original draws last and therefore on
 * top. Get the vertex offset applied to a copy's indices wrong and the outline is drawn from another
 * glyph's vertices; get the ordering wrong and the shadow covers the thing casting it. Neither shows
 * up as a crash, and neither is visible from a vertex count alone, which is why the tests below check
 * the index remap rather than just the array lengths.
 *
 * Two of the assertions below are about writes that go too far, and they are worth reading with the
 * layout in hand. A vertex is Position, Colour, four texture coordinates, then two packed tangents,
 * so a loop that copies eight coordinates out of a four-coordinate array writes over its own tangents
 * and then over the head of the vertex behind it. That is what the shadow used to do, and its upper
 * end -- the last vertex, whose overrun leaves the allocation entirely -- cannot be asserted on from
 * inside the process. So the guard here is arithmetic on the near side plus a fixture that no longer
 * over-reserves: if the bound comes back, the suite corrupts the heap instead of quietly absorbing it.
 * The gradient's overrun is the same shape one level up and IS provoked, by a vertex count that is not
 * a whole number of quads.
 */
namespace DreamMeshModifierTestLocal
{
	constexpr int32 VertsPerQuad = 4;
	constexpr int32 IndicesPerQuad = 6;

	/**
	 * A geometry of whole quads, which is the only shape a DreamGUI batch mesh ever emits: four
	 * vertices and six indices per quad, wound the way FDreamUIGeometry documents. Positions live in
	 * the UI plane -- Y across, Z up, X depth -- because that is the convention the offsets in
	 * Outline, Shadow and LongShadow are written against.
	 */
	static void BuildQuads(FDreamUIGeometry& Geo, int32 QuadCount, const FColor& InColor = FColor::White)
	{
		Geo.Clear();
		Geo.OriginVertices.Reserve(QuadCount * VertsPerQuad);
		Geo.Vertices.Reserve(QuadCount * VertsPerQuad);
		Geo.Triangles.Reserve(QuadCount * IndicesPerQuad);

		for (int32 Quad = 0; Quad < QuadCount; Quad++)
		{
			const int32 Base = Quad * VertsPerQuad;
			const float Left = Quad * 20.0f;
			const FVector3f Corners[VertsPerQuad] = {
				FVector3f(0.0f, Left, 0.0f),
				FVector3f(0.0f, Left + 10.0f, 0.0f),
				FVector3f(0.0f, Left, 10.0f),
				FVector3f(0.0f, Left + 10.0f, 10.0f),
			};
			for (int32 Corner = 0; Corner < VertsPerQuad; Corner++)
			{
				Geo.OriginVertices.Add(FDreamUIOriginVertexData(Corners[Corner]));
				FDreamUIMeshVertex Vertex(Corners[Corner], InColor);
				for (int32 Channel = 0; Channel < LEXUI_VERTEX_TEXCOORDINATE_COUNT; Channel++)
				{
					// Unique per (vertex, channel), so a copy that sourced its UV from the wrong
					// vertex is visible rather than accidentally correct.
					Vertex.TextureCoordinate[Channel] = FVector2f((float)(Base + Corner), (float)Channel);
				}
				Geo.Vertices.Add(Vertex);
			}
			const int32 Order[IndicesPerQuad] = { 0, 3, 2, 0, 1, 3 };
			for (int32 Index = 0; Index < IndicesPerQuad; Index++)
			{
				Geo.Triangles.Add((FDreamUIMeshIndex)(Base + Order[Index]));
			}
		}
	}

	/** FVector3f has no TestEqual of its own; widen rather than compare three floats at each site. */
	static FVector AsVector(const FVector3f& In) { return FVector(In.X, In.Y, In.Z); }

	/** A widget owning one visual, torn down by leaving scope. No world and no registration. */
	struct FScopedVisualWidget
	{
		UDreamWidgetTree* Tree = nullptr;
		UDreamWidget* Widget = nullptr;

		FScopedVisualWidget(UClass* InVisualClass, const TCHAR* InName)
		{
			Tree = NewObject<UDreamWidgetTree>(GetTransientPackage());
			Widget = Tree->ConstructWidget(UDreamWidget::StaticClass(), FName(InName));
			if (Widget != nullptr && InVisualClass != nullptr)
			{
				Widget->CreateNewVisual(InVisualClass);
			}
		}
		~FScopedVisualWidget()
		{
			if (Widget != nullptr)
			{
				Widget->DestroyWidget();
			}
		}
		UDreamVisualBatchMesh* Mesh() const
		{
			return Widget != nullptr ? Cast<UDreamVisualBatchMesh>(Widget->GetVisual()) : nullptr;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamMeshModifierGradientDirectionTest,
	"DreamGUI.MeshModifier.EachGradientDirectionPaintsTheQuadCornersInItsOwnOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMeshModifierGradientDirectionTest::RunTest(const FString& Parameters)
{
	using namespace DreamMeshModifierTestLocal;

	// The gradient never looks at a position. It assumes the emitter laid the quad's four corners out
	// bottom-left, bottom-right, top-left, top-right and paints by INDEX, which is the whole reason
	// the five directions are five different index patterns rather than five different vectors. That
	// makes the mapping the contract: swap two of them and every gradient in the project flips.
	UDreamMeshModifierGradientColor* Gradient =
		NewObject<UDreamMeshModifierGradientColor>(GetTransientPackage());
	Gradient->SetMultiplySourceAlpha(false);
	Gradient->SetColor1(FColor(10, 0, 0, 255));
	Gradient->SetColor2(FColor(20, 0, 0, 255));
	Gradient->SetColor3(FColor(30, 0, 0, 255));
	Gradient->SetColor4(FColor(40, 0, 0, 255));

	auto PaintOneQuad = [&](EDreamMeshModifierGradientColorDirection InDirection)
	{
		Gradient->SetDirectionType(InDirection);
		FDreamUIGeometry Geo;
		BuildQuads(Geo, 1);
		Gradient->ModifyUIGeometry(Geo, false, false, true, false);
		return Geo;
	};

	{
		const FDreamUIGeometry Geo = PaintOneQuad(EDreamMeshModifierGradientColorDirection::BottomToTop);
		TestEqual(TEXT("bottom-left takes colour 1"), Geo.Vertices[0].Color, Gradient->GetColor1());
		TestEqual(TEXT("bottom-right takes colour 1"), Geo.Vertices[1].Color, Gradient->GetColor1());
		TestEqual(TEXT("top-left takes colour 2"), Geo.Vertices[2].Color, Gradient->GetColor2());
		TestEqual(TEXT("top-right takes colour 2"), Geo.Vertices[3].Color, Gradient->GetColor2());
	}
	{
		// The exact inverse of the one above, and the pair that is easiest to get backwards.
		const FDreamUIGeometry Geo = PaintOneQuad(EDreamMeshModifierGradientColorDirection::TopToBottom);
		TestEqual(TEXT("bottom-left takes colour 2"), Geo.Vertices[0].Color, Gradient->GetColor2());
		TestEqual(TEXT("top-right takes colour 1"), Geo.Vertices[3].Color, Gradient->GetColor1());
	}
	{
		const FDreamUIGeometry Geo = PaintOneQuad(EDreamMeshModifierGradientColorDirection::LeftToRight);
		TestEqual(TEXT("the left column takes colour 1 at the bottom"), Geo.Vertices[0].Color, Gradient->GetColor1());
		TestEqual(TEXT("the right column takes colour 2 at the bottom"), Geo.Vertices[1].Color, Gradient->GetColor2());
		TestEqual(TEXT("the left column takes colour 1 at the top"), Geo.Vertices[2].Color, Gradient->GetColor1());
		TestEqual(TEXT("the right column takes colour 2 at the top"), Geo.Vertices[3].Color, Gradient->GetColor2());
	}
	{
		const FDreamUIGeometry Geo = PaintOneQuad(EDreamMeshModifierGradientColorDirection::RightToLeft);
		TestEqual(TEXT("and the mirrored direction swaps the columns"), Geo.Vertices[0].Color, Gradient->GetColor2());
		TestEqual(TEXT("on both rows"), Geo.Vertices[3].Color, Gradient->GetColor1());
	}
	{
		// Four corners is the only direction where all four properties are reachable, so it is the
		// only one that can prove colour 3 and colour 4 are wired to a corner at all.
		const FDreamUIGeometry Geo = PaintOneQuad(EDreamMeshModifierGradientColorDirection::FourCorner);
		TestEqual(TEXT("corner 1"), Geo.Vertices[0].Color, Gradient->GetColor1());
		TestEqual(TEXT("corner 2"), Geo.Vertices[1].Color, Gradient->GetColor2());
		TestEqual(TEXT("corner 3"), Geo.Vertices[2].Color, Gradient->GetColor3());
		TestEqual(TEXT("corner 4"), Geo.Vertices[3].Color, Gradient->GetColor4());
	}

	// Every quad is painted, not just the first: a text run is one quad per glyph and a gradient that
	// stopped after four vertices would tint the first letter and leave the word behind it untouched.
	{
		Gradient->SetDirectionType(EDreamMeshModifierGradientColorDirection::BottomToTop);
		FDreamUIGeometry Geo;
		BuildQuads(Geo, 3);
		Gradient->ModifyUIGeometry(Geo, false, false, true, false);
		TestEqual(TEXT("the last quad's bottom edge is painted too"), Geo.Vertices[8].Color, Gradient->GetColor1());
		TestEqual(TEXT("and its top edge"), Geo.Vertices[11].Color, Gradient->GetColor2());
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamMeshModifierGradientPartialQuadTest,
	"DreamGUI.MeshModifier.AGradientStopsAtTheLastWholeQuadRatherThanTheLastVertex",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMeshModifierGradientPartialQuadTest::RunTest(const FString& Parameters)
{
	using namespace DreamMeshModifierTestLocal;

	// The gradient is the one modifier in this family that consumes a vertex count it did not produce
	// itself: it runs after every modifier ahead of it in the list, any of which may have appended
	// vertices in any number. It paints four at a time, so a count that is not a whole number of quads
	// leaves a tail, and a loop that checks only the first corner of each group of four steps straight
	// off the end of the array with the remaining three.
	//
	// Six vertices is the smallest count that shows it: four make one quad, and the two left over pass
	// a first-corner test and then reach for two vertices that do not exist.
	UDreamMeshModifierGradientColor* Gradient =
		NewObject<UDreamMeshModifierGradientColor>(GetTransientPackage());
	Gradient->SetMultiplySourceAlpha(false);
	// All four tints share a zero green channel, so "was this vertex painted" is one comparison that
	// holds for every direction, including the four-corner one where colours 3 and 4 are reachable.
	Gradient->SetColor1(FColor(11, 0, 0, 255));
	Gradient->SetColor2(FColor(22, 0, 0, 255));
	Gradient->SetColor3(FColor(33, 0, 0, 255));
	Gradient->SetColor4(FColor(44, 0, 0, 255));

	for (int32 Direction = 0; Direction <= (int32)EDreamMeshModifierGradientColorDirection::FourCorner; Direction++)
	{
		Gradient->SetDirectionType((EDreamMeshModifierGradientColorDirection)Direction);

		FDreamUIGeometry Geo;
		BuildQuads(Geo, 2, FColor(200, 200, 200, 255));
		Geo.Vertices.SetNum(6);
		Geo.OriginVertices.SetNum(6);

		Gradient->ModifyUIGeometry(Geo, false, false, true, false);

		TestEqual(TEXT("the whole quad is still painted"), (int32)Geo.Vertices[0].Color.G, 0);
		TestEqual(TEXT("all four corners of it"), (int32)Geo.Vertices[3].Color.G, 0);
		// The tail keeps the colour it arrived with. Painting a partial quad would be a guess about
		// which corner each leftover vertex is, and the gradient has no way to make that guess.
		TestEqual(TEXT("and the two vertices that do not complete a quad are left alone"),
			Geo.Vertices[4].Color, FColor(200, 200, 200, 255));
		TestEqual(TEXT("both of them"), Geo.Vertices[5].Color, FColor(200, 200, 200, 255));
		TestEqual(TEXT("with nothing added to the mesh"), Geo.Vertices.Num(), 6);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamMeshModifierGradientAlphaTest,
	"DreamGUI.MeshModifier.AGradientCanTintWithoutThrowingAwayTheSourceAlpha",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMeshModifierGradientAlphaTest::RunTest(const FString& Parameters)
{
	using namespace DreamMeshModifierTestLocal;

	// Multiply-source-alpha is what makes a gradient composable with everything upstream of it: the
	// widget's own alpha, a fade, a text animation's per-character alpha. Replacing the alpha instead
	// of scaling it makes a faded-out widget snap back to opaque the moment somebody adds a gradient,
	// which is a bug that only ever shows up mid-transition.
	UDreamMeshModifierGradientColor* Gradient =
		NewObject<UDreamMeshModifierGradientColor>(GetTransientPackage());
	Gradient->SetDirectionType(EDreamMeshModifierGradientColorDirection::BottomToTop);
	Gradient->SetColor1(FColor(1, 2, 3, 128));
	Gradient->SetColor2(FColor(1, 2, 3, 128));

	{
		Gradient->SetMultiplySourceAlpha(true);
		FDreamUIGeometry Geo;
		BuildQuads(Geo, 1, FColor(200, 200, 200, 255));
		Gradient->ModifyUIGeometry(Geo, false, false, true, false);
		TestEqual(TEXT("the tint supplies the colour"), (int32)Geo.Vertices[0].Color.R, 1);
		TestEqual(TEXT("an opaque source keeps the tint's own alpha"), (int32)Geo.Vertices[0].Color.A, 128);
	}
	{
		// A fully transparent source stays transparent however opaque the tint is -- the direction
		// that actually matters, since it is the one that would make an invisible widget reappear.
		Gradient->SetColor1(FColor(1, 2, 3, 255));
		Gradient->SetColor2(FColor(1, 2, 3, 255));
		FDreamUIGeometry Geo;
		BuildQuads(Geo, 1, FColor(200, 200, 200, 0));
		Gradient->ModifyUIGeometry(Geo, false, false, true, false);
		TestEqual(TEXT("a transparent source stays transparent"), (int32)Geo.Vertices[0].Color.A, 0);
	}
	{
		// And with the flag off the alpha IS replaced, which is the whole difference between the two
		// modes and the reason the flag exists.
		Gradient->SetMultiplySourceAlpha(false);
		FDreamUIGeometry Geo;
		BuildQuads(Geo, 1, FColor(200, 200, 200, 0));
		Gradient->ModifyUIGeometry(Geo, false, false, true, false);
		TestEqual(TEXT("without multiplying, the tint's alpha wins outright"), (int32)Geo.Vertices[0].Color.A, 255);
	}

	// A modifier declares which of the four vertex streams it touches so the batch mesh can skip
	// recomputing the rest. Over-declaring costs a rebuild; UNDER-declaring means the stream this
	// modifier writes is never marked dirty, so the declaration is part of the contract.
	{
		bool bTriangles = false, bPosition = false, bUV = false, bColor = false;
		Gradient->ModifierWillChangeVertexData(bTriangles, bPosition, bUV, bColor);
		TestTrue(TEXT("the gradient declares that it writes colour"), bColor);
		TestFalse(TEXT("and nothing else: no triangles"), bTriangles);
		TestFalse(TEXT("no positions"), bPosition);
		TestFalse(TEXT("no UVs"), bUV);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamMeshModifierEmptyGeometryTest,
	"DreamGUI.MeshModifier.EveryDuplicatingModifierLeavesAnEmptyGeometryEmpty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMeshModifierEmptyGeometryTest::RunTest(const FString& Parameters)
{
	using namespace DreamMeshModifierTestLocal;

	// Empty geometry is not an edge case here, it is the steady state: a widget is sized zero, or its
	// text is blank, or the emitter has not run yet, and the modifier still gets called every rebuild.
	// Each of these four multiplies a count by a channel count and then indexes with the result, so
	// the early-out is the only thing standing between an empty text and an array walked from a
	// negative or zero-derived offset.
	FDreamUIGeometry Geo;

	NewObject<UDreamMeshModifierGradientColor>(GetTransientPackage())
		->ModifyUIGeometry(Geo, true, true, true, true);
	NewObject<UDreamMeshModifierOutline>(GetTransientPackage())
		->ModifyUIGeometry(Geo, true, true, true, true);
	NewObject<UDreamMeshModifierShadow>(GetTransientPackage())
		->ModifyUIGeometry(Geo, true, true, true, true);
	NewObject<UDreamMeshModifierLongShadow>(GetTransientPackage())
		->ModifyUIGeometry(Geo, true, true, true, true);

	TestEqual(TEXT("no vertices were invented"), Geo.Vertices.Num(), 0);
	TestEqual(TEXT("no origin vertices were invented"), Geo.OriginVertices.Num(), 0);
	TestEqual(TEXT("no triangles were invented"), Geo.Triangles.Num(), 0);

	// Triangles without vertices is the other half of the guard, and it is reachable: the geometry
	// keeps its triangle memory across a rebuild by design (see FDreamUIGeometry::Clear) while the
	// vertices are re-sized first.
	Geo.Triangles.Add((FDreamUIMeshIndex)0);
	Geo.Triangles.Add((FDreamUIMeshIndex)1);
	Geo.Triangles.Add((FDreamUIMeshIndex)2);
	NewObject<UDreamMeshModifierOutline>(GetTransientPackage())
		->ModifyUIGeometry(Geo, true, true, true, true);
	TestEqual(TEXT("triangles without vertices are left alone"), Geo.Triangles.Num(), 3);
	TestEqual(TEXT("and no vertices are conjured to match them"), Geo.Vertices.Num(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamMeshModifierOutlineFourDirectionTest,
	"DreamGUI.MeshModifier.AnOutlineCopiesTheMeshFourWaysAndDrawsTheOriginalLast",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMeshModifierOutlineFourDirectionTest::RunTest(const FString& Parameters)
{
	using namespace DreamMeshModifierTestLocal;

	UDreamMeshModifierOutline* Outline = NewObject<UDreamMeshModifierOutline>(GetTransientPackage());
	Outline->SetOutlineSize(FVector2f(3.0f, 5.0f));
	Outline->SetOutlineColor(FColor(9, 8, 7, 255));
	Outline->SetUse8Direction(false);

	FDreamUIGeometry Geo;
	BuildQuads(Geo, 1, FColor(255, 255, 255, 255));
	Outline->ModifyUIGeometry(Geo, true, true, true, true);

	TestEqual(TEXT("the mesh is the original plus four copies"), Geo.Vertices.Num(), VertsPerQuad * 5);
	TestEqual(TEXT("and its origin vertices match"), Geo.OriginVertices.Num(), VertsPerQuad * 5);
	TestEqual(TEXT("and so does the index buffer"), Geo.Triangles.Num(), IndicesPerQuad * 5);

	// Draw order is the entire point of an outline: the copies are laid down first and the original
	// last, so the glyph is painted over its own halo rather than under it. Nothing about the vertex
	// data says this -- only the position of the original's indices in the buffer does.
	const int32 OriginalIndexStart = IndicesPerQuad * 4;
	for (int32 Index = 0; Index < IndicesPerQuad; Index++)
	{
		TestEqual(TEXT("the original's triangles come last, unshifted"),
			(int32)Geo.Triangles[OriginalIndexStart + Index], (int32)Geo.Triangles[Index] - VertsPerQuad);
	}

	// Every copy's indices are the original's shifted by one whole vertex block. Shift a copy by the
	// wrong multiple and it silently draws another glyph's quad in this glyph's outline colour.
	for (int32 Channel = 1; Channel <= 4; Channel++)
	{
		const int32 Block = (Channel - 1) * IndicesPerQuad;
		for (int32 Index = 0; Index < IndicesPerQuad; Index++)
		{
			TestEqual(TEXT("a copy indexes its own vertex block"),
				(int32)Geo.Triangles[Block + Index],
				(int32)Geo.Triangles[OriginalIndexStart + Index] + Channel * VertsPerQuad);
		}
	}

	// The four diagonals, in the order the implementation lays them out. Y is across and Z is up, so
	// OutlineSize.X moves the copy sideways and OutlineSize.Y moves it vertically.
	const FVector3f Origin = Geo.OriginVertices[0].Position;
	TestEqual(TEXT("the original vertex did not move"), AsVector(Origin), FVector(0.0, 0.0, 0.0));
	TestEqual(TEXT("copy 1 goes up and right"),
		AsVector(Geo.OriginVertices[VertsPerQuad * 1].Position), FVector(0.0, 3.0, 5.0));
	TestEqual(TEXT("copy 2 goes up and left"),
		AsVector(Geo.OriginVertices[VertsPerQuad * 2].Position), FVector(0.0, -3.0, 5.0));
	TestEqual(TEXT("copy 3 goes down and right"),
		AsVector(Geo.OriginVertices[VertsPerQuad * 3].Position), FVector(0.0, 3.0, -5.0));
	TestEqual(TEXT("copy 4 goes down and left"),
		AsVector(Geo.OriginVertices[VertsPerQuad * 4].Position), FVector(0.0, -3.0, -5.0));

	// A copy must carry the ORIGINAL's UVs: it is the same glyph drawn again, and a copy sampling a
	// neighbour's atlas rectangle would put someone else's letter in the halo.
	for (int32 Channel = 1; Channel <= 4; Channel++)
	{
		TestEqual(TEXT("a copy samples the same texel as the vertex it shadows"),
			Geo.Vertices[VertsPerQuad * Channel].TextureCoordinate[0].X, Geo.Vertices[0].TextureCoordinate[0].X);
		TestEqual(TEXT("on the last channel too"),
			Geo.Vertices[VertsPerQuad * Channel].TextureCoordinate[LEXUI_VERTEX_TEXCOORDINATE_COUNT - 1].Y,
			Geo.Vertices[0].TextureCoordinate[LEXUI_VERTEX_TEXCOORDINATE_COUNT - 1].Y);
	}

	// The copies wear the outline colour; the original keeps its own. An outline that recoloured the
	// original would be indistinguishable from a tint.
	TestEqual(TEXT("the copy takes the outline colour"), Geo.Vertices[VertsPerQuad].Color, FColor(9, 8, 7, 255));
	TestEqual(TEXT("and the original is untouched"), Geo.Vertices[0].Color, FColor(255, 255, 255, 255));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamMeshModifierOutlineEightDirectionTest,
	"DreamGUI.MeshModifier.AnEightWayOutlineAddsTheFourAxisCopiesToTheDiagonals",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMeshModifierOutlineEightDirectionTest::RunTest(const FString& Parameters)
{
	using namespace DreamMeshModifierTestLocal;

	// Eight directions is not "twice as thick", it is the same four diagonals plus the four axis
	// directions that a diagonal-only outline leaves as notches on the flat edges of a glyph. The
	// extra copies must therefore be axis-ALIGNED -- one component zero each -- or the second set is
	// just the first set again and the option costs draw calls for nothing.
	UDreamMeshModifierOutline* Outline = NewObject<UDreamMeshModifierOutline>(GetTransientPackage());
	Outline->SetOutlineSize(FVector2f(2.0f, 4.0f));
	Outline->SetUse8Direction(true);
	TestTrue(TEXT("the eight-direction flag is readable back"), Outline->GetUse8Direction());

	FDreamUIGeometry Geo;
	BuildQuads(Geo, 2);
	Outline->ModifyUIGeometry(Geo, true, true, true, true);

	const int32 SourceVerts = VertsPerQuad * 2;
	const int32 SourceIndices = IndicesPerQuad * 2;
	TestEqual(TEXT("nine copies of the mesh in all"), Geo.Vertices.Num(), SourceVerts * 9);
	TestEqual(TEXT("and nine passes of indices"), Geo.Triangles.Num(), SourceIndices * 9);

	TestEqual(TEXT("copy 5 is straight left"),
		AsVector(Geo.OriginVertices[SourceVerts * 5].Position), FVector(0.0, -2.0, 0.0));
	TestEqual(TEXT("copy 6 is straight right"),
		AsVector(Geo.OriginVertices[SourceVerts * 6].Position), FVector(0.0, 2.0, 0.0));
	TestEqual(TEXT("copy 7 is straight up"),
		AsVector(Geo.OriginVertices[SourceVerts * 7].Position), FVector(0.0, 0.0, 4.0));
	TestEqual(TEXT("copy 8 is straight down"),
		AsVector(Geo.OriginVertices[SourceVerts * 8].Position), FVector(0.0, 0.0, -4.0));

	// The original still lands last, with nine passes in the buffer rather than five.
	const int32 OriginalIndexStart = SourceIndices * 8;
	TestEqual(TEXT("the original's first index is still the unshifted one"),
		(int32)Geo.Triangles[OriginalIndexStart], 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamMeshModifierOutlineAlphaTest,
	"DreamGUI.MeshModifier.AnOutlineFadesWithTheVertexItSurrounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMeshModifierOutlineAlphaTest::RunTest(const FString& Parameters)
{
	using namespace DreamMeshModifierTestLocal;

	// The outline scales its own alpha by the source vertex's, which is what lets a widget fade out
	// as one object. Without it the halo survives the fade and a "hidden" label leaves its outline
	// on screen -- and each copy reads the alpha of the vertex it was made from, not the first one,
	// so a per-vertex alpha ramp has to survive into the halo as well.
	UDreamMeshModifierOutline* Outline = NewObject<UDreamMeshModifierOutline>(GetTransientPackage());
	Outline->SetOutlineColor(FColor(200, 100, 50, 255));
	TestEqual(TEXT("the outline colour reads back"), Outline->GetOutlineColor(), FColor(200, 100, 50, 255));
	TestTrue(TEXT("and multiplying the source alpha is the default"), Outline->GetMultiplySourceAlpha());

	FDreamUIGeometry Geo;
	BuildQuads(Geo, 1, FColor(255, 255, 255, 255));
	Geo.Vertices[1].Color.A = 0;
	Outline->ModifyUIGeometry(Geo, true, true, true, true);

	TestEqual(TEXT("an opaque vertex gets an opaque halo"),
		(int32)Geo.Vertices[VertsPerQuad + 0].Color.A, 255);
	TestEqual(TEXT("and a transparent one gets none"),
		(int32)Geo.Vertices[VertsPerQuad + 1].Color.A, 0);
	TestEqual(TEXT("the halo still carries the outline's own colour"),
		(int32)Geo.Vertices[VertsPerQuad + 1].Color.R, 200);

	// The flag is EditAnywhere on all three duplicating modifiers, so a details panel can already
	// turn it off; without a setter a Blueprint could not, and could not read it back either. With
	// it off a transparent vertex gets an opaque halo, which is the whole reason the flag exists.
	Outline->SetMultiplySourceAlpha(false);
	TestFalse(TEXT("and the flag round-trips through Blueprint"), Outline->GetMultiplySourceAlpha());
	{
		FDreamUIGeometry Flat;
		BuildQuads(Flat, 1, FColor(255, 255, 255, 255));
		Flat.Vertices[1].Color.A = 0;
		Outline->ModifyUIGeometry(Flat, true, true, true, true);
		TestEqual(TEXT("with it off the halo keeps the outline's own alpha over a faded vertex"),
			(int32)Flat.Vertices[VertsPerQuad + 1].Color.A, 255);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamMeshModifierShadowTest,
	"DreamGUI.MeshModifier.AShadowDrawsAnOffsetCopyBehindTheOriginal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMeshModifierShadowTest::RunTest(const FString& Parameters)
{
	using namespace DreamMeshModifierTestLocal;

	UDreamMeshModifierShadow* Shadow = NewObject<UDreamMeshModifierShadow>(GetTransientPackage());
	Shadow->SetShadowOffset(FVector3f(0.0f, 2.0f, -3.0f));
	Shadow->SetShadowColor(FColor(0, 0, 0, 128));
	TestEqual(TEXT("the offset reads back"), AsVector(Shadow->GetShadowOffset()), FVector(0.0, 2.0, -3.0));

	FDreamUIGeometry Geo;
	BuildQuads(Geo, 1, FColor(255, 255, 255, 255));
	Shadow->ModifyUIGeometry(Geo, true, true, true, true);

	TestEqual(TEXT("the mesh doubled"), Geo.Vertices.Num(), VertsPerQuad * 2);
	TestEqual(TEXT("and so did the index buffer"), Geo.Triangles.Num(), IndicesPerQuad * 2);

	// Unlike the outline, the shadow rewrites the FIRST pass in place to point at the copy and moves
	// the untouched original to the back of the buffer. Both halves have to be right: leave the first
	// pass pointing at the original and the shadow is drawn on top of the thing casting it.
	for (int32 Index = 0; Index < IndicesPerQuad; Index++)
	{
		TestEqual(TEXT("the first pass draws the copy"),
			(int32)Geo.Triangles[Index], (int32)Geo.Triangles[IndicesPerQuad + Index] + VertsPerQuad);
	}
	TestEqual(TEXT("and the last pass is the original, unshifted"), (int32)Geo.Triangles[IndicesPerQuad], 0);

	TestEqual(TEXT("the original stays put"),
		AsVector(Geo.OriginVertices[0].Position), FVector(0.0, 0.0, 0.0));
	TestEqual(TEXT("and the copy sits at the offset"),
		AsVector(Geo.OriginVertices[VertsPerQuad].Position), FVector(0.0, 2.0, -3.0));
	TestEqual(TEXT("every vertex of the copy moves by the same offset"),
		AsVector(Geo.OriginVertices[VertsPerQuad + 3].Position), FVector(0.0, 12.0, 7.0));

	TestEqual(TEXT("the copy is tinted"), (int32)Geo.Vertices[VertsPerQuad].Color.R, 0);
	TestEqual(TEXT("with the shadow's alpha scaled by the source's"),
		(int32)Geo.Vertices[VertsPerQuad].Color.A, 128);
	TestEqual(TEXT("and the original keeps its colour"), Geo.Vertices[0].Color, FColor(255, 255, 255, 255));

	// Every channel of every copy, not just the first of the first. BuildQuads makes each coordinate
	// unique per (vertex, channel), so a copy that took its UVs from the wrong vertex or stopped
	// short of the last channel shows up here rather than as a glyph wearing someone else's texel.
	// The count itself is the interesting part: the array holds LEXUI_VERTEX_TEXCOORDINATE_COUNT
	// coordinates and a static mesh vertex holds twice that, and copying to the larger of the two
	// numbers writes over the tangents below and into the vertex behind.
	for (int32 Vertex = 0; Vertex < VertsPerQuad; Vertex++)
	{
		for (int32 Channel = 0; Channel < LEXUI_VERTEX_TEXCOORDINATE_COUNT; Channel++)
		{
			TestEqual(TEXT("the copy samples the same texel on every channel"),
				Geo.Vertices[VertsPerQuad + Vertex].TextureCoordinate[Channel].X,
				Geo.Vertices[Vertex].TextureCoordinate[Channel].X);
			TestEqual(TEXT("on both axes"),
				Geo.Vertices[VertsPerQuad + Vertex].TextureCoordinate[Channel].Y,
				Geo.Vertices[Vertex].TextureCoordinate[Channel].Y);
		}
		// The copy is the same surface moved sideways, so it faces the same way. This used to happen
		// by accident, as the first casualty of the overrun described above.
		TestTrue(TEXT("and it faces the same way as the vertex it shadows"),
			Geo.Vertices[VertsPerQuad + Vertex].TangentX == Geo.Vertices[Vertex].TangentX
			&& Geo.Vertices[VertsPerQuad + Vertex].TangentZ == Geo.Vertices[Vertex].TangentZ);
	}

	// The alpha flag, which a details panel could already reach and Blueprint could not.
	TestTrue(TEXT("multiplying the source alpha is the default"), Shadow->GetMultiplySourceAlpha());
	Shadow->SetMultiplySourceAlpha(false);
	TestFalse(TEXT("and the flag round-trips"), Shadow->GetMultiplySourceAlpha());
	{
		FDreamUIGeometry Flat;
		BuildQuads(Flat, 1, FColor(255, 255, 255, 0));
		Shadow->ModifyUIGeometry(Flat, true, true, true, true);
		TestEqual(TEXT("with it off a transparent source still casts a shadow"),
			(int32)Flat.Vertices[VertsPerQuad].Color.A, 128);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamMeshModifierLongShadowTest,
	"DreamGUI.MeshModifier.ALongShadowStacksOneCopyPerSegmentOutToItsFullLength",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMeshModifierLongShadowTest::RunTest(const FString& Parameters)
{
	using namespace DreamMeshModifierTestLocal;

	// A long shadow is a stack of copies marching out to ShadowSize. The segment count is a count of
	// GAPS, not of copies -- the implementation everywhere uses ShadowSegment + 1 -- so a "0 segment"
	// long shadow is still one copy, and the vertex budget of a text is multiplied by that number.
	UDreamMeshModifierLongShadow* LongShadow = NewObject<UDreamMeshModifierLongShadow>(GetTransientPackage());
	LongShadow->SetShadowSize(FVector3f(0.0f, 9.0f, -9.0f));
	LongShadow->SetShadowSegment(2);
	TestEqual(TEXT("the segment count reads back"), (int32)LongShadow->GetShadowSegments(), 2);

	FDreamUIGeometry Geo;
	BuildQuads(Geo, 1);
	LongShadow->ModifyUIGeometry(Geo, true, true, true, true);

	const int32 ChannelCount = 3;
	TestEqual(TEXT("two segments means three copies plus the original"),
		Geo.Vertices.Num(), VertsPerQuad * (ChannelCount + 1));
	TestEqual(TEXT("and four passes of indices"),
		Geo.Triangles.Num(), IndicesPerQuad * (ChannelCount + 1));

	// The copies are evenly spaced and the FIRST block is the far end. That order is what makes the
	// stack read as one solid shadow: the furthest copy is drawn first, so each nearer one covers the
	// seam behind it, and the original lands on top of the lot.
	TestEqual(TEXT("the first copy reaches the full shadow size"),
		AsVector(Geo.OriginVertices[VertsPerQuad * 1].Position), FVector(0.0, 9.0, -9.0));
	TestEqual(TEXT("the second sits two thirds out"),
		AsVector(Geo.OriginVertices[VertsPerQuad * 2].Position), FVector(0.0, 6.0, -6.0));
	TestEqual(TEXT("the third one third out"),
		AsVector(Geo.OriginVertices[VertsPerQuad * 3].Position), FVector(0.0, 3.0, -3.0));
	TestEqual(TEXT("and the original has not moved"),
		AsVector(Geo.OriginVertices[0].Position), FVector(0.0, 0.0, 0.0));

	for (int32 Channel = 1; Channel <= ChannelCount; Channel++)
	{
		const int32 Block = (Channel - 1) * IndicesPerQuad;
		for (int32 Index = 0; Index < IndicesPerQuad; Index++)
		{
			TestEqual(TEXT("each pass indexes the copy that belongs to it, furthest first"),
				(int32)Geo.Triangles[Block + Index],
				(int32)Geo.Triangles[IndicesPerQuad * ChannelCount + Index] + Channel * VertsPerQuad);
		}
	}
	TestEqual(TEXT("and the original's pass is last and unshifted"),
		(int32)Geo.Triangles[IndicesPerQuad * ChannelCount], 0);

	// Zero segments is still a shadow, not an absent one. Reading ShadowSegment as a copy count would
	// make this case draw nothing and silently delete the effect.
	{
		LongShadow->SetShadowSegment(0);
		FDreamUIGeometry Single;
		BuildQuads(Single, 1);
		LongShadow->ModifyUIGeometry(Single, true, true, true, true);
		TestEqual(TEXT("zero segments still casts one copy"), Single.Vertices.Num(), VertsPerQuad * 2);
		TestEqual(TEXT("at the full shadow size"),
			AsVector(Single.OriginVertices[VertsPerQuad].Position), FVector(0.0, 9.0, -9.0));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamMeshModifierLongShadowGradientTest,
	"DreamGUI.MeshModifier.ALongShadowFadesFromItsGradientColourAtTheFarEnd",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMeshModifierLongShadowGradientTest::RunTest(const FString& Parameters)
{
	using namespace DreamMeshModifierTestLocal;

	// The gradient runs across the stack, so which END gets which colour is the visible decision: the
	// far copy is drawn first and the near copy last, and reversing the ramp turns a shadow that
	// dissolves into the distance into one that dissolves into the letter.
	UDreamMeshModifierLongShadow* LongShadow = NewObject<UDreamMeshModifierLongShadow>(GetTransientPackage());
	LongShadow->SetShadowSegment(3);
	LongShadow->SetUseGradientColor(true);
	TestTrue(TEXT("the gradient is on"), LongShadow->GetUseGradientColor());
	TestEqual(TEXT("the far end colour is black by default"), LongShadow->GetGradientColor(), FColor::Black);
	TestEqual(TEXT("and the near end colour is white"), LongShadow->GetShadowColor(), FColor::White);

	// The two colours are separate properties and their setters have to stay separate. A gradient
	// setter that wrote the shadow colour would look like it worked -- the stack does change -- while
	// the property the author was aiming at stayed at its default forever.
	LongShadow->SetGradientColor(FColor(7, 8, 9, 255));
	TestEqual(TEXT("the gradient setter writes the gradient colour"),
		LongShadow->GetGradientColor(), FColor(7, 8, 9, 255));
	TestEqual(TEXT("and leaves the shadow colour where it was"),
		LongShadow->GetShadowColor(), FColor::White);
	LongShadow->SetGradientColor(FColor::Black);

	FDreamUIGeometry Geo;
	BuildQuads(Geo, 1, FColor(255, 255, 255, 255));
	LongShadow->ModifyUIGeometry(Geo, true, true, true, true);

	const int32 ChannelCount = 4;
	TestEqual(TEXT("the far copy is exactly the gradient colour"),
		(int32)Geo.Vertices[VertsPerQuad * 1].Color.R, 0);

	int32 Previous = -1;
	for (int32 Channel = 1; Channel <= ChannelCount; Channel++)
	{
		const int32 Red = (int32)Geo.Vertices[VertsPerQuad * Channel].Color.R;
		TestTrue(TEXT("each copy is lighter than the one further out"), Red > Previous);
		Previous = Red;
	}
	// Both ends of the ramp are colours somebody picked, so both have to be reachable. Dividing the
	// layer index by the layer count instead of by the number of gaps between layers stops the near
	// end at (n-1)/n, and the authored ShadowColor becomes a colour the effect can never show.
	TestEqual(TEXT("and the nearest copy arrives at the shadow colour exactly"), Previous, 255);

	// A single layer has no gap for the ramp to run across. It stands for the near end -- the edge
	// that touches the glyph -- so it takes the shadow colour outright, which also makes a one-layer
	// gradient agree with the gradient switched off. The counter-intuitive half is that GradientColor
	// is then unreachable until there is a second layer to fade towards.
	{
		LongShadow->SetShadowSegment(0);
		FDreamUIGeometry Single;
		BuildQuads(Single, 1, FColor(255, 255, 255, 255));
		LongShadow->ModifyUIGeometry(Single, true, true, true, true);
		TestEqual(TEXT("a lone layer takes the shadow colour rather than the gradient's"),
			(int32)Single.Vertices[VertsPerQuad].Color.R, 255);
		LongShadow->SetShadowSegment(3);
	}

	// With the gradient off, every copy is the one shadow colour -- the other half of the switch.
	{
		LongShadow->SetUseGradientColor(false);
		FDreamUIGeometry Flat;
		BuildQuads(Flat, 1, FColor(255, 255, 255, 255));
		LongShadow->ModifyUIGeometry(Flat, true, true, true, true);
		TestEqual(TEXT("the far copy takes the shadow colour"),
			(int32)Flat.Vertices[VertsPerQuad * 1].Color.R, 255);
		TestEqual(TEXT("and so does the near one"),
			(int32)Flat.Vertices[VertsPerQuad * ChannelCount].Color.R, 255);
	}

	// And the alpha flag, reachable from a details panel and until now from nowhere else.
	TestTrue(TEXT("multiplying the source alpha is the default"), LongShadow->GetMultiplySourceAlpha());
	LongShadow->SetMultiplySourceAlpha(false);
	TestFalse(TEXT("and the flag round-trips"), LongShadow->GetMultiplySourceAlpha());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamMeshModifierBaseRegistrationTest,
	"DreamGUI.MeshModifier.OnlyAConcreteModifierClassCanBeAddedToAMesh",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMeshModifierBaseRegistrationTest::RunTest(const FString& Parameters)
{
	using namespace DreamMeshModifierTestLocal;

	// AddMeshModifier is the Blueprint-facing door, so its argument arrives from wherever a class
	// picker got it: a null entry in an array, or the abstract base itself, which is offered by any
	// picker that lists the hierarchy. Constructing either would be a widget owning a component that
	// can never modify anything, so both are refused at the door rather than half-created.
	FScopedVisualWidget Fixture(UDreamImage::StaticClass(), TEXT("OutlinedImage"));
	UDreamVisualBatchMesh* Mesh = Fixture.Mesh();
	if (!TestNotNull(TEXT("the image visual is a batch mesh"), Mesh))
	{
		return false;
	}

	const TSubclassOf<UDreamMeshModifierBase> NoClass;
	TestNull(TEXT("a null modifier class is refused"), Mesh->AddMeshModifier(NoClass));
	TestNull(TEXT("and so is the abstract base"),
		Mesh->AddMeshModifier(UDreamMeshModifierBase::StaticClass()));

	UDreamMeshModifierBase* Added = Mesh->AddMeshModifier(UDreamMeshModifierOutline::StaticClass());
	if (!TestNotNull(TEXT("a concrete modifier is created"), Added))
	{
		return false;
	}
	TestEqual(TEXT("and it resolves back to the mesh it was added to"), Added->GetVisualBatchMesh(), Mesh);
	TestTrue(TEXT("a fresh modifier is enabled"), Added->GetEnable());

	// Disabling is what the batch mesh consults before running a modifier at all, so the flag has to
	// survive the round trip -- and toggling it dirties the mesh, which is why it is a setter and not
	// a plain property write.
	Added->SetEnable(false);
	TestFalse(TEXT("and can be switched off"), Added->GetEnable());
	Added->SetEnable(true);
	TestTrue(TEXT("and back on"), Added->GetEnable());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamMeshModifierWithoutAWidgetTest,
	"DreamGUI.MeshModifier.AModifierWithNoWidgetYetSurvivesEveryAccessor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMeshModifierWithoutAWidgetTest::RunTest(const FString& Parameters)
{
	using namespace DreamMeshModifierTestLocal;

	// A modifier exists before it is attached to anything: the class-default archetype, a template
	// being edited in a details panel, an object mid-construction. Every setter on this family ends
	// in "and then mark the mesh dirty", and the mesh is reached through a widget that is not there
	// yet, so each of those calls has to tolerate finding nothing.
	UDreamMeshModifierOutline* Outline = NewObject<UDreamMeshModifierOutline>(GetTransientPackage());
	TestNull(TEXT("there is no mesh to find"), Outline->GetVisualBatchMesh());

	Outline->SetEnable(false);
	Outline->SetOutlineColor(FColor::Red);
	Outline->SetOutlineSize(FVector2f(4.0f, 4.0f));
	Outline->SetUse8Direction(true);
	TestFalse(TEXT("the enable flag was still written"), Outline->GetEnable());
	TestEqual(TEXT("and so was the colour"), Outline->GetOutlineColor(), FColor::Red);

	UDreamMeshModifierGradientColor* Gradient =
		NewObject<UDreamMeshModifierGradientColor>(GetTransientPackage());
	Gradient->SetColor1(FColor::Blue);
	Gradient->SetMultiplySourceAlpha(false);
	TestEqual(TEXT("the gradient took its colour too"), Gradient->GetColor1(), FColor::Blue);
	TestFalse(TEXT("and its flag"), Gradient->GetMultiplySourceAlpha());

	UDreamMeshModifierLongShadow* LongShadow = NewObject<UDreamMeshModifierLongShadow>(GetTransientPackage());
	LongShadow->SetShadowSegment(7);
	LongShadow->SetShadowSize(FVector3f(0.0f, 1.0f, 1.0f));
	LongShadow->SetShadowColor(FColor::Green);
	LongShadow->SetUseGradientColor(false);
	TestEqual(TEXT("and the long shadow its segment count"), (int32)LongShadow->GetShadowSegments(), 7);

	// The base implementation of ModifyUIGeometry exists only to hand a helper to a Blueprint
	// subclass. Reached on a native class it must do nothing at all -- not allocate the helper, not
	// touch the geometry -- because every native modifier calls it as the first line of its override
	// in some LGUI-derived code and a base that "helpfully" did something would double the work.
	FDreamUIGeometry Geo;
	BuildQuads(Geo, 1);
	Outline->UDreamMeshModifierBase::ModifyUIGeometry(Geo, true, true, true, true);
	TestEqual(TEXT("the native base pass adds no vertices"), Geo.Vertices.Num(), VertsPerQuad);
	TestEqual(TEXT("and no triangles"), Geo.Triangles.Num(), IndicesPerQuad);

	// The base declaration claims everything, which is the safe direction: a Blueprint modifier can
	// write any stream and the batch mesh has no way to know which, so it must recompute all four.
	bool bTriangles = false, bPosition = false, bUV = false, bColor = false;
	Outline->UDreamMeshModifierBase::ModifierWillChangeVertexData(bTriangles, bPosition, bUV, bColor);
	TestTrue(TEXT("an unknown modifier is assumed to change triangles"), bTriangles);
	TestTrue(TEXT("and positions"), bPosition);
	TestTrue(TEXT("and UVs"), bUV);
	TestTrue(TEXT("and colours"), bColor);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamMeshModifierPositionAsUVGateTest,
	"DreamGUI.MeshModifier.PositionAsUVNeedsAMeshAndWritesOnlyTheChannelItWasGiven",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMeshModifierPositionAsUVGateTest::RunTest(const FString& Parameters)
{
	using namespace DreamMeshModifierTestLocal;

	// PositionAsUV is the one native modifier that reads off the widget, so it is the one that can be
	// reached in a state where its inputs do not exist. Having no batch mesh behind it is the only
	// thing it has to refuse; an unparented widget in an authoring tree is otherwise a perfectly
	// ordinary thing to rebuild, and the widget behind the visual has to be reached safely rather
	// than assumed.
	//
	// It used to refuse something else as well: channels 1 to 3 were skipped unless the widget had a
	// render canvas. That gate is gone rather than extended to the one channel that never had it.
	// It could not fire in the running pipeline, where a canvas is a precondition for geometry
	// updating at all, and against the one documented relationship between the canvas and the UV
	// channels it pointed backwards -- UV1 belongs to the canvas, so having one is exactly when
	// writing UV1 does damage, and that was the case the gate let through.
	{
		UDreamMeshModifierPositionAsUV* Loose =
			NewObject<UDreamMeshModifierPositionAsUV>(GetTransientPackage());
		FDreamUIGeometry Geo;
		BuildQuads(Geo, 1);
		const FVector2f Before = Geo.Vertices[1].TextureCoordinate[1];
		Loose->ModifyUIGeometry(Geo, true, true, true, true);
		TestEqual(TEXT("with no mesh to read, no UV is written"), Geo.Vertices[1].TextureCoordinate[1].X, Before.X);
		TestEqual(TEXT("on either axis"), Geo.Vertices[1].TextureCoordinate[1].Y, Before.Y);
		TestEqual(TEXT("and nothing was added to the mesh"), Geo.Vertices.Num(), VertsPerQuad);

		// The declaration the batch mesh reads to decide which streams to rebuild before the modifier
		// list runs. This one writes a UV and nothing else, and claiming nothing at all would leave
		// the UV stream unmarked -- the one direction that loses the write rather than costing a
		// recompute.
		bool bTriangles = true, bPosition = true, bUV = false, bColor = true;
		Loose->ModifierWillChangeVertexData(bTriangles, bPosition, bUV, bColor);
		TestTrue(TEXT("it declares that it writes UVs"), bUV);
		TestFalse(TEXT("and nothing else: no triangles"), bTriangles);
		TestFalse(TEXT("no positions"), bPosition);
		TestFalse(TEXT("no colours"), bColor);
	}

	{
		FScopedVisualWidget Fixture(UDreamImage::StaticClass(), TEXT("UnCanvassed"));
		UDreamVisualBatchMesh* Mesh = Fixture.Mesh();
		if (!TestNotNull(TEXT("the image visual is a batch mesh"), Mesh))
		{
			return false;
		}
		TestNull(TEXT("a widget outside a canvas has no render canvas"), Fixture.Widget->GetRenderCanvas());

		UDreamMeshModifierPositionAsUV* Modifier = Cast<UDreamMeshModifierPositionAsUV>(
			Mesh->AddMeshModifier(UDreamMeshModifierPositionAsUV::StaticClass()));
		if (!TestNotNull(TEXT("the modifier was added"), Modifier))
		{
			return false;
		}
		TestEqual(TEXT("and it can see the mesh"), Modifier->GetVisualBatchMesh(), Mesh);
		TestEqual(TEXT("channel 1 is where it points out of the box"), (int32)Modifier->GetUVChannel(), 1);

		// BuildQuads puts the top-left corner of the first quad at Y = 0, Z = 10, and the position is
		// read across (Y) and up (Z) because that is the plane a UI vertex lives in.
		FDreamUIGeometry Geo;
		BuildQuads(Geo, 1);
		Modifier->ModifyUIGeometry(Geo, true, true, true, true);
		TestEqual(TEXT("the position lands in the authored channel, canvas or no canvas"),
			Geo.Vertices[2].TextureCoordinate[1].X, 0.0f);
		TestEqual(TEXT("on both axes"), Geo.Vertices[2].TextureCoordinate[1].Y, 10.0f);
		// Every other channel keeps the coordinate the emitter put there, which is the difference
		// between a modifier that adds a channel and one that flattens the vertex.
		TestEqual(TEXT("the channels it was not pointed at keep the emitter's coordinate"),
			Geo.Vertices[2].TextureCoordinate[0].X, 2.0f);
		TestEqual(TEXT("all of them"), Geo.Vertices[2].TextureCoordinate[3].Y, 3.0f);

		// The channel and the scale are both EditAnywhere and neither had an accessor, so a Blueprint
		// could not point this modifier anywhere. The scale is per axis: it is what turns a position
		// in widget units into whatever range the material wants to sample in.
		Modifier->SetUVChannel(0);
		Modifier->SetScale(FVector2f(2.0f, 0.5f));
		TestEqual(TEXT("the channel round-trips"), (int32)Modifier->GetUVChannel(), 0);
		TestEqual(TEXT("and so does the scale"), Modifier->GetScale().X, 2.0f);
		{
			FDreamUIGeometry Scaled;
			BuildQuads(Scaled, 1);
			Modifier->ModifyUIGeometry(Scaled, true, true, true, true);
			TestEqual(TEXT("the scale applies across"), Scaled.Vertices[3].TextureCoordinate[0].X, 20.0f);
			TestEqual(TEXT("and up, independently"), Scaled.Vertices[3].TextureCoordinate[0].Y, 5.0f);
			TestEqual(TEXT("and the channel it moved away from is back to the emitter's coordinate"),
				Scaled.Vertices[3].TextureCoordinate[1].X, 3.0f);
		}

		// UIMin and UIMax on the property are a slider hint, not a clamp, so a channel the vertex does
		// not have has to be answered with silence rather than an indexed write past the array.
		Modifier->SetUVChannel((uint8)LEXUI_VERTEX_TEXCOORDINATE_COUNT);
		{
			FDreamUIGeometry Untouched;
			BuildQuads(Untouched, 1);
			Modifier->ModifyUIGeometry(Untouched, true, true, true, true);
			TestEqual(TEXT("a channel the vertex does not have is written nowhere"),
				Untouched.Vertices[3].TextureCoordinate[0].X, 3.0f);
			TestEqual(TEXT("not even into the last one it does have"),
				Untouched.Vertices[3].TextureCoordinate[LEXUI_VERTEX_TEXCOORDINATE_COUNT - 1].X, 3.0f);
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamMeshModifierVisualSwapTest,
	"DreamGUI.MeshModifier.AModifierFollowsItsWidgetOntoAReplacementVisual",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMeshModifierVisualSwapTest::RunTest(const FString& Parameters)
{
	using namespace DreamMeshModifierTestLocal;

	// A widget's visual is replaceable and its modifiers are not told when it is replaced: swapping
	// an image for a text destroys nothing, it just leaves the old visual orphaned. So a modifier
	// that resolved its mesh once and kept the answer goes on marking a mesh nobody draws while the
	// mesh being drawn never hears from it -- and the weak pointer it holds stays valid throughout,
	// because the object is alive, merely unreachable. Both halves are silent, which is why the
	// resolution is redone on every ask rather than cached.
	{
		FScopedVisualWidget Fixture(UDreamImage::StaticClass(), TEXT("Swapped"));
		Fixture.Widget->OnRegister();
		UDreamVisualBatchMesh* FirstMesh = Fixture.Mesh();
		if (!TestNotNull(TEXT("the image visual is a batch mesh"), FirstMesh))
		{
			return false;
		}
		UDreamMeshModifierBase* Modifier = Fixture.Mesh()->AddMeshModifier(UDreamMeshModifierOutline::StaticClass());
		if (!TestNotNull(TEXT("the outline was added"), Modifier))
		{
			return false;
		}
		TestEqual(TEXT("and it resolves to the visual that was there"), Modifier->GetVisualBatchMesh(), FirstMesh);

		Fixture.Widget->CreateNewVisual(UDreamText::StaticClass());
		UDreamVisualBatchMesh* SecondMesh = Fixture.Mesh();
		if (!TestNotNull(TEXT("the replacement text visual is a batch mesh"), SecondMesh))
		{
			return false;
		}
		TestNotEqual(TEXT("and it really is a different object"), SecondMesh, FirstMesh);
		TestEqual(TEXT("the modifier follows the widget rather than the visual it met first"),
			Modifier->GetVisualBatchMesh(), SecondMesh);
	}

	{
		// The other order. A modifier can be added to a widget that has no visual at all -- an
		// authoring tree is assembled component by component -- and the visual arrives afterwards.
		FScopedVisualWidget Fixture(nullptr, TEXT("Bare"));
		Fixture.Widget->OnRegister();
		TestNull(TEXT("the widget starts with no visual"), Fixture.Widget->GetVisual());

		UDreamMeshModifierBase* Modifier = Cast<UDreamMeshModifierBase>(
			Fixture.Widget->AddComponent(UDreamMeshModifierShadow::StaticClass()));
		if (!TestNotNull(TEXT("the shadow was added anyway"), Modifier))
		{
			return false;
		}
		TestNull(TEXT("with nothing for it to resolve to yet"), Modifier->GetVisualBatchMesh());

		Fixture.Widget->CreateNewVisual(UDreamImage::StaticClass());
		TestEqual(TEXT("and it picks up the visual that arrives later"),
			Modifier->GetVisualBatchMesh(), Fixture.Mesh());
	}

	{
		// The text animation keeps its own cache of the text on top of the mesh, and a text left
		// behind by a swap is still a live, valid object -- so that cache has to be re-derived too,
		// or the effect goes on animating the label nobody draws.
		FScopedVisualWidget Fixture(UDreamText::StaticClass(), TEXT("Relabelled"));
		Fixture.Widget->OnRegister();
		UDreamText* FirstText = Cast<UDreamText>(Fixture.Widget->GetVisual());
		if (!TestNotNull(TEXT("the first visual is a text"), FirstText))
		{
			return false;
		}
		UDreamMeshModifierTextAnimation* Animation = Cast<UDreamMeshModifierTextAnimation>(
			Fixture.Mesh()->AddMeshModifier(UDreamMeshModifierTextAnimation::StaticClass()));
		if (!TestNotNull(TEXT("the text animation was added"), Animation))
		{
			return false;
		}
		TestEqual(TEXT("and it finds the text it was added to"), Animation->GetDreamText(), FirstText);

		UDreamText* SecondText = Cast<UDreamText>(Fixture.Widget->CreateNewVisual(UDreamText::StaticClass()));
		if (!TestNotNull(TEXT("the replacement is a text as well"), SecondText))
		{
			return false;
		}
		TestEqual(TEXT("the animation follows the replacement"), Animation->GetDreamText(), SecondText);

		// And a replacement that is not a text at all has to read as no text, not as the old one.
		Fixture.Widget->CreateNewVisual(UDreamImage::StaticClass());
		TestNull(TEXT("and reports none when the widget stops being a text"), Animation->GetDreamText());
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamMeshModifierTextAnimationGatesTest,
	"DreamGUI.MeshModifier.ATextAnimationOnlyRunsOnATextThatHasASelector",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMeshModifierTextAnimationGatesTest::RunTest(const FString& Parameters)
{
	using namespace DreamMeshModifierTestLocal;

	// The text animation is a per-character effect, and every property it can apply indexes the
	// text's character table with a range the selector produced. There are three ways that pairing
	// can be absent -- the visual is not a text, there is no selector, or nothing about the geometry
	// changed -- and each has to stop the whole thing rather than let a property index a table that
	// does not exist. This asserts the two reachable ones.
	{
		// A mesh modifier is a component on a widget, and nothing stops an author from dropping a
		// text animation onto an image. The cast is the gate, and a modifier that only checked for a
		// batch mesh would walk an empty character table with a live vertex range.
		FScopedVisualWidget Fixture(UDreamImage::StaticClass(), TEXT("NotAText"));
		UDreamVisualBatchMesh* Mesh = Fixture.Mesh();
		if (!TestNotNull(TEXT("the image visual is a batch mesh"), Mesh))
		{
			return false;
		}
		UDreamMeshModifierTextAnimation* Animation = Cast<UDreamMeshModifierTextAnimation>(
			Mesh->AddMeshModifier(UDreamMeshModifierTextAnimation::StaticClass()));
		if (!TestNotNull(TEXT("the text animation was added"), Animation))
		{
			return false;
		}
		TestNull(TEXT("but it finds no text behind an image visual"), Animation->GetDreamText());

		FDreamUIGeometry Geo;
		BuildQuads(Geo, 2);
		const FVector3f Before = Geo.OriginVertices[1].Position;
		Animation->ModifyUIGeometry(Geo, true, true, true, true);
		TestEqual(TEXT("so the mesh is left exactly as it was"), Geo.Vertices.Num(), VertsPerQuad * 2);
		TestEqual(TEXT("down to the vertex positions"), AsVector(Geo.OriginVertices[1].Position), AsVector(Before));
	}

	{
		// On a real text with no selector there is no character range to apply anything to, so the
		// properties must not run. A default-constructed text animation is exactly this state, and it
		// is what an author sees for the first frame after adding the component.
		FScopedVisualWidget Fixture(UDreamText::StaticClass(), TEXT("Labelled"));
		UDreamVisualBatchMesh* Mesh = Fixture.Mesh();
		if (!TestNotNull(TEXT("the text visual is a batch mesh"), Mesh))
		{
			return false;
		}
		UDreamMeshModifierTextAnimation* Animation = Cast<UDreamMeshModifierTextAnimation>(
			Mesh->AddMeshModifier(UDreamMeshModifierTextAnimation::StaticClass()));
		if (!TestNotNull(TEXT("the text animation was added"), Animation))
		{
			return false;
		}
		TestNotNull(TEXT("and this time it finds a text"), Animation->GetDreamText());
		TestNull(TEXT("with no selector configured"), Animation->GetSelector());

		FDreamUIGeometry Geo;
		BuildQuads(Geo, 2);
		const FVector3f Before = Geo.OriginVertices[3].Position;
		Animation->ModifyUIGeometry(Geo, true, true, true, true);
		TestEqual(TEXT("nothing is applied without a selector"),
			AsVector(Geo.OriginVertices[3].Position), AsVector(Before));

		// An out-of-range property index is answered with null and an error rather than an indexed
		// read, because the index arrives from Blueprint and the array is authored in a details panel
		// that can shrink under it between frames. Both ends of the range, and the negative end is
		// the one that matters: an int pin carries whatever arithmetic produced it, and a bound that
		// only looks upward lets a negative index read backwards out of the allocation.
		AddExpectedError(TEXT("out of range"), EAutomationExpectedErrorFlags::Contains, 3);
		TestNull(TEXT("asking for a property that is not there gives null"), Animation->GetProperty(0));
		TestNull(TEXT("and a negative index is refused rather than followed"), Animation->GetProperty(-1));
		Animation->SetProperty(-1, nullptr);
		TestEqual(TEXT("and the property list really is empty"), Animation->GetProperties().Num(), 0);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamMeshModifierTextAnimationSelectorOffsetTest,
	"DreamGUI.MeshModifier.TheSelectorOffsetMirrorFollowsWhicheverSelectorIsInstalled",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMeshModifierTextAnimationSelectorOffsetTest::RunTest(const FString& Parameters)
{
	using namespace DreamMeshModifierTestLocal;

	// SelectorOffset on the component is not the offset -- the selector owns that. It is an agent
	// property that exists so Sequencer has something on a component to key, and it has to stay a
	// faithful mirror in both directions: a write has to reach the selector, and a read has to come
	// back FROM the selector, or an animated wipe plays against a stale number after any edit that
	// swaps the selector out.
	UDreamMeshModifierTextAnimation* Animation =
		NewObject<UDreamMeshModifierTextAnimation>(GetTransientPackage());

	// With no selector there is nowhere to put the value, so the write is dropped rather than
	// remembered and silently disagreeing with the selector installed a moment later.
	const float Initial = Animation->GetSelectorOffset();
	Animation->SetSelectorOffset(0.9f);
	TestEqual(TEXT("a write with no selector installed is dropped"), Animation->GetSelectorOffset(), Initial);

	UDreamMeshModifierTextAnimation_RangeSelector* Range =
		NewObject<UDreamMeshModifierTextAnimation_RangeSelector>(Animation);
	Range->SetOffset(0.25f);
	Animation->SetSelector(Range);
	TestEqual(TEXT("installing a selector is visible"), Animation->GetSelector(),
		(UDreamMeshModifierTextAnimation_Selector*)Range);
	TestEqual(TEXT("and the mirror reads the selector's own offset"), Animation->GetSelectorOffset(), 0.25f);

	Animation->SetSelectorOffset(0.75f);
	TestEqual(TEXT("a write reaches the selector"), Range->GetOffset(), 0.75f);
	TestEqual(TEXT("and reads back through the mirror"), Animation->GetSelectorOffset(), 0.75f);

	// Swapping the selector must re-point the mirror. This is the case a cached value gets wrong.
	UDreamMeshModifierTextAnimation_RandomSelector* Random =
		NewObject<UDreamMeshModifierTextAnimation_RandomSelector>(Animation);
	Random->SetOffset(0.1f);
	Animation->SetSelector(Random);
	TestEqual(TEXT("the mirror follows the new selector"), Animation->GetSelectorOffset(), 0.1f);
	TestEqual(TEXT("and the old one is not written to any more"), Range->GetOffset(), 0.75f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamMeshModifierSelectorGuardTest,
	"DreamGUI.MeshModifier.ASelectorRefusesARangeItCannotDivide",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMeshModifierSelectorGuardTest::RunTest(const FString& Parameters)
{
	using namespace DreamMeshModifierTestLocal;

	// Every selector turns its settings into a reciprocal -- 1/Range, 1/(character span), 1/(count-1)
	// -- and hands the results to properties that use them as interpolation weights. A false return
	// means "no selection this frame" and is the only thing standing between a degenerate setting and
	// an infinity written into a vertex position, where it would take the whole draw call with it
	// rather than showing up as one wrong glyph.
	FScopedVisualWidget Fixture(UDreamText::StaticClass(), TEXT("EmptyLabel"));
	UDreamText* Text = Cast<UDreamText>(Fixture.Widget != nullptr ? Fixture.Widget->GetVisual() : nullptr);
	if (!TestNotNull(TEXT("the text visual exists"), Text))
	{
		return false;
	}
	// No font and no canvas, so the character table is empty -- which is also the state a text is in
	// on the frame its content is cleared, with the modifiers still attached and still running.
	TestEqual(TEXT("the character table is empty"), Text->GetCharPropertyArray().Num(), 0);

	FDreamMeshModifierTextAnimation_SelectResult Selection;

	{
		UDreamMeshModifierTextAnimation_RangeSelector* Range =
			NewObject<UDreamMeshModifierTextAnimation_RangeSelector>(GetTransientPackage());
		Range->SetRange(0.0f);
		TestFalse(TEXT("a zero range would divide by zero, so nothing is selected"),
			Range->Select(Text, Selection));

		Range->SetRange(0.5f);
		Range->SetStart(0.8f);
		Range->SetEnd(0.2f);
		TestFalse(TEXT("an end before its start selects nothing"), Range->Select(Text, Selection));

		// An empty text is not a refusal: the selector reports a valid, empty selection. What matters
		// is that the empty span leaves no values behind for a property to read.
		Range->SetStart(0.0f);
		Range->SetEnd(1.0f);
		TestTrue(TEXT("an empty text still yields a selection"), Range->Select(Text, Selection));
		TestEqual(TEXT("that starts at the first character"), Selection.StartCharIndex, 0);
		TestEqual(TEXT("and ends there too"), Selection.EndCharCount, 0);
		TestEqual(TEXT("with no interpolation values to read"), Selection.LerpValueArray.Num(), 0);
	}

	{
		UDreamMeshModifierTextAnimation_RandomSelector* Random =
			NewObject<UDreamMeshModifierTextAnimation_RandomSelector>(GetTransientPackage());
		Random->SetStart(0.6f);
		Random->SetEnd(0.6f);
		TestFalse(TEXT("a zero-width random range selects nothing"), Random->Select(Text, Selection));

		Random->SetEnd(1.0f);
		TestTrue(TEXT("a real range on an empty text still yields a selection"), Random->Select(Text, Selection));
		TestEqual(TEXT("with nothing in it"), Selection.LerpValueArray.Num(), 0);
	}

	{
		// The rich-text selector keys off a custom tag. A missing tag is the normal case -- the tag
		// lives in the text's markup and the effect is configured separately -- so it has to be a
		// clean refusal rather than a search that falls through to index -1.
		UDreamMeshModifierTextAnimation_RichTextTagSelector* Tagged =
			NewObject<UDreamMeshModifierTextAnimation_RichTextTagSelector>(GetTransientPackage());
		Tagged->SetRange(0.0f);
		TestFalse(TEXT("a zero range refuses before it even looks for the tag"),
			Tagged->Select(Text, Selection));

		Tagged->SetRange(1.0f);
		Tagged->SetTagName(TEXT("wobble"));
		TestEqual(TEXT("the tag name reads back"), Tagged->GetTagName(), FName(TEXT("wobble")));
		TestFalse(TEXT("and a tag the text does not carry selects nothing"),
			Tagged->Select(Text, Selection));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamMeshModifierEasePropertyEmptySelectionTest,
	"DreamGUI.MeshModifier.AnEasePropertyGivenAnEmptySelectionTouchesNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMeshModifierEasePropertyEmptySelectionTest::RunTest(const FString& Parameters)
{
	using namespace DreamMeshModifierTestLocal;

	// The empty selection is not a contrived input, it is what ships: a range selector on a text with
	// no characters returns TRUE with an empty span (there is nothing degenerate about an empty
	// text), and every property in the list is then applied over it. So the frame a label's content
	// is cleared runs exactly this path, with the geometry still present from the frame before.
	//
	// Each of these properties reads its per-character interpolation value out of an array sized to
	// the selection and indexes the text's character table with the selection's range, so an empty
	// span has to mean an empty loop -- and everything a property computes BEFORE that loop has to
	// be safe to compute with nothing selected.
	FScopedVisualWidget Fixture(UDreamText::StaticClass(), TEXT("Cleared"));
	UDreamText* Text = Cast<UDreamText>(Fixture.Widget != nullptr ? Fixture.Widget->GetVisual() : nullptr);
	if (!TestNotNull(TEXT("the text visual exists"), Text))
	{
		return false;
	}
	TestEqual(TEXT("with no characters in it"), Text->GetCharPropertyArray().Num(), 0);

	UDreamMeshModifierTextAnimation* Animation =
		NewObject<UDreamMeshModifierTextAnimation>(GetTransientPackage());
	FDreamMeshModifierTextAnimation_SelectResult Empty;
	Empty.StartCharIndex = 0;
	Empty.EndCharCount = 0;

	const UClass* PropertyClasses[] = {
		UDreamMeshModifierTextAnimation_PositionProperty::StaticClass(),
		UDreamMeshModifierTextAnimation_PositionRandomProperty::StaticClass(),
		UDreamMeshModifierTextAnimation_RotationProperty::StaticClass(),
		UDreamMeshModifierTextAnimation_RotationRandomProperty::StaticClass(),
		UDreamMeshModifierTextAnimation_ScaleProperty::StaticClass(),
		UDreamMeshModifierTextAnimation_ScaleRandomProperty::StaticClass(),
		UDreamMeshModifierTextAnimation_AlphaProperty::StaticClass(),
		UDreamMeshModifierTextAnimation_ColorProperty::StaticClass(),
		UDreamMeshModifierTextAnimation_ColorRandomProperty::StaticClass(),
	};
	for (const UClass* PropertyClass : PropertyClasses)
	{
		UDreamMeshModifierTextAnimation_Property* Property =
			NewObject<UDreamMeshModifierTextAnimation_Property>(Animation, const_cast<UClass*>(PropertyClass));
		if (!TestNotNull(TEXT("the property was created"), Property))
		{
			continue;
		}
		FDreamUIGeometry Geo;
		BuildQuads(Geo, 2, FColor(200, 150, 100, 255));
		Property->ApplyProperty(Text, Empty, &Geo);

		// Vertex 5 is the second quad's bottom-right corner, which BuildQuads puts at Y = 30.
		TestEqual(*FString::Printf(TEXT("%s moved no vertex"), *PropertyClass->GetName()),
			AsVector(Geo.OriginVertices[5].Position), FVector(0.0, 30.0, 0.0));
		TestEqual(*FString::Printf(TEXT("%s recoloured nothing"), *PropertyClass->GetName()),
			Geo.Vertices[5].Color, FColor(200, 150, 100, 255));
		TestEqual(*FString::Printf(TEXT("%s added no geometry"), *PropertyClass->GetName()),
			Geo.Vertices.Num(), VertsPerQuad * 2);
	}

	// The ease type is what turns the selector's linear ramp into the shape an author drew, and it is
	// cached into a bound function on first use -- so the setter has to invalidate that cache, or the
	// first ease a property ever used is the only one it will ever use.
	UDreamMeshModifierTextAnimation_PositionProperty* Position =
		NewObject<UDreamMeshModifierTextAnimation_PositionProperty>(Animation);
	TestEqual(TEXT("the default ease is a smooth one"), Position->GetEaseType(), EDreamTweenEase::InOutSine);
	Position->SetEaseType(EDreamTweenEase::OutBounce);
	TestEqual(TEXT("and the authored ease is kept"), Position->GetEaseType(), EDreamTweenEase::OutBounce);
	Position->SetEaseCurve(nullptr);
	TestNull(TEXT("with no curve behind it"), Position->GetCurveFloat());
	Position->SetPosition(FVector(0.0, 0.0, 30.0));
	TestEqual(TEXT("and the displacement round-trips"), Position->GetPosition(), FVector(0.0, 0.0, 30.0));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamMeshModifierWavePropertyLifecycleTest,
	"DreamGUI.MeshModifier.AWavePropertyInitialisesAndTearsDownWithNoTweenManager",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMeshModifierWavePropertyLifecycleTest::RunTest(const FString& Parameters)
{
	using namespace DreamMeshModifierTestLocal;

	// The wave properties are the only ones that need a clock: they subscribe to the tween manager's
	// per-frame callback so the text keeps redrawing while nothing else changes. The manager is a
	// game-instance subsystem, so outside a running game there is none, and Init has to come back
	// with no subscription rather than a half-made one that Deinit then tries to kill. The clock
	// itself is allowed to be missing too -- a widget in an authoring tree has no world, and the
	// answer there is phase zero rather than a dereference.
	UDreamMeshModifierTextAnimation* Animation =
		NewObject<UDreamMeshModifierTextAnimation>(GetTransientPackage());
	UDreamMeshModifierTextAnimation_PositionWaveProperty* Wave =
		NewObject<UDreamMeshModifierTextAnimation_PositionWaveProperty>(Animation);

	Wave->Init();
	Wave->Deinit();
	Wave->Deinit();

	Wave->SetPosition(FVector(0.0, 0.0, 12.0));
	TestEqual(TEXT("the wave amplitude round-trips"), Wave->GetPosition(), FVector(0.0, 0.0, 12.0));

	// Frequency and Speed are two different numbers -- one is the wavelength along the string of
	// characters, the other how fast the wave travels along it -- and the accessor pair named after
	// the first of them used to read and write the second, leaving the first reachable only from a
	// details panel. A getter that returns a different property than the one it names cannot be
	// worked around by a caller who knows about it, so both are addressed by name now.
	Wave->SetFrequency(2.5f);
	Wave->SetSpeed(4.0f);
	TestEqual(TEXT("frequency is the property frequency writes"), Wave->GetFrequency(), 2.5f);
	TestEqual(TEXT("and speed is a separate number that does not move with it"), Wave->GetSpeed(), 4.0f);
	Wave->SetFrequency(3.5f);
	TestEqual(TEXT("changing one leaves the other alone"), Wave->GetSpeed(), 4.0f);

	// Properties are held by the component in an Instanced array, and a details-panel array is a
	// row of null the instant an author presses "+". The list has to carry that hole rather than
	// compact it away, because the row the author is about to fill in is identified by its index --
	// and every consumer therefore has to expect a null in the middle of the list.
	TArray<UDreamMeshModifierTextAnimation_Property*> Properties;
	Properties.Add(nullptr);
	Properties.Add(Wave);
	Animation->SetProperties(Properties);
	TestEqual(TEXT("the list keeps the hole the author left"), Animation->GetProperties().Num(), 2);
	TestNull(TEXT("with the empty row still empty"), Animation->GetProperty(0));
	TestEqual(TEXT("and the filled one still at its index"), Animation->GetProperty(1),
		(UDreamMeshModifierTextAnimation_Property*)Wave);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
