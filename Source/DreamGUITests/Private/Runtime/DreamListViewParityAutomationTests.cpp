// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "DreamControlTestScope.h"
#include "DreamListControlsTestTypes.h"

#include "Controls/DreamListView.h"
#include "Controls/DreamTileView.h"
#include "Controls/DreamTreeView.h"
#include "Core/Components/DreamWidget.h"
#include "Interaction/UIScrollView.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

/*
 * The list API a UMG user reaches for, and the two ways it fails silently.
 *
 * One is the same failure the scroll box has: a knob whose setter forgets to push reads back
 * correctly and does nothing, so every scrolling claim here is asserted against the behaviour's own
 * getter rather than the control's field.
 *
 * The other is particular to a list: every index in this control's API is an index into the SOURCE,
 * not into the rows on screen. A source that is edited by one array and not the other re-labels
 * every row after the hole, and reads as the list having scrambled itself -- so the source-editing
 * calls are asserted on both arrays at once.
 *
 * Headless: no world, no registration, no layout pass. Sizes are authored before Initialize.
 */
namespace DreamListViewParityTestLocal
{
	UDreamListView* MakeList(int32 InItemCount)
	{
		UDreamListView* List = NewObject<UDreamListView>(GetTransientPackage());
		// Inline rather than the sheet: a project sheet in the running editor would otherwise decide
		// what these assertions are comparing against.
		List->StyleSource = EDreamUIStyleSource::Inline;
		List->Style.RowHeight = 20.0f;
		List->Style.RowSpacing = 0.0f;
		List->SetWidth(200.0f);
		List->SetHeight(100.0f);
		for (int32 Index = 0; Index < InItemCount; ++Index)
		{
			List->Items.Add(FText::AsCultureInvariant(FString::Printf(TEXT("Row %d"), Index)));
		}
		List->Initialize();
		return List;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamListSourceEditingTest,
	"DreamGUI.ListView.EditingTheSourceMovesBothArraysOrNeither",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamListSourceEditingTest::RunTest(const FString& Parameters)
{
	using namespace DreamListViewParityTestLocal;

	TDreamTestControl<UDreamListView> List(MakeList(0));
	TStrongObjectPtr<UObject> First(NewObject<UDreamListControlsProbe>());
	TStrongObjectPtr<UObject> Second(NewObject<UDreamListControlsProbe>());

	List->AddItem(First.Get());
	List->AddItem(Second.Get());
	TestEqual(TEXT("two objects make two items"), List->GetNumItems(), 2);
	TestTrue(TEXT("and the list hands them back in order"), List->GetItemAt(0) == First.Get());
	TestTrue(TEXT("both of them"), List->GetItemAt(1) == Second.Get());
	TestEqual(TEXT("and can say where one of them sits"), List->GetIndexForItem(Second.Get()), 1);
	TestEqual(TEXT("an object it never took is nowhere"),
		List->GetIndexForItem(NewObject<UDreamListControlsProbe>()), INDEX_NONE);
	TestEqual(TEXT("the rows followed the source"), List->GetRowCount(), 2);

	List->RemoveItem(First.Get());
	TestEqual(TEXT("removing one leaves one"), List->GetNumItems(), 1);
	TestTrue(TEXT("and it is the other one, at index zero"), List->GetItemAt(0) == Second.Get());

	List->ClearListItems();
	TestEqual(TEXT("clearing empties the object source"), List->GetNumItems(), 0);
	TestEqual(TEXT("and the text source with it, which is the half that used to survive"),
		List->Items.Num(), 0);
	TestEqual(TEXT("and there are no rows left"), List->GetRowCount(), 0);

	// The parallel-array trap, stated as its own claim: a text source and an object source describe
	// the same rows, and a removal that shortened only one of them would re-label every row after it.
	TDreamTestControl<UDreamListView> Both(MakeList(3));
	Both->AddItem(First.Get());
	Both->AddItem(Second.Get());
	Both->AddItem(NewObject<UDreamListControlsProbe>());
	if (!TestEqual(TEXT("a list with three labels and three objects has three rows"), Both->GetNumItems(), 3))
	{
		return false;
	}
	Both->RemoveItemAt(0);
	TestEqual(TEXT("removing the first item shortens the object source"), Both->GetNumItems(), 2);
	TestEqual(TEXT("and the label source with it"), Both->Items.Num(), 2);
	TestEqual(TEXT("so row zero now wears what was row one's label"),
		Both->Items[0].ToString(), FString(TEXT("Row 1")));
	TestTrue(TEXT("and stands for what was row one's object"), Both->GetItemAt(0) == Second.Get());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamListNavigateToIndexTest,
	"DreamGUI.ListView.NavigatingToAnIndexSelectsItAndBringsItIntoView",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamListNavigateToIndexTest::RunTest(const FString& Parameters)
{
	using namespace DreamListViewParityTestLocal;

	// Twenty 20-tall rows in a 100-tall window: five fit, so most of the list is off screen and a
	// selection made without a scroll would be one nobody can see they made.
	TDreamTestControl<UDreamListView> List(MakeList(20));
	TestEqual(TEXT("the list took its source"), List->GetNumItems(), 20);
	// Stated as a precondition rather than assumed: every claim below is about a scroll, and a
	// viewport that resolved to nothing would make all of them fail for a reason none of them is
	// about. A stretched node answers with its resolved size even headless, which is what this is.
	if (!TestNotNull(TEXT("the list built a viewport"), List->ViewportNode.Get()) ||
		!TestTrue(TEXT("and it resolved to a real height with no layout pass"),
			List->ViewportNode->GetHeight() > 0.0f))
	{
		return false;
	}

	List->NavigateToIndex(15);
	TestEqual(TEXT("the row is selected"), List->GetSelectedIndex(), 15);
	TestEqual(TEXT("exactly one of them"), List->GetNumItemsSelected(), 1);
	TestTrue(TEXT("and the list scrolled to show it"), List->GetScrollOffset() > 0.0f);

	// Both ends, through the plain scroll calls, so the offsets the reveal is compared against are
	// known rather than whatever the reveal happened to pick.
	List->ScrollToBottom();
	const float BottomOffset = List->GetScrollOffset();
	TestTrue(TEXT("the bottom is further down than the top"), BottomOffset > 0.0f);
	List->ScrollToTop();
	TestEqual(TEXT("and the top is zero"), List->GetScrollOffset(), 0.0f);

	// Revealing without selecting, which is the other half of UMG's pair.
	List->ScrollIndexIntoView(19);
	TestTrue(TEXT("revealing the last row scrolled"), List->GetScrollOffset() > 0.0f);
	TestEqual(TEXT("and left the selection where it was"), List->GetSelectedIndex(), 15);

	// The selection as a set, written whole: a raw write onto the array used to be picked up only on
	// the next rebuild, which is the difference between a property and a setter.
	List->SetSelectionMode(EUIListSelectionMode::Multi);
	List->SetSelectedIndices({2, 7});
	TestEqual(TEXT("both rows are selected"), List->GetNumItemsSelected(), 2);
	TestTrue(TEXT("the first of them"), List->IsItemSelected(2));
	TestTrue(TEXT("and the second"), List->IsItemSelected(7));
	TestEqual(TEXT("and the anchor was re-derived from the set rather than left behind"),
		List->GetSelectedIndex(), 7);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamListScrollKnobsTest,
	"DreamGUI.ListView.EveryScrollingKnobIsPushedIntoTheBehaviourThatDecides",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamListScrollKnobsTest::RunTest(const FString& Parameters)
{
	using namespace DreamListViewParityTestLocal;

	TDreamTestControl<UDreamListView> List(MakeList(20));
	UUIScrollView* View = List->ScrollBehaviour.Get();
	if (!TestNotNull(TEXT("the list built its scroll behaviour"), View))
	{
		return false;
	}

	// Reading the control's own field back would pass for a setter that only wrote it, which is the
	// whole failure being guarded against.
	List->SetConsumeMouseWheel(EDreamScrollBoxConsumeMouseWheel::Always);
	TestEqual(TEXT("the wheel rule reached the behaviour"),
		View->GetConsumeMouseWheel(), EDreamScrollBoxConsumeMouseWheel::Always);
	List->SetAllowOverscroll(false);
	TestFalse(TEXT("and the overscroll switch"), View->GetAllowOverscroll());
	List->SetEnableTouchScrolling(false);
	TestFalse(TEXT("and the touch switch"), View->GetEnableTouchScrolling());
	List->SetEnableRightClickScrolling(false);
	TestFalse(TEXT("and the right-button switch"), View->GetAllowRightClickDragScrolling());
	List->SetScrollIntoViewDestination(EDreamUIScrollDestination::Center);
	TestEqual(TEXT("and where a revealed row ends up"),
		View->GetNavigationDestination(), EDreamUIScrollDestination::Center);

	// Configured is an ARGUMENT value -- "whatever this list is set up with" -- so storing it would
	// make the property its own answer.
	List->SetScrollIntoViewDestination(EDreamUIScrollDestination::Configured);
	TestEqual(TEXT("the placeholder destination is refused, leaving the real one"),
		List->GetScrollIntoViewDestination(), EDreamUIScrollDestination::Center);

	// The style push is the other road in, and it is the one a TEMPLATE-built list takes: the
	// behaviour is added fresh, carrying library defaults rather than authored ones.
	List->ApplyStyle();
	TestEqual(TEXT("a style push re-states the wheel rule rather than leaving the library default"),
		View->GetConsumeMouseWheel(), EDreamScrollBoxConsumeMouseWheel::Always);
	TestFalse(TEXT("and the overscroll switch"), View->GetAllowOverscroll());
	TestEqual(TEXT("and the destination"),
		View->GetNavigationDestination(), EDreamUIScrollDestination::Center);

	// Twenty 20-tall rows behind a 100-tall window: a quarter of the content is on screen.
	TestEqual(TEXT("the visible fraction is the window over the content"), List->GetViewFraction(), 0.25f);
	TestEqual(TEXT("and a list sitting in range is not overscrolled"), List->GetOverscroll(), 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamListScrollEventsTest,
	"DreamGUI.ListView.MovingTheRowsAnnouncesWhereTheyAreAndHowMuchIsShowing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamListScrollEventsTest::RunTest(const FString& Parameters)
{
	using namespace DreamListViewParityTestLocal;

	TDreamTestControl<UDreamListView> List(MakeList(20));
	TStrongObjectPtr<UDreamListControlsProbe> Probe(NewObject<UDreamListControlsProbe>());
	List->OnListViewScrolled.AddDynamic(Probe.Get(), &UDreamListControlsProbe::RecordScrollPair);
	List->OnListViewFinishedScrolling.AddDynamic(Probe.Get(), &UDreamListControlsProbe::RecordFinishedPair);

	List->SetScrollOffset(60.0f);
	if (!TestTrue(TEXT("moving the rows announced it"), Probe->ScrollValues.Num() > 0))
	{
		return false;
	}
	TestEqual(TEXT("with the offset it landed on"), Probe->ScrollValues.Last(), List->GetScrollOffset());
	TestEqual(TEXT("and how much of the list is showing, which a consumer cannot work out alone"),
		Probe->ScrollFractions.Last(), List->GetViewFraction());

	// SetScrollOffset kills the velocity, so the move lands with nothing left to carry it -- which is
	// exactly what "finished" means, and is why it is one call per gesture rather than one per frame.
	TestEqual(TEXT("and the finished half fired once for the one move that stopped"),
		Probe->FinishedValues.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTileViewEntrySizeTest,
	"DreamGUI.TileView.AnEntrySizeWrittenAtRuntimeReflowsTheTiles",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTileViewEntrySizeTest::RunTest(const FString& Parameters)
{
	TDreamTestControl<UDreamTileView> Tiles(NewObject<UDreamTileView>(GetTransientPackage()));
	Tiles->StyleSource = EDreamUIStyleSource::Inline;
	Tiles->Style.List.RowHeight = 40.0f;
	Tiles->Style.TileWidth = 50.0f;
	Tiles->Style.TileSpacing = 0.0f;
	Tiles->SetWidth(200.0f);
	Tiles->SetHeight(100.0f);
	for (int32 Index = 0; Index < 8; ++Index)
	{
		Tiles->Items.Add(FText::AsCultureInvariant(FString::Printf(TEXT("Tile %d"), Index)));
	}
	Tiles->Initialize();

	// The two numbers a tile's size IS, read off the resolved style rather than the inline field, so
	// a tile view wearing the project sheet answers with the size it is actually drawn at.
	TestEqual(TEXT("a tile is as tall as the row height, which is the same measurement"),
		Tiles->GetEntryHeight(), 40.0f);
	TestEqual(TEXT("and as wide as the tile width"), Tiles->GetEntryWidth(), 50.0f);
	const int32 ColumnsBefore = Tiles->GetColumnCount();
	if (!TestTrue(TEXT("the viewport fits more than one tile across"), ColumnsBefore > 1))
	{
		return false;
	}

	// A different width is a different column count, which is a different number of lines and a
	// different scroll range: this has to re-flow, not repaint.
	Tiles->SetEntryWidth(100.0f);
	TestEqual(TEXT("the width went into the style"), Tiles->Style.TileWidth, 100.0f);
	TestEqual(TEXT("and reads back through the control"), Tiles->GetEntryWidth(), 100.0f);
	TestTrue(TEXT("and wider tiles mean fewer of them across"), Tiles->GetColumnCount() < ColumnsBefore);

	Tiles->SetEntryHeight(25.0f);
	TestEqual(TEXT("the height went into the list half of the style, where a row's height lives"),
		Tiles->Style.List.RowHeight, 25.0f);
	TestEqual(TEXT("and reads back through the control"), Tiles->GetEntryHeight(), 25.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTreeViewCollapsedSetTest,
	"DreamGUI.TreeView.WritingTheWholeFoldStateMovesTheRowsWithIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTreeViewCollapsedSetTest::RunTest(const FString& Parameters)
{
	TDreamTestControl<UDreamTreeView> Tree(NewObject<UDreamTreeView>(GetTransientPackage()));
	Tree->StyleSource = EDreamUIStyleSource::Inline;
	Tree->SetWidth(200.0f);
	Tree->SetHeight(200.0f);
	// A root with two children, then a second root: the first root's subtree is exactly the run of
	// deeper rows after it, which is what makes collapsing a skip rather than a graph walk.
	Tree->Items = {
		FText::AsCultureInvariant(TEXT("Root")),
		FText::AsCultureInvariant(TEXT("Child A")),
		FText::AsCultureInvariant(TEXT("Child B")),
		FText::AsCultureInvariant(TEXT("Other")),
	};
	Tree->ItemDepths = {0, 1, 1, 0};
	Tree->Initialize();
	TestEqual(TEXT("everything starts expanded, so every item has a row"), Tree->GetRowCount(), 4);

	// The whole fold state at once. A raw write onto the set was picked up only on the next rebuild,
	// and a tree whose folds moved without its rows moving is a tree lying about where its items are.
	Tree->SetCollapsedItems({0});
	TestEqual(TEXT("collapsing the root skips the run of deeper rows after it"), Tree->GetRowCount(), 2);
	TestFalse(TEXT("and the root reads as collapsed"), Tree->IsItemExpanded(0));
	TestEqual(TEXT("the set reads back whole"), Tree->GetCollapsedItems().Num(), 1);

	// UMG's name for the call this control was written with, and it has to reach the same place.
	Tree->SetItemExpansion(0, true);
	TestTrue(TEXT("UMG's spelling expands it too"), Tree->IsItemExpanded(0));
	TestEqual(TEXT("and the rows came back"), Tree->GetRowCount(), 4);

	Tree->SetCollapsedItems(TSet<int32>());
	TestEqual(TEXT("an empty set is a fully expanded tree"), Tree->GetRowCount(), 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamListOrientationTest,
	"DreamGUI.ListView.TurningTheListSidewaysMovesEveryMeasurementWithIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamListOrientationTest::RunTest(const FString& Parameters)
{
	using namespace DreamListViewParityTestLocal;

	// 20 rows of 20 in a 200x100 box: five fit down the column, ten fit across the band. The two
	// orientations therefore disagree about everything, which is what makes them worth comparing.
	TDreamTestControl<UDreamListView> List(MakeList(20));
	UDreamWidget* First = List->GetRowWidget(0);
	UDreamWidget* Second = List->GetRowWidget(1);
	if (!TestNotNull(TEXT("the list realized a first row"), First) ||
		!TestNotNull(TEXT("and a second"), Second))
	{
		return false;
	}
	// Down the column: Y runs UP, so row one sits one pitch BELOW row zero, which is a smaller Y.
	TestEqual(TEXT("a vertical list stacks its rows down the Y axis"),
		Second->GetAnchoredPosition().Y - First->GetAnchoredPosition().Y, -20.0);
	TestEqual(TEXT("and leaves them all at the same X"),
		Second->GetAnchoredPosition().X, First->GetAnchoredPosition().X);
	const double VerticalEnd = List->ScrollBehaviour->GetScrollableExtent().Y;

	List->SetScrollOffset(80.0f);
	TestTrue(TEXT("and it scrolled where it was told"), List->GetScrollOffset() > 0.0f);

	List->SetOrientation(EDreamPanelOrientation::Horizontal);
	TestEqual(TEXT("the offset is zeroed on the way across"), List->GetScrollOffset(), 0.0f);
	First = List->GetRowWidget(0);
	Second = List->GetRowWidget(1);
	if (!TestNotNull(TEXT("the rows survived the turn"), First) || !TestNotNull(TEXT("both"), Second))
	{
		return false;
	}
	// Across the band: X runs right, so row one sits one pitch to the RIGHT, a LARGER X.
	TestEqual(TEXT("a horizontal list lays its rows out along X instead"),
		Second->GetAnchoredPosition().X - First->GetAnchoredPosition().X, 20.0);
	TestEqual(TEXT("and leaves them all at the same Y"),
		Second->GetAnchoredPosition().Y, First->GetAnchoredPosition().Y);
	// A row's RowHeight is its extent along the scroll axis, whichever axis that is -- which is the
	// whole claim the main/cross rewrite rests on.
	TestEqual(TEXT("the row's authored size now measures across the band"),
		static_cast<float>(First->GetSizeDelta().X), 20.0f);

	// The window is 200 wide where it was 100 tall, so there is half as far to go.
	TestTrue(TEXT("and the range is now measured on the other axis entirely"),
		!FMath::IsNearlyEqual(List->ScrollBehaviour->GetScrollableExtent().X, VerticalEnd));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTreeViewOrientationClampTest,
	"DreamGUI.TreeView.ATreeRefusesToBeTurnedSideways",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTreeViewOrientationClampTest::RunTest(const FString& Parameters)
{
	TDreamTestControl<UDreamTreeView> Tree(NewObject<UDreamTreeView>(GetTransientPackage()));
	Tree->StyleSource = EDreamUIStyleSource::Inline;
	Tree->SetWidth(200.0f);
	Tree->SetHeight(200.0f);
	Tree->Items = {FText::AsCultureInvariant(TEXT("Root"))};
	Tree->ItemDepths = {0};
	Tree->Initialize();

	// Clamped rather than silently ignored: the property and the layout must never disagree about
	// which way the control runs, and a write that "took" but did nothing is exactly that.
	Tree->SetOrientation(EDreamPanelOrientation::Horizontal);
	TestEqual(TEXT("a tree stays a column, because its indent is measured across its rows"),
		Tree->GetOrientation(), EDreamPanelOrientation::Vertical);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTileAlignmentTest,
	"DreamGUI.TileView.EachAlignmentSpendsTheLeftoverSpaceOnADifferentThing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTileAlignmentTest::RunTest(const FString& Parameters)
{
	// 200 wide, 50-wide tiles, no gap: four fit exactly, so a WIDER viewport is what creates slack.
	// 210 leaves 10 over -- small enough that every mode's answer is a distinct number.
	TDreamTestControl<UDreamTileView> Tiles(NewObject<UDreamTileView>(GetTransientPackage()));
	Tiles->StyleSource = EDreamUIStyleSource::Inline;
	Tiles->Style.List.RowHeight = 40.0f;
	Tiles->Style.TileWidth = 50.0f;
	Tiles->Style.TileSpacing = 0.0f;
	Tiles->Style.List.Bar.Thickness = 0.0f;
	Tiles->ScrollBarVisibility = EDreamScrollBoxScrollbarVisibility::Permanent;
	Tiles->SetWidth(210.0f);
	Tiles->SetHeight(100.0f);
	for (int32 Index = 0; Index < 6; ++Index)
	{
		Tiles->Items.Add(FText::AsCultureInvariant(FString::Printf(TEXT("Tile %d"), Index)));
	}
	Tiles->Initialize();
	if (!TestEqual(TEXT("four 50-wide tiles fit across a 210-wide window"), Tiles->GetColumnCount(), 4))
	{
		return false;
	}

	// One call answers all three at once, because the seven modes are seven ways of spending the SAME
	// leftover space -- asking separately is how two of them end up disagreeing about it.
	auto Layout = [&Tiles](EDreamTileAlignment InMode, int32 InTilesOnLine,
		float& OutExtent, float& OutStart, float& OutPitch)
	{
		Tiles->TileAlignment = InMode;
		OutExtent = 50.0f;
		OutStart = 0.0f;
		OutPitch = 50.0f;
		Tiles->ResolveLineLayout(InTilesOnLine, OutExtent, OutStart, OutPitch);
	};
	float Extent = 0.0f, Start = 0.0f, Pitch = 0.0f;

	Layout(EDreamTileAlignment::LeftAligned, 4, Extent, Start, Pitch);
	TestEqual(TEXT("left-aligned packs against the near edge and touches nothing else"), Start, 0.0f);
	TestEqual(TEXT("the tile keeps its size"), Extent, 50.0f);
	TestEqual(TEXT("and the pitch its authored value"), Pitch, 50.0f);

	Layout(EDreamTileAlignment::RightAligned, 4, Extent, Start, Pitch);
	TestEqual(TEXT("right-aligned pushes the whole line over by the slack"), Start, 10.0f);
	TestEqual(TEXT("without resizing anything"), Extent, 50.0f);

	Layout(EDreamTileAlignment::CenterAligned, 4, Extent, Start, Pitch);
	TestEqual(TEXT("centred splits the slack between both ends"), Start, 5.0f);

	Layout(EDreamTileAlignment::EvenlyDistributed, 4, Extent, Start, Pitch);
	TestEqual(TEXT("evenly distributed leaves the first tile flush"), Start, 0.0f);
	TestEqual(TEXT("keeps the tile's size"), Extent, 50.0f);
	// The slack goes into the three GAPS between four tiles, so the end tiles stay at both edges.
	TestEqual(TEXT("and puts the slack into the gaps, so both end tiles reach the edges"),
		Pitch, 50.0f + 10.0f / 3.0f);

	Layout(EDreamTileAlignment::EvenlySize, 4, Extent, Start, Pitch);
	TestEqual(TEXT("evenly sized grows the TILE instead of the gap"), Extent, 52.5f);
	TestEqual(TEXT("with the pitch following it"), Pitch, 52.5f);
	TestEqual(TEXT("and the line still flush"), Start, 0.0f);

	Layout(EDreamTileAlignment::EvenlyWide, 4, Extent, Start, Pitch);
	TestEqual(TEXT("evenly wide grows the CELL and leaves the tile alone"), Extent, 50.0f);
	TestEqual(TEXT("the cell being the width shared out"), Pitch, 52.5f);
	TestEqual(TEXT("with the tile floating in the middle of its cell"), Start, 1.25f);

	// The one place two modes differ, and the reason both exist: a SHORT last line. EvenlySize sizes
	// by the column count so every line matches; Fill sizes by this line's own count so the last one
	// stretches to the width on its own.
	Layout(EDreamTileAlignment::EvenlySize, 2, Extent, Start, Pitch);
	TestEqual(TEXT("a short line under EvenlySize keeps every other line's tile size"), Extent, 52.5f);
	Layout(EDreamTileAlignment::Fill, 2, Extent, Start, Pitch);
	TestEqual(TEXT("under Fill the same short line stretches to the whole width"), Extent, 105.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTileWrapNavigationTest,
	"DreamGUI.TileView.WrappingSteersOffOneLineEndOntoTheNextLineStart",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTileWrapNavigationTest::RunTest(const FString& Parameters)
{
	TDreamTestControl<UDreamTileView> Tiles(NewObject<UDreamTileView>(GetTransientPackage()));
	Tiles->StyleSource = EDreamUIStyleSource::Inline;
	Tiles->Style.List.RowHeight = 40.0f;
	Tiles->Style.TileWidth = 50.0f;
	Tiles->Style.TileSpacing = 0.0f;
	Tiles->Style.List.Bar.Thickness = 0.0f;
	Tiles->ScrollBarVisibility = EDreamScrollBoxScrollbarVisibility::Permanent;
	Tiles->SetWidth(200.0f);
	Tiles->SetHeight(100.0f);
	// Six tiles in rows of four: a full first line and a short second one, which is where every
	// interesting case lives.
	for (int32 Index = 0; Index < 6; ++Index)
	{
		Tiles->Items.Add(FText::AsCultureInvariant(FString::Printf(TEXT("Tile %d"), Index)));
	}
	Tiles->Initialize();
	if (!TestEqual(TEXT("four tiles fit across"), Tiles->GetColumnCount(), 4))
	{
		return false;
	}

	using ED = EDreamUINavigationDirection;
	// Inside a line, with no wrapping question to answer.
	TestEqual(TEXT("right steps along the line"), Tiles->GetNavigationTarget(1, ED::Right), 2);
	TestEqual(TEXT("left steps back"), Tiles->GetNavigationTarget(1, ED::Left), 0);
	// Off the end of a line, and wrapping off: nowhere to go.
	TestEqual(TEXT("with wrapping off, right off the end of a line goes nowhere"),
		Tiles->GetNavigationTarget(3, ED::Right), INDEX_NONE);
	TestEqual(TEXT("and left off the start of one goes nowhere either"),
		Tiles->GetNavigationTarget(0, ED::Left), INDEX_NONE);

	Tiles->SetWrapHorizontalNavigation(true);
	TestEqual(TEXT("with it on, right off the end lands on the next line's first tile"),
		Tiles->GetNavigationTarget(3, ED::Right), 4);
	TestEqual(TEXT("and left off the start lands on the previous line's LAST tile"),
		Tiles->GetNavigationTarget(4, ED::Left), 3);
	TestEqual(TEXT("the very first tile still has nowhere to go back to"),
		Tiles->GetNavigationTarget(0, ED::Left), INDEX_NONE);
	TestEqual(TEXT("and the very last has nowhere to go on to"),
		Tiles->GetNavigationTarget(5, ED::Right), INDEX_NONE);

	// Across the lines. Column 3 of line 0 has no tile below it -- the last line holds two -- so the
	// honest answer is that line's last tile rather than nothing.
	TestEqual(TEXT("down steps a whole line"), Tiles->GetNavigationTarget(0, ED::Down), 4);
	TestEqual(TEXT("and lands on the short line's last tile when its own column is missing"),
		Tiles->GetNavigationTarget(3, ED::Down), 5);
	TestEqual(TEXT("up off the first line goes nowhere"), Tiles->GetNavigationTarget(1, ED::Up), INDEX_NONE);
	// Source order already runs through the lines, so Next needs no wrapping rule at all.
	TestEqual(TEXT("Next is source order and crosses the seam without being asked to"),
		Tiles->GetNavigationTarget(3, ED::Next), 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamListBatchEditTest,
	"DreamGUI.ListView.ABatchEditRebuildsOnceAndKeepsBothArraysInStep",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamListBatchEditTest::RunTest(const FString& Parameters)
{
	using namespace DreamListViewParityTestLocal;

	TDreamTestControl<UDreamListView> List(MakeList(3));
	TStrongObjectPtr<UDreamListControlsProbe> Probe(NewObject<UDreamListControlsProbe>());
	TArray<UObject*> Three;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		Three.Add(NewObject<UDreamListControlsProbe>());
	}
	List->SetItemObjects(Three);
	List->OnRowsGenerated.AddDynamic(Probe.Get(), &UDreamListControlsProbe::RecordRowsGenerated);

	TArray<UObject*> Batch;
	Batch.Add(NewObject<UDreamListControlsProbe>());
	Batch.Add(NewObject<UDreamListControlsProbe>());
	List->AddItems(Batch);
	// ONE rebuild for the batch. Without the suppression each insert would lay the rows out against
	// a source the caller is still editing, and fire a release for every item that shifted along.
	TestEqual(TEXT("adding two items rebuilt the rows exactly once"), Probe->RowCounts.Num(), 1);
	TestEqual(TEXT("and the source grew by two"), List->GetNumItems(), 5);
	TestEqual(TEXT("with the label array grown to match"), List->Items.Num(), 5);

	Probe->RowCounts.Reset();
	TArray<UObject*> Inserted;
	Inserted.Add(NewObject<UDreamListControlsProbe>());
	List->AddItemsAt(Inserted, 1);
	TestEqual(TEXT("inserting rebuilt once too"), Probe->RowCounts.Num(), 1);
	TestTrue(TEXT("the object landed where it was asked to"), List->GetItemAt(1) == Inserted[0]);
	// The seam is the whole point: the label array has to grow AT THE SAME INDEX, or every row after
	// the insert wears the label of the one before it.
	TestEqual(TEXT("and the label array grew at the same index, not at the end"),
		List->Items[2].ToString(), FString(TEXT("Row 1")));
	TestTrue(TEXT("leaving the inserted row's own label blank rather than someone else's"),
		List->Items[1].IsEmpty());

	Probe->RowCounts.Reset();
	TArray<UObject*> Doomed;
	Doomed.Add(List->GetItemAt(0));
	Doomed.Add(List->GetItemAt(3));
	List->RemoveItems(Doomed);
	TestEqual(TEXT("removing two rebuilt once"), Probe->RowCounts.Num(), 1);
	TestEqual(TEXT("and took two off both arrays"), List->GetNumItems(), 4);
	TestEqual(TEXT("both of them"), List->Items.Num(), 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamListSelectableVetoTest,
	"DreamGUI.ListView.AVetoedItemIsRefusedByClickAndByNavigationAlike",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamListSelectableVetoTest::RunTest(const FString& Parameters)
{
	using namespace DreamListViewParityTestLocal;

	TDreamTestControl<UDreamListView> List(MakeList(10));
	TStrongObjectPtr<UDreamListControlsProbe> Probe(NewObject<UDreamListControlsProbe>());
	// Row 4 is the one nobody may have. Bound single-cast, because this is a QUESTION.
	Probe->VetoedIndex = 4;
	List->OnIsItemSelectableOrNavigable.BindDynamic(Probe.Get(), &UDreamListControlsProbe::AnswerSelectable);

	TestTrue(TEXT("an ordinary row is selectable"), List->IsItemSelectableOrNavigable(2));
	TestFalse(TEXT("the vetoed one is not"), List->IsItemSelectableOrNavigable(4));

	List->SetSelectedIndex(2);
	TestEqual(TEXT("selecting an allowed row works"), List->GetSelectedIndex(), 2);
	List->SetSelectedIndex(4);
	TestEqual(TEXT("selecting the vetoed row leaves the selection where it was"),
		List->GetSelectedIndex(), 2);
	// BOTH roads, or the two disagree about which rows exist and the player can navigate to a row
	// they cannot click.
	List->NavigateToIndex(4);
	TestEqual(TEXT("and navigating to it is refused the same way"), List->GetSelectedIndex(), 2);

	// Deselecting is never vetoed: a row that became unselectable while chosen has to be able to
	// stop being chosen, or the veto is a trap with no way out.
	Probe->VetoedIndex = 2;
	List->SetItemSelection(2, false, false);
	TestEqual(TEXT("a vetoed row can still be DEselected"), List->GetNumItemsSelected(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamListScrollGatesTest,
	"DreamGUI.ListView.TheTwoScrollingMasterSwitchesReachTheBehaviour",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamListScrollGatesTest::RunTest(const FString& Parameters)
{
	using namespace DreamListViewParityTestLocal;

	TDreamTestControl<UDreamListView> List(MakeList(20));
	UUIScrollView* View = List->ScrollBehaviour.Get();
	if (!TestNotNull(TEXT("the list built its behaviour"), View))
	{
		return false;
	}
	TestTrue(TEXT("both master switches start on, which is what every list already did"),
		View->GetPointerScrollingEnabled() && View->GetGamepadScrollingEnabled());

	List->SetIsPointerScrollingEnabled(false);
	TestFalse(TEXT("the pointer switch reached the behaviour"), View->GetPointerScrollingEnabled());
	List->SetIsGamepadScrollingEnabled(false);
	TestFalse(TEXT("and the gamepad one"), View->GetGamepadScrollingEnabled());

	// The style push is the road a TEMPLATE-built list takes, and it has to re-state both.
	List->ApplyStyle();
	TestFalse(TEXT("a style push keeps the pointer switch off"), View->GetPointerScrollingEnabled());
	TestFalse(TEXT("and the gamepad one"), View->GetGamepadScrollingEnabled());

	// A drag is refused outright while pointer scrolling is off -- it is the FIRST question
	// AcceptsDragGesture asks, ahead of the per-gesture switches.
	TestFalse(TEXT("and a drag with no event data is refused whatever the switches say"),
		View->AcceptsDragGesture(nullptr));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamListFixedLineOffsetTest,
	"DreamGUI.ListView.APinnedLineLandsAtItsShareOfTheWindowNotMerelyInside",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamListFixedLineOffsetTest::RunTest(const FString& Parameters)
{
	using namespace DreamListViewParityTestLocal;

	// 20-tall rows, a 100-tall window: five fit, so row 10 sits at offset 200 in the content.
	TDreamTestControl<UDreamListView> List(MakeList(30));
	if (!TestNotNull(TEXT("the list built a viewport"), List->ViewportNode.Get()) ||
		!TestTrue(TEXT("and it resolved to a real height"), List->ViewportNode->GetHeight() > 0.0f))
	{
		return false;
	}
	List->ScrollIndexIntoView(10);
	// The ordinary reveal stops the moment the row is inside: coming from above, that is the row's
	// bottom edge against the window's.
	const float PlainOffset = List->GetScrollOffset();
	TestEqual(TEXT("the ordinary reveal brings the row just inside the far edge"),
		PlainOffset, 10 * 20.0f + 20.0f - 100.0f);

	List->ScrollToTop();
	List->SetEnableFixedLineOffset(true);
	List->SetFixedLineScrollOffset(0.5f);
	List->ScrollIndexIntoView(10);
	// Pinned instead: the row's own top lands half a window in, wherever it came from.
	TestEqual(TEXT("pinned at half, the row's top sits half a window inside"),
		List->GetScrollOffset(), 10 * 20.0f - 50.0f);

	// The ends have no line to spare, so the pin is still clamped into the range.
	List->ScrollIndexIntoView(0);
	TestEqual(TEXT("and the first row cannot be pinned past the start"), List->GetScrollOffset(), 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamListDropZoneTest,
	"DreamGUI.ListView.ADropLandsOnASeamOrOnTheRowDependingOnWhereItHit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamListDropZoneTest::RunTest(const FString& Parameters)
{
	// Pure arithmetic, and the piece most worth pinning exactly: the first and last quarter of a row
	// are the seams, and the middle half is the row itself.
	TestEqual(TEXT("the near edge is the seam before the row"),
		UDreamListViewBase::ResolveDropZone(0.0f), EDreamItemDropZone::AboveItem);
	TestEqual(TEXT("just inside it, still that seam"),
		UDreamListViewBase::ResolveDropZone(0.2f), EDreamItemDropZone::AboveItem);
	TestEqual(TEXT("the middle is the row itself"),
		UDreamListViewBase::ResolveDropZone(0.5f), EDreamItemDropZone::OntoItem);
	TestEqual(TEXT("and the far quarter is the seam after it"),
		UDreamListViewBase::ResolveDropZone(0.9f), EDreamItemDropZone::BelowItem);
	// The boundary belongs to the row, not the seam: a quarter exactly is not yet past the edge.
	TestEqual(TEXT("the boundary itself belongs to the row"),
		UDreamListViewBase::ResolveDropZone(0.25f), EDreamItemDropZone::OntoItem);

	// Clamped, so a pointer that drifted outside the row still answers rather than falling through.
	TestEqual(TEXT("a point before the row reads as its leading seam"),
		UDreamListViewBase::ResolveDropZone(-3.0f), EDreamItemDropZone::AboveItem);
	TestEqual(TEXT("and one past it as the trailing seam"),
		UDreamListViewBase::ResolveDropZone(9.0f), EDreamItemDropZone::BelowItem);

	// An edge fraction of a half or more would make the two seams meet and leave OntoItem
	// unreachable, so it is clamped there -- the row keeps the boundary and stays hittable.
	TestEqual(TEXT("an edge fraction that would swallow the row leaves it its boundary"),
		UDreamListViewBase::ResolveDropZone(0.5f, 0.9f), EDreamItemDropZone::OntoItem);
	// Zero is the other end: no seams at all, every hit is the row.
	TestEqual(TEXT("and a zero edge makes every hit the row"),
		UDreamListViewBase::ResolveDropZone(0.01f, 0.0f), EDreamItemDropZone::OntoItem);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamListDragDefaultsTest,
	"DreamGUI.ListView.ListsThatDoNotDragCarryNothingThatCould",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamListDragDefaultsTest::RunTest(const FString& Parameters)
{
	using namespace DreamListViewParityTestLocal;

	TDreamTestControl<UDreamListView> List(MakeList(5));
	UDreamWidget* Row = List->GetRowWidget(0);
	if (!TestNotNull(TEXT("the list realized a row"), Row))
	{
		return false;
	}
	// "Off" is literal, and this is the claim that makes the whole family free by default: a list
	// that does not drag has no behaviour on its rows to tick, subscribe to, or change what a press
	// means.
	TestFalse(TEXT("dragging is off by default"), List->GetAllowDragging());
	TestFalse(TEXT("and dropping"), List->GetAllowDragDrop());
	TestNull(TEXT("so no row carries a drag source at all"), Row->GetComponent<UDreamListRowDragSource>());
	TestNull(TEXT("nor a drop target"), Row->GetComponent<UDreamListRowDropTarget>());
	TestFalse(TEXT("and the list is not dragging anything"), List->GetIsDraggingListItem());
	TestEqual(TEXT("with no dragged item to name"), List->GetDraggedItemIndex(), INDEX_NONE);

	// Turning it on has to reach rows that ALREADY exist, or the switch only works before the list
	// is built -- which is exactly when nobody is watching.
	List->SetAllowDragging(true);
	UDreamListRowDragSource* Source = Row->GetComponent<UDreamListRowDragSource>();
	if (!TestNotNull(TEXT("switching it on reaches rows that already exist"), Source))
	{
		return false;
	}
	TestTrue(TEXT("and the source knows whose row it is"), Source->OwningList == List.Get());
	TestEqual(TEXT("and which pool slot"), Source->PoolIndex, 0);
	// The visual falls back to the row's own class, which is the answer that needs no authoring.
	TestTrue(TEXT("with the drag visual falling back to the row's own class"),
		Source->DragVisualClass == List->RowTemplateClass);
	// The arbitration the whole family turns on: once a press becomes an item drag, it is no longer
	// the list's to scroll with.
	TestFalse(TEXT("and the gesture is NOT handed on to the scroll view underneath"),
		Source->bAllowEventBubbleUp);

	List->SetAllowDragDrop(true);
	TestNotNull(TEXT("dropping adds the target"), Row->GetComponent<UDreamListRowDropTarget>());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamListDragLifecycleTest,
	"DreamGUI.ListView.ADragThatIsCancelledLeavesNothingBehind",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamListDragLifecycleTest::RunTest(const FString& Parameters)
{
	using namespace DreamListViewParityTestLocal;

	TDreamTestControl<UDreamListView> List(MakeList(5));
	TArray<UObject*> Items;
	for (int32 Index = 0; Index < 5; ++Index)
	{
		Items.Add(NewObject<UDreamListControlsProbe>());
	}
	List->SetItemObjects(Items);
	List->SetAllowDragging(true);
	List->SetAllowDragDrop(true);

	TStrongObjectPtr<UDreamListControlsProbe> Probe(NewObject<UDreamListControlsProbe>());
	List->OnDraggingStateChanged.AddDynamic(Probe.Get(), &UDreamListControlsProbe::RecordOpenChanged);
	List->OnItemDragDetected.AddDynamic(Probe.Get(), &UDreamListControlsProbe::RecordItemDrag);
	List->OnItemDragCancelled.AddDynamic(Probe.Get(), &UDreamListControlsProbe::RecordSecondItemDrag);

	TStrongObjectPtr<UDreamDragDropOperation> Operation(NewObject<UDreamDragDropOperation>());
	// Driven through the control's own hand-off rather than a synthetic pointer: the press-and-move
	// test belongs to the library's drag source and is not this control's claim to make.
	List->HandleRowDragDetected(1, Operation.Get());
	TestTrue(TEXT("the list knows it is dragging"), List->GetIsDraggingListItem());
	TestEqual(TEXT("and which item"), List->GetDraggedItemIndex(), 1);
	// The PAYLOAD is the item, never the row widget: a recycled row stands for a different item a
	// few scrolls later, and a drop arriving after that would be about the wrong thing.
	TestTrue(TEXT("the operation carries the ITEM, not the row"), Operation->Payload == Items[1]);
	TestEqual(TEXT("and the state change was announced once"), Probe->OpenStates.Num(), 1);
	TestEqual(TEXT("as a start"), Probe->ItemIndices.Num(), 1);

	// Cancelling has to clear everything, or every later drag looks like one already in flight.
	List->CancelListViewDragDrop();
	TestFalse(TEXT("cancelling stops the drag"), List->GetIsDraggingListItem());
	TestEqual(TEXT("and forgets which item it was"), List->GetDraggedItemIndex(), INDEX_NONE);
	TestEqual(TEXT("the cancelled event fired once"), Probe->SecondItemIndices.Num(), 1);
	TestEqual(TEXT("for the item the drag started on"), Probe->SecondItemIndices[0], 1);
	TestEqual(TEXT("and the state change was announced a second time"), Probe->OpenStates.Num(), 2);
	TestFalse(TEXT("this time as a stop"), Probe->OpenStates[1]);

	// A drag that WAS taken is not a cancel, and the operation is the one thing that can say so --
	// it outlives the row, which a recycled row or a closed screen can destroy mid-flight.
	Probe->SecondItemIndices.Reset();
	List->HandleRowDragDetected(2, Operation.Get());
	Operation->bDropWasHandled = true;
	List->HandleRowDragEnded(Operation.Get());
	TestFalse(TEXT("an accepted drag also stops"), List->GetIsDraggingListItem());
	TestEqual(TEXT("but does NOT announce a cancel"), Probe->SecondItemIndices.Num(), 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
