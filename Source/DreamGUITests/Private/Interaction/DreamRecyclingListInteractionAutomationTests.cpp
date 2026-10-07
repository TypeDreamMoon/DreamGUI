// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamWidget.h"
#include "Interaction/UIListView.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Interaction/DreamDragInteractionTestTypes.h"
#include "Interaction/DreamListsInteractionTestTypes.h"

/*
 * THE RECYCLING LIST, UNDER A WHEEL.
 *
 * UUIRecyclableScrollView -- and UUIListView and UUITileView on top of it, which is where an `each`
 * block's rows live -- keeps a handful of cells and moves them round as the content scrolls: the cell
 * that leaves one end is put back at the other and told which item it shows now. Everything that goes
 * wrong with it goes wrong in that bookkeeping, so these judge the cells themselves: which item each
 * one says it shows, where it sits in the content, and whether it is awake.
 *
 * A cell shows item D when its UUIListEntry says D, and it sits where item D belongs when its top edge
 * is D lines down the content -- the layout InitializeOnDataSource writes and every recycle keeps.
 *
 * Built by hand rather than from a class: a host widget the list behaviour sits on, a content widget
 * it scrolls, and a cell template under the content carrying the entry the list fills in. Three hundred
 * units square with cells a hundred tall, so five cells cover the window: three on screen and the two
 * the recycler keeps in hand.
 */
namespace DreamRecyclingListInteractionTestLocal
{
	const FVector2D ViewSize(300.0, 300.0);
	constexpr float CellExtent = 100.0f;

	/**
	 * A recycling list of type TListView on the rig, scrolling vertically or sideways, with nothing in it
	 * yet, its cell template InCellExtent long along the scroll axis. Begun play first
	 * (DreamDragInteraction::BeginPlayForUI): the list lays its cells out in Start, and scrolls in reply
	 * to its own Tick and value events, none of which a world that never began play gives a behaviour.
	 */
	template<class TListView>
	TListView* MakeRecyclingList(FDreamDriverRig& InRig, bool bInHorizontal, UDreamWidget*& OutHost, float InCellExtent = CellExtent)
	{
		OutHost = InRig.MakeWidget(TEXT("ListHost"), nullptr, ViewSize);
		UDreamWidget* Content = OutHost != nullptr ? InRig.MakeWidget(TEXT("Content"), OutHost, ViewSize) : nullptr;
		UDreamWidget* Cell = Content != nullptr
			? InRig.MakeWidget(TEXT("Cell"), Content, bInHorizontal ? FVector2D(InCellExtent, ViewSize.Y) : FVector2D(ViewSize.X, InCellExtent))
			: nullptr;
		if (Cell == nullptr)
		{
			return nullptr;
		}
		Cell->AddComponent<UUIListEntry>();
		TListView* List = OutHost->AddComponent<TListView>();
		if (List == nullptr)
		{
			return nullptr;
		}
		// One axis exactly: the recycler lays nothing out for a view that scrolls both ways or neither.
		List->SetHorizontal(bInHorizontal);
		List->SetVertical(!bInHorizontal);
		List->SetContent(Content);
		List->SetCellTemplate(Cell);
		InRig.PumpFrames(2);
		return List;
	}

	/** The entry a cell carries -- what it says it shows. */
	const UUIListEntry* EntryOf(const FUIRecyclableScrollViewCellContainer& InCell)
	{
		return Cast<UUIListEntry>(InCell.CellComponent);
	}

	/**
	 * Every awake cell of a vertical list sits on the line of the item it shows, InColumns items to a
	 * line. Its top edge is that line's, D lines of CellExtent down from the content's top.
	 */
	void TestEveryAwakeCellSitsOnItsItemsLine(FAutomationTestBase& InTest, const UUIRecyclableScrollView& InList, int32 InColumns)
	{
		for (const FUIRecyclableScrollViewCellContainer& Cell : InList.GetCacheCellList())
		{
			const UUIListEntry* Entry = EntryOf(Cell);
			if (Cell.Widget == nullptr || Entry == nullptr || !Cell.Widget->GetWidgetActive())
			{
				continue;
			}
			const int32 Line = Entry->GetItemIndex() / FMath::Max(1, InColumns);
			const double ExpectedY = -Line * CellExtent - (1.0 - Cell.Widget->GetPivot().Y) * CellExtent;
			InTest.TestNearlyEqual(FString::Printf(TEXT("The cell showing item %d sits on line %d"), Entry->GetItemIndex(), Line),
				static_cast<float>(Cell.Widget->GetAnchoredPosition().Y), static_cast<float>(ExpectedY), 0.5f);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRecyclingListRefillTest,
	"DreamGUI.ListView.ARecyclingListShortenedWhileScrolledAndFilledAgainPutsEveryCellAtItsItem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamRecyclingListRefillTest, "DreamGUI.ListView.ARecyclingListShortenedWhileScrolledAndFilledAgainPutsEveryCellAtItsItem", "[Pointer][Animated]")

/*
 * A new source -- an `each` list whose array changed length, or SetListItems -- lays the cells out
 * again from the first item. It used to reset only the two cache cursors before scrolling the content
 * back to its start, and that scroll recycled against the OLD layout's first data index, first cell
 * position and content position. Halfway down a hundred rows and cut to three, it read cell 4 of a
 * three-cell cache and took the editor down; cut to five or more instead, it moved a cell to where the
 * old layout had been scrolled to, and the list never recycled again, leaving every row past the
 * fourth blank.
 *
 * Checked here: cutting the source to three while scrolled leaves three cells, and the hundred rows put
 * back can be wheeled through with every awake cell on its own item's line and a cell for each item on
 * screen.
 */
bool FDreamRecyclingListRefillTest::RunTest(const FString& Parameters)
{
	using namespace DreamRecyclingListInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(DreamListsInteraction::ViewportSize());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable())
		|| !TestTrue(TEXT("Its UI has begun play, as a game's has"), DreamDragInteraction::BeginPlayForUI(Rig.GetWorld())))
	{
		return false;
	}
	UDreamWidget* Host = nullptr;
	UUIListView* List = MakeRecyclingList<UUIListView>(Rig, /*bInHorizontal*/false, Host);
	if (!TestNotNull(TEXT("The recycling list was made"), List))
	{
		return false;
	}
	const TArray<UObject*> Items = DreamListsInteraction::MakeItems(100);
	List->SetListItems(Items);
	Rig.PumpFrames(1);
	if (!TestEqual(TEXT("A hundred rows keep five cells"), List->GetCacheCellList().Num(), 5))
	{
		return false;
	}

	// Halfway down, so the recycler's cursors are far from where a fresh layout starts.
	List->SetScrollProgress(FVector2D(0.0, 0.5));
	Rig.PumpFrames(1);
	List->SetListItems(TArray<UObject*>{ Items[0], Items[1], Items[2] });
	Rig.PumpFrames(1);
	TestEqual(TEXT("Three rows keep three cells"), List->GetCacheCellList().Num(), 3);
	TestEqual(TEXT("and the list is back at its start"), static_cast<float>(List->GetScrollOffset().Y), 0.0f, 0.5f);

	List->SetListItems(Items);
	Rig.PumpFrames(1);
	// Eight notches of forty: three and a fifth rows down, so the cells for the first three rows have
	// all gone round to the far end.
	TestTrue(TEXT("Turning the wheel over the list completes"),
		Rig.Driver()->Find(FDreamBy::Widget(Host))->ScrollBy(FVector2D(-8.0, -8.0)));
	Rig.PumpFrames(1);
	const float Offset = static_cast<float>(List->GetScrollOffset().Y);
	if (!TestTrue(FString::Printf(TEXT("The wheel scrolled the list past its third row (offset %.1f)"), Offset), Offset > 3.0f * CellExtent))
	{
		return false;
	}
	TestEveryAwakeCellSitsOnItsItemsLine(*this, *List, 1);
	// Every row the window shows has a cell, and that cell says it shows that row.
	const int32 FirstOnScreen = FMath::FloorToInt(Offset / CellExtent);
	const int32 LastOnScreen = FMath::FloorToInt((Offset + ViewSize.Y - 0.5f) / CellExtent);
	for (int32 Row = FirstOnScreen; Row <= LastOnScreen; ++Row)
	{
		FUIRecyclableScrollViewCellContainer Cell;
		const UUIListEntry* Entry = List->GetCellItemByDataIndex(Row, Cell) ? EntryOf(Cell) : nullptr;
		if (TestNotNull(FString::Printf(TEXT("Row %d, on screen, has a cell"), Row), Entry))
		{
			TestEqual(FString::Printf(TEXT("and that cell shows row %d"), Row), Entry->GetItemIndex(), Row);
			TestTrue(TEXT("and is awake"), Cell.Widget != nullptr && Cell.Widget->GetWidgetActive());
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRecyclingListHorizontalStartTest,
	"DreamGUI.ListView.AHorizontalRecyclingListOpensAtItsFirstItem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The cells are laid out from the first item at the content's START, so the content is put at its start
 * too. A horizontal list was sent to progress 1 instead, which the offset model reads as the far END on
 * both axes: it opened at its last column, with its cells drawn at the first, and never recycled its way
 * back. Checked here: a freshly filled horizontal list is at progress and offset zero, with the first
 * item's cell at the content's left edge.
 */
bool FDreamRecyclingListHorizontalStartTest::RunTest(const FString& Parameters)
{
	using namespace DreamRecyclingListInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(DreamListsInteraction::ViewportSize());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable())
		|| !TestTrue(TEXT("Its UI has begun play, as a game's has"), DreamDragInteraction::BeginPlayForUI(Rig.GetWorld())))
	{
		return false;
	}
	UDreamWidget* Host = nullptr;
	UUIListView* List = MakeRecyclingList<UUIListView>(Rig, /*bInHorizontal*/true, Host);
	if (!TestNotNull(TEXT("The recycling list was made"), List))
	{
		return false;
	}
	List->SetListItems(DreamListsInteraction::MakeItems(100));
	Rig.PumpFrames(1);

	TestEqual(TEXT("The list opens at progress zero along its scrolling axis"), static_cast<float>(List->GetScrollProgress().X), 0.0f, 0.001f);
	TestEqual(TEXT("which is no scroll at all"), static_cast<float>(List->GetScrollOffset().X), 0.0f, 0.5f);
	FUIRecyclableScrollViewCellContainer FirstCell;
	if (TestTrue(TEXT("The first item has a cell"), List->GetCellItemByDataIndex(0, FirstCell))
		&& TestNotNull(TEXT("with an entry"), EntryOf(FirstCell)))
	{
		TestEqual(TEXT("which shows the first item"), EntryOf(FirstCell)->GetItemIndex(), 0);
		TestNearlyEqual(TEXT("at the content's left edge"),
			static_cast<float>(FirstCell.Widget->GetAnchoredPosition().X), static_cast<float>(FirstCell.Widget->GetPivot().X * CellExtent), 0.5f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRecyclingListZeroPitchTest,
	"DreamGUI.ListView.ARecyclingListWhoseCellsTakeNoRoomLaysNothingOutAndCarriesOn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The layout counts the lines that fill the window by adding a cell and the space after it until the
 * total passes the window's length. A cell template with no length along the scroll axis and no space
 * after it added nothing, so the count went round for good and the game hung on the list's first
 * layout. That layout is refused now, with an error that says why.
 *
 * Checked here: a vertical list whose cell template has no height and no space after it, filled with
 * ten items, comes back from the fill having said why, and holds no cells.
 */
bool FDreamRecyclingListZeroPitchTest::RunTest(const FString& Parameters)
{
	using namespace DreamRecyclingListInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(DreamListsInteraction::ViewportSize());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable())
		|| !TestTrue(TEXT("Its UI has begun play, as a game's has"), DreamDragInteraction::BeginPlayForUI(Rig.GetWorld())))
	{
		return false;
	}
	// Said by every layout the list attempts -- in its Start as well as on the fill -- so at least once.
	AddExpectedErrorPlain(TEXT("a cell has to take up room"), EAutomationExpectedErrorFlags::Contains, 0);
	UDreamWidget* Host = nullptr;
	UUIListView* List = MakeRecyclingList<UUIListView>(Rig, /*bInHorizontal*/false, Host, /*InCellExtent*/0.0f);
	if (!TestNotNull(TEXT("The recycling list was made"), List))
	{
		return false;
	}
	List->SetListItems(DreamListsInteraction::MakeItems(10));
	Rig.PumpFrames(1);
	TestEqual(TEXT("A list whose cells take no room lays no cells out"), List->GetCacheCellList().Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRecyclingTileLastLineTest,
	"DreamGUI.TileView.ARecyclingGridScrolledToItsEndHidesTheCellsPastItsLastItem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamRecyclingTileLastLineTest, "DreamGUI.TileView.ARecyclingGridScrolledToItsEndHidesTheCellsPastItsLastItem", "[Pointer][Animated]")

/*
 * A grid of four columns and thirty items ends on a line of two. Recycling a line of cells onto that
 * last line hands two of them indices past the end, and those were wrapped round to the start --
 * every data index went through the infinite loop's modulo, looping or not -- so the line read 28, 29,
 * 0, 1, and the branches that hide a cell past the end could never run. Checked here: wheeled to the
 * end, exactly two cells are asleep, and every awake one sits on the line of the item it shows.
 */
bool FDreamRecyclingTileLastLineTest::RunTest(const FString& Parameters)
{
	using namespace DreamRecyclingListInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(DreamListsInteraction::ViewportSize());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable())
		|| !TestTrue(TEXT("Its UI has begun play, as a game's has"), DreamDragInteraction::BeginPlayForUI(Rig.GetWorld())))
	{
		return false;
	}
	UDreamWidget* Host = nullptr;
	UUITileView* Tiles = MakeRecyclingList<UUITileView>(Rig, /*bInHorizontal*/false, Host);
	if (!TestNotNull(TEXT("The recycling grid was made"), Tiles))
	{
		return false;
	}
	Tiles->SetListItems(DreamListsInteraction::MakeItems(30));
	Rig.PumpFrames(1);
	const int32 Columns = Tiles->GetColumns();
	if (!TestEqual(TEXT("The grid is four tiles across"), Columns, 4)
		|| !TestEqual(TEXT("and keeps five lines of cells"), Tiles->GetCacheCellList().Num(), 20))
	{
		return false;
	}

	// Twenty notches is further than the grid goes, so it comes to rest at its end.
	TestTrue(TEXT("Wheeling the grid to its end completes"),
		Rig.Driver()->Find(FDreamBy::Widget(Host))->ScrollBy(FVector2D(-20.0, -20.0)));
	Rig.PumpFrames(1);
	TestNearlyEqual(TEXT("The grid is at its end"), static_cast<float>(Tiles->GetScrollProgress().Y), 1.0f, 0.001f);

	int32 Asleep = 0;
	for (const FUIRecyclableScrollViewCellContainer& Cell : Tiles->GetCacheCellList())
	{
		if (Cell.Widget != nullptr && !Cell.Widget->GetWidgetActive())
		{
			++Asleep;
		}
	}
	TestEqual(TEXT("The two cells past the thirtieth item are asleep"), Asleep, 2);
	TestEveryAwakeCellSitsOnItsItemsLine(*this, *Tiles, Columns);
	for (int32 Item = 28; Item < 30; ++Item)
	{
		FUIRecyclableScrollViewCellContainer Cell;
		const UUIListEntry* Entry = Tiles->GetCellItemByDataIndex(Item, Cell) ? EntryOf(Cell) : nullptr;
		if (TestNotNull(FString::Printf(TEXT("Item %d, on the last line, has a cell"), Item), Entry))
		{
			TestEqual(TEXT("that shows it"), Entry->GetItemIndex(), Item);
			TestTrue(TEXT("and is awake"), Cell.Widget != nullptr && Cell.Widget->GetWidgetActive());
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRecyclingListDetachedSourceTest,
	"DreamGUI.ListView.ARecyclingListWithItsSourceDetachedCanScrollAndReconnectItsSource",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamRecyclingListDetachedSourceTest, "DreamGUI.ListView.ARecyclingListWithItsSourceDetachedCanScrollAndReconnectItsSource", "[Pointer][Animated]")

/*
 * Detaching a populated, scrolled list's source takes its cells away too. A wheel or programmatic
 * scroll with no source must not call the data-source interface on nullptr, and reattaching a valid
 * source must build and recycle the replacement rows as usual.
 */
bool FDreamRecyclingListDetachedSourceTest::RunTest(const FString& Parameters)
{
	using namespace DreamRecyclingListInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(DreamListsInteraction::ViewportSize());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable())
		|| !TestTrue(TEXT("Its UI has begun play"), DreamDragInteraction::BeginPlayForUI(Rig.GetWorld())))
	{
		return false;
	}
	UDreamWidget* Host = nullptr;
	UUIListView* List = MakeRecyclingList<UUIListView>(Rig, /*bInHorizontal*/false, Host);
	if (!TestNotNull(TEXT("The recycling list was made"), List))
	{
		return false;
	}
	List->SetListItems(DreamListsInteraction::MakeItems(100));
	Rig.PumpFrames(1);
	if (!TestEqual(TEXT("The populated list has five cells"), List->GetCacheCellList().Num(), 5))
	{
		return false;
	}
	List->SetScrollProgress(FVector2D(0.0, 0.5));
	Rig.PumpFrames(1);

	const TScriptInterface<IUIRecyclableScrollViewDataSource> Source = List->GetDataSource();
	TArray<TWeakObjectPtr<UDreamWidget>> PreviousCells;
	for (const FUIRecyclableScrollViewCellContainer& Cell : List->GetCacheCellList())
	{
		PreviousCells.Add(Cell.Widget.Get());
	}
	List->SetDataSource(TScriptInterface<IUIRecyclableScrollViewDataSource>());
	TestNull(TEXT("The source is detached"), List->GetDataSource().GetObject());
	TestEqual(TEXT("Its old cells are gone"), List->GetCacheCellList().Num(), 0);
	for (const TWeakObjectPtr<UDreamWidget>& Cell : PreviousCells)
	{
		TestFalse(TEXT("Each old row was destroyed"), Cell.IsValid());
	}
	FUIRecyclableScrollViewCellContainer DetachedCell;
	TestFalse(TEXT("No stale data index resolves to a cell"), List->GetCellItemByDataIndex(50, DetachedCell));

	// The virtual ApplyContentPositionWithProgress path still calls OnScrollCallback directly.
	List->SetScrollProgress(FVector2D(0.0, 0.75));
	TestTrue(TEXT("Turning the wheel with no source completes"),
		Rig.Driver()->Find(FDreamBy::Widget(Host))->ScrollBy(FVector2D(-2.0, -2.0)));
	Rig.PumpFrames(2);
	TestEqual(TEXT("Scrolling with no source leaves the list empty"), List->GetCacheCellList().Num(), 0);

	const TArray<UObject*> ReplacementItems = DreamListsInteraction::MakeItems(20);
	List->SetListItems(ReplacementItems);
	List->SetDataSource(Source);
	Rig.PumpFrames(1);
	if (!TestEqual(TEXT("Reattaching a source rebuilds five cells"), List->GetCacheCellList().Num(), 5))
	{
		return false;
	}
	TestNearlyEqual(TEXT("The replacement source starts at its first row"),
		static_cast<float>(List->GetScrollOffset().Y), 0.0f, 0.5f);
	TestTrue(TEXT("The replacement source can be wheeled through"),
		Rig.Driver()->Find(FDreamBy::Widget(Host))->ScrollBy(FVector2D(-8.0, -8.0)));
	Rig.PumpFrames(1);
	TestTrue(TEXT("The replacement source scrolled past its third row"), List->GetScrollOffset().Y > 3.0f * CellExtent);
	TestEveryAwakeCellSitsOnItsItemsLine(*this, *List, 1);
	for (const FUIRecyclableScrollViewCellContainer& Cell : List->GetCacheCellList())
	{
		const UUIListEntry* Entry = EntryOf(Cell);
		if (TestNotNull(TEXT("Each restored cell has an entry"), Entry)
			&& TestTrue(TEXT("Each restored entry has a valid replacement index"), ReplacementItems.IsValidIndex(Entry->GetItemIndex())))
		{
			TestEqual(TEXT("Each restored entry shows its replacement item"), Entry->GetItem(), ReplacementItems[Entry->GetItemIndex()]);
		}
	}
	return true;
}

#endif
