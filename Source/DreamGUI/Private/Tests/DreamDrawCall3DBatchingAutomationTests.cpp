// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamCanvas.h"
#include "Core/DreamUIDrawCall.h"
#include "Core/DreamUIGeometry.h"

/*
 * Draw-call batching for widgets that are not flat on the canvas.
 *
 * UDreamCanvas::BatchDrawCallAsync is a static function that runs on the batching worker thread and
 * touches no UObject, so these drive it directly with hand-built render data: no world, no widget,
 * no RHI. That is not only about speed. Under -nullrhi the canvas produces no mesh sections at all,
 * so anything asserted on the render side would be asserted against an empty list; the draw-call
 * array the batcher fills in is the real output of this stage and the only thing worth asserting on.
 *
 * The render data is built through CopyDataForPrepare, the same call PrepareDrawCallBatchingData
 * makes, so these go through the copy rather than around it -- a field the copy drops is a field the
 * batcher never sees, which is exactly how the 3D path came to be dead code.
 *
 * Geometry here carries no texture and no material on purpose. Two draw-calls whose material and
 * texture are both null can always consume each other, which takes material identity out of the
 * picture and leaves the 2D/3D decision as the only thing left that can split a batch.
 */
namespace DreamDrawCall3DBatchingTestLocal
{
	/** One quad, placed by its canvas-space bounds, standing at InTransformRelativeToCanvas. */
	FDreamUIGeometry MakeQuad(const FVector2D& InBoundsMin, const FVector2D& InBoundsMax, const FTransform& InTransformRelativeToCanvas)
	{
		//canvas space is X-into-the-screen, so the quad's plane is the Y/Z one
		FDreamUIGeometry Geo;
		Geo.Vertices.Add(FDreamUIMeshVertex(FVector3f(0.0f, (float)InBoundsMin.X, (float)InBoundsMin.Y)));
		Geo.Vertices.Add(FDreamUIMeshVertex(FVector3f(0.0f, (float)InBoundsMax.X, (float)InBoundsMin.Y)));
		Geo.Vertices.Add(FDreamUIMeshVertex(FVector3f(0.0f, (float)InBoundsMin.X, (float)InBoundsMax.Y)));
		Geo.Vertices.Add(FDreamUIMeshVertex(FVector3f(0.0f, (float)InBoundsMax.X, (float)InBoundsMax.Y)));
		Geo.Triangles = { 0, 3, 2, 0, 1, 3 };
		Geo.BoundsMin2DInCanvasSpace = InBoundsMin;
		Geo.BoundsMax2DInCanvasSpace = InBoundsMax;
		Geo.TransformRelativeToCanvas = InTransformRelativeToCanvas;
		Geo.bSupportDrawcallBatching = true;
		return Geo;
	}

	/** Wraps a geometry the way UDreamCanvas::PrepareDrawCallBatchingData does, copy included. */
	FDreamUIRenderData MakeBatchMeshRenderData(const FDreamUIGeometry& InGeo)
	{
		FDreamUIRenderData RenderData(EDreamUIDrawCallType::BatchMesh);
		RenderData.BatchMeshGeometry.CopyDataForPrepare(InGeo);
		return RenderData;
	}

	void Batch(const TArray<FDreamUIRenderData>& InRenderDataArray, TArray<FDreamUIDrawCall>& OutDrawCallList)
	{
		//a canvas rect wide enough to contain every rect these tests place, so the quad-tree root holds them
		UDreamCanvas::BatchDrawCallAsync(FVector2D(-500.0, -500.0), FVector2D(500.0, 500.0), InRenderDataArray, OutDrawCallList);
	}

	/** A card turned away from the viewer. */
	FTransform Yawed()
	{
		return FTransform(FRotator(0.0, 35.0, 0.0));
	}

	/** A card lifted off the canvas plane along the canvas's depth axis. */
	FTransform PushedInX()
	{
		return FTransform(FRotator::ZeroRotator, FVector(25.0, 0.0, 0.0));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDrawCallFlatWidgetsStillBatchTest,
	"DreamGUI.Canvas.TwoFlatWidgetsStillCollapseIntoASingleTwoDimensionalDrawCall",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDrawCallFlatWidgetsStillBatchTest::RunTest(const FString& Parameters)
{
	using namespace DreamDrawCall3DBatchingTestLocal;

	// The guard rail: teaching the batcher to see 3D must not cost ordinary flat UI its batching.
	// The two quads overlap, which is the case that forces the batcher to decide here and now rather
	// than letting the second one drift into some deeper draw-call.
	TArray<FDreamUIRenderData> RenderDataArray;
	RenderDataArray.Add(MakeBatchMeshRenderData(MakeQuad(FVector2D(-100.0, -100.0), FVector2D(100.0, 100.0), FTransform::Identity)));
	RenderDataArray.Add(MakeBatchMeshRenderData(MakeQuad(FVector2D(-50.0, -50.0), FVector2D(150.0, 150.0), FTransform::Identity)));

	TArray<FDreamUIDrawCall> DrawCallList;
	Batch(RenderDataArray, DrawCallList);

	if (!TestEqual(TEXT("Two overlapping flat widgets make one draw-call"), DrawCallList.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("Both geometries are in that draw-call"), DrawCallList[0].BatchMeshGeometryArray.Num(), 2);
	TestTrue(TEXT("The draw-call is still marked 2D space"), DrawCallList[0].bIs2DSpace);
	TestEqual(TEXT("The draw-call counts both quads' vertices"), DrawCallList[0].VerticesCount, 8);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDrawCallYawedWidgetDoesNotBatchWithFlatOnesTest,
	"DreamGUI.Canvas.AYawedWidgetTakesItsOwnDrawCallInsteadOfBatchingWithFlatOnes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDrawCallYawedWidgetDoesNotBatchWithFlatOnesTest::RunTest(const FString& Parameters)
{
	using namespace DreamDrawCall3DBatchingTestLocal;

	// 35 degrees of yaw has to read as 3D for any sane setting of the auto-batch threshold, which is
	// what Is2DUITransform measures against. Asserting the premise means a project that raised
	// UDreamUISettings::GetAutoBatchThreshold past 35 degrees fails here with a clear message instead
	// of quietly turning the rest of this test into a second copy of the flat-batching one.
	if (!TestTrue(TEXT("A 35 degree yaw is beyond the auto-batch threshold"), !UDreamCanvas::Is2DUITransform(Yawed())))
	{
		return false;
	}
	if (!TestTrue(TEXT("An identity transform is within the auto-batch threshold"), UDreamCanvas::Is2DUITransform(FTransform::Identity)))
	{
		return false;
	}

	// The turned card is drawn first, so it opens the draw-call list on its own; the two flat cards
	// behind it in sort order must not be allowed to join it. Before CopyDataForPrepare carried
	// TransformRelativeToCanvas this produced ONE draw-call holding all three, every one of them
	// reported as 2D space -- which is how a turned card ended up sharing a batch, and an overlap
	// test, with elements it is nowhere near in depth.
	TArray<FDreamUIRenderData> RenderDataArray;
	RenderDataArray.Add(MakeBatchMeshRenderData(MakeQuad(FVector2D(-100.0, -100.0), FVector2D(100.0, 100.0), Yawed())));
	RenderDataArray.Add(MakeBatchMeshRenderData(MakeQuad(FVector2D(-80.0, -80.0), FVector2D(120.0, 120.0), FTransform::Identity)));
	RenderDataArray.Add(MakeBatchMeshRenderData(MakeQuad(FVector2D(-60.0, -60.0), FVector2D(140.0, 140.0), FTransform::Identity)));

	TArray<FDreamUIDrawCall> DrawCallList;
	Batch(RenderDataArray, DrawCallList);

	if (!TestEqual(TEXT("The yawed widget splits the batch in two"), DrawCallList.Num(), 2))
	{
		return false;
	}
	TestFalse(TEXT("The yawed widget's draw-call is not 2D space"), DrawCallList[0].bIs2DSpace);
	TestEqual(TEXT("The yawed widget is alone in it"), DrawCallList[0].BatchMeshGeometryArray.Num(), 1);
	TestTrue(TEXT("The flat widgets' draw-call is 2D space"), DrawCallList[1].bIs2DSpace);
	TestEqual(TEXT("Both flat widgets batched together"), DrawCallList[1].BatchMeshGeometryArray.Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDrawCallDepthOffsetWidgetEndsTheBatchTest,
	"DreamGUI.Canvas.AWidgetPushedOffTheCanvasPlaneEndsTheBatchItLandsIn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDrawCallDepthOffsetWidgetEndsTheBatchTest::RunTest(const FString& Parameters)
{
	using namespace DreamDrawCall3DBatchingTestLocal;

	// The other half of Is2DUITransform: a widget that is square-on but standing off the canvas plane
	// along X is 3D too, and 25 units of it is well past any reasonable threshold.
	if (!TestTrue(TEXT("25 units of X displacement is beyond the auto-batch threshold"), !UDreamCanvas::Is2DUITransform(PushedInX())))
	{
		return false;
	}

	// Flat, then depth-offset, then flat. A 3D item may batch into the draw-call immediately before it
	// -- that neighbour is adjacent in sort order, so merging keeps the drawing order -- but the
	// draw-call is 3D from then on, and the flat item that follows has to start a new one instead of
	// being drawn through it.
	TArray<FDreamUIRenderData> RenderDataArray;
	RenderDataArray.Add(MakeBatchMeshRenderData(MakeQuad(FVector2D(-100.0, -100.0), FVector2D(0.0, 100.0), FTransform::Identity)));
	RenderDataArray.Add(MakeBatchMeshRenderData(MakeQuad(FVector2D(-20.0, -100.0), FVector2D(80.0, 100.0), PushedInX())));
	RenderDataArray.Add(MakeBatchMeshRenderData(MakeQuad(FVector2D(60.0, -100.0), FVector2D(160.0, 100.0), FTransform::Identity)));

	TArray<FDreamUIDrawCall> DrawCallList;
	Batch(RenderDataArray, DrawCallList);

	if (!TestEqual(TEXT("The depth-offset widget splits the batch in two"), DrawCallList.Num(), 2))
	{
		return false;
	}
	TestEqual(TEXT("The depth-offset widget merged into the draw-call before it"), DrawCallList[0].BatchMeshGeometryArray.Num(), 2);
	TestFalse(TEXT("That merge marked the draw-call 3D space"), DrawCallList[0].bIs2DSpace);
	TestEqual(TEXT("The following flat widget started a new draw-call"), DrawCallList[1].BatchMeshGeometryArray.Num(), 1);
	TestTrue(TEXT("And that new draw-call is 2D space"), DrawCallList[1].bIs2DSpace);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDrawCallFlatBatchAboveA3DDrawCallTest,
	"DreamGUI.Canvas.AFlatWidgetStillBatchesIntoTheFlatDrawCallStackedAboveAThreeDimensionalOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDrawCallFlatBatchAboveA3DDrawCallTest::RunTest(const FString& Parameters)
{
	using namespace DreamDrawCall3DBatchingTestLocal;

	// A 3D draw-call is a floor for the batching search, not a dead end. The two flat quads here do
	// not overlap each other, so the search for the second one walks past the first, collects it as a
	// candidate, and only then reaches the 3D draw-call underneath. It has to fall back on the
	// candidate it already has: nothing between that candidate and the tail overlaps the item, so
	// putting it there draws the same picture, and the search never crosses the 3D draw-call.
	TArray<FDreamUIRenderData> RenderDataArray;
	RenderDataArray.Add(MakeBatchMeshRenderData(MakeQuad(FVector2D(-40.0, -40.0), FVector2D(40.0, 40.0), Yawed())));
	RenderDataArray.Add(MakeBatchMeshRenderData(MakeQuad(FVector2D(-200.0, -50.0), FVector2D(-100.0, 50.0), FTransform::Identity)));
	RenderDataArray.Add(MakeBatchMeshRenderData(MakeQuad(FVector2D(100.0, -50.0), FVector2D(200.0, 50.0), FTransform::Identity)));

	TArray<FDreamUIDrawCall> DrawCallList;
	Batch(RenderDataArray, DrawCallList);

	if (!TestEqual(TEXT("The flat pair share one draw-call above the 3D one"), DrawCallList.Num(), 2))
	{
		return false;
	}
	TestFalse(TEXT("The yawed widget's draw-call is not 2D space"), DrawCallList[0].bIs2DSpace);
	TestEqual(TEXT("The yawed widget is alone in it"), DrawCallList[0].BatchMeshGeometryArray.Num(), 1);
	TestTrue(TEXT("The flat draw-call above it is 2D space"), DrawCallList[1].bIs2DSpace);
	TestEqual(TEXT("Both flat widgets batched together"), DrawCallList[1].BatchMeshGeometryArray.Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDrawCallPrepareCarriesCanvasTransformTest,
	"DreamGUI.Canvas.PreparingGeometryForBatchingCarriesTheCanvasRelativeTransform",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDrawCallPrepareCarriesCanvasTransformTest::RunTest(const FString& Parameters)
{
	using namespace DreamDrawCall3DBatchingTestLocal;

	// Pinning the field itself, not just its consequence. CopyDataForPrepare is written out
	// field-by-field, so a field is only ever one careless edit away from being dropped again, and a
	// dropped TransformRelativeToCanvas does not read as missing data downstream -- an identity
	// transform is a perfectly valid answer that happens to mean "flat", so everything keeps working
	// and quietly stops being 3D.
	const FTransform Authored(FRotator(11.0, 47.0, 3.0), FVector(12.0, -34.0, 56.0), FVector(2.0, 3.0, 4.0));
	const FDreamUIGeometry Source = MakeQuad(FVector2D(-10.0, -20.0), FVector2D(30.0, 40.0), Authored);

	FDreamUIGeometry Prepared;
	Prepared.CopyDataForPrepare(Source);

	TestTrue(TEXT("CopyDataForPrepare carries TransformRelativeToCanvas"),
		Prepared.TransformRelativeToCanvas.Equals(Source.TransformRelativeToCanvas));
	TestFalse(TEXT("So the prepared geometry is still read as 3D"),
		UDreamCanvas::Is2DUITransform(Prepared.TransformRelativeToCanvas));
	TestEqual(TEXT("The vertices came across"), Prepared.Vertices.Num(), Source.Vertices.Num());
	TestEqual(TEXT("The triangle indices came across"), Prepared.Triangles.Num(), Source.Triangles.Num());
	TestTrue(TEXT("The canvas-space bounds minimum came across"),
		Prepared.BoundsMin2DInCanvasSpace.Equals(Source.BoundsMin2DInCanvasSpace));
	TestTrue(TEXT("The canvas-space bounds maximum came across"),
		Prepared.BoundsMax2DInCanvasSpace.Equals(Source.BoundsMax2DInCanvasSpace));
	return true;
}

#endif
