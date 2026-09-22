// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Controls/DreamListView.h"
#include "Core/Components/DreamWidget.h"
#include "UObject/StrongObjectPtr.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Interaction/DreamListsInteractionTestTypes.h"

/*
 * A ROW PICKED UP BY THE POINTER AND PUT DOWN SOMEWHERE.
 *
 * The list's parity suite drives its drag hand-off by calling it: HandleRowDragDetected, then
 * HandleRowDragEnded, with an operation made by the test. That pins what the list does with a drag
 * it has been handed. These pin that it is handed one: a press on a row, a move past the raycaster's
 * drag threshold, a release over another row or over nothing -- through UDreamListRowDragSource and
 * UDreamListRowDropTarget, the behaviours the list puts on its rows, and the event system's own
 * begin/drop/end ordering.
 *
 * The reference is UMG 5.8's list drag and drop (SObjectTableRow::OnDragDetected / OnDrop,
 * UListView's BP_OnItemDragDetected / BP_OnItemAcceptDrop / BP_OnItemDragCancelled). Neither UMG nor
 * this library re-orders the list on a drop: DreamListViewBase.md says so in as many words, and the
 * drop is the consumer's to act on -- so "the order changed" is never the claim here.
 *
 * Eight rows forty tall in a window four hundred tall: all of them on screen, none scrolled.
 */
namespace DreamListRowDragDropInteractionTestLocal
{
	constexpr float RowHeight = 40.0f;
	const FVector2D ListSize(300.0, 400.0);

	/** A list on the rig whose rows can be picked up AND dropped on. */
	UDreamListView* MakeDraggableList(FDreamDriverRig& InRig, int32 InItemCount, TArray<UObject*>& OutItems)
	{
		UDreamListView* List = InRig.MakeControl<UDreamListView>(TEXT("List"), nullptr, ListSize);
		if (List == nullptr)
		{
			return nullptr;
		}
		List->SetStyleSource(EDreamUIStyleSource::Inline);
		List->SetStyle(DreamListsInteraction::WithRows(List->GetStyle(), RowHeight));
		OutItems = DreamListsInteraction::MakeItems(InItemCount);
		List->SetItemObjects(OutItems);
		// Both halves: dragging puts a drag source on every row, dropping a drop target. Either alone
		// is a list that only does half of what these tests do.
		List->SetAllowDragging(true);
		List->SetAllowDragDrop(true);
		InRig.PumpFrames(2);
		return List;
	}

	/** The row standing for an item, as something to drag or drop on. */
	FDreamElementRef RowElement(FDreamDriverRig& InRig, UDreamListView& InList, int32 InItemIndex)
	{
		return InRig.Driver()->Find(FDreamBy::Widget(InList.GetRowWidget(InItemIndex)));
	}

	/** Whether the list's source still holds InItems in exactly that order. */
	bool KeepsOrder(const UDreamListView& InList, const TArray<UObject*>& InItems)
	{
		if (InList.GetNumItems() != InItems.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < InItems.Num(); ++Index)
		{
			if (InList.GetItemAt(Index) != InItems[Index])
			{
				return false;
			}
		}
		return true;
	}

	/**
	 * The band and the speed the edge-scroll tests use: a row deep, and ten units a frame at the
	 * pump's sixty frames a second -- big enough to see in a few frames, small enough that ten frames
	 * stay far from the end of a thirty-row list.
	 */
	constexpr float EdgeBand = 40.0f;
	constexpr float EdgeSpeed = 600.0f;

	/** Thirty rows, twenty more than fit, so an edge scroll has somewhere to go. */
	UDreamListView* MakeScrollingDraggableList(FDreamDriverRig& InRig, TArray<UObject*>& OutItems, bool bInEdgeScrolling)
	{
		UDreamListView* List = MakeDraggableList(InRig, 30, OutItems);
		if (List == nullptr)
		{
			return nullptr;
		}
		List->SetDragEdgeScrollBandSize(EdgeBand);
		List->SetDragEdgeScrollSpeed(EdgeSpeed);
		List->SetEnableDragEdgeScrolling(bInEdgeScrolling);
		InRig.PumpFrames(1);
		return List;
	}

	/** Where the pointer rests to hold a drag inside the viewport's bottom band, and in its middle. */
	bool FindBandAndMiddlePixels(FDreamDriverRig& InRig, UDreamListView& InList, FVector2D& OutBottomBand, FVector2D& OutMiddle)
	{
		const TOptional<FBox2D> WindowRect = InRig.Driver()->Find(FDreamBy::Widget(InList.ViewportNode.Get()))->GetPixelRect();
		if (!WindowRect.IsSet())
		{
			return false;
		}
		const FVector2D Centre = WindowRect->GetCenter();
		// A quarter of the band up from the bottom edge: well inside it, and still over a row. Pixels
		// are canvas units here (ConstantPixelSize, scale one), which is the unit the band is in.
		OutBottomBand = FVector2D(Centre.X, WindowRect->Max.Y - EdgeBand * 0.25);
		OutMiddle = Centre;
		return true;
	}

	/**
	 * Press the second row, turn the press into a drag, carry it down into the bottom band, and hold
	 * it there for InHoldFrames frames -- without letting go, which is each test's own next move.
	 */
	bool DragSecondRowIntoBottomBand(FDreamDriverRig& InRig, UDreamListView& InList, const FVector2D& InBottomBand, int32 InHoldFrames)
	{
		UDreamWidget* SecondRow = InList.GetRowWidget(1);
		if (SecondRow == nullptr)
		{
			return false;
		}
		return InRig.Driver()->Sequence()
			.MoveTo(FDreamBy::Widget(SecondRow))
			.Press()
			// Past the raycaster's drag threshold first (five units), so the press is a drag before it
			// travels anywhere.
			.MoveBy(FVector2D(0.0, 12.0))
			.MoveToPixel(InBottomBand)
			.WaitFrames(InHoldFrames)
			.Perform();
	}

	/** Let go wherever the pointer is. */
	bool ReleaseDrag(FDreamDriverRig& InRig)
	{
		return InRig.Driver()->Sequence().Release().Perform();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamListsRowDragDropOntoRowTest,
	"DreamGUI.ListRowDragDrop.DraggingTheSecondRowOntoTheFifthAnnouncesOneDropCarryingTheSecondItem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamListsRowDragDropOntoRowTest::RunTest(const FString& Parameters)
{
	using namespace DreamListRowDragDropInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(DreamListsInteraction::ViewportSize());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	TArray<UObject*> Items;
	UDreamListView* List = MakeDraggableList(Rig, 8, Items);
	if (!TestNotNull(TEXT("The list was made on the rig"), List))
	{
		return false;
	}
	TStrongObjectPtr<UDreamListsInteractionProbe> Probe(NewObject<UDreamListsInteractionProbe>());
	DreamListsInteraction::ListenToList(*List, *Probe);

	FDreamElementRef SecondRow = RowElement(Rig, *List, 1);
	FDreamElementRef FifthRow = RowElement(Rig, *List, 4);
	if (!TestTrue(TEXT("Both rows are there to drag between"), SecondRow->Exists() && FifthRow->Exists()))
	{
		return false;
	}
	// Press on the second row's centre, cross the drag threshold, and let go on the fifth row's centre.
	TestTrue(TEXT("Dragging the second row onto the fifth completes"), SecondRow->DragTo(FifthRow));

	// BP_OnItemDragDetected once, for the row that was picked up.
	if (TestEqual(TEXT("One drag was detected"), Probe->DragDetectedItems.Num(), 1))
	{
		TestEqual(TEXT("on the second row"), Probe->DragDetectedItems[0], 1);
	}
	// BP_OnItemAcceptDrop once, on the row under the pointer, in the zone the pointer was in -- the
	// middle of a row is OntoItem in UMG (ZoneFromPointerPosition keeps a quarter at each end for
	// the seams) and in this library (ResolveDropZone's default edge fraction is the same quarter).
	if (TestEqual(TEXT("One drop was accepted"), Probe->DropItems.Num(), 1))
	{
		TestEqual(TEXT("on the fifth row"), Probe->DropItems[0], 4);
		TestTrue(TEXT("onto the row itself rather than a seam beside it"), Probe->DropZones[0] == EDreamItemDropZone::OntoItem);
	}
	// The payload is the ITEM, not the row widget: a recycled row stands for another item later.
	TestSamePtr(TEXT("What was dropped is the second item"), Probe->LastDropPayload.Get(), Items[1]);
	TestEqual(TEXT("A drop that landed is not a cancel"), Probe->DragCancelledItems.Num(), 0);
	TestFalse(TEXT("The list is no longer dragging"), List->GetIsDraggingListItem());
	// The drop is announced, not performed: the source's order is the consumer's to change.
	TestTrue(TEXT("The list did not re-order itself"), KeepsOrder(*List, Items));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamListsRowDragDropOutsideTest,
	"DreamGUI.ListRowDragDrop.ReleasingADraggedRowOutsideTheListCancelsItAndKeepsTheOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamListsRowDragDropOutsideTest::RunTest(const FString& Parameters)
{
	using namespace DreamListRowDragDropInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(DreamListsInteraction::ViewportSize());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	TArray<UObject*> Items;
	UDreamListView* List = MakeDraggableList(Rig, 8, Items);
	if (!TestNotNull(TEXT("The list was made on the rig"), List))
	{
		return false;
	}
	TStrongObjectPtr<UDreamListsInteractionProbe> Probe(NewObject<UDreamListsInteractionProbe>());
	DreamListsInteraction::ListenToList(*List, *Probe);

	FDreamElementRef SecondRow = RowElement(Rig, *List, 1);
	if (!TestTrue(TEXT("The second row is there to drag"), SecondRow->Exists()))
	{
		return false;
	}
	// Four hundred pixels to the left of the row's centre: the list is 300 wide and centred, so the
	// release lands on bare viewport, where nothing is hit and nothing can take the drop.
	TestTrue(TEXT("Dragging the second row off the list completes"), SecondRow->DragBy(FVector2D(-400.0, 0.0)));

	TestEqual(TEXT("One drag was detected"), Probe->DragDetectedItems.Num(), 1);
	TestEqual(TEXT("Nothing accepted it"), Probe->DropItems.Num(), 0);
	// BP_OnItemDragCancelled: the drag ended and no target took it.
	if (TestEqual(TEXT("The drag was announced as cancelled once"), Probe->DragCancelledItems.Num(), 1))
	{
		TestEqual(TEXT("for the second row"), Probe->DragCancelledItems[0], 1);
	}
	// BP_OnListViewDraggingStateChanged: both edges, in order.
	if (TestEqual(TEXT("Dragging started and stopped"), Probe->DraggingStates.Num(), 2))
	{
		TestTrue(TEXT("first starting"), Probe->DraggingStates[0]);
		TestFalse(TEXT("then stopping"), Probe->DraggingStates[1]);
	}
	TestFalse(TEXT("The list is no longer dragging"), List->GetIsDraggingListItem());
	TestTrue(TEXT("and every item is where it was"), KeepsOrder(*List, Items));
	return true;
}

/*
 * EDGE SCROLLING -- this library's own addition; UMG's list has no such thing.
 *
 * A drag the list would accept, held within DragEdgeScrollBandSize of the viewport's far end, scrolls
 * the list that way at DragEdgeScrollSpeed and stops when the drag leaves the band or lets go. What
 * these pin is the gesture from the player's side: hold, move back, let go, and the switch.
 */

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamListsRowDragEdgeScrollTest,
	"DreamGUI.ListRowDragDrop.HoldingADraggedRowInTheBottomEdgeBandScrollsTheListDown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamListsRowDragEdgeScrollTest::RunTest(const FString& Parameters)
{
	using namespace DreamListRowDragDropInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(DreamListsInteraction::ViewportSize());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	TArray<UObject*> Items;
	UDreamListView* List = MakeScrollingDraggableList(Rig, Items, /*bInEdgeScrolling*/true);
	FVector2D BottomBand;
	FVector2D Middle;
	if (!TestNotNull(TEXT("The list was made on the rig"), List)
		|| !TestTrue(TEXT("The viewport projects to pixels"), FindBandAndMiddlePixels(Rig, *List, BottomBand, Middle))
		|| !TestEqual(TEXT("The list starts at the top"), List->GetScrollOffset(), 0.0f, 0.01f))
	{
		return false;
	}

	TestTrue(TEXT("Carrying the second row into the bottom band completes"), DragSecondRowIntoBottomBand(Rig, *List, BottomBand, 10));
	if (!TestTrue(TEXT("The row is still being dragged"), List->GetIsDraggingListItem()))
	{
		ReleaseDrag(Rig);
		return false;
	}
	const float AfterFirstHold = List->GetScrollOffset();
	TestTrue(TEXT("Holding the drag in the bottom band scrolled the list down"), AfterFirstHold > 0.0f);
	// Eleven frames had the pointer in the band -- the one it arrived on and the ten it was held --
	// at a sixtieth of the speed each. The ceiling is there to catch a step taken twice a frame: the
	// hover's Over arrives from the drag event AND from the subsystem's tick.
	const float PerFrame = EdgeSpeed / 60.0f;
	TestTrue(TEXT("at the stated speed, one step a frame"), AfterFirstHold >= 5.0f * PerFrame && AfterFirstHold <= 15.0f * PerFrame);

	Rig.PumpFrames(10);
	TestTrue(TEXT("and it keeps scrolling for as long as the drag stays there"), List->GetScrollOffset() > AfterFirstHold);

	ReleaseDrag(Rig);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamListsRowDragEdgeScrollLeavesBandTest,
	"DreamGUI.ListRowDragDrop.MovingTheDragBackToTheMiddleStopsTheEdgeScroll",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamListsRowDragEdgeScrollLeavesBandTest::RunTest(const FString& Parameters)
{
	using namespace DreamListRowDragDropInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(DreamListsInteraction::ViewportSize());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	TArray<UObject*> Items;
	UDreamListView* List = MakeScrollingDraggableList(Rig, Items, /*bInEdgeScrolling*/true);
	FVector2D BottomBand;
	FVector2D Middle;
	if (!TestNotNull(TEXT("The list was made on the rig"), List)
		|| !TestTrue(TEXT("The viewport projects to pixels"), FindBandAndMiddlePixels(Rig, *List, BottomBand, Middle)))
	{
		return false;
	}
	if (!TestTrue(TEXT("Carrying the second row into the bottom band completes"), DragSecondRowIntoBottomBand(Rig, *List, BottomBand, 10))
		|| !TestTrue(TEXT("and the list scrolled while it was there"), List->GetScrollOffset() > 0.0f))
	{
		ReleaseDrag(Rig);
		return false;
	}

	// Back to the middle of the window, still holding: out of both bands, still over the rows.
	TestTrue(TEXT("Moving the drag back to the middle completes"), Rig.Driver()->Sequence().MoveToPixel(Middle).Perform());
	const float WhenMovedBack = List->GetScrollOffset();
	Rig.PumpFrames(10);
	TestNearlyEqual(TEXT("Out of the band, the list holds still"), List->GetScrollOffset(), WhenMovedBack, 0.01f);
	TestTrue(TEXT("with the drag still in flight"), List->GetIsDraggingListItem());

	ReleaseDrag(Rig);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamListsRowDragEdgeScrollReleaseTest,
	"DreamGUI.ListRowDragDrop.LettingGoInTheEdgeBandStopsTheEdgeScroll",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamListsRowDragEdgeScrollReleaseTest::RunTest(const FString& Parameters)
{
	using namespace DreamListRowDragDropInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(DreamListsInteraction::ViewportSize());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	TArray<UObject*> Items;
	UDreamListView* List = MakeScrollingDraggableList(Rig, Items, /*bInEdgeScrolling*/true);
	FVector2D BottomBand;
	FVector2D Middle;
	if (!TestNotNull(TEXT("The list was made on the rig"), List)
		|| !TestTrue(TEXT("The viewport projects to pixels"), FindBandAndMiddlePixels(Rig, *List, BottomBand, Middle)))
	{
		return false;
	}
	if (!TestTrue(TEXT("Carrying the second row into the bottom band completes"), DragSecondRowIntoBottomBand(Rig, *List, BottomBand, 10))
		|| !TestTrue(TEXT("and the list scrolled while it was there"), List->GetScrollOffset() > 0.0f))
	{
		ReleaseDrag(Rig);
		return false;
	}

	// Let go without moving: the pointer is still in the band, and nothing is dragging any more.
	TestTrue(TEXT("Letting go completes"), ReleaseDrag(Rig));
	TestFalse(TEXT("The drag is over"), List->GetIsDraggingListItem());
	const float WhenReleased = List->GetScrollOffset();
	Rig.PumpFrames(10);
	TestNearlyEqual(TEXT("Once let go, the list holds still even with the pointer in the band"),
		List->GetScrollOffset(), WhenReleased, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamListsRowDragEdgeScrollSwitchTest,
	"DreamGUI.ListRowDragDrop.WithEdgeScrollingSwitchedOffADragHeldInTheBandDoesNotScroll",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamListsRowDragEdgeScrollSwitchTest::RunTest(const FString& Parameters)
{
	using namespace DreamListRowDragDropInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(DreamListsInteraction::ViewportSize());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	TArray<UObject*> Items;
	// Built with it on and switched off through the setter, so the claim covers the runtime road and
	// not only the default.
	UDreamListView* List = MakeScrollingDraggableList(Rig, Items, /*bInEdgeScrolling*/true);
	FVector2D BottomBand;
	FVector2D Middle;
	if (!TestNotNull(TEXT("The list was made on the rig"), List)
		|| !TestTrue(TEXT("The viewport projects to pixels"), FindBandAndMiddlePixels(Rig, *List, BottomBand, Middle)))
	{
		return false;
	}
	List->SetEnableDragEdgeScrolling(false);
	TestFalse(TEXT("The switch reads back off"), List->GetEnableDragEdgeScrolling());

	TestTrue(TEXT("Carrying the second row into the bottom band completes"), DragSecondRowIntoBottomBand(Rig, *List, BottomBand, 20));
	TestTrue(TEXT("The row is being dragged"), List->GetIsDraggingListItem());
	TestEqual(TEXT("With the switch off, the list has not moved"), List->GetScrollOffset(), 0.0f, 0.01f);

	ReleaseDrag(Rig);
	return true;
}

#endif
