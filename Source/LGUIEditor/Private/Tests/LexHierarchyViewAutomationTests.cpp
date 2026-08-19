// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "PrefabEditor/LexWidgetEditorHierarchyView.h"
#include "PrefabEditor/LexWidgetHierarchyPickerView.h"
#include "Core/Components/LexWidget.h"
#include "Core/Components/LexPanelLayouts.h"
#include "Engine/World.h"

// Two of the hierarchy panels asked their questions of Slate rather than of the model, and Slate
// answers "I have not built that yet" by handing back a null TSharedPtr -- which the caller then
// dereferenced. The rename policy is the one that crashed (F2 on a row scrolled off screen), and
// the picker's collection is the one that only ever saw the first root hierarchy. Both now live in
// free functions over the model, which is what makes them reachable from here at all: everything
// left in those files needs a generated Slate row or a live prefab editor toolkit.
// COVERAGE BOUNDARY, so the next reader is not misled: these tests pin the rename policy and the
// picker's root collection as free functions. They do NOT reach the two Slate call sites the
// defects actually lived in -- SLexWidgetEditorHierarchyView::CanRename's null-row dereference, and
// SLexWidgetHierarchyPickerView::RefreshTree feeding BuildRoots. Reverting either call site while
// leaving the helpers alone keeps every assertion below green.
namespace LexHierarchyViewTestLocal
{
	struct FScopedTestWorld
	{
		UWorld* World = nullptr;
		FScopedTestWorld() { World = UWorld::CreateWorld(EWorldType::Editor, false); }
		~FScopedTestWorld() { if (World) { World->DestroyWorld(false); } }
	};

	ULexWidget* MakeWidget(UWorld* World, UObject* Outer, const TCHAR* Name)
	{
		ULexWidget* Widget = NewObject<ULexWidget>(Outer ? Outer : (UObject*)World);
		Widget->SetDisplayName(Name);
		Widget->SetWidth(100.0f);
		Widget->SetHeight(100.0f);
		return Widget;
	}

	/** A root with one child under it, which is the smallest tree the picker walks past its own row. */
	ULexWidget* MakeRootWithChild(UWorld* World, const TCHAR* RootName, const TCHAR* ChildName)
	{
		ULexWidget* Root = MakeWidget(World, nullptr, RootName);
		Root->CreateNewLayoutContainer<ULexLayoutContainerOverlay>();
		ULexWidget* Child = MakeWidget(World, Root, ChildName);
		Child->TrySetParent(Root, false);
		return Root;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexHierarchyRenamePolicyTest,
	"LGUI.Editor.HierarchyRename.PolicyRefusesNullAndLockedWidgets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexHierarchyRenamePolicyTest::RunTest(const FString& Parameters)
{
	using namespace LexHierarchyViewTestLocal;
	FScopedTestWorld TestWorld;

	// The null case is the crash: the command's CanExecute is polled with whatever is selected, and
	// a selection applied from the viewport has no row behind it until the tree scrolls there.
	TestFalse(TEXT("nothing selected cannot be renamed"), LexWidgetHierarchyRename::CanRename(nullptr, false));

	ULexWidget* Widget = MakeWidget(TestWorld.World, nullptr, TEXT("Widget"));
	TestTrue(TEXT("an ordinary widget can be renamed"), LexWidgetHierarchyRename::CanRename(Widget, false));

	// Drag and drop-accept already refused a locked row; typing over the name did not.
	TestFalse(TEXT("a widget locked in the designer cannot be renamed"), LexWidgetHierarchyRename::CanRename(Widget, true));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexHierarchyPickerCollectsEveryRootTest,
	"LGUI.Editor.HierarchyPicker.EveryRootHierarchyIsCollected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexHierarchyPickerCollectsEveryRootTest::RunTest(const FString& Parameters)
{
	using namespace LexHierarchyViewTestLocal;
	FScopedTestWorld TestWorld;

	ULexWidget* FirstRoot = MakeRootWithChild(TestWorld.World, TEXT("FirstRoot"), TEXT("FirstChild"));
	ULexWidget* SecondRoot = MakeRootWithChild(TestWorld.World, TEXT("SecondRoot"), TEXT("SecondChild"));

	// Without this the "kept its child" assertions below could fail for the wrong reason.
	if (!TestEqual(TEXT("fixture: the first root has its child"), FirstRoot->GetChildren().Num(), 1)
		|| !TestEqual(TEXT("fixture: the second root has its child"), SecondRoot->GetChildren().Num(), 1))
	{
		return false;
	}

	TArray<TSharedPtr<FLexWidgetHierarchyPickerView_DataItem>> Roots;
	LexWidgetHierarchyPicker_BuildRoots({FirstRoot, SecondRoot}, ULexWidget::StaticClass(), Roots);

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
	FLexHierarchyPickerHandlesNoRootsTest,
	"LGUI.Editor.HierarchyPicker.NoRootsProducesAnEmptyTree",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexHierarchyPickerHandlesNoRootsTest::RunTest(const FString& Parameters)
{
	using namespace LexHierarchyViewTestLocal;
	FScopedTestWorld TestWorld;

	// A property picker can be opened on a world that holds no LexUI hierarchy at all. That used to
	// index RootWidgets[0] on the very first Tick.
	TArray<TSharedPtr<FLexWidgetHierarchyPickerView_DataItem>> Roots;
	Roots.Add(MakeShared<FLexWidgetHierarchyPickerView_DataItem>(TEXT("Stale"), nullptr));
	LexWidgetHierarchyPicker_BuildRoots({}, ULexWidget::StaticClass(), Roots);
	TestEqual(TEXT("no roots, no rows"), Roots.Num(), 0);

	// A root that has been garbage collected out from under the picker is the same situation.
	ULexWidget* Root = MakeRootWithChild(TestWorld.World, TEXT("Root"), TEXT("Child"));
	LexWidgetHierarchyPicker_BuildRoots({nullptr, Root}, ULexWidget::StaticClass(), Roots);
	TestEqual(TEXT("a null root is skipped, the live one is not"), Roots.Num(), 1);
	return true;
}

#endif
