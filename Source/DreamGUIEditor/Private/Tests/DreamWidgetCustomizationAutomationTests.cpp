// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "DetailCustomization/DreamWidgetCustomization.h"
#include "Core/Components/DreamWidget.h"
#include "Engine/World.h"

// The widget details panel edits the whole selection but used to describe only the first widget of
// it. Pivot compensation cached one rect and replayed it onto one widget, so every other selected
// widget kept the displacement the pivot write caused; and the anchor number rows asked one shared
// question -- "is this stretched?" -- that a mixed selection has no single answer to. Both defects
// are invisible with one widget selected and both move things the user did not touch, so the two
// decisions are pinned here away from the details panel that hosts them.
namespace DreamWidgetCustomizationTestLocal
{
	struct FScopedTestWorld
	{
		UWorld* World = nullptr;
		FScopedTestWorld() { World = UWorld::CreateWorld(EWorldType::Editor, false); }
		~FScopedTestWorld() { if (World) { World->DestroyWorld(false); } }
	};

	UDreamWidget* MakeRoot(UWorld* World)
	{
		UDreamWidget* Root = NewObject<UDreamWidget>(World);
		Root->SetDisplayName(TEXT("Root"));
		Root->SetWidth(800.0f);
		Root->SetHeight(600.0f);
		return Root;
	}

	/** Anchor offsets are measured against a parent, so every fixture widget needs one. */
	UDreamWidget* MakeChild(UDreamWidget* Root, const TCHAR* Name, float Width, float Height)
	{
		UDreamWidget* Child = NewObject<UDreamWidget>(Root);
		Child->SetDisplayName(Name);
		Child->SetWidth(Width);
		Child->SetHeight(Height);
		Child->TrySetParent(Root, false);
		return Child;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetPivotCompensatesEverySelectedWidgetTest,
	"DreamGUI.Editor.WidgetDetails.PivotKeepsEverySelectedWidgetInPlace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetPivotCompensatesEverySelectedWidgetTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetCustomizationTestLocal;
	FScopedTestWorld TestWorld;

	UDreamWidget* Root = MakeRoot(TestWorld.World);
	UDreamWidget* First = MakeChild(Root, TEXT("First"), 200.0f, 100.0f);
	UDreamWidget* Second = MakeChild(Root, TEXT("Second"), 300.0f, 150.0f);
	// Anchor offsets degrade to local-space values without a parent, and every setter below turns
	// into a no-op, which would make the whole test pass on nothing.
	if (!TestTrue(TEXT("the fixture widgets are parented"), First->GetParent() == Root && Second->GetParent() == Root))return true;

	// Off-centre, so a shared cache taken from the first widget cannot happen to fit the second.
	Second->SetHorizontalAnchoredPosition(70.0f);
	Second->SetVerticalAnchoredPosition(-40.0f);

	TArray<TWeakObjectPtr<UDreamWidget>> Selection;
	Selection.Add(First);
	Selection.Add(Second);
	TArray<FMargin> Captured;
	FDreamWidgetCustomization::CaptureAnchorOffsets(Selection, Captured);
	// A cache that does not cover the selection is the defect itself, and every assertion below
	// indexes into it, so stop here rather than read off the end of it.
	if (!TestEqual(TEXT("one cache entry per selected widget"), Captured.Num(), Selection.Num()))return true;

	// What the Pivot property row does: the handle writes the new value to every selected widget.
	for (auto& Widget : Selection)
	{
		Widget->SetPivot(FVector2D(0.0f, 1.0f));
	}

	// Without this the test would pass on a no-op. Pivot moves the rect under a fixed anchored
	// position, and it has to move the *second* widget for the replay below to be worth asserting.
	TestTrue(TEXT("the pivot write displaced the second widget"), FMath::Abs(Second->GetAnchorOffsetLeft() - Captured[1].Left) > 1.0f);
	TestTrue(TEXT("vertically too"), FMath::Abs(Second->GetAnchorOffsetTop() - Captured[1].Top) > 1.0f);

	FDreamWidgetCustomization::RestoreAnchorOffsets(Selection, Captured);

	TestEqual(TEXT("first widget left"), First->GetAnchorOffsetLeft(), Captured[0].Left, 0.01f);
	TestEqual(TEXT("first widget top"), First->GetAnchorOffsetTop(), Captured[0].Top, 0.01f);
	TestEqual(TEXT("first widget right"), First->GetAnchorOffsetRight(), Captured[0].Right, 0.01f);
	TestEqual(TEXT("first widget bottom"), First->GetAnchorOffsetBottom(), Captured[0].Bottom, 0.01f);
	// The whole defect: this widget is not the primary selection and used to be left displaced.
	TestEqual(TEXT("second widget left"), Second->GetAnchorOffsetLeft(), Captured[1].Left, 0.01f);
	TestEqual(TEXT("second widget top"), Second->GetAnchorOffsetTop(), Captured[1].Top, 0.01f);
	TestEqual(TEXT("second widget right"), Second->GetAnchorOffsetRight(), Captured[1].Right, 0.01f);
	TestEqual(TEXT("second widget bottom"), Second->GetAnchorOffsetBottom(), Captured[1].Bottom, 0.01f);

	// The pivot itself is the one thing that was meant to change.
	TestEqual(TEXT("the pivot did change horizontally"), (float)Second->GetAnchorData().Pivot.X, 0.0f, 0.001f);
	TestEqual(TEXT("the pivot did change vertically"), (float)Second->GetAnchorData().Pivot.Y, 1.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetAnchorValueReadsStretchPerWidgetTest,
	"DreamGUI.Editor.WidgetDetails.AnchorValueAsksEachWidgetWhetherItIsStretched",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetAnchorValueReadsStretchPerWidgetTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetCustomizationTestLocal;
	FScopedTestWorld TestWorld;

	UDreamWidget* Root = MakeRoot(TestWorld.World);
	UDreamWidget* Stretched = MakeChild(Root, TEXT("Stretched"), 200.0f, 100.0f);
	UDreamWidget* Fixed = MakeChild(Root, TEXT("Fixed"), 200.0f, 100.0f);
	Stretched->SetAnchorMin(FVector2D(0.0f, 0.5f));
	Stretched->SetAnchorMax(FVector2D(1.0f, 0.5f));

	// A selection of two widgets that are both stretched, or neither, cannot tell the two readings
	// apart, and this test would pass without the fix. Pin the mix itself.
	TestTrue(TEXT("one selected widget is horizontally stretched"), Stretched->GetAnchorData().IsHorizontalStretched());
	TestFalse(TEXT("the other is not"), Fixed->GetAnchorData().IsHorizontalStretched());

	TArray<TWeakObjectPtr<UDreamWidget>> Selection;
	Selection.Add(Stretched);
	Selection.Add(Fixed);

	// Row 0: the same typed number is a left edge offset for the stretched widget and a horizontal
	// anchored position for the other one.
	FDreamWidgetCustomization::ApplyAnchorValueToWidgets(Selection, 25.0f, 0);
	TestEqual(TEXT("stretched widget took it as a left offset"), Stretched->GetAnchorOffsetLeft(), 25.0f, 0.01f);
	TestEqual(TEXT("fixed widget took it as an anchored position"), Fixed->GetHorizontalAnchoredPosition(), 25.0f, 0.01f);

	// Row 2: a right edge offset versus a width, which is the reading that resizes when it is wrong.
	FDreamWidgetCustomization::ApplyAnchorValueToWidgets(Selection, 60.0f, 2);
	TestEqual(TEXT("stretched widget took it as a right offset"), Stretched->GetAnchorOffsetRight(), 60.0f, 0.01f);
	TestEqual(TEXT("fixed widget took it as a width"), Fixed->GetWidth(), 60.0f, 0.01f);
	return true;
}

#endif
