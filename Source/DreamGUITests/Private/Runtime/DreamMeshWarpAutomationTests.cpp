// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Engine/World.h"
#include "UObject/Package.h"

#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamTexture.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIGeometry.h"
#include "Core/DreamUIManager.h"
#include "Extensions/DreamPath.h"
#include "MeshModifier/DreamMeshDeform.h"
#include "MeshModifier/DreamMeshModifierJelly.h"
#include "MeshModifier/DreamMeshModifierOutline.h"
#include "MeshModifier/DreamMeshModifierShadow.h"
#include "MeshModifier/DreamMeshModifierTextAnimation.h"
#include "MeshModifier/DreamMeshModifierTextOnPath.h"
#include "MeshModifier/DreamMeshModifierWarp.h"
#include "Utils/DreamUIUtils.h"

#include "Lifecycle/DreamLifecycleFixtures.h"

/*
 * Mesh warps, jellies and text on a path, on the CPU: the cutting that gives a mesh vertices to bend, the warp styles, the
 * lattice, the springs, the paths and the placing of glyphs along them. Everything here is arithmetic the GPU never
 * re-does -- a modifier writes vertices -- so what these hold is what is drawn; how it looks is the picture tests'
 * (DreamMeshWarpPixelAutomationTests).
 */
namespace DreamMeshWarpTestLocal
{
	/** A quad of two triangles over InMin..InMax, wound as the rect writers wind theirs, its UV0 0..1 across it. */
	void AddQuad(FDreamUIGeometry& Geo, FVector2f InMin, FVector2f InMax, float InCode = 7.0f)
	{
		const int32 Base = Geo.OriginVertices.Num();
		const FVector2f Corners[4] = { InMin, FVector2f(InMax.X, InMin.Y), FVector2f(InMin.X, InMax.Y), InMax };
		const FVector2f UVs[4] = { FVector2f(0, 1), FVector2f(1, 1), FVector2f(0, 0), FVector2f(1, 0) };
		for (int32 Corner = 0; Corner < 4; ++Corner)
		{
			const FVector3f Position(0.0f, Corners[Corner].X, Corners[Corner].Y);
			Geo.OriginVertices.Add(FDreamUIOriginVertexData(Position));
			FDreamUIMeshVertex Vertex(Position, FColor(200, 100, 50, 255));
			for (int32 Channel = 0; Channel < LEXUI_VERTEX_TEXCOORDINATE_COUNT; ++Channel)
			{
				Vertex.TextureCoordinate[Channel] = FVector2f::ZeroVector;
			}
			Vertex.TextureCoordinate[0] = UVs[Corner];
			// A code the whole quad shares, read back bit for bit (as a text quad's code or a rect block's data row is).
			Vertex.TextureCoordinate[2] = FVector2f(InCode, 0.1f);
			Vertex.TextureCoordinate[3] = FVector2f(InCode * 3.0f + 0.3f, 0.0f);
			Geo.Vertices.Add(Vertex);
		}
		for (const int32 Index : { 0, 3, 2, 0, 1, 3 })
		{
			Geo.Triangles.Add(static_cast<FDreamUIMeshIndex>(Base + Index));
		}
	}

	float TriangleArea(const FDreamUIGeometry& Geo, int32 InFirst)
	{
		const FVector3f& A = Geo.OriginVertices[Geo.Triangles[InFirst]].Position;
		const FVector3f& B = Geo.OriginVertices[Geo.Triangles[InFirst + 1]].Position;
		const FVector3f& C = Geo.OriginVertices[Geo.Triangles[InFirst + 2]].Position;
		return 0.5f * ((B.Y - A.Y) * (C.Z - A.Z) - (C.Y - A.Y) * (B.Z - A.Z));
	}

	/** A game world with a UI manager, a world-space canvas root and frames to run, as the clip mask tests have it. */
	struct FStage
	{
		DreamTests::Lifecycle::FScopedWorld World{ EWorldType::Game };
		UDreamUIManagerWorldSubsystem* Manager = nullptr;
		UDreamWidget* Root = nullptr;

		FStage()
		{
			Manager = UDreamUIManagerWorldSubsystem::GetInstance(World.World);
			Root = MakeWidget(nullptr, TEXT("Root"), FVector2D(800.0, 600.0));
			if (UDreamCanvas* Canvas = Root->AddComponent<UDreamCanvas>())
			{
				Canvas->SetRenderMode(EDreamRenderMode::WorldSpace);
			}
		}
		bool IsUsable() const { return Manager != nullptr && Root != nullptr && Root->GetRenderCanvas() != nullptr; }
		void Frames(int32 InCount = 2) const
		{
			for (int32 Frame = 0; Frame < InCount; ++Frame)
			{
				Manager->Tick(1.0f / 30.0f);
				Manager->SubmitCanvasDrawCall();
			}
		}
		UDreamWidget* MakeWidget(UDreamWidget* InParent, const TCHAR* InName, FVector2D InSize, FVector2D InPosition = FVector2D::ZeroVector) const
		{
			UDreamWidget* Widget = NewObject<UDreamWidget>(World.World, NAME_None, RF_Transient);
			Widget->SetDisplayName(InName);
			Widget->SetWidth(static_cast<float>(InSize.X));
			Widget->SetHeight(static_cast<float>(InSize.Y));
			Widget->OnRegister();
			if (InParent != nullptr)
			{
				Widget->TrySetParent(InParent, false);
				Widget->SetAnchoredPosition(InPosition);
			}
			return Widget;
		}
		UDreamWidget* MakeBlock(const TCHAR* InName, FVector2D InSize, FVector2D InPosition = FVector2D::ZeroVector) const
		{
			UDreamWidget* Widget = MakeWidget(Root, InName, InSize, InPosition);
			if (UDreamTexture* Visual = Widget->CreateNewVisual<UDreamTexture>())
			{
				Visual->SetTexture(FDreamUIUtils::GetDefaultWhiteTexture());
			}
			return Widget;
		}
		UDreamText* MakeText(const TCHAR* InName, const TCHAR* InText, FVector2D InSize, FVector2D InPosition = FVector2D::ZeroVector) const
		{
			UDreamWidget* Widget = MakeWidget(Root, InName, InSize, InPosition);
			UDreamText* Text = Widget->CreateNewVisual<UDreamText>();
			if (Text != nullptr)
			{
				Text->SetFontSize(24.0f);
				Text->SetText(FText::FromString(InText));
			}
			return Text;
		}
	};

	FDreamUIGeometry* GeometryOf(UDreamWidget* InWidget)
	{
		UDreamVisualBatchMesh* Mesh = InWidget != nullptr ? Cast<UDreamVisualBatchMesh>(InWidget->GetVisual()) : nullptr;
		return Mesh != nullptr ? Mesh->GetGeometry() : nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamMeshWarpSubdivisionTest,
	"DreamGUI.MeshWarp.CuttingKeepsEveryVertexItHadAndCutsASharedEdgeAlikeOnBothSides",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMeshWarpSubdivisionTest::RunTest(const FString& Parameters)
{
	using namespace DreamMeshWarpTestLocal;
	// Two quads side by side sharing the line x = 30, as a nine-slice's columns do (each with its own vertices).
	FDreamUIGeometry Geo;
	AddQuad(Geo, FVector2f(0, 0), FVector2f(30, 20), 7.0f);
	AddQuad(Geo, FVector2f(30, 0), FVector2f(70, 20), 9.0f);
	const TArray<FDreamUIOriginVertexData> Before = Geo.OriginVertices;
	const TArray<FDreamUIMeshVertex> BeforeVertices = Geo.Vertices;
	float AreaBefore = 0.0f;
	for (int32 Index = 0; Index < Geo.Triangles.Num(); Index += 3)
	{
		AreaBefore += FMath::Abs(TriangleArea(Geo, Index));
	}
	FDreamMeshDeform::FSubdivision Cut;
	Cut.CellLength = 8.0f;
	TArray<int32> Source;
	const int32 Added = FDreamMeshDeform::Subdivide(Geo, Cut, &Source);
	TestTrue(TEXT("Vertices were added"), Added > 0);
	TestEqual(TEXT("The two arrays stay the same length"), Geo.Vertices.Num(), Geo.OriginVertices.Num());
	TestEqual(TEXT("Every vertex has a source"), Source.Num(), Geo.OriginVertices.Num());
	for (int32 Index = 0; Index < Before.Num(); ++Index)
	{
		TestTrue(*FString::Printf(TEXT("Vertex %d is where it was"), Index), Geo.OriginVertices[Index].Position == Before[Index].Position);
		TestTrue(*FString::Printf(TEXT("Vertex %d keeps its UV"), Index), Geo.Vertices[Index].TextureCoordinate[0] == BeforeVertices[Index].TextureCoordinate[0]);
		TestEqual(*FString::Printf(TEXT("Vertex %d is its own source"), Index), Source[Index], Index);
	}
	float AreaAfter = 0.0f;
	float Longest = 0.0f;
	bool bWindingKept = true;
	for (int32 Index = 0; Index < Geo.Triangles.Num(); Index += 3)
	{
		const float Area = TriangleArea(Geo, Index);
		AreaAfter += FMath::Abs(Area);
		bWindingKept &= Area > 0.0f;
		for (int32 Side = 0; Side < 3; ++Side)
		{
			const FVector3f& A = Geo.OriginVertices[Geo.Triangles[Index + Side]].Position;
			const FVector3f& B = Geo.OriginVertices[Geo.Triangles[Index + (Side + 1) % 3]].Position;
			// Along each axis, which a diagonal spans no further than the cell's sides do.
			Longest = FMath::Max(Longest, FMath::Max(FMath::Abs(A.Y - B.Y), FMath::Abs(A.Z - B.Z)));
		}
	}
	TestTrue(TEXT("The pieces cover the quads exactly"), FMath::IsNearlyEqual(AreaAfter, AreaBefore, 0.01f));
	TestTrue(TEXT("Every piece is wound the way the quad was"), bWindingKept);
	TestTrue(TEXT("No piece is longer than a cell"), Longest <= Cut.CellLength + 0.001f);
	// The new vertices on x = 30 from either quad: the same heights, to the bit, so the shared edge is cut alike.
	TArray<float> LeftEdge, RightEdge;
	for (int32 Index = Before.Num(); Index < Geo.OriginVertices.Num(); ++Index)
	{
		const FVector3f& P = Geo.OriginVertices[Index].Position;
		if (P.Y == 30.0f)
		{
			(Source[Index] < 4 ? LeftEdge : RightEdge).Add(P.Z);
		}
		// Bilinear UVs over its quad, and the shared codes copied untouched.
		const FVector2f UV = Geo.Vertices[Index].TextureCoordinate[0];
		const bool bLeft = Source[Index] < 4;
		const FVector2f Expected((P.Y - (bLeft ? 0.0f : 30.0f)) / (bLeft ? 30.0f : 40.0f), 1.0f - P.Z / 20.0f);
		TestTrue(*FString::Printf(TEXT("Vertex %d's UV follows its place"), Index), UV.Equals(Expected, 1.0e-5f));
		TestTrue(*FString::Printf(TEXT("Vertex %d copies its quad's code"), Index), Geo.Vertices[Index].TextureCoordinate[2].X == (bLeft ? 7.0f : 9.0f)
			&& Geo.Vertices[Index].TextureCoordinate[3].X == (bLeft ? 7.0f : 9.0f) * 3.0f + 0.3f);
	}
	LeftEdge.Sort();
	RightEdge.Sort();
	TestTrue(TEXT("Both quads cut their shared edge"), LeftEdge.Num() > 0);
	TestTrue(TEXT("...at the same points"), LeftEdge == RightEdge);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamMeshWarpTriangleCutTest,
	"DreamGUI.MeshWarp.ATriangleThatIsNoRectangleIsCutIntoATriangularGrid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMeshWarpTriangleCutTest::RunTest(const FString& Parameters)
{
	FDreamUIGeometry Geo;
	const FVector2f Corners[3] = { FVector2f(0, 0), FVector2f(40, 0), FVector2f(0, 40) };
	for (int32 Corner = 0; Corner < 3; ++Corner)
	{
		const FVector3f Position(0.0f, Corners[Corner].X, Corners[Corner].Y);
		Geo.OriginVertices.Add(FDreamUIOriginVertexData(Position));
		FDreamUIMeshVertex Vertex(Position, FColor(Corner * 100, 0, 0, 255));
		for (int32 Channel = 0; Channel < LEXUI_VERTEX_TEXCOORDINATE_COUNT; ++Channel)
		{
			Vertex.TextureCoordinate[Channel] = FVector2f(Corners[Corner].X / 40.0f, Corners[Corner].Y / 40.0f);
		}
		Geo.Vertices.Add(Vertex);
	}
	Geo.Triangles = { 0, 1, 2 };
	FDreamMeshDeform::FSubdivision Cut;
	Cut.CellLength = 10.0f;
	FDreamMeshDeform::Subdivide(Geo, Cut);
	// The longest side is 40 * sqrt 2 = 56.6 units: six pieces to a side, 36 triangles, 28 vertices.
	TestEqual(TEXT("Thirty-six pieces"), Geo.Triangles.Num() / 3, 36);
	TestEqual(TEXT("Twenty-eight vertices"), Geo.OriginVertices.Num(), 28);
	for (int32 Index = 3; Index < Geo.OriginVertices.Num(); ++Index)
	{
		const FVector3f& P = Geo.OriginVertices[Index].Position;
		TestTrue(*FString::Printf(TEXT("Vertex %d's UV is its place over the triangle"), Index),
			Geo.Vertices[Index].TextureCoordinate[0].Equals(FVector2f(P.Y / 40.0f, P.Z / 40.0f), 1.0e-5f));
		TestTrue(*FString::Printf(TEXT("Vertex %d is inside"), Index), P.Y >= -0.001f && P.Z >= -0.001f && P.Y + P.Z <= 40.001f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamMeshWarpBudgetTest,
	"DreamGUI.MeshWarp.TheCellsGrowUntilTheCutMeshFitsItsBudget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMeshWarpBudgetTest::RunTest(const FString& Parameters)
{
	using namespace DreamMeshWarpTestLocal;
	FDreamUIGeometry Generous;
	AddQuad(Generous, FVector2f(0, 0), FVector2f(400, 400));
	FDreamUIGeometry Tight = Generous;
	FDreamMeshDeform::FSubdivision Cut;
	Cut.CellLength = 4.0f;
	Cut.MaxVertices = 20000;
	FDreamMeshDeform::Subdivide(Generous, Cut);
	TestEqual(TEXT("At cells of 4 a 400-square quad is 101 by 101 vertices"), Generous.OriginVertices.Num(), 101 * 101);
	Cut.MaxVertices = 500;
	FDreamMeshDeform::Subdivide(Tight, Cut);
	TestTrue(TEXT("Under a budget of 500 it stays under 500"), Tight.OriginVertices.Num() <= 500);
	TestTrue(TEXT("...and is still cut"), Tight.OriginVertices.Num() > 4);
	FDreamUIGeometry Hopeless;
	AddQuad(Hopeless, FVector2f(0, 0), FVector2f(10, 10));
	Cut.MaxVertices = 4;
	TestEqual(TEXT("A budget nothing fits leaves the mesh as it was"), FDreamMeshDeform::Subdivide(Hopeless, Cut), 0);
	TestEqual(TEXT("...four vertices"), Hopeless.OriginVertices.Num(), 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamMeshWarpStylesTest,
	"DreamGUI.MeshWarp.EveryStyleRestsAtNoBendAndBendsTheSameEveryTime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMeshWarpStylesTest::RunTest(const FString& Parameters)
{
	const FVector2f Centre(10.0f, -5.0f);
	const FVector2f Half(80.0f, 20.0f);
	const UEnum* Styles = StaticEnum<EDreamWarpStyle>();
	TestEqual(TEXT("Fifteen styles and None"), Styles->NumEnums() - 1, 16);
	for (int32 StyleIndex = 1; StyleIndex <= 15; ++StyleIndex)
	{
		const EDreamWarpStyle Style = static_cast<EDreamWarpStyle>(StyleIndex);
		const FString Name = Styles->GetNameStringByValue(StyleIndex);
		FDreamMeshDeform::FStyle Warp;
		Warp.Style = Style;
		Warp.Bend = 0.0f;
		TestTrue(*FString::Printf(TEXT("%s at no bend is at rest"), *Name), Warp.IsIdentity());
		uint32 First = 0, Second = 0, Back = 0;
		bool bMoves = false;
		for (int32 Pass = 0; Pass < 3; ++Pass)
		{
			Warp.Bend = Pass == 2 ? -0.6f : 0.6f;
			Warp.Phase = 0.25f;
			uint32 Hash = 0;
			for (int32 Y = 0; Y <= 8; ++Y)
			{
				for (int32 X = 0; X <= 8; ++X)
				{
					const FVector2f P = Centre + FVector2f((X / 4.0f - 1.0f) * Half.X, (Y / 4.0f - 1.0f) * Half.Y);
					const FVector2f Out = FDreamMeshDeform::WarpPoint(Warp, P, Centre, Half);
					TestTrue(*FString::Printf(TEXT("%s gives a number"), *Name), FMath::IsFinite(Out.X) && FMath::IsFinite(Out.Y));
					bMoves |= !Out.Equals(P, 0.01f);
					Hash = HashCombine(Hash, HashCombine(GetTypeHash(Out.X), GetTypeHash(Out.Y)));
				}
			}
			(Pass == 0 ? First : (Pass == 1 ? Second : Back)) = Hash;
		}
		TestEqual(*FString::Printf(TEXT("%s bends the same way twice, to the bit"), *Name), First, Second);
		TestTrue(*FString::Printf(TEXT("%s moves something at a bend of 60"), *Name), bMoves);
		TestNotEqual(*FString::Printf(TEXT("%s bent back is bent differently"), *Name), Back, First);
	}
	// What a few styles must do, by their names.
	auto At = [&](EDreamWarpStyle InStyle, float InBend, FVector2f InNormalized)
	{
		FDreamMeshDeform::FStyle Warp;
		Warp.Style = InStyle;
		Warp.Bend = InBend;
		const FVector2f Out = FDreamMeshDeform::WarpPoint(Warp, Centre + InNormalized * Half, Centre, Half);
		return (Out - Centre) / Half;
	};
	TestTrue(TEXT("Arc keeps the middle column where it was"), At(EDreamWarpStyle::Arc, 0.5f, FVector2f(0.0f, 1.0f)).Equals(FVector2f(0.0f, 1.0f), 1.0e-5f));
	TestTrue(TEXT("...and drops its ends when bent up"), At(EDreamWarpStyle::Arc, 0.5f, FVector2f(1.0f, 0.0f)).Y < -0.5f);
	TestTrue(TEXT("Arch lifts the middle"), At(EDreamWarpStyle::Arch, 0.5f, FVector2f(0.0f, 0.0f)).Y > 0.5f);
	TestTrue(TEXT("...and leaves the ends"), At(EDreamWarpStyle::Arch, 0.5f, FVector2f(1.0f, 1.0f)).Equals(FVector2f(1.0f, 1.0f), 1.0e-5f));
	TestTrue(TEXT("Arc Lower keeps the top"), At(EDreamWarpStyle::ArcLower, 0.5f, FVector2f(0.0f, 1.0f)).Equals(FVector2f(0.0f, 1.0f), 1.0e-5f));
	TestTrue(TEXT("...and lowers the bottom's middle"), At(EDreamWarpStyle::ArcLower, 0.5f, FVector2f(0.0f, -1.0f)).Y < -1.5f);
	TestTrue(TEXT("Arc Upper keeps the bottom"), At(EDreamWarpStyle::ArcUpper, 0.5f, FVector2f(0.0f, -1.0f)).Equals(FVector2f(0.0f, -1.0f), 1.0e-5f));
	TestTrue(TEXT("Bulge doubles the middle at full bend"), At(EDreamWarpStyle::Bulge, 1.0f, FVector2f(0.0f, 1.0f)).Equals(FVector2f(0.0f, 2.0f), 1.0e-5f));
	TestTrue(TEXT("Twist holds the centre"), At(EDreamWarpStyle::Twist, 1.0f, FVector2f::ZeroVector).Equals(FVector2f::ZeroVector, 1.0e-5f));
	TestTrue(TEXT("...and the ends"), At(EDreamWarpStyle::Twist, 1.0f, FVector2f(1.0f, 0.0f)).Equals(FVector2f(1.0f, 0.0f), 1.0e-5f));
	TestTrue(TEXT("...and turns the halves of the length opposite ways, an S"), At(EDreamWarpStyle::Twist, 0.5f, FVector2f(0.5f, 0.0f)).Y > 0.05f && At(EDreamWarpStyle::Twist, 0.5f, FVector2f(-0.5f, 0.0f)).Y < -0.05f);
	TestTrue(TEXT("Fisheye keeps the centre"), At(EDreamWarpStyle::Fisheye, 1.0f, FVector2f::ZeroVector).Equals(FVector2f::ZeroVector, 1.0e-5f));
	TestTrue(TEXT("...and keeps the corners, outside its lens"), At(EDreamWarpStyle::Fisheye, 1.0f, FVector2f(1.0f, 1.0f)).Equals(FVector2f(1.0f, 1.0f), 1.0e-5f));
	TestTrue(TEXT("Inflate keeps the corners"), At(EDreamWarpStyle::Inflate, 1.0f, FVector2f(1.0f, -1.0f)).Equals(FVector2f(1.0f, -1.0f), 1.0e-5f));
	TestTrue(TEXT("...and pushes the middle of a side out"), At(EDreamWarpStyle::Inflate, 1.0f, FVector2f(0.0f, 1.0f)).Y > 1.25f);
	TestTrue(TEXT("Rise lifts the right end and lowers the left"), At(EDreamWarpStyle::Rise, 0.5f, FVector2f(1.0f, 0.0f)).Y > 0.5f && At(EDreamWarpStyle::Rise, 0.5f, FVector2f(-1.0f, 0.0f)).Y < -0.5f);
	// A wave's phase: half a turn swings it to the other side.
	FDreamMeshDeform::FStyle Flag;
	Flag.Style = EDreamWarpStyle::Flag;
	Flag.Bend = 0.5f;
	const FVector2f AtQuarter = Centre + FVector2f(0.5f * Half.X, 0.0f);
	const float Up = FDreamMeshDeform::WarpPoint(Flag, AtQuarter, Centre, Half).Y - Centre.Y;
	Flag.Phase = 0.5f;
	const float Down = FDreamMeshDeform::WarpPoint(Flag, AtQuarter, Centre, Half).Y - Centre.Y;
	TestTrue(TEXT("Flag's phase swings it"), FMath::IsNearlyEqual(Up, -Down, 1.0e-3f) && Up > 1.0f);
	// Vertical is horizontal with the box turned: the same bend, a quarter turn on.
	FDreamMeshDeform::FStyle Across;
	Across.Style = EDreamWarpStyle::Bulge;
	Across.Bend = 0.5f;
	FDreamMeshDeform::FStyle Upright = Across;
	Upright.bVertical = true;
	const FVector2f Q(30.0f, 12.0f);
	const FVector2f Turned = FDreamMeshDeform::WarpPoint(Across, FVector2f(Q.Y, -Q.X), FVector2f::ZeroVector, FVector2f(Half.Y, Half.X));
	TestTrue(TEXT("Vertical is horizontal turned a quarter"), FDreamMeshDeform::WarpPoint(Upright, Q, FVector2f::ZeroVector, Half).Equals(FVector2f(-Turned.Y, Turned.X), 1.0e-4f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamMeshWarpDistortionTest,
	"DreamGUI.MeshWarp.ADistortionMakesOneEndTheLargerAndKeepsTheEndsWhereTheyWere",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMeshWarpDistortionTest::RunTest(const FString& Parameters)
{
	const FVector2f Half(50.0f, 20.0f);
	FDreamMeshDeform::FStyle Warp;
	Warp.HorizontalDistortion = 0.5f;
	TestFalse(TEXT("A distortion alone is not at rest"), Warp.IsIdentity());
	const FVector2f RightTop = FDreamMeshDeform::WarpPoint(Warp, FVector2f(50, 20), FVector2f::ZeroVector, Half);
	const FVector2f LeftTop = FDreamMeshDeform::WarpPoint(Warp, FVector2f(-50, 20), FVector2f::ZeroVector, Half);
	TestTrue(TEXT("The right end is taller"), RightTop.Y > 20.0f && LeftTop.Y < 20.0f);
	TestTrue(TEXT("The ends stay at the ends"), FMath::IsNearlyEqual(RightTop.X, 50.0f, 1.0e-3f) && FMath::IsNearlyEqual(LeftTop.X, -50.0f, 1.0e-3f));
	TestTrue(TEXT("The middle moves towards the smaller end"), FDreamMeshDeform::WarpPoint(Warp, FVector2f(0, 0), FVector2f::ZeroVector, Half).X < 0.0f);
	Warp.HorizontalDistortion = 0.0f;
	Warp.VerticalDistortion = 0.5f;
	TestTrue(TEXT("A vertical distortion widens the top"),
		FDreamMeshDeform::WarpPoint(Warp, FVector2f(50, 20), FVector2f::ZeroVector, Half).X > 50.0f
		&& FDreamMeshDeform::WarpPoint(Warp, FVector2f(50, -20), FVector2f::ZeroVector, Half).X < 50.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamMeshWarpLatticeTest,
	"DreamGUI.MeshWarp.ALatticeAtRestMovesNothingAndAMovedPointPullsByItsBernsteinWeight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMeshWarpLatticeTest::RunTest(const FString& Parameters)
{
	for (int32 Degree = 1; Degree <= 4; ++Degree)
	{
		for (const float T : { 0.0f, 0.3f, 0.5f, 1.0f })
		{
			float Sum = 0.0f;
			for (int32 Index = 0; Index <= Degree; ++Index)
			{
				Sum += FDreamMeshDeform::Bernstein(Degree, Index, T);
			}
			TestTrue(*FString::Printf(TEXT("The weights of degree %d sum to 1 at %.1f"), Degree, T), FMath::IsNearlyEqual(Sum, 1.0f, 1.0e-6f));
		}
	}
	const FVector2f Min(-40.0f, -10.0f), Max(60.0f, 30.0f);
	for (int32 Columns = 2; Columns <= 5; ++Columns)
	{
		for (int32 Rows = 2; Rows <= 5; ++Rows)
		{
			TArray<FVector2D> Rest;
			FDreamMeshDeform::MakeRestLattice(Columns, Rows, Rest);
			bool bStill = true;
			for (const FVector2f P : { FVector2f(-40, -10), FVector2f(0, 0), FVector2f(13.5f, 27.0f), FVector2f(60, 30), FVector2f(80, -20) })
			{
				bStill &= FDreamMeshDeform::EvaluateLattice(Rest, Columns, Rows, P, Min, Max).Equals(P, 1.0e-3f);
			}
			TestTrue(*FString::Printf(TEXT("A %d by %d lattice at rest moves nothing, outside the box either"), Columns, Rows), bStill);
		}
	}
	// One point of a 4 by 4 moved: a point moves by that point's two weights times the move.
	TArray<FVector2D> Lattice;
	FDreamMeshDeform::MakeRestLattice(4, 4, Lattice);
	const FVector2D Moved(0.1, -0.2);
	Lattice[1 * 4 + 2] += Moved; // row 1, column 2
	const FVector2f P(10.0f, 20.0f);
	const float U = (P.X - Min.X) / (Max.X - Min.X);
	const float V = (Max.Y - P.Y) / (Max.Y - Min.Y);
	const float Weight = FDreamMeshDeform::Bernstein(3, 2, U) * FDreamMeshDeform::Bernstein(3, 1, V);
	const FVector2f Expected = P + FVector2f(Moved.X * (Max.X - Min.X), -Moved.Y * (Max.Y - Min.Y)) * Weight;
	TestTrue(TEXT("The move is weighted by Bernstein"), FDreamMeshDeform::EvaluateLattice(Lattice, 4, 4, P, Min, Max).Equals(Expected, 1.0e-3f));
	TestTrue(TEXT("A lattice of the wrong size is ignored"), FDreamMeshDeform::EvaluateLattice(Lattice, 3, 3, P, Min, Max).Equals(P, 0.0f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamMeshWarpModifierTest,
	"DreamGUI.MeshWarp.TheWarpCutsAndBendsAMeshAndLeavesItAloneAtRest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMeshWarpModifierTest::RunTest(const FString& Parameters)
{
	using namespace DreamMeshWarpTestLocal;
	UDreamMeshModifierWarp* Warp = NewObject<UDreamMeshModifierWarp>(GetTransientPackage());
	Warp->SetBend(0.0f);
	bool bTriangles = true, bPositions = true, bUVs = true, bColours = true;
	Warp->ModifierWillChangeVertexData(bTriangles, bPositions, bUVs, bColours);
	TestFalse(TEXT("At rest it asks for nothing to be made afresh"), bTriangles || bPositions || bUVs || bColours);
	FDreamUIGeometry Geo;
	AddQuad(Geo, FVector2f(-50, -20), FVector2f(50, 20));
	const TArray<FDreamUIOriginVertexData> Flat = Geo.OriginVertices;
	Warp->ModifyUIGeometry(Geo, true, true, true, true);
	TestEqual(TEXT("At rest it does not cut"), Geo.OriginVertices.Num(), 4);
	TestTrue(TEXT("A warp runs in the deform stage"), Warp->GetStage() == EDreamMeshModifierStage::Deform);

	Warp->SetStyle(EDreamWarpStyle::Arch);
	Warp->SetBend(50.0f);
	Warp->ModifierWillChangeVertexData(bTriangles, bPositions, bUVs, bColours);
	TestTrue(TEXT("Bent, it asks for everything"), bTriangles && bPositions && bUVs && bColours);
	Warp->ModifyUIGeometry(Geo, true, true, true, true);
	TestTrue(TEXT("Bent, it cuts"), Geo.OriginVertices.Num() > 4);
	TestTrue(TEXT("The box it bent is the mesh's"), Warp->GetLastWarpBox().Min.Equals(FVector2f(-50, -20)) && Warp->GetLastWarpBox().Max.Equals(FVector2f(50, 20)));
	const FDreamMeshDeform::FStyle Style = Warp->MakeStyle();
	for (int32 Index = 0; Index < 4; ++Index)
	{
		const FVector2f From(Flat[Index].Position.Y, Flat[Index].Position.Z);
		const FVector2f To(Geo.OriginVertices[Index].Position.Y, Geo.OriginVertices[Index].Position.Z);
		TestTrue(*FString::Printf(TEXT("Corner %d went where the style sends it, at its own index"), Index),
			To.Equals(FDreamMeshDeform::WarpPoint(Style, From, FVector2f::ZeroVector, FVector2f(50, 20)), 1.0e-4f));
	}
	// The middle of the top edge is a new vertex, lifted by the arch.
	bool bLifted = false;
	for (const FDreamUIOriginVertexData& Vertex : Geo.OriginVertices)
	{
		bLifted |= FMath::Abs(Vertex.Position.Y) < 4.0f && Vertex.Position.Z > 30.0f;
	}
	TestTrue(TEXT("The middle of the top is lifted"), bLifted);
	Warp->SetMode(EDreamWarpMode::Lattice);
	TestTrue(TEXT("An unmoved lattice is at rest"), Warp->IsAtRest());
	Warp->SetLatticePoint(1, 0, FVector2D(0.4, -0.3));
	TestFalse(TEXT("A moved point is not"), Warp->IsAtRest());
	Warp->ResetLattice();
	TestTrue(TEXT("Reset puts it back"), Warp->IsAtRest());
	Warp->SetLatticeSize(5, 3);
	TestEqual(TEXT("A new size lays out its points"), Warp->GetLatticePoints().Num(), 15);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamMeshWarpStageOrderTest,
	"DreamGUI.MeshWarp.AShadowCopiesTheBentMeshWhicheverComponentCameFirst",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMeshWarpStageOrderTest::RunTest(const FString& Parameters)
{
	using namespace DreamMeshWarpTestLocal;
	TestTrue(TEXT("A text animation works on characters"), GetDefault<UDreamMeshModifierTextAnimation>()->GetStage() == EDreamMeshModifierStage::Characters);
	TestTrue(TEXT("Text on a path lays out"), GetDefault<UDreamMeshModifierTextOnPath>()->GetStage() == EDreamMeshModifierStage::Layout);
	TestTrue(TEXT("A jelly deforms"), GetDefault<UDreamMeshModifierJelly>()->GetStage() == EDreamMeshModifierStage::Deform);
	TestTrue(TEXT("A shadow copies"), GetDefault<UDreamMeshModifierShadow>()->GetStage() == EDreamMeshModifierStage::Copies);
	TestTrue(TEXT("An outline copies"), GetDefault<UDreamMeshModifierOutline>()->GetStage() == EDreamMeshModifierStage::Copies);

	FStage Stage;
	if (!TestTrue(TEXT("The stage is usable"), Stage.IsUsable()))
	{
		return false;
	}
	for (const bool bShadowFirst : { true, false })
	{
		UDreamWidget* Block = Stage.MakeBlock(bShadowFirst ? TEXT("ShadowFirst") : TEXT("WarpFirst"), FVector2D(100.0, 40.0));
		UDreamMeshModifierShadow* Shadow = nullptr;
		UDreamMeshModifierWarp* Warp = nullptr;
		if (bShadowFirst)
		{
			Shadow = Block->AddComponent<UDreamMeshModifierShadow>();
			Warp = Block->AddComponent<UDreamMeshModifierWarp>();
		}
		else
		{
			Warp = Block->AddComponent<UDreamMeshModifierWarp>();
			Shadow = Block->AddComponent<UDreamMeshModifierShadow>();
		}
		Warp->SetStyle(EDreamWarpStyle::Arch);
		Warp->SetBend(60.0f);
		Shadow->SetShadowOffset(FVector3f(0.0f, 5.0f, -5.0f));
		Stage.Frames(2);
		const FDreamUIGeometry* Geo = GeometryOf(Block);
		if (!TestNotNull(TEXT("The block drew"), Geo))
		{
			continue;
		}
		// The shadow puts its copy first and the original after it: both halves are the bent mesh, one moved by the offset.
		const int32 Half = Geo->OriginVertices.Num() / 2;
		TestTrue(TEXT("The shadow copied a cut mesh"), Half > 4);
		bool bFollows = true;
		for (int32 Index = 0; Index < Half; ++Index)
		{
			const FVector3f Offset = Geo->OriginVertices[Index].Position - Geo->OriginVertices[Half + Index].Position;
			bFollows &= Offset.Equals(FVector3f(0.0f, 5.0f, -5.0f), 1.0e-3f) || Offset.Equals(FVector3f(0.0f, -5.0f, 5.0f), 1.0e-3f);
		}
		TestTrue(*FString::Printf(TEXT("The shadow follows the bent block (%s)"), bShadowFirst ? TEXT("shadow added first") : TEXT("warp added first")), bFollows);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamMeshWarpHitTestTest,
	"DreamGUI.MeshWarp.AVisualHitByItsMeshIsHitWhereTheBentTrianglesAre",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMeshWarpHitTestTest::RunTest(const FString& Parameters)
{
	using namespace DreamMeshWarpTestLocal;
	FStage Stage;
	if (!TestTrue(TEXT("The stage is usable"), Stage.IsUsable()))
	{
		return false;
	}
	UDreamWidget* Block = Stage.MakeBlock(TEXT("Bent"), FVector2D(200.0, 40.0));
	UDreamVisual* Visual = Block->GetVisual();
	Visual->SetRaycastType(EDreamVisualRaycastType::Mesh);
	UDreamMeshModifierWarp* Warp = Block->AddComponent<UDreamMeshModifierWarp>();
	Warp->SetStyle(EDreamWarpStyle::Arch);
	Warp->SetBend(80.0f);
	Stage.Frames(2);
	auto Hits = [&](FVector2D InLocal)
	{
		const FVector Point = Block->GetWorldTransform().TransformPosition(FVector(0.0, InLocal.X, InLocal.Y));
		const FVector Normal = Block->GetWorldTransform().GetUnitAxis(EAxis::X);
		FDreamUIHitResult Hit;
		return Visual->LineTraceUI(Hit, Point - Normal * 10.0, Point + Normal * 10.0);
	};
	// The arch lifts the middle by 0.5 * 0.8 * 100 = 40 units: the middle of the block's rect is now below it.
	const FVector2D Centre = Block->GetLocalSpaceCenter();
	TestTrue(TEXT("Above the rect, where the middle went, is hit"), Hits(Centre + FVector2D(0.0, 45.0)));
	TestFalse(TEXT("The middle of the rect, left behind, is not"), Hits(Centre + FVector2D(0.0, -10.0)));
	TestTrue(TEXT("The ends, which stay, are"), Hits(Centre + FVector2D(-98.0, 0.0)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamMeshWarpTextTest,
	"DreamGUI.MeshWarp.AWarpedTextKeepsItsCharactersVertexRangesOnTheirGlyphs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMeshWarpTextTest::RunTest(const FString& Parameters)
{
	using namespace DreamMeshWarpTestLocal;
	FStage Stage;
	if (!TestTrue(TEXT("The stage is usable"), Stage.IsUsable()))
	{
		return false;
	}
	UDreamText* Text = Stage.MakeText(TEXT("Warped"), TEXT("WARP"), FVector2D(200.0, 40.0));
	if (!TestNotNull(TEXT("A text"), Text))
	{
		return false;
	}
	UDreamMeshModifierWarp* Warp = Text->GetWidget()->AddComponent<UDreamMeshModifierWarp>();
	Warp->SetStyle(EDreamWarpStyle::Flag);
	Warp->SetBend(0.0f);
	Stage.Frames(3);
	const FDreamUIGeometry* Geo = GeometryOf(Text->GetWidget());
	if (!TestNotNull(TEXT("The text drew"), Geo) || !TestTrue(TEXT("...glyphs"), Geo->OriginVertices.Num() >= 16))
	{
		return false;
	}
	const TArray<FDreamUIOriginVertexData> Flat = Geo->OriginVertices;
	const FBox2f Box = FDreamMeshDeform::MeasureBounds(*Geo);
	Warp->SetBend(70.0f);
	Stage.Frames(2);
	Geo = GeometryOf(Text->GetWidget());
	TestTrue(TEXT("Bent, the glyphs are cut"), Geo->OriginVertices.Num() > Flat.Num());
	const FDreamMeshDeform::FStyle Style = Warp->MakeStyle();
	for (const FDreamUITextCharProperty& Char : Text->GetCharPropertyArray())
	{
		for (int32 Vertex = Char.StartVertIndex; Vertex < Char.StartVertIndex + Char.VertCount; ++Vertex)
		{
			const FVector2f From(Flat[Vertex].Position.Y, Flat[Vertex].Position.Z);
			const FVector2f To(Geo->OriginVertices[Vertex].Position.Y, Geo->OriginVertices[Vertex].Position.Z);
			TestTrue(*FString::Printf(TEXT("Character %d's vertex %d is its glyph's corner, bent"), Char.CharIndex, Vertex),
				To.Equals(FDreamMeshDeform::WarpPoint(Style, From, Box.GetCenter(), Box.GetExtent()), 0.01f));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamJellySpringsTest,
	"DreamGUI.MeshWarp.JellySpringsStepTheSameWhateverTheFramesAndSettle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamJellySpringsTest::RunTest(const FString& Parameters)
{
	auto Make = []()
	{
		FDreamJellySprings Springs;
		Springs.Reset(3, 3);
		Springs.Frequency = 4.0f;
		Springs.DampingRatio = 0.3f;
		Springs.MaxOffset = 1000.0f;
		Springs.SetAnchor(EDreamJellyAnchor::Centre);
		return Springs;
	};
	FDreamJellySprings Sixtieths = Make();
	FDreamJellySprings Twentieths = Make();
	// A shove to the right for a tenth of a second, then nothing: the same in frames of 1/60 and of 1/120.
	for (int32 Frame = 0; Frame < 6; ++Frame)
	{
		Sixtieths.Advance(1.0f / 60.0f, FVector2f(3000.0f, 0.0f));
	}
	for (int32 Frame = 0; Frame < 12; ++Frame)
	{
		Twentieths.Advance(1.0f / 120.0f, FVector2f(3000.0f, 0.0f));
	}
	bool bSame = Sixtieths.Offset == Twentieths.Offset && Sixtieths.Velocity == Twentieths.Velocity;
	TestTrue(TEXT("Frames of 1/60 and 1/120 step alike, to the bit"), bSame);
	TestTrue(TEXT("The anchor in the middle does not move"), Sixtieths.Offset[4].IsNearlyZero(1.0e-6f));
	TestTrue(TEXT("A corner lags behind the shove: to the left"), Sixtieths.Offset[0].X < -1.0f);
	TestTrue(TEXT("...and only across"), FMath::IsNearlyZero(Sixtieths.Offset[0].Y, 1.0e-4f));
	TestTrue(TEXT("A middle of a side lags less than a corner"), FMath::Abs(Sixtieths.Offset[1].X) < FMath::Abs(Sixtieths.Offset[0].X));
	// Left alone it swings past its rest and comes back to it.
	bool bOvershot = false;
	for (int32 Frame = 0; Frame < 60 * 6; ++Frame)
	{
		Sixtieths.Advance(1.0f / 60.0f, FVector2f::ZeroVector);
		bOvershot |= Sixtieths.Offset[0].X > 0.1f;
	}
	TestTrue(TEXT("Under-damped, it overshoots"), bOvershot);
	TestTrue(TEXT("After six seconds it has settled"), Sixtieths.IsAtRest(0.01f));
	// Critically damped it never crosses.
	FDreamJellySprings Critical = Make();
	Critical.DampingRatio = 1.0f;
	Critical.Velocity[0] = FVector2f(-200.0f, 0.0f);
	bool bCrossed = false;
	for (int32 Frame = 0; Frame < 120; ++Frame)
	{
		Critical.Advance(1.0f / 60.0f, FVector2f::ZeroVector);
		bCrossed |= Critical.Offset[0].X > 0.01f;
	}
	TestFalse(TEXT("Critically damped, it does not overshoot"), bCrossed);
	// The stretch limit holds.
	FDreamJellySprings Limited = Make();
	Limited.MaxOffset = 5.0f;
	Limited.Advance(0.1f, FVector2f(1.0e6f, 0.0f));
	TestTrue(TEXT("No point stretches past the limit"), Limited.Offset[0].Size() <= 5.0f + 1.0e-4f);
	// A frame longer than the steps allowed drops the rest rather than stalling.
	FDreamJellySprings Long = Make();
	TestEqual(TEXT("A long frame takes at most the allowed steps"), Long.Advance(10.0f, FVector2f::ZeroVector), FDreamJellySprings::MaxStepsPerAdvance);
	TestEqual(TEXT("...and carries nothing over"), Long.Remainder, 0.0f);
	// The anchors.
	FDreamJellySprings Bottom = Make();
	Bottom.SetAnchor(EDreamJellyAnchor::Bottom);
	TestTrue(TEXT("Anchored at the bottom: the bottom row holds, the top row feels it all"), Bottom.Weight[6] == 0.0f && Bottom.Weight[0] == 1.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamJellyPressTest,
	"DreamGUI.MeshWarp.APressSquashesAJellyAndLettingGoBouncesItBackToRest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamJellyPressTest::RunTest(const FString& Parameters)
{
	using namespace DreamMeshWarpTestLocal;
	FStage Stage;
	if (!TestTrue(TEXT("The stage is usable"), Stage.IsUsable()))
	{
		return false;
	}
	UDreamWidget* Block = Stage.MakeBlock(TEXT("Jelly"), FVector2D(100.0, 100.0));
	UDreamMeshModifierJelly* Jelly = Block->AddComponent<UDreamMeshModifierJelly>();
	Stage.Frames(2);
	TestTrue(TEXT("Untouched it is at rest"), Jelly->IsAtRest());
	TestEqual(TEXT("...and the block is not cut"), GeometryOf(Block)->OriginVertices.Num(), 4);
	const FVector2D Centre = Block->GetLocalSpaceCenter();
	Jelly->SetPressed(true, Centre);
	for (int32 Frame = 0; Frame < 120; ++Frame)
	{
		Jelly->Simulate(1.0f / 60.0f, FVector2f::ZeroVector);
	}
	const FDreamJellySprings& Springs = Jelly->GetSprings();
	TestTrue(TEXT("Held, a corner has drawn in towards the press"), Springs.Offset[0].X > 1.0f && Springs.Offset[0].Y < -1.0f);
	Stage.Frames(1);
	TestTrue(TEXT("...and the block is cut and bent"), GeometryOf(Block)->OriginVertices.Num() > 4);
	Jelly->SetPressed(false, FVector2D::ZeroVector);
	bool bBounced = false;
	for (int32 Frame = 0; Frame < 60 * 6; ++Frame)
	{
		Jelly->Simulate(1.0f / 60.0f, FVector2f::ZeroVector);
		bBounced |= Springs.Offset[0].X < -0.1f;
	}
	TestTrue(TEXT("Let go, it bounces past its rest"), bBounced);
	TestTrue(TEXT("...and comes to rest"), Jelly->IsAtRest());
	Stage.Frames(2);
	TestEqual(TEXT("At rest the block is one quad again"), GeometryOf(Block)->OriginVertices.Num(), 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPathCurveTest,
	"DreamGUI.TextOnPath.APathIsMeasuredAlongItsLengthEndsAndLoopsIncluded",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPathCurveTest::RunTest(const FString& Parameters)
{
	FDreamPathCurve Circle;
	Circle.BuildCircle(FVector2f(10.0f, 20.0f), 50.0f, 0.0f, 360.0f, true);
	TestTrue(TEXT("A whole circle is closed"), Circle.bClosed);
	TestTrue(TEXT("...and as long as its circumference"), FMath::IsNearlyEqual(Circle.GetLength(), 2.0f * PI * 50.0f, 1.0e-3f));
	FVector2f Point, Direction;
	Circle.Evaluate(0.0f, Point, Direction);
	TestTrue(TEXT("It starts at twelve o'clock"), Point.Equals(FVector2f(10.0f, 70.0f), 1.0e-3f));
	TestTrue(TEXT("...going right, clockwise"), Direction.Equals(FVector2f(1.0f, 0.0f), 1.0e-4f));
	Circle.Evaluate(Circle.GetLength() * 0.25f, Point, Direction);
	TestTrue(TEXT("A quarter round is three o'clock"), Point.Equals(FVector2f(60.0f, 20.0f), 1.0e-3f));
	Circle.Evaluate(Circle.GetLength() * 1.25f, Point, Direction);
	TestTrue(TEXT("Past the end of a closed path wraps"), Point.Equals(FVector2f(60.0f, 20.0f), 1.0e-2f));
	FDreamPathCurve Arc;
	Arc.BuildCircle(FVector2f::ZeroVector, 100.0f, -90.0f, 180.0f, true);
	TestFalse(TEXT("Half a circle is open"), Arc.bClosed);
	Arc.Evaluate(Arc.GetLength() * 0.5f, Point, Direction);
	TestTrue(TEXT("Its middle is the top"), Point.Equals(FVector2f(0.0f, 100.0f), 1.0e-3f));
	Arc.Evaluate(Arc.GetLength() + 10.0f, Point, Direction);
	TestTrue(TEXT("Past its end it goes straight on"), Point.Equals(FVector2f(100.0f, -10.0f), 1.0e-2f));

	FDreamPathCurve Line;
	const TArray<FVector2D> Corner = { FVector2D(0, 0), FVector2D(30, 0), FVector2D(30, 40) };
	Line.Build(Corner, EDreamPathCurve::Linear, 1, false);
	TestEqual(TEXT("A straight path is as long as its segments"), Line.GetLength(), 70.0f);
	Line.Evaluate(15.0f, Point, Direction);
	TestTrue(TEXT("Along the first"), Point.Equals(FVector2f(15.0f, 0.0f), 1.0e-4f) && Direction.Equals(FVector2f(1.0f, 0.0f), 1.0e-4f));
	Line.Evaluate(30.0f, Point, Direction);
	TestTrue(TEXT("At the corner the direction is halfway round"), Direction.Equals(FVector2f(1.0f, 1.0f).GetSafeNormal(), 1.0e-3f));
	Line.Build(Corner, EDreamPathCurve::Linear, 1, true);
	TestEqual(TEXT("Closed, it adds the way back"), Line.GetLength(), 120.0f);
	FDreamPathCurve Smooth;
	const TArray<FVector2D> Wave = { FVector2D(0, 0), FVector2D(50, 30), FVector2D(100, 0), FVector2D(150, 30) };
	Smooth.Build(Wave, EDreamPathCurve::Smooth, 8, false);
	TestEqual(TEXT("Eight pieces a span, and the end"), Smooth.Points.Num(), 3 * 8 + 1);
	for (int32 Index = 0; Index < Wave.Num(); ++Index)
	{
		TestTrue(*FString::Printf(TEXT("The smooth path passes through point %d"), Index), Smooth.Points[Index * 8].Equals(FVector2f(Wave[Index]), 1.0e-3f));
	}
	TArray<FVector2D> Samples;
	Smooth.Sample(5, Samples);
	TestEqual(TEXT("Five samples, both ends"), Samples.Num(), 5);
	TestTrue(TEXT("...the last at its end"), Samples.Last().Equals(FVector2D(150, 30), 1.0e-2));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextOnPathCircleTest,
	"DreamGUI.TextOnPath.GlyphsSitAlongTheCircleWholeInOrderAndFlipInside",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextOnPathCircleTest::RunTest(const FString& Parameters)
{
	using namespace DreamMeshWarpTestLocal;
	FStage Stage;
	if (!TestTrue(TEXT("The stage is usable"), Stage.IsUsable()))
	{
		return false;
	}
	UDreamText* Text = Stage.MakeText(TEXT("Round"), TEXT("ABCDE"), FVector2D(300.0, 40.0));
	if (!TestNotNull(TEXT("A text"), Text))
	{
		return false;
	}
	Stage.Frames(3);
	const TArray<FDreamUIOriginVertexData> Flat = GeometryOf(Text->GetWidget())->OriginVertices;
	UDreamMeshModifierTextOnPath* OnPath = Text->GetWidget()->AddComponent<UDreamMeshModifierTextOnPath>();
	const float Radius = 120.0f;
	OnPath->SetCircle(Radius, FVector2D(0.0, -Radius), -90.0f, 180.0f, true);
	OnPath->SetAnchor(EDreamTextPathAnchor::Bottom);
	Stage.Frames(2);
	const FDreamUIGeometry* Geo = GeometryOf(Text->GetWidget());
	if (!TestEqual(TEXT("Whole glyphs are not cut"), Geo->OriginVertices.Num(), Flat.Num()))
	{
		return false;
	}
	const FVector2f Centre = FVector2f(Text->GetWidget()->GetLocalSpaceCenter()) + FVector2f(0.0f, -Radius);
	TArray<float> Angles;
	for (const FDreamUITextCharProperty& Char : Text->GetCharPropertyArray())
	{
		if (Char.VertCount < 4)
		{
			continue;
		}
		const int32 First = Char.StartVertIndex;
		// A glyph is moved whole: its corners keep their distances from each other.
		const float Before = FVector3f::Distance(Flat[First].Position, Flat[First + 3].Position);
		const float After = FVector3f::Distance(Geo->OriginVertices[First].Position, Geo->OriginVertices[First + 3].Position);
		TestTrue(*FString::Printf(TEXT("Glyph %d keeps its shape"), Char.CharIndex), FMath::IsNearlyEqual(Before, After, 0.01f));
		FVector2f Middle = FVector2f::ZeroVector;
		float Lowest = MAX_flt;
		for (int32 Vertex = First; Vertex < First + Char.VertCount; ++Vertex)
		{
			const FVector2f P(Geo->OriginVertices[Vertex].Position.Y, Geo->OriginVertices[Vertex].Position.Z);
			Middle += P / Char.VertCount;
			Lowest = FMath::Min(Lowest, FVector2f::Distance(P, Centre));
		}
		// Standing on the circle: the glyph's lowest point is on it (to within the glyph's tilt), the rest outside.
		TestTrue(*FString::Printf(TEXT("Glyph %d stands on the circle"), Char.CharIndex), Lowest > Radius - 3.0f && Lowest < Radius + 3.0f);
		Angles.Add(FMath::Atan2(Middle.X - Centre.X, Middle.Y - Centre.Y));
	}
	TestTrue(TEXT("Five glyphs"), Angles.Num() == 5);
	for (int32 Index = 1; Index < Angles.Num(); ++Index)
	{
		TestTrue(*FString::Printf(TEXT("Glyph %d is further round, clockwise"), Index), Angles[Index] > Angles[Index - 1]);
	}
	TestTrue(TEXT("Centred on twelve o'clock"), FMath::Abs(Angles[2]) < 0.05f);
	OnPath->SetFlip(true);
	Stage.Frames(2);
	Geo = GeometryOf(Text->GetWidget());
	const FDreamUITextCharProperty& FirstChar = Text->GetCharPropertyArray()[0];
	FVector2f Middle = FVector2f::ZeroVector;
	for (int32 Vertex = FirstChar.StartVertIndex; Vertex < FirstChar.StartVertIndex + FirstChar.VertCount; ++Vertex)
	{
		Middle += FVector2f(Geo->OriginVertices[Vertex].Position.Y, Geo->OriginVertices[Vertex].Position.Z) / FirstChar.VertCount;
	}
	TestTrue(TEXT("Flipped, the glyphs are inside the circle"), FVector2f::Distance(Middle, Centre) < Radius);
	TestTrue(TEXT("...and the first is now on the right"), Middle.X > Centre.X);
	// Bent glyph by glyph: cut into columns, every vertex on its own ring about the centre.
	OnPath->SetFlip(false);
	OnPath->SetBendGlyphs(true);
	Stage.Frames(2);
	Geo = GeometryOf(Text->GetWidget());
	TestTrue(TEXT("Bent, the glyphs are cut into columns"), Geo->OriginVertices.Num() > Flat.Num());
	float Nearest = MAX_flt;
	for (const FDreamUIOriginVertexData& Vertex : Geo->OriginVertices)
	{
		Nearest = FMath::Min(Nearest, FVector2f::Distance(FVector2f(Vertex.Position.Y, Vertex.Position.Z), Centre));
	}
	TestTrue(TEXT("...the lowest of them on the circle"), FMath::IsNearlyEqual(Nearest, Radius, 0.5f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextOnPathWidgetTest,
	"DreamGUI.TextOnPath.TextFollowsAPathWidgetAndFollowsItWhenItMoves",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextOnPathWidgetTest::RunTest(const FString& Parameters)
{
	using namespace DreamMeshWarpTestLocal;
	FStage Stage;
	if (!TestTrue(TEXT("The stage is usable"), Stage.IsUsable()))
	{
		return false;
	}
	UDreamWidget* PathWidget = Stage.MakeWidget(Stage.Root, TEXT("Path"), FVector2D(300.0, 100.0), FVector2D(0.0, -50.0));
	UDreamPath* Path = PathWidget->CreateNewVisual<UDreamPath>();
	if (!TestNotNull(TEXT("A path"), Path))
	{
		return false;
	}
	Path->SetCurveType(EDreamPathCurve::Linear);
	Path->SetPoints({ FVector2D(-150.0, 0.0), FVector2D(150.0, 0.0) });
	UDreamText* Text = Stage.MakeText(TEXT("Rider"), TEXT("ON"), FVector2D(200.0, 40.0), FVector2D(0.0, 100.0));
	UDreamMeshModifierTextOnPath* OnPath = Text->GetWidget()->AddComponent<UDreamMeshModifierTextOnPath>();
	OnPath->SetSource(EDreamTextPathSource::Path);
	OnPath->SetPathWidget(PathWidget);
	OnPath->SetAnchor(EDreamTextPathAnchor::Bottom);
	Stage.Frames(3);
	auto LowestWorldZ = [&]()
	{
		const FDreamUIGeometry* Geo = GeometryOf(Text->GetWidget());
		float Lowest = MAX_flt;
		for (const FDreamUIOriginVertexData& Vertex : Geo->OriginVertices)
		{
			Lowest = FMath::Min(Lowest, static_cast<float>(Text->GetWidget()->GetWorldTransform().TransformPosition(FVector(Vertex.Position)).Z));
		}
		return Lowest;
	};
	const float PathZ = static_cast<float>(PathWidget->GetWorldTransform().TransformPosition(FVector(0.0, 0.0, 0.0)).Z);
	TestTrue(TEXT("The text stands on the path"), FMath::IsNearlyEqual(LowestWorldZ(), PathZ, 0.5f));
	PathWidget->SetAnchoredPosition(FVector2D(0.0, -120.0));
	Stage.Frames(3);
	const float MovedZ = static_cast<float>(PathWidget->GetWorldTransform().TransformPosition(FVector(0.0, 0.0, 0.0)).Z);
	TestTrue(TEXT("Moved, the path has moved"), !FMath::IsNearlyEqual(MovedZ, PathZ, 1.0f));
	TestTrue(TEXT("...and the text with it"), FMath::IsNearlyEqual(LowestWorldZ(), MovedZ, 0.5f));
	Path->SetPoints({ FVector2D(-150.0, 30.0), FVector2D(150.0, 30.0) });
	Stage.Frames(2);
	TestTrue(TEXT("A new shape is followed too"), FMath::IsNearlyEqual(LowestWorldZ(), MovedZ + 30.0f, 0.5f));
	return true;
}

#endif
