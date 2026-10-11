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
#include "Interaction/DreamListsConfigInteractionTestHelpers.h"
#include "Interaction/DreamListsInteractionTestTypes.h"

/*
 * A LIST DISABLED, AND ON A CANVAS SCALED TO HALF AND TO DOUBLE.
 *
 * UMG's list is SListView over STableViewBase, its rows SObjectTableRow. Disabled, the list and every row in it are off the hit
 * path (FHittestGrid::GetBubblePath, SlateCore/Private/Input/HittestGrid.cpp:241, cuts the path at the first disabled
 * widget): no click or tap chooses a row and no wheel or finger scrolls it. Navigation passes over it (FindFocusableWidget asks
 * every parent, HittestGrid.cpp:336, :471), so Tab and the D-pad go round it. On a scaled layer each row is hit where it is
 * drawn; a finger dragged along the list scrolls it by its travel in the list's own units (STableViewBase::OnTouchMoved, the
 * cursor's delta over the geometry's scale, Slate/Private/Widgets/Views/STableViewBase.cpp:760-783); and the D-pad scrolls the
 * row it lands on into the window as drawn (SListView::NavigationSelect, RequestNavigateToItem).
 *
 * Rows forty units tall with nothing between them, so every pixel worked out below is a multiple of the pitch.
 */
namespace DreamListViewConfigTestLocal
{
	constexpr float RowHeight = 40.0f;

	/** A list of InItemCount items, InSize, at InPosition (canvas units, Y up), a finger allowed to scroll it, laid out. */
	UDreamListView* MakeList(FDreamDriverRig& InRig, const FVector2D& InSize, const FVector2D& InPosition, int32 InItemCount)
	{
		UDreamListView* List = InRig.MakeControl<UDreamListView>(TEXT("List"), nullptr, InSize, InPosition);
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

	FDreamElementRef RowOf(FDreamDriverRig& InRig, UDreamListViewBase& InList, int32 InItemIndex)
	{
		return InRig.Driver()->Find(FDreamBy::Widget(InList.GetRowWidget(InItemIndex)));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamListViewDisabledPressTest,
	"DreamGUI.ListView.ADisabledListIsChosenFromByNoClickOrTapAndScrolledByNoWheelOrFingerUntilItIsEnabledAgain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamListViewDisabledPressTest, "DreamGUI.ListView.ADisabledListIsChosenFromByNoClickOrTapAndScrolledByNoWheelOrFingerUntilItIsEnabledAgain", "[Pointer][Touch][Disabled]")

/*
 * Thirty items in a window ten rows tall, the list disabled. A click on the third row, three wheel notches, a tap on the fourth
 * row and a finger dragged four rows up the list choose nothing, click nothing and scroll nothing. Enabled again, a click on
 * the third row chooses it.
 */
bool FDreamListViewDisabledPressTest::RunTest(const FString& Parameters)
{
	using namespace DreamListViewConfigTestLocal;
	using namespace DreamControlsConfigTest;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize());
	Rig.BindTest(this);
	UDreamListView* List = Rig.IsUsable() ? MakeList(Rig, FVector2D(300.0, 400.0), FVector2D::ZeroVector, 30) : nullptr;
	if (!TestNotNull(TEXT("The rig and a list came up"), List))
	{
		return false;
	}
	List->SetIsEnabled(false);
	Rig.PumpFrames(1);
	TStrongObjectPtr<UDreamListsInteractionProbe> Probe(NewObject<UDreamListsInteractionProbe>());
	DreamListsInteraction::ListenToList(*List, *Probe);
	FDreamElementRef ThirdRow = RowOf(Rig, *List, 2);
	if (!TestTrue(TEXT("The third and seventh items have rows"), ThirdRow->Exists() && RowOf(Rig, *List, 6)->Exists()))
	{
		return false;
	}

	TestTrue(TEXT("Clicking the third row completes"), ThirdRow->Click());
	TestTrue(TEXT("Three wheel notches over the list complete"), Rig.Driver()->Find(FDreamBy::Widget(List))->ScrollBy(FVector2D(0.0, -3.0)));
	TestTrue(TEXT("Tapping the fourth row completes"), RowOf(Rig, *List, 3)->Tap());
	TestTrue(TEXT("A finger dragged four rows up the list completes"), RowOf(Rig, *List, 6)->TouchDragBy(FVector2D(0.0, -4.0 * RowHeight)));
	DreamListsConfigTest::WaitUntilStill(Rig, *List);
	TestEqual(TEXT("Nothing was chosen"), List->GetNumItemsSelected(), 0);
	TestEqual(TEXT("...no choice was announced"), Probe->SelectionChanges.Num(), 0);
	TestEqual(TEXT("...no row was clicked"), Probe->ClickedItems.Num(), 0);
	TestNearlyEqual(TEXT("...and the list is still at the top"), List->GetScrollOffset(), 0.0f, 0.5f);

	List->SetIsEnabled(true);
	Rig.PumpFrames(1);
	TestTrue(TEXT("Clicking the third row of the list enabled again completes"), RowOf(Rig, *List, 2)->Click());
	TestEqual(TEXT("Enabled again, the click chooses the third row"), List->GetSelectedIndex(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamListViewDisabledPadTest,
	"DreamGUI.ListView.TabAndTheDPadPassOverADisabledListAndOnceEnabledTabEntersItAtItsFirstRow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamListViewDisabledPadTest, "DreamGUI.ListView.TabAndTheDPadPassOverADisabledListAndOnceEnabledTabEntersItAtItsFirstRow", "[Nav][Disabled]")

/*
 * A button above a disabled list and one below it, the focus on the top button. Tab passes over the list to the bottom button
 * and Shift+Tab back; D-pad down passes over it too. Enabled again, Tab from the top button enters the list at its first row,
 * and D-pad down moves the focus -- and the choice, which follows navigation -- to the second.
 */
bool FDreamListViewDisabledPadTest::RunTest(const FString& Parameters)
{
	using namespace DreamListViewConfigTestLocal;
	using namespace DreamControlsConfigTest;
	FFocusDrawnByKeys Settings;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize());
	Rig.BindTest(this);
	UDreamButton* Above = Rig.IsUsable() ? Rig.MakeControl<UDreamButton>(TEXT("Above"), nullptr, FVector2D(160.0, 40.0), FVector2D(0.0, 260.0)) : nullptr;
	UDreamListView* List = Rig.IsUsable() ? MakeList(Rig, FVector2D(300.0, 400.0), FVector2D::ZeroVector, 30) : nullptr;
	UDreamButton* Below = Rig.IsUsable() ? Rig.MakeControl<UDreamButton>(TEXT("Below"), nullptr, FVector2D(160.0, 40.0), FVector2D(0.0, -260.0)) : nullptr;
	if (!TestTrue(TEXT("A button, a list and a button came up"), Above != nullptr && List != nullptr && Below != nullptr
		&& Above->FaceNode != nullptr && Below->FaceNode != nullptr))
	{
		return false;
	}
	List->SetIsEnabled(false);
	Rig.PumpFrames(1);
	if (!TestTrue(TEXT("The focus goes onto the top button"), PutFocusOn(Rig, Above->FaceNode.Get())))
	{
		return false;
	}

	TestTrue(TEXT("Tab completes"), Rig.Driver()->Sequence().Tab().WaitFrames(1).Perform());
	TestTrue(TEXT("Tab passes over the disabled list to the bottom button"), FocusIsIn(Rig, Below));
	TestTrue(TEXT("Shift+Tab completes"), Rig.Driver()->Sequence().ShiftTab().WaitFrames(1).Perform());
	TestTrue(TEXT("Shift+Tab passes back over it"), FocusIsIn(Rig, Above));
	TestTrue(TEXT("D-pad down completes"), PressKey(Rig, EKeys::Gamepad_DPad_Down));
	TestTrue(TEXT("D-pad down passes over it too"), FocusIsIn(Rig, Below));
	TestEqual(TEXT("Nothing in the list was chosen on the way"), List->GetNumItemsSelected(), 0);

	List->SetIsEnabled(true);
	Rig.PumpFrames(1);
	TestTrue(TEXT("The focus goes back onto the top button"), PutFocusOn(Rig, Above->FaceNode.Get()));
	TestTrue(TEXT("Tab completes once the list is enabled"), Rig.Driver()->Sequence().Tab().WaitFrames(1).Perform());
	TestTrue(TEXT("Enabled again, Tab enters the list at its first row"), List->GetRowWidget(0) != nullptr && FocusIsIn(Rig, List->GetRowWidget(0)));
	TestTrue(TEXT("D-pad down completes"), PressKey(Rig, EKeys::Gamepad_DPad_Down));
	TestTrue(TEXT("...and D-pad down moves the focus to the second row"), List->GetRowWidget(1) != nullptr && FocusIsIn(Rig, List->GetRowWidget(1)));
	TestEqual(TEXT("...the choice following it"), List->GetSelectedIndex(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamListViewScaledPressTest,
	"DreamGUI.ListView.OnACanvasScaledToHalfAndToDoubleAClickOrATapChoosesTheRowDrawnUnderItRightUpToTheRowsDrawnEdge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamListViewScaledPressTest, "DreamGUI.ListView.OnACanvasScaledToHalfAndToDoubleAClickOrATapChoosesTheRowDrawnUnderItRightUpToTheRowsDrawnEdge", "[Pointer][Touch][Scaled]")

/*
 * A list 560 pixels tall off the middle, at each scale: its 40-unit rows are 20 pixels apart at half scale and 80 at double. A
 * click three pixels above the third row's drawn bottom edge chooses the third row, and one three pixels below the fourth
 * row's drawn top edge -- six pixels further down -- chooses the fourth. Taps three pixels inside the fifth row's bottom and
 * the sixth row's top choose the fifth and then the sixth.
 */
bool FDreamListViewScaledPressTest::RunTest(const FString& Parameters)
{
	using namespace DreamListViewConfigTestLocal;
	using namespace DreamControlsConfigTest;
	for (const double Scale : Scales())
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(ScaledOptions(Scale));
		Rig.BindTest(this);
		if (!CameUpAt(*this, Rig, Scale))
		{
			return false;
		}
		UDreamListView* List = MakeList(Rig, FVector2D(300.0, 560.0 / Scale), FVector2D(-100.0, 0.0), 30);
		if (!TestNotNull(*At(Scale, TEXT("A list came up")), List))
		{
			return false;
		}
		TOptional<FBox2D> Rows[6];
		bool bAllDrawn = true;
		for (int32 Item = 2; Item < 6; ++Item)
		{
			Rows[Item] = DreamListsConfigTest::RowRect(*List, Item);
			bAllDrawn &= Rows[Item].IsSet();
		}
		if (!TestTrue(At(Scale, TEXT("The third to sixth rows are drawn")), bAllDrawn))
		{
			return false;
		}
		TestNearlyEqual(*At(Scale, TEXT("A row is drawn its pitch times the scale tall")), Rows[3]->Max.Y - Rows[3]->Min.Y,
			static_cast<double>(RowHeight) * Scale, 1.5);

		TestTrue(At(Scale, TEXT("A click just above the third row's bottom edge completes")),
			ClickAt(Rig, FVector2D(Rows[2]->GetCenter().X, Rows[2]->Max.Y - 3.0)));
		TestEqual(*At(Scale, TEXT("It chose the third row")), List->GetSelectedIndex(), 2);
		TestTrue(At(Scale, TEXT("A click just below the fourth row's top edge completes")),
			ClickAt(Rig, FVector2D(Rows[3]->GetCenter().X, Rows[3]->Min.Y + 3.0)));
		TestEqual(*At(Scale, TEXT("It chose the fourth row")), List->GetSelectedIndex(), 3);

		TestTrue(At(Scale, TEXT("A tap just above the fifth row's bottom edge completes")),
			TapAt(Rig, FVector2D(Rows[4]->GetCenter().X, Rows[4]->Max.Y - 3.0)));
		TestEqual(*At(Scale, TEXT("It chose the fifth row")), List->GetSelectedIndex(), 4);
		TestTrue(At(Scale, TEXT("A tap just below the sixth row's top edge completes")),
			TapAt(Rig, FVector2D(Rows[5]->GetCenter().X, Rows[5]->Min.Y + 3.0)));
		TestEqual(*At(Scale, TEXT("It chose the sixth row")), List->GetSelectedIndex(), 5);
		TestEqual(*At(Scale, TEXT("One row is chosen")), List->GetNumItemsSelected(), 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamListViewScaledFingerTest,
	"DreamGUI.ListView.OnACanvasScaledToHalfAndToDoubleAFingerDraggedAlongTheListScrollsItByItsTravelInTheListsUnits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamListViewScaledFingerTest, "DreamGUI.ListView.OnACanvasScaledToHalfAndToDoubleAFingerDraggedAlongTheListScrollsItByItsTravelInTheListsUnits", "[Touch][Scaled]")

/*
 * A list 400 pixels tall at either scale. A finger lands on a row near its bottom, crosses the drag distance and goes on 120
 * pixels up: while it is still down the list has scrolled by those 120 pixels in its own units -- 240 at half scale, 60 at
 * double -- give or take the stretch that only decided this was a drag. No row was chosen.
 */
bool FDreamListViewScaledFingerTest::RunTest(const FString& Parameters)
{
	using namespace DreamListViewConfigTestLocal;
	using namespace DreamControlsConfigTest;
	for (const double Scale : Scales())
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(ScaledOptions(Scale));
		Rig.BindTest(this);
		if (!CameUpAt(*this, Rig, Scale))
		{
			return false;
		}
		UDreamListView* List = MakeList(Rig, FVector2D(300.0, 400.0 / Scale), FVector2D(-100.0, 0.0), 60);
		const TOptional<FBox2D> Window = List != nullptr ? DreamListsConfigTest::WindowRect(*List) : TOptional<FBox2D>();
		if (!TestTrue(At(Scale, TEXT("A list with its window on the viewport came up")), Window.IsSet()))
		{
			return false;
		}
		const double FirstMove = PastDragThreshold(Rig);
		const double Travel = 120.0;
		const FVector2D Landed(Window->GetCenter().X, Window->Max.Y - 30.0);

		TestTrue(At(Scale, TEXT("The finger's drag up the list completes")),
			Rig.Driver()->Sequence()
				.TouchDown(0, Landed)
				.TouchMoveTo(0, Landed - FVector2D(0.0, FirstMove))
				.TouchMoveTo(0, Landed - FVector2D(0.0, FirstMove + Travel * 0.5))
				.TouchMoveTo(0, Landed - FVector2D(0.0, FirstMove + Travel))
				.Perform());
		const double WhileHeld = List->GetScrollOffset();
		const double Least = (Travel - 6.0) / Scale;
		const double Most = (FirstMove + Travel + 2.0) / Scale;
		TestTrue(At(Scale, FString::Printf(TEXT("With the finger still down the list followed it by its travel in the list's units: between %.1f and %.1f (it is %.1f)"),
			Least, Most, WhileHeld)), WhileHeld >= Least && WhileHeld <= Most);
		TestTrue(At(Scale, TEXT("The finger lifts")), Rig.Driver()->Sequence().TouchUp(0).Perform());
		DreamListsConfigTest::WaitUntilStill(Rig, *List);
		TestEqual(*At(Scale, TEXT("No row was chosen")), List->GetNumItemsSelected(), 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamListViewScaledPadTest,
	"DreamGUI.ListView.OnACanvasScaledToHalfAndToDoubleTheDPadDownPastTheLastRowOnShowScrollsTheNextRowIntoTheDrawnWindowWithTheRingOnIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamListViewScaledPadTest, "DreamGUI.ListView.OnACanvasScaledToHalfAndToDoubleTheDPadDownPastTheLastRowOnShowScrollsTheNextRowIntoTheDrawnWindowWithTheRingOnIt", "[Nav][Scaled]")

/*
 * Thirty items in a window 300 pixels tall at either scale -- fifteen rows at half scale, under four at double. Tab enters the
 * list at its first row; then D-pad down eighteen times: after every press the focus is on the next row and that row is drawn
 * inside the window as drawn, the list having scrolled it in once it was below. The ring sits on the last row where it is
 * drawn.
 */
bool FDreamListViewScaledPadTest::RunTest(const FString& Parameters)
{
	using namespace DreamListViewConfigTestLocal;
	using namespace DreamControlsConfigTest;
	FFocusDrawnByKeys Settings;
	for (const double Scale : Scales())
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(ScaledOptions(Scale));
		Rig.BindTest(this);
		if (!CameUpAt(*this, Rig, Scale))
		{
			return false;
		}
		UDreamListView* List = MakeList(Rig, FVector2D(300.0, 300.0 / Scale), FVector2D(100.0, 0.0), 30);
		const TOptional<FBox2D> Window = List != nullptr ? DreamListsConfigTest::WindowRect(*List) : TOptional<FBox2D>();
		if (!TestTrue(At(Scale, TEXT("A list with its window on the viewport came up")), Window.IsSet()))
		{
			return false;
		}
		TestTrue(At(Scale, TEXT("Tab completes")), Rig.Driver()->Sequence().Tab().WaitFrames(1).Perform());
		if (!TestTrue(At(Scale, TEXT("Tab entered the list at its first row")), DreamListsConfigTest::IsFocusOnRowInside(Rig, *List, 0, Window.GetValue())))
		{
			return false;
		}

		for (int32 Press = 1; Press <= 18; ++Press)
		{
			TestTrue(At(Scale, FString::Printf(TEXT("D-pad down %d completes"), Press)), PressKey(Rig, EKeys::Gamepad_DPad_Down));
			DreamListsConfigTest::WaitUntilStill(Rig, *List);
			if (!TestTrue(At(Scale, FString::Printf(TEXT("After D-pad down %d the focus is on row %d, drawn inside the window %s"), Press, Press,
				*Describe(Window.GetValue()))), DreamListsConfigTest::IsFocusOnRowInside(Rig, *List, Press, Window.GetValue())))
			{
				return false;
			}
		}
		TestTrue(At(Scale, FString::Printf(TEXT("The list scrolled to show the rows the pad went to (offset %.1f)"), List->GetScrollOffset())),
			List->GetScrollOffset() > 1.0f);
		FString Where;
		const bool bRingSits = WaitForRingOnFocus(Rig, Where);
		TestTrue(At(Scale, FString::Printf(TEXT("The ring sits on the last row where it is drawn (%s)"), *Where)), bRingSits);
	}
	return true;
}

#endif
