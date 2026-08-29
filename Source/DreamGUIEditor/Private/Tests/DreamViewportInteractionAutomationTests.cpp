// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Designer/DreamWidgetDesignerViewportClient.h"
#include "Core/Components/DreamWidget.h"
#include "Core/Components/DreamLayout.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Engine/World.h"
#include "UObject/StrongObjectPtr.h"

// The three viewport gestures whose answers cannot be read off the screen.
//
// A Move that lands on an arranged widget is discarded by the next arrange, yet it still opened a
// transaction, dirtied the prefab and possibly wrote an animation key -- so the gesture has to be
// refused before any of that, per axis, because a widget arranged in X may still be free in Y.
//
// A multi-widget Move used to snap the first widget's delta and hand the same numbers to the rest.
// The same numbers are a different distance inside parents of differing scale or rotation, so the
// selection deformed as it was dragged. Every widget is measured and snapped where it lives.
//
// The click-through stack index used to be reset only by mouse-move events, which are not delivered
// for every click, so a click somewhere new could resume the previous stack's depth.
namespace DreamViewportInteractionTestLocal
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

	FDreamWidgetDesignerViewportClient::FMoveDragTarget MakeTarget(const FTransform& InPlane, const FVector& InTravel)
	{
		FDreamWidgetDesignerViewportClient::FMoveDragTarget Target;
		Target.PlaneTransform = InPlane;
		Target.StartPlanePoint = FVector::ZeroVector;
		Target.CurrentPlanePoint = InTravel;
		return Target;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamViewportMoveRefusedWhenArrangedTest,
	"DreamGUI.Editor.ViewportInteraction.MoveIsRefusedWhenEveryAxisIsArranged",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamViewportMoveRefusedWhenArrangedTest::RunTest(const FString& Parameters)
{
	using namespace DreamViewportInteractionTestLocal;
	FScopedTestWorld TestWorld;

	TStrongObjectPtr<UDreamWidget> ArrangingRoot(MakeWidget(TestWorld.World, TEXT("ArrangingRoot"), 800.0f, 600.0f));
	ArrangingRoot->CreateNewLayoutContainer<UDreamLayoutContainerVerticalBox>();
	TStrongObjectPtr<UDreamWidget> Arranged(MakeWidget(ArrangingRoot.Get(), TEXT("Arranged"), 100.0f, 50.0f));
	Arranged->TrySetParent(ArrangingRoot.Get(), false);

	// A canvas panel decides nothing about a plain child's position, which is why the answer cannot
	// be "the parent has a container".
	TStrongObjectPtr<UDreamWidget> CanvasRoot(MakeWidget(TestWorld.World, TEXT("CanvasRoot"), 800.0f, 600.0f));
	CanvasRoot->CreateNewLayoutContainer<UDreamLayoutContainerCanvasPanel>();
	TStrongObjectPtr<UDreamWidget> Free(MakeWidget(CanvasRoot.Get(), TEXT("Free"), 100.0f, 50.0f));
	Free->TrySetParent(CanvasRoot.Get(), false);

	TArray<UDreamWidget*> OnlyArranged = { Arranged.Get() };
	TArray<UDreamWidget*> OnlyFree = { Free.Get() };
	TArray<UDreamWidget*> Mixed = { Arranged.Get(), Free.Get() };
	TestFalse(TEXT("a widget a box arranges cannot be moved"), FDreamWidgetDesignerViewportClient::CanMoveSelection(OnlyArranged));
	TestTrue(TEXT("a canvas child can"), FDreamWidgetDesignerViewportClient::CanMoveSelection(OnlyFree));
	TestTrue(TEXT("and one free widget keeps the gesture alive for the selection"), FDreamWidgetDesignerViewportClient::CanMoveSelection(Mixed));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamViewportNudgeDropsArrangedAxisTest,
	"DreamGUI.Editor.ViewportInteraction.NudgeDropsTheArrangedAxisNotTheWidget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamViewportNudgeDropsArrangedAxisTest::RunTest(const FString& Parameters)
{
	const FVector2D Nudge(3.0, 5.0);

	FDreamLayoutControlAnchorData Free;
	TestTrue(TEXT("nothing arranged, nothing dropped"),
		FDreamWidgetDesignerViewportClient::FilterMoveDelta(Nudge, Free).Equals(Nudge));

	FDreamLayoutControlAnchorData HorizontalTaken;
	HorizontalTaken.bCanControlHorizontalPosition = true;
	TestTrue(TEXT("an arranged X keeps its free Y"),
		FDreamWidgetDesignerViewportClient::FilterMoveDelta(Nudge, HorizontalTaken).Equals(FVector2D(0.0, 5.0)));

	FDreamLayoutControlAnchorData BothTaken;
	BothTaken.bCanControlHorizontalPosition = true;
	BothTaken.bCanControlVerticalPosition = true;
	// Zero is what the nudge reads to decide there is nothing to open a transaction for.
	TestTrue(TEXT("both arranged leaves nothing to write"),
		FDreamWidgetDesignerViewportClient::FilterMoveDelta(Nudge, BothTaken).IsZero());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamViewportMoveDragMeasuresEachParentTest,
	"DreamGUI.Editor.ViewportInteraction.MoveDragMeasuresEachWidgetInItsOwnParent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamViewportMoveDragMeasuresEachParentTest::RunTest(const FString& Parameters)
{
	using namespace DreamViewportInteractionTestLocal;

	// One pointer travel, two parents. The parent-local distance the same world travel covers is
	// halved by a parent twice as large, so a single shared delta stretches the selection.
	const FVector Travel(0.0, 10.0, 0.0);
	TArray<FDreamWidgetDesignerViewportClient::FMoveDragTarget> Targets;
	Targets.Add(MakeTarget(FTransform::Identity, Travel));
	Targets.Add(MakeTarget(FTransform(FQuat::Identity, FVector::ZeroVector, FVector(2.0)), Travel));

	TArray<FDreamWidgetDesignerViewportClient::FMoveDragResult> Results;
	FDreamWidgetDesignerViewportClient::ResolveMoveDrag(Targets, 0.0f, Results);
	if (!TestEqual(TEXT("one result per target"), Results.Num(), 2))return false;
	TestTrue(TEXT("the unscaled parent travels the full distance"), Results[0].Position.Equals(FVector2D(10.0, 0.0)));
	TestTrue(TEXT("the doubled parent travels half of it"), Results[1].Position.Equals(FVector2D(5.0, 0.0)));
	TestFalse(TEXT("no grid, no snap to report"), Results[0].bSnappedHorizontal);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamViewportSnapCrossesParentScaleTest,
	"DreamGUI.Editor.ViewportInteraction.SnapKeepsItsMeaningAcrossParentScales",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamViewportSnapCrossesParentScaleTest::RunTest(const FString& Parameters)
{
	using namespace DreamViewportInteractionTestLocal;

	// Two widgets under parents of different scale, dragged together. The grid is consulted once so
	// the pair stays rigid, but "once" is a distance on screen, not a number: 2 units of correction
	// inside a parent scaled by 2 is 1 unit inside an unscaled one. Handing the same number to both
	// moves them different distances on screen, which is the deformation snapping once exists to
	// avoid -- the bug hides completely whenever every parent happens to share a scale.
	const FVector Travel(0.0, 10.0, 0.0);
	TArray<FDreamWidgetDesignerViewportClient::FMoveDragTarget> Targets;
	Targets.Add(MakeTarget(FTransform::Identity, Travel));
	Targets.Add(MakeTarget(FTransform(FQuat::Identity, FVector::ZeroVector, FVector(2.0)), Travel));

	TArray<FDreamWidgetDesignerViewportClient::FMoveDragResult> Results;
	FDreamWidgetDesignerViewportClient::ResolveMoveDrag(Targets, 8.0f, Results);
	if (!TestEqual(TEXT("one result per target"), Results.Num(), 2))return false;

	// The leader travels 10 in its own space and the grid pulls it back to 8: a correction of -2,
	// which is -2 on screen. The doubled parent measured 5 for the same travel and must give up the
	// same -2 of SCREEN distance, which is 1 of its own units: 5 - 1 = 4.
	TestTrue(TEXT("the leader lands on its gridline"), Results[0].Position.Equals(FVector2D(8.0, 0.0)));
	TestTrue(TEXT("and the scaled parent gives up the same screen distance, not the same number"),
		Results[1].Position.Equals(FVector2D(4.0, 0.0), 0.001));

	// What rigidity actually means: convert both back to screen distance and the gap is unchanged.
	const double LeaderScreen = Results[0].Position.X;
	const double ScaledScreen = Results[1].Position.X * 2.0;
	TestEqual(TEXT("the pair covers the same screen distance it did before the grid spoke"),
		ScaledScreen, LeaderScreen, 0.001);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamViewportMoveDragLeavesArrangedAxisTest,
	"DreamGUI.Editor.ViewportInteraction.MoveDragLeavesTheArrangedAxisWhereItWas",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamViewportMoveDragLeavesArrangedAxisTest::RunTest(const FString& Parameters)
{
	using namespace DreamViewportInteractionTestLocal;

	FDreamWidgetDesignerViewportClient::FMoveDragTarget Target = MakeTarget(FTransform::Identity, FVector(0.0, 10.0, 7.0));
	Target.StartPosition = FVector2D(100.0, 200.0);
	Target.bHorizontalFree = false;
	TArray<FDreamWidgetDesignerViewportClient::FMoveDragTarget> Targets = { Target };

	TArray<FDreamWidgetDesignerViewportClient::FMoveDragResult> Results;
	FDreamWidgetDesignerViewportClient::ResolveMoveDrag(Targets, 8.0f, Results);
	if (!TestEqual(TEXT("one result"), Results.Num(), 1))return false;
	// A real grid, because with no grid the snap branch never runs and the guard below is
	// unreachable -- the assertion then holds whether or not the axis is honoured.
	TestTrue(TEXT("the arranged axis is untouched and the free one moves to its gridline"),
		Results[0].Position.Equals(FVector2D(100.0, 208.0)));
	TestFalse(TEXT("an axis nothing may write cannot claim a guide"), Results[0].bSnappedHorizontal);
	TestTrue(TEXT("the axis that did move to a gridline claims one"), Results[0].bSnappedVertical);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamViewportClickCycleResetTest,
	"DreamGUI.Editor.ViewportInteraction.ClickCycleRestartsAtANewPixel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamViewportClickCycleResetTest::RunTest(const FString& Parameters)
{
	const FIntPoint Pixel(120, 80);
	TestEqual(TEXT("clicking the same pixel walks deeper into that stack"),
		FDreamWidgetDesignerViewportClient::ResolveClickCycleIndex(Pixel, Pixel, 2), 2);
	TestEqual(TEXT("clicking anywhere else starts at the top of the new one"),
		FDreamWidgetDesignerViewportClient::ResolveClickCycleIndex(Pixel, FIntPoint(121, 80), 2), (int32)INDEX_NONE);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamViewportSafeZoneRectTest,
	"DreamGUI.Editor.ViewportInteraction.SafeZoneInsetsTheCanvasRect",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamViewportSafeZoneRectTest::RunTest(const FString& Parameters)
{
	// 1920x1080 about a centred pivot, inset 5% per side: the rect the platform's title-safe padding
	// leaves, in the same local space the canvas outline is drawn in.
	const FBox2D Safe = FDreamWidgetDesignerViewportClient::GetSafeZoneLocalRect(
		FVector2D(1920.0, 1080.0), FVector2D(0.5, 0.5), FVector4(96.0, 54.0, 96.0, 54.0));
	if (!TestTrue(TEXT("a rect at all"), Safe.bIsValid))return false;
	TestTrue(TEXT("inset from the bottom-left"), Safe.Min.Equals(FVector2D(-864.0, -486.0)));
	TestTrue(TEXT("and from the top-right"), Safe.Max.Equals(FVector2D(864.0, 486.0)));

	const FBox2D Swallowed = FDreamWidgetDesignerViewportClient::GetSafeZoneLocalRect(
		FVector2D(100.0, 100.0), FVector2D(0.5, 0.5), FVector4(80.0, 0.0, 80.0, 0.0));
	TestFalse(TEXT("padding wider than the canvas leaves no rect, not an inside-out one"), Swallowed.bIsValid);
	return true;
}

#endif
