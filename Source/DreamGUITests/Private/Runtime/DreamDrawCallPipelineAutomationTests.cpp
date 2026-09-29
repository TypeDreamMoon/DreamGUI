// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "HAL/PlatformProcess.h"

#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamVisualBatchMesh.h"
#include "Core/Components/DreamVisualEmpty.h"
#include "Core/DreamCanvasDrawCallProcessingRunnable.h"
#include "Core/DreamUIDrawCall.h"
#include "Core/DreamUIGeometry.h"
#include "Engine/Texture2D.h"

/*
 * The stages between "the canvas has geometry" and "the mesh component has sections": the off-thread
 * batching worker, the cheap per-frame refresh of an already-built draw-call, and the vertex
 * transform that feeds both.
 *
 * None of these touch the RHI, and the pieces exercised here take plain data rather than live
 * widgets, so they run under -nullrhi with no world and no Slate. That is deliberate: under NullRHI
 * a canvas produces no mesh sections at all, so anything asserted further down the pipeline would be
 * asserted against nothing.
 */
namespace DreamDrawCallPipelineTestLocal
{
	/** A quad, as the geometry stage leaves it: vertices, triangles and canvas-space bounds. */
	FDreamUIGeometry MakeQuad(const FVector2D& InBoundsMin, const FVector2D& InBoundsMax)
	{
		FDreamUIGeometry Geo;
		Geo.Vertices.Add(FDreamUIMeshVertex(FVector3f(0.0f, (float)InBoundsMin.X, (float)InBoundsMin.Y)));
		Geo.Vertices.Add(FDreamUIMeshVertex(FVector3f(0.0f, (float)InBoundsMax.X, (float)InBoundsMin.Y)));
		Geo.Vertices.Add(FDreamUIMeshVertex(FVector3f(0.0f, (float)InBoundsMin.X, (float)InBoundsMax.Y)));
		Geo.Vertices.Add(FDreamUIMeshVertex(FVector3f(0.0f, (float)InBoundsMax.X, (float)InBoundsMax.Y)));
		Geo.Triangles = { 0, 3, 2, 0, 1, 3 };
		Geo.BoundsMin2DInCanvasSpace = InBoundsMin;
		Geo.BoundsMax2DInCanvasSpace = InBoundsMax;
		Geo.bSupportDrawcallBatching = true;
		return Geo;
	}

	/** The canvas rect every test here batches against; wide enough to hold any rect they place. */
	const FVector2D CanvasLeftBottom(-500.0, -500.0);
	const FVector2D CanvasRightTop(500.0, 500.0);

	/**
	 * Wraps a geometry the way UDreamCanvas::PrepareDrawCallBatchingData does, copy included -- so
	 * these go through CopyDataForPrepare rather than around it. A field the copy drops is a field the
	 * batcher never sees, which is exactly how the 3D path once came to be dead code.
	 */
	FDreamUIRenderData MakeBatchMeshRenderData(const FDreamUIGeometry& InGeo)
	{
		FDreamUIRenderData RenderData(EDreamUIDrawCallType::BatchMesh);
		const TSharedRef<FDreamUIGeometry> Prepared = MakeShared<FDreamUIGeometry>();
		Prepared->CopyDataForPrepare(InGeo);
		RenderData.BatchMeshGeometry = Prepared;
		return RenderData;
	}

	/** Batch against the canvas rect above, with element culling off -- the default everywhere but a root canvas. */
	void Batch(const TArray<FDreamUIRenderData>& InRenderDataArray, TArray<FDreamUIDrawCall>& OutDrawCallList)
	{
		UDreamCanvas::BatchDrawCallAsync(CanvasLeftBottom, CanvasRightTop, InRenderDataArray, OutDrawCallList);
	}

	/**
	 * A visual whose geometry is InGeo. Rooted for the duration of the test through the array the
	 * caller keeps: these are plain NewObject'd visuals with no widget and no canvas, which is all
	 * the refresh path reads them for.
	 *
	 * UDreamVisualBatchMesh itself is abstract, so the concrete UDreamVisualEmpty stands in -- it is
	 * the lightest subclass there is (its constructor does nothing, and it owns no sprite, material
	 * or data asset), and the refresh path only ever asks a visual for its geometry.
	 */
	UDreamVisualBatchMesh* MakeVisualWithGeometry(const FDreamUIGeometry& InGeo)
	{
		UDreamVisualBatchMesh* Visual = NewObject<UDreamVisualEmpty>();
		if (Visual != nullptr && Visual->GetGeometry() != nullptr)
		{
			*Visual->GetGeometry() = InGeo;
		}
		return Visual;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDrawCallBatchingWorkerNoEmptyResultTest,
	"DreamGUI.Canvas.TheBatchingWorkerNeverPublishesAResultItWasNotGivenDataFor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDrawCallBatchingWorkerNoEmptyResultTest::RunTest(const FString& Parameters)
{
	/*
	 * The worker is woken once per push, but a push that lands while a batch is already in flight
	 * leaves its data for that batch to drain -- so a wake can legitimately find the queue already
	 * empty. It used to batch the default-constructed data anyway: an empty draw-call list stamped
	 * FrameNumber 0, which blanked the canvas for a frame and, because the cheap refresh path's only
	 * test is CurrentDrawCallData.FrameNumber == NewestDrawCallFrameNumber, also stopped that path
	 * from running until the next full rebuild.
	 */
	FDreamCanvasDrawCallProcessingRunnable Runnable;
	Runnable.Start();

	FDreamCanvasPreparedDrawCallData FirstPush;
	FirstPush.LeftBottomPoint = FVector2D(-500.0, -500.0);
	FirstPush.RightTopPoint = FVector2D(500.0, 500.0);
	FirstPush.FrameNumber = 100;
	FDreamUIRenderData RenderData = DreamDrawCallPipelineTestLocal::MakeBatchMeshRenderData(
		DreamDrawCallPipelineTestLocal::MakeQuad(FVector2D(-10.0, -10.0), FVector2D(10.0, 10.0)));
	FirstPush.DataArray.Add(RenderData);

	FDreamCanvasPreparedDrawCallData SecondPush = FirstPush;
	SecondPush.FrameNumber = 200;

	Runnable.PushPreparedDrawCallData(MoveTemp(FirstPush));
	Runnable.PushPreparedDrawCallData(MoveTemp(SecondPush));

	/*
	 * How the worker splits two pushes between batches is its business -- one drain may take both, or
	 * the second may arrive while the first is in flight and be picked up by a follow-up. So collect
	 * everything it publishes until the newest push has come back, and assert about the set: every
	 * result belongs to data that was actually pushed, and the newest one is the newest push.
	 */
	uint64 NewestSeen = 0;
	int32 EmptyResultCount = 0;
	int32 ResultCount = 0;
	for (int32 Attempt = 0; Attempt < 500 && NewestSeen < 200; Attempt++)
	{
		Runnable.WaitForBatchingToFinish();
		FDreamCanvasPendingDrawCallData Result;
		if (Runnable.TryGetDrawCallData(Result))
		{
			ResultCount++;
			if (Result.FrameNumber == 0)
			{
				EmptyResultCount++;
			}
			NewestSeen = FMath::Max(NewestSeen, Result.FrameNumber);
			if (Result.FrameNumber != 0)
			{
				TestEqual(TEXT("A published batch holds the quad it was given, as one draw-call"), Result.DrawCallArray.Num(), 1);
			}
			continue;
		}
		FPlatformProcess::Sleep(0.001f);
	}

	TestTrue(TEXT("The worker published something"), ResultCount > 0);
	// The regression. A wake that found the queue already drained used to batch the default-constructed
	// data: an empty draw-call list stamped FrameNumber 0, which blanked the canvas for a frame and
	// stalled the cheap refresh path until the next full rebuild.
	TestEqual(TEXT("No batch was published for data the worker was never given"), EmptyResultCount, 0);
	TestEqual(TEXT("The newest frame pushed is what came back"), NewestSeen, (uint64)200);

	// And once everything pushed has been taken, nothing further appears on its own.
	Runnable.WaitForBatchingToFinish();
	FDreamCanvasPendingDrawCallData Leftover;
	TestFalse(TEXT("Nothing else was published"), Runnable.TryGetDrawCallData(Leftover));

	Runnable.Stop();
	TestFalse(TEXT("A stopped worker reports no batching in flight"), Runnable.IsBatching());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDrawCallTransformVerticesNeedsNoObjectsTest,
	"DreamGUI.Canvas.TransformingVerticesReadsOnlyItsParametersAndTheGeometry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDrawCallTransformVerticesNeedsNoObjectsTest::RunTest(const FString& Parameters)
{
	/*
	 * The transform runs on a worker task. It used to be handed the canvas and the visual as bare
	 * pointers and call GetWorldTransform / GetGeometryBoundsInLocalSpace /
	 * GetActualRequireNormalAndTangent on them from there, which races with garbage collection. This
	 * asserts the shape of the fix as much as the arithmetic: the whole transform is expressible
	 * against a parameter struct and a geometry, with no UObject in reach.
	 */
	FDreamUIGeometry Geo;
	Geo.OriginVertices.Add(FDreamUIOriginVertexData(FVector3f(0.0f, -10.0f, -20.0f)));
	Geo.OriginVertices.Add(FDreamUIOriginVertexData(FVector3f(0.0f, 30.0f, 40.0f)));
	Geo.Vertices.AddDefaulted(2);

	FDreamUIGeometry::FTransformVerticesParams Params;
	Params.InverseCanvasTransform = FTransform::Identity;
	//canvas space is X-into-the-screen, so a widget offset lives in Y (horizontal) and Z (vertical)
	Params.ItemWorldTransform = FTransform(FVector(0.0, 100.0, 200.0));
	Params.LocalBoundsMin = FVector2D(-10.0, -20.0);
	Params.LocalBoundsMax = FVector2D(30.0, 40.0);

	FDreamUIGeometry::TransformVertices(Params, &Geo);

	TestEqual(TEXT("The first vertex moved into canvas space"), FVector(Geo.Vertices[0].Position), FVector(0.0, 90.0, 180.0));
	TestEqual(TEXT("The second vertex moved into canvas space"), FVector(Geo.Vertices[1].Position), FVector(0.0, 130.0, 240.0));
	TestEqual(TEXT("The canvas-space bounds came from the local bounds in the parameters, horizontally"), Geo.BoundsMin2DInCanvasSpace.X, 90.0);
	TestEqual(TEXT("...and vertically"), Geo.BoundsMin2DInCanvasSpace.Y, 180.0);
	TestEqual(TEXT("...and the far corner horizontally"), Geo.BoundsMax2DInCanvasSpace.X, 130.0);
	TestEqual(TEXT("...and the far corner vertically"), Geo.BoundsMax2DInCanvasSpace.Y, 240.0);
	TestEqual(TEXT("The transform relative to the canvas is recorded for the batcher"), Geo.TransformRelativeToCanvas.GetLocation(), FVector(0.0, 100.0, 200.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDrawCallRefreshSkipsTheSameGeometryTheBatchDidTest,
	"DreamGUI.Canvas.AVertexRefreshSkipsTheSameTrianglelessGeometryTheBatchLeftOut",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDrawCallRefreshSkipsTheSameGeometryTheBatchDidTest::RunTest(const FString& Parameters)
{
	using namespace DreamDrawCallPipelineTestLocal;

	/*
	 * ApplyBatchMeshGeometryToCombined builds the combined buffer and leaves out any geometry with no
	 * triangles. CopyBatchMeshGeometry -- the cheap path that re-reads vertices without rebuilding
	 * the draw-call -- used to walk every geometry, so from the first triangleless one onwards it
	 * wrote each geometry at the offset belonging to the previous one. The visible symptom was a
	 * frame of displaced vertices every so often, never reproducible on a rebuild.
	 */
	FDreamUIGeometry FirstGeo = MakeQuad(FVector2D(-10.0, -10.0), FVector2D(10.0, 10.0));
	FDreamUIGeometry EmptyGeo = MakeQuad(FVector2D(0.0, 0.0), FVector2D(0.0, 0.0));
	EmptyGeo.Triangles.Reset();//a widget whose geometry collapsed to nothing this frame
	FDreamUIGeometry LastGeo = MakeQuad(FVector2D(50.0, 50.0), FVector2D(70.0, 70.0));

	TArray<UDreamVisualBatchMesh*> Visuals;
	Visuals.Add(MakeVisualWithGeometry(FirstGeo));
	Visuals.Add(MakeVisualWithGeometry(EmptyGeo));
	Visuals.Add(MakeVisualWithGeometry(LastGeo));
	for (UDreamVisualBatchMesh* Visual : Visuals)
	{
		if (!TestNotNull(TEXT("Visual created"), Visual))
		{
			return false;
		}
	}

	FDreamUIDrawCall DrawCall(DreamUIQuadTree::Rectangle(FVector2D(-500.0, -500.0), FVector2D(500.0, 500.0)));
	DrawCall.BatchMeshGeometryArray = { MakeShared<FDreamUIGeometry>(FirstGeo), MakeShared<FDreamUIGeometry>(EmptyGeo), MakeShared<FDreamUIGeometry>(LastGeo) };
	DrawCall.BatchMeshVisualArray = { Visuals[0], Visuals[1], Visuals[2] };
	DrawCall.VerticesCount = FirstGeo.Vertices.Num() + EmptyGeo.Vertices.Num() + LastGeo.Vertices.Num();
	DrawCall.IndicesCount = FirstGeo.Triangles.Num() + EmptyGeo.Triangles.Num() + LastGeo.Triangles.Num();
	DrawCall.ApplyBatchMeshGeometryToCombined();

	const int32 CombinedCount = DrawCall.CombinedBatchMeshGeometryVertices.Num();
	if (!TestEqual(TEXT("The combined buffer holds only the two geometries that have triangles"), CombinedCount, 8))
	{
		return false;
	}
	//where the last geometry's first vertex sits, as the batch laid the buffer out
	const FVector3f BuiltLastFirstVertex = DrawCall.CombinedBatchMeshGeometryVertices[4].Position;

	//now the refresh: the visuals' live vertices are what it re-reads, so mark them apart
	for (int32 VertexIndex = 0; VertexIndex < 4; VertexIndex++)
	{
		Visuals[0]->GetGeometry()->Vertices[VertexIndex].Position.X = 1.0f;
		Visuals[2]->GetGeometry()->Vertices[VertexIndex].Position.X = 3.0f;
	}
	DrawCall.CopyBatchMeshGeometry();

	TestEqual(TEXT("The refresh did not resize the combined buffer"), DrawCall.CombinedBatchMeshGeometryVertices.Num(), CombinedCount);
	TestEqual(TEXT("The first geometry refreshed into its own slot"), DrawCall.CombinedBatchMeshGeometryVertices[0].Position.X, 1.0f);
	TestEqual(TEXT("The last geometry refreshed into the slot the batch gave it, not the skipped one's"),
		DrawCall.CombinedBatchMeshGeometryVertices[4].Position.X, 3.0f);
	TestEqual(TEXT("...which is where it already was"),
		DrawCall.CombinedBatchMeshGeometryVertices[4].Position.Y, BuiltLastFirstVertex.Y);
	TestEqual(TEXT("...in both directions"),
		DrawCall.CombinedBatchMeshGeometryVertices[4].Position.Z, BuiltLastFirstVertex.Z);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDrawCallUnchangedGeometryIsNotCopiedAgainTest,
	"DreamGUI.Canvas.AGeometryThatDidNotChangeIsHandedToTheBatchingAsTheSameCopy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDrawCallUnchangedGeometryIsNotCopiedAgainTest::RunTest(const FString& Parameters)
{
	using namespace DreamDrawCallPipelineTestLocal;

	/*
	 * Preparing a canvas for batching copied every element's geometry each time anything in the canvas changed, so a
	 * panel where one widget moved paid for copying all of them. The visual keeps the copy it made and hands the same
	 * one over while its geometry still holds what the copy holds. The copy itself is never written: a batch on a
	 * worker thread, and the draw calls it builds, go on reading it after the geometry has moved on.
	 */
	UDreamVisualBatchMesh* Visual = MakeVisualWithGeometry(MakeQuad(FVector2D(-10.0, -10.0), FVector2D(10.0, 10.0)));
	if (!TestNotNull(TEXT("Visual created"), Visual))
	{
		return false;
	}
	const TSharedPtr<const FDreamUIGeometry> First = Visual->GetGeometryForBatching();
	if (!TestTrue(TEXT("The visual hands over a copy"), First.IsValid()))
	{
		return false;
	}
	TestTrue(TEXT("...which is not the geometry itself"), First.Get() != Visual->GetGeometry());
	TestTrue(TEXT("Asked again with nothing changed, it hands over the same copy"), Visual->GetGeometryForBatching() == First);

	Visual->GetGeometry()->Vertices[0].Color = FColor::Red;
	const TSharedPtr<const FDreamUIGeometry> AfterColour = Visual->GetGeometryForBatching();
	TestTrue(TEXT("A vertex changed: a new copy"), AfterColour.IsValid() && AfterColour != First);
	TestTrue(TEXT("...that holds the change"), AfterColour.IsValid() && AfterColour->Vertices[0].Color == FColor::Red);
	TestTrue(TEXT("...while the copy a batch may still be reading is as it was"), First->Vertices[0].Color != FColor::Red);

	Visual->GetGeometry()->BlendMode = EDreamUIBlendMode::Additive;
	TestTrue(TEXT("A batching key changed with no vertex changing: a new copy as well"), Visual->GetGeometryForBatching() != AfterColour);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDrawCallRefreshLeavesUnchangedDrawCallsTest,
	"DreamGUI.Canvas.AVertexRefreshLeavesADrawCallWhoseElementsDidNotChangeAsItWas",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDrawCallRefreshLeavesUnchangedDrawCallsTest::RunTest(const FString& Parameters)
{
	using namespace DreamDrawCallPipelineTestLocal;

	/*
	 * The cheap refresh -- a colour or an alpha changed, nothing moved -- copied every draw call's vertices out of its
	 * visuals and uploaded them all again. Each visual hands over its copy of its geometry now, the same copy while
	 * nothing in it changed, so a draw call none of whose visuals changed has nothing to copy and nothing to upload.
	 */
	UDreamVisualBatchMesh* First = MakeVisualWithGeometry(MakeQuad(FVector2D(-10.0, -10.0), FVector2D(10.0, 10.0)));
	UDreamVisualBatchMesh* Second = MakeVisualWithGeometry(MakeQuad(FVector2D(20.0, 20.0), FVector2D(40.0, 40.0)));
	if (!TestNotNull(TEXT("A first visual"), First) || !TestNotNull(TEXT("...and a second"), Second))
	{
		return false;
	}
	FDreamUIDrawCall DrawCall(DreamUIQuadTree::Rectangle(FVector2D(-500.0, -500.0), FVector2D(500.0, 500.0)));
	DrawCall.BatchMeshGeometryArray = { First->GetGeometryForBatching(), Second->GetGeometryForBatching() };
	DrawCall.BatchMeshVisualArray = { First, Second };
	DrawCall.VerticesCount = 8;
	DrawCall.IndicesCount = 12;
	DrawCall.ApplyBatchMeshGeometryToCombined();
	if (!TestEqual(TEXT("The combined buffer holds both quads"), DrawCall.CombinedBatchMeshGeometryVertices.Num(), 8))
	{
		return false;
	}

	TestFalse(TEXT("Nothing changed: the refresh leaves the draw call as it was"), DrawCall.CopyBatchMeshGeometry());

	Second->GetGeometry()->Vertices[0].Color = FColor::Red;
	TestTrue(TEXT("A vertex of the second visual changed: the draw call is refreshed"), DrawCall.CopyBatchMeshGeometry());
	TestEqual(TEXT("...its vertex, where the batch put it"), DrawCall.CombinedBatchMeshGeometryVertices[4].Color, FColor::Red);
	TestTrue(TEXT("...and the draw call holds the visual's new copy"), DrawCall.BatchMeshGeometryArray[1] == Second->GetGeometryForBatching());
	TestTrue(TEXT("...whose triangles are the ones the buffer has"), DrawCall.bTrianglesAsBuilt);
	TestFalse(TEXT("Asked again with nothing new, it has nothing to do"), DrawCall.CopyBatchMeshGeometry());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDrawCallCombineLeftForATakenBackSectionTest,
	"DreamGUI.Canvas.TheBatchingLeavesUncombinedADrawCallWhoseSectionWillBeTakenBackAsItIs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDrawCallCombineLeftForATakenBackSectionTest::RunTest(const FString& Parameters)
{
	using namespace DreamDrawCallPipelineTestLocal;

	/*
	 * Combining a draw call's geometries into one buffer is most of what the batching does, and a draw call built from
	 * the very copies one of the canvas's sections was built from does not need it: the section is taken back as it is.
	 * The batching leaves such a draw call's buffers empty, works out its bounds alone, and says so; anything that
	 * reads the buffers combines them first. Two quads of different textures make two draw calls; the first is one the
	 * canvas already has a section for.
	 */
	FDreamUIGeometry Left = MakeQuad(FVector2D(-40.0, -10.0), FVector2D(-20.0, 10.0));
	Left.Texture = UTexture2D::CreateTransient(4, 4);
	FDreamUIGeometry Right = MakeQuad(FVector2D(20.0, -10.0), FVector2D(40.0, 10.0));
	Right.Texture = UTexture2D::CreateTransient(4, 4);
	TArray<FDreamUIRenderData> RenderDataArray;
	RenderDataArray.Add(MakeBatchMeshRenderData(Left));
	RenderDataArray.Add(MakeBatchMeshRenderData(Right));
	const TArray<TArray<TSharedPtr<const FDreamUIGeometry>>> OnSections = { { RenderDataArray[0].BatchMeshGeometry } };

	TArray<FDreamUIDrawCall> DrawCallList;
	UDreamCanvas::BatchDrawCallAsync(CanvasLeftBottom, CanvasRightTop, MoveTemp(RenderDataArray), DrawCallList, false, &OnSections);
	if (!TestEqual(TEXT("Two textures, two draw calls"), DrawCallList.Num(), 2))
	{
		return false;
	}
	TestTrue(TEXT("The draw call a section holds already is left uncombined"), DrawCallList[0].bCombinePending);
	TestEqual(TEXT("...its buffer empty"), DrawCallList[0].CombinedBatchMeshGeometryVertices.Num(), 0);
	TestTrue(TEXT("...but its bounds worked out"), DrawCallList[0].CombinedBounds.IsValid != 0);
	TestFalse(TEXT("The other is combined as ever"), DrawCallList[1].bCombinePending);
	TestEqual(TEXT("...its buffer full"), DrawCallList[1].CombinedBatchMeshGeometryVertices.Num(), 4);

	const FBox BoundsLeftAlone = DrawCallList[0].CombinedBounds;
	DrawCallList[0].CombineIfPending();
	TestFalse(TEXT("Asked for, the buffer is combined"), DrawCallList[0].bCombinePending);
	TestEqual(TEXT("...all of it"), DrawCallList[0].CombinedBatchMeshGeometryVertices.Num(), 4);
	TestEqual(TEXT("...with its indices"), DrawCallList[0].CombinedBatchMeshGeometryTriangles.Num(), 6);
	TestTrue(TEXT("...and the bounds are the ones worked out alone"), DrawCallList[0].CombinedBounds.Equals(BoundsLeftAlone));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDrawCallGeometryListsShareLayoutTest,
	"DreamGUI.Canvas.TwoGeometryListsShareALayoutWhenOnlyTheirVerticesDiffer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDrawCallGeometryListsShareLayoutTest::RunTest(const FString& Parameters)
{
	using namespace DreamDrawCallPipelineTestLocal;

	/*
	 * A section built from one list of geometries can take another list's vertices in place when the two are laid out
	 * alike: as many geometries, each with the vertex count and the triangles of the one in its place.
	 */
	auto Shared = [](const FDreamUIGeometry& InGeometry) { return TSharedPtr<const FDreamUIGeometry>(MakeShared<FDreamUIGeometry>(InGeometry)); };
	const TSharedPtr<const FDreamUIGeometry> First = Shared(MakeQuad(FVector2D(-10.0, -10.0), FVector2D(10.0, 10.0)));
	const TSharedPtr<const FDreamUIGeometry> Second = Shared(MakeQuad(FVector2D(20.0, 20.0), FVector2D(40.0, 40.0)));
	const TSharedPtr<const FDreamUIGeometry> SecondMoved = Shared(MakeQuad(FVector2D(60.0, 20.0), FVector2D(80.0, 40.0)));
	FDreamUIGeometry TurnedQuad = MakeQuad(FVector2D(20.0, 20.0), FVector2D(40.0, 40.0));
	TurnedQuad.Triangles = { 0, 1, 3, 0, 3, 2 };
	const TSharedPtr<const FDreamUIGeometry> SecondTurned = Shared(TurnedQuad);
	FDreamUIGeometry GrownQuad = MakeQuad(FVector2D(20.0, 20.0), FVector2D(40.0, 40.0));
	const FDreamUIMeshVertex ExtraVertex = GrownQuad.Vertices[0];
	GrownQuad.Vertices.Add(ExtraVertex);
	const TSharedPtr<const FDreamUIGeometry> SecondGrown = Shared(GrownQuad);

	using FList = TArray<TSharedPtr<const FDreamUIGeometry>>;
	TestTrue(TEXT("A list shares its own layout"), FDreamUIDrawCall::GeometryListsShareLayout(FList{ First, Second }, FList{ First, Second }));
	TestTrue(TEXT("...and one with an element moved"), FDreamUIDrawCall::GeometryListsShareLayout(FList{ First, Second }, FList{ First, SecondMoved }));
	TestFalse(TEXT("Not one with other triangles"), FDreamUIDrawCall::GeometryListsShareLayout(FList{ First, Second }, FList{ First, SecondTurned }));
	TestFalse(TEXT("Not one with more vertices"), FDreamUIDrawCall::GeometryListsShareLayout(FList{ First, Second }, FList{ First, SecondGrown }));
	TestFalse(TEXT("Not one with fewer geometries"), FDreamUIDrawCall::GeometryListsShareLayout(FList{ First, Second }, FList{ First }));
	TestFalse(TEXT("Not two empty lists: nothing is laid out"), FDreamUIDrawCall::GeometryListsShareLayout(FList{}, FList{}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDrawCallCombineLeftForAPatchedSectionTest,
	"DreamGUI.Canvas.TheBatchingLeavesUncombinedADrawCallWhoseSectionWillTakeItsNewVerticesInPlace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDrawCallCombineLeftForAPatchedSectionTest::RunTest(const FString& Parameters)
{
	using namespace DreamDrawCallPipelineTestLocal;

	/*
	 * A draw call built from copies laid out as a section's are takes that section back with its vertices written in
	 * place, so the batching leaves it uncombined too. Each section goes to one draw call: two single quads of different
	 * textures are both laid out as the one section there is -- the left quad's, from before it moved -- and only the
	 * first of them is left uncombined.
	 */
	FDreamUIGeometry Left = MakeQuad(FVector2D(-40.0, -10.0), FVector2D(-20.0, 10.0));
	Left.Texture = UTexture2D::CreateTransient(4, 4);
	FDreamUIGeometry Right = MakeQuad(FVector2D(20.0, -10.0), FVector2D(40.0, 10.0));
	Right.Texture = UTexture2D::CreateTransient(4, 4);
	TArray<FDreamUIRenderData> RenderDataArray;
	RenderDataArray.Add(MakeBatchMeshRenderData(Left));
	RenderDataArray.Add(MakeBatchMeshRenderData(Right));
	const FDreamUIRenderData Before = MakeBatchMeshRenderData(MakeQuad(FVector2D(-60.0, -10.0), FVector2D(-40.0, 10.0)));
	const TArray<TArray<TSharedPtr<const FDreamUIGeometry>>> OnSections = { { Before.BatchMeshGeometry } };

	TArray<FDreamUIDrawCall> DrawCallList;
	UDreamCanvas::BatchDrawCallAsync(CanvasLeftBottom, CanvasRightTop, MoveTemp(RenderDataArray), DrawCallList, false, &OnSections);
	if (!TestEqual(TEXT("Two textures, two draw calls"), DrawCallList.Num(), 2))
	{
		return false;
	}
	TestTrue(TEXT("The first, laid out as the section is, is left uncombined"), DrawCallList[0].bCombinePending);
	TestTrue(TEXT("...its bounds worked out"), DrawCallList[0].CombinedBounds.IsValid != 0);
	TestFalse(TEXT("The second finds the section taken, and is combined"), DrawCallList[1].bCombinePending);
	TestEqual(TEXT("...its buffer full"), DrawCallList[1].CombinedBatchMeshGeometryVertices.Num(), 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDrawCallRefreshOfUncombinedDrawCallTest,
	"DreamGUI.Canvas.AVertexRefreshOfADrawCallLeftUncombinedTakesTheNewCopiesAndCombinesNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDrawCallRefreshOfUncombinedDrawCallTest::RunTest(const FString& Parameters)
{
	using namespace DreamDrawCallPipelineTestLocal;

	/*
	 * A draw call the batching left uncombined, because its section is taken back, has no combined buffer for the cheap
	 * refresh to write into, and needs none: its section takes the changed vertices in place. The refresh takes the new
	 * copies and leaves the buffer to be made from them should anything read it. A copy whose triangles changed is the
	 * exception -- no section can take it in place -- so the buffer is made as built and the vertices written into it.
	 */
	UDreamVisualBatchMesh* First = MakeVisualWithGeometry(MakeQuad(FVector2D(-10.0, -10.0), FVector2D(10.0, 10.0)));
	UDreamVisualBatchMesh* Second = MakeVisualWithGeometry(MakeQuad(FVector2D(20.0, 20.0), FVector2D(40.0, 40.0)));
	if (!TestNotNull(TEXT("A first visual"), First) || !TestNotNull(TEXT("...and a second"), Second))
	{
		return false;
	}
	FDreamUIDrawCall DrawCall(DreamUIQuadTree::Rectangle(FVector2D(-500.0, -500.0), FVector2D(500.0, 500.0)));
	DrawCall.BatchMeshGeometryArray = { First->GetGeometryForBatching(), Second->GetGeometryForBatching() };
	DrawCall.BatchMeshVisualArray = { First, Second };
	DrawCall.VerticesCount = 8;
	DrawCall.IndicesCount = 12;
	DrawCall.ApplyBatchMeshBoundsToCombined();
	DrawCall.bCombinePending = true;

	Second->GetGeometry()->Vertices[0].Color = FColor::Red;
	TestTrue(TEXT("A vertex of the second visual changed: the draw call is refreshed"), DrawCall.CopyBatchMeshGeometry());
	TestTrue(TEXT("...and left uncombined"), DrawCall.bCombinePending);
	TestEqual(TEXT("...its buffer still empty"), DrawCall.CombinedBatchMeshGeometryVertices.Num(), 0);
	TestTrue(TEXT("...holding the visual's new copy"), DrawCall.BatchMeshGeometryArray[1] == Second->GetGeometryForBatching());
	DrawCall.CombineIfPending();
	if (TestEqual(TEXT("Made when asked for, the buffer holds both quads"), DrawCall.CombinedBatchMeshGeometryVertices.Num(), 8))
	{
		TestEqual(TEXT("...the new vertex among them"), DrawCall.CombinedBatchMeshGeometryVertices[4].Color, FColor::Red);
	}

	// Left uncombined again; then the second copy's triangles turn.
	DrawCall.CombinedBatchMeshGeometryVertices.Reset();
	DrawCall.CombinedBatchMeshGeometryTriangles.Reset();
	DrawCall.bCombinePending = true;
	Second->GetGeometry()->Triangles = { 0, 1, 3, 0, 3, 2 };
	Second->GetGeometry()->Vertices[0].Color = FColor::Blue;
	TestTrue(TEXT("Its triangles turned: the draw call is refreshed"), DrawCall.CopyBatchMeshGeometry());
	TestFalse(TEXT("...its buffer made, as built"), DrawCall.bCombinePending);
	if (TestEqual(TEXT("...with both quads"), DrawCall.CombinedBatchMeshGeometryVertices.Num(), 8))
	{
		TestEqual(TEXT("...the new vertex written into it"), DrawCall.CombinedBatchMeshGeometryVertices[4].Color, FColor::Blue);
		TestEqual(TEXT("...and the triangles as built"), static_cast<int32>(DrawCall.CombinedBatchMeshGeometryTriangles[6]), 4);
	}
	TestFalse(TEXT("...which the new copy's are not"), DrawCall.bTrianglesAsBuilt);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDrawCallBlendModeSplitsBatchesTest,
	"DreamGUI.Canvas.ElementsThatCompositeDifferentlyNeverShareADrawCall",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDrawCallBlendModeSplitsBatchesTest::RunTest(const FString& Parameters)
{
	using namespace DreamDrawCallPipelineTestLocal;

	/*
	 * The blend state is chosen once per draw-call, so two elements that composite differently cannot
	 * share one whatever else they have in common. These quads have the same (null) material and the
	 * same (null) texture and overlap, so before blend modes existed they collapsed into one draw-call
	 * -- which is exactly what the first half asserts still happens when they agree.
	 */
	auto MakeQuadWithBlend = [](const FVector2D& Min, const FVector2D& Max, EDreamUIBlendMode Blend)
	{
		FDreamUIGeometry Geo = MakeQuad(Min, Max);
		Geo.BlendMode = Blend;
		return MakeBatchMeshRenderData(Geo);
	};

	{
		TArray<FDreamUIRenderData> RenderDataArray;
		RenderDataArray.Add(MakeQuadWithBlend(FVector2D(-100.0, -100.0), FVector2D(100.0, 100.0), EDreamUIBlendMode::Alpha));
		RenderDataArray.Add(MakeQuadWithBlend(FVector2D(-50.0, -50.0), FVector2D(150.0, 150.0), EDreamUIBlendMode::Alpha));
		TArray<FDreamUIDrawCall> DrawCallList;
		Batch(RenderDataArray, DrawCallList);
		if (TestEqual(TEXT("Two elements that agree still collapse into one draw-call"), DrawCallList.Num(), 1))
		{
			TestEqual(TEXT("...carrying the blend mode they agreed on"), (int32)DrawCallList[0].BlendMode, (int32)EDreamUIBlendMode::Alpha);
		}
	}

	{
		TArray<FDreamUIRenderData> RenderDataArray;
		RenderDataArray.Add(MakeQuadWithBlend(FVector2D(-100.0, -100.0), FVector2D(100.0, 100.0), EDreamUIBlendMode::Alpha));
		RenderDataArray.Add(MakeQuadWithBlend(FVector2D(-50.0, -50.0), FVector2D(150.0, 150.0), EDreamUIBlendMode::Additive));
		TArray<FDreamUIDrawCall> DrawCallList;
		Batch(RenderDataArray, DrawCallList);
		if (!TestEqual(TEXT("An additive element takes a draw-call of its own"), DrawCallList.Num(), 2))
		{
			return false;
		}
		TestEqual(TEXT("The first draw-call is the alpha one"), (int32)DrawCallList[0].BlendMode, (int32)EDreamUIBlendMode::Alpha);
		TestEqual(TEXT("The second is the additive one"), (int32)DrawCallList[1].BlendMode, (int32)EDreamUIBlendMode::Additive);
	}

	{
		// Non-overlapping is the interesting case: without a blend mode these two would happily share
		// a draw-call, because not overlapping is exactly what lets the batcher put them together.
		TArray<FDreamUIRenderData> RenderDataArray;
		RenderDataArray.Add(MakeQuadWithBlend(FVector2D(-200.0, -200.0), FVector2D(-100.0, -100.0), EDreamUIBlendMode::Multiply));
		RenderDataArray.Add(MakeQuadWithBlend(FVector2D(100.0, 100.0), FVector2D(200.0, 200.0), EDreamUIBlendMode::Alpha));
		TArray<FDreamUIDrawCall> DrawCallList;
		Batch(RenderDataArray, DrawCallList);
		TestEqual(TEXT("Even apart, a multiply and an alpha element stay in separate draw-calls"), DrawCallList.Num(), 2);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDrawCallElementCullingTest,
	"DreamGUI.Canvas.AnElementEntirelyOffTheCanvasIsLeftOutOfTheDrawCallListAltogether",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDrawCallElementCullingTest::RunTest(const FString& Parameters)
{
	using namespace DreamDrawCallPipelineTestLocal;

	// Same canvas rect as the Batch() helper, so the "with culling" and "without culling" cases below
	// differ in nothing but the flag.
	auto BatchWithCulling = [](const TArray<FDreamUIRenderData>& In, TArray<FDreamUIDrawCall>& Out)
	{
		UDreamCanvas::BatchDrawCallAsync(CanvasLeftBottom, CanvasRightTop, In, Out, /*bCullElementsOutsideCanvasRect*/true);
	};

	/*
	 * The same two quads, batched twice, differing only in the cull flag. Note what does NOT change:
	 * the draw-call COUNT. These two share a material and a texture and do not overlap, so the batcher
	 * puts them in one draw-call whether or not one of them is off-canvas -- counting draw-calls here
	 * would pass for the wrong reason. What culling changes is what is INSIDE that draw-call, so the
	 * element count and the vertex count are the assertions that mean anything.
	 */
	TArray<FDreamUIRenderData> OneOnScreenOneOff;
	OneOnScreenOneOff.Add(MakeBatchMeshRenderData(MakeQuad(FVector2D(-10.0, -10.0), FVector2D(10.0, 10.0))));
	OneOnScreenOneOff.Add(MakeBatchMeshRenderData(MakeQuad(FVector2D(2000.0, 2000.0), FVector2D(2100.0, 2100.0))));

	{
		TArray<FDreamUIDrawCall> DrawCallList;
		BatchWithCulling(OneOnScreenOneOff, DrawCallList);
		if (!TestEqual(TEXT("The surviving element still makes one draw-call"), DrawCallList.Num(), 1))
		{
			return false;
		}
		TestEqual(TEXT("The off-canvas element is not in it at all"), DrawCallList[0].BatchMeshGeometryArray.Num(), 1);
		TestEqual(TEXT("...so only the on-screen quad's vertices were assembled"), DrawCallList[0].VerticesCount, 4);
		TestEqual(TEXT("...and only its triangles"), DrawCallList[0].IndicesCount, 6);
	}

	{
		// Off by default, because a child canvas's rect is not a statement about visibility -- a canvas
		// is not a clipper, and its children are free to sit outside it.
		TArray<FDreamUIDrawCall> DrawCallList;
		Batch(OneOnScreenOneOff, DrawCallList);
		if (!TestEqual(TEXT("Without culling the two quads still batch into one draw-call"), DrawCallList.Num(), 1))
		{
			return false;
		}
		TestEqual(TEXT("...but the off-canvas element is assembled into it"), DrawCallList[0].BatchMeshGeometryArray.Num(), 2);
		TestEqual(TEXT("...carrying its vertices"), DrawCallList[0].VerticesCount, 8);
		TestEqual(TEXT("...and its triangles"), DrawCallList[0].IndicesCount, 12);
	}

	{
		// Touching the edge is kept: a half-pixel of antialiasing on the boundary is visible, and an
		// element that straddles it is plainly partly on screen.
		TArray<FDreamUIRenderData> RenderDataArray;
		RenderDataArray.Add(MakeBatchMeshRenderData(MakeQuad(FVector2D(-600.0, -10.0), FVector2D(-500.0, 10.0))));
		RenderDataArray.Add(MakeBatchMeshRenderData(MakeQuad(FVector2D(450.0, -10.0), FVector2D(600.0, 10.0))));

		TArray<FDreamUIDrawCall> DrawCallList;
		BatchWithCulling(RenderDataArray, DrawCallList);
		TestEqual(TEXT("An element exactly on the edge and one straddling it both survive"), DrawCallList.Num(), 1);
		TestEqual(TEXT("...with both quads' vertices"), DrawCallList[0].VerticesCount, 8);
	}

	{
		// Culling must not change which of the SURVIVORS batch together: dropping an element from the
		// middle of the list has to leave the two around it in one draw-call, not three.
		TArray<FDreamUIRenderData> RenderDataArray;
		RenderDataArray.Add(MakeBatchMeshRenderData(MakeQuad(FVector2D(-100.0, -100.0), FVector2D(-50.0, -50.0))));
		RenderDataArray.Add(MakeBatchMeshRenderData(MakeQuad(FVector2D(-9000.0, -9000.0), FVector2D(-8000.0, -8000.0))));
		RenderDataArray.Add(MakeBatchMeshRenderData(MakeQuad(FVector2D(50.0, 50.0), FVector2D(100.0, 100.0))));

		TArray<FDreamUIDrawCall> DrawCallList;
		BatchWithCulling(RenderDataArray, DrawCallList);
		if (TestEqual(TEXT("The two survivors share a single draw-call"), DrawCallList.Num(), 1))
		{
			TestEqual(TEXT("...holding exactly their two geometries"), DrawCallList[0].BatchMeshGeometryArray.Num(), 2);
		}
	}
	return true;
}

#endif
