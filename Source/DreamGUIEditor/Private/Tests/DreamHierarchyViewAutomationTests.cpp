// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Designer/DreamWidgetEditorHierarchyView.h"
#include "Designer/DreamWidgetHierarchyPickerView.h"
#include "Core/Components/DreamWidget.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Engine/World.h"
#include "Designer/DreamWidgetEditorHierarchyViewItem.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Text/STextBlock.h"
#include "Core/Components/DreamImage.h"
#include "Core/DreamUserWidget.h"

// Two of the hierarchy panels asked their questions of Slate rather than of the model, and Slate
// answers "I have not built that yet" by handing back a null TSharedPtr -- which the caller then
// dereferenced. The rename policy is the one that crashed (F2 on a row scrolled off screen), and
// the picker's collection is the one that only ever saw the first root hierarchy. Both now live in
// free functions over the model, which is what makes them reachable from here at all: everything
// left in those files needs a generated Slate row or a live prefab editor toolkit.
// COVERAGE BOUNDARY, so the next reader is not misled: these tests pin the rename policy and the
// picker's root collection as free functions. They do NOT reach the two Slate call sites the
// defects actually lived in -- SDreamWidgetEditorHierarchyView::CanRename's null-row dereference, and
// SDreamWidgetHierarchyPickerView::RefreshTree feeding BuildRoots. Reverting either call site while
// leaving the helpers alone keeps every assertion below green.
namespace DreamHierarchyViewTestLocal
{
	struct FScopedTestWorld
	{
		UWorld* World = nullptr;
		FScopedTestWorld() { World = UWorld::CreateWorld(EWorldType::Editor, false); }
		~FScopedTestWorld() { if (World) { World->DestroyWorld(false); } }
	};

	UDreamWidget* MakeWidget(UWorld* World, UObject* Outer, const TCHAR* Name)
	{
		UDreamWidget* Widget = NewObject<UDreamWidget>(Outer ? Outer : (UObject*)World);
		Widget->SetDisplayName(Name);
		Widget->SetWidth(100.0f);
		Widget->SetHeight(100.0f);
		return Widget;
	}

	/** A root with one child under it, which is the smallest tree the picker walks past its own row. */
	UDreamWidget* MakeRootWithChild(UWorld* World, const TCHAR* RootName, const TCHAR* ChildName)
	{
		UDreamWidget* Root = MakeWidget(World, nullptr, RootName);
		Root->CreateNewLayoutContainer<UDreamLayoutContainerOverlay>();
		UDreamWidget* Child = MakeWidget(World, Root, ChildName);
		Child->TrySetParent(Root, false);
		return Root;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamHierarchyRenamePolicyTest,
	"DreamGUI.Editor.HierarchyRename.PolicyRefusesNullAndLockedWidgets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamHierarchyRenamePolicyTest::RunTest(const FString& Parameters)
{
	using namespace DreamHierarchyViewTestLocal;
	FScopedTestWorld TestWorld;

	// The null case is the crash: the command's CanExecute is polled with whatever is selected, and
	// a selection applied from the viewport has no row behind it until the tree scrolls there.
	TestFalse(TEXT("nothing selected cannot be renamed"), DreamWidgetHierarchyRename::CanRename(nullptr, false));

	UDreamWidget* Widget = MakeWidget(TestWorld.World, nullptr, TEXT("Widget"));
	TestTrue(TEXT("an ordinary widget can be renamed"), DreamWidgetHierarchyRename::CanRename(Widget, false));

	// Drag and drop-accept already refused a locked row; typing over the name did not.
	TestFalse(TEXT("a widget locked in the designer cannot be renamed"), DreamWidgetHierarchyRename::CanRename(Widget, true));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamHierarchyPickerCollectsEveryRootTest,
	"DreamGUI.Editor.HierarchyPicker.EveryRootHierarchyIsCollected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamHierarchyPickerCollectsEveryRootTest::RunTest(const FString& Parameters)
{
	using namespace DreamHierarchyViewTestLocal;
	FScopedTestWorld TestWorld;

	UDreamWidget* FirstRoot = MakeRootWithChild(TestWorld.World, TEXT("FirstRoot"), TEXT("FirstChild"));
	UDreamWidget* SecondRoot = MakeRootWithChild(TestWorld.World, TEXT("SecondRoot"), TEXT("SecondChild"));

	// Without this the "kept its child" assertions below could fail for the wrong reason.
	if (!TestEqual(TEXT("fixture: the first root has its child"), FirstRoot->GetChildren().Num(), 1)
		|| !TestEqual(TEXT("fixture: the second root has its child"), SecondRoot->GetChildren().Num(), 1))
	{
		return false;
	}

	TArray<TSharedPtr<FDreamWidgetHierarchyPickerView_DataItem>> Roots;
	DreamWidgetHierarchyPicker_BuildRoots({FirstRoot, SecondRoot}, UDreamWidget::StaticClass(), Roots);

	if (!TestEqual(TEXT("both hierarchies get a row"), Roots.Num(), 2))
	{
		return false;
	}

	// The second root used to come out of here untouched: no valid objects, so the row renders
	// disabled, and no children, so it has no expander either -- a hierarchy nothing could bind to.
	for (int32 Index = 0; Index < Roots.Num(); Index++)
	{
		const FString Where = FString::Printf(TEXT("root %d (%s)"), Index, *Roots[Index]->DisplayText);
		TestTrue(*(Where + TEXT(" holds something bindable")), Roots[Index]->ValidObjectArray.Num() > 0);
		TestTrue(*(Where + TEXT(" is marked as containing a valid object")), Roots[Index]->bContainsValidObject);
		TestEqual(*(Where + TEXT(" kept its child")), Roots[Index]->Children.Num(), 1);
	}

	TestEqual(TEXT("the roots come back in order"), Roots[0]->DisplayText, FirstRoot->GetDisplayName());
	TestEqual(TEXT("and the second is the second"), Roots[1]->DisplayText, SecondRoot->GetDisplayName());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamHierarchyPickerHandlesNoRootsTest,
	"DreamGUI.Editor.HierarchyPicker.NoRootsProducesAnEmptyTree",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamHierarchyPickerHandlesNoRootsTest::RunTest(const FString& Parameters)
{
	using namespace DreamHierarchyViewTestLocal;
	FScopedTestWorld TestWorld;

	// A property picker can be opened on a world that holds no DreamUI hierarchy at all. That used to
	// index RootWidgets[0] on the very first Tick.
	TArray<TSharedPtr<FDreamWidgetHierarchyPickerView_DataItem>> Roots;
	Roots.Add(MakeShared<FDreamWidgetHierarchyPickerView_DataItem>(TEXT("Stale"), nullptr));
	DreamWidgetHierarchyPicker_BuildRoots({}, UDreamWidget::StaticClass(), Roots);
	TestEqual(TEXT("no roots, no rows"), Roots.Num(), 0);

	// A root that has been garbage collected out from under the picker is the same situation.
	UDreamWidget* Root = MakeRootWithChild(TestWorld.World, TEXT("Root"), TEXT("Child"));
	DreamWidgetHierarchyPicker_BuildRoots({nullptr, Root}, UDreamWidget::StaticClass(), Roots);
	TestEqual(TEXT("a null root is skipped, the live one is not"), Roots.Num(), 1);
	return true;
}

/*
 * A hierarchy row shows the widget's name.
 *
 * This is the one thing every other row test takes for granted, and it is exactly what an edit to the
 * row's Slate tree can silently take away: removing the sub-prefab badge from the middle of one
 * SHorizontalBox took the canvas draw-call badge, the name and the type label out with it, and every
 * headless test stayed green while the panel showed nothing but icons. Nothing else in the suite
 * builds the row, so nothing else could have noticed.
 *
 * It reads the text out of the built row rather than checking which Slate classes are present: what
 * matters is that the name reaches the screen, not which widget carries it.
 */
namespace DreamHierarchyViewTestLocal
{
	void CollectTexts(TSharedRef<SWidget> Root, TArray<FString>& Out)
	{
		if (Root->GetType() == TEXT("STextBlock"))
		{
			Out.Add(StaticCastSharedRef<STextBlock>(Root)->GetText().ToString());
		}
		FChildren* Children = Root->GetChildren();
		for (int32 Index = 0; Children != nullptr && Index < Children->Num(); ++Index)
		{
			CollectTexts(Children->GetChildAt(Index), Out);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamHierarchyRowShowsTheNameTest,
	"DreamGUI.Editor.HierarchyView.ARowShowsTheWidgetsName",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamHierarchyRowShowsTheNameTest::RunTest(const FString& Parameters)
{
	using namespace DreamHierarchyViewTestLocal;
	FScopedTestWorld TestWorld;
	UDreamWidget* Widget = MakeWidget(TestWorld.World, nullptr, TEXT("PlayButton"));
	Widget->CreateNewVisual<UDreamImage>();

	// The row needs an owner table view to construct against; it never asks it anything here.
	TArray<TWeakObjectPtr<UDreamWidget>> Items = { Widget };
	TSharedRef<SListView<TWeakObjectPtr<UDreamWidget>>> OwnerTable =
		SNew(SListView<TWeakObjectPtr<UDreamWidget>>)
		.ListItemsSource(&Items)
		.OnGenerateRow_Lambda([](TWeakObjectPtr<UDreamWidget>, const TSharedRef<STableViewBase>& Table)
		{
			return SNew(STableRow<TWeakObjectPtr<UDreamWidget>>, Table);
		});

	TSharedRef<SDreamWidgetEditorHierarchyViewItem> Row =
		SNew(SDreamWidgetEditorHierarchyViewItem, OwnerTable, TWeakObjectPtr<UDreamWidget>(Widget), nullptr, nullptr);

	TArray<FString> Texts;
	CollectTexts(Row, Texts);
	TestTrue(FString::Printf(TEXT("the row shows the widget's name, saw [%s]"), *FString::Join(Texts, TEXT(" | "))),
		Texts.Contains(TEXT("PlayButton")));

	// And what it is made of, because every row is a UDreamWidget: without the type label a picture and
	// a horizontal box are told apart by the icon alone.
	TestTrue(FString::Printf(TEXT("the row shows what the widget is, saw [%s]"), *FString::Join(Texts, TEXT(" | "))),
		Texts.ContainsByPredicate([](const FString& Text) { return Text.Contains(TEXT("Image")); }));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamHierarchyRowsNoWidgetTwiceTest,
	"DreamGUI.Editor.HierarchyRows.NoWidgetLandsOnTwoRows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamHierarchyRowsNoWidgetTwiceTest::RunTest(const FString& Parameters)
{
	using namespace DreamHierarchyViewTestLocal;
	FScopedTestWorld TestWorld;

	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"));
	Root->CreateNewLayoutContainer<UDreamLayoutContainerOverlay>();
	UDreamWidget* Panel = MakeWidget(TestWorld.World, nullptr, TEXT("Panel"));
	Panel->TrySetParent(Root, false);
	UDreamWidget* Child = MakeWidget(TestWorld.World, nullptr, TEXT("Child"));
	Child->TrySetParent(Panel, false);
	RegisterDreamWidgetHierarchy(Root);

	// The shape the editor actually died in, reproduced rather than imagined: duplicating a panel
	// with DuplicateObject copied the panel alone -- its children are outered to the tree, not to it,
	// so they are not its subobjects -- and left the copy's Children array pointing at the original's
	// child. One widget, two parents. The duplicate path no longer does this (DuplicateSubtree), but
	// a persisted Children array can arrive malformed and the panel must survive reading it.
	UDreamWidget* Copy = DuplicateObject<UDreamWidget>(Panel, TestWorld.World);
	if (!TestNotNull(TEXT("the malformed copy exists"), (UObject*)Copy))return false;
	Copy->SetDisplayName(TEXT("PanelCopy"));
	Copy->SetParentBeforeRegister(Root);
	Copy->OnRegister();
	if (!TestEqual(TEXT("and shares the original's child, which is the whole point of the fixture"),
		Copy->GetChildren().Num(), 1))return false;
	if (!TestEqual(TEXT("the very same object"), (const UDreamWidget*)Copy->GetChildren()[0], (const UDreamWidget*)Child))return false;

	// Walk it the way the tree view does: roots from the manager's list, then children per row.
	TArray<TObjectPtr<UDreamWidget>> AllWidgets = { Root, Panel, Child, Copy };
	TArray<TWeakObjectPtr<UDreamWidget>> Roots;
	DreamWidgetHierarchyRows::CollectRoots(AllWidgets, Roots);
	TestEqual(TEXT("one root"), Roots.Num(), 1);

	TArray<const UDreamWidget*> Rows;
	TFunction<void(UDreamWidget*)> Walk = [&Rows, &Walk](UDreamWidget* Widget)
	{
		Rows.Add(Widget);
		TArray<TWeakObjectPtr<UDreamWidget>> Children;
		DreamWidgetHierarchyRows::CollectChildren(Widget, Children);
		for (const TWeakObjectPtr<UDreamWidget>& ChildRow : Children)
		{
			Walk(ChildRow.Get());
		}
	};
	for (const TWeakObjectPtr<UDreamWidget>& RootRow : Roots)
	{
		Walk(RootRow.Get());
	}

	// SListView.h:1154 is a check(false), so this is not a cosmetic claim: a repeat here took the
	// editor down. The row the widget appears on may be either parent -- what may not happen is both.
	TSet<const UDreamWidget*> Seen;
	for (const UDreamWidget* Row : Rows)
	{
		bool bAlreadySeen = false;
		Seen.Add(Row, &bAlreadySeen);
		TestFalse(FString::Printf(TEXT("'%s' is on exactly one row"), *Row->GetDisplayName()), bAlreadySeen);
	}
	TestEqual(TEXT("every widget still reachable is shown"), Rows.Num(), Seen.Num());
	TestTrue(TEXT("the shared child is shown somewhere, not dropped"), Seen.Contains(Child));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamHierarchyRootsAreDedupedTest,
	"DreamGUI.Editor.HierarchyRows.ARootRegisteredTwiceIsOneRoot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamHierarchyRootsAreDedupedTest::RunTest(const FString& Parameters)
{
	using namespace DreamHierarchyViewTestLocal;
	FScopedTestWorld TestWorld;

	UDreamWidget* Root = MakeRootWithChild(TestWorld.World, TEXT("Root"), TEXT("Child"));
	RegisterDreamWidgetHierarchy(Root);

	// The manager's array is append-on-register and nothing there refuses a second append, so a
	// double registration reaches the panel as the same pointer twice.
	TArray<TObjectPtr<UDreamWidget>> AllWidgets = { Root, Root };
	TArray<TWeakObjectPtr<UDreamWidget>> Roots;
	DreamWidgetHierarchyRows::CollectRoots(AllWidgets, Roots);
	TestEqual(TEXT("the root is collected once"), Roots.Num(), 1);
	return true;
}


#endif
