// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamListView.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamEventSystem.h"

#include "Driver/DreamDriverRig.h"
#include "Interaction/DreamControlsConfigInteractionTestHelpers.h"
#include "Interaction/DreamControlsWorldSpaceInteractionTestHelpers.h"
#include "Interaction/DreamListsInteractionTestTypes.h"

/*
 * LIST ROWS PICKED UP AND PUT DOWN ON A PANEL IN THE LEVEL.
 *
 * The reference is UMG's list drag and drop (SObjectTableRow::OnDragDetected / OnDrop; UListView's BP_OnItemDragDetected,
 * BP_OnItemAcceptDrop). In a UWidgetComponent the drag runs in the component's window: the press, the moves and the drop are
 * the interaction's pointer where its ray lands, so the row a drop lands on is the row seen under the pointer, and which part
 * of it -- the seam above, the row itself, the seam below -- is decided in the row's own units: STableRow::
 * ZoneFromPointerPosition (Slate/Public/Widgets/Views/STableRow.h:751-769) takes the local pointer position against a
 * boundary of a quarter of the row's local size, clamped to 3..10 units -- ten units on a 40-unit row, about twenty pixels as
 * the camera sees the panel here.
 *
 * A finger picks rows up only on a list told to (UDreamListViewBase::FingerDrag, PickUpRow); UMG has no such switch, and a
 * finger there scrolls the list, so the finger's half of the test asks for it.
 */
namespace DreamListRowDragDropWorldTestLocal
{
	constexpr float RowHeight = 40.0f;

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
	FDreamListRowDragDropWorldTest,
	"DreamGUI.ListRowDragDrop.OnAWorldPanelARowIsDroppedOnTheRowAndTheSeamSeenUnderThePointerOrFinger",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamListRowDragDropWorldTest, "DreamGUI.ListRowDragDrop.OnAWorldPanelARowIsDroppedOnTheRowAndTheSeamSeenUnderThePointerOrFinger", "[Pointer][Touch][World]")

/*
 * Seven draggable rows in a list on a panel three metres ahead; a row is seen about 85 pixels tall and its seams are its top
 * and bottom ten units. The mouse carries the second row onto the fourth three times, letting go 15% of the way down the fourth
 * row as the camera sees it, halfway, and 85%: the drops land on the fourth row above it, onto it and below it. Fingers set to
 * pick rows up, a finger carries the second row onto the fifth, 15% and then halfway down it: above it, then onto it. Every
 * drop carries the second item, and nothing is cancelled.
 */
bool FDreamListRowDragDropWorldTest::RunTest(const FString& Parameters)
{
	using namespace DreamListRowDragDropWorldTestLocal;
	using namespace DreamControlsConfigTest;
	using namespace DreamControlsWorldTest;
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
	FDreamDriverRig Rig = FDreamDriverRig::Headless(DreamControlsWorldTest::ViewportSize());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	const FWorldStage Stage = SetUpPanel(*this, Rig);
	if (!Stage.IsReady())
	{
		return false;
	}
	const TArray<UObject*> Items = DreamListsInteraction::MakeItems(7);
	UDreamListView* List = Rig.MakeControl<UDreamListView>(TEXT("List"), Stage.Panel, FVector2D(300.0, 7.0 * RowHeight), FVector2D(-60.0, 0.0));
	if (!TestNotNull(TEXT("A list came up on the panel"), List))
	{
		return false;
	}
	List->SetStyleSource(EDreamUIStyleSource::Inline);
	List->SetStyle(DreamListsInteraction::WithRows(List->GetStyle(), RowHeight));
	List->SetItemObjects(Items);
	List->SetAllowDragging(true);
	List->SetAllowDragDrop(true);
	List->SetEnableDragEdgeScrolling(false);
	List->SetEnableTouchScrolling(true);
	Rig.EventSystem()->SetDoubleClickTime(0.0f);
	Rig.PumpFrames(2);
	TStrongObjectPtr<UDreamListsInteractionProbe> Probe(NewObject<UDreamListsInteractionProbe>());
	DreamListsInteraction::ListenToList(*List, *Probe);
	const TOptional<FBox2D> Fourth = SeenRect(Rig, List->GetRowWidget(3));
	if (!TestTrue(TEXT("The camera sees the fourth row"), Fourth.IsSet()))
	{
		return false;
	}
	TestNearlyEqual(TEXT("A row is seen its pitch in panel units tall"), Fourth->Max.Y - Fourth->Min.Y, static_cast<double>(RowHeight) * PixelsPerUnit(), 1.5);
	const double FirstMove = DreamControlsWorldTest::PastDragThreshold(Stage.Pointer);

	for (int32 DropIndex = 0; DropIndex < Drops.Num(); ++DropIndex)
	{
		const FDrop& Drop = Drops[DropIndex];
		if (Drop.bFinger && List->GetFingerDrag() != EDreamListFingerDrag::PickUpRow)
		{
			List->SetFingerDrag(EDreamListFingerDrag::PickUpRow);
			Rig.PumpFrames(1);
		}
		const TOptional<FBox2D> From = SeenRect(Rig, List->GetRowWidget(1));
		const TOptional<FBox2D> Onto = SeenRect(Rig, List->GetRowWidget(Drop.Onto));
		if (!TestTrue(FString::Printf(TEXT("The camera sees the second row and row %d"), Drop.Onto + 1), From.IsSet() && Onto.IsSet()))
		{
			return false;
		}
		const FVector2D Landed = From->GetCenter();
		const FVector2D LetGo(Onto->GetCenter().X, Onto->Min.Y + Drop.Fraction * (Onto->Max.Y - Onto->Min.Y));
		const FString What = FString::Printf(TEXT("%s carrying the second row %.0f%% of the way down row %d"),
			Drop.bFinger ? TEXT("A finger") : TEXT("The mouse"), Drop.Fraction * 100.0, Drop.Onto + 1);
		const bool bCarried = Drop.bFinger
			? Rig.Driver()->Sequence()
				.TouchDown(0, Landed)
				.TouchMoveTo(0, Landed + FVector2D(0.0, FirstMove))
				.TouchMoveTo(0, LetGo)
				.WaitFrames(1)
				.TouchUp(0)
				.Perform()
			: Rig.Driver()->Sequence()
				.MoveToPixel(Landed)
				.Press()
				.MoveToPixel(Landed + FVector2D(0.0, FirstMove))
				.MoveToPixel(LetGo)
				.WaitFrames(1)
				.Release()
				.Perform();
		TestTrue(What + TEXT(" completes"), bCarried);
		if (!TestEqual(What + TEXT(": one more drop was heard"), Probe->DropItems.Num(), DropIndex + 1))
		{
			return false;
		}
		TestEqual(What + TEXT(": the drop landed on that row"), Probe->DropItems[DropIndex], Drop.Onto);
		TestTrue(What + FString::Printf(TEXT(": the drop is %s it (it was %s)"), ZoneName(Drop.Zone), ZoneName(Probe->DropZones[DropIndex])),
			Probe->DropZones[DropIndex] == Drop.Zone);
		TestSamePtr(*(What + TEXT(": the drop carried the second item")), Probe->LastDropPayload.Get(), Items[1]);
		Rig.PumpFrames(1);
	}
	TestEqual(TEXT("Every carry was a drop: nothing was cancelled"), Probe->DragCancelledItems.Num(), 0);
	TestEqual(TEXT("...and every one began as a row picked up"), Probe->DragDetectedItems.Num(), Drops.Num());
	return true;
}

#endif
