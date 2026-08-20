// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "DetailCustomization/DreamWidgetCustomization.h"
#include "DetailCustomization/DreamPanelSlotCustomization.h"
#include "Core/Components/DreamWidget.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Engine/World.h"
#include "UObject/StrongObjectPtr.h"

// Three decisions the anchor half of the widget details panel makes, none of which a details panel is
// needed to ask. Clicking an anchor preset writes the widget's rect and then has to tell the panel slot
// what of that rect was authored -- get that wrong and the authored size is overwritten with layout
// output, with nothing left to restore it from. And every gate around those rows used to describe the
// first selected widget only, which on a mixed selection is a description of somebody else.
namespace DreamAnchorEditingTestLocal
{
	struct FScopedTestWorld
	{
		UWorld* World = nullptr;
		FScopedTestWorld() { World = UWorld::CreateWorld(EWorldType::Editor, false); }
		~FScopedTestWorld() { if (World) { World->DestroyWorld(false); } }
	};

	/** Roots are referenced by nothing but this pointer, so GC would take them mid-test. */
	TStrongObjectPtr<UDreamWidget> MakeRoot(UWorld* World, const TCHAR* Name, float Width, float Height)
	{
		TStrongObjectPtr<UDreamWidget> Root(NewObject<UDreamWidget>(World));
		Root->SetDisplayName(Name);
		Root->SetWidth(Width);
		Root->SetHeight(Height);
		return Root;
	}

	UDreamWidget* MakeChild(UDreamWidget* Root, const TCHAR* Name, float Width, float Height)
	{
		UDreamWidget* Child = NewObject<UDreamWidget>(Root);
		Child->SetDisplayName(Name);
		Child->SetWidth(Width);
		Child->SetHeight(Height);
		Child->TrySetParent(Root, false);
		// CalculateLayoutTree skips anything unregistered, so an unregistered tree never arranges --
		// and a test whose whole point is arranged-vs-authored divergence would see none.
		if (!Root->HasRegistered())Root->OnRegister();
		Child->OnRegister();
		return Child;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamAnchorPresetKeepsTheAuthoredSizeTest,
	"DreamGUI.Editor.AnchorEditing.AnchorPresetDoesNotPromoteLayoutOutputToAuthored",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamAnchorPresetKeepsTheAuthoredSizeTest::RunTest(const FString& Parameters)
{
	using namespace DreamAnchorEditingTestLocal;
	FScopedTestWorld TestWorld;

	// A vertical stack narrower than its child: the arranged width is forced to 200 while the authored
	// width stays 300, which is the only reason the two can be told apart below.
	TStrongObjectPtr<UDreamWidget> Root = MakeRoot(TestWorld.World, TEXT("Stack"), 200.0f, 600.0f);
	Root->CreateNewLayoutContainer<UDreamLayoutContainerStackBox>();
	UDreamWidget* Child = MakeChild(Root.Get(), TEXT("Child"), 300.0f, 80.0f);

	UDreamPanelSlot* Slot = Child->GetPanelSlot();
	if (!TestNotNull(TEXT("the child got a panel slot"), Slot))return true;
	const FVector2f Authored = Slot->GetAuthoredDesiredSizeFallback();
	if (!TestTrue(TEXT("the authored size is the size it was created at"), Authored.Equals(FVector2f(300.0f, 80.0f), 0.01f)))return true;

	UDreamWidget::MarkLayoutForRebuild(Root.Get());
	UDreamWidget::RebuildLayoutImmediately(Root.Get());

	// Without a divergence between arranged and authored, both spellings of the fix agree and this
	// test would pass on nothing.
	if (!TestTrue(TEXT("the stack arranged the child to a different width"),
		FMath::Abs(Child->GetWidth() - 300.0) > 1.0))return true;

	// What clicking an anchor preset does after it has written the widget's rect.
	FDreamWidgetCustomization::SyncPanelSlotAfterAnchorEdit(Child);

	// The defect: a forced re-capture here records the arranged 200 as the authored width, and because
	// the slot already has authored geometry nothing ever re-records the real one.
	TestEqual(TEXT("the authored size survives the preset"), Slot->GetAuthoredDesiredSizeFallback(), Authored);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamAnchorGateFoldsAcrossSelectionTest,
	"DreamGUI.Editor.AnchorEditing.LayoutControlIsFoldedAcrossTheWholeSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamAnchorGateFoldsAcrossSelectionTest::RunTest(const FString& Parameters)
{
	using namespace DreamAnchorEditingTestLocal;
	FScopedTestWorld TestWorld;

	// Arranged: a panel owns every axis of its children. Free: a parent with no layout container at all.
	TStrongObjectPtr<UDreamWidget> Arranger = MakeRoot(TestWorld.World, TEXT("Stack"), 200.0f, 600.0f);
	Arranger->CreateNewLayoutContainer<UDreamLayoutContainerStackBox>();
	UDreamWidget* Arranged = MakeChild(Arranger.Get(), TEXT("Arranged"), 100.0f, 50.0f);
	TStrongObjectPtr<UDreamWidget> Plain = MakeRoot(TestWorld.World, TEXT("Plain"), 200.0f, 600.0f);
	UDreamWidget* Free = MakeChild(Plain.Get(), TEXT("Free"), 100.0f, 50.0f);

	TArray<TWeakObjectPtr<UDreamWidget>> Selection;
	Selection.Add(Free);
	TestFalse(TEXT("a free widget on its own is free"),
		FDreamWidgetCustomization::FoldLayoutControlAcrossSelection(Selection).AnyControl());

	Selection.Add(Arranged);
	// The defect: the gate read the first entry, so this selection reported itself free to edit and the
	// row wrote through to the arranged widget as well.
	const FDreamLayoutControlAnchorData Mixed = FDreamWidgetCustomization::FoldLayoutControlAcrossSelection(Selection);
	TestTrue(TEXT("an arranged widget anywhere in the selection is enough"), Mixed.bCanControlHorizontalPosition);
	TestTrue(TEXT("vertical position too"), Mixed.bCanControlVerticalPosition);
	TestTrue(TEXT("horizontal size too"), Mixed.bCanControlHorizontalSize);
	TestTrue(TEXT("vertical size too"), Mixed.bCanControlVerticalSize);

	// Order must not decide the answer either, which the old reading of entry zero made it do.
	TArray<TWeakObjectPtr<UDreamWidget>> Reversed;
	Reversed.Add(Arranged);
	Reversed.Add(Free);
	TestTrue(TEXT("the fold does not depend on selection order"),
		FDreamWidgetCustomization::FoldLayoutControlAcrossSelection(Reversed).AnyControl());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamArrangedByBannerNamesEveryArrangerTest,
	"DreamGUI.Editor.AnchorEditing.ArrangedByBannerNamesEveryPanelInTheSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamArrangedByBannerNamesEveryArrangerTest::RunTest(const FString& Parameters)
{
	using namespace DreamAnchorEditingTestLocal;
	FScopedTestWorld TestWorld;

	TStrongObjectPtr<UDreamWidget> StackRoot = MakeRoot(TestWorld.World, TEXT("Stack"), 200.0f, 600.0f);
	StackRoot->CreateNewLayoutContainer<UDreamLayoutContainerStackBox>();
	UDreamWidget* InStack = MakeChild(StackRoot.Get(), TEXT("InStack"), 100.0f, 50.0f);
	TStrongObjectPtr<UDreamWidget> OverlayRoot = MakeRoot(TestWorld.World, TEXT("Overlay"), 200.0f, 600.0f);
	OverlayRoot->CreateNewLayoutContainer<UDreamLayoutContainerOverlay>();
	UDreamWidget* InOverlay = MakeChild(OverlayRoot.Get(), TEXT("InOverlay"), 100.0f, 50.0f);

	TArray<TWeakObjectPtr<UDreamWidget>> Selection;
	Selection.Add(InStack);
	Selection.Add(InOverlay);

	// The defect: the banner described the first widget, so the second widget's panel went unnamed and
	// the user was told to go edit a panel that is not the one holding it.
	const TArray<FString> Arrangers = FDreamWidgetCustomization::CollectArrangerNames(Selection);
	if (!TestEqual(TEXT("one entry per arranging panel"), Arrangers.Num(), 2))return true;
	TestTrue(TEXT("the stack is named"), Arrangers[0].Contains(TEXT("Stack")));
	TestTrue(TEXT("and so is the overlay"), Arrangers[1].Contains(TEXT("Overlay")));

	// A widget nobody arranges contributes nothing, which is what keeps the banner hidden.
	TStrongObjectPtr<UDreamWidget> Plain = MakeRoot(TestWorld.World, TEXT("Plain"), 200.0f, 600.0f);
	UDreamWidget* Free = MakeChild(Plain.Get(), TEXT("Free"), 100.0f, 50.0f);
	TArray<TWeakObjectPtr<UDreamWidget>> FreeOnly;
	FreeOnly.Add(Free);
	TestEqual(TEXT("nothing arranges a widget under a plain parent"),
		FDreamWidgetCustomization::CollectArrangerNames(FreeOnly).Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSlotZOrderRowFollowsTheConsumerTest,
	"DreamGUI.Editor.AnchorEditing.ZOrderRowIsShownWhereThePanelReadsIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamSlotZOrderRowFollowsTheConsumerTest::RunTest(const FString& Parameters)
{
	using namespace DreamAnchorEditingTestLocal;
	FScopedTestWorld TestWorld;

	TStrongObjectPtr<UDreamWidget> Host = MakeRoot(TestWorld.World, TEXT("Host"), 200.0f, 600.0f);

	// The defect: Overlay applies ZOrder on every pass as a sibling-index write, and had no row - so a
	// value arriving with a copied prefab silently undid hierarchy reordering with nothing to clear it.
	TestTrue(TEXT("overlay"), FDreamPanelSlotCustomization::ShouldShowZOrder(
		Host->CreateNewLayoutContainer<UDreamLayoutContainerOverlay>()));
	TestTrue(TEXT("grid panel"), FDreamPanelSlotCustomization::ShouldShowZOrder(
		Host->CreateNewLayoutContainer<UDreamLayoutContainerGridPanel>()));
	TestTrue(TEXT("canvas panel"), FDreamPanelSlotCustomization::ShouldShowZOrder(
		Host->CreateNewLayoutContainer<UDreamLayoutContainerCanvasPanel>()));
	// A stack arranges strictly by sibling order, so the row would promise something it never applies.
	TestFalse(TEXT("stack box"), FDreamPanelSlotCustomization::ShouldShowZOrder(
		Host->CreateNewLayoutContainer<UDreamLayoutContainerStackBox>()));
	TestFalse(TEXT("no parent layout at all"), FDreamPanelSlotCustomization::ShouldShowZOrder(nullptr));
	return true;
}

#endif
