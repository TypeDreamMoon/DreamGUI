// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Controls/DreamListView.h"
#include "Core/Components/DreamWidget.h"
#include "Interaction/UIListView.h"
#include "UObject/StrongObjectPtr.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Interaction/DreamListsInteractionTestTypes.h"

/*
 * A LIST, USED THE WAY A PLAYER USES ONE.
 *
 * The list's own suite (DreamListControlsAutomationTests, DreamListViewParityAutomationTests) drives
 * the control through its API and through broadcasts fired by hand on a row's button. That proves
 * what the list does once it has been told something happened. These prove the list is told at all:
 * every action below is a real pointer or navigation gesture, projected from the row's transform,
 * put through the production input module and hit-tested by the real raycaster, and every assertion
 * is on the list's public state or on what its public events announced.
 *
 * Where a claim is about behaviour, the reference is UMG 5.8 -- SObjectTableRow (the row UListView
 * builds), SListView and STableViewBase -- unless Docs/Reference/DreamListView*.md states a different
 * design, in which case the document wins and the test says so.
 *
 * Thirty items, forty units a row, a four-hundred-unit window: ten rows on screen, twenty below.
 */
namespace DreamListViewInteractionTestLocal
{
	constexpr float RowHeight = 40.0f;
	const FVector2D ListSize(300.0, 400.0);

	/** A list on the rig, rows InRowHeight apart, standing for InItemCount fresh items. */
	UDreamListView* MakeList(FDreamDriverRig& InRig, int32 InItemCount, TArray<UObject*>& OutItems)
	{
		UDreamListView* List = InRig.MakeControl<UDreamListView>(TEXT("List"), nullptr, ListSize);
		if (List == nullptr)
		{
			return nullptr;
		}
		// Inline, so a project sheet in the running editor cannot decide how tall a row is -- every
		// offset and pixel below is a multiple of the pitch this states.
		List->SetStyleSource(EDreamUIStyleSource::Inline);
		List->SetStyle(DreamListsInteraction::WithRows(List->GetStyle(), RowHeight));
		OutItems = DreamListsInteraction::MakeItems(InItemCount);
		List->SetItemObjects(OutItems);
		// Two frames: the first lays the rows out, the second settles whatever the first dirtied.
		InRig.PumpFrames(2);
		return List;
	}

	/** The row standing for an item, as something to click. An element that does not exist if it has no row. */
	FDreamElementRef RowElement(FDreamDriverRig& InRig, UDreamListViewBase& InList, int32 InItemIndex)
	{
		return InRig.Driver()->Find(FDreamBy::Widget(InList.GetRowWidget(InItemIndex)));
	}

	/** Press and release a navigation direction InTimes times, one step each. */
	bool NavigateTimes(FDreamDriverRig& InRig, EDreamUINavigationDirection InDirection, int32 InTimes)
	{
		bool bAllCompleted = true;
		for (int32 Step = 0; Step < InTimes; ++Step)
		{
			if (!InRig.Driver()->Sequence().Navigate(InDirection).Perform())
			{
				bAllCompleted = false;
			}
		}
		return bAllCompleted;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamListsListViewClickSelectsTest,
	"DreamGUI.ListView.ClickingTheThirdRowSelectsItWithOneSelectionChangeAndOneClick",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamListsListViewClickSelectsTest::RunTest(const FString& Parameters)
{
	using namespace DreamListViewInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(DreamListsInteraction::ViewportSize());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	TArray<UObject*> Items;
	UDreamListView* List = MakeList(Rig, 30, Items);
	if (!TestNotNull(TEXT("The list was made on the rig"), List))
	{
		return false;
	}
	TStrongObjectPtr<UDreamListsInteractionProbe> Probe(NewObject<UDreamListsInteractionProbe>());
	DreamListsInteraction::ListenToList(*List, *Probe);

	FDreamElementRef ThirdRow = RowElement(Rig, *List, 2);
	if (!TestTrue(TEXT("The third item has a row to click"), ThirdRow->Exists()))
	{
		return false;
	}
	TestTrue(TEXT("Clicking the third row completes"), ThirdRow->Click());

	// SObjectTableRow selects on the press and signals once on the release, then reports the click:
	// one selection change and one click for one click, whatever the order.
	TestEqual(TEXT("The third item is the selection"), List->GetSelectedIndex(), 2);
	const TArray<UObject*> Selected = List->GetSelectedItems();
	if (TestEqual(TEXT("Exactly one item is selected"), Selected.Num(), 1))
	{
		TestSamePtr(TEXT("and it is the third item's object"), Selected[0], Items[2]);
	}
	if (TestEqual(TEXT("The selection changed once"), Probe->SelectionChanges.Num(), 1))
	{
		TestEqual(TEXT("to the third item"), Probe->SelectionChanges[0], 2);
	}
	if (TestEqual(TEXT("One click was announced"), Probe->ClickedItems.Num(), 1))
	{
		TestEqual(TEXT("for the third item"), Probe->ClickedItems[0], 2);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamListsListViewSingleModeMovesSelectionTest,
	"DreamGUI.ListView.ClickingAnotherRowInSingleModeMovesTheSelectionInOneChange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamListsListViewSingleModeMovesSelectionTest::RunTest(const FString& Parameters)
{
	using namespace DreamListViewInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(DreamListsInteraction::ViewportSize());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	TArray<UObject*> Items;
	UDreamListView* List = MakeList(Rig, 30, Items);
	if (!TestNotNull(TEXT("The list was made on the rig"), List))
	{
		return false;
	}
	if (!TestTrue(TEXT("A list starts in Single mode"), List->GetSelectionMode() == EUIListSelectionMode::Single))
	{
		return false;
	}
	TStrongObjectPtr<UDreamListsInteractionProbe> Probe(NewObject<UDreamListsInteractionProbe>());
	DreamListsInteraction::ListenToList(*List, *Probe);

	if (!TestTrue(TEXT("Clicking the third row completes"), RowElement(Rig, *List, 2)->Click())
		|| !TestEqual(TEXT("and selects it"), List->GetSelectedIndex(), 2))
	{
		return false;
	}
	Probe->ClearRecords();

	TestTrue(TEXT("Clicking the fifth row completes"), RowElement(Rig, *List, 4)->Click());

	// SListView replaces a single selection in one step and signals it once with the new item --
	// UListView's HandleSelectionChanged has nothing to say about the row that was let go.
	TestEqual(TEXT("The fifth item is now the selection"), List->GetSelectedIndex(), 4);
	TestFalse(TEXT("and the third is no longer selected"), List->IsItemSelected(2));
	TestEqual(TEXT("One item is selected, not two"), List->GetNumItemsSelected(), 1);
	if (TestEqual(TEXT("Moving the selection was announced once"), Probe->SelectionChanges.Num(), 1))
	{
		TestEqual(TEXT("naming the fifth item"), Probe->SelectionChanges[0], 4);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamListsListViewMultiModeAccumulatesTest,
	"DreamGUI.ListView.ClickingTwoRowsInMultiModeKeepsBothSelected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamListsListViewMultiModeAccumulatesTest::RunTest(const FString& Parameters)
{
	using namespace DreamListViewInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(DreamListsInteraction::ViewportSize());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	TArray<UObject*> Items;
	UDreamListView* List = MakeList(Rig, 30, Items);
	if (!TestNotNull(TEXT("The list was made on the rig"), List))
	{
		return false;
	}
	// The reference page's design, not UMG's: "Multi accumulates". A plain click in UMG's Multi mode
	// replaces the selection and only Ctrl adds to it -- but this library's pointer events carry no
	// modifier state for a click to read, and DreamListViewBase.md states the accumulating rule.
	List->SetSelectionMode(EUIListSelectionMode::Multi);
	Rig.PumpFrames(1);

	TestTrue(TEXT("Clicking the third row completes"), RowElement(Rig, *List, 2)->Click());
	TestTrue(TEXT("Clicking the fifth row completes"), RowElement(Rig, *List, 4)->Click());

	TestTrue(TEXT("The third item is still selected"), List->IsItemSelected(2));
	TestTrue(TEXT("and the fifth has joined it"), List->IsItemSelected(4));
	TestEqual(TEXT("Two items are selected"), List->GetNumItemsSelected(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamListsListViewWheelScrollsRowsTest,
	"DreamGUI.ListView.ThreeWheelNotchesScrollThreeRowsAndFinishScrollingOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamListsListViewWheelScrollsRowsTest::RunTest(const FString& Parameters)
{
	using namespace DreamListViewInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(DreamListsInteraction::ViewportSize());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	TArray<UObject*> Items;
	UDreamListView* List = MakeList(Rig, 30, Items);
	if (!TestNotNull(TEXT("The list was made on the rig"), List)
		|| !TestEqual(TEXT("The list starts at the top"), List->GetScrollOffset(), 0.0f, 0.01f))
	{
		return false;
	}
	TStrongObjectPtr<UDreamListsInteractionProbe> Probe(NewObject<UDreamListsInteractionProbe>());
	DreamListsInteraction::ListenToList(*List, *Probe);

	// Three notches towards the user, delivered as one wheel event over the rows. Negative is "down
	// the list": UE's wheel delta is negative towards the user, and STableViewBase scrolls by minus it.
	// Both axes carry it because that is the shape the production input actors feed (InputScroll(
	// FVector2D(Axis, Axis))); a vertical list reads only Y.
	FDreamElementRef ListElement = Rig.Driver()->Find(FDreamBy::Widget(List));
	TestTrue(TEXT("Turning the wheel over the list completes"), ListElement->ScrollBy(FVector2D(-3.0, -3.0)));
	// A few idle frames, so a "finished" that repeated every frame would be counted as it repeated.
	Rig.PumpFrames(5);

	// One notch is one ROW here -- the reference page's rule ("A list's notch is a ROW"), where UMG
	// scrolls a notch by a pixel distance -- so three notches put the fourth row at the window's top.
	TestNearlyEqual(TEXT("The list moved three rows"), List->GetScrollOffset(), 3.0f * RowHeight, 0.5f);
	const TOptional<FBox2D> WindowRect = Rig.Driver()->Find(FDreamBy::Widget(List->ViewportNode.Get()))->GetPixelRect();
	const TOptional<FBox2D> FourthRowRect = RowElement(Rig, *List, 3)->GetPixelRect();
	if (TestTrue(TEXT("The window and the fourth row both project to pixels"), WindowRect.IsSet() && FourthRowRect.IsSet()))
	{
		TestNearlyEqual(TEXT("The fourth row is now the first one on screen"),
			FourthRowRect->Min.Y, WindowRect->Min.Y, 1.5);
	}

	// UMG: OnListViewScrolled for the move, and OnFinishedScrolling on the refresh tick where the
	// current offset reaches the target -- once, because the wheel is not animated by default.
	TestTrue(TEXT("The move was announced"), Probe->ScrolledOffsets.Num() >= 1);
	if (TestEqual(TEXT("Finishing was announced exactly once"), Probe->FinishedOffsets.Num(), 1))
	{
		TestNearlyEqual(TEXT("at the offset the list came to rest at"), Probe->FinishedOffsets[0], 3.0f * RowHeight, 0.5f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamListsListViewWheelAfterRevealTest,
	"DreamGUI.ListView.TheWheelAfterScrollingAnIndexIntoViewCarriesOnFromThere",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamListsListViewWheelAfterRevealTest::RunTest(const FString& Parameters)
{
	using namespace DreamListViewInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(DreamListsInteraction::ViewportSize());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	TArray<UObject*> Items;
	UDreamListView* List = MakeList(Rig, 30, Items);
	if (!TestNotNull(TEXT("The list was made on the rig"), List))
	{
		return false;
	}

	List->ScrollIndexIntoView(25);
	Rig.PumpFrames(1);
	const float RevealOffset = List->GetScrollOffset();
	// Where exactly the reveal parks the row is the destination setting's business, not this test's:
	// anywhere that shows the whole of row 25 will do, and the claim is about what the wheel does next.
	const float RowTop = 25.0f * RowHeight;
	if (!TestTrue(TEXT("Revealing item 25 scrolled the list so the whole row is in the window"),
		RevealOffset >= RowTop + RowHeight - ListSize.Y - 0.5f && RevealOffset <= RowTop + 0.5f))
	{
		return false;
	}

	// One notch away from the user: up the list by one row, FROM WHERE THE REVEAL LEFT IT. A wheel
	// that started from a stale position -- the top, or wherever the last gesture ended -- would jump.
	FDreamElementRef ListElement = Rig.Driver()->Find(FDreamBy::Widget(List));
	TestTrue(TEXT("Turning the wheel over the list completes"), ListElement->ScrollBy(FVector2D(1.0, 1.0)));
	TestNearlyEqual(TEXT("The wheel carried on from the revealed row, one row up"),
		List->GetScrollOffset(), RevealOffset - RowHeight, 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamListsListViewDoubleClickTest,
	"DreamGUI.ListView.DoubleClickingARowAnnouncesOneDoubleClickAndSelectsItOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamListsListViewDoubleClickTest::RunTest(const FString& Parameters)
{
	using namespace DreamListViewInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(DreamListsInteraction::ViewportSize());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	TArray<UObject*> Items;
	UDreamListView* List = MakeList(Rig, 30, Items);
	if (!TestNotNull(TEXT("The list was made on the rig"), List))
	{
		return false;
	}
	TStrongObjectPtr<UDreamListsInteractionProbe> Probe(NewObject<UDreamListsInteractionProbe>());
	DreamListsInteraction::ListenToList(*List, *Probe);

	TestTrue(TEXT("Double clicking the fifth row completes"), RowElement(Rig, *List, 4)->DoubleClick());

	// SObjectTableRow::OnMouseButtonDoubleClick reports the double click and changes nothing: the
	// selection is the first press's doing, and the second press does not select a second time.
	// How many single clicks come with it is deliberately not asserted -- OnItemClicked is documented
	// as firing for EVERY click, where UMG's second press arrives as the double click instead.
	if (TestEqual(TEXT("One double click was announced"), Probe->DoubleClickedItems.Num(), 1))
	{
		TestEqual(TEXT("for the fifth item"), Probe->DoubleClickedItems[0], 4);
	}
	TestEqual(TEXT("The fifth item is the selection"), List->GetSelectedIndex(), 4);
	TestEqual(TEXT("The selection changed once for the pair, not once per press"), Probe->SelectionChanges.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamListsListViewHoverTest,
	"DreamGUI.ListView.HoveringARowAnnouncesItAndLeavingAnnouncesItAgain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamListsListViewHoverTest::RunTest(const FString& Parameters)
{
	using namespace DreamListViewInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(DreamListsInteraction::ViewportSize());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	TArray<UObject*> Items;
	UDreamListView* List = MakeList(Rig, 30, Items);
	if (!TestNotNull(TEXT("The list was made on the rig"), List))
	{
		return false;
	}
	TStrongObjectPtr<UDreamListsInteractionProbe> Probe(NewObject<UDreamListsInteractionProbe>());
	DreamListsInteraction::ListenToList(*List, *Probe);

	FDreamElementRef ThirdRow = RowElement(Rig, *List, 2);
	TestTrue(TEXT("Moving onto the third row completes"), ThirdRow->Hover());
	TestTrue(TEXT("The third row is under the pointer"), ThirdRow->IsHovered());
	// UListView::HandleListEntryHovered: the ITEM, and true, once.
	if (!TestEqual(TEXT("Arriving announced one hover change"), Probe->HoverItems.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("for the third item"), Probe->HoverItems[0], 2);
	TestTrue(TEXT("as hovered"), Probe->HoverStates[0]);

	// Well clear of the list: it is 300 wide and centred, so 400 pixels sideways is outside it.
	TestTrue(TEXT("Moving off the list completes"), ThirdRow->MoveBy(FVector2D(400.0, 0.0)));
	TestFalse(TEXT("The third row is no longer under the pointer"), ThirdRow->IsHovered());
	// UListView::HandleListEntryUnhovered: the same item, and false.
	if (TestEqual(TEXT("Leaving announced one more hover change"), Probe->HoverItems.Num(), 2))
	{
		TestEqual(TEXT("for the same item"), Probe->HoverItems[1], 2);
		TestFalse(TEXT("as no longer hovered"), Probe->HoverStates[1]);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamListsListViewNavigateDownTest,
	"DreamGUI.ListView.NavigatingDownThreeTimesMovesTheSelectionThreeRowsDown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamListsListViewNavigateDownTest::RunTest(const FString& Parameters)
{
	using namespace DreamListViewInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(DreamListsInteraction::ViewportSize());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	TArray<UObject*> Items;
	UDreamListView* List = MakeList(Rig, 30, Items);
	if (!TestNotNull(TEXT("The list was made on the rig"), List))
	{
		return false;
	}
	if (!TestTrue(TEXT("Selecting on navigation is on by default"), List->GetSelectItemOnNavigation()))
	{
		return false;
	}

	// The click is where navigation starts from: it selects the first row and puts the pointer's
	// navigation highlight on it, the way SListView's SelectorItem follows a click.
	if (!TestTrue(TEXT("Clicking the first row completes"), RowElement(Rig, *List, 0)->Click())
		|| !TestEqual(TEXT("and selects it"), List->GetSelectedIndex(), 0))
	{
		return false;
	}

	TestTrue(TEXT("Three presses of Down complete"), NavigateTimes(Rig, EDreamUINavigationDirection::Down, 3));

	// SListView::OnNavigation steps the selector one line per press and NavigationSelect selects
	// what it lands on while bSelectItemOnNavigation is true -- which DreamListViewBase.md documents
	// as "Whether landing on a row by navigation also SELECTS it", on by default.
	TestEqual(TEXT("The selection followed navigation to the fourth row"), List->GetSelectedIndex(), 3);
	TestEqual(TEXT("and only the fourth row is selected"), List->GetNumItemsSelected(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamListsListViewNavigateBoundTest,
	"DreamGUI.ListView.NavigatingDownPastTheLastRowLeavesTheSelectionOnTheLastRow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamListsListViewNavigateBoundTest::RunTest(const FString& Parameters)
{
	using namespace DreamListViewInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(DreamListsInteraction::ViewportSize());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	// Five rows, all on screen, so the end of the list is the end of what is drawn.
	TArray<UObject*> Items;
	UDreamListView* List = MakeList(Rig, 5, Items);
	if (!TestNotNull(TEXT("The list was made on the rig"), List))
	{
		return false;
	}
	if (!TestTrue(TEXT("Clicking the third row completes"), RowElement(Rig, *List, 2)->Click())
		|| !TestEqual(TEXT("and selects it"), List->GetSelectedIndex(), 2))
	{
		return false;
	}

	// Two presses reach the last row; the next two have nowhere to go.
	TestTrue(TEXT("Four presses of Down complete"), NavigateTimes(Rig, EDreamUINavigationDirection::Down, 4));

	// SListView::OnNavigation only selects an index the source has: past the end it hands the move
	// to STableViewBase, which leaves the list, and the selection stays where the last step put it.
	TestEqual(TEXT("The selection stopped on the last row"), List->GetSelectedIndex(), 4);
	TestEqual(TEXT("and is still exactly one row"), List->GetNumItemsSelected(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamListsListViewRemoveSelectedTest,
	"DreamGUI.ListView.RemovingTheSelectedItemClearsTheSelectionAndSaysSoOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamListsListViewRemoveSelectedTest::RunTest(const FString& Parameters)
{
	using namespace DreamListViewInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(DreamListsInteraction::ViewportSize());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	TArray<UObject*> Items;
	UDreamListView* List = MakeList(Rig, 30, Items);
	if (!TestNotNull(TEXT("The list was made on the rig"), List))
	{
		return false;
	}
	if (!TestTrue(TEXT("Clicking the third row completes"), RowElement(Rig, *List, 2)->Click())
		|| !TestEqual(TEXT("and selects it"), List->GetSelectedIndex(), 2))
	{
		return false;
	}
	// Listening from here on: the only events that count are the ones the removal causes.
	TStrongObjectPtr<UDreamListsInteractionProbe> Probe(NewObject<UDreamListsInteractionProbe>());
	DreamListsInteraction::ListenToList(*List, *Probe);

	List->RemoveItem(Items[2]);
	// A frame for anything deferred: UMG notices on its next tick, in UpdateSelectionSet.
	Rig.PumpFrames(1);

	TestEqual(TEXT("The item is gone from the source"), List->GetNumItems(), 29);
	TestEqual(TEXT("Nothing is selected any more"), List->GetSelectedIndex(), INDEX_NONE);
	TestEqual(TEXT("not even an index that now means another item"), List->GetNumItemsSelected(), 0);
	// SListView::UpdateSelectionSet drops the vanished item and signals the change (ESelectInfo::
	// Direct), so a consumer holding "the selected item" hears that it no longer has one.
	if (TestEqual(TEXT("Losing the selection was announced once"), Probe->SelectionChanges.Num(), 1))
	{
		TestEqual(TEXT("as no selection"), Probe->SelectionChanges[0], INDEX_NONE);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamListsListViewRecyclingTest,
	"DreamGUI.ListView.ScrollingARowOutOfTheWindowReleasesItAndScrollingBackGeneratesItAgain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamListsListViewRecyclingTest::RunTest(const FString& Parameters)
{
	using namespace DreamListViewInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(DreamListsInteraction::ViewportSize());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	TArray<UObject*> Items;
	UDreamListView* List = MakeList(Rig, 30, Items);
	if (!TestNotNull(TEXT("The list was made on the rig"), List))
	{
		return false;
	}
	// Thirty is under the default threshold, where every item keeps a widget of its own; lowering it
	// is what turns the pool into a window that recycles, which is what UMG's ListView always is.
	List->SetVirtualizationThreshold(10);
	Rig.PumpFrames(2);
	if (!TestTrue(TEXT("The list is recycling"), List->IsVirtualizing())
		|| !TestTrue(TEXT("with fewer row widgets than items"), List->GetRealizedRowCount() < List->GetNumItems())
		|| !TestNotNull(TEXT("and the first item has a row to begin with"), List->GetRowWidget(0)))
	{
		return false;
	}
	TStrongObjectPtr<UDreamListsInteractionProbe> Probe(NewObject<UDreamListsInteractionProbe>());
	DreamListsInteraction::ListenToList(*List, *Probe);
	FDreamElementRef ListElement = Rig.Driver()->Find(FDreamBy::Widget(List));

	// Down to row 25 and past: twenty-five notches is further than the list goes, so it comes to
	// rest at the bottom with row 25 on screen.
	TestTrue(TEXT("Wheeling down to the end completes"), ListElement->ScrollBy(FVector2D(-25.0, -25.0)));
	TestNotNull(TEXT("Row 25 has a widget now"), List->GetRowWidget(25));
	TestNull(TEXT("and the first item has none"), List->GetRowWidget(0));
	// UMG's OnEntryReleased: the widget that stood for the first item was let go of it.
	TestTrue(TEXT("The first item's row was released"), Probe->ReleasedItems.Contains(0));

	Probe->ClearRecords();
	TestTrue(TEXT("Wheeling back up to the top completes"), ListElement->ScrollBy(FVector2D(25.0, 25.0)));
	TestNotNull(TEXT("The first item has a widget again"), List->GetRowWidget(0));
	// UMG's OnEntryGenerated: coming back round is a fresh bind, announced as one.
	TestTrue(TEXT("and it was announced as generated again"), Probe->GeneratedItems.Contains(0));
	return true;
}

#endif
