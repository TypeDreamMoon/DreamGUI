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
#include "Interaction/DreamRecyclingListReentryTestTypes.h"
#include "UObject/GarbageCollection.h"
#include "UObject/StrongObjectPtr.h"

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
	TListView* MakeRecyclingList(FDreamDriverRig& InRig, bool bInHorizontal, UDreamWidget*& OutHost, float InCellExtent = CellExtent,
		TSubclassOf<UUIListEntry> InEntryClass = UUIListEntry::StaticClass())
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
		Cell->AddComponent(InEntryClass);
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

namespace DreamRecyclingListReentryTestLocal
{
	enum class EMutation : uint8 { Clear, Replace, Recreate, ChangeSource };

	bool RunCase(FAutomationTestBase& InTest, EDreamRecyclingCallback InCallback, EMutation InMutation, bool bInScroll, bool bInHorizontal)
	{
		using namespace DreamRecyclingListInteractionTestLocal;
		FDreamDriverRig Rig = FDreamDriverRig::Headless(DreamListsInteraction::ViewportSize());
		Rig.BindTest(&InTest);
		if (!InTest.TestTrue(TEXT("The rig came up"), Rig.IsUsable())
			|| !InTest.TestTrue(TEXT("Its UI has begun play"), DreamDragInteraction::BeginPlayForUI(Rig.GetWorld())))
		{
			return false;
		}
		UDreamWidget* Host = nullptr;
		UDreamRecyclingReentryList* List = MakeRecyclingList<UDreamRecyclingReentryList>(Rig, bInHorizontal, Host,
			CellExtent, UDreamRecyclingReentryEntry::StaticClass());
		if (!InTest.TestNotNull(TEXT("The recycling list was made"), List))return false;
		TStrongObjectPtr<UDreamRecyclingReentryProbe> Probe(NewObject<UDreamRecyclingReentryProbe>());
		List->BindProbe(Probe.Get());
		UDreamRecyclingReentryEntry* Template = List->GetCellTemplate()->GetComponent<UDreamRecyclingReentryEntry>();
		if (!InTest.TestNotNull(TEXT("Its template carries the callback entry"), Template))return false;
		Template->Probe = Probe.Get();
		const TArray<UObject*> OriginalItems = DreamListsInteraction::MakeItems(100);
		const TArray<UObject*> ReplacementItems = DreamListsInteraction::MakeItems(3);
		TStrongObjectPtr<UDreamRecyclingReplacementSource> ReplacementSource(NewObject<UDreamRecyclingReplacementSource>());
		const TArray<UObject*> SourceItems = DreamListsInteraction::MakeItems(12);
		for (UObject* Item : SourceItems)ReplacementSource->Items.Add(Item);
		if (bInScroll || InCallback == EDreamRecyclingCallback::Activated)
		{
			List->SetListItems(OriginalItems);
			Rig.PumpFrames(1);
			if (InCallback == EDreamRecyclingCallback::Activated)
			{
				if (!InTest.TestTrue(TEXT("An existing row can be reactivated"), List->GetCacheCellList().Num() > 0))return false;
				List->GetCacheCellList()[0].Widget->SetWidgetActive(false);
			}
		}
		Probe->Trigger = InCallback;
		Probe->Action = [List, InMutation, ReplacementItems, Source = ReplacementSource.Get()]()
		{
			switch (InMutation)
			{
			case EMutation::Clear: List->ClearListItems(); break;
			case EMutation::Replace: List->SetListItems(ReplacementItems); break;
			case EMutation::Recreate: List->RecreateList(); break;
			case EMutation::ChangeSource:
			{
				TScriptInterface<IUIRecyclableScrollViewDataSource> NewSource;
				NewSource.SetObject(Source);
				NewSource.SetInterface(Cast<IUIRecyclableScrollViewDataSource>(Source));
				List->SetDataSource(NewSource);
				break;
			}
			}
		};
		if (bInScroll)
		{
			List->SetScrollProgress(bInHorizontal ? FVector2D(0.5, 0.0) : FVector2D(0.0, 0.5));
		}
		else
		{
			List->SetListItems(OriginalItems);
		}
		Rig.PumpFrames(1);
		InTest.TestEqual(FString::Printf(TEXT("The callback made its mutation once (callback=%d, mutation=%d, %s, %s)"),
			static_cast<int32>(InCallback), static_cast<int32>(InMutation),
			bInHorizontal ? TEXT("horizontal") : TEXT("vertical"), bInScroll ? TEXT("scroll") : TEXT("initialize")), Probe->MutationCount, 1);
		InTest.TestFalse(TEXT("The measuring template is asleep after a callback stops its pass"), List->GetCellTemplate()->GetWidgetActive());
		const TArray<UObject*> ExpectedItems = InMutation == EMutation::Clear ? TArray<UObject*>()
			: (InMutation == EMutation::Replace ? ReplacementItems
				: (InMutation == EMutation::ChangeSource ? SourceItems : OriginalItems));
		// A vertical view keeps five lines; the horizontal one keeps four with this exact-size viewport.
		const int32 ExpectedPoolSize = FMath::Min(ExpectedItems.Num(), bInHorizontal ? 4 : 5);
		InTest.TestEqual(TEXT("The final pool belongs to the callback's result"), List->GetCacheCellList().Num(), ExpectedPoolSize);
		if (InMutation == EMutation::ChangeSource)
		{
			InTest.TestEqual(TEXT("The replacement data-source object stays installed"), List->GetDataSource().GetObject(), static_cast<UObject*>(ReplacementSource.Get()));
		}
		if (ExpectedItems.IsEmpty())return true;
		InTest.TestNearlyEqual(TEXT("A callback rebuild starts at the new source's first item"),
			static_cast<float>(bInHorizontal ? List->GetScrollOffset().X : List->GetScrollOffset().Y), 0.0f, 0.5f);
		for (const FUIRecyclableScrollViewCellContainer& Cell : List->GetCacheCellList())
		{
			const UUIListEntry* Entry = EntryOf(Cell);
			if (InTest.TestNotNull(TEXT("Each final cell has an entry"), Entry)
				&& InTest.TestTrue(TEXT("Each final entry indexes the final source"), ExpectedItems.IsValidIndex(Entry->GetItemIndex()))
				&& InTest.TestNotNull(TEXT("Each final cell has a widget"), Cell.Widget.Get()))
			{
				InTest.TestEqual(TEXT("The old pass never overwrites the callback's item"), Entry->GetItem(), ExpectedItems[Entry->GetItemIndex()]);
				InTest.TestTrue(TEXT("Every final row is awake"), Cell.Widget->GetWidgetActive());
				const double ExpectedPosition = bInHorizontal
					? (Entry->GetItemIndex() + Cell.Widget->GetPivot().X) * CellExtent
					: -(Entry->GetItemIndex() + 1.0 - Cell.Widget->GetPivot().Y) * CellExtent;
				InTest.TestNearlyEqual(TEXT("The old pass never overwrites the callback's row position"),
					static_cast<float>(bInHorizontal ? Cell.Widget->GetAnchoredPosition().X : Cell.Widget->GetAnchoredPosition().Y),
					static_cast<float>(ExpectedPosition), 0.5f);
			}
		}
		return true;
	}

	bool RunMutations(FAutomationTestBase& InTest, EDreamRecyclingCallback InCallback, EMutation InMutation)
	{
		bool bSucceeded = true;
		for (bool bScroll : { false, true })
		{
			for (bool bHorizontal : { false, true })
			{
				bSucceeded = RunCase(InTest, InCallback, InMutation, bScroll, bHorizontal) && bSucceeded;
			}
		}
		return bSucceeded;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRecyclingListAssignedReentryTest,
	"DreamGUI.ListView.AnEntryAssignedCallbackCanClearOrReplaceItemsDuringInitializationAndRecycling",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamRecyclingListAssignedReentryTest, "DreamGUI.ListView.AnEntryAssignedCallbackCanClearOrReplaceItemsDuringInitializationAndRecycling", "[Pointer][Animated]")

bool FDreamRecyclingListAssignedReentryTest::RunTest(const FString& Parameters)
{
	using namespace DreamRecyclingListReentryTestLocal;
	const bool bCleared = RunMutations(*this, EDreamRecyclingCallback::Assigned, EMutation::Clear);
	return RunMutations(*this, EDreamRecyclingCallback::Assigned, EMutation::Replace) && bCleared;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRecyclingListGeneratedReentryTest,
	"DreamGUI.ListView.AnEntryGeneratedCallbackCanClearOrReplaceItemsDuringInitializationAndRecycling",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamRecyclingListGeneratedReentryTest, "DreamGUI.ListView.AnEntryGeneratedCallbackCanClearOrReplaceItemsDuringInitializationAndRecycling", "[Pointer][Animated]")

bool FDreamRecyclingListGeneratedReentryTest::RunTest(const FString& Parameters)
{
	using namespace DreamRecyclingListReentryTestLocal;
	const bool bCleared = RunMutations(*this, EDreamRecyclingCallback::Generated, EMutation::Clear);
	return RunMutations(*this, EDreamRecyclingCallback::Generated, EMutation::Replace) && bCleared;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRecyclingListSourceReentryTest,
	"DreamGUI.ListView.ABindingCallbackCanReplaceTheRecyclingListsDataSource",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamRecyclingListSourceReentryTest, "DreamGUI.ListView.ABindingCallbackCanReplaceTheRecyclingListsDataSource", "[Pointer][Animated]")

bool FDreamRecyclingListSourceReentryTest::RunTest(const FString& Parameters)
{
	using namespace DreamRecyclingListReentryTestLocal;
	const bool bAssigned = RunMutations(*this, EDreamRecyclingCallback::Assigned, EMutation::ChangeSource);
	return RunMutations(*this, EDreamRecyclingCallback::Generated, EMutation::ChangeSource) && bAssigned;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRecyclingListBracketReentryTest,
	"DreamGUI.ListView.ABeforeOrAfterCellCallbackCanRecreateTheRecyclingList",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamRecyclingListBracketReentryTest, "DreamGUI.ListView.ABeforeOrAfterCellCallbackCanRecreateTheRecyclingList", "[Pointer][Animated]")

bool FDreamRecyclingListBracketReentryTest::RunTest(const FString& Parameters)
{
	using namespace DreamRecyclingListReentryTestLocal;
	const bool bBefore = RunMutations(*this, EDreamRecyclingCallback::Before, EMutation::Recreate);
	return RunMutations(*this, EDreamRecyclingCallback::After, EMutation::Recreate) && bBefore;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRecyclingListActiveReentryTest,
	"DreamGUI.ListView.AReactivatedCellCanClearOrReplaceItemsDuringInitializationAndRecycling",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamRecyclingListActiveReentryTest, "DreamGUI.ListView.AReactivatedCellCanClearOrReplaceItemsDuringInitializationAndRecycling", "[Pointer][Animated]")

bool FDreamRecyclingListActiveReentryTest::RunTest(const FString& Parameters)
{
	using namespace DreamRecyclingListReentryTestLocal;
	const bool bCleared = RunMutations(*this, EDreamRecyclingCallback::Activated, EMutation::Clear);
	return RunMutations(*this, EDreamRecyclingCallback::Activated, EMutation::Replace) && bCleared;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRecyclingListCreateReentryTest,
	"DreamGUI.ListView.ANewCellsCreationCallbackCanClearOrReplaceTheListsItems",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamRecyclingListCreateReentryTest, "DreamGUI.ListView.ANewCellsCreationCallbackCanClearOrReplaceTheListsItems", "[Pointer][Animated]")

bool FDreamRecyclingListCreateReentryTest::RunTest(const FString& Parameters)
{
	using namespace DreamRecyclingListReentryTestLocal;
	bool bSucceeded = true;
	for (bool bHorizontal : { false, true })
	{
		bSucceeded = RunCase(*this, EDreamRecyclingCallback::Created, EMutation::Clear, false, bHorizontal) && bSucceeded;
		bSucceeded = RunCase(*this, EDreamRecyclingCallback::Created, EMutation::Replace, false, bHorizontal) && bSucceeded;
	}
	return bSucceeded;
}

namespace DreamRecyclingListCellDestructionTestLocal
{
	enum class EPass : uint8 { Initialize, Scroll, Update };

	bool TestPool(FAutomationTestBase& InTest, UUIRecyclableScrollView& InList, const TArray<UObject*>& InItems, bool bInHorizontal)
	{
		using namespace DreamRecyclingListInteractionTestLocal;
		bool bValid = InTest.TestEqual(TEXT("the recovered pool has every visible row"), InList.GetCacheCellList().Num(),
			FMath::Min(InItems.Num(), bInHorizontal ? 4 : 5));
		TSet<int32> SeenIndices;
		for (const FUIRecyclableScrollViewCellContainer& Cell : InList.GetCacheCellList())
		{
			if (!InTest.TestTrue(TEXT("each recovered row has a live widget and component"),
				IsValid(Cell.Widget) && IsValid(Cell.CellComponent)))
			{
				bValid = false;
				continue;
			}
			const UUIListEntry* Entry = EntryOf(Cell);
			if (!InTest.TestNotNull(TEXT("each recovered row has an entry"), Entry)
				|| !InTest.TestTrue(TEXT("each recovered entry belongs to the current source"), InItems.IsValidIndex(Entry->GetItemIndex())))
			{
				bValid = false;
				continue;
			}
			bValid = InTest.TestEqual(TEXT("each recovered row shows the current item"), Entry->GetItem(), InItems[Entry->GetItemIndex()]) && bValid;
			bValid = InTest.TestFalse(TEXT("no item occupies two recovered rows"), SeenIndices.Contains(Entry->GetItemIndex())) && bValid;
			SeenIndices.Add(Entry->GetItemIndex());
			bValid = InTest.TestTrue(TEXT("each recovered row is awake"), Cell.Widget->GetWidgetActive()) && bValid;
			const double ExpectedPosition = bInHorizontal
				? (Entry->GetItemIndex() + Cell.Widget->GetPivot().X) * CellExtent
				: -(Entry->GetItemIndex() + 1.0 - Cell.Widget->GetPivot().Y) * CellExtent;
			bValid = InTest.TestNearlyEqual(TEXT("each recovered row sits on its own item line"),
				static_cast<float>(bInHorizontal ? Cell.Widget->GetAnchoredPosition().X : Cell.Widget->GetAnchoredPosition().Y),
				static_cast<float>(ExpectedPosition), 0.5f) && bValid;
		}
		return bValid;
	}

	bool RunCase(FAutomationTestBase& InTest, EDreamRecyclingCallback InCallback, EPass InPass, bool bInHorizontal)
	{
		using namespace DreamRecyclingListInteractionTestLocal;
		FDreamDriverRig Rig = FDreamDriverRig::Headless(DreamListsInteraction::ViewportSize());
		Rig.BindTest(&InTest);
		if (!InTest.TestTrue(TEXT("the cell-destruction rig came up"), Rig.IsUsable())
			|| !InTest.TestTrue(TEXT("the cell-destruction UI began play"), DreamDragInteraction::BeginPlayForUI(Rig.GetWorld())))return false;
		UDreamWidget* Host = nullptr;
		UDreamRecyclingReentryList* List = MakeRecyclingList<UDreamRecyclingReentryList>(Rig, bInHorizontal, Host,
			CellExtent, UDreamRecyclingReentryEntry::StaticClass());
		if (!InTest.TestNotNull(TEXT("the cell-destruction list was made"), List))return false;
		TStrongObjectPtr<UDreamRecyclingReentryProbe> Probe(NewObject<UDreamRecyclingReentryProbe>());
		List->BindProbe(Probe.Get());
		const TArray<UObject*> OriginalItems = DreamListsInteraction::MakeItems(100);
		TStrongObjectPtr<UDreamRecyclingReplacementSource> ReplacementSource(NewObject<UDreamRecyclingReplacementSource>());
		const TArray<UObject*> SourceItems = DreamListsInteraction::MakeItems(12);
		for (UObject* Item : SourceItems)ReplacementSource->Items.Add(Item);
		if (InPass != EPass::Initialize)
		{
			List->SetListItems(OriginalItems);
			Rig.PumpFrames(1);
		}
		TWeakObjectPtr<UDreamWidget> DestroyedCell;
		Probe->Trigger = InCallback;
		Probe->Action = [&InTest, List, Probe = Probe.Get(), InCallback, &DestroyedCell]()
		{
			UDreamWidget* Widget = InCallback == EDreamRecyclingCallback::Before
				? (List->GetCacheCellList().IsEmpty() ? nullptr : List->GetCacheCellList().Last().Widget.Get())
				: Probe->CallbackWidget.Get();
			if (InTest.TestTrue(TEXT("the callback destroys a real live cell"), IsValid(Widget)))
			{
				DestroyedCell = Widget;
				Widget->DestroyWidget();
			}
		};
		if (InPass == EPass::Initialize)List->SetListItems(OriginalItems);
		else if (InPass == EPass::Scroll)List->SetScrollProgress(bInHorizontal ? FVector2D(0.5, 0.0) : FVector2D(0.0, 0.5));
		else List->UpdateCellData();
		InTest.TestEqual(FString::Printf(TEXT("one cell was destroyed by callback=%d, pass=%d, horizontal=%d"),
			static_cast<int32>(InCallback), static_cast<int32>(InPass), bInHorizontal), Probe->MutationCount, 1);
		InTest.TestFalse(TEXT("the callback's cell is destroyed"), DestroyedCell.IsValid());
		InTest.TestFalse(TEXT("an aborted layout leaves the measuring template inactive"), List->GetCellTemplate()->GetWidgetActive());
		for (const FUIRecyclableScrollViewCellContainer& Cell : List->GetCacheCellList())
		{
			InTest.TestTrue(TEXT("the callback leaves no dead cell in the pool"), IsValid(Cell.Widget) && IsValid(Cell.CellComponent));
		}
		CollectGarbage(RF_NoFlags);
		// Scroll can recover before the pending rebuild's next tick; an Update callback also
		// exercises recovery with no further input at all.
		if (InPass != EPass::Update)List->SetScrollProgress(bInHorizontal ? FVector2D(0.25, 0.0) : FVector2D(0.0, 0.25));
		Rig.PumpFrames(2);
		bool bValid = TestPool(InTest, *List, OriginalItems, bInHorizontal);
		List->SetScrollProgress(bInHorizontal ? FVector2D(0.6, 0.0) : FVector2D(0.0, 0.6));
		Rig.PumpFrames(1);
		bValid = TestPool(InTest, *List, OriginalItems, bInHorizontal) && bValid;

		// Also destroy a row outside a list callback. GC clears its reflected pool pointers before
		// RecreateList and source replacement must detect it and restore a complete, correctly indexed pool.
		auto DestroyAndCollect = [&InTest, List]()
		{
			if (!InTest.TestTrue(TEXT("a live row is available for external destruction"), !List->GetCacheCellList().IsEmpty()))return;
			UDreamWidget* Widget = List->GetCacheCellList()[0].Widget.Get();
			if (!InTest.TestTrue(TEXT("the externally destroyed row is live"), IsValid(Widget)))return;
			Widget->DestroyWidget();
			CollectGarbage(RF_NoFlags);
		};
		DestroyAndCollect();
		List->SetScrollProgress(bInHorizontal ? FVector2D(0.4, 0.0) : FVector2D(0.0, 0.4));
		Rig.PumpFrames(2);
		bValid = TestPool(InTest, *List, OriginalItems, bInHorizontal) && bValid;
		DestroyAndCollect();
		List->RecreateList();
		Rig.PumpFrames(2);
		bValid = TestPool(InTest, *List, OriginalItems, bInHorizontal) && bValid;
		DestroyAndCollect();
		TScriptInterface<IUIRecyclableScrollViewDataSource> NewSource;
		NewSource.SetObject(ReplacementSource.Get());
		NewSource.SetInterface(Cast<IUIRecyclableScrollViewDataSource>(ReplacementSource.Get()));
		List->SetDataSource(NewSource);
		Rig.PumpFrames(2);
		InTest.TestEqual(TEXT("the replacement source stays installed after recovery"), List->GetDataSource().GetObject(),
			static_cast<UObject*>(ReplacementSource.Get()));
		return TestPool(InTest, *List, SourceItems, bInHorizontal) && bValid;
	}

	bool RunPasses(FAutomationTestBase& InTest, EDreamRecyclingCallback InCallback)
	{
		bool bValid = true;
		for (bool bHorizontal : {false, true})
		{
			for (EPass Pass : {EPass::Initialize, EPass::Scroll, EPass::Update})
			{
				if (InCallback == EDreamRecyclingCallback::Created && Pass != EPass::Initialize)continue;
				bValid = RunCase(InTest, InCallback, Pass, bHorizontal) && bValid;
			}
		}
		return bValid;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRecyclingListCellDestroyedInInitTest,
	"DreamGUI.ListView.ACellDestroyedInInitOnCreateCanBeCollectedAndThePoolRecovers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamRecyclingListCellDestroyedInInitTest, "DreamGUI.ListView.ACellDestroyedInInitOnCreateCanBeCollectedAndThePoolRecovers", "[Pointer][Animated]")

bool FDreamRecyclingListCellDestroyedInInitTest::RunTest(const FString& Parameters)
{
	return DreamRecyclingListCellDestructionTestLocal::RunPasses(*this, EDreamRecyclingCallback::Created);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRecyclingListCellDestroyedBeforeSetTest,
	"DreamGUI.ListView.ACellDestroyedInBeforeSetCellCanBeCollectedAndThePoolRecovers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamRecyclingListCellDestroyedBeforeSetTest, "DreamGUI.ListView.ACellDestroyedInBeforeSetCellCanBeCollectedAndThePoolRecovers", "[Pointer][Animated]")

bool FDreamRecyclingListCellDestroyedBeforeSetTest::RunTest(const FString& Parameters)
{
	return DreamRecyclingListCellDestructionTestLocal::RunPasses(*this, EDreamRecyclingCallback::Before);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRecyclingListCellDestroyedInSetTest,
	"DreamGUI.ListView.ACellDestroyedInSetCellCanBeCollectedAndThePoolRecovers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamRecyclingListCellDestroyedInSetTest, "DreamGUI.ListView.ACellDestroyedInSetCellCanBeCollectedAndThePoolRecovers", "[Pointer][Animated]")

bool FDreamRecyclingListCellDestroyedInSetTest::RunTest(const FString& Parameters)
{
	return DreamRecyclingListCellDestructionTestLocal::RunPasses(*this, EDreamRecyclingCallback::Set);
}
#endif
