// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamImage.h"
#include "Core/Components/DreamTexture.h"
#include "Core/Components/DreamVisualBatchMesh.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIClipMask.h"
#include "Core/DreamUIEdgeAntiAliasing.h"
#include "Core/DreamUIGeometry.h"
#include "Core/DreamUISettings.h"
#include "Driver/DreamDriverRig.h"
#include "Extensions/DreamShape.h"
#include "Misc/ScopeExit.h"
#include "Utils/DreamUIUtils.h"

/*
 * DreamUIEdgeAntiAliasing::Apply, on geometry made by hand. What makes the strip right is where its vertices land: the
 * outline's own half a pixel in from each of its edges, their transparent copies half a pixel out, so that coverage is
 * one half on the edge itself, full one half-pixel in and none one half-pixel out -- and so that an edge on the pixel
 * grid gives no partial pixel at all. Those distances, the texture coordinate a moved vertex reads, which edges are the
 * outline (a seam of repeated vertices is not; a hole's rim is, facing into the hole) and the meshes it leaves alone are
 * what these tests hold it to. Whether the picture then shows it is the RHI test's question.
 */

namespace DreamEdgeAntiAliasingTestLocal
{
	FDreamUIGeometry MakeGeometry(TConstArrayView<FVector2f> InPositions, TConstArrayView<FVector2f> InUVs, TConstArrayView<int32> InIndices,
		FColor InColour = FColor::White)
	{
		FDreamUIGeometry Geometry;
		for (int32 Index = 0; Index < InPositions.Num(); ++Index)
		{
			const FVector3f Position(0.0f, InPositions[Index].X, InPositions[Index].Y);
			Geometry.OriginVertices.Add(FDreamUIOriginVertexData(Position));
			FDreamUIMeshVertex Vertex(Position, InColour);
			Vertex.TextureCoordinate[0] = InUVs.IsValidIndex(Index) ? InUVs[Index] : FVector2f::ZeroVector;
			Geometry.Vertices.Add(Vertex);
		}
		for (const int32 Index : InIndices)
		{
			Geometry.Triangles.Add((FDreamUIMeshIndex)Index);
		}
		return Geometry;
	}

	FVector2f PositionOf(const FDreamUIGeometry& InGeometry, int32 InIndex)
	{
		const FVector3f& Position = InGeometry.OriginVertices[InIndex].Position;
		return FVector2f(Position.Y, Position.Z);
	}

	float SignedArea(const FDreamUIGeometry& InGeometry, int32 InTriangle)
	{
		const FVector2f A = PositionOf(InGeometry, InGeometry.Triangles[InTriangle * 3]);
		const FVector2f B = PositionOf(InGeometry, InGeometry.Triangles[InTriangle * 3 + 1]);
		const FVector2f C = PositionOf(InGeometry, InGeometry.Triangles[InTriangle * 3 + 2]);
		return ((B - A) ^ (C - A)) * 0.5f;
	}

	/** Signed distance of InPoint from the line through A and B, positive on the side InInside is on. */
	float DistanceFromLine(const FVector2f& InPoint, const FVector2f& A, const FVector2f& B, const FVector2f& InInside)
	{
		const FVector2f Along = (B - A).GetSafeNormal();
		const FVector2f Normal(-Along.Y, Along.X);
		const float Side = FVector2f::DotProduct(InInside - A, Normal) >= 0.0f ? 1.0f : -1.0f;
		return FVector2f::DotProduct(InPoint - A, Normal) * Side;
	}

	const FVector2f SquareCorners[] = { FVector2f(0.0f, 0.0f), FVector2f(100.0f, 0.0f), FVector2f(100.0f, 100.0f), FVector2f(0.0f, 100.0f) };
	const FVector2f SquareUVs[] = { FVector2f(0.0f, 0.0f), FVector2f(1.0f, 0.0f), FVector2f(1.0f, 1.0f), FVector2f(0.0f, 1.0f) };
	const int32 SquareIndices[] = { 0, 1, 2, 0, 2, 3 };

	/** The project's settings as Default reads them in these tests -- the strip on, no MSAA -- and back as they were after. */
	struct FScopedStripSettings
	{
		UDreamUISettings* Settings = GetMutableDefault<UDreamUISettings>();
		EDreamUIRendererAntiAliasingMethod SavedMethod = Settings->AntiAliasingMethod;
		bool bSavedEdge = Settings->bEdgeAntiAliasing;

		FScopedStripSettings()
		{
			Settings->AntiAliasingMethod = EDreamUIRendererAntiAliasingMethod::None;
			Settings->bEdgeAntiAliasing = true;
		}
		~FScopedStripSettings()
		{
			Settings->AntiAliasingMethod = SavedMethod;
			Settings->bEdgeAntiAliasing = bSavedEdge;
		}
	};

	/** The element's vertex count once the rig has drawn whatever was changed. */
	int32 VerticesAfterFrames(FDreamDriverRig& InRig, const UDreamVisualBatchMesh* InVisual)
	{
		InRig.PumpFrames(2);
		const FDreamUIGeometry* Geometry = InVisual != nullptr ? InVisual->GetGeometry() : nullptr;
		return Geometry != nullptr ? Geometry->Vertices.Num() : -1;
	}

	/** An Image drawing the white texture as InDrawAs: a 32 px image with a quarter of it for each border. */
	UDreamImage* MakeImage(FDreamDriverRig& InRig, const TCHAR* InName, FVector2D InSize, EDreamUIImageBrushDrawType InDrawAs)
	{
		UDreamWidget* Widget = InRig.MakeWidget(InName, nullptr, InSize);
		UDreamImage* Image = Widget != nullptr ? Widget->CreateNewVisual<UDreamImage>() : nullptr;
		if (Image != nullptr)
		{
			FDreamUIImageBrush Brush = Image->GetBrush();
			Brush.SetResourceObject(FDreamUIUtils::GetDefaultWhiteTexture());
			Brush.DrawAs = InDrawAs;
			Brush.ImageSize = FVector2f(32.0f, 32.0f);
			Brush.Margin = FMargin(0.25f);
			Image->SetBrush(Brush);
		}
		return Image;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamEdgeAntiAliasingTriangleTest,
	"DreamGUI.EdgeAntiAliasing.ATrianglesEdgesGetAStripFromHalfAPixelInsideToHalfAPixelOutside",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamEdgeAntiAliasingTriangleTest::RunTest(const FString& Parameters)
{
	using namespace DreamEdgeAntiAliasingTestLocal;
	const FVector2f Corners[] = { FVector2f(0.0f, 0.0f), FVector2f(100.0f, 0.0f), FVector2f(0.0f, 100.0f) };
	const int32 Indices[] = { 0, 1, 2 };
	FDreamUIGeometry Geometry = MakeGeometry(Corners, {}, Indices);
	const float OriginalSign = FMath::Sign(SignedArea(Geometry, 0));

	const DreamUIEdgeAntiAliasing::FResult Result = DreamUIEdgeAntiAliasing::Apply(Geometry, 1.0f);
	TestTrue(TEXT("The strips are added"), Result.bApplied);
	TestEqual(TEXT("All three edges are the outline"), Result.OutlineEdges, 3);
	TestEqual(TEXT("Two transparent copies per edge"), Geometry.Vertices.Num(), 3 + 6);
	TestEqual(TEXT("Two triangles per edge"), Geometry.Triangles.Num(), 3 + 18);
	TestEqual(TEXT("The positions and the vertices still pair up"), Geometry.OriginVertices.Num(), Geometry.Vertices.Num());

	// Every outline vertex is half a pixel inside both edges that meet at it -- the mitre is what keeps it so.
	const FVector2f Centre = (Corners[0] + Corners[1] + Corners[2]) / 3.0f;
	for (int32 Corner = 0; Corner < 3; ++Corner)
	{
		const FVector2f Moved = PositionOf(Geometry, Corner);
		for (const int32 Other : { (Corner + 1) % 3, (Corner + 2) % 3 })
		{
			TestNearlyEqual(*FString::Printf(TEXT("Corner %d is half a pixel inside its edge to corner %d"), Corner, Other),
				DistanceFromLine(Moved, Corners[Corner], Corners[Other], Centre), 0.5f, 1.0e-3f);
		}
		TestEqual(*FString::Printf(TEXT("Corner %d keeps its colour"), Corner), Geometry.Vertices[Corner].Color.A, (uint8)255);
	}
	// And every copy is half a pixel outside the edge it belongs to, and transparent.
	for (int32 Strip = 0; Strip < 3; ++Strip)
	{
		const int32 First = Geometry.Triangles[3 + Strip * 6];
		const int32 Second = Geometry.Triangles[3 + Strip * 6 + 1];
		const int32 OuterA = Geometry.Triangles[3 + Strip * 6 + 2];
		const int32 OuterB = Geometry.Triangles[3 + Strip * 6 + 5];
		for (const int32 Outer : { OuterA, OuterB })
		{
			TestNearlyEqual(*FString::Printf(TEXT("Strip %d: its outer vertex is half a pixel out"), Strip),
				DistanceFromLine(PositionOf(Geometry, Outer), Corners[First], Corners[Second], Centre), -0.5f, 1.0e-3f);
			TestEqual(*FString::Printf(TEXT("Strip %d: and transparent"), Strip), Geometry.Vertices[Outer].Color.A, (uint8)0);
		}
	}
	for (int32 Triangle = 0; Triangle < Geometry.Triangles.Num() / 3; ++Triangle)
	{
		TestEqual(*FString::Printf(TEXT("Triangle %d is wound as the original"), Triangle), FMath::Sign(SignedArea(Geometry, Triangle)), OriginalSign);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamEdgeAntiAliasingSquareTest,
	"DreamGUI.EdgeAntiAliasing.AnEdgeOnThePixelGridStaysSharpAndTheTextureStaysWhereItWas",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamEdgeAntiAliasingSquareTest::RunTest(const FString& Parameters)
{
	using namespace DreamEdgeAntiAliasingTestLocal;
	FDreamUIGeometry Geometry = MakeGeometry(SquareCorners, SquareUVs, SquareIndices);
	const DreamUIEdgeAntiAliasing::FResult Result = DreamUIEdgeAntiAliasing::Apply(Geometry, 1.0f);
	TestTrue(TEXT("The strips are added"), Result.bApplied);
	TestEqual(TEXT("The four sides are the outline and the diagonal is not"), Result.OutlineEdges, 4);

	// The left side is at x = 0, a pixel boundary. Its strip runs from x = 0.5 (opaque) to x = -0.5 (clear): the centre
	// of the pixel inside it is fully covered and the centre of the pixel outside not at all, so the edge is as sharp
	// as it was without the strip.
	TestNearlyEqual(TEXT("The corner moves half a pixel in along x"), PositionOf(Geometry, 0).X, 0.5f, 1.0e-4f);
	TestNearlyEqual(TEXT("...and along y"), PositionOf(Geometry, 0).Y, 0.5f, 1.0e-4f);
	float MinOuterX = MAX_flt;
	for (int32 Index = 4; Index < Geometry.Vertices.Num(); ++Index)
	{
		MinOuterX = FMath::Min(MinOuterX, PositionOf(Geometry, Index).X);
	}
	TestNearlyEqual(TEXT("The strip ends half a pixel out"), MinOuterX, -0.5f, 1.0e-4f);

	// The moved corner reads the texture coordinate of where it now is, so the picture is not squeezed half a pixel.
	TestNearlyEqual(TEXT("The moved corner's u is that of x = 0.5"), Geometry.Vertices[0].TextureCoordinate[0].X, 0.005f, 1.0e-5f);
	TestNearlyEqual(TEXT("...and its v that of y = 0.5"), Geometry.Vertices[0].TextureCoordinate[0].Y, 0.005f, 1.0e-5f);
	TestNearlyEqual(TEXT("The opposite corner's u is that of x = 99.5"), Geometry.Vertices[2].TextureCoordinate[0].X, 0.995f, 1.0e-5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamEdgeAntiAliasingSeamTest,
	"DreamGUI.EdgeAntiAliasing.RepeatedVerticesAlongASeamAreNotTakenForTheOutline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamEdgeAntiAliasingSeamTest::RunTest(const FString& Parameters)
{
	using namespace DreamEdgeAntiAliasingTestLocal;
	// The square as two triangles that share no vertex: the diagonal is a seam of repeated vertices, as a mesh that
	// changes its texture or colour along a line has.
	const FVector2f Corners[] = { SquareCorners[0], SquareCorners[1], SquareCorners[2], SquareCorners[0], SquareCorners[2], SquareCorners[3] };
	const int32 Indices[] = { 0, 1, 2, 3, 4, 5 };
	FDreamUIGeometry Geometry = MakeGeometry(Corners, {}, Indices);
	const DreamUIEdgeAntiAliasing::FResult Result = DreamUIEdgeAntiAliasing::Apply(Geometry, 1.0f);
	TestEqual(TEXT("Only the four sides are the outline"), Result.OutlineEdges, 4);
	// Both copies of a corner on the seam move the same way, so the two halves still meet.
	TestTrue(TEXT("The two copies of the seam's corner stay together"),
		PositionOf(Geometry, 0).Equals(PositionOf(Geometry, 3), 1.0e-4f) && PositionOf(Geometry, 2).Equals(PositionOf(Geometry, 4), 1.0e-4f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamEdgeAntiAliasingHoleTest,
	"DreamGUI.EdgeAntiAliasing.AHolesRimIsOutlineTooAndItsStripLiesInTheHole",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamEdgeAntiAliasingHoleTest::RunTest(const FString& Parameters)
{
	using namespace DreamEdgeAntiAliasingTestLocal;
	// A square frame: the outer square 0..100, the hole 25..75, in eight triangles.
	const FVector2f Corners[] = {
		FVector2f(0.0f, 0.0f), FVector2f(100.0f, 0.0f), FVector2f(100.0f, 100.0f), FVector2f(0.0f, 100.0f),
		FVector2f(25.0f, 25.0f), FVector2f(75.0f, 25.0f), FVector2f(75.0f, 75.0f), FVector2f(25.0f, 75.0f) };
	const int32 Indices[] = { 0, 1, 5, 0, 5, 4, 1, 2, 6, 1, 6, 5, 2, 3, 7, 2, 7, 6, 3, 0, 4, 3, 4, 7 };
	FDreamUIGeometry Geometry = MakeGeometry(Corners, {}, Indices);
	const DreamUIEdgeAntiAliasing::FResult Result = DreamUIEdgeAntiAliasing::Apply(Geometry, 1.0f);
	TestEqual(TEXT("Four outer sides and four sides of the hole"), Result.OutlineEdges, 8);
	// The hole's corner moves away from the hole, into the frame, and its copies stand in the hole.
	TestTrue(TEXT("The hole's corner moves into the frame"), PositionOf(Geometry, 4).X < 25.0f && PositionOf(Geometry, 4).Y < 25.0f);
	int32 CopiesInTheHole = 0;
	for (int32 Index = 8; Index < Geometry.Vertices.Num(); ++Index)
	{
		const FVector2f Position = PositionOf(Geometry, Index);
		CopiesInTheHole += (Position.X > 25.0f && Position.X < 75.0f && Position.Y > 25.0f && Position.Y < 75.0f) ? 1 : 0;
	}
	TestEqual(TEXT("The hole's four strips have their outer halves in the hole"), CopiesInTheHole, 8);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamEdgeAntiAliasingTranslucentTest,
	"DreamGUI.EdgeAntiAliasing.ATranslucentFillKeepsItsAlphaAndOnlyTheStripFadesToNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamEdgeAntiAliasingTranslucentTest::RunTest(const FString& Parameters)
{
	using namespace DreamEdgeAntiAliasingTestLocal;
	FDreamUIGeometry Geometry = MakeGeometry(SquareCorners, SquareUVs, SquareIndices, FColor(200, 100, 50, 128));
	DreamUIEdgeAntiAliasing::Apply(Geometry, 1.0f);
	for (int32 Index = 0; Index < 4; ++Index)
	{
		TestEqual(*FString::Printf(TEXT("Corner %d keeps its alpha"), Index), Geometry.Vertices[Index].Color, FColor(200, 100, 50, 128));
	}
	for (int32 Index = 4; Index < Geometry.Vertices.Num(); ++Index)
	{
		TestEqual(*FString::Printf(TEXT("Copy %d keeps the colour"), Index), Geometry.Vertices[Index].Color.R, (uint8)200);
		TestEqual(*FString::Printf(TEXT("Copy %d is clear"), Index), Geometry.Vertices[Index].Color.A, (uint8)0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamEdgeAntiAliasingThinTest,
	"DreamGUI.EdgeAntiAliasing.AShapeThinnerThanAPixelThinsRatherThanTurningInsideOut",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamEdgeAntiAliasingThinTest::RunTest(const FString& Parameters)
{
	using namespace DreamEdgeAntiAliasingTestLocal;
	// A bar 100 long and 0.4 of a pixel tall: half a pixel in from each side would cross the other.
	const FVector2f Corners[] = { FVector2f(0.0f, 0.0f), FVector2f(100.0f, 0.0f), FVector2f(100.0f, 0.4f), FVector2f(0.0f, 0.4f) };
	FDreamUIGeometry Geometry = MakeGeometry(Corners, {}, SquareIndices);
	DreamUIEdgeAntiAliasing::Apply(Geometry, 1.0f);
	TestTrue(TEXT("The bottom corner stays below the top one"), PositionOf(Geometry, 0).Y < PositionOf(Geometry, 3).Y);
	TestTrue(TEXT("...on the other end too"), PositionOf(Geometry, 1).Y < PositionOf(Geometry, 2).Y);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamEdgeAntiAliasingRefusalTest,
	"DreamGUI.EdgeAntiAliasing.AMeshItCannotStripIsLeftExactlyAsItWas",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamEdgeAntiAliasingRefusalTest::RunTest(const FString& Parameters)
{
	using namespace DreamEdgeAntiAliasingTestLocal;
	const auto Unchanged = [this](const FDreamUIGeometry& InGeometry, const TCHAR* InWhat)
	{
		TestEqual(*FString::Printf(TEXT("%s: no vertex added"), InWhat), InGeometry.Vertices.Num(), 4);
		TestEqual(*FString::Printf(TEXT("%s: no triangle added"), InWhat), InGeometry.Triangles.Num(), 6);
		TestTrue(*FString::Printf(TEXT("%s: no vertex moved"), InWhat), PositionOf(InGeometry, 0).Equals(FVector2f::ZeroVector));
	};
	{
		FDreamUIGeometry Geometry = MakeGeometry(SquareCorners, SquareUVs, SquareIndices);
		TestFalse(TEXT("No pixel: not applied"), DreamUIEdgeAntiAliasing::Apply(Geometry, 0.0f).bApplied);
		Unchanged(Geometry, TEXT("No pixel"));
	}
	{
		FDreamUIGeometry Geometry = MakeGeometry(SquareCorners, SquareUVs, SquareIndices);
		Geometry.OriginVertices[2].Position.X = 5.0f;
		TestFalse(TEXT("A mesh off its plane: not applied"), DreamUIEdgeAntiAliasing::Apply(Geometry, 1.0f).bApplied);
		Unchanged(Geometry, TEXT("Off its plane"));
	}
	{
		FDreamUIGeometry Geometry = MakeGeometry(SquareCorners, SquareUVs, SquareIndices);
		Geometry.Triangles[5] = 9;
		TestFalse(TEXT("An index past the vertices: not applied"), DreamUIEdgeAntiAliasing::Apply(Geometry, 1.0f).bApplied);
		TestEqual(TEXT("...and nothing added"), Geometry.Vertices.Num(), 4);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamEdgeAntiAliasingElementTest,
	"DreamGUI.EdgeAntiAliasing.AShapeOnAScreenCanvasGetsItsStripAsItsSettingSays",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The element's side: a Shape on a screen-space canvas, pumped as a game would. Three points make three triangle
 * corners and one triangle; the strip adds two vertices and two triangles an edge. On adds it whatever the project
 * says; Off never; Default not while MSAA smooths the outline already, and again once the setting is switched off.
 */
bool FDreamEdgeAntiAliasingElementTest::RunTest(const FString& Parameters)
{
	FDreamDriverRig Rig = FDreamDriverRig::Headless(FIntPoint(1280, 720));
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamWidget* Widget = Rig.MakeWidget(TEXT("Slash"), nullptr, FVector2D(200.0, 120.0));
	UDreamShape* Shape = Widget != nullptr ? Widget->CreateNewVisual<UDreamShape>() : nullptr;
	if (!TestNotNull(TEXT("A shape"), Shape))
	{
		return false;
	}
	Shape->SetPoints({ FVector2D(0.0, 0.0), FVector2D(1.0, 0.0), FVector2D(0.3, 1.0) });

	UDreamUISettings* Settings = GetMutableDefault<UDreamUISettings>();
	const EDreamUIRendererAntiAliasingMethod SavedMethod = Settings->AntiAliasingMethod;
	const bool bSavedEdge = Settings->bEdgeAntiAliasing;
	ON_SCOPE_EXIT
	{
		Settings->AntiAliasingMethod = SavedMethod;
		Settings->bEdgeAntiAliasing = bSavedEdge;
	};
	const auto Vertices = [&Rig, Shape](EDreamEdgeAntiAliasing InMode)
	{
		Shape->SetEdgeAntiAliasing(InMode);
		Shape->MarkVerticesDirty();
		Rig.PumpFrames(2);
		const FDreamUIGeometry* Geometry = Shape->GetGeometry();
		return Geometry != nullptr ? Geometry->Vertices.Num() : -1;
	};

	Settings->bEdgeAntiAliasing = true;
	Settings->AntiAliasingMethod = EDreamUIRendererAntiAliasingMethod::None;
	TestEqual(TEXT("Default, no MSAA: the three edges get their strips"), Vertices(EDreamEdgeAntiAliasing::Default), 3 + 6);
	TestTrue(TEXT("...on a pixel worked out from the canvas"), Shape->GetEdgeAntiAliasingPixel() > 0.0f);
	TestEqual(TEXT("Off: the shape's own three vertices"), Vertices(EDreamEdgeAntiAliasing::Off), 3);
	TestEqual(TEXT("On again: the strips come back"), Vertices(EDreamEdgeAntiAliasing::On), 3 + 6);

	Settings->AntiAliasingMethod = EDreamUIRendererAntiAliasingMethod::MSAA;
	if (UDreamUISettings::GetMultisamplesInEffect() > 1)
	{
		TestEqual(TEXT("Default under MSAA: no strips"), Vertices(EDreamEdgeAntiAliasing::Default), 3);
		TestEqual(TEXT("On under MSAA: strips anyway"), Vertices(EDreamEdgeAntiAliasing::On), 3 + 6);
	}
	Settings->AntiAliasingMethod = EDreamUIRendererAntiAliasingMethod::None;
	Settings->bEdgeAntiAliasing = false;
	TestEqual(TEXT("Default with the setting off: no strips"), Vertices(EDreamEdgeAntiAliasing::Default), 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamEdgeAntiAliasingKeptQuadTest,
	"DreamGUI.EdgeAntiAliasing.AStrippedQuadKeepsTheLinesItWasDrawnWith",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamEdgeAntiAliasingKeptQuadTest::RunTest(const FString& Parameters)
{
	using namespace DreamEdgeAntiAliasingTestLocal;
	// A simple quad as UpdateUIRectSimpleVertex lays one out: bottom-left, bottom-right, top-left, top-right.
	const FVector2f Corners[] = { FVector2f(-60.0f, -40.0f), FVector2f(60.0f, -40.0f), FVector2f(-60.0f, 40.0f), FVector2f(60.0f, 40.0f) };
	const FVector2f UVs[] = { FVector2f(0.0f, 1.0f), FVector2f(1.0f, 1.0f), FVector2f(0.0f, 0.0f), FVector2f(1.0f, 0.0f) };
	const int32 Indices[] = { 0, 3, 2, 0, 1, 3 };
	FDreamUIGeometry Geometry = MakeGeometry(Corners, UVs, Indices);
	Geometry.DrawnQuad = FDreamUIGeometry::EDrawnQuad::Simple;
	FDreamUIGeometry::FDrawnQuadLines Before;
	if (!TestTrue(TEXT("The quad's lines read off its vertices"), Geometry.ReadDrawnQuadLines(Before)))
	{
		return false;
	}
	TestTrue(TEXT("The strips are added"), DreamUIEdgeAntiAliasing::Apply(Geometry, 1.0f).bApplied);
	TestTrue(TEXT("It is still the simple quad a clip underlay reads"), Geometry.DrawnQuad == FDreamUIGeometry::EDrawnQuad::Simple);
	TestTrue(TEXT("...with its lines kept"), Geometry.KeptQuadLines.IsSet());

	FDreamUIGeometry::FDrawnQuadLines Moved;
	FDreamUIGeometry::FDrawnQuadLines Kept;
	Geometry.ReadDrawnQuadLines(Moved);
	TestTrue(TEXT("The lines asked for"), Geometry.GetDrawnQuadLines(Kept));
	TestNearlyEqual(TEXT("The corner itself moved half a pixel in"), Moved.X[0], -59.5f, 1.0e-4f);
	for (int32 Line = 0; Line < 4; ++Line)
	{
		TestNearlyEqual(*FString::Printf(TEXT("Line %d: x as drawn"), Line), Kept.X[Line], Before.X[Line], 1.0e-5f);
		TestNearlyEqual(*FString::Printf(TEXT("Line %d: y as drawn"), Line), Kept.Y[Line], Before.Y[Line], 1.0e-5f);
		TestNearlyEqual(*FString::Printf(TEXT("Line %d: u as drawn"), Line), Kept.U[Line], Before.U[Line], 1.0e-6f);
		TestNearlyEqual(*FString::Printf(TEXT("Line %d: v as drawn"), Line), Kept.V[Line], Before.V[Line], 1.0e-6f);
	}
	Geometry.Clear();
	TestFalse(TEXT("Cleared, nothing is kept"), Geometry.KeptQuadLines.IsSet());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamEdgeAntiAliasingTurnedRectTest,
	"DreamGUI.EdgeAntiAliasing.ARectGetsItsStripOnlyWhileItsSidesAreTurnedOffThePixelGrid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A Texture drawn plain is four vertices. Unturned its sides lie along the pixel rows and columns -- off whole pixels or not
 * -- and it stays four. Turned in the canvas plane, by itself or by its parent, each of its four sides gets a strip, two
 * vertices; a quarter turn puts them back on the rows and columns, and so does turning it back.
 */
bool FDreamEdgeAntiAliasingTurnedRectTest::RunTest(const FString& Parameters)
{
	using namespace DreamEdgeAntiAliasingTestLocal;
	const FScopedStripSettings StripSettings;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(FIntPoint(1280, 720));
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamWidget* Holder = Rig.MakeWidget(TEXT("Holder"), nullptr, FVector2D(300.0, 200.0));
	UDreamWidget* Card = Rig.MakeWidget(TEXT("Card"), Holder, FVector2D(120.0, 80.0), FVector2D(10.5, 3.25));
	UDreamTexture* Texture = Card != nullptr ? Card->CreateNewVisual<UDreamTexture>() : nullptr;
	if (!TestNotNull(TEXT("A texture"), Texture))
	{
		return false;
	}
	Texture->SetTexture(FDreamUIUtils::GetDefaultWhiteTexture());

	TestEqual(TEXT("Unturned, off whole pixels: the quad's own four vertices"), VerticesAfterFrames(Rig, Texture), 4);
	TestEqual(TEXT("...and no pixel to make a strip with"), Texture->GetEdgeAntiAliasingPixel(), 0.0f);
	Card->SetRelativeRotationEuler(FRotator(0.0, 0.0, 30.0));
	TestEqual(TEXT("Turned 30 degrees: a strip along each side"), VerticesAfterFrames(Rig, Texture), 4 + 8);
	TestTrue(TEXT("...on a pixel worked out from the canvas"), Texture->GetEdgeAntiAliasingPixel() > 0.0f);
	Card->SetRelativeRotationEuler(FRotator(0.0, 0.0, 90.0));
	TestEqual(TEXT("A quarter turn: its sides are along the rows and columns again"), VerticesAfterFrames(Rig, Texture), 4);
	Card->SetRelativeRotationEuler(FRotator::ZeroRotator);
	Holder->SetRelativeRotationEuler(FRotator(0.0, 0.0, -20.0));
	TestEqual(TEXT("Turned by its parent: the strips again"), VerticesAfterFrames(Rig, Texture), 4 + 8);
	Holder->SetRelativeRotationEuler(FRotator::ZeroRotator);
	TestEqual(TEXT("Turned back: its own four"), VerticesAfterFrames(Rig, Texture), 4);
	Texture->SetEdgeAntiAliasing(EDreamEdgeAntiAliasing::Off);
	Card->SetRelativeRotationEuler(FRotator(0.0, 0.0, 30.0));
	TestEqual(TEXT("Off: four, turned or not"), VerticesAfterFrames(Rig, Texture), 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamEdgeAntiAliasingSlicedTiledTest,
	"DreamGUI.EdgeAntiAliasing.ATurnedNineSliceOrTiledImageIsStrippedAlongItsOutsideOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A nine-slice is sixteen vertices in four rows of four, and a tiled image four to a tile, every tile its own: inside
 * either, cells meet along seams, which are no outline. Turned, a nine-slice gets three strips a side, twelve; one without
 * its centre has the hole's four sides besides; a tiled image gets one a tile along its outside.
 */
bool FDreamEdgeAntiAliasingSlicedTiledTest::RunTest(const FString& Parameters)
{
	using namespace DreamEdgeAntiAliasingTestLocal;
	const FScopedStripSettings StripSettings;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(FIntPoint(1280, 720));
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamImage* Sliced = MakeImage(Rig, TEXT("Sliced"), FVector2D(120.0, 80.0), EDreamUIImageBrushDrawType::Box);
	UDreamImage* Frame = MakeImage(Rig, TEXT("Frame"), FVector2D(120.0, 80.0), EDreamUIImageBrushDrawType::Border);
	// 100 x 70 of 32 px tiles: four across, the last one 4 px wide, and three up, the last one 6 px tall.
	UDreamImage* Tiled = MakeImage(Rig, TEXT("Tiled"), FVector2D(100.0, 70.0), EDreamUIImageBrushDrawType::Tiled);
	if (!TestNotNull(TEXT("A nine-slice"), Sliced) || !TestNotNull(TEXT("A frame"), Frame) || !TestNotNull(TEXT("A tiled image"), Tiled))
	{
		return false;
	}
	TestEqual(TEXT("Unturned, the nine-slice's sixteen"), VerticesAfterFrames(Rig, Sliced), 16);
	TestEqual(TEXT("...the frame's sixteen"), VerticesAfterFrames(Rig, Frame), 16);
	TestEqual(TEXT("...and twelve tiles' four each"), VerticesAfterFrames(Rig, Tiled), 48);
	for (UDreamImage* Image : { Sliced, Frame, Tiled })
	{
		Image->GetWidget()->SetRelativeRotationEuler(FRotator(0.0, 0.0, 25.0));
	}
	TestEqual(TEXT("Turned, the nine-slice: three strips a side"), VerticesAfterFrames(Rig, Sliced), 16 + 2 * 12);
	TestEqual(TEXT("...the frame: and four round its hole"), VerticesAfterFrames(Rig, Frame), 16 + 2 * (12 + 4));
	TestEqual(TEXT("...the tiled image: four along the bottom and the top, three up each side"), VerticesAfterFrames(Rig, Tiled), 48 + 2 * 14);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamEdgeAntiAliasingUnderlayTest,
	"DreamGUI.EdgeAntiAliasing.ATurnedImageUnderAClipIsReadAsTheQuadItDrewBeforeItsStrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A widget clipping to the image under it reads that image's quad (UDreamVisualBatchMesh::GetClipUnderlay). The strip moves
 * the quad's corners half a pixel in and gives them the texture coordinate of where they then stand; read off them, the
 * clip would follow a quad a pixel smaller. It reads what the image drew instead, the same with the strip as without.
 */
bool FDreamEdgeAntiAliasingUnderlayTest::RunTest(const FString& Parameters)
{
	using namespace DreamEdgeAntiAliasingTestLocal;
	const FScopedStripSettings StripSettings;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(FIntPoint(1280, 720));
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	for (const EDreamUIImageBrushDrawType DrawAs : { EDreamUIImageBrushDrawType::Image, EDreamUIImageBrushDrawType::Box })
	{
		const bool bSliced = DrawAs == EDreamUIImageBrushDrawType::Box;
		const TCHAR* What = bSliced ? TEXT("Nine-slice") : TEXT("Plain");
		UDreamImage* Image = MakeImage(Rig, What, FVector2D(120.0, 80.0), DrawAs);
		if (!TestNotNull(*FString::Printf(TEXT("%s: an image"), What), Image))
		{
			return false;
		}
		Image->GetWidget()->SetRelativeRotationEuler(FRotator(0.0, 0.0, 25.0));
		const int32 Own = bSliced ? 16 : 4;
		TestEqual(*FString::Printf(TEXT("%s: turned, it has its strips"), What), VerticesAfterFrames(Rig, Image), Own + 2 * (bSliced ? 12 : 4));
		FDreamClipUnderlay WithStrip;
		TestTrue(*FString::Printf(TEXT("%s: a clip can follow it"), What), Image->GetClipUnderlay(WithStrip));
		Image->SetEdgeAntiAliasing(EDreamEdgeAntiAliasing::Off);
		TestEqual(*FString::Printf(TEXT("%s: off, its own vertices"), What), VerticesAfterFrames(Rig, Image), Own);
		FDreamClipUnderlay WithoutStrip;
		TestTrue(*FString::Printf(TEXT("%s: ...which a clip follows too"), What), Image->GetClipUnderlay(WithoutStrip));
		TestTrue(*FString::Printf(TEXT("%s: the centre is the one it drew"), What), WithStrip.Center.Equals(WithoutStrip.Center, 1.0e-4f));
		TestTrue(*FString::Printf(TEXT("%s: the half size too, not half a pixel less"), What), WithStrip.HalfSize.Equals(WithoutStrip.HalfSize, 1.0e-4f));
		TestTrue(*FString::Printf(TEXT("%s: the borders"), What), WithStrip.Border.Equals(WithoutStrip.Border, 1.0e-4f));
		TestTrue(*FString::Printf(TEXT("%s: the u across"), What), WithStrip.U.Equals(WithoutStrip.U, 1.0e-6f));
		TestTrue(*FString::Printf(TEXT("%s: the v up"), What), WithStrip.V.Equals(WithoutStrip.V, 1.0e-6f));
		TestNearlyEqual(*FString::Printf(TEXT("%s: the half width it drew"), What), WithStrip.HalfSize.X, 60.0f, 1.0e-3f);
	}
	return true;
}

#endif
