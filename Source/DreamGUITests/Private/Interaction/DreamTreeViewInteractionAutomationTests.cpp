// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Controls/DreamTreeView.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamEventSystem.h"
#include "UObject/StrongObjectPtr.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Interaction/DreamListsInteractionTestTypes.h"

/*
 * A TREE HAS TWO THINGS TO CLICK ON EVERY PARENT ROW.
 *
 * The twisty opens and closes the row; the rest of the row selects it. Which of the two a click
 * reaches is decided by which widget the pointer actually lands on -- the twisty is a button of its
 * own, sitting inside the row's button -- so the only honest test of that split is a real click at
 * a real pixel. The tree's own suite toggles expansion through the API and cannot see it.
 *
 * The reference is UMG 5.8: SExpanderArrow (a button that toggles and consumes the press),
 * SObjectTableRow (the row selects), and STreeView::OnKeyDown (Right opens a collapsed parent, Left
 * closes an open one).
 *
 * A tree starts fully expanded here -- DreamTreeView.md: "Everything is expanded to begin with" --
 * where UMG's starts folded, so every test states its folds explicitly.
 */
namespace DreamTreeViewInteractionTestLocal
{
	constexpr float RowHeight = 40.0f;
	const FVector2D TreeSize(300.0, 400.0);

	/**
	 * A tree on the rig: InLabels in pre-order with InDepths beside them, and InCollapsed folded.
	 *
	 * The folds go in AFTER the source: a text source has no identity, so a new one drops every fold
	 * the tree was holding (DreamTreeView::OnSourceChanged), and folding first would fold nothing.
	 */
	UDreamTreeView* MakeTree(FDreamDriverRig& InRig, const TArray<FString>& InLabels, const TArray<int32>& InDepths,
		const TSet<int32>& InCollapsed)
	{
		UDreamTreeView* Tree = InRig.MakeControl<UDreamTreeView>(TEXT("Tree"), nullptr, TreeSize);
		if (Tree == nullptr)
		{
			return nullptr;
		}
		Tree->SetStyleSource(EDreamUIStyleSource::Inline);
		FDreamTreeViewStyle TreeStyle = Tree->GetStyle();
		TreeStyle.List = DreamListsInteraction::WithRows(TreeStyle.List, RowHeight);
		Tree->SetStyle(TreeStyle);
		TArray<FText> Labels;
		Labels.Reserve(InLabels.Num());
		for (const FString& Label : InLabels)
		{
			Labels.Add(FText::AsCultureInvariant(Label));
		}
		Tree->SetItemsWithDepths(Labels, InDepths);
		Tree->SetCollapsedItems(InCollapsed);
		InRig.PumpFrames(2);
		return Tree;
	}

	/** Fruit (Apple, Pear) and Veg (Leek), with Fruit folded: three rows showing, five items in all. */
	UDreamTreeView* MakeProduceTree(FDreamDriverRig& InRig)
	{
		return MakeTree(InRig,
			TArray<FString>{ TEXT("Fruit"), TEXT("Apple"), TEXT("Pear"), TEXT("Veg"), TEXT("Leek") },
			TArray<int32>{ 0, 1, 1, 0, 1 },
			TSet<int32>{ 0 });
	}

	/** The row standing for an item, as something to click. */
	FDreamElementRef RowElement(FDreamDriverRig& InRig, UDreamTreeView& InTree, int32 InItemIndex)
	{
		return InRig.Driver()->Find(FDreamBy::Widget(InTree.GetRowWidget(InItemIndex)));
	}

	/**
	 * The twisty on an item's row, found the way the tree itself finds it: a direct child of the row
	 * called "Twisty". Null for an item with no row.
	 */
	UDreamWidget* TwistyOf(UDreamTreeView& InTree, int32 InItemIndex)
	{
		UDreamWidget* Row = InTree.GetRowWidget(InItemIndex);
		return Row != nullptr ? Row->FindChildByDisplayName(TEXT("Twisty")) : nullptr;
	}

	/** Listen to everything the tree says, the list half and the expansion half. */
	void ListenToTree(UDreamTreeView& InTree, UDreamListsInteractionProbe& InProbe)
	{
		DreamListsInteraction::ListenToList(InTree, InProbe);
		InTree.OnItemExpansionChanged.AddDynamic(&InProbe, &UDreamListsInteractionProbe::RecordExpansionChanged);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamListsTreeViewTwistyTest,
	"DreamGUI.TreeView.ClickingATwistyExpandsItsParentAndClickingItAgainCollapsesIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamListsTreeViewTwistyTest::RunTest(const FString& Parameters)
{
	using namespace DreamTreeViewInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(DreamListsInteraction::ViewportSize());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamTreeView* Tree = MakeProduceTree(Rig);
	if (!TestNotNull(TEXT("The tree was made on the rig"), Tree)
		|| !TestFalse(TEXT("Fruit starts folded"), Tree->IsItemExpanded(0))
		|| !TestEqual(TEXT("so three rows show"), Tree->GetRowCount(), 3))
	{
		return false;
	}
	TStrongObjectPtr<UDreamListsInteractionProbe> Probe(NewObject<UDreamListsInteractionProbe>());
	ListenToTree(*Tree, *Probe);

	UDreamWidget* Twisty = TwistyOf(*Tree, 0);
	if (!TestNotNull(TEXT("Fruit's row has a twisty"), Twisty))
	{
		return false;
	}
	TestTrue(TEXT("Clicking the twisty completes"), Rig.Driver()->Find(FDreamBy::Widget(Twisty))->Click());

	// SExpanderArrow::OnArrowClicked -> ToggleExpansion -> OnExpansionChanged, once; and the arrow's
	// button consumes the press, so the row under it neither selects nor hears a click.
	TestTrue(TEXT("Fruit is open"), Tree->IsItemExpanded(0));
	TestEqual(TEXT("and its two children are rows now"), Tree->GetRowCount(), 5);
	if (TestEqual(TEXT("Opening was announced once"), Probe->ExpansionItems.Num(), 1))
	{
		TestEqual(TEXT("for Fruit"), Probe->ExpansionItems[0], 0);
		TestTrue(TEXT("as expanded"), Probe->ExpansionStates[0]);
	}
	TestEqual(TEXT("The twisty is not the row: nothing was selected"), Tree->GetSelectedIndex(), INDEX_NONE);

	// Longer than the event system's double-click time -- read rather than assumed -- so the second click
	// is a click of its own and not the second half of a pair.
	TestTrue(TEXT("Letting the double-click time pass completes"),
		Rig.Driver()->Sequence().WaitSeconds(Rig.EventSystem()->GetDoubleClickTime() + 0.1f).Perform());
	UDreamWidget* TwistyAgain = TwistyOf(*Tree, 0);
	if (!TestNotNull(TEXT("Fruit's row still has a twisty"), TwistyAgain))
	{
		return false;
	}
	TestTrue(TEXT("Clicking the twisty again completes"), Rig.Driver()->Find(FDreamBy::Widget(TwistyAgain))->Click());

	TestFalse(TEXT("Fruit is folded again"), Tree->IsItemExpanded(0));
	TestEqual(TEXT("and its children are gone from the rows"), Tree->GetRowCount(), 3);
	if (TestEqual(TEXT("Closing was announced as a second change"), Probe->ExpansionItems.Num(), 2))
	{
		TestEqual(TEXT("for Fruit"), Probe->ExpansionItems[1], 0);
		TestFalse(TEXT("as collapsed"), Probe->ExpansionStates[1]);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamListsTreeViewRowBodyTest,
	"DreamGUI.TreeView.ClickingAParentRowAwayFromItsTwistySelectsItWithoutExpandingIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamListsTreeViewRowBodyTest::RunTest(const FString& Parameters)
{
	using namespace DreamTreeViewInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(DreamListsInteraction::ViewportSize());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamTreeView* Tree = MakeProduceTree(Rig);
	if (!TestNotNull(TEXT("The tree was made on the rig"), Tree)
		|| !TestFalse(TEXT("Fruit starts folded"), Tree->IsItemExpanded(0)))
	{
		return false;
	}
	TStrongObjectPtr<UDreamListsInteractionProbe> Probe(NewObject<UDreamListsInteractionProbe>());
	ListenToTree(*Tree, *Probe);

	// The row's centre: the twisty sits at the row's left end, well away from the middle of a row
	// three hundred wide, so this lands on the label and bubbles to the row's own button.
	TestTrue(TEXT("Clicking Fruit's row completes"), RowElement(Rig, *Tree, 0)->Click());

	// SObjectTableRow: a click on a row selects it. Expansion is the arrow's alone (a single click on
	// a UMG tree row never toggles it).
	TestEqual(TEXT("Fruit is the selection"), Tree->GetSelectedIndex(), 0);
	TestFalse(TEXT("and it is still folded"), Tree->IsItemExpanded(0));
	TestEqual(TEXT("so the same three rows show"), Tree->GetRowCount(), 3);
	TestEqual(TEXT("and no expansion was announced"), Probe->ExpansionItems.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamListsTreeViewChildClickTest,
	"DreamGUI.TreeView.ClickingAChildAfterExpandingSelectsTheChildAndNotItsParent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamListsTreeViewChildClickTest::RunTest(const FString& Parameters)
{
	using namespace DreamTreeViewInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(DreamListsInteraction::ViewportSize());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	// Two folded parents. Opening the SECOND leaves the first folded above it, so the child clicked
	// below is shown at row 2 while it is item 4 in the source -- a list that confused the row it drew
	// with the item it stands for would select the wrong thing, and this is where it would show.
	UDreamTreeView* Tree = MakeTree(Rig,
		TArray<FString>{ TEXT("A"), TEXT("A1"), TEXT("A2"), TEXT("B"), TEXT("B1"), TEXT("B2") },
		TArray<int32>{ 0, 1, 1, 0, 1, 1 },
		TSet<int32>{ 0, 3 });
	if (!TestNotNull(TEXT("The tree was made on the rig"), Tree)
		|| !TestEqual(TEXT("Both parents start folded, so two rows show"), Tree->GetRowCount(), 2))
	{
		return false;
	}

	UDreamWidget* TwistyOfB = TwistyOf(*Tree, 3);
	if (!TestNotNull(TEXT("B's row has a twisty"), TwistyOfB))
	{
		return false;
	}
	if (!TestTrue(TEXT("Clicking B's twisty completes"), Rig.Driver()->Find(FDreamBy::Widget(TwistyOfB))->Click())
		|| !TestTrue(TEXT("and opens B"), Tree->IsItemExpanded(3))
		|| !TestNotNull(TEXT("so B's first child has a row"), Tree->GetRowWidget(4)))
	{
		return false;
	}

	TestTrue(TEXT("Clicking B's first child completes"), RowElement(Rig, *Tree, 4)->Click());

	// Selection is an index into the SOURCE (DreamTreeView.md), and a child is an item like any other:
	// clicking it selects it and only it.
	TestEqual(TEXT("B's first child is the selection"), Tree->GetSelectedIndex(), 4);
	TestFalse(TEXT("B itself is not selected"), Tree->IsItemSelected(3));
	TestEqual(TEXT("Exactly one item is selected"), Tree->GetNumItemsSelected(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamListsTreeViewNavigateExpandTest,
	"DreamGUI.TreeView.NavigatingRightOpensACollapsedParentAndLeftClosesIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamListsTreeViewNavigateExpandTest::RunTest(const FString& Parameters)
{
	using namespace DreamTreeViewInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(DreamListsInteraction::ViewportSize());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamTreeView* Tree = MakeProduceTree(Rig);
	if (!TestNotNull(TEXT("The tree was made on the rig"), Tree)
		|| !TestFalse(TEXT("Fruit starts folded"), Tree->IsItemExpanded(0)))
	{
		return false;
	}
	// The click makes Fruit the item the keyboard acts on -- SListView's SelectorItem, which a click
	// sets and STreeView::OnKeyDown reads.
	if (!TestTrue(TEXT("Clicking Fruit's row completes"), RowElement(Rig, *Tree, 0)->Click())
		|| !TestEqual(TEXT("and selects it"), Tree->GetSelectedIndex(), 0))
	{
		return false;
	}
	TStrongObjectPtr<UDreamListsInteractionProbe> Probe(NewObject<UDreamListsInteractionProbe>());
	ListenToTree(*Tree, *Probe);

	// STreeView::OnKeyDown: Right on a collapsed item that has children expands it; Left on an
	// expanded one collapses it. (Those are the keyboard arrows; the driver's navigation press is
	// the same direction arriving at the same list.)
	TestTrue(TEXT("Navigating right completes"), Rig.Driver()->Sequence().Navigate(EDreamUINavigationDirection::Right).Perform());
	TestTrue(TEXT("Right opened Fruit"), Tree->IsItemExpanded(0));
	TestEqual(TEXT("and showed its children"), Tree->GetRowCount(), 5);

	TestTrue(TEXT("Navigating left completes"), Rig.Driver()->Sequence().Navigate(EDreamUINavigationDirection::Left).Perform());
	TestFalse(TEXT("Left folded Fruit again"), Tree->IsItemExpanded(0));

	// Both edges, each announced once, in order.
	if (TestEqual(TEXT("Two expansion changes were announced"), Probe->ExpansionItems.Num(), 2))
	{
		TestTrue(TEXT("the first opening Fruit"), Probe->ExpansionItems[0] == 0 && Probe->ExpansionStates[0]);
		TestTrue(TEXT("the second closing it"), Probe->ExpansionItems[1] == 0 && !Probe->ExpansionStates[1]);
	}
	return true;
}

#endif
