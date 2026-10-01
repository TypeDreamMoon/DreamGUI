// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamButton.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamTexture.h"
#include "Core/Components/DreamVisualBatchMesh.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamCanvasProcessingDrawCallData.h"
#include "Core/DreamUIDrawCall.h"
#include "Core/DreamUIGeometry.h"
#include "Core/DreamUIManager.h"
#include "Core/DreamUIMesh/DreamUIMeshComponent.h"
#include "Core/DreamUIRenderLayerTable.h"
#include "Engine/Texture2D.h"
#include "DreamUIRender/DreamUIRenderStats.h"
#include "Engine/World.h"
#include "Event/DreamWorldSpaceRaycaster.h"
#include "Utils/DreamUIUtils.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverWorldSpace.h"
#include "Interaction/DreamPressInteractionTestTypes.h"
#include "Lifecycle/DreamLifecycleFixtures.h"
#include "Core/DreamUIGoneCount.h"

/*
 * A widget whose render transform keeps changing becomes a render layer of its canvas: the geometry under it is kept
 * relative to it, and its transform is applied on the GPU, through its row of the world's render layer table. While it
 * turns, nothing under it is transformed, patched or uploaded again -- only its row goes to the render thread.
 *
 * The batching rules come first, on hand-built render data, as DreamCanvasMoveRefreshAutomationTests.cpp drives the
 * batcher: a layer's elements batch as 3D elements do, into the draw call just before them whatever layer that one's
 * are in, and where a layer lies never changes what the batch comes out as. The rest drive a game-world canvas drawn by
 * DreamGUI's renderer frame by frame and read the counters: when a widget becomes a layer, what its moves cost after
 * that, when it stops being one, and what the switches and settings decide. The pictures are in
 * DreamRenderLayerPixelAutomationTests.cpp.
 */
namespace DreamRenderLayerTestLocal
{
	FDreamUIGeometry MakeQuad(const FVector2D& InMin, const FVector2D& InMax, const UDreamWidget* InLayer = nullptr, int32 InLayerRow = 0)
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
		if (InLayer != nullptr)
		{
			Geo.RenderLayer = InLayer;
			Geo.RenderLayerRow = InLayerRow;
		}
		return Geo;
	}

	/** Through CopyDataForPrepare, as the canvas prepares its elements: a field the copy dropped would never reach the batch. */
	FDreamUIRenderData MakeRenderData(const FDreamUIGeometry& InGeo)
	{
		FDreamUIRenderData RenderData(EDreamUIDrawCallType::BatchMesh);
		const TSharedRef<FDreamUIGeometry> Prepared = MakeShared<FDreamUIGeometry>();
		Prepared->CopyDataForPrepare(InGeo);
		RenderData.BatchMeshGeometry = Prepared;
		return RenderData;
	}

	FDreamUIBatchPlacement Batch(const TArray<FDreamUIGeometry>& InGeometries, bool bInCull, TArray<FDreamUIDrawCall>& OutDrawCalls)
	{
		TArray<FDreamUIRenderData> RenderData;
		for (const FDreamUIGeometry& Geo : InGeometries)
		{
			RenderData.Add(MakeRenderData(Geo));
		}
		FDreamUIBatchPlacement Placement;
		UDreamCanvas::BatchDrawCallAsync(FVector2D(-500.0, -500.0), FVector2D(500.0, 500.0), RenderData, OutDrawCalls, bInCull, &Placement);
		return Placement;
	}

	/**
	 * A game world holding a canvas drawn by DreamGUI's world-space renderer -- the one kind of mesh a layer can be drawn in
	 * -- and on it a card that turns, a face on the card, and a block that stays where it is.
	 */
	struct FStage
	{
		DreamTests::Lifecycle::FScopedWorld World{EWorldType::Game};
		UDreamUIManagerWorldSubsystem* Manager = nullptr;
		UDreamCanvas* Canvas = nullptr;
		UDreamWidget* Root = nullptr;
		UDreamWidget* Card = nullptr;
		UDreamWidget* Face = nullptr;
		UDreamWidget* Still = nullptr;

		bool Build(FAutomationTestBase& InTest, EDreamRenderMode InRenderMode = EDreamRenderMode::WorldSpace_DreamUI)
		{
			Manager = UDreamUIManagerWorldSubsystem::GetInstance(World.World);
			if (!InTest.TestNotNull(TEXT("A game world with a UI manager"), Manager))
			{
				return false;
			}
			Root = MakeWidget(nullptr, 400.0f, 400.0f, FVector2D::ZeroVector);
			Canvas = Root->AddComponent<UDreamCanvas>();
			if (!InTest.TestNotNull(TEXT("A canvas on the root"), Canvas))
			{
				return false;
			}
			Canvas->SetRenderMode(InRenderMode);
			Card = MakeBlock(Root, 120.0f, 80.0f, FVector2D(-80.0, 0.0), FColor::Red);
			Face = MakeBlock(Card, 60.0f, 40.0f, FVector2D(15.0, 5.0), FColor::Green);
			Still = MakeBlock(Root, 60.0f, 60.0f, FVector2D(120.0, 0.0), FColor::Blue);
			if (!InTest.TestTrue(TEXT("Each block has a visual"), Card->GetVisual() != nullptr && Face->GetVisual() != nullptr && Still->GetVisual() != nullptr))
			{
				return false;
			}
			// Built, batched, and the batch taken.
			Frames(3);
			return true;
		}

		UDreamWidget* MakeWidget(UDreamWidget* InParent, float InWidth, float InHeight, const FVector2D& InPosition)
		{
			UDreamWidget* Widget = NewObject<UDreamWidget>(World.World, NAME_None, RF_Transient);
			Widget->SetWidth(InWidth);
			Widget->SetHeight(InHeight);
			Widget->OnRegister();
			if (InParent != nullptr)
			{
				Widget->TrySetParent(InParent, false);
				Widget->SetAnchoredPosition(InPosition);
			}
			return Widget;
		}

		UDreamWidget* MakeBlock(UDreamWidget* InParent, float InWidth, float InHeight, const FVector2D& InPosition, const FColor& InColour)
		{
			UDreamWidget* Widget = MakeWidget(InParent, InWidth, InHeight, InPosition);
			if (UDreamTexture* Visual = Widget->CreateNewVisual<UDreamTexture>())
			{
				Visual->SetTexture(FDreamUIUtils::GetDefaultWhiteTexture());
				Visual->SetColor(InColour);
			}
			return Widget;
		}

		/**
		 * Whole frames: the manager's update, then the submission that ends a frame. The frame counter moves on as an engine
		 * frame's does: a render transform changing on frames in a row is what makes a layer.
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

		/** One frame in which the card has turned to InYaw, as an animation turns it. */
		void TurnCard(double InYaw)
		{
			Card->SetRenderRotation(FRotator(0.0, InYaw, 0.0));
			Frames(1);
		}

		const FDreamUIGeometry* GeometryOf(const UDreamWidget* InWidget) const
		{
			const UDreamVisualBatchMesh* Visual = Cast<UDreamVisualBatchMesh>(InWidget->GetVisual());
			return Visual != nullptr ? Visual->GetGeometry() : nullptr;
		}
	};

	/** The counters start again from here. */
	void ResetCounters()
	{
		DreamUIRenderStats::TakeSnapshot(/*bInReset*/ true);
	}

	/** The counters of the frames since the last reset, which it resets. */
	struct FCounted
	{
		DreamUIRenderStats::FSnapshot Snapshot = DreamUIRenderStats::TakeSnapshot(/*bInReset*/ true);

		int64 Get(DreamUIRenderStats::ECounter InCounter) const
		{
			return Snapshot.Counters[static_cast<int32>(InCounter)];
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRenderLayerBatchingTest,
	"DreamGUI.RenderLayer.ALayersElementsBatchIntoTheDrawCallBeforeThemWhateverLayerItHolds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRenderLayerBatchingTest::RunTest(const FString& Parameters)
{
	using namespace DreamRenderLayerTestLocal;
	// Two widgets that stand only for keys: the batching compares them and never looks at them.
	DreamTests::Lifecycle::FScopedWorld World(EWorldType::Game);
	const TStrongObjectPtr<UDreamWidget> LayerA(NewObject<UDreamWidget>(World.World, NAME_None, RF_Transient));
	const TStrongObjectPtr<UDreamWidget> LayerB(NewObject<UDreamWidget>(World.World, NAME_None, RF_Transient));
	constexpr int32 RowA = 3;
	constexpr int32 RowB = 7;

	// Six elements that would all share one draw call -- no texture, no material, one blend mode, nothing overlapping --
	// were they all flat: none, A, A, none, B, B. A layer's element goes into the draw call before it, as a 3D one does,
	// and makes it one no flat element walks back past.
	TArray<FDreamUIDrawCall> DrawCalls;
	FDreamUIBatchPlacement Placement = Batch({
		MakeQuad(FVector2D(-400.0, -20.0), FVector2D(-350.0, 20.0)),
		MakeQuad(FVector2D(-300.0, -20.0), FVector2D(-250.0, 20.0), LayerA.Get(), RowA),
		MakeQuad(FVector2D(-200.0, -20.0), FVector2D(-150.0, 20.0), LayerA.Get(), RowA),
		MakeQuad(FVector2D(-100.0, -20.0), FVector2D(-50.0, 20.0)),
		MakeQuad(FVector2D(0.0, -20.0), FVector2D(50.0, 20.0), LayerB.Get(), RowB),
		MakeQuad(FVector2D(100.0, -20.0), FVector2D(150.0, 20.0), LayerB.Get(), RowB) }, false, DrawCalls);
	if (!TestEqual(TEXT("None, A, A, none, B, B: two draw calls"), DrawCalls.Num(), 2))
	{
		return false;
	}
	TestEqual(TEXT("The first holds the element of no layer and both of A's"), DrawCalls[0].BatchMeshGeometryArray.Num(), 3);
	TestFalse(TEXT("...and ends a flat element's walk back, as a 3D draw call does"), DrawCalls[0].bIs2DSpace);
	TestTrue(TEXT("...boxing A's elements by A's row"), DrawCalls[0].LayerBounds.Num() == 1 && DrawCalls[0].LayerBounds[0].Row == RowA);
	TestTrue(TEXT("...and the other on the canvas"), DrawCalls[0].CombinedBounds.IsValid != 0);
	TestEqual(TEXT("The flat element after it opened the second, which took both of B's"), DrawCalls[1].BatchMeshGeometryArray.Num(), 3);
	TestTrue(TEXT("...boxed by B's row"), DrawCalls[1].LayerBounds.Num() == 1 && DrawCalls[1].LayerBounds[0].Row == RowB);
	TestTrue(TEXT("Nothing about it depended on where the elements are"), Placement.bIndependentOfPositions);

	// Two layers one after the other: one draw call, each vertex placed through its own layer's row.
	DrawCalls.Reset();
	Placement = Batch({
		MakeQuad(FVector2D(-300.0, -20.0), FVector2D(-250.0, 20.0), LayerA.Get(), RowA),
		MakeQuad(FVector2D(-200.0, -20.0), FVector2D(-150.0, 20.0), LayerB.Get(), RowB) }, false, DrawCalls);
	TestEqual(TEXT("A's element and then B's: one draw call"), DrawCalls.Num(), 1);
	TestTrue(TEXT("...holding a run of each layer's"), DrawCalls.Num() == 1 && DrawCalls[0].LayerBounds.Num() == 2
		&& DrawCalls[0].LayerBounds[0].Row == RowA && DrawCalls[0].LayerBounds[1].Row == RowB);

	// A layer's element in the same place as a flat one, and then far from it: the batch is the same either way, because a
	// layer's elements are placed by its row wherever that takes them.
	for (const FVector2D& Offset : { FVector2D(-380.0, 0.0), FVector2D(300.0, 0.0) })
	{
		DrawCalls.Reset();
		Placement = Batch({
			MakeQuad(FVector2D(-400.0, -20.0), FVector2D(-350.0, 20.0)),
			MakeQuad(Offset + FVector2D(0.0, -20.0), Offset + FVector2D(50.0, 20.0), LayerA.Get(), RowA),
			MakeQuad(FVector2D(200.0, -20.0), FVector2D(250.0, 20.0)) }, false, DrawCalls);
		TestEqual(FString::Printf(TEXT("With the layer's element at %.0f: two draw calls"), Offset.X), DrawCalls.Num(), 2);
		TestTrue(FString::Printf(TEXT("With the layer's element at %.0f: nothing depended on positions"), Offset.X), Placement.bIndependentOfPositions);
	}

	// Far outside the canvas rect, with culling on: an element of no layer is left out, a layer's is not -- where it ends
	// up is its layer's business.
	DrawCalls.Reset();
	Placement = Batch({
		MakeQuad(FVector2D(900.0, 900.0), FVector2D(950.0, 950.0)),
		MakeQuad(FVector2D(900.0, 900.0), FVector2D(950.0, 950.0), LayerA.Get(), RowA) }, true, DrawCalls);
	TestEqual(TEXT("Outside the rect, only the layer's element is drawn"), DrawCalls.Num(), 1);
	TestTrue(TEXT("...in a draw call boxed by its layer's row"), DrawCalls.Num() == 1 && DrawCalls[0].HasRenderLayerElements());
	TestEqual(TEXT("...and only the other is listed as culled"), Placement.CulledVisuals.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRenderLayerPositionSensitiveFromTest,
	"DreamGUI.RenderLayer.ABatchThatDependedOnPositionsSaysFromWhichDrawCallOn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRenderLayerPositionSensitiveFromTest::RunTest(const FString& Parameters)
{
	using namespace DreamRenderLayerTestLocal;
	DreamTests::Lifecycle::FScopedWorld World(EWorldType::Game);
	// Textures named outright: an element with none batches with any draw call, and takes on the texture of the next.
	const TStrongObjectPtr<UTexture2D> White(NewObject<UTexture2D>(GetTransientPackage(), NAME_None, RF_Transient));
	const TStrongObjectPtr<UTexture2D> Other(NewObject<UTexture2D>(GetTransientPackage(), NAME_None, RF_Transient));
	auto Textured = [](const FVector2D& InMin, const FVector2D& InMax, UTexture2D* InTexture)
	{
		FDreamUIGeometry Geo = MakeQuad(InMin, InMax);
		Geo.Texture = InTexture;
		return Geo;
	};
	// A 3D element, then white, other, white: the last one can go back past the other texture's draw call into the first
	// white one, which it does not overlap -- where it went depended on positions, but no further back than the 3D one.
	FDreamUIGeometry Turned = Textured(FVector2D(-400.0, -20.0), FVector2D(-350.0, 20.0), White.Get());
	Turned.TransformRelativeToCanvas = FTransform(FRotator(0.0, 30.0, 0.0));
	TArray<FDreamUIDrawCall> DrawCalls;
	const FDreamUIBatchPlacement Placement = Batch({
		Turned,
		Textured(FVector2D(-300.0, -20.0), FVector2D(-250.0, 20.0), White.Get()),
		Textured(FVector2D(-100.0, -20.0), FVector2D(-50.0, 20.0), Other.Get()),
		Textured(FVector2D(100.0, -20.0), FVector2D(150.0, 20.0), White.Get()) }, false, DrawCalls);
	if (!TestEqual(TEXT("3D, white, other, white: three draw calls"), DrawCalls.Num(), 3))
	{
		return false;
	}
	TestEqual(TEXT("The last white element went back into the first white draw call"), DrawCalls[1].BatchMeshGeometryArray.Num(), 2);
	TestFalse(TEXT("...which depended on positions"), Placement.bIndependentOfPositions);
	TestEqual(TEXT("...from the draw call after the 3D one on"), Placement.PositionSensitiveFrom, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRenderLayerTableRowsTest,
	"DreamGUI.RenderLayer.ATableRowIsHandedOutAgainOnlyAFewFramesAfterItIsGivenBack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRenderLayerTableRowsTest::RunTest(const FString& Parameters)
{
	const TStrongObjectPtr<UDreamUIRenderLayerTable> Table(NewObject<UDreamUIRenderLayerTable>(GetTransientPackage(), NAME_None, RF_Transient));
	const int32 A = Table->AcquireRow();
	const int32 B = Table->AcquireRow();
	TestTrue(TEXT("Two rows, neither of them row 0, which is no layer's"), A > 0 && B > 0 && A != B);
	TestNotNull(TEXT("...in a texture the shaders read"), Table->GetTexture());

	const FMatrix44f Placed(FTransform(FRotator(10.0, 20.0, 30.0), FVector(1.0, 2.0, 3.0), FVector(2.0)).ToMatrixWithScale());
	Table->WriteRow(A, Placed);
	TestTrue(TEXT("A row reads back what was written"), Table->ReadRow(A).Equals(Placed, 1.e-5f));
	TestTrue(TEXT("...and is sent up at the next flush"), Table->IsRowWrittenSinceFlush(A));
	Table->Flush();
	TestFalse(TEXT("...after which it is not any more"), Table->IsRowWrittenSinceFlush(A));
	TestTrue(TEXT("Row 0 reads identity"), Table->ReadRow(0).Equals(FMatrix44f::Identity));

	// Given back: what was drawn through it is transformed out of it at its canvas's next update, so it waits.
	Table->ReleaseRow(A);
	const int32 C = Table->AcquireRow();
	TestTrue(TEXT("A row given back is not handed out again at once"), C != A && C > 0);
	TestTrue(TEXT("...and keeps what it holds meanwhile"), Table->ReadRow(A).Equals(Placed, 1.e-5f));
	GFrameCounter += UDreamUIRenderLayerTable::ReleaseDelayFrames;
	Table->Flush();
	TestEqual(TEXT("Once the frames passed, it is handed out again"), Table->AcquireRow(), A);
	TestEqual(TEXT("Three rows are held"), Table->GetNumRowsInUse(), 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRenderLayerTurningCardTest,
	"DreamGUI.RenderLayer.AWidgetTurningEveryFrameBecomesALayerAndThenOnlyItsSectionsMove",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRenderLayerTurningCardTest::RunTest(const FString& Parameters)
{
	using namespace DreamRenderLayerTestLocal;
	using DreamUIRenderStats::ECounter;
	const DreamTests::Lifecycle::FScopedConsoleVariable Layers(TEXT("r.DreamUI.RenderLayers"), 1);
	const DreamTests::Lifecycle::FScopedConsoleVariable Promote(TEXT("r.DreamUI.RenderLayerPromoteFrames"), 2);
	// Short, so that the test sees the card held still for long enough.
	const DreamTests::Lifecycle::FScopedConsoleVariable Demote(TEXT("r.DreamUI.RenderLayerDemoteFrames"), 6);
	FStage Stage;
	if (!Stage.Build(*this))
	{
		return false;
	}

	// One frame of turning is not yet a run of them.
	ResetCounters();
	Stage.TurnCard(10.0);
	TestFalse(TEXT("After one frame of turning, the card is no layer yet"), Stage.Card->IsRenderLayer());
	Stage.TurnCard(20.0);
	const FCounted Made;
	if (!TestTrue(TEXT("After two frames in a row, the card is a layer"), Stage.Card->IsRenderLayer()))
	{
		return false;
	}
	TestEqual(TEXT("...made once"), Made.Get(ECounter::RenderLayerPromotions), static_cast<int64>(1));
	TestTrue(TEXT("The face on it is in its layer"), Stage.Face->GetRenderLayer() == Stage.Card);
	TestTrue(TEXT("...and so is the card itself"), Stage.Card->GetRenderLayer() == Stage.Card);
	TestTrue(TEXT("The block beside it is in none"), Stage.Still->GetRenderLayer() == nullptr);
	const FDreamUIGeometry* FaceGeometry = Stage.GeometryOf(Stage.Face);
	if (!TestTrue(TEXT("The face has a geometry"), FaceGeometry != nullptr && FaceGeometry->Vertices.Num() > 0))
	{
		return false;
	}
	TestTrue(TEXT("...kept relative to the card"), FaceGeometry->RenderLayer == TObjectKey<UDreamWidget>(Stage.Card));
	TestTrue(TEXT("The card holds a row of the world's render layer table"), Stage.Card->GetRenderLayerRow() > 0);
	TestEqual(TEXT("...which the face is placed through"), FaceGeometry->RenderLayerRow, Stage.Card->GetRenderLayerRow());
	const FVector3f FaceCorner = FaceGeometry->Vertices[0].Position;

	// Still turning, as an animation keeps turning it: each frame moves the card's sections, and nothing else.
	for (int32 Step = 3; Step <= 7; ++Step)
	{
		Stage.TurnCard(10.0 * Step);
	}
	const FCounted Turning;
	TestEqual(TEXT("Five more frames of turning rebuild nothing"), Turning.Get(ECounter::DrawCallRebuilds), static_cast<int64>(0));
	TestEqual(TEXT("...patch no section"), Turning.Get(ECounter::SectionPatches), static_cast<int64>(0));
	TestEqual(TEXT("...upload no section"), Turning.Get(ECounter::SectionUploads), static_cast<int64>(0));
	TestEqual(TEXT("...and move the layer once a frame"), Turning.Get(ECounter::RenderLayerMoves), static_cast<int64>(5));
	TestTrue(TEXT("The face's vertices, relative to the card, are what they were"), FaceGeometry->Vertices[0].Position == FaceCorner);
	TestTrue(TEXT("...and the card is a layer still"), Stage.Card->IsRenderLayer());

	// Held still: once the frames pass, the card goes back to batching with the rest.
	Stage.Frames(8);
	const FCounted Held;
	TestFalse(TEXT("Held still, the card is a layer no longer"), Stage.Card->IsRenderLayer());
	TestEqual(TEXT("...taken back once"), Held.Get(ECounter::RenderLayerDemotions), static_cast<int64>(1));
	TestEqual(TEXT("...for one rebuild"), Held.Get(ECounter::DrawCallRebuilds), static_cast<int64>(1));
	TestEqual(TEXT("...and nothing moved meanwhile"), Held.Get(ECounter::RenderLayerMoves), static_cast<int64>(0));
	TestTrue(TEXT("The face is in no layer"), Stage.Face->GetRenderLayer() == nullptr);
	FaceGeometry = Stage.GeometryOf(Stage.Face);
	TestTrue(TEXT("...and its geometry is back in canvas space"), FaceGeometry != nullptr && !FaceGeometry->IsInRenderLayer());
	TestEqual(TEXT("...placed through no row"), FaceGeometry != nullptr ? FaceGeometry->RenderLayerRow : -1, 0);
	TestEqual(TEXT("The card holds no row any more"), Stage.Card->GetRenderLayerRow(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRenderLayerSwitchedOffTest,
	"DreamGUI.RenderLayer.WithLayersSwitchedOffATurningWidgetIsRefreshedOnTheCpuAsBefore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRenderLayerSwitchedOffTest::RunTest(const FString& Parameters)
{
	using namespace DreamRenderLayerTestLocal;
	using DreamUIRenderStats::ECounter;
	const DreamTests::Lifecycle::FScopedConsoleVariable Layers(TEXT("r.DreamUI.RenderLayers"), 0);
	FStage Stage;
	if (!Stage.Build(*this))
	{
		return false;
	}
	// Turned off the canvas plane first, so that the turns after are moves of a 3D element the canvas refreshes in place.
	Stage.TurnCard(10.0);
	Stage.TurnCard(20.0);
	ResetCounters();
	for (int32 Step = 3; Step <= 7; ++Step)
	{
		Stage.TurnCard(10.0 * Step);
	}
	const FCounted Turning;
	TestFalse(TEXT("The card is no layer"), Stage.Card->IsRenderLayer());
	TestTrue(TEXT("...nor is anything its face is in"), Stage.Face->GetRenderLayer() == nullptr);
	TestEqual(TEXT("No layer is made"), Turning.Get(ECounter::RenderLayerPromotions), static_cast<int64>(0));
	TestEqual(TEXT("...and none moves"), Turning.Get(ECounter::RenderLayerMoves), static_cast<int64>(0));
	TestEqual(TEXT("Each frame refreshes the draw calls in place, as before"), Turning.Get(ECounter::InPlaceRefreshes), static_cast<int64>(5));
	TestTrue(TEXT("...patching the turned vertices into their section"), Turning.Get(ECounter::SectionPatches) >= 5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRenderLayerSettingsTest,
	"DreamGUI.RenderLayer.TheWidgetsSettingAndTheSwitchDecideWhatIsALayer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRenderLayerSettingsTest::RunTest(const FString& Parameters)
{
	using namespace DreamRenderLayerTestLocal;
	using DreamUIRenderStats::ECounter;
	FStage Stage;
	if (!Stage.Build(*this))
	{
		return false;
	}
	{
		const DreamTests::Lifecycle::FScopedConsoleVariable Layers(TEXT("r.DreamUI.RenderLayers"), 1);
		ResetCounters();
		// Always: a layer at the next update, whether or not anything moves it.
		Stage.Card->SetRenderLayerMode(EDreamWidgetRenderLayer::Always);
		Stage.Frames(2);
		TestTrue(TEXT("Set to Always, the card is a layer without being moved"), Stage.Card->IsRenderLayer());
		TestEqual(TEXT("...made once"), FCounted().Get(ECounter::RenderLayerPromotions), static_cast<int64>(1));
		// The canvas's own widget places the whole canvas, and is never a layer of it.
		Stage.Root->SetRenderLayerMode(EDreamWidgetRenderLayer::Always);
		Stage.Frames(2);
		TestFalse(TEXT("The canvas's own widget is no layer, whatever it is set to"), Stage.Root->IsRenderLayer());
		Stage.Root->SetRenderLayerMode(EDreamWidgetRenderLayer::Auto);
	}
	{
		// Switched off: every layer is taken back, and the draw calls are rebuilt without it.
		const DreamTests::Lifecycle::FScopedConsoleVariable Layers(TEXT("r.DreamUI.RenderLayers"), 0);
		Stage.Frames(3);
		const FCounted Off;
		TestFalse(TEXT("With layers switched off, the card is a layer no longer"), Stage.Card->IsRenderLayer());
		TestEqual(TEXT("...taken back once"), Off.Get(ECounter::RenderLayerDemotions), static_cast<int64>(1));
		TestTrue(TEXT("...and the draw calls were rebuilt"), Off.Get(ECounter::DrawCallRebuilds) >= 1);
		const FDreamUIGeometry* FaceGeometry = Stage.GeometryOf(Stage.Face);
		TestTrue(TEXT("The face's geometry is back in canvas space"), FaceGeometry != nullptr && !FaceGeometry->IsInRenderLayer());
	}
	{
		// On again: a widget set to Always is one again, with nothing else to wake its canvas.
		const DreamTests::Lifecycle::FScopedConsoleVariable Layers(TEXT("r.DreamUI.RenderLayers"), 1);
		Stage.Frames(3);
		TestTrue(TEXT("Switched on again, the card set to Always is a layer again"), Stage.Card->IsRenderLayer());

		// Never: taken back, and not made one however it turns.
		Stage.Card->SetRenderLayerMode(EDreamWidgetRenderLayer::Never);
		Stage.Frames(2);
		TestFalse(TEXT("Set to Never, the card is a layer no longer"), Stage.Card->IsRenderLayer());
		ResetCounters();
		for (int32 Step = 1; Step <= 5; ++Step)
		{
			Stage.TurnCard(10.0 * Step);
		}
		TestFalse(TEXT("...and turning it every frame does not make it one"), Stage.Card->IsRenderLayer());
		TestEqual(TEXT("...none is made"), FCounted().Get(ECounter::RenderLayerPromotions), static_cast<int64>(0));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRenderLayerRefusedTest,
	"DreamGUI.RenderLayer.AWidgetThatCannotBeALayerStaysOnTheCpuWhileItTurns",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRenderLayerRefusedTest::RunTest(const FString& Parameters)
{
	using namespace DreamRenderLayerTestLocal;
	using DreamUIRenderStats::ECounter;
	const DreamTests::Lifecycle::FScopedConsoleVariable Layers(TEXT("r.DreamUI.RenderLayers"), 1);
	{
		// Something under the card drawn through the matrix path: a shear is built from world transforms, which a layer's
		// transform does not carry.
		FStage Stage;
		if (!Stage.Build(*this))
		{
			return false;
		}
		Stage.Face->SetRenderShear(FVector2D(15.0, 0.0));
		Stage.Frames(1);
		ResetCounters();
		for (int32 Step = 1; Step <= 5; ++Step)
		{
			Stage.TurnCard(10.0 * Step);
		}
		TestFalse(TEXT("A card with a sheared face is no layer however it turns"), Stage.Card->IsRenderLayer());
		TestEqual(TEXT("...none is made"), FCounted().Get(ECounter::RenderLayerPromotions), static_cast<int64>(0));
	}
	{
		// The engine's renderer draws the canvas: it takes the primitive's transform alone, and has no room for a layer's.
		FStage Stage;
		if (!Stage.Build(*this, EDreamRenderMode::WorldSpace))
		{
			return false;
		}
		for (int32 Step = 1; Step <= 5; ++Step)
		{
			Stage.TurnCard(10.0 * Step);
		}
		TestFalse(TEXT("A card on a canvas the engine's renderer draws is no layer"), Stage.Card->IsRenderLayer());
		Stage.Card->SetRenderLayerMode(EDreamWidgetRenderLayer::Always);
		Stage.Frames(2);
		TestFalse(TEXT("...not even when set to Always"), Stage.Card->IsRenderLayer());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRenderLayerLimitTest,
	"DreamGUI.RenderLayer.ACanvasMakesNoMoreLayersOfItsOwnAccordThanItIsAllowed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRenderLayerLimitTest::RunTest(const FString& Parameters)
{
	using namespace DreamRenderLayerTestLocal;
	const DreamTests::Lifecycle::FScopedConsoleVariable Layers(TEXT("r.DreamUI.RenderLayers"), 1);
	const DreamTests::Lifecycle::FScopedConsoleVariable Limit(TEXT("r.DreamUI.RenderLayerMaxPerCanvas"), 1);
	FStage Stage;
	if (!Stage.Build(*this))
	{
		return false;
	}
	// The card and the block beside it turn together, frame after frame: two widgets the canvas would make layers.
	for (int32 Step = 1; Step <= 4; ++Step)
	{
		Stage.Still->SetRenderRotation(FRotator(0.0, 10.0 * Step, 0.0));
		Stage.TurnCard(10.0 * Step);
	}
	const int32 LayerCount = (Stage.Card->IsRenderLayer() ? 1 : 0) + (Stage.Still->IsRenderLayer() ? 1 : 0);
	TestEqual(TEXT("With room for one layer, one of the two turning widgets is a layer"), LayerCount, 1);

	// The limit is on what the canvas makes of its own accord: a widget set to Always is made one past it.
	UDreamWidget* Other = Stage.Card->IsRenderLayer() ? Stage.Still : Stage.Card;
	Other->SetRenderLayerMode(EDreamWidgetRenderLayer::Always);
	Stage.Frames(2);
	TestTrue(TEXT("Set to Always, the other one is a layer too"), Stage.Card->IsRenderLayer() && Stage.Still->IsRenderLayer());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRenderLayerTreeTest,
	"DreamGUI.RenderLayer.AWidgetLeavingALayerIsKeptInCanvasSpaceAgain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRenderLayerTreeTest::RunTest(const FString& Parameters)
{
	using namespace DreamRenderLayerTestLocal;
	const DreamTests::Lifecycle::FScopedConsoleVariable Layers(TEXT("r.DreamUI.RenderLayers"), 1);
	FStage Stage;
	if (!Stage.Build(*this))
	{
		return false;
	}
	Stage.Card->SetRenderRotation(FRotator(0.0, 35.0, 10.0));
	Stage.Card->SetRenderLayerMode(EDreamWidgetRenderLayer::Always);
	Stage.Frames(2);
	if (!TestTrue(TEXT("The card is a layer"), Stage.Card->IsRenderLayer()))
	{
		return false;
	}
	const FDreamUIGeometry* FaceGeometry = Stage.GeometryOf(Stage.Face);
	if (!TestTrue(TEXT("The face's geometry is kept relative to the card"), FaceGeometry != nullptr && FaceGeometry->RenderLayer == TObjectKey<UDreamWidget>(Stage.Card)))
	{
		return false;
	}
	// Relative to the card is where the face is in the card: its own local transform, the card's turn left out.
	TestTrue(TEXT("...at its place in the card, whatever the card's turn"),
		FaceGeometry->TransformRelativeToCanvas.Equals(Stage.Face->GetRenderLocalTransform(), 1.e-4));

	// Taken out of the card: the tree changed, and with it the layer the face's geometry is kept relative to.
	Stage.Face->TrySetParent(Stage.Root, true);
	Stage.Frames(2);
	TestTrue(TEXT("Out of the card, the face is in no layer"), Stage.Face->GetRenderLayer() == nullptr);
	FaceGeometry = Stage.GeometryOf(Stage.Face);
	TestTrue(TEXT("...and its geometry is in canvas space again"), FaceGeometry != nullptr && !FaceGeometry->IsInRenderLayer());
	TestTrue(TEXT("The card is still a layer"), Stage.Card->IsRenderLayer());
	return true;
}

namespace DreamRenderLayerTestLocal
{
	const FIntPoint HitViewportSize(1280, 720);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRenderLayerHitTest,
	"DreamGUI.RenderLayer.AButtonInATurnedLayerIsClickedWhereItIsDrawn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRenderLayerHitTest::RunTest(const FString& Parameters)
{
	using namespace DreamRenderLayerTestLocal;
	const DreamTests::Lifecycle::FScopedConsoleVariable Layers(TEXT("r.DreamUI.RenderLayers"), 1);
	FDreamDriverRig Rig = FDreamDriverRig::Headless(HitViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	// The world pointer first, as a player exists before the level's panels; then a panel 300 cm ahead, facing the eye.
	UDreamDriverWorldSpaceRaycaster* Pointer = DreamDriverWorld::AttachWorldPointer(Rig,
		DreamDriverWorld::MakeView(FVector::ZeroVector, FRotator::ZeroRotator, 90.0f, HitViewportSize), EDreamWorldPointerSource::Mouse);
	UDreamWidget* Panel = DreamDriverWorld::MakeWorldPanel(Rig, TEXT("Panel"), FTransform(FRotator::ZeroRotator, FVector(300.0, 0.0, 0.0)), FVector2D(400.0, 300.0));
	if (!TestNotNull(TEXT("A world pointer"), Pointer) || !TestNotNull(TEXT("A panel drawn by DreamGUI's world-space renderer"), Panel))
	{
		return false;
	}
	UDreamWidget* Card = Rig.MakeWidget(TEXT("Card"), Panel, FVector2D(220.0, 140.0));
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	UDreamButton* Button = Rig.MakeControl<UDreamButton>(TEXT("Play"), Card, FVector2D(100.0, 50.0), FVector2D(30.0, 20.0));
	if (!TestNotNull(TEXT("A card on the panel"), Card) || !TestNotNull(TEXT("A button on the card"), Button))
	{
		return false;
	}
	Button->OnClicked.AddDynamic(Listener.Get(), &UDreamPressInteractionListener::HandleClicked);
	// Turned off the panel's plane and in it, and drawn as a layer: its geometry is kept relative to it, and its turn is
	// applied on the GPU. Hit tests read the widgets' own transforms, and have to land where that draws the button.
	Card->SetRenderRotation(FRotator(0.0, 30.0, 15.0));
	Card->SetRenderLayerMode(EDreamWidgetRenderLayer::Always);
	Rig.PumpFrames(3);
	TestTrue(TEXT("The card is a layer"), Card->IsRenderLayer());
	TestTrue(TEXT("...and the button is in it"), Button->GetRenderLayer() == Card);

	FDreamElementRef Play = Rig.Driver()->Find(FDreamBy::Widget(Button));
	TestTrue(TEXT("Clicking the button where the turned card puts it completes"), Play->Click());
	TestEqual(TEXT("...and clicks it once"), Listener->ClickedCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRenderLayerQuietTest,
	"DreamGUI.RenderLayer.WhatAQuietLayerHoldsIsNotWalkedWhenItMovesAndStandsWhereTheLayerTookItWhenRead",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRenderLayerQuietTest::RunTest(const FString& Parameters)
{
	using namespace DreamRenderLayerTestLocal;
	const DreamTests::Lifecycle::FScopedConsoleVariable Layers(TEXT("r.DreamUI.RenderLayers"), 1);
	const DreamTests::Lifecycle::FScopedConsoleVariable Promote(TEXT("r.DreamUI.RenderLayerPromoteFrames"), 2);
	const DreamTests::Lifecycle::FScopedConsoleVariable Quiet(TEXT("r.DreamUI.QuietRenderLayers"), 1);
	FStage Stage;
	if (!Stage.Build(*this))
	{
		return false;
	}
	Stage.TurnCard(10.0);
	Stage.TurnCard(20.0);
	if (!TestTrue(TEXT("Turned on two frames in a row, the card is a layer"), Stage.Card->IsRenderLayer()))
	{
		return false;
	}
	TestTrue(TEXT("Nothing on it hosts a canvas, is a layer or listens: it is quiet"), Stage.Card->IsRenderLayerQuiet());
	const FTransform FaceOnCard = Stage.Face->GetWorldTransform().GetRelativeTransform(Stage.Card->GetWorldTransform());

	// Turned again: only the card is walked. The face moved with it, and is left stale until something reads it.
	Stage.TurnCard(30.0);
	TestFalse(TEXT("The card's move was announced"), Stage.Card->IsTransformChangePending());
	TestTrue(TEXT("The face's move with it was not"), Stage.Face->IsTransformChangePending());
	TestTrue(TEXT("...nor was the face composed"), Stage.Face->IsWorldTransformDirty());
	TestTrue(TEXT("Read, the face stands where the card took it"),
		Stage.Face->GetWorldTransform().GetRelativeTransform(Stage.Card->GetWorldTransform()).Equals(FaceOnCard, UE_KINDA_SMALL_NUMBER));

	// A listener on the face is told of every move, so the card is not quiet while it listens.
	int32 Heard = 0;
	const FDelegateHandle Listening = Stage.Face->GetTransformChangedEvent().AddLambda([&Heard]() { ++Heard; });
	TestFalse(TEXT("With a listener on the face, the card is not quiet"), Stage.Card->IsRenderLayerQuiet());
	Stage.TurnCard(40.0);
	TestEqual(TEXT("The listener heard the card's turn"), Heard, 1);
	TestFalse(TEXT("...and the face's move was announced"), Stage.Face->IsTransformChangePending());
	Stage.Face->GetTransformChangedEvent().Remove(Listening);
	TestTrue(TEXT("Its listener gone, the card is quiet again"), Stage.Card->IsRenderLayerQuiet());

	// Moved on its own while the card turns, the face is announced from itself, and its geometry is taken to where it now
	// is in the layer.
	Stage.TurnCard(50.0);
	const FDreamUIGeometry* FaceGeometry = Stage.GeometryOf(Stage.Face);
	if (!TestTrue(TEXT("The face has a geometry"), FaceGeometry != nullptr && FaceGeometry->Vertices.Num() > 0))
	{
		return false;
	}
	const FVector3f CornerBefore = FaceGeometry->Vertices[0].Position;
	Stage.Face->SetAnchoredPosition(Stage.Face->GetAnchoredPosition() + FVector2D(10.0, 0.0));
	Stage.TurnCard(60.0);
	TestFalse(TEXT("The face that moved on its own was announced"), Stage.Face->IsTransformChangePending());
	FaceGeometry = Stage.GeometryOf(Stage.Face);
	TestTrue(TEXT("...and its vertices, relative to the card, moved with it"),
		FaceGeometry != nullptr && FaceGeometry->Vertices.Num() > 0 && !FaceGeometry->Vertices[0].Position.Equals(CornerBefore));

	// Switched off, every widget under a moved layer is walked, as it was.
	{
		const DreamTests::Lifecycle::FScopedConsoleVariable Loud(TEXT("r.DreamUI.QuietRenderLayers"), 0);
		Stage.TurnCard(70.0);
		TestFalse(TEXT("With quiet layers switched off, the face's move with the card is announced"), Stage.Face->IsTransformChangePending());
	}
	return true;
}

namespace DreamRenderLayerTestLocal
{
	/** What a run of turning cards left: each panel's mesh bounds after every frame, and the counters over the run. */
	struct FPlacedRun
	{
		TArray<FBox> Bounds;
		int64 Promotions = 0;
		int64 Moves = 0;
	};

	/** Forty panels, each with a card turned on every frame, placed on the game thread or also on the workers. */
	bool TurnManyCards(FAutomationTestBase& InTest, int32 InParallelMinCanvases, FPlacedRun& OutRun)
	{
		const DreamTests::Lifecycle::FScopedConsoleVariable Layers(TEXT("r.DreamUI.RenderLayers"), 1);
		const DreamTests::Lifecycle::FScopedConsoleVariable Promote(TEXT("r.DreamUI.RenderLayerPromoteFrames"), 2);
		const DreamTests::Lifecycle::FScopedConsoleVariable Parallel(TEXT("r.DreamUI.ParallelLayerPlacementMinCanvases"), InParallelMinCanvases);
		FStage Stage;
		if (!Stage.Build(InTest))
		{
			return false;
		}
		constexpr int32 PanelCount = 40;
		TArray<UDreamCanvas*> Canvases = { Stage.Canvas };
		TArray<UDreamWidget*> Cards = { Stage.Card };
		for (int32 Index = 1; Index < PanelCount; ++Index)
		{
			UDreamWidget* Root = Stage.MakeWidget(nullptr, 400.0f, 400.0f, FVector2D::ZeroVector);
			UDreamCanvas* Canvas = Root->AddComponent<UDreamCanvas>();
			if (!InTest.TestNotNull(TEXT("A canvas on each panel"), Canvas))
			{
				return false;
			}
			Canvas->SetRenderMode(EDreamRenderMode::WorldSpace_DreamUI);
			UDreamWidget* Card = Stage.MakeBlock(Root, 100.0f + Index, 60.0f, FVector2D(-50.0, 0.0), FColor::Red);
			Stage.MakeBlock(Card, 40.0f, 20.0f, FVector2D(10.0, 5.0), FColor::Green);
			Canvases.Add(Canvas);
			Cards.Add(Card);
		}
		Stage.Frames(3);
		ResetCounters();
		for (int32 Step = 1; Step <= 5; ++Step)
		{
			for (int32 Index = 0; Index < PanelCount; ++Index)
			{
				Cards[Index]->SetRenderRotation(FRotator(5.0 * Step, 10.0 * Step + Index, 0.0));
			}
			Stage.Frames(1);
			for (UDreamCanvas* Canvas : Canvases)
			{
				const UDreamUIMeshComponent* Mesh = Canvas->GetUIMesh();
				OutRun.Bounds.Add(Mesh != nullptr ? Mesh->Bounds.GetBox() : FBox(ForceInit));
			}
		}
		const FCounted Counted;
		OutRun.Promotions = Counted.Get(DreamUIRenderStats::ECounter::RenderLayerPromotions);
		OutRun.Moves = Counted.Get(DreamUIRenderStats::ECounter::RenderLayerMoves);
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRenderLayerParallelPlacementTest,
	"DreamGUI.RenderLayer.LayersPlacedTogetherOnWorkersEndWhereTheyDoPlacedOneAfterAnother",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRenderLayerParallelPlacementTest::RunTest(const FString& Parameters)
{
	using namespace DreamRenderLayerTestLocal;
	FPlacedRun OneAfterAnother;
	FPlacedRun Together;
	if (!TurnManyCards(*this, 0, OneAfterAnother) || !TurnManyCards(*this, 1, Together))
	{
		return false;
	}
	TestEqual(TEXT("Every card became a layer, one after another"), OneAfterAnother.Promotions, static_cast<int64>(40));
	TestEqual(TEXT("...and together"), Together.Promotions, OneAfterAnother.Promotions);
	TestTrue(TEXT("The layers moved"), OneAfterAnother.Moves > 0);
	TestEqual(TEXT("...as often together as one after another"), Together.Moves, OneAfterAnother.Moves);
	if (!TestEqual(TEXT("Both runs saw every panel every frame"), Together.Bounds.Num(), OneAfterAnother.Bounds.Num()))
	{
		return false;
	}
	for (int32 Index = 0; Index < Together.Bounds.Num(); ++Index)
	{
		const FBox& A = OneAfterAnother.Bounds[Index];
		const FBox& B = Together.Bounds[Index];
		TestTrue(FString::Printf(TEXT("Panel %d, frame %d: the mesh bounds follow the card the same way"), Index % 40, Index / 40 + 1),
			A.IsValid == B.IsValid && A.Min.Equals(B.Min, 1.e-3) && A.Max.Equals(B.Max, 1.e-3));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRenderLayerManyOnOneCanvasTest,
	"DreamGUI.RenderLayer.ManyTurningWidgetsOnOneCanvasAreLayersDrawnTogether",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRenderLayerManyOnOneCanvasTest::RunTest(const FString& Parameters)
{
	using namespace DreamRenderLayerTestLocal;
	using DreamUIRenderStats::ECounter;
	const DreamTests::Lifecycle::FScopedConsoleVariable Layers(TEXT("r.DreamUI.RenderLayers"), 1);
	const DreamTests::Lifecycle::FScopedConsoleVariable Promote(TEXT("r.DreamUI.RenderLayerPromoteFrames"), 2);
	FStage Stage;
	if (!Stage.Build(*this))
	{
		return false;
	}
	// Forty cards more beside the stage's, each with a face, all turning every frame: more layers than a canvas held when
	// each layer had draw calls of its own.
	constexpr int32 CardCount = 40;
	TArray<UDreamWidget*> Cards = { Stage.Card };
	for (int32 Index = 1; Index < CardCount; ++Index)
	{
		UDreamWidget* Card = Stage.MakeBlock(Stage.Root, 20.0f, 20.0f, FVector2D(-180.0 + 9.0 * Index, -150.0), FColor::Red);
		Stage.MakeBlock(Card, 10.0f, 10.0f, FVector2D(2.0, 2.0), FColor::Green);
		Cards.Add(Card);
	}
	Stage.Frames(2);
	auto TurnAll = [&Stage, &Cards](double InYaw)
	{
		for (int32 Index = 0; Index < Cards.Num(); ++Index)
		{
			Cards[Index]->SetRenderRotation(FRotator(0.0, InYaw + Index, 0.0));
		}
		Stage.Frames(1);
	};
	TurnAll(10.0);
	TurnAll(20.0);
	int32 LayerCount = 0;
	for (const UDreamWidget* Card : Cards)
	{
		LayerCount += Card->IsRenderLayer() ? 1 : 0;
	}
	TestEqual(TEXT("Every turning card is a layer"), LayerCount, CardCount);
	ResetCounters();
	for (int32 Step = 3; Step <= 6; ++Step)
	{
		TurnAll(10.0 * Step);
	}
	const FCounted Turning;
	TestEqual(TEXT("Four more frames of turning rebuild nothing"), Turning.Get(ECounter::DrawCallRebuilds), static_cast<int64>(0));
	TestEqual(TEXT("...patch no section"), Turning.Get(ECounter::SectionPatches), static_cast<int64>(0));
	TestEqual(TEXT("...and move every layer once a frame"), Turning.Get(ECounter::RenderLayerMoves), static_cast<int64>(CardCount * 4));
	// The draw calls in hand: the stage's card and its face in one; the block that stands still after it opens another, which
	// every card after it goes into with its face. Two draw calls for forty layers, a run of each layer's elements apiece.
	const UDreamUIMeshComponent* Mesh = Stage.Canvas->GetUIMesh();
	TestTrue(TEXT("The canvas has a mesh"), Mesh != nullptr);
	int32 LayerDrawCalls = 0;
	int32 LayerRuns = 0;
	for (const FDreamUIDrawCall& DrawCall : Stage.Canvas->CurrentDrawCallData.DrawCallArray)
	{
		if (DrawCall.HasRenderLayerElements())
		{
			++LayerDrawCalls;
			LayerRuns += DrawCall.LayerBounds.Num();
		}
	}
	TestEqual(TEXT("The forty layers' elements are drawn in two draw calls"), LayerDrawCalls, 2);
	TestEqual(TEXT("...boxed a layer at a time"), LayerRuns, CardCount);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRenderLayerRefreshBeforeSensitiveTest,
	"DreamGUI.RenderLayer.AMoveBeforeWhereTheBatchLookedBackIsRefreshedInPlace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRenderLayerRefreshBeforeSensitiveTest::RunTest(const FString& Parameters)
{
	using namespace DreamRenderLayerTestLocal;
	using DreamUIRenderStats::ECounter;
	// On the CPU: the card turns as a 3D element whose vertices are refreshed in place.
	const DreamTests::Lifecycle::FScopedConsoleVariable Layers(TEXT("r.DreamUI.RenderLayers"), 0);
	FStage Stage;
	if (!Stage.Build(*this))
	{
		return false;
	}
	// After the card, the block and two more that stand still: one of another texture, and one white again that goes back
	// past it into the block's draw call -- a batch that depends on positions, from the block's draw call on.
	const TStrongObjectPtr<UTexture2D> Other(UTexture2D::CreateTransient(4, 4));
	UDreamWidget* OtherBlock = Stage.MakeWidget(Stage.Root, 30.0f, 30.0f, FVector2D(-150.0, 150.0));
	if (UDreamTexture* Visual = OtherBlock->CreateNewVisual<UDreamTexture>())
	{
		Visual->SetTexture(Other.Get());
	}
	Stage.MakeBlock(Stage.Root, 30.0f, 30.0f, FVector2D(150.0, -150.0), FColor::White);
	Stage.Frames(2);
	Stage.TurnCard(10.0);
	Stage.TurnCard(20.0);
	const FDreamUIBatchPlacement& Placement = Stage.Canvas->CurrentDrawCallData.Placement;
	TestFalse(TEXT("The batch depended on positions"), Placement.bIndependentOfPositions);
	TestTrue(TEXT("...only after the turning card's draw call"), Placement.PositionSensitiveFrom >= 1);
	ResetCounters();
	for (int32 Step = 3; Step <= 7; ++Step)
	{
		Stage.TurnCard(10.0 * Step);
	}
	const FCounted Turning;
	TestEqual(TEXT("The card's turns rebuild nothing"), Turning.Get(ECounter::DrawCallRebuilds), static_cast<int64>(0));
	TestEqual(TEXT("...and each is refreshed in place"), Turning.Get(ECounter::InPlaceRefreshes), static_cast<int64>(5));
	return true;
}

/*
 * A canvas keeps its render layers' widgets and its own widget, and the UI manager its canvases, as weak look-ups found
 * them while the count of objects gone reads the same (DreamUIGone). With r.DreamUI.VerifyKeptPointers every use looks
 * them up as well. A card turns until it is a layer, is destroyed while it turns and another takes its place, and another
 * canvas comes and goes beside the stage's, round after round: no kept pointer ever disagrees with its look-up.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRenderLayerKeptPointersTest,
	"DreamGUI.RenderLayer.KeptPointersAgreeWithTheirLookUpsAsLayersAndCanvasesComeAndGo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRenderLayerKeptPointersTest::RunTest(const FString& Parameters)
{
	using namespace DreamRenderLayerTestLocal;
	const DreamTests::Lifecycle::FScopedConsoleVariable Verify(TEXT("r.DreamUI.VerifyKeptPointers"), 1);
	const uint64 Before = DreamUIGone::GetKeptDisagreements();
	FStage Stage;
	if (!Stage.Build(*this))
	{
		return false;
	}
	for (int32 Round = 0; Round < 6; ++Round)
	{
		// The card turns until it is a layer...
		for (int32 Step = 1; Step <= 4; ++Step)
		{
			Stage.TurnCard(Round * 30.0 + Step * 5.0);
		}
		// ...and goes while it turns, its face with it; another takes its place.
		Stage.Card->DestroyWidget();
		Stage.Face = nullptr;
		Stage.Card = Stage.MakeBlock(Stage.Root, 120.0f, 80.0f, FVector2D(-80.0, 0.0), FColor::Red);
		Stage.Frames(1);
		// Another canvas comes and goes beside the stage's.
		UDreamWidget* OtherRoot = Stage.MakeWidget(nullptr, 100.0f, 100.0f, FVector2D::ZeroVector);
		OtherRoot->AddComponent<UDreamCanvas>();
		Stage.Frames(1);
		OtherRoot->DestroyWidget();
		Stage.Frames(1);
	}
	TestEqual(TEXT("No kept pointer disagreed with its look-up"),
		static_cast<int64>(DreamUIGone::GetKeptDisagreements() - Before), static_cast<int64>(0));
	return true;
}

#endif
