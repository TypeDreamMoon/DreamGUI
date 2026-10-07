// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Controls/DreamTileView.h"
#include "Controls/DreamTreeView.h"
#include "Core/Components/DreamWidget.h"
#include "InputCoreTypes.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Interaction/DreamListsInteractionTestTypes.h"

/*
 * THE PAD WALKS A TILE VIEW AND A TREE PAST THE EDGE OF THEIR WINDOWS.
 *
 * SListView::OnKeyDown / NavigationSelect step the selector one line (a tile view: one tile along a line, a whole line
 * up or down) and RequestNavigateToItem scrolls the item it lands on into view before the focus goes there, so walking a
 * long tile view or tree with the D-pad never leaves the focus on something out of sight. STileView and STreeView inherit
 * both. Here the D-pad goes in through the rig's key road (DreamUIKeyRouting::RouteKey), and after every press the focus is
 * on the drawn row of the item it should be on, inside the window -- the window having scrolled to show it.
 */
namespace DreamListPadScrollInteractionTestLocal
{
	/** The D-pad pressed once, and a frame for the step and the scroll it asks for to land. */
	bool PadDown(FDreamDriverRig& InRig)
	{
		return InRig.Driver()->Sequence().Key(EKeys::Gamepad_DPad_Down).WaitFrames(1).Perform();
	}

	/** Whether item InItemIndex's row has the focus, is drawn, and lies inside InWindow. */
	bool IsFocusOnShownRow(FDreamDriverRig& InRig, UDreamListViewBase& InList, int32 InItemIndex, const FBox2D& InWindow)
	{
		UDreamWidget* Row = InList.GetRowWidget(InItemIndex);
		const TOptional<FBox2D> RowRect = Row != nullptr ? InRig.Driver()->Find(FDreamBy::Widget(Row))->GetPixelRect() : TOptional<FBox2D>();
		return Row != nullptr && Row->HasFocus() && Row->GetRenderVisibleInHierarchy() && RowRect.IsSet()
			&& RowRect->Min.Y >= InWindow.Min.Y - 1.5 && RowRect->Max.Y <= InWindow.Max.Y + 1.5;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTileViewPadScrollTest,
	"DreamGUI.TileView.TheDPadDownPastTheLastLineOnShowScrollsTheNextLineIntoViewWithTheFocusOnIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamTileViewPadScrollTest, "DreamGUI.TileView.TheDPadDownPastTheLastLineOnShowScrollsTheNextLineIntoViewWithTheFocusOnIt", "[Pointer][Nav][Animated]")

/*
 * Forty tiles four to a line in a window five lines tall: the first tile clicked, the pointer taken off the view, then the
 * D-pad down seven times. Each press is a whole line, so the focus goes to the first tile of the next line, and from the
 * sixth press on that line was out of sight until the press scrolled it in.
 */
bool FDreamTileViewPadScrollTest::RunTest(const FString& Parameters)
{
	using namespace DreamListPadScrollInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(DreamListsInteraction::ViewportSize());
	Rig.BindTest(this);
	UDreamTileView* Tiles = Rig.IsUsable() ? Rig.MakeControl<UDreamTileView>(TEXT("Tiles"), nullptr, FVector2D(360.0, 300.0)) : nullptr;
	if (!TestNotNull(TEXT("The rig and a tile view came up"), Tiles))
	{
		return false;
	}
	Tiles->SetStyleSource(EDreamUIStyleSource::Inline);
	FDreamTileViewStyle TileStyle = Tiles->GetStyle();
	TileStyle.List = DreamListsInteraction::WithRows(TileStyle.List, 60.0f);
	TileStyle.TileWidth = 80.0f;
	TileStyle.TileSpacing = 0.0f;
	Tiles->SetStyle(TileStyle);
	Tiles->SetItemObjects(DreamListsInteraction::MakeItems(40));
	Rig.PumpFrames(2);
	FDreamElementRef FirstTile = Rig.Driver()->Find(FDreamBy::Widget(Tiles->GetRowWidget(0)));
	if (!TestEqual(TEXT("Four tiles fit across"), Tiles->GetColumnCount(), 4)
		|| !TestTrue(TEXT("Clicking the first tile completes"), FirstTile->Click())
		|| !TestTrue(TEXT("...and moving the pointer off the view completes"), FirstTile->MoveBy(FVector2D(500.0, 0.0))))
	{
		return false;
	}
	const TOptional<FBox2D> Window = Rig.Driver()->Find(FDreamBy::Widget(Tiles->ViewportNode.Get()))->GetPixelRect();
	if (!TestTrue(TEXT("The view's window projects to pixels"), Window.IsSet()))
	{
		return false;
	}

	for (int32 Press = 1; Press <= 7; ++Press)
	{
		const int32 Expected = Press * 4;
		if (!TestTrue(FString::Printf(TEXT("D-pad down %d completes"), Press), PadDown(Rig))
			|| !TestTrue(FString::Printf(TEXT("After D-pad down %d the focus is on the drawn tile of item %d, inside the window"), Press, Expected),
				IsFocusOnShownRow(Rig, *Tiles, Expected, Window.GetValue())))
		{
			return false;
		}
	}
	TestTrue(TEXT("The view scrolled to show the lines the pad went to"), Tiles->GetScrollOffset() > 0.0f);
	// Gone round to another item, or still there and scrolled above the window.
	UDreamWidget* FirstTileNow = Tiles->GetRowWidget(0);
	const TOptional<FBox2D> FirstTileRect = FirstTileNow != nullptr
		? Rig.Driver()->Find(FDreamBy::Widget(FirstTileNow))->GetPixelRect()
		: TOptional<FBox2D>();
	TestTrue(TEXT("...far enough that the first line is out of sight"),
		!FirstTileRect.IsSet() || FirstTileRect->Max.Y <= Window->Min.Y + 1.5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTreeViewPadScrollTest,
	"DreamGUI.TreeView.TheDPadDownPastTheLastRowOnShowScrollsTheNextRowIntoViewWithTheFocusOnIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamTreeViewPadScrollTest, "DreamGUI.TreeView.TheDPadDownPastTheLastRowOnShowScrollsTheNextRowIntoViewWithTheFocusOnIt", "[Pointer][Nav][Animated]")

/*
 * Ten groups of three -- a parent and two children each, all open -- in a window ten rows tall: thirty rows, the first
 * clicked, the pointer taken off the tree, then the D-pad down fifteen times. STreeView walks the rows that show in the
 * order they show, so every press is the next row, and from the eleventh press on that row was below the window until the
 * press scrolled it in.
 */
bool FDreamTreeViewPadScrollTest::RunTest(const FString& Parameters)
{
	using namespace DreamListPadScrollInteractionTestLocal;
	constexpr float RowHeight = 40.0f;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(DreamListsInteraction::ViewportSize());
	Rig.BindTest(this);
	UDreamTreeView* Tree = Rig.IsUsable() ? Rig.MakeControl<UDreamTreeView>(TEXT("Tree"), nullptr, FVector2D(300.0, 400.0)) : nullptr;
	if (!TestNotNull(TEXT("The rig and a tree came up"), Tree))
	{
		return false;
	}
	Tree->SetStyleSource(EDreamUIStyleSource::Inline);
	FDreamTreeViewStyle TreeStyle = Tree->GetStyle();
	TreeStyle.List = DreamListsInteraction::WithRows(TreeStyle.List, RowHeight);
	Tree->SetStyle(TreeStyle);
	TArray<FText> Labels;
	TArray<int32> Depths;
	for (int32 Group = 0; Group < 10; ++Group)
	{
		Labels.Add(FText::AsCultureInvariant(FString::Printf(TEXT("Group %d"), Group)));
		Depths.Add(0);
		for (int32 Child = 0; Child < 2; ++Child)
		{
			Labels.Add(FText::AsCultureInvariant(FString::Printf(TEXT("Item %d.%d"), Group, Child)));
			Depths.Add(1);
		}
	}
	Tree->SetItemsWithDepths(Labels, Depths);
	Rig.PumpFrames(2);
	FDreamElementRef FirstRow = Rig.Driver()->Find(FDreamBy::Widget(Tree->GetRowWidget(0)));
	if (!TestEqual(TEXT("Every group is open, so thirty rows show"), Tree->GetRowCount(), 30)
		|| !TestTrue(TEXT("Clicking the first row completes"), FirstRow->Click())
		|| !TestTrue(TEXT("...and moving the pointer off the tree completes"), FirstRow->MoveBy(FVector2D(500.0, 0.0))))
	{
		return false;
	}
	const TOptional<FBox2D> Window = Rig.Driver()->Find(FDreamBy::Widget(Tree->ViewportNode.Get()))->GetPixelRect();
	if (!TestTrue(TEXT("The tree's window projects to pixels"), Window.IsSet()))
	{
		return false;
	}

	for (int32 Press = 1; Press <= 15; ++Press)
	{
		if (!TestTrue(FString::Printf(TEXT("D-pad down %d completes"), Press), PadDown(Rig))
			|| !TestTrue(FString::Printf(TEXT("After D-pad down %d the focus is on the drawn row of item %d, inside the window"), Press, Press),
				IsFocusOnShownRow(Rig, *Tree, Press, Window.GetValue())))
		{
			return false;
		}
	}
	TestTrue(TEXT("The tree scrolled to show the rows the pad went to"), Tree->GetScrollOffset() >= 5.0f * RowHeight - 1.0f);
	TestTrue(TEXT("...and the groups the walk went through are still open"), Tree->IsItemExpanded(0) && Tree->IsItemExpanded(3));
	return true;
}

#endif
