// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamListView.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamEventSystem.h"

#include "Driver/DreamDriverRig.h"
#include "Interaction/DreamControlsConfigInteractionTestHelpers.h"
#include "Interaction/DreamListsConfigInteractionTestHelpers.h"
#include "Interaction/DreamListsInteractionTestTypes.h"

/*
 * LIST ROWS PICKED UP AND PUT DOWN WITH A LIST DISABLED, AND ON A CANVAS SCALED TO HALF AND TO DOUBLE.
 *
 * The reference is UMG's list drag and drop (SObjectTableRow::OnDragDetected / OnDrop; UListView's BP_OnItemDragDetected,
 * BP_OnItemAcceptDrop, BP_OnItemDragCancelled). A disabled list is off the hit path (FHittestGrid::GetBubblePath,
 * SlateCore/Private/Input/HittestGrid.cpp:241): the press that would start a drag never reaches its rows, and a drag let go
 * over it is heard by none of them, so nothing takes the drop and the drag is cancelled. On a scaled layer the row a drop
 * lands on is the row drawn under the pointer, and which part of it -- the seam above, the row itself, the seam below -- is
 * decided in the row's own units: STableRow::ZoneFromPointerPosition (Slate/Public/Widgets/Views/STableRow.h:751-769) takes
 * the local pointer position against a boundary of a quarter of the row's local size, clamped to 3..10 units -- ten units on a
 * 40-unit row, which is 5 pixels at half scale and 20 at double.
 *
 * A finger picks rows up only on a list told to (UDreamListViewBase::FingerDrag, PickUpRow); UMG has no such switch, and a
 * finger there scrolls the list, so the finger's half of each test asks for it.
 */
namespace DreamListRowDragDropConfigTestLocal
{
	constexpr float RowHeight = 40.0f;

	/** A list of InItems at InPosition (canvas units, Y up), InSize, whose rows can be picked up and dropped on, no edge scroll. */
	UDreamListView* MakeDraggableList(FDreamDriverRig& InRig, const TCHAR* InName, const FVector2D& InSize, const FVector2D& InPosition, const TArray<UObject*>& InItems)
	{
		UDreamListView* List = InRig.MakeControl<UDreamListView>(InName, nullptr, InSize, InPosition);
		if (List == nullptr)
		{
			return nullptr;
		}
		List->SetStyleSource(EDreamUIStyleSource::Inline);
		List->SetStyle(DreamListsInteraction::WithRows(List->GetStyle(), RowHeight));
		List->SetItemObjects(InItems);
		List->SetAllowDragging(true);
		List->SetAllowDragDrop(true);
		List->SetEnableDragEdgeScrolling(false);
		List->SetEnableTouchScrolling(true);
		return List;
	}

	/** The mouse presses item InFrom's row, crosses the drag threshold, goes to InTo and lets go there. */
	bool MouseCarry(FDreamDriverRig& InRig, UDreamListView& InList, int32 InFrom, const FVector2D& InTo)
	{
		UDreamWidget* Row = InList.GetRowWidget(InFrom);
		return Row != nullptr && InRig.Driver()->Sequence()
			.MoveTo(FDreamBy::Widget(Row))
			.Press()
			.MoveBy(FVector2D(0.0, DreamControlsConfigTest::PastDragThreshold(InRig)))
			.MoveToPixel(InTo)
			.WaitFrames(1)
			.Release()
			.Perform();
	}

	/** A finger lands on item InFrom's row, crosses the drag threshold, goes to InTo, rests a frame and lifts there. */
	bool FingerCarry(FDreamDriverRig& InRig, UDreamListView& InList, int32 InFrom, const FVector2D& InTo)
	{
		const TOptional<FBox2D> From = DreamListsConfigTest::RowRect(InList, InFrom);
		if (!From.IsSet())
		{
			return false;
		}
		const FVector2D Landed = From->GetCenter();
		return InRig.Driver()->Sequence()
			.TouchDown(0, Landed)
			.TouchMoveTo(0, Landed + FVector2D(0.0, DreamControlsConfigTest::PastDragThreshold(InRig)))
			.TouchMoveTo(0, InTo)
			.WaitFrames(1)
			.TouchUp(0)
			.Perform();
	}

	/** The pixel InFraction of the way down item InIndex's drawn row, on its centre line, or unset. */
	TOptional<FVector2D> DownTheRow(const UDreamListView& InList, int32 InIndex, double InFraction)
	{
		const TOptional<FBox2D> Rect = DreamListsConfigTest::RowRect(InList, InIndex);
		return Rect.IsSet() ? FVector2D(Rect->GetCenter().X, Rect->Min.Y + InFraction * (Rect->Max.Y - Rect->Min.Y)) : TOptional<FVector2D>();
	}

	const TCHAR* ZoneName(EDreamItemDropZone InZone)
	{
		switch (InZone)
		{
		case EDreamItemDropZone::AboveItem: return TEXT("above");
		case EDreamItemDropZone::OntoItem: return TEXT("onto");
		case EDreamItemDropZone::BelowItem: return TEXT("below");
		default: return TEXT("?");
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamListRowDragDropDisabledMouseTest,
	"DreamGUI.ListRowDragDrop.AMouseDragOnADisabledListPicksNothingUpAndARowLetGoOverADisabledListIsCancelledNotDropped",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamListRowDragDropDisabledMouseTest, "DreamGUI.ListRowDragDrop.AMouseDragOnADisabledListPicksNothingUpAndARowLetGoOverADisabledListIsCancelledNotDropped", "[Pointer][Disabled]")

/*
 * A backpack and a stash side by side, each a list of draggable rows. With the backpack disabled, a mouse drag from its second
 * row onto its fourth picks nothing up and drops nothing. With the backpack enabled and the stash disabled, the backpack's
 * second row carried onto the stash's third is picked up, heard by no row of the stash, and cancelled. With both enabled, the
 * same carry drops onto the stash's third row.
 */
bool FDreamListRowDragDropDisabledMouseTest::RunTest(const FString& Parameters)
{
	using namespace DreamListRowDragDropConfigTestLocal;
	using namespace DreamControlsConfigTest;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	const TArray<UObject*> Items = DreamListsInteraction::MakeItems(10);
	UDreamListView* Backpack = MakeDraggableList(Rig, TEXT("Backpack"), FVector2D(300.0, 400.0), FVector2D(-300.0, 0.0), TArray<UObject*>(Items.GetData(), 6));
	UDreamListView* Stash = MakeDraggableList(Rig, TEXT("Stash"), FVector2D(300.0, 400.0), FVector2D(300.0, 0.0), TArray<UObject*>(Items.GetData() + 6, 4));
	if (!TestTrue(TEXT("The backpack and the stash came up"), Backpack != nullptr && Stash != nullptr))
	{
		return false;
	}
	TStrongObjectPtr<UDreamListsInteractionProbe> BackpackProbe(NewObject<UDreamListsInteractionProbe>());
	TStrongObjectPtr<UDreamListsInteractionProbe> StashProbe(NewObject<UDreamListsInteractionProbe>());
	DreamListsInteraction::ListenToList(*Backpack, *BackpackProbe);
	DreamListsInteraction::ListenToList(*Stash, *StashProbe);
	Rig.EventSystem()->SetDoubleClickTime(0.0f);
	Backpack->SetIsEnabled(false);
	Rig.PumpFrames(2);

	const TOptional<FVector2D> BackpackFourth = DownTheRow(*Backpack, 3, 0.5);
	const TOptional<FVector2D> StashThird = DownTheRow(*Stash, 2, 0.5);
	if (!TestTrue(TEXT("The rows aimed at are drawn"), BackpackFourth.IsSet() && StashThird.IsSet()))
	{
		return false;
	}
	TestTrue(TEXT("A mouse drag from the disabled backpack's second row onto its fourth completes"), MouseCarry(Rig, *Backpack, 1, BackpackFourth.GetValue()));
	TestEqual(TEXT("Nothing was picked up"), BackpackProbe->DragDetectedItems.Num(), 0);
	TestEqual(TEXT("...nothing was dropped"), BackpackProbe->DropItems.Num(), 0);
	TestFalse(TEXT("...and the backpack is not dragging an item"), Backpack->GetIsDraggingListItem());

	Backpack->SetIsEnabled(true);
	Stash->SetIsEnabled(false);
	Rig.PumpFrames(1);
	TestTrue(TEXT("Carrying the backpack's second row onto the disabled stash's third completes"), MouseCarry(Rig, *Backpack, 1, StashThird.GetValue()));
	TestEqual(TEXT("The backpack's row was picked up"), BackpackProbe->DragDetectedItems.Num(), 1);
	TestEqual(TEXT("The disabled stash heard no drop"), StashProbe->DropItems.Num(), 0);
	TestEqual(TEXT("...so nothing took it, and the backpack heard its cancel, once"), BackpackProbe->DragCancelledItems.Num(), 1);

	Stash->SetIsEnabled(true);
	Rig.PumpFrames(1);
	TestTrue(TEXT("The same carry with both lists enabled completes"), MouseCarry(Rig, *Backpack, 1, StashThird.GetValue()));
	if (TestEqual(TEXT("Enabled again, the stash hears one drop"), StashProbe->DropItems.Num(), 1))
	{
		TestEqual(TEXT("...on its third row"), StashProbe->DropItems[0], 2);
	}
	TestSamePtr(TEXT("...carrying the backpack's second item"), StashProbe->LastDropPayload.Get(), Items[1]);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamListRowDragDropDisabledFingerTest,
	"DreamGUI.ListRowDragDrop.WithFingersSetToPickRowsUpAFingerDragOnADisabledListPicksNothingUpAndScrollsNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamListRowDragDropDisabledFingerTest, "DreamGUI.ListRowDragDrop.WithFingersSetToPickRowsUpAFingerDragOnADisabledListPicksNothingUpAndScrollsNothing", "[Touch][Disabled]")

/*
 * Thirty draggable rows, fingers set to pick them up, the list disabled. A finger landing on the seventh row and dragging four
 * rows up picks nothing up and scrolls nothing. Enabled again, the same finger drag picks the seventh row up, once.
 */
bool FDreamListRowDragDropDisabledFingerTest::RunTest(const FString& Parameters)
{
	using namespace DreamListRowDragDropConfigTestLocal;
	using namespace DreamControlsConfigTest;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize());
	Rig.BindTest(this);
	UDreamListView* List = Rig.IsUsable()
		? MakeDraggableList(Rig, TEXT("List"), FVector2D(300.0, 400.0), FVector2D::ZeroVector, DreamListsInteraction::MakeItems(30))
		: nullptr;
	if (!TestNotNull(TEXT("The rig and a list of draggable rows came up"), List))
	{
		return false;
	}
	List->SetFingerDrag(EDreamListFingerDrag::PickUpRow);
	List->SetIsEnabled(false);
	Rig.PumpFrames(2);
	TStrongObjectPtr<UDreamListsInteractionProbe> Probe(NewObject<UDreamListsInteractionProbe>());
	DreamListsInteraction::ListenToList(*List, *Probe);
	FDreamElementRef SeventhRow = Rig.Driver()->Find(FDreamBy::Widget(List->GetRowWidget(6)));

	TestTrue(TEXT("A finger dragging four rows up from the disabled list's seventh row completes"), SeventhRow->TouchDragBy(FVector2D(0.0, -4.0 * RowHeight)));
	DreamListsConfigTest::WaitUntilStill(Rig, *List);
	TestEqual(TEXT("Nothing was picked up"), Probe->DragDetectedItems.Num(), 0);
	TestFalse(TEXT("...the list is not dragging an item"), List->GetIsDraggingListItem());
	TestNearlyEqual(TEXT("...and it did not scroll"), List->GetScrollOffset(), 0.0f, 0.5f);

	List->SetIsEnabled(true);
	Rig.PumpFrames(1);
	TestTrue(TEXT("The same finger drag on the list enabled again completes"),
		Rig.Driver()->Find(FDreamBy::Widget(List->GetRowWidget(6)))->TouchDragBy(FVector2D(0.0, -4.0 * RowHeight)));
	if (TestEqual(TEXT("Enabled again, the finger picks one row up"), Probe->DragDetectedItems.Num(), 1))
	{
		TestEqual(TEXT("...the seventh"), Probe->DragDetectedItems[0], 6);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamListRowDragDropScaledTest,
	"DreamGUI.ListRowDragDrop.OnACanvasScaledToHalfAndToDoubleARowIsDroppedOnTheRowAndTheSeamDrawnUnderThePointerOrFinger",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamListRowDragDropScaledTest, "DreamGUI.ListRowDragDrop.OnACanvasScaledToHalfAndToDoubleARowIsDroppedOnTheRowAndTheSeamDrawnUnderThePointerOrFinger", "[Pointer][Touch][Scaled]")

/*
 * Eight draggable rows in a list 560 pixels tall, at each scale; a row is 20 pixels tall at half scale and 80 at double, and the
 * seams are its top and bottom ten units. The mouse carries the second row onto the fourth three times, letting go 15% of the
 * way down the fourth row as drawn, halfway, and 85%: the drops land on the fourth row above it, onto it and below it. Fingers
 * set to pick rows up, a finger carries the second row onto the fifth, 15% and then halfway down it: above it, then onto it.
 * Every drop carries the second item.
 */
bool FDreamListRowDragDropScaledTest::RunTest(const FString& Parameters)
{
	using namespace DreamListRowDragDropConfigTestLocal;
	using namespace DreamControlsConfigTest;
	struct FDrop
	{
		bool bFinger;
		int32 Onto;
		double Fraction;
		EDreamItemDropZone Zone;
	};
	const TArray<FDrop> Drops = {
		{ false, 3, 0.15, EDreamItemDropZone::AboveItem },
		{ false, 3, 0.5, EDreamItemDropZone::OntoItem },
		{ false, 3, 0.85, EDreamItemDropZone::BelowItem },
		{ true, 4, 0.15, EDreamItemDropZone::AboveItem },
		{ true, 4, 0.5, EDreamItemDropZone::OntoItem },
	};
	for (const double Scale : Scales())
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(ScaledOptions(Scale));
		Rig.BindTest(this);
		if (!CameUpAt(*this, Rig, Scale))
		{
			return false;
		}
		const TArray<UObject*> Items = DreamListsInteraction::MakeItems(8);
		UDreamListView* List = MakeDraggableList(Rig, TEXT("List"), FVector2D(300.0, 560.0 / Scale), FVector2D(-100.0, 0.0), Items);
		if (!TestNotNull(*At(Scale, TEXT("A list of draggable rows came up")), List))
		{
			return false;
		}
		Rig.EventSystem()->SetDoubleClickTime(0.0f);
		Rig.PumpFrames(2);
		TStrongObjectPtr<UDreamListsInteractionProbe> Probe(NewObject<UDreamListsInteractionProbe>());
		DreamListsInteraction::ListenToList(*List, *Probe);
		const TOptional<FBox2D> Fourth = DreamListsConfigTest::RowRect(*List, 3);
		if (!TestTrue(At(Scale, TEXT("The fourth row is drawn")), Fourth.IsSet()))
		{
			return false;
		}
		TestNearlyEqual(*At(Scale, TEXT("A row is drawn its pitch times the scale tall")), Fourth->Max.Y - Fourth->Min.Y, static_cast<double>(RowHeight) * Scale, 1.5);

		for (int32 DropIndex = 0; DropIndex < Drops.Num(); ++DropIndex)
		{
			const FDrop& Drop = Drops[DropIndex];
			if (Drop.bFinger && List->GetFingerDrag() != EDreamListFingerDrag::PickUpRow)
			{
				List->SetFingerDrag(EDreamListFingerDrag::PickUpRow);
				Rig.PumpFrames(1);
			}
			const TOptional<FVector2D> LetGo = DownTheRow(*List, Drop.Onto, Drop.Fraction);
			if (!TestTrue(At(Scale, FString::Printf(TEXT("Row %d is drawn"), Drop.Onto + 1)), LetGo.IsSet()))
			{
				return false;
			}
			const FString What = FString::Printf(TEXT("%s carrying the second row %.0f%% of the way down row %d"),
				Drop.bFinger ? TEXT("A finger") : TEXT("The mouse"), Drop.Fraction * 100.0, Drop.Onto + 1);
			TestTrue(At(Scale, What + TEXT(" completes")),
				Drop.bFinger ? FingerCarry(Rig, *List, 1, LetGo.GetValue()) : MouseCarry(Rig, *List, 1, LetGo.GetValue()));
			if (!TestEqual(*At(Scale, What + TEXT(": one more drop was heard")), Probe->DropItems.Num(), DropIndex + 1))
			{
				return false;
			}
			TestEqual(*At(Scale, What + TEXT(": the drop landed on that row")), Probe->DropItems[DropIndex], Drop.Onto);
			TestTrue(At(Scale, What + FString::Printf(TEXT(": the drop is %s it (it was %s)"), ZoneName(Drop.Zone), ZoneName(Probe->DropZones[DropIndex]))),
				Probe->DropZones[DropIndex] == Drop.Zone);
			TestSamePtr(*At(Scale, What + TEXT(": the drop carried the second item")), Probe->LastDropPayload.Get(), Items[1]);
			Rig.PumpFrames(1);
		}
		TestEqual(*At(Scale, TEXT("Every carry was a drop: nothing was cancelled")), Probe->DragCancelledItems.Num(), 0);
	}
	return true;
}

#endif
