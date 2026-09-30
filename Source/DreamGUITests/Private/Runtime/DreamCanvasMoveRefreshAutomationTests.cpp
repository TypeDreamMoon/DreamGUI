// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamTexture.h"
#include "Core/Components/DreamVisualBatchMesh.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamCanvasProcessingDrawCallData.h"
#include "Core/DreamUIDrawCall.h"
#include "Core/DreamUIGeometry.h"
#include "Core/DreamUIManager.h"
#include "Core/DreamUIMesh/DreamUIMeshComponent.h"
#include "DreamUIRender/DreamUIRenderStats.h"
#include "Engine/World.h"
#include "Utils/DreamUIUtils.h"

#include "Lifecycle/DreamLifecycleFixtures.h"

/*
 * A widget that only moves keeps its canvas's draw calls.
 *
 * Every move used to rebuild them -- prepare every element, batch on a worker, take the sections back, send the
 * materials again -- so a panel whose one button turned paid for a panel built from nothing, every frame of the turn.
 * A batch only depends on where its elements lie in three ways: which elements are flat, which lie outside the canvas
 * rect, and, where an element could have gone into an earlier draw call than the last one, what it overlaps on the way.
 * The batch says whether the last one mattered (FDreamUIBatchPlacement); if it did not and the other two are as they
 * were, a new batch would make the draw calls in hand, and only their vertices and bounds follow the move.
 */
namespace DreamCanvasMoveRefreshTestLocal
{
	FDreamUIGeometry MakeQuad(const FVector2D& InMin, const FVector2D& InMax, EDreamUIBlendMode InBlend)
	{
		FDreamUIGeometry Geo;
		Geo.Vertices.Add(FDreamUIMeshVertex(FVector3f(0.0f, (float)InMin.X, (float)InMin.Y)));
		Geo.Vertices.Add(FDreamUIMeshVertex(FVector3f(0.0f, (float)InMax.X, (float)InMin.Y)));
		Geo.Vertices.Add(FDreamUIMeshVertex(FVector3f(0.0f, (float)InMin.X, (float)InMax.Y)));
		Geo.Vertices.Add(FDreamUIMeshVertex(FVector3f(0.0f, (float)InMax.X, (float)InMax.Y)));
		Geo.Triangles = { 0, 3, 2, 0, 1, 3 };
		Geo.BoundsMin2DInCanvasSpace = InMin;
		Geo.BoundsMax2DInCanvasSpace = InMax;
		Geo.bSupportDrawcallBatching = true;
		Geo.BlendMode = InBlend;
		return Geo;
	}

	FDreamUIRenderData MakeRenderData(const FDreamUIGeometry& InGeo)
	{
		FDreamUIRenderData RenderData(EDreamUIDrawCallType::BatchMesh);
		const TSharedRef<FDreamUIGeometry> Prepared = MakeShared<FDreamUIGeometry>();
		Prepared->CopyDataForPrepare(InGeo);
		RenderData.BatchMeshGeometry = Prepared;
		return RenderData;
	}

	FDreamUIBatchPlacement Batch(const TArray<FDreamUIGeometry>& InGeometries, bool bInCull, int32& OutDrawCalls)
	{
		TArray<FDreamUIRenderData> RenderData;
		for (const FDreamUIGeometry& Geo : InGeometries)
		{
			RenderData.Add(MakeRenderData(Geo));
		}
		TArray<FDreamUIDrawCall> DrawCalls;
		FDreamUIBatchPlacement Placement;
		UDreamCanvas::BatchDrawCallAsync(FVector2D(-500.0, -500.0), FVector2D(500.0, 500.0), RenderData, DrawCalls, bInCull, &Placement);
		OutDrawCalls = DrawCalls.Num();
		return Placement;
	}

	/** A game world holding a world-space root canvas and a row of blocks, each with the blend mode given. */
	struct FRow
	{
		DreamTests::Lifecycle::FScopedWorld World{EWorldType::Game};
		UDreamUIManagerWorldSubsystem* Manager = nullptr;
		UDreamCanvas* Canvas = nullptr;
		TArray<UDreamWidget*> Blocks;

		bool Build(FAutomationTestBase& InTest, const TArray<EDreamUIBlendMode>& InBlends)
		{
			Manager = UDreamUIManagerWorldSubsystem::GetInstance(World.World);
			if (!InTest.TestNotNull(TEXT("A game world with a UI manager"), Manager))
			{
				return false;
			}
			UDreamWidget* Root = NewObject<UDreamWidget>(World.World, NAME_None, RF_Transient);
			Root->SetWidth(400.0f);
			Root->SetHeight(400.0f);
			Root->OnRegister();
			Canvas = Root->AddComponent<UDreamCanvas>();
			if (!InTest.TestNotNull(TEXT("A canvas on the root"), Canvas))
			{
				return false;
			}
			Canvas->SetRenderMode(EDreamRenderMode::WorldSpace);
			for (int32 Index = 0; Index < InBlends.Num(); ++Index)
			{
				UDreamWidget* Block = NewObject<UDreamWidget>(World.World, NAME_None, RF_Transient);
				Block->SetWidth(40.0f);
				Block->SetHeight(40.0f);
				Block->OnRegister();
				Block->TrySetParent(Root, false);
				// Apart, so that nothing overlaps: the batch's answer then turns on what could take each element alone.
				Block->SetAnchoredPosition(FVector2D(-120.0 + Index * 120.0, 0.0));
				UDreamTexture* Visual = Block->CreateNewVisual<UDreamTexture>();
				if (!InTest.TestNotNull(TEXT("A texture visual on each block"), Visual))
				{
					return false;
				}
				Visual->SetTexture(FDreamUIUtils::GetDefaultWhiteTexture());
				Visual->SetBlendMode(InBlends[Index]);
				Blocks.Add(Block);
			}
			// Built, batched, and the batch taken.
			Frames(3);
			return true;
		}

		/**
		 * Whole frames: the manager's update, then the submission that ends a frame, where the canvas takes its draw calls.
		 * The frame counter moves on as an engine frame's does, since the in-place refresh only takes draw calls made in an
		 * earlier frame than its own.
		 */
		void Frames(int32 InCount)
		{
			for (int32 Frame = 0; Frame < InCount; ++Frame)
			{
				++GFrameCounter;
				Manager->Tick(1.0f / 30.0f);
				Manager->SubmitCanvasDrawCall();
			}
		}

		/** What InChange and one frame after it cost: rebuilds and in-place refreshes, in that order. */
		TPair<int64, int64> Count(TFunctionRef<void()> InChange)
		{
			DreamUIRenderStats::TakeSnapshot(/*bInReset*/ true);
			InChange();
			Frames(1);
			const DreamUIRenderStats::FSnapshot Snapshot = DreamUIRenderStats::TakeSnapshot(/*bInReset*/ true);
			return TPair<int64, int64>(
				Snapshot.Counters[static_cast<int32>(DreamUIRenderStats::ECounter::DrawCallRebuilds)],
				Snapshot.Counters[static_cast<int32>(DreamUIRenderStats::ECounter::InPlaceRefreshes)]);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamBatchPlacementTest,
	"DreamGUI.Canvas.ABatchSaysWhetherWhereItsElementsLieCouldChangeItsDrawCalls",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamBatchPlacementTest::RunTest(const FString& Parameters)
{
	using namespace DreamCanvasMoveRefreshTestLocal;
	const EDreamUIBlendMode A = EDreamUIBlendMode::Alpha;
	const EDreamUIBlendMode B = EDreamUIBlendMode::Additive;
	int32 DrawCalls = 0;

	// Two elements nothing can put together: each takes a draw call of its own wherever it lies.
	FDreamUIBatchPlacement Placement = Batch({ MakeQuad(FVector2D(-100.0), FVector2D(-50.0), A), MakeQuad(FVector2D(50.0), FVector2D(100.0), B) }, false, DrawCalls);
	TestEqual(TEXT("Two keys: two draw calls"), DrawCalls, 2);
	TestTrue(TEXT("...and nothing about them depended on positions"), Placement.bIndependentOfPositions);

	// Two alike in a row: the second can only go into the last draw call, which it does overlapping or not.
	Placement = Batch({ MakeQuad(FVector2D(-100.0), FVector2D(-50.0), A), MakeQuad(FVector2D(50.0), FVector2D(100.0), A) }, false, DrawCalls);
	TestEqual(TEXT("Two alike in a row: one draw call"), DrawCalls, 1);
	TestTrue(TEXT("...independent of positions"), Placement.bIndependentOfPositions);

	// Alike with another between them: the third joins the first because it clears the second, and would not if it
	// overlapped it. That is a batch a move can change.
	Placement = Batch({ MakeQuad(FVector2D(-200.0), FVector2D(-150.0), A), MakeQuad(FVector2D(-20.0), FVector2D(20.0), B),
		MakeQuad(FVector2D(150.0), FVector2D(200.0), A) }, false, DrawCalls);
	TestEqual(TEXT("A, B, A apart: the two alike share a draw call"), DrawCalls, 2);
	TestFalse(TEXT("...which depended on the third clearing the second"), Placement.bIndependentOfPositions);

	// Culled elements are named, so that one coming back in is noticed.
	Placement = Batch({ MakeQuad(FVector2D(-100.0), FVector2D(-50.0), A), MakeQuad(FVector2D(900.0), FVector2D(950.0), A) }, true, DrawCalls);
	TestEqual(TEXT("One element off the rect is left out"), DrawCalls, 1);
	TestEqual(TEXT("...and listed as culled"), Placement.CulledVisuals.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCanvasTurnInPlaneRefreshesTest,
	"DreamGUI.Canvas.AWidgetTurningInTheCanvasPlaneKeepsTheDrawCallsAndTheirVerticesFollowIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamCanvasTurnInPlaneRefreshesTest::RunTest(const FString& Parameters)
{
	using namespace DreamCanvasMoveRefreshTestLocal;
	FRow Row;
	if (!Row.Build(*this, { EDreamUIBlendMode::Alpha, EDreamUIBlendMode::Additive }))
	{
		return false;
	}
	UDreamWidget* Turned = Row.Blocks[0];
	const FDreamUIGeometry* Geometry = static_cast<UDreamVisualBatchMesh*>(Turned->GetVisual())->GetGeometry();
	if (!TestTrue(TEXT("The block that turns has a geometry"), Geometry != nullptr && Geometry->Vertices.Num() > 0))
	{
		return false;
	}
	const FVector3f Before = Geometry->Vertices[0].Position;

	// A roll is a turn about the canvas's depth axis: the block stays flat on the canvas.
	const TPair<int64, int64> Rolled = Row.Count([Turned]() { Turned->SetRenderRotation(FRotator(0.0, 0.0, 30.0)); });
	TestEqual(TEXT("Turning in the canvas plane rebuilds nothing"), Rolled.Key, static_cast<int64>(0));
	TestEqual(TEXT("...the canvas refreshes its draw calls in place"), Rolled.Value, static_cast<int64>(1));
	TestFalse(TEXT("...and the vertices follow the turn"), Geometry->Vertices[0].Position.Equals(Before));

	// Still turning, frame after frame, as an animation does.
	for (int32 Step = 2; Step <= 4; ++Step)
	{
		const TPair<int64, int64> Again = Row.Count([Turned, Step]() { Turned->SetRenderRotation(FRotator(0.0, 0.0, 30.0 * Step)); });
		TestEqual(FString::Printf(TEXT("Turn %d: still no rebuild"), Step), Again.Key, static_cast<int64>(0));
	}

	// A move along the canvas is the same case.
	const TPair<int64, int64> Slid = Row.Count([Turned]() { Turned->SetRenderTranslation(FVector(0.0, 15.0, 10.0)); });
	TestEqual(TEXT("Sliding along the canvas rebuilds nothing either"), Slid.Key, static_cast<int64>(0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCanvasMoveThatCanRebatchRebuildsTest,
	"DreamGUI.Canvas.AMoveThatCouldChangeHowTheCanvasBatchesRebuildsItsDrawCalls",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamCanvasMoveThatCanRebatchRebuildsTest::RunTest(const FString& Parameters)
{
	using namespace DreamCanvasMoveRefreshTestLocal;
	{
		FRow Row;
		if (!Row.Build(*this, { EDreamUIBlendMode::Alpha, EDreamUIBlendMode::Additive }))
		{
			return false;
		}
		// Turned away from the viewer, a block stops being flat, and a flat element and a 3D one batch differently.
		UDreamWidget* Block = Row.Blocks[0];
		const TPair<int64, int64> Yawed = Row.Count([Block]() { Block->SetRenderRotation(FRotator(0.0, 35.0, 0.0)); });
		TestEqual(TEXT("Turning off the canvas plane rebuilds the draw calls"), Yawed.Key, static_cast<int64>(1));

		// The root canvas culls by its rect: a block that leaves it is left out of the next batch.
		UDreamWidget* Other = Row.Blocks[1];
		const TPair<int64, int64> Left = Row.Count([Other]() { Other->SetRenderTranslation(FVector(0.0, 5000.0, 0.0)); });
		TestEqual(TEXT("Moving off the canvas rect rebuilds the draw calls"), Left.Key, static_cast<int64>(1));
	}
	{
		// Alike, other, alike: the third went into the first's draw call because it cleared the second; moved onto the
		// second, it would not. The canvas cannot tell without batching again.
		FRow Row;
		if (!Row.Build(*this, { EDreamUIBlendMode::Alpha, EDreamUIBlendMode::Additive, EDreamUIBlendMode::Alpha }))
		{
			return false;
		}
		UDreamWidget* Third = Row.Blocks[2];
		const TPair<int64, int64> Moved = Row.Count([Third]() { Third->SetRenderTranslation(FVector(0.0, -30.0, 0.0)); });
		TestEqual(TEXT("A move in a batch that depends on positions rebuilds the draw calls"), Moved.Key, static_cast<int64>(1));
		TestEqual(TEXT("...and does not refresh them in place"), Moved.Value, static_cast<int64>(0));
	}
	return true;
}

namespace DreamCanvasMoveRefreshTestLocal
{
	/** Several world-space panels in one game world, each a root canvas holding one block. */
	struct FPanels
	{
		DreamTests::Lifecycle::FScopedWorld World{EWorldType::Game};
		UDreamUIManagerWorldSubsystem* Manager = nullptr;
		TArray<UDreamCanvas*> Canvases;
		TArray<UDreamWidget*> Blocks;

		bool Build(FAutomationTestBase& InTest, int32 InCount)
		{
			Manager = UDreamUIManagerWorldSubsystem::GetInstance(World.World);
			if (!InTest.TestNotNull(TEXT("A game world with a UI manager"), Manager))
			{
				return false;
			}
			for (int32 Index = 0; Index < InCount; ++Index)
			{
				UDreamWidget* Root = NewObject<UDreamWidget>(World.World, NAME_None, RF_Transient);
				Root->SetWidth(200.0f);
				Root->SetHeight(200.0f);
				Root->OnRegister();
				UDreamCanvas* Canvas = Root->AddComponent<UDreamCanvas>();
				if (!InTest.TestNotNull(TEXT("A canvas on each panel"), Canvas))
				{
					return false;
				}
				Canvas->SetRenderMode(EDreamRenderMode::WorldSpace);
				UDreamWidget* Block = NewObject<UDreamWidget>(World.World, NAME_None, RF_Transient);
				Block->SetWidth(40.0f + Index);
				Block->SetHeight(30.0f);
				Block->OnRegister();
				Block->TrySetParent(Root, false);
				UDreamTexture* Visual = Block->CreateNewVisual<UDreamTexture>();
				if (!InTest.TestNotNull(TEXT("A texture visual on each block"), Visual))
				{
					return false;
				}
				Visual->SetTexture(FDreamUIUtils::GetDefaultWhiteTexture());
				Canvases.Add(Canvas);
				Blocks.Add(Block);
			}
			Frames(3);
			return true;
		}

		void Frames(int32 InCount)
		{
			for (int32 Frame = 0; Frame < InCount; ++Frame)
			{
				++GFrameCounter;
				Manager->Tick(1.0f / 30.0f);
				Manager->SubmitCanvasDrawCall();
			}
		}
	};

	/** What a run of turning frames left: each panel's mesh bounds after every frame, and the counters over the run. */
	struct FTurnRun
	{
		TArray<FBox> Bounds;
		int64 Rebuilds = 0;
		int64 InPlaceRefreshes = 0;
		int64 SectionPatches = 0;
	};

	bool TurnPanels(FAutomationTestBase& InTest, int32 InParallelMinCanvases, FTurnRun& OutRun)
	{
		const DreamTests::Lifecycle::FScopedConsoleVariable Parallel(TEXT("r.DreamUI.ParallelVertexRefreshMinCanvases"), InParallelMinCanvases);
		constexpr int32 PanelCount = 40;
		FPanels Panels;
		if (!Panels.Build(InTest, PanelCount))
		{
			return false;
		}
		DreamUIRenderStats::TakeSnapshot(/*bInReset*/ true);
		for (int32 Step = 1; Step <= 3; ++Step)
		{
			for (int32 Index = 0; Index < PanelCount; ++Index)
			{
				Panels.Blocks[Index]->SetRenderRotation(FRotator(0.0, 0.0, 7.0 * Index + 20.0 * Step));
			}
			Panels.Frames(1);
			for (UDreamCanvas* Canvas : Panels.Canvases)
			{
				const UDreamUIMeshComponent* Mesh = Canvas->GetUIMesh();
				OutRun.Bounds.Add(Mesh != nullptr ? Mesh->Bounds.GetBox() : FBox(ForceInit));
			}
		}
		const DreamUIRenderStats::FSnapshot Snapshot = DreamUIRenderStats::TakeSnapshot(/*bInReset*/ true);
		OutRun.Rebuilds = Snapshot.Counters[static_cast<int32>(DreamUIRenderStats::ECounter::DrawCallRebuilds)];
		OutRun.InPlaceRefreshes = Snapshot.Counters[static_cast<int32>(DreamUIRenderStats::ECounter::InPlaceRefreshes)];
		OutRun.SectionPatches = Snapshot.Counters[static_cast<int32>(DreamUIRenderStats::ECounter::SectionPatches)];
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCanvasParallelVertexRefreshTest,
	"DreamGUI.Canvas.PanelsRefreshedTogetherOnWorkersEndAsTheyDoOneAfterAnother",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamCanvasParallelVertexRefreshTest::RunTest(const FString& Parameters)
{
	using namespace DreamCanvasMoveRefreshTestLocal;
	FTurnRun OneAfterAnother;
	FTurnRun Together;
	if (!TurnPanels(*this, 0, OneAfterAnother) || !TurnPanels(*this, 1, Together))
	{
		return false;
	}
	TestEqual(TEXT("Neither run rebuilds a draw call for a turn in the canvas plane"), OneAfterAnother.Rebuilds + Together.Rebuilds, static_cast<int64>(0));
	TestEqual(TEXT("Every panel refreshes in place every frame, one after another"), OneAfterAnother.InPlaceRefreshes, static_cast<int64>(3 * 40));
	TestEqual(TEXT("...and together"), Together.InPlaceRefreshes, OneAfterAnother.InPlaceRefreshes);
	TestTrue(TEXT("The refreshes patch sections"), OneAfterAnother.SectionPatches > 0);
	TestEqual(TEXT("...as many together as one after another"), Together.SectionPatches, OneAfterAnother.SectionPatches);
	if (!TestEqual(TEXT("Both runs saw every panel every frame"), Together.Bounds.Num(), OneAfterAnother.Bounds.Num()))
	{
		return false;
	}
	for (int32 Index = 0; Index < Together.Bounds.Num(); ++Index)
	{
		const FBox& A = OneAfterAnother.Bounds[Index];
		const FBox& B = Together.Bounds[Index];
		TestTrue(FString::Printf(TEXT("Panel %d, frame %d: the mesh bounds follow the turn the same way"), Index % 40, Index / 40 + 1),
			A.IsValid == B.IsValid && A.Min.Equals(B.Min, 1.e-3) && A.Max.Equals(B.Max, 1.e-3));
	}
	return true;
}

#endif
