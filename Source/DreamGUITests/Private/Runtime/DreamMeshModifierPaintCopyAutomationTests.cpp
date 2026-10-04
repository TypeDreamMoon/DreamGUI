// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Core/DreamUIGeometry.h"
#include "Core/Text/DreamTextPainter.h"
#include "DreamUIRender/DreamUIMeshIndex.h"
#include "MeshModifier/DreamMeshModifierBase.h"
#include "MeshModifier/DreamMeshModifierGradientColor.h"
#include "MeshModifier/DreamMeshModifierLongShadow.h"
#include "MeshModifier/DreamMeshModifierOutline.h"
#include "MeshModifier/DreamMeshModifierShadow.h"
#include "UObject/Package.h"

/*
 * The copies a mesh modifier makes of a text -- Shadow, Outline, LongShadow -- and painted text.
 *
 * A painted glyph quad carries which of its text's paints it uses on top of its code in UV2.x (DreamTextQuadCode's slots)
 * and its place in its gradient's boxes in UV4. A copy is a shadow: it paints nothing, so it must lose both, or the shadow
 * of a gold title is gold. A colour glyph's copy gets one more change: its face is drawn from the glyph's own colours,
 * which would give a shadow that is a second full-colour emoji, so the copy becomes its silhouette
 * (DreamTextQuadCode::ColorSilhouette), drawn in the copy's colour at the glyph's alpha. What stays: the originals,
 * untouched; an image's UV2, which is no quad code; and GradientColor, which only recolours.
 *
 * The geometries are built by hand, as the painter would have left them, and the modifiers run on them directly: no
 * widget, no canvas, nothing drawn.
 */
namespace DreamMeshModifierPaintCopyTestLocal
{
	constexpr int32 VertsPerQuad = 4;
	constexpr int32 IndicesPerQuad = 6;

	/** One glyph quad: its code without its slot, its slot, and whether it sits in a gradient's boxes (a UV4 of its own). */
	struct FQuad
	{
		float Code = 0.0f;
		int32 Slot = 0;
	};

	/**
	 * The quads of every kind a painter writes, painted where a quad can be: a field face (slot 1), the field effects copy
	 * that paints the outline (slot 1), a tag paint's coverage face (slot 5), a bitmap face (slot 3), a colour face and its
	 * effects copy (never painted), and a face of no paint. Codes chosen so that adding and taking off the slot is exact.
	 */
	const FQuad PaintedText[] =
	{
		{ 0.25f, 1 },
		{ DreamTextQuadCode::FieldLayerStride + 0.5f, 1 },
		{ DreamTextQuadCode::CoverageBase + 2.0f, 5 },
		{ 0.0f, 3 },
		{ DreamTextQuadCode::ColorFace, 0 },
		{ DreamTextQuadCode::ColorEffects, 0 },
		{ 0.75f, 0 },
	};

	/** Quads as a painter leaves them: a text's geometry (bIsFont), four vertices and two triangles each. */
	void BuildText(FDreamUIGeometry& OutGeo, TConstArrayView<FQuad> InQuads, bool bInIsFont = true)
	{
		OutGeo.Clear();
		OutGeo.bIsFont = bInIsFont;
		for (int32 QuadIndex = 0; QuadIndex < InQuads.Num(); ++QuadIndex)
		{
			const FQuad& Quad = InQuads[QuadIndex];
			const int32 Base = QuadIndex * VertsPerQuad;
			const float Left = QuadIndex * 20.0f;
			const FVector3f Corners[VertsPerQuad] =
			{
				FVector3f(0.0f, Left, 0.0f),
				FVector3f(0.0f, Left + 10.0f, 0.0f),
				FVector3f(0.0f, Left, 10.0f),
				FVector3f(0.0f, Left + 10.0f, 10.0f),
			};
			for (int32 Corner = 0; Corner < VertsPerQuad; ++Corner)
			{
				OutGeo.OriginVertices.Add(FDreamUIOriginVertexData(Corners[Corner]));
				FDreamUIMeshVertex Vertex(Corners[Corner], FColor(255, 255, 255, 255));
				Vertex.TextureCoordinate[0] = FVector2f((float)(Base + Corner), 0.5f);
				Vertex.TextureCoordinate[1] = FVector2f(7.0f, 2.0f);
				Vertex.TextureCoordinate[2] = FVector2f(DreamTextQuadCode::AddSlot(Quad.Code, Quad.Slot), 0.375f);
				Vertex.TextureCoordinate[3] = FVector2f(1.0f, 0.0f);
				if (Quad.Slot > 0)
				{
					Vertex.UV4 = FVector2f(0.125f * (Corner + 1), 0.0625f * (QuadIndex + 1));
				}
				OutGeo.Vertices.Add(Vertex);
			}
			const int32 Order[IndicesPerQuad] = { 0, 3, 2, 0, 1, 3 };
			for (int32 Index = 0; Index < IndicesPerQuad; ++Index)
			{
				OutGeo.Triangles.Add((FDreamUIMeshIndex)(Base + Order[Index]));
			}
		}
	}

	/** What a copy of InOriginal's code is: its code without the slot, a colour face turned into its silhouette. */
	float ExpectedCopyCode(float InOriginal)
	{
		const float Code = DreamTextQuadCode::StripSlot(InOriginal);
		return Code == DreamTextQuadCode::ColorFace ? DreamTextQuadCode::ColorSilhouette : Code;
	}

	/**
	 * InGeo after a modifier that made InCopies copies of the InOriginal vertices it started from, each block of copies laid
	 * out as the originals are, after them: every copy paints nothing and keeps the code of what it copies, every original
	 * is as it was.
	 */
	void CheckCopies(FAutomationTestBase& InTest, const TCHAR* InWhat, const FDreamUIGeometry& InGeo, const TArray<FDreamUIMeshVertex>& InOriginals, int32 InCopies)
	{
		const int32 Count = InOriginals.Num();
		if (!InTest.TestEqual(*FString::Printf(TEXT("%s: the originals and %d copies"), InWhat, InCopies), InGeo.Vertices.Num(), Count * (InCopies + 1)))
		{
			return;
		}
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const FDreamUIMeshVertex& Original = InGeo.Vertices[Index];
			InTest.TestTrue(*FString::Printf(TEXT("%s: original %d keeps its code, slot and all"), InWhat, Index),
				Original.TextureCoordinate[2].X == InOriginals[Index].TextureCoordinate[2].X);
			InTest.TestEqual(*FString::Printf(TEXT("%s: original %d keeps its UV4"), InWhat, Index), Original.UV4, InOriginals[Index].UV4);
		}
		for (int32 Copy = 1; Copy <= InCopies; ++Copy)
		{
			for (int32 Index = 0; Index < Count; ++Index)
			{
				const FDreamUIMeshVertex& Vertex = InGeo.Vertices[Copy * Count + Index];
				const FString Which = FString::Printf(TEXT("%s: copy %d of vertex %d"), InWhat, Copy, Index);
				InTest.TestEqual(*(Which + TEXT(" paints no slot")), DreamTextQuadCode::GetSlot(Vertex.TextureCoordinate[2].X), 0);
				InTest.TestTrue(*(Which + TEXT(" has the code of what it copies")),
					Vertex.TextureCoordinate[2].X == ExpectedCopyCode(InOriginals[Index].TextureCoordinate[2].X));
				InTest.TestEqual(*(Which + TEXT(" sits in no gradient box")), Vertex.UV4, FVector2f(0.0f, 0.0f));
				InTest.TestEqual(*(Which + TEXT(" keeps the fill position in UV2.y")), Vertex.TextureCoordinate[2].Y, InOriginals[Index].TextureCoordinate[2].Y);
			}
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamMeshModifierPaintCopyTest,
	"DreamGUI.MeshModifier.EveryCopyOfAPaintedTextPaintsNothingAndKeepsTheCodeOfWhatItCopies",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMeshModifierPaintCopyTest::RunTest(const FString& Parameters)
{
	using namespace DreamMeshModifierPaintCopyTestLocal;

	{
		FDreamUIGeometry Geo;
		BuildText(Geo, PaintedText);
		const TArray<FDreamUIMeshVertex> Originals = Geo.Vertices;
		NewObject<UDreamMeshModifierShadow>(GetTransientPackage())->ModifyUIGeometry(Geo, true, true, true, true);
		CheckCopies(*this, TEXT("Shadow"), Geo, Originals, 1);
	}
	{
		FDreamUIGeometry Geo;
		BuildText(Geo, PaintedText);
		const TArray<FDreamUIMeshVertex> Originals = Geo.Vertices;
		UDreamMeshModifierOutline* Outline = NewObject<UDreamMeshModifierOutline>(GetTransientPackage());
		Outline->SetUse8Direction(true);
		Outline->ModifyUIGeometry(Geo, true, true, true, true);
		CheckCopies(*this, TEXT("Outline"), Geo, Originals, 8);
	}
	{
		FDreamUIGeometry Geo;
		BuildText(Geo, PaintedText);
		const TArray<FDreamUIMeshVertex> Originals = Geo.Vertices;
		UDreamMeshModifierLongShadow* LongShadow = NewObject<UDreamMeshModifierLongShadow>(GetTransientPackage());
		LongShadow->SetShadowSegment(2);
		LongShadow->ModifyUIGeometry(Geo, true, true, true, true);
		CheckCopies(*this, TEXT("LongShadow"), Geo, Originals, 3);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamMeshModifierColorSilhouetteTest,
	"DreamGUI.MeshModifier.ACopyOfAColourGlyphIsItsSilhouetteInTheCopysColour",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMeshModifierColorSilhouetteTest::RunTest(const FString& Parameters)
{
	using namespace DreamMeshModifierPaintCopyTestLocal;

	// An emoji's face and its effects copy, shadowed and then outlined: the outline copies the shadow's copies as well.
	const FQuad Emoji[] = { { DreamTextQuadCode::ColorFace, 0 }, { DreamTextQuadCode::ColorEffects, 0 } };
	FDreamUIGeometry Geo;
	BuildText(Geo, Emoji);
	UDreamMeshModifierShadow* Shadow = NewObject<UDreamMeshModifierShadow>(GetTransientPackage());
	Shadow->SetShadowColor(FColor(10, 20, 30, 255));
	Shadow->ModifyUIGeometry(Geo, true, true, true, true);
	const int32 Count = 2 * VertsPerQuad;
	TestEqual(TEXT("The face's shadow is its silhouette"), Geo.Vertices[Count].TextureCoordinate[2].X, DreamTextQuadCode::ColorSilhouette);
	TestTrue(TEXT("...drawn as one, below the silhouette threshold"), Geo.Vertices[Count].TextureCoordinate[2].X < DreamTextQuadCode::ColorSilhouetteThreshold);
	TestEqual(TEXT("...in the shadow's colour"), Geo.Vertices[Count].Color, FColor(10, 20, 30, 255));
	TestEqual(TEXT("The effects copy's shadow draws the underlay as it did"), Geo.Vertices[Count + VertsPerQuad].TextureCoordinate[2].X, DreamTextQuadCode::ColorEffects);
	TestEqual(TEXT("The emoji itself is still a colour face"), Geo.Vertices[0].TextureCoordinate[2].X, DreamTextQuadCode::ColorFace);

	UDreamMeshModifierOutline* Outline = NewObject<UDreamMeshModifierOutline>(GetTransientPackage());
	Outline->SetUse8Direction(false);
	Outline->ModifyUIGeometry(Geo, true, true, true, true);
	const int32 Shadowed = 2 * Count;
	TestEqual(TEXT("The outline copied the face and its shadow four times over"), Geo.Vertices.Num(), Shadowed * 5);
	bool bEveryFaceCopyIsASilhouette = true;
	for (int32 Copy = 1; Copy <= 4; ++Copy)
	{
		// In each block, as in the mesh it copies: the emoji's face and its effects copy, then their shadows.
		bEveryFaceCopyIsASilhouette &= Geo.Vertices[Copy * Shadowed].TextureCoordinate[2].X == DreamTextQuadCode::ColorSilhouette;
		bEveryFaceCopyIsASilhouette &= Geo.Vertices[Copy * Shadowed + Count].TextureCoordinate[2].X == DreamTextQuadCode::ColorSilhouette;
	}
	TestTrue(TEXT("Every copy of the face, and of its silhouette, is a silhouette"), bEveryFaceCopyIsASilhouette);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamMeshModifierImageCopyTest,
	"DreamGUI.MeshModifier.ACopyOfAnImageKeepsItsUV2WhichIsNoGlyphCode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMeshModifierImageCopyTest::RunTest(const FString& Parameters)
{
	using namespace DreamMeshModifierPaintCopyTestLocal;

	// An image whose UV2 holds a position (as PositionAsUV writes one): far outside every code, and not a code at all.
	const FQuad Image[] = { { 300.0f, 0 } };
	FDreamUIGeometry Geo;
	BuildText(Geo, Image, /*bInIsFont*/ false);
	NewObject<UDreamMeshModifierShadow>(GetTransientPackage())->ModifyUIGeometry(Geo, true, true, true, true);
	if (!TestEqual(TEXT("The image and its shadow"), Geo.Vertices.Num(), 2 * VertsPerQuad))
	{
		return false;
	}
	TestEqual(TEXT("The shadow keeps the image's UV2"), Geo.Vertices[VertsPerQuad].TextureCoordinate[2].X, 300.0f);
	TestEqual(TEXT("...and paints nothing either"), Geo.Vertices[VertsPerQuad].UV4, FVector2f(0.0f, 0.0f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamMeshModifierGradientKeepsPaintTest,
	"DreamGUI.MeshModifier.AGradientColourRecoloursAPaintedTextAndLeavesItsSlotsAndBoxesAlone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMeshModifierGradientKeepsPaintTest::RunTest(const FString& Parameters)
{
	using namespace DreamMeshModifierPaintCopyTestLocal;

	FDreamUIGeometry Geo;
	BuildText(Geo, PaintedText);
	const TArray<FDreamUIMeshVertex> Originals = Geo.Vertices;
	UDreamMeshModifierGradientColor* Gradient = NewObject<UDreamMeshModifierGradientColor>(GetTransientPackage());
	Gradient->SetDirectionType(EDreamMeshModifierGradientColorDirection::FourCorner);
	Gradient->SetColor1(FColor(255, 0, 0, 255));
	Gradient->ModifyUIGeometry(Geo, true, true, true, true);
	TestEqual(TEXT("No vertex added"), Geo.Vertices.Num(), Originals.Num());
	bool bKept = true;
	for (int32 Index = 0; Index < Originals.Num(); ++Index)
	{
		bKept &= Geo.Vertices[Index].TextureCoordinate[2].X == Originals[Index].TextureCoordinate[2].X;
		bKept &= Geo.Vertices[Index].UV4 == Originals[Index].UV4;
	}
	TestTrue(TEXT("Every quad keeps its slot and its place in its boxes"), bKept);
	TestEqual(TEXT("...while its colours are the gradient's"), Geo.Vertices[0].Color, FColor(255, 0, 0, 255));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamMeshModifierShadowBudgetTest,
	"DreamGUI.MeshModifier.AMeshTooLargeForItsShadowIsDrawnWithoutItRatherThanDroppedWhole",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMeshModifierShadowBudgetTest::RunTest(const FString& Parameters)
{
	using namespace DreamMeshModifierPaintCopyTestLocal;

	if constexpr (LEXUI_MAX_VERTEX_COUNT > 4 * 65536)
	{
		AddInfo(TEXT("This build indexes with 32 bits: no mesh a test can build is too large for its shadow."));
	}
	else
	{
		// The largest mesh whose copy still fits, and the smallest whose copy does not.
		const int32 FittingQuads = (LEXUI_MAX_VERTEX_COUNT / 2) / VertsPerQuad;
		const int32 TooLargeQuads = FittingQuads + 1;
		TArray<FQuad> Quads;
		Quads.Init(FQuad{ 0.25f, 0 }, TooLargeQuads);

		UDreamMeshModifierShadow* Shadow = NewObject<UDreamMeshModifierShadow>(GetTransientPackage());
		AddExpectedMessagePlain(TEXT("too large to cast a shadow"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
		{
			FDreamUIGeometry Geo;
			BuildText(Geo, Quads);
			const int32 Vertices = Geo.Vertices.Num();
			const int32 Triangles = Geo.Triangles.Num();
			TestTrue(TEXT("The mesh and its copy would not fit"), 2 * Vertices > LEXUI_MAX_VERTEX_COUNT);
			Shadow->ModifyUIGeometry(Geo, true, true, true, true);
			TestEqual(TEXT("The mesh is left as it was: no copy"), Geo.Vertices.Num(), Vertices);
			TestEqual(TEXT("...its triangles too"), Geo.Triangles.Num(), Triangles);
			TestEqual(TEXT("...and its origin vertices"), Geo.OriginVertices.Num(), Vertices);
			// Said once, though a mesh is rebuilt every frame.
			Shadow->ModifyUIGeometry(Geo, true, true, true, true);
			TestEqual(TEXT("A second rebuild leaves it as it was too"), Geo.Vertices.Num(), Vertices);
		}
		{
			Quads.SetNum(FittingQuads);
			FDreamUIGeometry Geo;
			BuildText(Geo, Quads);
			const int32 Vertices = Geo.Vertices.Num();
			Shadow->ModifyUIGeometry(Geo, true, true, true, true);
			TestEqual(TEXT("A mesh whose copy fits still casts its shadow"), Geo.Vertices.Num(), 2 * Vertices);
			TestTrue(TEXT("...and every index stays inside what an index can address"), Geo.Vertices.Num() - 1 <= LEXUI_MAX_VERTEX_COUNT);
		}
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
