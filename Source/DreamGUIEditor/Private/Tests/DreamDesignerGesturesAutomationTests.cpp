// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Designer/DreamWidgetDesignerViewportClient.h"
#include "Designer/DreamWidgetBlueprintEditor.h"
#include "Thumbnail/DreamWidgetBlueprintThumbnailRenderer.h"
#include "Core/Components/DreamWidget.h"
#include "Core/Components/DreamWidgetPlacement.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Engine/World.h"

// Two designer gestures that the viewport can only perform with a live FSceneView, so each one's
// decision is a static taking geometry rather than widgets-and-a-viewport: the anchor medallion's
// rect-preservation and axis policy, and the marquee's screen-rect test and selection folding.
namespace DreamDesignerGestureTestLocal
{
	struct FScopedTestWorld
	{
		UWorld* World = nullptr;
		FScopedTestWorld() { World = UWorld::CreateWorld(EWorldType::Editor, false); }
		~FScopedTestWorld() { if (World) { World->DestroyWorld(false); } }
	};

	UDreamWidget* MakeWidget(UObject* Outer, const TCHAR* Name, float Width, float Height)
	{
		UDreamWidget* Widget = NewObject<UDreamWidget>(Outer);
		Widget->SetDisplayName(Name);
		Widget->SetWidth(Width);
		Widget->SetHeight(Height);
		return Widget;
	}

	/** A four-corner quad, in the ring order the projection emits. */
	TArray<FVector2D> MakeQuad(const FVector2D& A, const FVector2D& B, const FVector2D& C, const FVector2D& D)
	{
		return TArray<FVector2D>{ A, B, C, D };
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamAnchorDragKeepsTheRectTest,
	"DreamGUI.Editor.DesignerAnchors.MovingAnAnchorLeavesTheRectWhereItIs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamAnchorDragKeepsTheRectTest::RunTest(const FString& Parameters)
{
	using namespace DreamDesignerGestureTestLocal;
	FScopedTestWorld TestWorld;

	UDreamWidget* Root = MakeWidget(TestWorld.World, TEXT("Root"), 800.0f, 600.0f);
	UDreamWidget* Child = MakeWidget(Root, TEXT("Child"), 100.0f, 50.0f);
	Child->TrySetParent(Root, false);
	// Off-centre on both axes, so an offset that failed to follow its anchor line shows up as a move
	// rather than cancelling out against a symmetric layout.
	Child->SetAnchoredPosition(FVector2D(120.0f, -80.0f));

	const FVector Location = Child->GetRelativeLocation();
	const FVector2D Size = Child->GetSize();

	// Centre anchors to the bottom-left corner: the anchor line travels 400 x 300 across the parent,
	// which is exactly how far the rect teleports if the offsets are not re-measured against it.
	FDreamWidgetDesignerViewportClient::SetAnchorsPreservingRect(Child, FVector2D(0.0f, 0.0f), FVector2D(0.0f, 0.0f));
	TestTrue(TEXT("the anchors did move"), Child->GetAnchorMin().Equals(FVector2D(0.0f, 0.0f), 0.001f));
	TestTrue(TEXT("and the rect did not"), Child->GetRelativeLocation().Equals(Location, 0.01));
	TestTrue(TEXT("nor did its size"), Child->GetSize().Equals(Size, 0.01));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamAnchorDragToStretchKeepsTheRectTest,
	"DreamGUI.Editor.DesignerAnchors.StretchingTheAnchorsLeavesTheRectWhereItIs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamAnchorDragToStretchKeepsTheRectTest::RunTest(const FString& Parameters)
{
	using namespace DreamDesignerGestureTestLocal;
	FScopedTestWorld TestWorld;

	UDreamWidget* Root = MakeWidget(TestWorld.World, TEXT("Root"), 800.0f, 600.0f);
	UDreamWidget* Child = MakeWidget(Root, TEXT("Child"), 100.0f, 50.0f);
	Child->TrySetParent(Root, false);
	Child->SetAnchoredPosition(FVector2D(120.0f, -80.0f));

	const FVector Location = Child->GetRelativeLocation();
	const FVector2D Size = Child->GetSize();

	// Pulling the anchors apart is the case where the size itself changes meaning: once stretched,
	// the parent contributes the span and SizeDelta is only what is left over, so a SizeDelta that
	// stayed put would grow the widget to the full 800 x 600.
	FDreamWidgetDesignerViewportClient::SetAnchorsPreservingRect(Child, FVector2D(0.0f, 0.0f), FVector2D(1.0f, 1.0f));
	TestTrue(TEXT("the widget is now stretched"), Child->GetAnchorData().IsHorizontalStretched() && Child->GetAnchorData().IsVerticalStretched());
	TestTrue(TEXT("the rect did not move"), Child->GetRelativeLocation().Equals(Location, 0.01));
	TestTrue(TEXT("and kept its size"), Child->GetSize().Equals(Size, 0.01));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamAnchorSnapTest,
	"DreamGUI.Editor.DesignerAnchors.NearGridlinesSnapAndTheRestDoNot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamAnchorSnapTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("just past a gridline"), FDreamWidgetDesignerViewportClient::SnapAnchorFraction(0.262, 0.02), 0.25);
	TestEqual(TEXT("just short of the far edge"), FDreamWidgetDesignerViewportClient::SnapAnchorFraction(0.99, 0.02), 1.0);
	// Halfway between two stops belongs to neither: snapping everything would make the free positions
	// the gesture exists to reach unreachable.
	TestEqual(TEXT("between two stops"), FDreamWidgetDesignerViewportClient::SnapAnchorFraction(0.4, 0.02), 0.4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamAnchorAxisPolicyTest,
	"DreamGUI.Editor.DesignerAnchors.OnlyAxesNothingElseDecidesGetHandles",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamAnchorAxisPolicyTest::RunTest(const FString& Parameters)
{
	using namespace DreamDesignerGestureTestLocal;
	FScopedTestWorld TestWorld;

	UDreamWidget* Orphan = MakeWidget(TestWorld.World, TEXT("Orphan"), 100.0f, 50.0f);
	bool bHorizontal = true, bVertical = true;
	FDreamWidgetDesignerViewportClient::GetAnchorEditableAxes(Orphan, bHorizontal, bVertical);
	TestFalse(TEXT("a parentless widget has no anchor space at all"), bHorizontal || bVertical);

	UDreamWidget* CanvasRoot = MakeWidget(TestWorld.World, TEXT("CanvasRoot"), 800.0f, 600.0f);
	CanvasRoot->CreateNewLayoutContainer<UDreamLayoutContainerCanvasPanel>();
	UDreamWidget* CanvasChild = MakeWidget(CanvasRoot, TEXT("CanvasChild"), 100.0f, 50.0f);
	CanvasChild->TrySetParent(CanvasRoot, false);
	FDreamWidgetDesignerViewportClient::GetAnchorEditableAxes(CanvasChild, bHorizontal, bVertical);
	TestTrue(TEXT("a canvas child is placed by its anchors, so both axes are the author's"), bHorizontal && bVertical);

	// The case the gesture must not offer: a vertical box writes its children's position and size, so
	// an anchor the author dragged there would be overwritten on the next arrange.
	UDreamWidget* BoxRoot = MakeWidget(TestWorld.World, TEXT("BoxRoot"), 800.0f, 600.0f);
	BoxRoot->CreateNewLayoutContainer<UDreamLayoutContainerVerticalBox>();
	UDreamWidget* BoxChild = MakeWidget(BoxRoot, TEXT("BoxChild"), 100.0f, 50.0f);
	BoxChild->TrySetParent(BoxRoot, false);
	FDreamWidgetDesignerViewportClient::GetAnchorEditableAxes(BoxChild, bHorizontal, bVertical);
	TestFalse(TEXT("an arranged child gets no anchor handles"), bHorizontal || bVertical);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamMarqueeMeetsRectTest,
	"DreamGUI.Editor.DesignerMarquee.BoxCatchesOnlyTheRectsItActuallyCrosses",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMarqueeMeetsRectTest::RunTest(const FString& Parameters)
{
	using namespace DreamDesignerGestureTestLocal;

	const TArray<FVector2D> Upright = MakeQuad(FVector2D(100, 100), FVector2D(200, 100), FVector2D(200, 160), FVector2D(100, 160));
	TestTrue(TEXT("a box laid over the rect catches it"), FDreamWidgetDesignerViewportClient::DoesMarqueeMeetQuad(FBox2D(FVector2D(80, 90), FVector2D(150, 150)), Upright));
	TestTrue(TEXT("a box entirely inside it counts as crossing it"), FDreamWidgetDesignerViewportClient::DoesMarqueeMeetQuad(FBox2D(FVector2D(120, 120), FVector2D(130, 130)), Upright));
	TestFalse(TEXT("a box beside it does not"), FDreamWidgetDesignerViewportClient::DoesMarqueeMeetQuad(FBox2D(FVector2D(0, 0), FVector2D(60, 60)), Upright));

	// A rotated widget projects to a diamond. Its bounding box reaches into all four corners it does
	// not occupy, so a box-against-bounds test would hand it a marquee that never touched it.
	const TArray<FVector2D> Diamond = MakeQuad(FVector2D(100, 50), FVector2D(150, 100), FVector2D(100, 150), FVector2D(50, 100));
	TestFalse(TEXT("a corner of the bounding box is not the widget"), FDreamWidgetDesignerViewportClient::DoesMarqueeMeetQuad(FBox2D(FVector2D(52, 52), FVector2D(62, 62)), Diamond));
	TestTrue(TEXT("but its middle is"), FDreamWidgetDesignerViewportClient::DoesMarqueeMeetQuad(FBox2D(FVector2D(95, 95), FVector2D(105, 105)), Diamond));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamMarqueeSelectionModesTest,
	"DreamGUI.Editor.DesignerMarquee.CtrlAddsAltRemovesNeitherReplaces",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMarqueeSelectionModesTest::RunTest(const FString& Parameters)
{
	using namespace DreamDesignerGestureTestLocal;
	using EMarqueeMode = FDreamWidgetDesignerViewportClient::EMarqueeMode;
	FScopedTestWorld TestWorld;

	UDreamWidget* A = MakeWidget(TestWorld.World, TEXT("A"), 10.0f, 10.0f);
	UDreamWidget* B = MakeWidget(TestWorld.World, TEXT("B"), 10.0f, 10.0f);
	UDreamWidget* C = MakeWidget(TestWorld.World, TEXT("C"), 10.0f, 10.0f);
	const TArray<UDreamWidget*> Current{ A, B };
	const TArray<UDreamWidget*> Caught{ B, C };

	TSet<UDreamWidget*> Result;
	FDreamWidgetDesignerViewportClient::CombineMarqueeSelection(EMarqueeMode::Replace, Current, Caught, Result);
	TestTrue(TEXT("a plain marquee replaces"), Result.Num() == 2 && Result.Contains(B) && Result.Contains(C));

	// B is in both sets, which is where a toggle would go wrong: ctrl+marquee over something already
	// selected has to leave it selected, not turn it off.
	FDreamWidgetDesignerViewportClient::CombineMarqueeSelection(EMarqueeMode::Add, Current, Caught, Result);
	TestTrue(TEXT("ctrl adds without toggling"), Result.Num() == 3 && Result.Contains(A) && Result.Contains(B) && Result.Contains(C));

	FDreamWidgetDesignerViewportClient::CombineMarqueeSelection(EMarqueeMode::Remove, Current, Caught, Result);
	TestTrue(TEXT("alt removes, and never adds what it caught"), Result.Num() == 1 && Result.Contains(A));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerEdgeSnapTest,
	"DreamGUI.Designer.ADragSnapsToASiblingsEdgeBeforeItSnapsToTheGrid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The alignment rule, stated without a viewport.
 *
 * The designer had a grid and nothing else, so lining a button up with the one above it was done by
 * eye or by typing numbers -- and the grid actively got in the way, catching the drag a few units
 * short of the edge the author could see they were aiming at.
 */
bool FDreamDesignerEdgeSnapTest::RunTest(const FString&)
{
	using FEdgeSnapResult = FDreamWidgetDesignerViewportClient::FEdgeSnapResult;
	// A sibling occupying 100..300, so its edges and centre are the three lines on offer.
	const TArray<double> Lines = { 100.0, 200.0, 300.0 };

	// Low edge to low edge: a rect at 104..154 is four short of the 100 line.
	{
		const FEdgeSnapResult Snap = FDreamWidgetDesignerViewportClient::SolveEdgeSnap(104.0, 154.0, Lines, 6.0);
		TestTrue(TEXT("a near low edge snaps"), Snap.bSnapped);
		TestEqual(TEXT("and moves onto the line"), Snap.Delta, -4.0);
		TestEqual(TEXT("and the guide belongs on that line"), Snap.Line, 100.0);
	}
	// High edge to high edge, which the low edge cannot reach.
	{
		const FEdgeSnapResult Snap = FDreamWidgetDesignerViewportClient::SolveEdgeSnap(248.0, 298.0, Lines, 6.0);
		TestTrue(TEXT("a near high edge snaps"), Snap.bSnapped);
		TestEqual(TEXT("by the distance to it"), Snap.Delta, 2.0);
		TestEqual(TEXT("on the far line"), Snap.Line, 300.0);
	}
	// Centre to centre: 176..226 has its middle at 201, one from the sibling's 200.
	{
		const FEdgeSnapResult Snap = FDreamWidgetDesignerViewportClient::SolveEdgeSnap(176.0, 226.0, Lines, 6.0);
		TestTrue(TEXT("centres snap to centres"), Snap.bSnapped);
		TestEqual(TEXT("by one unit"), Snap.Delta, -1.0);
	}
	// Out of reach stays where it is; a drag must not jump to a line the author is nowhere near.
	{
		const FEdgeSnapResult Snap = FDreamWidgetDesignerViewportClient::SolveEdgeSnap(140.0, 190.0, Lines, 6.0);
		TestFalse(TEXT("nothing within tolerance does not snap"), Snap.bSnapped);
		TestEqual(TEXT("and asks for no correction"), Snap.Delta, 0.0);
	}
	// No lines and no tolerance are both "off", which is what every caller that gathers no siblings
	// passes -- the case that must leave the grid exactly as it was.
	{
		TestFalse(TEXT("no lines means no snap"),
			FDreamWidgetDesignerViewportClient::SolveEdgeSnap(104.0, 154.0, TArray<double>(), 6.0).bSnapped);
		TestFalse(TEXT("zero tolerance means no snap"),
			FDreamWidgetDesignerViewportClient::SolveEdgeSnap(104.0, 154.0, Lines, 0.0).bSnapped);
	}

	// And through the drag resolver: a sibling edge beats the gridline that would otherwise win.
	{
		FDreamWidgetDesignerViewportClient::FMoveDragTarget Target;
		Target.StartPosition = FVector2D(129.0, 0.0);
		Target.Size = FVector2D(50.0, 50.0);
		Target.Pivot = FVector2D(0.5, 0.5);
		// No travel: the drag is asked where it would land if the mouse had not moved, so the answer
		// is entirely the snapping.
		TArray<FDreamWidgetDesignerViewportClient::FMoveDragResult> Results;
		FDreamWidgetDesignerViewportClient::FSiblingSnapLines SnapLines;
		SnapLines.Horizontal = Lines;
		SnapLines.Tolerance = 6.0;
		// Pivot-centred at 129 means the rect runs 104..154, four from the sibling's 100 line; the
		// grid of 8 would have caught the POSITION at 128, which is one unit the other way.
		TArray<FDreamWidgetDesignerViewportClient::FMoveDragTarget> Targets;
		Targets.Add(Target);
		FDreamWidgetDesignerViewportClient::ResolveMoveDrag(Targets, 8.0f, Results, SnapLines);
		if (!TestEqual(TEXT("one target answers once"), Results.Num(), 1))
		{
			return false;
		}
		TestEqual(TEXT("the sibling edge wins the axis, not the gridline"), Results[0].Position.X, 125.0);
		TestTrue(TEXT("and the axis reports snapped, so a guide is drawn"), Results[0].bSnappedHorizontal);

		// With no lines, the same drag is the grid's again -- the proof that this is an addition.
		Results.Reset();
		FDreamWidgetDesignerViewportClient::ResolveMoveDrag(Targets, 8.0f, Results);
		TestEqual(TEXT("without siblings the gridline still decides"), Results[0].Position.X, 128.0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerDPIPreviewTest,
	"DreamGUI.Designer.TheDPIPreviewShrinksTheCanvasAndNeverToZero",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDesignerDPIPreviewTest::RunTest(const FString&)
{
	// Off, and the identity -- which is what every author who has not asked for this gets.
	TestTrue(TEXT("a scale of one changes nothing"),
		FDreamWidgetBlueprintEditor::ApplyDPIScaleToViewportSize(FIntPoint(1920, 1080), 1.0f) == FIntPoint(1920, 1080));
	TestTrue(TEXT("and so does a scale of zero, which is 'no answer' rather than 'infinitely small'"),
		FDreamWidgetBlueprintEditor::ApplyDPIScaleToViewportSize(FIntPoint(1920, 1080), 0.0f) == FIntPoint(1920, 1080));
	TestTrue(TEXT("a 2x DPI halves the surface the widget is laid out on"),
		FDreamWidgetBlueprintEditor::ApplyDPIScaleToViewportSize(FIntPoint(1920, 1080), 2.0f) == FIntPoint(960, 540));
	// A canvas of zero measures every widget in it at zero, which is a hierarchy that is structurally
	// perfect and entirely invisible. It must not be reachable from a curve.
	TestTrue(TEXT("an absurd scale still leaves a canvas of at least one"),
		FDreamWidgetBlueprintEditor::ApplyDPIScaleToViewportSize(FIntPoint(4, 4), 1000.0f) == FIntPoint(1, 1));

	// The thumbnail's two pure decisions, which share the same "a diagram must stay a diagram" rule.
	{
		const FBox2D Tile(FVector2D(0.0, 0.0), FVector2D(64.0, 64.0));
		const FBox2D Fitted = UDreamWidgetBlueprintThumbnailRenderer::FitCanvasIntoThumbnail(Tile, FIntPoint(1920, 1080));
		TestTrue(TEXT("a 16:9 canvas is letterboxed into a square tile, not stretched"),
			FMath::IsNearlyEqual(Fitted.Max.X - Fitted.Min.X, 64.0) && FMath::IsNearlyEqual(Fitted.Max.Y - Fitted.Min.Y, 36.0));
		TestTrue(TEXT("and centred in it"), FMath::IsNearlyEqual(Fitted.Min.Y, 14.0));

		// A stretched child: anchors 0..1 with no size delta fills its parent exactly.
		FDreamUIAnchorData Stretched;
		Stretched.AnchorMin = FVector2D(0.0, 0.0);
		Stretched.AnchorMax = FVector2D(1.0, 1.0);
		Stretched.AnchoredPosition = FVector2D::ZeroVector;
		Stretched.SizeDelta = FVector2D::ZeroVector;
		Stretched.Pivot = FVector2D(0.5, 0.5);
		const FBox2D Parent(FVector2D(0.0, 0.0), FVector2D(100.0, 200.0));
		const FBox2D Filled = UDreamWidgetBlueprintThumbnailRenderer::ResolveAnchoredRect(Parent, Stretched);
		TestTrue(TEXT("a stretched child fills its parent"),
			Filled.Min.Equals(Parent.Min) && Filled.Max.Equals(Parent.Max));

		// A point-anchored child: the size delta IS the size, and the anchored position names the
		// pivot rather than a corner.
		FDreamUIAnchorData Point;
		Point.AnchorMin = FVector2D(0.5, 0.5);
		Point.AnchorMax = FVector2D(0.5, 0.5);
		Point.AnchoredPosition = FVector2D::ZeroVector;
		Point.SizeDelta = FVector2D(40.0, 20.0);
		Point.Pivot = FVector2D(0.5, 0.5);
		const FBox2D Centred = UDreamWidgetBlueprintThumbnailRenderer::ResolveAnchoredRect(Parent, Point);
		TestTrue(TEXT("a centre-anchored child is its size delta, centred"),
			Centred.Min.Equals(FVector2D(30.0, 90.0)) && Centred.Max.Equals(FVector2D(70.0, 110.0)));
	}
	return true;
}

#endif
