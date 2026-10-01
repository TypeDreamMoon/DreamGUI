// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Controls/DreamTileView.h"
#include "Core/Components/DreamWidget.h"
#include "UObject/StrongObjectPtr.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Interaction/DreamListsInteractionTestTypes.h"

/*
 * A TILE VIEW IS A LIST THAT COUNTS IN LINES.
 *
 * Everything a list does, a tile view does -- the same base class, the same pool, the same
 * selection. What these add is the one question a tile view answers differently: where "the next
 * one" is. Across is the next tile, down is the next LINE, and a wheel notch moves the view by a
 * line rather than by a tile. The reference is UMG 5.8's STileView (its OnNavigation and its line
 * arithmetic), with Docs/Reference/DreamTileView.md and DreamListViewBase.md where they differ.
 *
 * Forty tiles, eighty units wide and sixty tall, in a view 360 by 300: four to a line whether or
 * not the scroll bar takes its gutter, five lines on screen, ten in all.
 */
namespace DreamTileViewInteractionTestLocal
{
	constexpr float LineHeight = 60.0f;
	constexpr float TileWidth = 80.0f;
	const FVector2D TileViewSize(360.0, 300.0);

	/** A tile view on the rig, tiles TileWidth by LineHeight with no gaps, for InItemCount fresh items. */
	UDreamTileView* MakeTiles(FDreamDriverRig& InRig, int32 InItemCount, TArray<UObject*>& OutItems)
	{
		UDreamTileView* Tiles = InRig.MakeControl<UDreamTileView>(TEXT("Tiles"), nullptr, TileViewSize);
		if (Tiles == nullptr)
		{
			return nullptr;
		}
		// Inline, so the running editor's sheet cannot change how many tiles fit across.
		Tiles->SetStyleSource(EDreamUIStyleSource::Inline);
		FDreamTileViewStyle TileStyle = Tiles->GetStyle();
		TileStyle.List = DreamListsInteraction::WithRows(TileStyle.List, LineHeight);
		TileStyle.TileWidth = TileWidth;
		TileStyle.TileSpacing = 0.0f;
		Tiles->SetStyle(TileStyle);
		OutItems = DreamListsInteraction::MakeItems(InItemCount);
		Tiles->SetItemObjects(OutItems);
		InRig.PumpFrames(2);
		return Tiles;
	}

	/** The tile standing for an item, as something to click or ask about. */
	FDreamElementRef TileElement(FDreamDriverRig& InRig, UDreamTileView& InTiles, int32 InItemIndex)
	{
		return InRig.Driver()->Find(FDreamBy::Widget(InTiles.GetRowWidget(InItemIndex)));
	}

	/** One press and release of a navigation direction. */
	bool NavigateOnce(FDreamDriverRig& InRig, EDreamUINavigationDirection InDirection)
	{
		return InRig.Driver()->Sequence().Navigate(InDirection).Perform();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamListsTileViewClickTest,
	"DreamGUI.TileView.ClickingTheSixthTileSelectsTheSecondTileOfTheSecondLine",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamListsTileViewClickTest, "DreamGUI.TileView.ClickingTheSixthTileSelectsTheSecondTileOfTheSecondLine", "[Pointer][Animated]")

bool FDreamListsTileViewClickTest::RunTest(const FString& Parameters)
{
	using namespace DreamTileViewInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(DreamListsInteraction::ViewportSize());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	TArray<UObject*> Items;
	UDreamTileView* Tiles = MakeTiles(Rig, 40, Items);
	if (!TestNotNull(TEXT("The tile view was made on the rig"), Tiles)
		|| !TestEqual(TEXT("Four tiles fit across, so the sixth opens the second line's second column"), Tiles->GetColumnCount(), 4))
	{
		return false;
	}
	TStrongObjectPtr<UDreamListsInteractionProbe> Probe(NewObject<UDreamListsInteractionProbe>());
	DreamListsInteraction::ListenToList(*Tiles, *Probe);

	FDreamElementRef SixthTile = TileElement(Rig, *Tiles, 5);
	if (!TestTrue(TEXT("The sixth item has a tile to click"), SixthTile->Exists()))
	{
		return false;
	}
	TestTrue(TEXT("Clicking the sixth tile completes"), SixthTile->Click());

	// A click on a tile is a click on a row: the item under the pointer is what gets selected, and
	// the pointer found it by where the tile is DRAWN -- which is what a wrong column count breaks.
	TestEqual(TEXT("The sixth item is the selection"), Tiles->GetSelectedIndex(), 5);
	if (TestEqual(TEXT("One click was announced"), Probe->ClickedItems.Num(), 1))
	{
		TestEqual(TEXT("for the sixth item"), Probe->ClickedItems[0], 5);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamListsTileViewWheelLinesTest,
	"DreamGUI.TileView.AWheelNotchScrollsAWholeLineOfTilesNotOneTile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamListsTileViewWheelLinesTest, "DreamGUI.TileView.AWheelNotchScrollsAWholeLineOfTilesNotOneTile", "[Pointer][Animated]")

bool FDreamListsTileViewWheelLinesTest::RunTest(const FString& Parameters)
{
	using namespace DreamTileViewInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(DreamListsInteraction::ViewportSize());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	TArray<UObject*> Items;
	UDreamTileView* Tiles = MakeTiles(Rig, 40, Items);
	if (!TestNotNull(TEXT("The tile view was made on the rig"), Tiles)
		|| !TestEqual(TEXT("Four tiles fit across"), Tiles->GetColumnCount(), 4)
		|| !TestEqual(TEXT("The view starts at the top"), Tiles->GetScrollOffset(), 0.0f, 0.01f))
	{
		return false;
	}

	FDreamElementRef TileViewElement = Rig.Driver()->Find(FDreamBy::Widget(Tiles));
	TestTrue(TEXT("One wheel notch over the tiles completes"), TileViewElement->ScrollBy(FVector2D(-1.0, -1.0)));

	// The unit is a LINE: STileView's scroll offset counts lines of NumItemsPerLine, and here a notch
	// is one row pitch (DreamListViewBase.md: "A list's notch is a ROW"), which in a grid is a line.
	TestNearlyEqual(TEXT("The view moved exactly one line"), Tiles->GetScrollOffset(), LineHeight, 0.5f);
	// So the tile now at the top-left is the fifth -- the first of the second line -- and not the
	// second, which is where a view counting in tiles would have landed.
	const TOptional<FBox2D> WindowRect = Rig.Driver()->Find(FDreamBy::Widget(Tiles->ViewportNode.Get()))->GetPixelRect();
	const TOptional<FBox2D> FifthTileRect = TileElement(Rig, *Tiles, 4)->GetPixelRect();
	if (TestTrue(TEXT("The window and the fifth tile both project to pixels"), WindowRect.IsSet() && FifthTileRect.IsSet()))
	{
		TestNearlyEqual(TEXT("The fifth tile's top edge is the window's top edge"),
			FifthTileRect->Min.Y, WindowRect->Min.Y, 1.5);
		TestNearlyEqual(TEXT("and it sits at the window's left edge"),
			FifthTileRect->Min.X, WindowRect->Min.X, 1.5);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamListsTileViewNavigateSelectsTest,
	"DreamGUI.TileView.NavigatingRightThenDownMovesTheSelectionOneTileThenOneLine",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamListsTileViewNavigateSelectsTest, "DreamGUI.TileView.NavigatingRightThenDownMovesTheSelectionOneTileThenOneLine", "[Pointer][Nav][Animated]")

bool FDreamListsTileViewNavigateSelectsTest::RunTest(const FString& Parameters)
{
	using namespace DreamTileViewInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(DreamListsInteraction::ViewportSize());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	TArray<UObject*> Items;
	UDreamTileView* Tiles = MakeTiles(Rig, 40, Items);
	if (!TestNotNull(TEXT("The tile view was made on the rig"), Tiles)
		|| !TestEqual(TEXT("Four tiles fit across"), Tiles->GetColumnCount(), 4))
	{
		return false;
	}
	if (!TestTrue(TEXT("Clicking the first tile completes"), TileElement(Rig, *Tiles, 0)->Click())
		|| !TestEqual(TEXT("and selects it"), Tiles->GetSelectedIndex(), 0))
	{
		return false;
	}

	// STileView::OnNavigation: Right is the next tile inside the line; Down falls through to
	// SListView::OnNavigation, which steps a whole line (NumItemsPerLine). Both land through
	// NavigationSelect, which selects while bSelectItemOnNavigation is on -- as it is by default.
	TestTrue(TEXT("Navigating right completes"), NavigateOnce(Rig, EDreamUINavigationDirection::Right));
	TestEqual(TEXT("Right selects the second tile"), Tiles->GetSelectedIndex(), 1);
	TestTrue(TEXT("Navigating down completes"), NavigateOnce(Rig, EDreamUINavigationDirection::Down));
	TestEqual(TEXT("Down selects the tile one line below it, the sixth"), Tiles->GetSelectedIndex(), 5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamListsTileViewNavigateFocusTest,
	"DreamGUI.TileView.NavigatingRightThenDownMovesFocusOneTileThenOneLine",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamListsTileViewNavigateFocusTest, "DreamGUI.TileView.NavigatingRightThenDownMovesFocusOneTileThenOneLine", "[Pointer][Nav][Animated]")

bool FDreamListsTileViewNavigateFocusTest::RunTest(const FString& Parameters)
{
	using namespace DreamTileViewInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(DreamListsInteraction::ViewportSize());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	TArray<UObject*> Items;
	UDreamTileView* Tiles = MakeTiles(Rig, 40, Items);
	if (!TestNotNull(TEXT("The tile view was made on the rig"), Tiles)
		|| !TestEqual(TEXT("Four tiles fit across"), Tiles->GetColumnCount(), 4))
	{
		return false;
	}
	if (!TestTrue(TEXT("Clicking the first tile completes"), TileElement(Rig, *Tiles, 0)->Click()))
	{
		return false;
	}

	// The same two presses as the selection test next door, asked about FOCUS instead: where the
	// navigation highlight lands. The tile view answers the press by index and names the row that
	// shows its answer, and the pipeline focuses that row -- so focus and selection cannot disagree
	// about which tile the press reached: the one to the right, then the one straight below, and not
	// a diagonal neighbour or the scroll bar.
	TestTrue(TEXT("Navigating right completes"), NavigateOnce(Rig, EDreamUINavigationDirection::Right));
	TestTrue(TEXT("Focus is on the second tile"), TileElement(Rig, *Tiles, 1)->IsSelected());
	TestTrue(TEXT("Navigating down completes"), NavigateOnce(Rig, EDreamUINavigationDirection::Down));
	TestTrue(TEXT("Focus is on the sixth tile, one line below"), TileElement(Rig, *Tiles, 5)->IsSelected());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamListsTileViewGutterReflowTest,
	"DreamGUI.TileView.WhenTheBarComesOutAndATileNoLongerFitsTheLastLineCanStillBeScrolledTo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamListsTileViewGutterReflowTest, "DreamGUI.TileView.WhenTheBarComesOutAndATileNoLongerFitsTheLastLineCanStillBeScrolledTo", "[Pointer][Animated]")

/*
 * A tile view measured its scrolled column with the columns that fit BEFORE the scroll bar's gutter was
 * cut, then placed its tiles with the columns that fit after. Four hundred wide, tiles a hundred square:
 * twelve tiles are three lines that exactly fill a window three hundred tall, so the bar stays away and
 * four fit across. The thirteenth makes four lines, the bar comes out, ten units go to its gutter -- and
 * three fit across now, so the tiles take five lines while the column was measured for four. The last
 * tile sat a line below the end of the scroll range, where nothing could reach it.
 *
 * Checked here: after the thirteenth tile the column is as tall as the lines the tiles actually make,
 * and wheeling to the end brings the last tile wholly into the window.
 */
bool FDreamListsTileViewGutterReflowTest::RunTest(const FString& Parameters)
{
	FDreamDriverRig Rig = FDreamDriverRig::Headless(DreamListsInteraction::ViewportSize());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	constexpr float TileSize = 100.0f;
	UDreamTileView* Tiles = Rig.MakeControl<UDreamTileView>(TEXT("Tiles"), nullptr, FVector2D(400.0, 300.0));
	if (!TestNotNull(TEXT("The tile view was made on the rig"), Tiles))
	{
		return false;
	}
	Tiles->SetStyleSource(EDreamUIStyleSource::Inline);
	FDreamTileViewStyle TileStyle = Tiles->GetStyle();
	TileStyle.List = DreamListsInteraction::WithRows(TileStyle.List, TileSize);
	TileStyle.List.Bar.Thickness = 10.0f;
	TileStyle.TileWidth = TileSize;
	TileStyle.TileSpacing = 0.0f;
	Tiles->SetStyle(TileStyle);
	Tiles->SetItemObjects(DreamListsInteraction::MakeItems(12));
	Rig.PumpFrames(2);
	if (!TestNotNull(TEXT("The tile view has a bar"), Tiles->ScrollBarNode.Get())
		|| !TestEqual(TEXT("Twelve tiles fit four across"), Tiles->GetColumnCount(), 4)
		|| !TestFalse(TEXT("in three lines that fill the window, so the bar stays away"), Tiles->ScrollBarNode->GetWidgetActive()))
	{
		return false;
	}

	Tiles->AddItem(DreamListsInteraction::MakeItems(1)[0]);
	Rig.PumpFrames(2);
	TestTrue(TEXT("The thirteenth tile brought the bar out"), Tiles->ScrollBarNode->GetWidgetActive());
	TestEqual(TEXT("and its gutter leaves room for three across"), Tiles->GetColumnCount(), 3);
	TestEqual(TEXT("The scrolled column is as tall as the lines the tiles make"),
		Tiles->ColumnNode->GetHeight(), Tiles->GetRowLineCount() * TileSize);

	// Further than the tiles go, so it comes to rest at the end.
	TestTrue(TEXT("Wheeling to the end completes"), Rig.Driver()->Find(FDreamBy::Widget(Tiles))->ScrollBy(FVector2D(-10.0, -10.0)));
	Rig.PumpFrames(1);
	const TOptional<FBox2D> WindowRect = Rig.Driver()->Find(FDreamBy::Widget(Tiles->ViewportNode.Get()))->GetPixelRect();
	const TOptional<FBox2D> LastTileRect = DreamTileViewInteractionTestLocal::TileElement(Rig, *Tiles, 12)->GetPixelRect();
	if (TestTrue(TEXT("The window and the last tile both project to pixels"), WindowRect.IsSet() && LastTileRect.IsSet()))
	{
		TestTrue(FString::Printf(TEXT("The last tile ends inside the window (%.1f against %.1f)"), LastTileRect->Max.Y, WindowRect->Max.Y),
			LastTileRect->Max.Y <= WindowRect->Max.Y + 1.5 && LastTileRect->Min.Y >= WindowRect->Min.Y - 1.5);
	}
	return true;
}

#endif
