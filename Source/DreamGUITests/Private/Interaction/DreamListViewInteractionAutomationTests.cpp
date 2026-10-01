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
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamListsListViewClickSelectsTest, "DreamGUI.ListView.ClickingTheThirdRowSelectsItWithOneSelectionChangeAndOneClick", "[Pointer][Animated]")

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
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamListsListViewSingleModeMovesSelectionTest, "DreamGUI.ListView.ClickingAnotherRowInSingleModeMovesTheSelectionInOneChange", "[Pointer][Animated]")

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
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamListsListViewMultiModeAccumulatesTest, "DreamGUI.ListView.ClickingTwoRowsInMultiModeKeepsBothSelected", "[Pointer][Animated]")

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
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamListsListViewWheelScrollsRowsTest, "DreamGUI.ListView.ThreeWheelNotchesScrollThreeRowsAndFinishScrollingOnce", "[Pointer][Animated]")

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
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamListsListViewWheelAfterRevealTest, "DreamGUI.ListView.TheWheelAfterScrollingAnIndexIntoViewCarriesOnFromThere", "[Pointer][Animated]")

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
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamListsListViewDoubleClickTest, "DreamGUI.ListView.DoubleClickingARowAnnouncesOneDoubleClickAndSelectsItOnce", "[Pointer][Animated]")

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
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamListsListViewHoverTest, "DreamGUI.ListView.HoveringARowAnnouncesItAndLeavingAnnouncesItAgain", "[Pointer][Animated]")

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
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamListsListViewNavigateDownTest, "DreamGUI.ListView.NavigatingDownThreeTimesMovesTheSelectionThreeRowsDown", "[Pointer][Nav][Animated]")

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
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamListsListViewNavigateBoundTest, "DreamGUI.ListView.NavigatingDownPastTheLastRowLeavesTheSelectionOnTheLastRow", "[Pointer][Nav][Animated]")

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
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamListsListViewRemoveSelectedTest, "DreamGUI.ListView.RemovingTheSelectedItemClearsTheSelectionAndSaysSoOnce", "[Pointer][Animated]")

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
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamListsListViewRecyclingTest, "DreamGUI.ListView.ScrollingARowOutOfTheWindowReleasesItAndScrollingBackGeneratesItAgain", "[Pointer][Animated]")

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamListsListViewScrollUnderHoverTest,
	"DreamGUI.ListView.WheelingARecyclingListUnderARestingPointerEndsTheHoverOfTheItemThatMovedAway",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamListsListViewScrollUnderHoverTest, "DreamGUI.ListView.WheelingARecyclingListUnderARestingPointerEndsTheHoverOfTheItemThatMovedAway", "[Pointer][Animated]")

/*
 * A recycling list used to hand slot N of its window to pool row N, so a scroll of a line re-bound
 * every row to the next item along -- and the widget the pointer had entered was suddenly showing
 * another item, with nothing told. When the pointer then left that widget, the list reported the NEW
 * item un-hovered, and the item that had really been hovered was never un-hovered at all. Rows now
 * keep their items while those stay in the window, so the hovered widget scrolls away with its item.
 *
 * Checked here: hover the third row, wheel three rows under the resting pointer, and the hover changes
 * read "third item on, third item off, sixth item on" -- the sixth being the row now under the pointer.
 */
bool FDreamListsListViewScrollUnderHoverTest::RunTest(const FString& Parameters)
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
	List->SetVirtualizationThreshold(10);
	Rig.PumpFrames(2);
	if (!TestTrue(TEXT("The list is recycling"), List->IsVirtualizing()))
	{
		return false;
	}
	TStrongObjectPtr<UDreamListsInteractionProbe> Probe(NewObject<UDreamListsInteractionProbe>());
	DreamListsInteraction::ListenToList(*List, *Probe);

	if (!TestTrue(TEXT("Moving onto the third row completes"), RowElement(Rig, *List, 2)->Hover())
		|| !TestEqual(TEXT("and announces it hovered"), Probe->HoverItems.Num(), 1))
	{
		return false;
	}
	// The wheel goes to whatever the pointer is over, so the pointer stays exactly where it was.
	TestTrue(TEXT("Three notches under the resting pointer complete"),
		Rig.Driver()->Sequence().ScrollBy(FVector2D(-3.0, -3.0)).Perform());
	Rig.PumpFrames(2);
	TestNearlyEqual(TEXT("The list moved three rows"), List->GetScrollOffset(), 3.0f * RowHeight, 0.5f);

	if (TestEqual(TEXT("Three hover changes: on, off, and on again for the row now under the pointer"), Probe->HoverItems.Num(), 3))
	{
		TestTrue(TEXT("The third item was hovered"), Probe->HoverItems[0] == 2 && Probe->HoverStates[0]);
		TestTrue(TEXT("and it is the third item whose hover ended"), Probe->HoverItems[1] == 2 && !Probe->HoverStates[1]);
		TestTrue(TEXT("and the sixth, three rows on, is hovered now"), Probe->HoverItems[2] == 5 && Probe->HoverStates[2]);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamListsListViewRowEventsPairTest,
	"DreamGUI.ListView.RowEventsComeInPairsAndOnlyForRowsThatChangeTheirItem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamListsListViewRowEventsPairTest, "DreamGUI.ListView.RowEventsComeInPairsAndOnlyForRowsThatChangeTheirItem", "[Pointer][Animated]")

/*
 * Every refresh of a recycling list used to re-bind every pool row: OnRowGenerated for all fifteen
 * rows on every frame a fling moved the list, with OnRowReleased only for a row whose INDEX changed --
 * and that release looked its object up in the new source. So a scroll inside one row announced
 * fifteen generations and no releases, and removing an item from a list of one widget per item never
 * released the removed object: every row past it kept its index, and the last row was destroyed with
 * no release at all.
 *
 * Checked here: a scroll that changes no row's item announces nothing; three notches announce exactly
 * the row that left and the row that arrived; and a removal releases the removed object, with one
 * release for every row the list had and one generation for every row it has.
 */
bool FDreamListsListViewRowEventsPairTest::RunTest(const FString& Parameters)
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
	List->SetVirtualizationThreshold(10);
	Rig.PumpFrames(2);
	if (!TestTrue(TEXT("The list is recycling"), List->IsVirtualizing()))
	{
		return false;
	}
	TStrongObjectPtr<UDreamListsInteractionProbe> Probe(NewObject<UDreamListsInteractionProbe>());
	DreamListsInteraction::ListenToList(*List, *Probe);

	List->SetScrollOffset(10.0f);
	Rig.PumpFrames(1);
	TestEqual(TEXT("A scroll inside the first row binds no row"), Probe->GeneratedItems.Num(), 0);
	TestEqual(TEXT("and releases none"), Probe->ReleasedItems.Num(), 0);

	// Three notches: the first line on screen is now the fourth, so with two lines of overscan the
	// window starts one line further on -- the first item leaves it and the sixteenth arrives.
	TestTrue(TEXT("Three notches over the list complete"), Rig.Driver()->Find(FDreamBy::Widget(List))->ScrollBy(FVector2D(-3.0, -3.0)));
	Rig.PumpFrames(1);
	if (TestEqual(TEXT("One row was released"), Probe->ReleasedItems.Num(), 1))
	{
		TestEqual(TEXT("the first item's"), Probe->ReleasedItems[0], 0);
	}
	if (TestEqual(TEXT("and one bound"), Probe->GeneratedItems.Num(), 1))
	{
		TestEqual(TEXT("to the sixteenth item"), Probe->GeneratedItems[0], 15);
	}

	// One widget per item again, then the third item taken out of the source.
	List->SetVirtualizationThreshold(200);
	Rig.PumpFrames(1);
	if (!TestFalse(TEXT("The list holds a widget per item"), List->IsVirtualizing()))
	{
		return false;
	}
	Probe->ClearRecords();
	List->RemoveItem(Items[2]);
	Rig.PumpFrames(1);
	TestTrue(TEXT("The removed item was released, by the row that showed it"), Probe->ReleasedObjects.Contains(Items[2]));
	TestEqual(TEXT("One release for each of the thirty rows there were"), Probe->ReleasedItems.Num(), 30);
	TestEqual(TEXT("and one generation for each of the twenty-nine there are"), Probe->GeneratedItems.Num(), 29);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamListsListViewLoadMoreTest,
	"DreamGUI.ListView.ALoadMoreHandlerOnItemsScrollingIntoViewHearsEveryArrivalOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamListsListViewLoadMoreTest, "DreamGUI.ListView.ALoadMoreHandlerOnItemsScrollingIntoViewHearsEveryArrivalOnce", "[Pointer][Animated]")

/*
 * The paging pattern OnItemScrolledIntoView exists for: when the last item arrives, load more. The list
 * announced its arrivals by walking its own set of realized items, and loading more rebuilt the window
 * from inside that walk, rewriting the very set being walked -- a broken iteration, and arrivals
 * announced twice. The arrivals are a list of their own now, taken before the first is announced.
 *
 * Checked here: a recycling list of thirty that loads ten more when its thirtieth item arrives, wheeled
 * to the end: the load happened, every item that came into view was announced exactly once, and the
 * items the load brought into the window were among them.
 */
bool FDreamListsListViewLoadMoreTest::RunTest(const FString& Parameters)
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
	List->SetVirtualizationThreshold(10);
	Rig.PumpFrames(2);
	TStrongObjectPtr<UDreamListsInteractionProbe> Probe(NewObject<UDreamListsInteractionProbe>());
	Probe->LoadMoreList = List;
	Probe->LoadMoreAtItem = 29;
	for (UObject* More : DreamListsInteraction::MakeItems(10))
	{
		Probe->LoadMoreItems.Add(More);
	}
	List->OnItemScrolledIntoView.AddDynamic(Probe.Get(), &UDreamListsInteractionProbe::RecordItemScrolledIntoView);

	// Further than the thirty rows go, so the thirtieth comes into view on the way.
	TestTrue(TEXT("Wheeling down to the end completes"), Rig.Driver()->Find(FDreamBy::Widget(List))->ScrollBy(FVector2D(-25.0, -25.0)));
	Rig.PumpFrames(1);

	TestEqual(TEXT("The thirtieth item's arrival loaded ten more"), List->GetNumItems(), 40);
	TMap<int32, int32> TimesAnnounced;
	for (int32 ItemIndex : Probe->ScrolledIntoViewItems)
	{
		++TimesAnnounced.FindOrAdd(ItemIndex);
	}
	for (const TPair<int32, int32>& Announced : TimesAnnounced)
	{
		TestEqual(FString::Printf(TEXT("Item %d came into view once"), Announced.Key), Announced.Value, 1);
	}
	TestTrue(TEXT("The thirtieth item was announced"), TimesAnnounced.Contains(29));
	TestTrue(TEXT("and so was the first item the load brought into the window"), TimesAnnounced.Contains(30));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamListsListViewFocusAnchorTest,
	"DreamGUI.ListView.NavigatingFromARowThatWasRecycledWhileFocusedStepsFromTheItemFocusWasOn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamListsListViewFocusAnchorTest, "DreamGUI.ListView.NavigatingFromARowThatWasRecycledWhileFocusedStepsFromTheItemFocusWasOn", "[Pointer][Nav][Animated]")

/*
 * Focus sits on a row WIDGET, and a recycling list hands widgets new items as its window moves. Scroll
 * the focused row's item out of the window -- with a stick, a page key, code -- and the focused widget
 * comes back showing another item; a navigation press then stepped from THAT item, so Down from the
 * first item selected whatever followed the stranger. SListView steps from its selector item instead,
 * and the list now keeps the item focus was on for a row that is handed another one.
 *
 * Checked here: click the first row, take the pointer off the list, scroll five rows down by code, and
 * Down selects the second item and brings it back into view.
 */
bool FDreamListsListViewFocusAnchorTest::RunTest(const FString& Parameters)
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
	List->SetVirtualizationThreshold(10);
	Rig.PumpFrames(2);
	FDreamElementRef FirstRow = RowElement(Rig, *List, 0);
	if (!TestTrue(TEXT("The list is recycling"), List->IsVirtualizing())
		|| !TestTrue(TEXT("Clicking the first row completes"), FirstRow->Click())
		|| !TestEqual(TEXT("and selects it"), List->GetSelectedIndex(), 0))
	{
		return false;
	}
	// Off the list, so the pointer cannot hand the navigation highlight to whatever row scrolls under it.
	TestTrue(TEXT("Moving the pointer off the list completes"), FirstRow->MoveBy(FVector2D(400.0, 0.0)));
	// Five rows down: with two lines of overscan the window now starts at the fourth item, so the first
	// item's row has gone round to one of the items arriving at the far end.
	List->SetScrollOffset(5.0f * RowHeight);
	Rig.PumpFrames(1);
	if (!TestNull(TEXT("The first item has no row any more"), List->GetRowWidget(0)))
	{
		return false;
	}

	TestTrue(TEXT("One press of Down completes"), NavigateTimes(Rig, EDreamUINavigationDirection::Down, 1));
	TestEqual(TEXT("Down stepped from the first item to the second"), List->GetSelectedIndex(), 1);
	TestEqual(TEXT("and only the second is selected"), List->GetNumItemsSelected(), 1);
	TestNotNull(TEXT("which was brought back into view"), List->GetRowWidget(1));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamListsListViewSidewaysTest,
	"DreamGUI.ListView.AListTurnedSidewaysScrollsItsWholeBandAndBringsItsBarOut",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamListsListViewSidewaysTest, "DreamGUI.ListView.AListTurnedSidewaysScrollsItsWholeBandAndBringsItsBarOut", "[Pointer][Animated]")

/*
 * Turning a list sideways wrote the band's length onto a column whose horizontal axis was still
 * stretched from the vertical layout, and that stored the length LESS the viewport: the band could be
 * scrolled one viewport short of its end until something rebuilt it. The column also kept, across, the
 * position its vertical scroll had left, holding every row that far out of the window. And the
 * auto-hiding bar asked whether the column was TALLER than the viewport, which a band never is, so it
 * never came out.
 *
 * Checked here, on a list scrolled down before the turn: the range is the band less the window, the
 * bar is out after a rebuild, the first row starts at the window's top-left corner, and the wheel
 * reaches the last row.
 */
bool FDreamListsListViewSidewaysTest::RunTest(const FString& Parameters)
{
	using namespace DreamListViewInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(DreamListsInteraction::ViewportSize());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	TArray<UObject*> Items;
	UDreamListView* List = MakeList(Rig, 20, Items);
	if (!TestNotNull(TEXT("The list was made on the rig"), List)
		|| !TestNotNull(TEXT("with a scroll behaviour"), List->ScrollBehaviour.Get())
		|| !TestNotNull(TEXT("and a bar"), List->ScrollBarNode.Get()))
	{
		return false;
	}
	// Scrolled down the column first, so the turn has a position on the axis that stops scrolling.
	List->SetScrollOffset(2.0f * RowHeight);
	Rig.PumpFrames(1);
	List->SetOrientation(EDreamPanelOrientation::Horizontal);
	Rig.PumpFrames(2);

	const float BandLength = 20.0f * RowHeight;
	const float Window = List->ViewportNode->GetWidth();
	TestNearlyEqual(TEXT("The scroll range is the whole band less the window"),
		static_cast<float>(List->ScrollBehaviour->GetScrollableExtent().X), BandLength - Window, 0.5f);
	List->RebuildRows();
	Rig.PumpFrames(1);
	TestTrue(TEXT("The auto-hiding bar is out for a band that runs past its window"), List->ScrollBarNode->GetWidgetActive());

	const TOptional<FBox2D> WindowRect = Rig.Driver()->Find(FDreamBy::Widget(List->ViewportNode.Get()))->GetPixelRect();
	const TOptional<FBox2D> FirstRowRect = RowElement(Rig, *List, 0)->GetPixelRect();
	if (TestTrue(TEXT("The window and the first row both project to pixels"), WindowRect.IsSet() && FirstRowRect.IsSet()))
	{
		TestNearlyEqual(TEXT("The first row starts at the window's left edge"), FirstRowRect->Min.X, WindowRect->Min.X, 1.5);
		TestNearlyEqual(TEXT("and at its top edge"), FirstRowRect->Min.Y, WindowRect->Min.Y, 1.5);
	}

	// Further than the band goes, so it comes to rest at its end.
	TestTrue(TEXT("Wheeling along the band completes"), Rig.Driver()->Find(FDreamBy::Widget(List))->ScrollBy(FVector2D(-30.0, -30.0)));
	Rig.PumpFrames(1);
	TestNearlyEqual(TEXT("The band scrolled to its end"), List->GetScrollOffset(), BandLength - Window, 0.5f);
	const TOptional<FBox2D> LastRowRect = RowElement(Rig, *List, 19)->GetPixelRect();
	if (TestTrue(TEXT("The last row projects to pixels"), WindowRect.IsSet() && LastRowRect.IsSet()))
	{
		TestTrue(FString::Printf(TEXT("The last row ends inside the window (%.1f against %.1f)"), LastRowRect->Max.X, WindowRect->Max.X),
			LastRowRect->Max.X <= WindowRect->Max.X + 1.5 && LastRowRect->Min.X >= WindowRect->Min.X - 1.5);
	}
	return true;
}

#endif
