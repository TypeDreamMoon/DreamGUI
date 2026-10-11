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
#include "Interaction/DreamControlsWorldSpaceInteractionTestHelpers.h"
#include "Interaction/DreamListsConfigInteractionTestHelpers.h"
#include "Interaction/DreamListsInteractionTestTypes.h"

/*
 * A TILE VIEW ON A PANEL IN THE LEVEL.
 *
 * UMG's tile view is STileView, an SListView laid out in lines of tiles, and inherits its input. In a UWidgetComponent a tile
 * is hit where the interaction's ray -- the mouse's, or a finger's pointer index's -- lands on the component's window, so a
 * click or a tap chooses the tile seen under it right up to its edges; and the D-pad scrolls the line it moves to into the
 * window (SListView::NavigationSelect, RequestNavigateToItem), the window standing in the level.
 *
 * Tiles 80 units wide and 60 tall with nothing between them, four to a line in a view 360 wide.
 */
namespace DreamTileViewWorldTestLocal
{
	constexpr float TileWidth = 80.0f;
	constexpr float LineHeight = 60.0f;

	UDreamTileView* MakeTiles(FDreamDriverRig& InRig, UDreamWidget* InPanel, double InHeight, const FVector2D& InPosition, int32 InItemCount)
	{
		UDreamTileView* Tiles = InRig.MakeControl<UDreamTileView>(TEXT("Tiles"), InPanel, FVector2D(360.0, InHeight), InPosition);
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

	/** Whether player 0's focus is on item InItemIndex's tile, and that tile is seen inside InWindow, to within a pixel and a half. */
	bool IsFocusOnTileSeenInside(const FDreamDriverRig& InRig, const UDreamListViewBase& InTiles, int32 InItemIndex, const FBox2D& InWindow)
	{
		const UDreamWidget* Tile = InTiles.GetRowWidget(InItemIndex);
		const TOptional<FBox2D> Rect = DreamControlsWorldTest::SeenRect(InRig, Tile);
		return Tile != nullptr && DreamControlsConfigTest::FocusIsIn(InRig, Tile) && Tile->GetRenderVisibleInHierarchy() && Rect.IsSet()
			&& Rect->Min.Y >= InWindow.Min.Y - 1.5 && Rect->Max.Y <= InWindow.Max.Y + 1.5;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTileViewWorldPressTest,
	"DreamGUI.TileView.OnAWorldPanelAClickOrATapChoosesTheTileSeenUnderItRightUpToItsEdges",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamTileViewWorldPressTest, "DreamGUI.TileView.OnAWorldPanelAClickOrATapChoosesTheTileSeenUnderItRightUpToItsEdges", "[Pointer][Touch][World]")

/*
 * Forty tiles, four to a line, on a panel three metres ahead. As the camera sees them: a click three pixels inside the first
 * tile's right edge chooses the first tile, and one three pixels inside the second's left edge chooses the second. A tap three
 * pixels above the sixth tile's bottom edge chooses the sixth, and one three pixels below the tenth's top edge -- the tile
 * under it on the next line -- chooses the tenth.
 */
bool FDreamTileViewWorldPressTest::RunTest(const FString& Parameters)
{
	using namespace DreamTileViewWorldTestLocal;
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
	UDreamTileView* Tiles = MakeTiles(Rig, Stage.Panel, 240.0, FVector2D(-40.0, 0.0), 40);
	if (!TestTrue(TEXT("A tile view four tiles across came up on the panel"), Tiles != nullptr && Tiles->GetColumnCount() == 4))
	{
		return false;
	}
	const TOptional<FBox2D> First = SeenRect(Rig, Tiles->GetRowWidget(0));
	const TOptional<FBox2D> Second = SeenRect(Rig, Tiles->GetRowWidget(1));
	const TOptional<FBox2D> Sixth = SeenRect(Rig, Tiles->GetRowWidget(5));
	const TOptional<FBox2D> Tenth = SeenRect(Rig, Tiles->GetRowWidget(9));
	if (!TestTrue(TEXT("The camera sees the tiles aimed at"), First.IsSet() && Second.IsSet() && Sixth.IsSet() && Tenth.IsSet()))
	{
		return false;
	}
	TestTrue(FString::Printf(TEXT("A tile is seen at its size in panel units (%s)"), *Describe(Second.GetValue())),
		IsSeenAtItsSize(Second.GetValue(), Tiles->GetRowWidget(1)->GetSize()));

	TestTrue(TEXT("A click just inside the first tile's right edge completes"), ClickAt(Rig, FVector2D(First->Max.X - 3.0, First->GetCenter().Y)));
	TestEqual(TEXT("It chose the first tile"), Tiles->GetSelectedIndex(), 0);
	TestTrue(TEXT("A click just inside the second tile's left edge completes"), ClickAt(Rig, FVector2D(Second->Min.X + 3.0, Second->GetCenter().Y)));
	TestEqual(TEXT("It chose the second tile"), Tiles->GetSelectedIndex(), 1);

	TestTrue(TEXT("A tap just above the sixth tile's bottom edge completes"), TapAt(Rig, FVector2D(Sixth->GetCenter().X, Sixth->Max.Y - 3.0)));
	TestEqual(TEXT("It chose the sixth tile"), Tiles->GetSelectedIndex(), 5);
	TestTrue(TEXT("A tap just below the tenth tile's top edge completes"), TapAt(Rig, FVector2D(Tenth->GetCenter().X, Tenth->Min.Y + 3.0)));
	TestEqual(TEXT("It chose the tenth tile"), Tiles->GetSelectedIndex(), 9);
	TestEqual(TEXT("One tile is chosen"), Tiles->GetNumItemsSelected(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTileViewWorldPadTest,
	"DreamGUI.TileView.OnAWorldPanelTheDPadDownPastTheLastLineOnShowScrollsTheNextLineIntoTheWindowAsSeenWithTheRingOnIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamTileViewWorldPadTest, "DreamGUI.TileView.OnAWorldPanelTheDPadDownPastTheLastLineOnShowScrollsTheNextLineIntoTheWindowAsSeenWithTheRingOnIt", "[Nav][World]")

/*
 * A button above the first column of a tile view on the panel -- forty tiles, four to a line, in a window three lines tall --
 * the focus on the button. D-pad down enters the view at its first tile; then D-pad down six times: every press is a whole
 * line, so the focus is on the first tile of the next line, seen inside the window, the view having scrolled it in once it was
 * below. The ring sits on the last tile reached where the camera sees it.
 */
bool FDreamTileViewWorldPadTest::RunTest(const FString& Parameters)
{
	using namespace DreamTileViewWorldTestLocal;
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
	UDreamTileView* Tiles = MakeTiles(Rig, Stage.Panel, 3.0 * LineHeight, FVector2D(0.0, -40.0), 40);
	if (!TestTrue(TEXT("A tile view four tiles across came up on the panel"), Tiles != nullptr && Tiles->GetColumnCount() == 4))
	{
		return false;
	}
	const TOptional<FBox2D> Window = SeenRect(Rig, Tiles->ViewportNode.Get());
	// Over the first tile's column, so the nearest thing below the button is the first tile.
	const FVector2D FirstTileOnPanel = Tiles->GetRowWidget(0) != nullptr ? OnPanel(Stage.Panel, Tiles->GetRowWidget(0)) : FVector2D::ZeroVector;
	UDreamButton* Above = Rig.MakeControl<UDreamButton>(TEXT("Above"), Stage.Panel, FVector2D(70.0, 40.0), FVector2D(FirstTileOnPanel.X, 115.0));
	if (!TestTrue(TEXT("The camera sees the view's window, and a button above its first tile came up"),
		Window.IsSet() && Tiles->GetRowWidget(0) != nullptr && Above != nullptr && Above->FaceNode != nullptr))
	{
		return false;
	}
	Rig.PumpFrames(2);
	if (!TestTrue(TEXT("The focus goes onto the button"), PutFocusOn(Rig, Above->FaceNode.Get())))
	{
		return false;
	}
	TestTrue(TEXT("D-pad down completes"), PressKey(Rig, EKeys::Gamepad_DPad_Down));
	if (!TestTrue(TEXT("D-pad down entered the view at its first tile"), IsFocusOnTileSeenInside(Rig, *Tiles, 0, Window.GetValue())))
	{
		return false;
	}

	for (int32 Press = 1; Press <= 6; ++Press)
	{
		const int32 Expected = Press * 4;
		TestTrue(FString::Printf(TEXT("D-pad down %d completes"), Press), PressKey(Rig, EKeys::Gamepad_DPad_Down));
		DreamListsConfigTest::WaitUntilStill(Rig, *Tiles);
		if (!TestTrue(FString::Printf(TEXT("After D-pad down %d the focus is on tile %d, seen inside the window %s"), Press, Expected,
			*Describe(Window.GetValue())), IsFocusOnTileSeenInside(Rig, *Tiles, Expected, Window.GetValue())))
		{
			return false;
		}
	}
	TestTrue(FString::Printf(TEXT("The view scrolled to show the lines the pad went to (offset %.1f)"), Tiles->GetScrollOffset()),
		Tiles->GetScrollOffset() > 1.0f);
	FString Where;
	const bool bRingSits = WaitForRingOnFocusAsSeen(Rig, Where);
	TestTrue(FString::Printf(TEXT("The ring sits on the last tile reached where the camera sees it (%s)"), *Where), bRingSits);
	return true;
}

#endif
