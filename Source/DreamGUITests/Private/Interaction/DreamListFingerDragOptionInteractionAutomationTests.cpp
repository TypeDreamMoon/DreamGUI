// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Controls/DreamListView.h"
#include "Core/Components/DreamWidget.h"
#include "Interaction/UIScrollView.h"
#include "UObject/StrongObjectPtr.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Driver/DreamDriverUntil.h"
#include "Interaction/DreamListsInteractionTestTypes.h"

/*
 * A FINGER ON ROWS THAT CAN BE PICKED UP, WHEN THE LIST SAYS FINGERS PICK THEM UP.
 *
 * By default a finger dragged along a list whose rows can be dragged scrolls the list, as UMG's does: STableRow::
 * OnDragDetected gives a touch drag to its table before the row's own drag is asked (Slate/Public/Widgets/Views/
 * STableRow.h), and the mouse is what picks rows up. UMG has no way to say otherwise; DreamGUI's list does
 * (UDreamListViewBase::FingerDrag), for a touch-first screen whose rows are dragged by finger. Set to PickUpRow, a finger's
 * drag is the row's, exactly as the mouse's is: the row it landed on is picked up and the list stays where it was.
 *
 * Rows forty units tall with nothing between them, as in the list's other touch tests.
 */
namespace DreamListFingerDragOptionTestLocal
{
	constexpr float RowHeight = 40.0f;
	const FVector2D ListSize(300.0, 400.0);

	/** A list of InItemCount items whose window shows ten rows at a time, scrollable by a finger, its rows draggable. */
	UDreamListView* MakeDraggableList(FDreamDriverRig& InRig, int32 InItemCount)
	{
		UDreamListView* List = InRig.MakeControl<UDreamListView>(TEXT("List"), nullptr, ListSize);
		if (List == nullptr)
		{
			return nullptr;
		}
		List->SetStyleSource(EDreamUIStyleSource::Inline);
		List->SetStyle(DreamListsInteraction::WithRows(List->GetStyle(), RowHeight));
		List->SetItemObjects(DreamListsInteraction::MakeItems(InItemCount));
		List->SetEnableTouchScrolling(true);
		List->SetAllowDragging(true);
		List->SetAllowDragDrop(true);
		InRig.PumpFrames(2);
		return List;
	}

	/** Pumped until the list has come to rest, for as long as its own glide takes. */
	bool WaitUntilStill(FDreamDriverRig& InRig, const UDreamListViewBase& InList)
	{
		const FWaitTimeout Timeout = FWaitTimeout::InSeconds(5.0);
		const UDreamListViewBase* List = &InList;
		return InRig.Driver()->Wait(FDreamUntil::Condition([List]()
		{
			return List->ScrollBehaviour == nullptr || !List->ScrollBehaviour->IsScrolling();
		}, Timeout), Timeout, TEXT("the list comes to rest"));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamListRowFingerDragPicksUpWhenAskedTest,
	"DreamGUI.ListRowDragDrop.WithFingersSetToPickRowsUpAFingerDragPicksUpTheRowItLandedOnAndLeavesTheListWhereItWas",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamListRowFingerDragPicksUpWhenAskedTest, "DreamGUI.ListRowDragDrop.WithFingersSetToPickRowsUpAFingerDragPicksUpTheRowItLandedOnAndLeavesTheListWhereItWas", "[Touch][Animated]")

/*
 * The list's switch is UMG's way round until it is turned: a list starts with fingers scrolling it. Set to PickUpRow, a
 * finger lands on the seventh row and drags four rows up: the drag is the row's, as the mouse's drag on the same row is
 * (STableRow::OnDragDetected with the row holding the press, OnDragDetected_Handler) -- the seventh item is picked up, once,
 * and the list does not scroll under it.
 */
bool FDreamListRowFingerDragPicksUpWhenAskedTest::RunTest(const FString& Parameters)
{
	using namespace DreamListFingerDragOptionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(DreamListsInteraction::ViewportSize());
	Rig.BindTest(this);
	UDreamListView* List = Rig.IsUsable() ? MakeDraggableList(Rig, 30) : nullptr;
	if (!TestNotNull(TEXT("The rig and a list of draggable rows came up"), List))
	{
		return false;
	}
	TestTrue(TEXT("A list starts with fingers scrolling it, as UMG's does"), List->GetFingerDrag() == EDreamListFingerDrag::ScrollList);
	List->SetFingerDrag(EDreamListFingerDrag::PickUpRow);
	TestTrue(TEXT("...and is told fingers pick its rows up"), List->GetFingerDrag() == EDreamListFingerDrag::PickUpRow);
	Rig.PumpFrames(1);
	TStrongObjectPtr<UDreamListsInteractionProbe> Probe(NewObject<UDreamListsInteractionProbe>());
	DreamListsInteraction::ListenToList(*List, *Probe);

	TestTrue(TEXT("A finger lands on the seventh row and drags four rows up the list"),
		Rig.Driver()->Find(FDreamBy::Widget(List->GetRowWidget(6)))->TouchDragBy(FVector2D(0.0, -4.0 * RowHeight)));
	TestTrue(TEXT("...and the list is at rest"), WaitUntilStill(Rig, *List));
	if (TestEqual(TEXT("The finger picked one row up"), Probe->DragDetectedItems.Num(), 1))
	{
		TestEqual(TEXT("...the seventh, which it landed on"), Probe->DragDetectedItems[0], 6);
	}
	TestEqual(TEXT("The list did not scroll under the finger"), List->GetScrollOffset(), 0.0f, 0.5f);
	TestFalse(TEXT("...and, the finger lifted, the list is not dragging an item any more"), List->GetIsDraggingListItem());
	return true;
}

#endif
