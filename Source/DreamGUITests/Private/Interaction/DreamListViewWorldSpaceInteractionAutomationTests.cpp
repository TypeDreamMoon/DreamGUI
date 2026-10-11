// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamButton.h"
#include "Controls/DreamListView.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamEventSystem.h"
#include "InputCoreTypes.h"

#include "Driver/DreamDriverRig.h"
#include "Interaction/DreamControlsConfigInteractionTestHelpers.h"
#include "Interaction/DreamControlsWorldSpaceInteractionTestHelpers.h"
#include "Interaction/DreamDragInteractionTestTypes.h"
#include "Interaction/DreamListsConfigInteractionTestHelpers.h"
#include "Interaction/DreamListsInteractionTestTypes.h"

/*
 * A LIST ON A PANEL IN THE LEVEL, UNDER A FINGER AND THE PAD.
 *
 * UMG's list is SListView over STableViewBase, its rows SObjectTableRow. In a UWidgetComponent a finger is the interaction's
 * pointer index landing on the component's window: a tap on a row selects it as a click does (SObjectTableRow's press and
 * release), and a finger dragged along the list scrolls it by its travel in the list's own units (STableViewBase::
 * OnTouchMoved, the cursor's delta over the geometry's scale, Slate/Private/Widgets/Views/STableViewBase.cpp:760-783). The pad
 * walks the rows one a press and scrolls the row it lands on into the window (SListView::NavigationSelect,
 * RequestNavigateToItem) -- the window standing in the level.
 *
 * Rows forty units tall with nothing between them, so every point worked out below is a multiple of the pitch.
 */
namespace DreamListViewWorldTestLocal
{
	constexpr float RowHeight = 40.0f;

	/** A list of InItemCount items, InSize, at InPosition on InPanel (panel units, Y up), a finger allowed to scroll it, laid out. */
	UDreamListView* MakeList(FDreamDriverRig& InRig, UDreamWidget* InPanel, const FVector2D& InSize, const FVector2D& InPosition, int32 InItemCount)
	{
		UDreamListView* List = InRig.MakeControl<UDreamListView>(TEXT("List"), InPanel, InSize, InPosition);
		if (List == nullptr)
		{
			return nullptr;
		}
		List->SetStyleSource(EDreamUIStyleSource::Inline);
		List->SetStyle(DreamListsInteraction::WithRows(List->GetStyle(), RowHeight));
		List->SetItemObjects(DreamListsInteraction::MakeItems(InItemCount));
		List->SetEnableTouchScrolling(true);
		if (InRig.EventSystem() != nullptr)
		{
			InRig.EventSystem()->SetDoubleClickTime(0.0f);
		}
		// Two frames: the first lays the rows out, the second settles whatever the first dirtied.
		InRig.PumpFrames(2);
		return List;
	}

	/** Whether player 0's focus is on item InItemIndex's row, and that row is seen inside InWindow, to within a pixel and a half. */
	bool IsFocusOnRowSeenInside(const FDreamDriverRig& InRig, const UDreamListViewBase& InList, int32 InItemIndex, const FBox2D& InWindow)
	{
		const UDreamWidget* Row = InList.GetRowWidget(InItemIndex);
		const TOptional<FBox2D> Rect = DreamControlsWorldTest::SeenRect(InRig, Row);
		return Row != nullptr && DreamControlsConfigTest::FocusIsIn(InRig, Row) && Row->GetRenderVisibleInHierarchy() && Rect.IsSet()
			&& Rect->Min.Y >= InWindow.Min.Y - 1.5 && Rect->Max.Y <= InWindow.Max.Y + 1.5;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamListViewWorldTapTest,
	"DreamGUI.ListView.OnAWorldPanelATapChoosesTheRowSeenUnderItRightUpToTheRowsEdges",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamListViewWorldTapTest, "DreamGUI.ListView.OnAWorldPanelATapChoosesTheRowSeenUnderItRightUpToTheRowsEdges", "[Touch][World]")

/*
 * Thirty items in a list on a panel three metres ahead; a 40-unit row is seen about 85 pixels tall. A tap three pixels above
 * the third row's bottom edge as the camera sees it chooses the third row, with one selection change and one click; a tap
 * three pixels below the fourth row's top edge -- six pixels further down -- chooses the fourth.
 */
bool FDreamListViewWorldTapTest::RunTest(const FString& Parameters)
{
	using namespace DreamListViewWorldTestLocal;
	using namespace DreamControlsConfigTest;
	using namespace DreamControlsWorldTest;
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
	UDreamListView* List = MakeList(Rig, Stage.Panel, FVector2D(300.0, 280.0), FVector2D(-60.0, 0.0), 30);
	if (!TestNotNull(TEXT("A list came up on the panel"), List))
	{
		return false;
	}
	TStrongObjectPtr<UDreamListsInteractionProbe> Probe(NewObject<UDreamListsInteractionProbe>());
	DreamListsInteraction::ListenToList(*List, *Probe);
	const TOptional<FBox2D> Third = SeenRect(Rig, List->GetRowWidget(2));
	const TOptional<FBox2D> Fourth = SeenRect(Rig, List->GetRowWidget(3));
	if (!TestTrue(TEXT("The camera sees the third and fourth rows"), Third.IsSet() && Fourth.IsSet()))
	{
		return false;
	}
	TestNearlyEqual(TEXT("A row is seen its pitch in panel units tall"), Fourth->Max.Y - Fourth->Min.Y,
		static_cast<double>(RowHeight) * PixelsPerUnit(), 1.5);

	TestTrue(TEXT("A tap just above the third row's bottom edge completes"), TapAt(Rig, FVector2D(Third->GetCenter().X, Third->Max.Y - 3.0)));
	TestEqual(TEXT("It chose the third row"), List->GetSelectedIndex(), 2);
	TestEqual(TEXT("...with one selection change"), Probe->SelectionChanges.Num(), 1);
	if (TestEqual(TEXT("...and one click"), Probe->ClickedItems.Num(), 1))
	{
		TestEqual(TEXT("...for the third item"), Probe->ClickedItems[0], 2);
	}
	TestTrue(TEXT("A tap just below the fourth row's top edge completes"), TapAt(Rig, FVector2D(Fourth->GetCenter().X, Fourth->Min.Y + 3.0)));
	TestEqual(TEXT("It chose the fourth row"), List->GetSelectedIndex(), 3);
	TestEqual(TEXT("One row is chosen"), List->GetNumItemsSelected(), 1);
	TestEqual(TEXT("Nothing scrolled"), List->GetScrollOffset(), 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamListViewWorldFingerTest,
	"DreamGUI.ListView.OnAWorldPanelAFingerDraggedAlongTheListScrollsItByItsTravelInThePanelsUnits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamListViewWorldFingerTest, "DreamGUI.ListView.OnAWorldPanelAFingerDraggedAlongTheListScrollsItByItsTravelInThePanelsUnits", "[Touch][World]")

/*
 * Sixty items in a list 280 units tall on the panel. A finger lands on a row near the window's bottom, crosses the world
 * pointer's drag distance and goes on 150 pixels up: while it is still down the list has scrolled by those 150 pixels in its
 * own units -- about seventy -- give or take the stretch that only decided this was a drag. No row was chosen.
 */
bool FDreamListViewWorldFingerTest::RunTest(const FString& Parameters)
{
	using namespace DreamListViewWorldTestLocal;
	using namespace DreamControlsConfigTest;
	using namespace DreamControlsWorldTest;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(DreamControlsWorldTest::ViewportSize());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable())
		|| !TestTrue(TEXT("Its UI has begun play, as a game's has"), DreamDragInteraction::BeginPlayForUI(Rig.GetWorld())))
	{
		return false;
	}
	const FWorldStage Stage = SetUpPanel(*this, Rig);
	if (!Stage.IsReady())
	{
		return false;
	}
	UDreamListView* List = MakeList(Rig, Stage.Panel, FVector2D(300.0, 280.0), FVector2D(-60.0, 0.0), 60);
	const TOptional<FBox2D> Window = List != nullptr ? SeenRect(Rig, List->ViewportNode.Get()) : TOptional<FBox2D>();
	if (!TestTrue(TEXT("A list came up on the panel with its window seen by the camera"), Window.IsSet()))
	{
		return false;
	}
	const double FirstMove = DreamControlsWorldTest::PastDragThreshold(Stage.Pointer);
	const double Travel = 150.0;
	const FVector2D Landed(Window->GetCenter().X, Window->Max.Y - 30.0);

	TestTrue(TEXT("The finger's drag up the list completes"),
		Rig.Driver()->Sequence()
			.TouchDown(0, Landed)
			.TouchMoveTo(0, Landed - FVector2D(0.0, FirstMove))
			.TouchMoveTo(0, Landed - FVector2D(0.0, FirstMove + Travel * 0.5))
			.TouchMoveTo(0, Landed - FVector2D(0.0, FirstMove + Travel))
			.Perform());
	const double WhileHeld = List->GetScrollOffset();
	const double Least = (Travel - 6.0) / PixelsPerUnit();
	const double Most = (FirstMove + Travel + 2.0) / PixelsPerUnit();
	TestTrue(FString::Printf(TEXT("With the finger still down the list followed it by its travel in the panel's units: between %.1f and %.1f (it is %.1f)"),
		Least, Most, WhileHeld), WhileHeld >= Least && WhileHeld <= Most);
	TestTrue(TEXT("The finger lifts"), Rig.Driver()->Sequence().TouchUp(0).Perform());
	DreamListsConfigTest::WaitUntilStill(Rig, *List);
	TestEqual(TEXT("No row was chosen"), List->GetNumItemsSelected(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamListViewWorldPadTest,
	"DreamGUI.ListView.OnAWorldPanelTheDPadDownPastTheLastRowOnShowScrollsTheNextRowIntoTheWindowAsSeenWithTheRingOnIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamListViewWorldPadTest, "DreamGUI.ListView.OnAWorldPanelTheDPadDownPastTheLastRowOnShowScrollsTheNextRowIntoTheWindowAsSeenWithTheRingOnIt", "[Nav][World]")

/*
 * A button above a list of twenty items in a window five rows tall, on the panel, the focus on the button. D-pad down enters
 * the list at its first row; then D-pad down eight times: after every press the focus is on the next row and that row is seen
 * inside the window, the list having scrolled it in once it was below. The ring sits on the last row where the camera sees it.
 */
bool FDreamListViewWorldPadTest::RunTest(const FString& Parameters)
{
	using namespace DreamListViewWorldTestLocal;
	using namespace DreamControlsConfigTest;
	using namespace DreamControlsWorldTest;
	FFocusDrawnByKeys Settings;
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
	UDreamButton* Above = Rig.MakeControl<UDreamButton>(TEXT("Above"), Stage.Panel, FVector2D(160.0, 40.0), FVector2D(0.0, 115.0));
	UDreamListView* List = MakeList(Rig, Stage.Panel, FVector2D(300.0, 5.0 * RowHeight), FVector2D(0.0, -40.0), 20);
	const TOptional<FBox2D> Window = List != nullptr ? SeenRect(Rig, List->ViewportNode.Get()) : TOptional<FBox2D>();
	if (!TestTrue(TEXT("A button and, below it, a list with its window seen by the camera came up on the panel"),
		Above != nullptr && Above->FaceNode != nullptr && Window.IsSet()))
	{
		return false;
	}
	if (!TestTrue(TEXT("The focus goes onto the button"), PutFocusOn(Rig, Above->FaceNode.Get())))
	{
		return false;
	}
	TestTrue(TEXT("D-pad down completes"), PressKey(Rig, EKeys::Gamepad_DPad_Down));
	if (!TestTrue(TEXT("D-pad down entered the list at its first row"), IsFocusOnRowSeenInside(Rig, *List, 0, Window.GetValue())))
	{
		return false;
	}

	for (int32 Press = 1; Press <= 8; ++Press)
	{
		TestTrue(FString::Printf(TEXT("D-pad down %d completes"), Press), PressKey(Rig, EKeys::Gamepad_DPad_Down));
		DreamListsConfigTest::WaitUntilStill(Rig, *List);
		if (!TestTrue(FString::Printf(TEXT("After D-pad down %d the focus is on row %d, seen inside the window %s"), Press, Press,
			*Describe(Window.GetValue())), IsFocusOnRowSeenInside(Rig, *List, Press, Window.GetValue())))
		{
			return false;
		}
	}
	TestTrue(FString::Printf(TEXT("The list scrolled to show the rows the pad went to (offset %.1f)"), List->GetScrollOffset()),
		List->GetScrollOffset() > 1.0f);
	FString Where;
	const bool bRingSits = WaitForRingOnFocusAsSeen(Rig, Where);
	TestTrue(FString::Printf(TEXT("The ring sits on the last row where the camera sees it (%s)"), *Where), bRingSits);
	return true;
}

#endif
