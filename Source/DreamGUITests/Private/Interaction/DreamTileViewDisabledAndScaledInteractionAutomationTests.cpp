// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamButton.h"
#include "Controls/DreamTileView.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamEventSystem.h"
#include "InputCoreTypes.h"

#include "Driver/DreamDriverRig.h"
#include "Interaction/DreamControlsConfigInteractionTestHelpers.h"
#include "Interaction/DreamListsConfigInteractionTestHelpers.h"
#include "Interaction/DreamListsInteractionTestTypes.h"

/*
 * A TILE VIEW DISABLED, AND ON A CANVAS SCALED TO HALF AND TO DOUBLE.
 *
 * UMG's tile view is STileView, an SListView laid out in lines of tiles, and inherits its input. Disabled, it and its tiles are
 * off the hit path (FHittestGrid::GetBubblePath, SlateCore/Private/Input/HittestGrid.cpp:241) and passed over by navigation
 * (FindFocusableWidget asks every parent, HittestGrid.cpp:336, :471). On a scaled layer each tile is hit where it is drawn, and
 * the D-pad scrolls the line it moves to into the window as drawn (SListView::NavigationSelect, RequestNavigateToItem). How
 * many tiles fit across is a matter of the view's own units, so the lines are the same at every scale.
 *
 * Tiles 80 units wide and 60 tall with nothing between them, four to a line in a view 360 wide.
 */
namespace DreamTileViewConfigTestLocal
{
	constexpr float TileWidth = 80.0f;
	constexpr float LineHeight = 60.0f;

	UDreamTileView* MakeTiles(FDreamDriverRig& InRig, double InHeight, const FVector2D& InPosition, int32 InItemCount)
	{
		UDreamTileView* Tiles = InRig.MakeControl<UDreamTileView>(TEXT("Tiles"), nullptr, FVector2D(360.0, InHeight), InPosition);
		if (Tiles == nullptr)
		{
			return nullptr;
		}
		Tiles->SetStyleSource(EDreamUIStyleSource::Inline);
		FDreamTileViewStyle TileStyle = Tiles->GetStyle();
		TileStyle.List = DreamListsInteraction::WithRows(TileStyle.List, LineHeight);
		TileStyle.TileWidth = TileWidth;
		TileStyle.TileSpacing = 0.0f;
		Tiles->SetStyle(TileStyle);
		Tiles->SetItemObjects(DreamListsInteraction::MakeItems(InItemCount));
		Tiles->SetEnableTouchScrolling(true);
		if (InRig.EventSystem() != nullptr)
		{
			InRig.EventSystem()->SetDoubleClickTime(0.0f);
		}
		InRig.PumpFrames(2);
		return Tiles;
	}

	FDreamElementRef TileOf(FDreamDriverRig& InRig, UDreamListViewBase& InTiles, int32 InItemIndex)
	{
		return InRig.Driver()->Find(FDreamBy::Widget(InTiles.GetRowWidget(InItemIndex)));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTileViewDisabledPressTest,
	"DreamGUI.TileView.ADisabledTileViewIsChosenFromByNoClickOrTapAndScrolledByNoWheelOrFingerUntilItIsEnabledAgain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamTileViewDisabledPressTest, "DreamGUI.TileView.ADisabledTileViewIsChosenFromByNoClickOrTapAndScrolledByNoWheelOrFingerUntilItIsEnabledAgain", "[Pointer][Touch][Disabled]")

/*
 * Forty tiles in a window five lines tall, the view disabled. A click on the third tile, three wheel notches, a tap on the
 * sixth tile and a finger dragged two lines up the view choose nothing, click nothing and scroll nothing. Enabled again, a
 * click on the third tile chooses it.
 */
bool FDreamTileViewDisabledPressTest::RunTest(const FString& Parameters)
{
	using namespace DreamTileViewConfigTestLocal;
	using namespace DreamControlsConfigTest;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize());
	Rig.BindTest(this);
	UDreamTileView* Tiles = Rig.IsUsable() ? MakeTiles(Rig, 300.0, FVector2D::ZeroVector, 40) : nullptr;
	if (!TestTrue(TEXT("The rig and a tile view four tiles across came up"), Tiles != nullptr && Tiles->GetColumnCount() == 4))
	{
		return false;
	}
	Tiles->SetIsEnabled(false);
	Rig.PumpFrames(1);
	TStrongObjectPtr<UDreamListsInteractionProbe> Probe(NewObject<UDreamListsInteractionProbe>());
	DreamListsInteraction::ListenToList(*Tiles, *Probe);

	TestTrue(TEXT("Clicking the third tile completes"), TileOf(Rig, *Tiles, 2)->Click());
	TestTrue(TEXT("Three wheel notches over the view complete"), Rig.Driver()->Find(FDreamBy::Widget(Tiles))->ScrollBy(FVector2D(0.0, -3.0)));
	TestTrue(TEXT("Tapping the sixth tile completes"), TileOf(Rig, *Tiles, 5)->Tap());
	TestTrue(TEXT("A finger dragged two lines up the view completes"), TileOf(Rig, *Tiles, 13)->TouchDragBy(FVector2D(0.0, -2.0 * LineHeight)));
	DreamListsConfigTest::WaitUntilStill(Rig, *Tiles);
	TestEqual(TEXT("Nothing was chosen"), Tiles->GetNumItemsSelected(), 0);
	TestEqual(TEXT("...no choice was announced"), Probe->SelectionChanges.Num(), 0);
	TestEqual(TEXT("...no tile was clicked"), Probe->ClickedItems.Num(), 0);
	TestNearlyEqual(TEXT("...and the view is still at the top"), Tiles->GetScrollOffset(), 0.0f, 0.5f);

	Tiles->SetIsEnabled(true);
	Rig.PumpFrames(1);
	TestTrue(TEXT("Clicking the third tile of the view enabled again completes"), TileOf(Rig, *Tiles, 2)->Click());
	TestEqual(TEXT("Enabled again, the click chooses the third tile"), Tiles->GetSelectedIndex(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTileViewDisabledPadTest,
	"DreamGUI.TileView.TabAndTheDPadPassOverADisabledTileViewAndOnceEnabledTabEntersItAtItsFirstTile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamTileViewDisabledPadTest, "DreamGUI.TileView.TabAndTheDPadPassOverADisabledTileViewAndOnceEnabledTabEntersItAtItsFirstTile", "[Nav][Disabled]")

/*
 * A button above a disabled tile view and one below it, the focus on the top button. Tab passes over the view to the bottom
 * button, and D-pad up comes back over it. Enabled again, Tab from the top button enters the view at its first tile, and
 * D-pad right moves the focus one tile along.
 */
bool FDreamTileViewDisabledPadTest::RunTest(const FString& Parameters)
{
	using namespace DreamTileViewConfigTestLocal;
	using namespace DreamControlsConfigTest;
	FFocusDrawnByKeys Settings;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize());
	Rig.BindTest(this);
	UDreamButton* Above = Rig.IsUsable() ? Rig.MakeControl<UDreamButton>(TEXT("Above"), nullptr, FVector2D(160.0, 40.0), FVector2D(0.0, 220.0)) : nullptr;
	UDreamTileView* Tiles = Rig.IsUsable() ? MakeTiles(Rig, 300.0, FVector2D::ZeroVector, 40) : nullptr;
	UDreamButton* Below = Rig.IsUsable() ? Rig.MakeControl<UDreamButton>(TEXT("Below"), nullptr, FVector2D(160.0, 40.0), FVector2D(0.0, -220.0)) : nullptr;
	if (!TestTrue(TEXT("A button, a tile view and a button came up"), Above != nullptr && Tiles != nullptr && Below != nullptr
		&& Above->FaceNode != nullptr && Below->FaceNode != nullptr))
	{
		return false;
	}
	Tiles->SetIsEnabled(false);
	Rig.PumpFrames(1);
	if (!TestTrue(TEXT("The focus goes onto the top button"), PutFocusOn(Rig, Above->FaceNode.Get())))
	{
		return false;
	}

	TestTrue(TEXT("Tab completes"), Rig.Driver()->Sequence().Tab().WaitFrames(1).Perform());
	TestTrue(TEXT("Tab passes over the disabled tile view to the bottom button"), FocusIsIn(Rig, Below));
	TestTrue(TEXT("D-pad up completes"), PressKey(Rig, EKeys::Gamepad_DPad_Up));
	TestTrue(TEXT("D-pad up passes back over it to the top button"), FocusIsIn(Rig, Above));

	Tiles->SetIsEnabled(true);
	Rig.PumpFrames(1);
	TestTrue(TEXT("Tab completes once the view is enabled"), Rig.Driver()->Sequence().Tab().WaitFrames(1).Perform());
	TestTrue(TEXT("Enabled again, Tab enters the view at its first tile"), Tiles->GetRowWidget(0) != nullptr && FocusIsIn(Rig, Tiles->GetRowWidget(0)));
	TestTrue(TEXT("D-pad right completes"), PressKey(Rig, EKeys::Gamepad_DPad_Right));
	TestTrue(TEXT("...and D-pad right moves the focus to the second tile"), Tiles->GetRowWidget(1) != nullptr && FocusIsIn(Rig, Tiles->GetRowWidget(1)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTileViewScaledPressTest,
	"DreamGUI.TileView.OnACanvasScaledToHalfAndToDoubleAClickOrATapChoosesTheTileDrawnUnderItRightUpToTheTilesDrawnEdges",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamTileViewScaledPressTest, "DreamGUI.TileView.OnACanvasScaledToHalfAndToDoubleAClickOrATapChoosesTheTileDrawnUnderItRightUpToTheTilesDrawnEdges", "[Pointer][Touch][Scaled]")

/*
 * A tile view 360 pixels tall off the middle, at each scale, four tiles to a line whatever the scale. A click three pixels
 * inside the first tile's drawn right edge chooses the first tile, and one three pixels inside the second's left edge chooses
 * the second. A tap three pixels above the sixth tile's drawn bottom edge chooses the sixth, and one three pixels below the
 * tenth's top edge -- the tile under it on the next line -- chooses the tenth.
 */
bool FDreamTileViewScaledPressTest::RunTest(const FString& Parameters)
{
	using namespace DreamTileViewConfigTestLocal;
	using namespace DreamControlsConfigTest;
	for (const double Scale : Scales())
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(ScaledOptions(Scale));
		Rig.BindTest(this);
		if (!CameUpAt(*this, Rig, Scale))
		{
			return false;
		}
		UDreamTileView* Tiles = MakeTiles(Rig, 360.0 / Scale, FVector2D(-60.0, 0.0), 40);
		if (!TestTrue(At(Scale, TEXT("A tile view four tiles across came up")), Tiles != nullptr && Tiles->GetColumnCount() == 4))
		{
			return false;
		}
		const TOptional<FBox2D> First = DreamListsConfigTest::RowRect(*Tiles, 0);
		const TOptional<FBox2D> Second = DreamListsConfigTest::RowRect(*Tiles, 1);
		const TOptional<FBox2D> Sixth = DreamListsConfigTest::RowRect(*Tiles, 5);
		const TOptional<FBox2D> Tenth = DreamListsConfigTest::RowRect(*Tiles, 9);
		if (!TestTrue(At(Scale, TEXT("The tiles aimed at are drawn")), First.IsSet() && Second.IsSet() && Sixth.IsSet() && Tenth.IsSet()))
		{
			return false;
		}
		TestTrue(At(Scale, FString::Printf(TEXT("A tile is drawn its size times the scale (%s)"), *Describe(Second.GetValue()))),
			IsDrawnAtScale(Second.GetValue(), Tiles->GetRowWidget(1)->GetSize(), Scale));

		TestTrue(At(Scale, TEXT("A click just inside the first tile's right edge completes")),
			ClickAt(Rig, FVector2D(First->Max.X - 3.0, First->GetCenter().Y)));
		TestEqual(*At(Scale, TEXT("It chose the first tile")), Tiles->GetSelectedIndex(), 0);
		TestTrue(At(Scale, TEXT("A click just inside the second tile's left edge completes")),
			ClickAt(Rig, FVector2D(Second->Min.X + 3.0, Second->GetCenter().Y)));
		TestEqual(*At(Scale, TEXT("It chose the second tile")), Tiles->GetSelectedIndex(), 1);

		TestTrue(At(Scale, TEXT("A tap just above the sixth tile's bottom edge completes")),
			TapAt(Rig, FVector2D(Sixth->GetCenter().X, Sixth->Max.Y - 3.0)));
		TestEqual(*At(Scale, TEXT("It chose the sixth tile")), Tiles->GetSelectedIndex(), 5);
		TestTrue(At(Scale, TEXT("A tap just below the tenth tile's top edge completes")),
			TapAt(Rig, FVector2D(Tenth->GetCenter().X, Tenth->Min.Y + 3.0)));
		TestEqual(*At(Scale, TEXT("It chose the tenth tile")), Tiles->GetSelectedIndex(), 9);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTileViewScaledPadTest,
	"DreamGUI.TileView.OnACanvasScaledToHalfAndToDoubleTheDPadDownPastTheLastLineOnShowScrollsTheNextLineIntoTheDrawnWindowWithTheRingOnIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamTileViewScaledPadTest, "DreamGUI.TileView.OnACanvasScaledToHalfAndToDoubleTheDPadDownPastTheLastLineOnShowScrollsTheNextLineIntoTheDrawnWindowWithTheRingOnIt", "[Nav][Scaled]")

/*
 * Eighty tiles, four to a line, in a window 300 pixels tall at either scale -- ten lines at half scale, two and a half at
 * double. Tab enters the view at its first tile; then D-pad down twelve times: every press is a whole line, so the focus is on
 * the first tile of the next line, drawn inside the window as drawn, the view having scrolled it in once it was below. The
 * ring sits on the last tile reached where it is drawn.
 */
bool FDreamTileViewScaledPadTest::RunTest(const FString& Parameters)
{
	using namespace DreamTileViewConfigTestLocal;
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
		UDreamTileView* Tiles = MakeTiles(Rig, 300.0 / Scale, FVector2D(60.0, 0.0), 80);
		const TOptional<FBox2D> Window = Tiles != nullptr ? DreamListsConfigTest::WindowRect(*Tiles) : TOptional<FBox2D>();
		if (!TestTrue(At(Scale, TEXT("A tile view four tiles across with its window on the viewport came up")),
			Window.IsSet() && Tiles->GetColumnCount() == 4))
		{
			return false;
		}
		TestTrue(At(Scale, TEXT("Tab completes")), Rig.Driver()->Sequence().Tab().WaitFrames(1).Perform());
		if (!TestTrue(At(Scale, TEXT("Tab entered the view at its first tile")), DreamListsConfigTest::IsFocusOnRowInside(Rig, *Tiles, 0, Window.GetValue())))
		{
			return false;
		}

		for (int32 Press = 1; Press <= 12; ++Press)
		{
			const int32 Expected = Press * 4;
			TestTrue(At(Scale, FString::Printf(TEXT("D-pad down %d completes"), Press)), PressKey(Rig, EKeys::Gamepad_DPad_Down));
			DreamListsConfigTest::WaitUntilStill(Rig, *Tiles);
			if (!TestTrue(At(Scale, FString::Printf(TEXT("After D-pad down %d the focus is on tile %d, drawn inside the window %s"), Press, Expected,
				*Describe(Window.GetValue()))), DreamListsConfigTest::IsFocusOnRowInside(Rig, *Tiles, Expected, Window.GetValue())))
			{
				return false;
			}
		}
		TestTrue(At(Scale, FString::Printf(TEXT("The view scrolled to show the lines the pad went to (offset %.1f)"), Tiles->GetScrollOffset())),
			Tiles->GetScrollOffset() > 1.0f);
		FString Where;
		const bool bRingSits = WaitForRingOnFocus(Rig, Where);
		TestTrue(At(Scale, FString::Printf(TEXT("The ring sits on the last tile reached where it is drawn (%s)"), *Where)), bRingSits);
	}
	return true;
}

#endif
