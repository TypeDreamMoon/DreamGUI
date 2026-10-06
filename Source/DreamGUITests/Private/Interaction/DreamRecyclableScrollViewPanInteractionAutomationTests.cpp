// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamWidget.h"
#include "Event/DreamScreenSpaceRaycaster.h"
#include "Interaction/UIListView.h"
#include "Interaction/UIRecyclableScrollView.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Interaction/DreamDragInteractionTestTypes.h"
#include "Interaction/DreamListsInteractionTestTypes.h"

/*
 * A RECYCLING VIEW UNDER A FINGER THAT KEEPS GOING.
 *
 * UUIRecyclableScrollView keeps a handful of cells and moves them round as the content scrolls (see
 * DreamRecyclingListInteractionAutomationTests.cpp for the arrangement, built the same way here). SListView pans under a
 * finger by every move's own delta for as long as the finger is down (STableViewBase::OnTouchMoved), and the rows it
 * regenerates as items come and go take nothing from the pan: the content stays under the finger. So a single pan that goes
 * on past several rows -- each one a cell going round to the far end mid-gesture -- moves the content by every pixel the
 * finger moves, and when it is set down every row on screen has its own cell.
 */
namespace DreamRecyclableScrollViewPanTestLocal
{
	const FVector2D ViewSize(300.0, 300.0);
	constexpr float CellExtent = 100.0f;

	/** A vertical recycling list on the rig with nothing in it yet, cells a hundred tall. */
	UUIListView* MakeRecyclingList(FDreamDriverRig& InRig, UDreamWidget*& OutHost)
	{
		OutHost = InRig.MakeWidget(TEXT("ListHost"), nullptr, ViewSize);
		UDreamWidget* Content = OutHost != nullptr ? InRig.MakeWidget(TEXT("Content"), OutHost, ViewSize) : nullptr;
		UDreamWidget* Cell = Content != nullptr ? InRig.MakeWidget(TEXT("Cell"), Content, FVector2D(ViewSize.X, CellExtent)) : nullptr;
		if (Cell == nullptr)
		{
			return nullptr;
		}
		Cell->AddComponent<UUIListEntry>();
		UUIListView* List = OutHost->AddComponent<UUIListView>();
		if (List == nullptr)
		{
			return nullptr;
		}
		List->SetHorizontal(false);
		List->SetVertical(true);
		List->SetContent(Content);
		List->SetCellTemplate(Cell);
		InRig.PumpFrames(2);
		return List;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRecyclableScrollViewFingerPanTest,
	"DreamGUI.ScrollView.AFingerPanningARecyclingViewPastSeveralRowsKeepsTheContentUnderItAsTheCellsGoRound",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamRecyclableScrollViewFingerPanTest, "DreamGUI.ScrollView.AFingerPanningARecyclingViewPastSeveralRowsKeepsTheContentUnderItAsTheCellsGoRound", "[Touch][Animated]")

/*
 * A hundred rows in a window three rows tall. A finger lands, crosses the drag distance, and then goes up a row's height at
 * a time, four times, in one pan: after every row the content has moved a row further, as much after the cells have gone
 * round as before. The finger then rests long enough that nothing is left to fling, and lifts; every row on screen has a
 * cell that says it shows that row.
 */
bool FDreamRecyclableScrollViewFingerPanTest::RunTest(const FString& Parameters)
{
	using namespace DreamRecyclableScrollViewPanTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(DreamListsInteraction::ViewportSize());
	Rig.BindTest(this);
	// Begun play: the list lays its cells out in Start and scrolls from its Tick.
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable())
		|| !TestTrue(TEXT("Its UI has begun play, as a game's has"), DreamDragInteraction::BeginPlayForUI(Rig.GetWorld())))
	{
		return false;
	}
	UDreamWidget* Host = nullptr;
	UUIListView* List = MakeRecyclingList(Rig, Host);
	if (!TestNotNull(TEXT("The recycling list was made"), List))
	{
		return false;
	}
	List->SetListItems(DreamListsInteraction::MakeItems(100));
	Rig.PumpFrames(1);
	const TOptional<FVector2D> Centre = Rig.Driver()->Find(FDreamBy::Widget(Host))->GetCentrePixel();
	if (!TestTrue(TEXT("The list is on the viewport"), Centre.IsSet()))
	{
		return false;
	}
	// Low in the window, so four rows of travel upward stay inside it.
	const FVector2D Landed = Centre.GetValue() + FVector2D(0.0, 120.0);
	const double Threshold = FMath::Sqrt(static_cast<double>(Rig.Raycaster()->GetScaledDragThresholdSquare()));
	FDreamDriverRef Driver = Rig.Driver();

	TestTrue(TEXT("The finger lands and starts a pan"),
		Driver->Sequence().TouchDown(0, Landed).TouchMoveTo(0, Landed - FVector2D(0.0, Threshold + 4.0)).Perform());
	float Previous = static_cast<float>(List->GetScrollOffset().Y);
	FVector2D Finger = Landed - FVector2D(0.0, Threshold + 4.0);
	for (int32 Row = 0; Row < 4; ++Row)
	{
		Finger -= FVector2D(0.0, CellExtent * 0.6);
		TestTrue(TEXT("The finger goes up a row's height"),
			Driver->Sequence().TouchMoveTo(0, Finger).TouchMoveTo(0, Finger - FVector2D(0.0, CellExtent * 0.4)).Perform());
		Finger -= FVector2D(0.0, CellExtent * 0.4);
		const float Now = static_cast<float>(List->GetScrollOffset().Y);
		TestNearlyEqual(FString::Printf(TEXT("After row %d of the pan the content moved a row further (%.1f to %.1f)"), Row + 1, Previous, Now),
			Now - Previous, CellExtent, 1.5f);
		Previous = Now;
	}
	TestTrue(FString::Printf(TEXT("The pan has gone past several rows (offset %.1f)"), Previous), Previous > 3.0f * CellExtent);

	// Still for longer than a fling looks back over, then up: the content stays where the finger left it.
	TestTrue(TEXT("The finger rests and lifts"), Driver->Sequence().WaitSeconds(0.25f).TouchUp(0).WaitFrames(2).Perform());
	const float Offset = static_cast<float>(List->GetScrollOffset().Y);
	TestNearlyEqual(TEXT("Set down, the content is where the finger left it"), Offset, Previous, 1.5f);
	const int32 FirstOnScreen = FMath::FloorToInt(Offset / CellExtent);
	const int32 LastOnScreen = FMath::FloorToInt((Offset + static_cast<float>(ViewSize.Y) - 0.5f) / CellExtent);
	for (int32 Row = FirstOnScreen; Row <= LastOnScreen; ++Row)
	{
		FUIRecyclableScrollViewCellContainer Cell;
		const UUIListEntry* Entry = List->GetCellItemByDataIndex(Row, Cell) ? Cast<UUIListEntry>(Cell.CellComponent) : nullptr;
		if (TestNotNull(FString::Printf(TEXT("Row %d, on screen, has a cell"), Row), Entry))
		{
			TestEqual(FString::Printf(TEXT("...that shows row %d"), Row), Entry->GetItemIndex(), Row);
			TestTrue(TEXT("...and is awake"), Cell.Widget != nullptr && Cell.Widget->GetWidgetActive());
		}
	}
	return true;
}

#endif
