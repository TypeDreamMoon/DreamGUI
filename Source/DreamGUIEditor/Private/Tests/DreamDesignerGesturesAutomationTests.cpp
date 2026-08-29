// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Designer/DreamWidgetDesignerViewportClient.h"
#include "Core/Components/DreamWidget.h"
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

#endif
