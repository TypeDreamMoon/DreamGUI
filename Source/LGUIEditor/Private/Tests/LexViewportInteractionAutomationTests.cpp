// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "PrefabEditor/LexUIPrefabEditorViewportClient.h"
#include "Core/Components/LexWidget.h"
#include "Core/Components/LexLayout.h"
#include "Core/Components/LexPanelLayouts.h"
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
namespace LexViewportInteractionTestLocal
{
	struct FScopedTestWorld
	{
		UWorld* World = nullptr;
		FScopedTestWorld() { World = UWorld::CreateWorld(EWorldType::Editor, false); }
		~FScopedTestWorld() { if (World) { World->DestroyWorld(false); } }
	};

	ULexWidget* MakeWidget(UObject* Outer, const TCHAR* Name, float Width, float Height)
	{
		ULexWidget* Widget = NewObject<ULexWidget>(Outer);
		Widget->SetDisplayName(Name);
		Widget->SetWidth(Width);
		Widget->SetHeight(Height);
		return Widget;
	}

	FLexUIPrefabEditorViewportClient::FMoveDragTarget MakeTarget(const FTransform& InPlane, const FVector& InTravel)
	{
		FLexUIPrefabEditorViewportClient::FMoveDragTarget Target;
		Target.PlaneTransform = InPlane;
		Target.StartPlanePoint = FVector::ZeroVector;
		Target.CurrentPlanePoint = InTravel;
		return Target;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexViewportMoveRefusedWhenArrangedTest,
	"LGUI.Editor.ViewportInteraction.MoveIsRefusedWhenEveryAxisIsArranged",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexViewportMoveRefusedWhenArrangedTest::RunTest(const FString& Parameters)
{
	using namespace LexViewportInteractionTestLocal;
	FScopedTestWorld TestWorld;

	TStrongObjectPtr<ULexWidget> ArrangingRoot(MakeWidget(TestWorld.World, TEXT("ArrangingRoot"), 800.0f, 600.0f));
	ArrangingRoot->CreateNewLayoutContainer<ULexLayoutContainerVerticalBox>();
	TStrongObjectPtr<ULexWidget> Arranged(MakeWidget(ArrangingRoot.Get(), TEXT("Arranged"), 100.0f, 50.0f));
	Arranged->TrySetParent(ArrangingRoot.Get(), false);

	// A canvas panel decides nothing about a plain child's position, which is why the answer cannot
	// be "the parent has a container".
	TStrongObjectPtr<ULexWidget> CanvasRoot(MakeWidget(TestWorld.World, TEXT("CanvasRoot"), 800.0f, 600.0f));
	CanvasRoot->CreateNewLayoutContainer<ULexLayoutContainerCanvasPanel>();
	TStrongObjectPtr<ULexWidget> Free(MakeWidget(CanvasRoot.Get(), TEXT("Free"), 100.0f, 50.0f));
	Free->TrySetParent(CanvasRoot.Get(), false);

	TArray<ULexWidget*> OnlyArranged = { Arranged.Get() };
	TArray<ULexWidget*> OnlyFree = { Free.Get() };
	TArray<ULexWidget*> Mixed = { Arranged.Get(), Free.Get() };
	TestFalse(TEXT("a widget a box arranges cannot be moved"), FLexUIPrefabEditorViewportClient::CanMoveSelection(OnlyArranged));
	TestTrue(TEXT("a canvas child can"), FLexUIPrefabEditorViewportClient::CanMoveSelection(OnlyFree));
	TestTrue(TEXT("and one free widget keeps the gesture alive for the selection"), FLexUIPrefabEditorViewportClient::CanMoveSelection(Mixed));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexViewportNudgeDropsArrangedAxisTest,
	"LGUI.Editor.ViewportInteraction.NudgeDropsTheArrangedAxisNotTheWidget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexViewportNudgeDropsArrangedAxisTest::RunTest(const FString& Parameters)
{
	const FVector2D Nudge(3.0, 5.0);

	FLexLayoutControlAnchorData Free;
	TestTrue(TEXT("nothing arranged, nothing dropped"),
		FLexUIPrefabEditorViewportClient::FilterMoveDelta(Nudge, Free).Equals(Nudge));

	FLexLayoutControlAnchorData HorizontalTaken;
	HorizontalTaken.bCanControlHorizontalPosition = true;
	TestTrue(TEXT("an arranged X keeps its free Y"),
		FLexUIPrefabEditorViewportClient::FilterMoveDelta(Nudge, HorizontalTaken).Equals(FVector2D(0.0, 5.0)));

	FLexLayoutControlAnchorData BothTaken;
	BothTaken.bCanControlHorizontalPosition = true;
	BothTaken.bCanControlVerticalPosition = true;
	// Zero is what the nudge reads to decide there is nothing to open a transaction for.
	TestTrue(TEXT("both arranged leaves nothing to write"),
		FLexUIPrefabEditorViewportClient::FilterMoveDelta(Nudge, BothTaken).IsZero());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexViewportMoveDragMeasuresEachParentTest,
	"LGUI.Editor.ViewportInteraction.MoveDragMeasuresEachWidgetInItsOwnParent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexViewportMoveDragMeasuresEachParentTest::RunTest(const FString& Parameters)
{
	using namespace LexViewportInteractionTestLocal;

	// One pointer travel, two parents. The parent-local distance the same world travel covers is
	// halved by a parent twice as large, so a single shared delta stretches the selection.
	const FVector Travel(0.0, 10.0, 0.0);
	TArray<FLexUIPrefabEditorViewportClient::FMoveDragTarget> Targets;
	Targets.Add(MakeTarget(FTransform::Identity, Travel));
	Targets.Add(MakeTarget(FTransform(FQuat::Identity, FVector::ZeroVector, FVector(2.0)), Travel));

	TArray<FLexUIPrefabEditorViewportClient::FMoveDragResult> Results;
	FLexUIPrefabEditorViewportClient::ResolveMoveDrag(Targets, 0.0f, Results);
	if (!TestEqual(TEXT("one result per target"), Results.Num(), 2))return false;
	TestTrue(TEXT("the unscaled parent travels the full distance"), Results[0].Position.Equals(FVector2D(10.0, 0.0)));
	TestTrue(TEXT("the doubled parent travels half of it"), Results[1].Position.Equals(FVector2D(5.0, 0.0)));
	TestFalse(TEXT("no grid, no snap to report"), Results[0].bSnappedHorizontal);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexViewportMoveDragSnapsPerSpaceTest,
	"LGUI.Editor.ViewportInteraction.MoveDragSnapsInEachWidgetsOwnSpace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexViewportMoveDragSnapsPerSpaceTest::RunTest(const FString& Parameters)
{
	using namespace LexViewportInteractionTestLocal;

	const FVector Travel(0.0, 10.0, 0.0);
	TArray<FLexUIPrefabEditorViewportClient::FMoveDragTarget> Targets;
	Targets.Add(MakeTarget(FTransform::Identity, Travel));
	Targets.Add(MakeTarget(FTransform(FQuat::Identity, FVector::ZeroVector, FVector(4.0)), Travel));

	TArray<FLexUIPrefabEditorViewportClient::FMoveDragResult> Results;
	FLexUIPrefabEditorViewportClient::ResolveMoveDrag(Targets, 8.0f, Results);
	if (!TestEqual(TEXT("one result per target"), Results.Num(), 2))return false;
	// 10 in its own space rounds to 8; 2.5 in its own space rounds to 0. Snapping once and sharing
	// the answer would put both on the same number and pull the two widgets together.
	TestTrue(TEXT("the unscaled parent lands on its own gridline"), Results[0].Position.Equals(FVector2D(8.0, 0.0)));
	TestTrue(TEXT("the scaled parent lands on its own"), Results[1].Position.Equals(FVector2D(0.0, 0.0)));

	// The guide line exists to say "the grid took this over", so it may only light up where it did.
	TestTrue(TEXT("the grid moved X, so X has something to show"), Results[0].bSnappedHorizontal);
	TestFalse(TEXT("Y never left the gridline it started on"), Results[0].bSnappedVertical);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexViewportMoveDragLeavesArrangedAxisTest,
	"LGUI.Editor.ViewportInteraction.MoveDragLeavesTheArrangedAxisWhereItWas",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexViewportMoveDragLeavesArrangedAxisTest::RunTest(const FString& Parameters)
{
	using namespace LexViewportInteractionTestLocal;

	FLexUIPrefabEditorViewportClient::FMoveDragTarget Target = MakeTarget(FTransform::Identity, FVector(0.0, 10.0, 7.0));
	Target.StartPosition = FVector2D(100.0, 200.0);
	Target.bHorizontalFree = false;
	TArray<FLexUIPrefabEditorViewportClient::FMoveDragTarget> Targets = { Target };

	TArray<FLexUIPrefabEditorViewportClient::FMoveDragResult> Results;
	FLexUIPrefabEditorViewportClient::ResolveMoveDrag(Targets, 0.0f, Results);
	if (!TestEqual(TEXT("one result"), Results.Num(), 1))return false;
	TestTrue(TEXT("the arranged axis is untouched and the free one moves"),
		Results[0].Position.Equals(FVector2D(100.0, 207.0)));
	TestFalse(TEXT("an axis nothing may write cannot claim a guide"), Results[0].bSnappedHorizontal);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexViewportClickCycleResetTest,
	"LGUI.Editor.ViewportInteraction.ClickCycleRestartsAtANewPixel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexViewportClickCycleResetTest::RunTest(const FString& Parameters)
{
	const FIntPoint Pixel(120, 80);
	TestEqual(TEXT("clicking the same pixel walks deeper into that stack"),
		FLexUIPrefabEditorViewportClient::ResolveClickCycleIndex(Pixel, Pixel, 2), 2);
	TestEqual(TEXT("clicking anywhere else starts at the top of the new one"),
		FLexUIPrefabEditorViewportClient::ResolveClickCycleIndex(Pixel, FIntPoint(121, 80), 2), (int32)INDEX_NONE);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexViewportSafeZoneRectTest,
	"LGUI.Editor.ViewportInteraction.SafeZoneInsetsTheCanvasRect",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexViewportSafeZoneRectTest::RunTest(const FString& Parameters)
{
	// 1920x1080 about a centred pivot, inset 5% per side: the rect the platform's title-safe padding
	// leaves, in the same local space the canvas outline is drawn in.
	const FBox2D Safe = FLexUIPrefabEditorViewportClient::GetSafeZoneLocalRect(
		FVector2D(1920.0, 1080.0), FVector2D(0.5, 0.5), FVector4(96.0, 54.0, 96.0, 54.0));
	if (!TestTrue(TEXT("a rect at all"), Safe.bIsValid))return false;
	TestTrue(TEXT("inset from the bottom-left"), Safe.Min.Equals(FVector2D(-864.0, -486.0)));
	TestTrue(TEXT("and from the top-right"), Safe.Max.Equals(FVector2D(864.0, 486.0)));

	const FBox2D Swallowed = FLexUIPrefabEditorViewportClient::GetSafeZoneLocalRect(
		FVector2D(100.0, 100.0), FVector2D(0.5, 0.5), FVector4(80.0, 0.0, 80.0, 0.0));
	TestFalse(TEXT("padding wider than the canvas leaves no rect, not an inside-out one"), Swallowed.bIsValid);
	return true;
}

#endif
