// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamVisualBatchMesh.h"
#include "Core/Components/DreamVisualDirectMesh.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIGeometry.h"
#include "Core/DreamWidgetTree.h"
#include "Extensions/2DLineRenderer/Dream2DLineChildrenAsPoints.h"
#include "Extensions/2DLineRenderer/Dream2DLineRaw.h"
#include "Extensions/2DLineRenderer/Dream2DLineRendererBase.h"
#include "Extensions/DreamCanvasRenderTargetPreviewer.h"
#include "Extensions/DreamPolygon.h"
#include "Extensions/DreamPolygonLine.h"
#include "Extensions/DreamPostProcessRenderElement.h"
#include "Extensions/DreamPostProcessRenderElement_Text.h"
#include "Extensions/DreamRing.h"
#include "Extensions/DreamStaticMesh.h"
#include "Materials/Material.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

/*
 * The geometry-generating extension visuals, and the line between what a headless test can hold and
 * what it cannot.
 *
 * Everything under Extensions/ came across from the fork untouched and has never had a test. The
 * shapes these classes build are pure arithmetic -- a ring of points, a fan of triangles, a strip
 * two vertices wide -- so on paper they are the most testable code in the plugin. In practice the
 * arithmetic is unreachable from here, and it is worth writing down exactly why so the next person
 * does not spend the afternoon rediscovering it:
 *
 *   - OnUpdateGeometry and CalculatePoints are protected, and their only caller is
 *     UDreamVisualBatchMesh::UpdateGeometry, which opens with check(Canvas) and then hands the
 *     vertex transform to Canvas->PushAsyncFunction_TransformVertices. A test would have to stand
 *     up a canvas AND pump it to get an answer back, and a half-pumped canvas leaves the geometry
 *     flagged bIsCalculating forever.
 *   - The suite runs -nullrhi, so there is no picture to compare against even if the vertices came
 *     out. Asserting the positions this file could compute for itself would be measuring its own
 *     arithmetic, which is the failure mode these tests exist to avoid.
 *
 * So what IS pinned here is the layer above the arithmetic: the guards that decide what the
 * arithmetic is allowed to be handed. Every one of them is a MAX or a count comparison protecting a
 * divide or an index, and every one of them fails as a look rather than a crash -- a polygon with
 * one side, a ring whose angle step divides by zero, a line whose right-hand width went negative.
 * Those are the decisions; the rest is memcpy.
 *
 * Three defects were found while first reading this code and pinned here as they stood, with a note
 * saying so. All three are now fixed and the assertions below say what the right answer is instead:
 *
 *   - UDreamStaticMesh::GetMaterial dereferenced MeshCache without checking it, and MeshCache
 *     defaults to null. GetRenderMaterial and GetOrCreateDynamicMaterialInstance are both
 *     BlueprintCallable and both route through it, so a freshly added static mesh answered a
 *     Blueprint question with a null dereference -- which is why the mesh test used to attach a
 *     cache first and say why. It now asks the question with no cache at all.
 *   - UDream2DLineRendererBase::GenerateLinePoint divided the half-width by sin(acos(dot)) with
 *     nothing bounding the quotient. A polyline doubling exactly back on itself sent it to
 *     infinity, and a merely sharp corner produced a merely enormous spike by the same arithmetic.
 *     The joint decision now lives in ComputeMiterScale, which is public and static precisely so a
 *     test with no canvas can reach it -- see the miter test.
 *   - UDream2DLineChildrenAsPoints::OnChildPositionChanged is the hook that marks the line's
 *     vertices dirty when one of the child widgets it reads moves, and nothing called it. It is now
 *     wired to each child's transform-changed event, which is what the subscription test pins.
 *
 * One defect is fixed and NOT pinned here, because nothing headless can see it:
 * UDreamCanvasRenderTargetPreviewer registered its render-target listener with AddLambda and
 * unregistered with RemoveAll(this), which can never match a lambda bound to no object -- so every
 * register/unregister pair leaked one more live listener. Reaching the register path needs a canvas
 * with a render target, which needs an RHI. The fix is to bind the object (AddWeakLambda), matching
 * the sibling class that always did.
 */

namespace DreamExtensionVisualsTestLocal
{
	/** A worldless authoring tree with one widget, which is the state the designer builds in. */
	struct FVisualFixture
	{
		UDreamWidgetTree* Tree = nullptr;
		UDreamWidget* Widget = nullptr;

		explicit FVisualFixture(UClass* InVisualClass, const TCHAR* InName = TEXT("Shape"))
		{
			Tree = NewObject<UDreamWidgetTree>(GetTransientPackage());
			Widget = Tree->ConstructWidget(UDreamWidget::StaticClass(), FName(InName));
			if (Widget != nullptr)
			{
				Tree->RootWidget = Widget;
				Widget->SetWidth(200.0f);
				Widget->SetHeight(100.0f);
				Widget->CreateNewVisual(InVisualClass);
			}
		}

		UDreamVisual* GetVisual() const { return Widget != nullptr ? Widget->GetVisual() : nullptr; }

		void Teardown()
		{
			if (Widget != nullptr)
			{
				Widget->DestroyWidget();
				Widget = nullptr;
			}
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPolygonSideClampTest,
	"DreamGUI.Extensions.APolygonNeverKeepsFewerSidesThanItsShapeNeeds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPolygonSideClampTest::RunTest(const FString& Parameters)
{
	using namespace DreamExtensionVisualsTestLocal;

	// Sides is the divisor of the angle step and the length of the triangle fan, so it is the one
	// number in this class that cannot be allowed to reach zero. The floor is different for the two
	// shapes the class draws: a closed cycle needs three sides before it encloses anything, while an
	// open fan is legible with one. The StretchSpriteHeight UV path divides by (vertexCount - 2) as
	// well, which is Sides when the fan is open and Sides - 1 when the cycle is closed -- so the two
	// floors are also exactly what keeps THAT divisor at one or above.
	FVisualFixture Fixture(UDreamPolygon::StaticClass(), TEXT("Poly"));
	UDreamPolygon* Poly = Cast<UDreamPolygon>(Fixture.GetVisual());
	if (!TestNotNull(TEXT("the polygon exists on a widget"), Poly))
	{
		Fixture.Teardown();
		return false;
	}

	TestEqual(TEXT("a fresh polygon is a triangle"), Poly->GetSides(), 3);
	TestTrue(TEXT("and it is a closed cycle"), Poly->GetFullCycle());

	Poly->SetSides(8);
	TestEqual(TEXT("a bigger count is taken as written"), Poly->GetSides(), 8);

	Poly->SetSides(2);
	TestEqual(TEXT("two sides enclose nothing, so a cycle keeps three"), Poly->GetSides(), 3);
	Poly->SetSides(0);
	TestEqual(TEXT("and zero would divide the sweep by nothing"), Poly->GetSides(), 3);
	Poly->SetSides(-4);
	TestEqual(TEXT("a negative count is the same refusal"), Poly->GetSides(), 3);

	// The open fan has the lower floor, which is the whole reason the clamp reads the FullCycle flag
	// rather than being a constant.
	Poly->SetFullCycle(false);
	Poly->SetSides(1);
	TestEqual(TEXT("an open fan is legible with a single side"), Poly->GetSides(), 1);
	Poly->SetSides(0);
	TestEqual(TEXT("but still not with none"), Poly->GetSides(), 1);

	// The other half of the pair, and the one that used to be missing. SetFullCycle changes WHICH
	// floor applies, so it has to re-run the clamp: without that the object sat in a state its own
	// invariant forbids -- a closed cycle reporting one side. Nothing drew wrong, because
	// OnUpdateGeometry re-clamps before it uses the number, but GetSides is BlueprintCallable and
	// everything sizing an offset array from it was told 1.
	Poly->SetFullCycle(true);
	TestEqual(TEXT("closing the cycle raises the count back to the three a cycle needs"),
		Poly->GetSides(), 3);

	// And opening it again does not push the count back down: the clamp is a floor, not a target.
	Poly->SetFullCycle(false);
	TestEqual(TEXT("opening it again leaves the count where it is"), Poly->GetSides(), 3);
	Fixture.Teardown();

	// UDreamPolygonLine carries a copy of the same clamp, and copies drift.
	FVisualFixture LineFixture(UDreamPolygonLine::StaticClass(), TEXT("PolyLine"));
	UDreamPolygonLine* Line = Cast<UDreamPolygonLine>(LineFixture.GetVisual());
	if (!TestNotNull(TEXT("the polygon line exists on a widget"), Line))
	{
		LineFixture.Teardown();
		return false;
	}
	TestEqual(TEXT("a fresh polygon line is a triangle too"), Line->GetSides(), 3);
	Line->SetSides(0);
	TestEqual(TEXT("and refuses to lose its cycle the same way"), Line->GetSides(), 3);
	Line->SetFullCycle(false);
	Line->SetSides(1);
	TestEqual(TEXT("and opens to the same lower floor"), Line->GetSides(), 1);
	Line->SetFullCycle(true);
	TestEqual(TEXT("and the copy re-clamps on closing too"), Line->GetSides(), 3);
	LineFixture.Teardown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPolygonVertexOffsetArrayTest,
	"DreamGUI.Extensions.AVertexOffsetArrayMustBeOnePerVertexTheShapeAsksFor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPolygonVertexOffsetArrayTest::RunTest(const FString& Parameters)
{
	using namespace DreamExtensionVisualsTestLocal;

	// VertexOffsetArray is indexed by the vertex loop with no bounds check -- originVertices[i+1]
	// reads VertexOffsetArray[i] for every side -- so a short array would read past its end. The
	// setter therefore refuses a mismatched length rather than resizing, which is right: an array
	// of the wrong length is an author mistake about which corners they are moving, and padding it
	// silently would move the shape somewhere nobody asked for while reporting success.
	//
	// The length it measures against is the one the SHAPE implies -- one offset per ring vertex,
	// plus the extra vertex an open fan needs to close its far edge. It used to measure against the
	// STORED array instead, and that was a deadlock rather than a contract: the stored array is only
	// ever sized inside OnUpdateGeometry, so it was empty until the polygon had been drawn once, and
	// the obvious authoring order -- pick a side count, then hand over one offset per side -- was
	// refused every time with a message naming a length no Blueprint could produce. The only route
	// that worked was GetVertexOffsetArray_Direct, which is not a UFUNCTION and so is not reachable
	// from Blueprint at all.
	AddExpectedError(TEXT("Array count not equal"), EAutomationExpectedErrorFlags::Contains, 0);

	FVisualFixture Fixture(UDreamPolygon::StaticClass(), TEXT("Poly"));
	UDreamPolygon* Poly = Cast<UDreamPolygon>(Fixture.GetVisual());
	if (!TestNotNull(TEXT("the polygon exists on a widget"), Poly))
	{
		Fixture.Teardown();
		return false;
	}

	// The stored array starts empty, and that no longer stands in the way of anything.
	TestEqual(TEXT("a fresh polygon holds no offsets at all"), Poly->GetVertexOffsetArray().Num(), 0);

	Poly->SetSides(6);
	TArray<float> SixOffsets;
	SixOffsets.Init(0.25f, 6);
	Poly->SetVertexOffsetArray(SixOffsets);
	if (TestEqual(TEXT("one offset per side is accepted on a shape that has never been drawn"),
		Poly->GetVertexOffsetArray().Num(), 6))
	{
		TestEqual(TEXT("and the values are the ones handed over"),
			Poly->GetVertexOffsetArray()[0], 0.25f);
		TestEqual(TEXT("all of them"), Poly->GetVertexOffsetArray()[5], 0.25f);
	}

	// A length that does not match the side count is still refused, and now the caller could have
	// known: the required count is GetSides, which they set themselves.
	TArray<float> ThreeOffsets;
	ThreeOffsets.Init(1.0f, 3);
	Poly->SetVertexOffsetArray(ThreeOffsets);
	TestEqual(TEXT("a shorter array leaves the stored one untouched"),
		Poly->GetVertexOffsetArray().Num(), 6);
	TestEqual(TEXT("values and all"), Poly->GetVertexOffsetArray()[0], 0.25f);

	// An open fan needs one MORE offset than it has sides, because the sweep has two ends rather
	// than one seam -- the same asymmetry that gives the two shapes different side floors. This is
	// the case a caller cannot guess from the side count alone, so it is spelled out.
	Poly->SetFullCycle(false);
	Poly->SetSides(4);
	TArray<float> FourOffsets;
	FourOffsets.Init(0.5f, 4);
	Poly->SetVertexOffsetArray(FourOffsets);
	TestEqual(TEXT("four offsets are one short for an open fan of four sides"),
		Poly->GetVertexOffsetArray().Num(), 6);

	TArray<float> FiveOffsets;
	FiveOffsets.Init(0.5f, 5);
	Poly->SetVertexOffsetArray(FiveOffsets);
	if (TestEqual(TEXT("five is what an open fan of four sides wants"),
		Poly->GetVertexOffsetArray().Num(), 5))
	{
		TestEqual(TEXT("and it took the values"), Poly->GetVertexOffsetArray()[4], 0.5f);
	}

	Fixture.Teardown();

	// UDreamPolygonLine carries its own copy of the setter, and copies drift.
	FVisualFixture LineFixture(UDreamPolygonLine::StaticClass(), TEXT("PolyLine"));
	UDreamPolygonLine* Line = Cast<UDreamPolygonLine>(LineFixture.GetVisual());
	if (TestNotNull(TEXT("the polygon line exists on a widget"), Line))
	{
		Line->SetSides(5);
		TArray<float> FiveMore;
		FiveMore.Init(0.75f, 5);
		Line->SetVertexOffsetArray(FiveMore);
		TestEqual(TEXT("the polygon line takes one offset per side on an undrawn shape too"),
			Line->GetVertexOffsetArray().Num(), 5);
	}
	LineFixture.Teardown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRingSegmentClampTest,
	"DreamGUI.Extensions.ARingKeepsTheTwoEndPointsThatMakeItALine",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRingSegmentClampTest::RunTest(const FString& Parameters)
{
	using namespace DreamExtensionVisualsTestLocal;

	FVisualFixture Fixture(UDreamRing::StaticClass(), TEXT("Ring"));
	UDreamRing* Ring = Cast<UDreamRing>(Fixture.GetVisual());
	if (!TestNotNull(TEXT("the ring exists on a widget"), Ring))
	{
		Fixture.Teardown();
		return false;
	}

	TestEqual(TEXT("a fresh ring is twelve segments of arc"), Ring->GetSegment(), 12);

	// Segment feeds two numbers in CalculatePoints: the angle step is the sweep over (Segment + 1),
	// and the point count is Segment + 2. Zero is therefore the correct floor and not an oversight
	// -- it is the smallest value that still leaves the divisor at one and yields the two points a
	// line renderer needs to draw anything at all. One lower and the step divides by zero; the
	// points would still be produced, every one of them NaN, and NaN positions reach the vertex
	// buffer without any assert firing. The clamp is written twice, in the setter and again at the
	// top of CalculatePoints, which is what the property being editable directly costs.
	Ring->SetSegment(0);
	TestEqual(TEXT("no segments is still an arc with two ends"), Ring->GetSegment(), 0);
	Ring->SetSegment(-5);
	TestEqual(TEXT("and a negative count cannot push the divisor to zero"), Ring->GetSegment(), 0);
	Ring->SetSegment(200);
	TestEqual(TEXT("the upper end is the inspector's business, not the setter's"),
		Ring->GetSegment(), 200);

	// The degenerate arc is allowed on purpose. Start == End collapses every point onto the same
	// position, and the line builder's equality branch -- InCurrentPoint == InPrevPoint, reuse the
	// previous direction -- is what keeps that finite instead of normalising a zero vector.
	Ring->SetStartAngle(45.0f);
	Ring->SetEndAngle(45.0f);
	TestEqual(TEXT("a zero-sweep arc is accepted"), Ring->GetStartAngle(), 45.0f);
	TestEqual(TEXT("at both ends"), Ring->GetEndAngle(), 45.0f);

	// Sweeping backwards is likewise allowed; it is a negative step, not an error.
	Ring->SetEndAngle(-90.0f);
	TestEqual(TEXT("and so is an arc that sweeps the other way"), Ring->GetEndAngle(), -90.0f);

	Fixture.Teardown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLineRendererEndTypeTest,
	"DreamGUI.Extensions.EachLineShapeDecidesForItselfWhetherItsEndsMeet",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamLineRendererEndTypeTest::RunTest(const FString& Parameters)
{
	using namespace DreamExtensionVisualsTestLocal;

	// EndType is not decoration: it is the only thing that decides the size of both buffers and
	// whether the seam is closed. Cap adds four vertices and twelve indices for the two end quads;
	// ConnectStartAndEnd adds six indices and no vertices, wrapping the last segment onto the
	// first; None adds neither. Get it wrong on a closed shape and the ring has a visible gap at
	// its seam -- which is precisely why UDreamPolygonLine overrides the inherited default in its
	// constructor, and why that override is worth a test: it lives in one line of a constructor and
	// nothing else in the class refers to it.
	auto EndTypeOf = [this](UClass* InClass, const TCHAR* InName) -> EDream2DLineRenderer_EndType
	{
		FVisualFixture Fixture(InClass, InName);
		UDream2DLineRendererBase* Line = Cast<UDream2DLineRendererBase>(Fixture.GetVisual());
		const EDream2DLineRenderer_EndType Result = Line != nullptr
			? Line->GetEndType() : EDream2DLineRenderer_EndType::None;
		TestNotNull(*FString::Printf(TEXT("%s is a line renderer"), InName), Line);
		Fixture.Teardown();
		return Result;
	};

	TestEqual(TEXT("a raw point list is an open line with caps"),
		EndTypeOf(UDream2DLineRaw::StaticClass(), TEXT("Raw")),
		EDream2DLineRenderer_EndType::Cap);
	TestEqual(TEXT("a ring is an arc, so it caps rather than closes"),
		EndTypeOf(UDreamRing::StaticClass(), TEXT("Ring")),
		EDream2DLineRenderer_EndType::Cap);
	TestEqual(TEXT("children-as-points is an open path too"),
		EndTypeOf(UDream2DLineChildrenAsPoints::StaticClass(), TEXT("Children")),
		EDream2DLineRenderer_EndType::Cap);
	TestEqual(TEXT("but a polygon line closes its own seam"),
		EndTypeOf(UDreamPolygonLine::StaticClass(), TEXT("PolyLine")),
		EDream2DLineRenderer_EndType::ConnectStartAndEnd);

	// And the seam only actually closes with three points or more -- two points wrapped onto
	// themselves would emit the same quad twice. That gate lives in CanConnectStartEndPoint and is
	// not reachable from here; it is named so the next reader knows the default above is a
	// PREFERENCE the geometry pass may decline, not a promise.
	FVisualFixture Fixture(UDream2DLineRaw::StaticClass(), TEXT("Raw"));
	UDream2DLineRaw* Raw = Cast<UDream2DLineRaw>(Fixture.GetVisual());
	if (TestNotNull(TEXT("the raw line exists on a widget"), Raw))
	{
		Raw->SetEndType(EDream2DLineRenderer_EndType::ConnectStartAndEnd);
		TestEqual(TEXT("the end type is settable at runtime"),
			Raw->GetEndType(), EDream2DLineRenderer_EndType::ConnectStartAndEnd);
		Raw->SetEndType(EDream2DLineRenderer_EndType::None);
		TestEqual(TEXT("including all the way off"),
			Raw->GetEndType(), EDream2DLineRenderer_EndType::None);

		// A point list under two entries makes OnUpdateGeometry clear the geometry and return, so
		// setting one is a legitimate way to hide the line rather than an error.
		Raw->SetPoints(TArray<FVector2D>{ FVector2D(0.0, 0.0) });
		Raw->SetPoints(TArray<FVector2D>());
		Raw->SetPoints(TArray<FVector2D>{ FVector2D(-50.0, 0.0), FVector2D(50.0, 0.0) });
	}
	Fixture.Teardown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLineWidthRangeTest,
	"DreamGUI.Extensions.TheLineWidthSettersHoldTheRangeTheInspectorHolds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamLineWidthRangeTest::RunTest(const FString& Parameters)
{
	using namespace DreamExtensionVisualsTestLocal;

	// LineWidthOffset splits the strip: the left edge sits at LineWidth * Offset and the right edge
	// at LineWidth * (1 - Offset). Outside 0..1 one of those two products goes NEGATIVE, which puts
	// both edges on the same side of the path -- the strip turns inside out and every quad winds
	// backwards. The property has always said so, carrying ClampMin 0 and ClampMax 1, and the
	// inspector has always honoured them; the setter did not, so an author dragging the slider could
	// not reach a bad value and one line of Blueprint could. That disagreement was the defect.
	//
	// The metadata is asserted alongside the behaviour because the two are now one statement: the
	// setter enforces exactly what the property declares, and a change to either has to come here.
	const FProperty* OffsetProperty =
		UDream2DLineRendererBase::StaticClass()->FindPropertyByName(TEXT("LineWidthOffset"));
	if (TestNotNull(TEXT("LineWidthOffset is a reflected property"), OffsetProperty))
	{
		TestEqual(TEXT("whose authored floor is zero"),
			OffsetProperty->GetMetaData(TEXT("ClampMin")), FString(TEXT("0")));
		TestEqual(TEXT("and whose authored ceiling is one"),
			OffsetProperty->GetMetaData(TEXT("ClampMax")), FString(TEXT("1")));
	}
	const FProperty* WidthProperty =
		UDream2DLineRendererBase::StaticClass()->FindPropertyByName(TEXT("LineWidth"));
	if (TestNotNull(TEXT("LineWidth is a reflected property"), WidthProperty))
	{
		TestEqual(TEXT("with a floor of zero, which it did not carry before"),
			WidthProperty->GetMetaData(TEXT("ClampMin")), FString(TEXT("0")));
	}

	FVisualFixture Fixture(UDream2DLineRaw::StaticClass(), TEXT("Raw"));
	UDream2DLineRaw* Raw = Cast<UDream2DLineRaw>(Fixture.GetVisual());
	if (!TestNotNull(TEXT("the raw line exists on a widget"), Raw))
	{
		Fixture.Teardown();
		return false;
	}

	TestEqual(TEXT("a fresh line is ten units wide"), Raw->GetLineWidth(), 10.0f);
	TestEqual(TEXT("and centred on its path"), Raw->GetLineWidthOffset(), 0.5f);

	// Both ends of the range are reachable: the whole width on one side of the path is a legitimate
	// drawing, and it is the value immediately outside that is not.
	Raw->SetLineWidthOffset(1.0f);
	TestEqual(TEXT("the whole width can sit on one side"), Raw->GetLineWidthOffset(), 1.0f);
	Raw->SetLineWidthOffset(2.0f);
	TestEqual(TEXT("past the ceiling stops at the ceiling, rather than putting the right edge behind the left"),
		Raw->GetLineWidthOffset(), 1.0f);
	Raw->SetLineWidthOffset(0.0f);
	TestEqual(TEXT("and the other end is reachable too"), Raw->GetLineWidthOffset(), 0.0f);
	Raw->SetLineWidthOffset(-1.0f);
	TestEqual(TEXT("below the floor stops at the floor"), Raw->GetLineWidthOffset(), 0.0f);

	// A negative width was the same defect wearing different clothes: both halves flip at once, so
	// the strip drew the correct size with its edges swapped.
	Raw->SetLineWidth(-10.0f);
	TestEqual(TEXT("a negative width is refused down to nothing"), Raw->GetLineWidth(), 0.0f);
	Raw->SetLineWidth(0.0f);
	TestEqual(TEXT("and no width at all is a legitimate way to hide the line"),
		Raw->GetLineWidth(), 0.0f);
	Raw->SetLineWidth(4.0f);
	TestEqual(TEXT("a positive width is taken as written"), Raw->GetLineWidth(), 4.0f);

	Fixture.Teardown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLineMiterLimitTest,
	"DreamGUI.Extensions.ASharpCornerProducesABoundedMiterRatherThanASpike",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamLineMiterLimitTest::RunTest(const FString& Parameters)
{
	// The joint decision, and the only piece of this class's geometry a test with no canvas can
	// reach. ComputeMiterScale answers "how far out along the joint normal do the two edge vertices
	// have to sit for the strip's outer edges to meet", as a multiple of the half-width, given the
	// sine of half the included angle.
	//
	// A right-angle corner is the reference: half of ninety degrees has a sine of about 0.7071, and
	// the miter is the square root of two -- which is just the diagonal of the square the two strips
	// overlap in, so it can be checked by hand rather than against this function's own arithmetic.
	TestEqual(TEXT("a straight run needs no extension at all"),
		UDream2DLineRendererBase::ComputeMiterScale(1.0f), 1.0, 1e-6);
	TestEqual(TEXT("a right-angle corner reaches out by the diagonal of the overlap"),
		UDream2DLineRendererBase::ComputeMiterScale(FMath::Sin(UE_PI * 0.25f)), UE_DOUBLE_SQRT_2, 1e-5);

	// The blow-up. A polyline doubling exactly back on itself makes the two neighbour directions
	// coincide: the dot is 1, the angle is 0, the sine is 0, and the miter is infinite. This used to
	// divide anyway and write an infinity straight into the vertex buffer, where nothing asserts.
	const double DoubledBack = UDream2DLineRendererBase::ComputeMiterScale(0.0f);
	TestTrue(TEXT("a path that doubles back produces a number rather than an infinity"),
		FMath::IsFinite(DoubledBack));
	TestEqual(TEXT("and that number is the limit itself"), DoubledBack, 4.0, 1e-6);

	// The limit is the more interesting half, because it is not about a degenerate input. A merely
	// sharp corner produces a merely enormous spike by the same arithmetic -- unbounded, five
	// degrees of included angle asks for twenty-three times the line width, which on a ten unit UI
	// line is a two-hundred unit shard. Four is SVG's stroke-miterlimit default; it bites at about
	// twenty-nine degrees, sharper than any corner drawn on purpose.
	const double FiveDegrees = UDream2DLineRendererBase::ComputeMiterScale(FMath::Sin(UE_PI * 5.0f / 360.0f));
	TestEqual(TEXT("a five degree corner is clamped rather than allowed its twenty-three"),
		FiveDegrees, 4.0, 1e-6);

	// Either side of the limit, so the clamp is a limit and not a constant. A thirty degree included
	// angle is just inside it and passes through untouched.
	const double ThirtyDegrees = UDream2DLineRendererBase::ComputeMiterScale(FMath::Sin(UE_PI * 30.0f / 360.0f));
	TestTrue(TEXT("a thirty degree corner is still under the limit"), ThirtyDegrees < 4.0);
	TestTrue(TEXT("and is still a real miter, longer than a straight run"), ThirtyDegrees > 1.0);

	// The sign of the sine is not a decision this function makes. Which side of the path the joint
	// bulges towards is settled by the caller flipping the normal, so a negative sine here is the
	// same corner and has to answer the same.
	TestEqual(TEXT("the answer does not depend on which way the corner turns"),
		UDream2DLineRendererBase::ComputeMiterScale(-FMath::Sin(UE_PI * 0.25f)),
		UDream2DLineRendererBase::ComputeMiterScale(FMath::Sin(UE_PI * 0.25f)), 1e-9);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamStaticMeshMaterialTest,
	"DreamGUI.Extensions.AStaticMeshPrefersItsReplacementMaterialOverTheMeshsOwn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamStaticMeshMaterialTest::RunTest(const FString& Parameters)
{
	using namespace DreamExtensionVisualsTestLocal;

	FVisualFixture Fixture(UDreamStaticMesh::StaticClass(), TEXT("Mesh"));
	UDreamStaticMesh* Mesh = Cast<UDreamStaticMesh>(Fixture.GetVisual());
	if (!TestNotNull(TEXT("the static mesh exists on a widget"), Mesh))
	{
		Fixture.Teardown();
		return false;
	}

	// No cache yet, which is the state a mesh visual is in from the moment it is dropped on a widget
	// until somebody picks an asset. Asking it what it renders with used to be a null dereference,
	// so this whole test had to attach a cache first and say why; the question is now asked with
	// nothing configured at all, because that is when a Blueprint is most likely to ask it.
	TestNull(TEXT("a mesh with no cache reports no material rather than crashing"),
		Mesh->GetRenderMaterial());

	// And the dynamic-instance path, which is the other BlueprintCallable route through the same
	// getter. Its "no material" branch existed all along and was unreachable, because the getter
	// took the process down before it could return null.
	AddExpectedMessage(TEXT("Material is invalid on"),
		ELogVerbosity::Warning, EAutomationExpectedErrorFlags::Contains, -1);
	TestNull(TEXT("and it cannot make a dynamic instance out of nothing"),
		Mesh->GetOrCreateDynamicMaterialInstance());

	// A cache is attached now and never removed for the rest of this test.
	UDreamUIStaticMeshCacheData* Cache = NewObject<UDreamUIStaticMeshCacheData>(GetTransientPackage());
	Mesh->SetMesh(Cache);
	TestEqual(TEXT("the cache is what was handed over"), Mesh->GetMeshCache(), Cache);

	// An empty cache is the state a freshly made one is in, and the render path has to recognise it:
	// CreateGeometry reads MeshCache->GetVertexData() straight into a memcpy loop, so "no data" has
	// to be answered before it, not inside it.
	TestEqual(TEXT("a fresh cache holds no vertices"), Cache->GetVertexData().Num(), 0);
	TestEqual(TEXT("nor any indices"), Cache->GetIndexData().Num(), 0);
	TestFalse(TEXT("so the mesh reports it has nothing to draw"),
		static_cast<UDreamVisualDirectMesh*>(Mesh)->HaveValidData());

	// The precedence itself. A static mesh carries the material its source asset was built with,
	// and ReplaceMaterial is the per-instance override -- so the override has to win while it is
	// set and get out of the way the moment it is cleared. Getting that backwards is invisible
	// until somebody clears an override and the old material keeps drawing.
	TestNull(TEXT("with no override, the material comes from the cache"), Mesh->GetRenderMaterial());

	UMaterial* Replacement = UMaterial::GetDefaultMaterial(MD_Surface);
	Mesh->SetReplaceMaterial(Replacement);
	TestEqual(TEXT("the override is remembered"), Mesh->GetReplaceMaterial(),
		static_cast<UMaterialInterface*>(Replacement));
	TestEqual(TEXT("and it is what actually renders"), Mesh->GetRenderMaterial(),
		static_cast<UMaterialInterface*>(Replacement));

	Mesh->SetReplaceMaterial(nullptr);
	TestNull(TEXT("clearing the override falls back to the cache again"), Mesh->GetRenderMaterial());

	// The colour mode is the mesh's only other authored decision and it is read once per vertex in
	// the copy loop, so it has to survive the round trip untouched.
	TestEqual(TEXT("a static mesh ignores the UI colour by default"),
		Mesh->GetVertexColorType(), EDreamStaticMeshVertexColorType::NotAffectByUIColor);
	Mesh->SetVertexColorType(EDreamStaticMeshVertexColorType::ReplaceByUIColor);
	TestEqual(TEXT("and takes the mode it is given"),
		Mesh->GetVertexColorType(), EDreamStaticMeshVertexColorType::ReplaceByUIColor);

	Fixture.Teardown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDream2DLineChildSubscriptionTest,
	"DreamGUI.Extensions.AChildrenAsPointsLineListensToTheChildrenItDrawsFrom",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDream2DLineChildSubscriptionTest::RunTest(const FString& Parameters)
{
	// This line's points ARE its children's positions, so a child moving is the one event it cannot
	// afford to miss -- and until now it missed all of them. OnChildPositionChanged, the hook that
	// marks the vertices dirty, had no callers anywhere in the plugin: dragging a point around
	// recalculated the point array on whatever geometry pass happened along next and threw the
	// result away, because nothing had marked the pass that mattered.
	//
	// What is pinned here is the WIRING, not the redraw. The dirty flags live behind a protected
	// GetAnythingDirty and the geometry pass that would consume them needs a canvas to pump, so the
	// observable is which object the child's transform event is bound to. That is exactly the thing
	// that was missing, and it is enough to catch the two ways this can regress: a hookup that never
	// happens, and one that is never undone.
	UDreamWidgetTree* Tree = NewObject<UDreamWidgetTree>(GetTransientPackage());
	UDreamWidget* Root = Tree->ConstructWidget(UDreamWidget::StaticClass(), TEXT("Line"));
	UDreamWidget* PointA = Tree->ConstructWidget(UDreamWidget::StaticClass(), TEXT("PointA"));
	UDreamWidget* PointB = Tree->ConstructWidget(UDreamWidget::StaticClass(), TEXT("PointB"));
	UDreamWidget* LatePoint = Tree->ConstructWidget(UDreamWidget::StaticClass(), TEXT("LatePoint"));
	if (!TestNotNull(TEXT("the line's widget was constructed"), Root) ||
		!TestNotNull(TEXT("the first point was constructed"), PointA) ||
		!TestNotNull(TEXT("the second point was constructed"), PointB) ||
		!TestNotNull(TEXT("the late point was constructed"), LatePoint))
	{
		return false;
	}
	Tree->RootWidget = Root;
	Root->SetWidth(200.0f);
	Root->SetHeight(100.0f);
	PointA->TrySetParent(Root, false);
	PointB->TrySetParent(Root, false);

	UDream2DLineChildrenAsPoints* Line = Cast<UDream2DLineChildrenAsPoints>(
		Root->CreateNewVisual(UDream2DLineChildrenAsPoints::StaticClass()));
	if (!TestNotNull(TEXT("the line visual was created"), Line))
	{
		Root->DestroyWidget();
		LatePoint->DestroyWidget();
		return false;
	}

	// Registration is where the hookup happens, and it has to reach every child already in place.
	TestTrue(TEXT("the line listens to the first point it draws from"),
		PointA->GetTransformChangedEvent().IsBoundToObject(Line));
	TestTrue(TEXT("and to the second"),
		PointB->GetTransformChangedEvent().IsBoundToObject(Line));

	// A widget that is not a child is not a point, so it is not listened to. The set is derived from
	// the children list rather than from whatever happens to be in the tree.
	TestFalse(TEXT("but not to a widget that is not one of its children"),
		LatePoint->GetTransformChangedEvent().IsBoundToObject(Line));

	// Children come and go. Replacing the visual is the reachable way to force a re-sync from a test
	// with no world -- OnChildDimensionsChanged, the other re-sync point, is gated on GetWorld() one
	// level up in UDreamUIBehaviour and never fires in an authoring tree -- and it exercises the
	// unregister half at the same time, which is the half a leak hides in.
	PointB->TrySetParent(nullptr, false);
	LatePoint->TrySetParent(Root, false);

	UDream2DLineChildrenAsPoints* Successor = Cast<UDream2DLineChildrenAsPoints>(
		Root->CreateNewVisual(UDream2DLineChildrenAsPoints::StaticClass()));
	if (TestNotNull(TEXT("a second line visual replaced the first"), Successor))
	{
		TestFalse(TEXT("the replaced line let go of the child it still had"),
			PointA->GetTransformChangedEvent().IsBoundToObject(Line));
		TestFalse(TEXT("and of the one that had been detached from under it"),
			PointB->GetTransformChangedEvent().IsBoundToObject(Line));

		TestTrue(TEXT("the new line picked up the child that stayed"),
			PointA->GetTransformChangedEvent().IsBoundToObject(Successor));
		TestTrue(TEXT("and the one that arrived after the first line was built"),
			LatePoint->GetTransformChangedEvent().IsBoundToObject(Successor));
		TestFalse(TEXT("and not the one that left"),
			PointB->GetTransformChangedEvent().IsBoundToObject(Successor));

		// Swapping to a visual that is not a line at all unregisters the last one, which is the
		// plain unsubscribe path with nothing taking over.
		Root->CreateNewVisual(UDreamPolygon::StaticClass());
		TestFalse(TEXT("replacing the line with another shape leaves no listener behind"),
			PointA->GetTransformChangedEvent().IsBoundToObject(Successor));
		TestFalse(TEXT("on any of its points"),
			LatePoint->GetTransformChangedEvent().IsBoundToObject(Successor));
	}

	Root->DestroyWidget();
	PointB->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamExtensionVisualsAuthoringTreeTest,
	"DreamGUI.Extensions.EveryExtensionVisualCanBeBuiltAndTornDownWithoutAWorld",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamExtensionVisualsAuthoringTreeTest::RunTest(const FString& Parameters)
{
	using namespace DreamExtensionVisualsTestLocal;

	// The authoring tree has no world and no render canvas. That is not a test artefact -- it is
	// the state the prefab designer, the .dui builder and every class template live in, and a
	// visual that reaches through GetWorld() or GetRenderCanvas() during registration takes the
	// editor down when somebody drops it on a widget rather than when somebody plays.
	//
	// The extension visuals are the ones nobody has swept for that: they arrived from the fork,
	// where registration always happened under a world. Each of them registers here (CreateNewVisual
	// calls Call_OnRegister) and unregisters again on teardown.
	const TArray<UClass*> ExtensionVisuals =
	{
		UDreamPolygon::StaticClass(),
		UDreamPolygonLine::StaticClass(),
		UDreamRing::StaticClass(),
		UDream2DLineRaw::StaticClass(),
		UDream2DLineChildrenAsPoints::StaticClass(),
		UDreamCanvasRenderTargetPreviewer::StaticClass(),
		UDreamPostProcessRenderElement::StaticClass(),
		UDreamPostProcessRenderElement_Text::StaticClass(),
		UDreamStaticMesh::StaticClass(),
	};

	for (UClass* VisualClass : ExtensionVisuals)
	{
		const FString ClassName = VisualClass->GetName();
		FVisualFixture Fixture(VisualClass, TEXT("Shape"));
		if (!TestNotNull(*FString::Printf(TEXT("%s: the widget was constructed"), *ClassName),
			Fixture.Widget))
		{
			continue;
		}

		TestNull(*FString::Printf(TEXT("%s: the authoring tree has no world"), *ClassName),
			Fixture.Widget->GetWorld());
		TestNull(*FString::Printf(TEXT("%s: and nothing renders it yet"), *ClassName),
			Fixture.Widget->GetRenderCanvas());

		UDreamVisual* Visual = Fixture.GetVisual();
		if (TestNotNull(*FString::Printf(TEXT("%s: the visual was created and registered"), *ClassName),
			Visual))
		{
			TestTrue(*FString::Printf(TEXT("%s: and it is the class that was asked for"), *ClassName),
				Visual->IsA(VisualClass));

			// A batched visual owns its geometry buffer from construction -- both the previewer and
			// the line base call Clear() on it inside OnUpdateGeometry before anything has had a
			// chance to allocate one, so a null buffer there would be a null dereference on the
			// first frame of an unconfigured widget rather than an empty draw.
			if (UDreamVisualBatchMesh* Batched = Cast<UDreamVisualBatchMesh>(Visual))
			{
				if (TestNotNull(*FString::Printf(TEXT("%s: it owns a geometry buffer already"), *ClassName),
					Batched->GetGeometry()))
				{
					TestEqual(*FString::Printf(TEXT("%s: which starts empty"), *ClassName),
						Batched->GetGeometry()->OriginVertices.Num(), 0);
					TestEqual(*FString::Printf(TEXT("%s: with no triangles either"), *ClassName),
						Batched->GetGeometry()->Triangles.Num(), 0);
				}

				// Marking dirty is the call every one of these makes from its own setters, and it
				// walks back through the widget to a canvas that is not there. Doing it once per
				// class is what proves the walk tolerates the missing canvas rather than each
				// setter having been lucky.
				Batched->MarkVerticesDirty();
			}
			Visual->MarkAllDirty();
		}

		Fixture.Teardown();
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
