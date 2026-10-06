// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Controls/DreamListView.h"
#include "Controls/DreamTileView.h"
#include "Controls/DreamTreeView.h"
#include "Core/Components/DreamWidget.h"
#include "Interaction/UIButton.h"
#include "Interaction/UIScrollView.h"
#include "Interaction/UISelectable.h"
#include "UObject/StrongObjectPtr.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Driver/DreamDriverUntil.h"
#include "Interaction/DreamListsInteractionTestTypes.h"

/*
 * A FINGER ON A LIST, A TILE VIEW AND A TREE.
 *
 * STableRow (Slate/Public/Widgets/Views/STableRow.h) answers a finger on its own terms. OnTouchStarted starts a
 * "selection touch" and asks for drag detection; OnTouchEnded, while that touch is still on, chooses the row -- in Multi
 * mode by adding it and never taking it away -- and reports the click. OnDragDetected, for a selection touch that turned
 * into a drag, gives the touch to the list it is in (CaptureMouse on the owner table) and calls nothing of the row's own:
 * "With touch input, dragging scrolls the list while selection requires a tap." So a tap chooses, a finger dragged along
 * the list scrolls it and chooses nothing, and a row that can be dragged and dropped is picked up by the mouse, never by
 * a finger.
 *
 * Rows forty units tall with nothing between them, so every pixel worked out below is a multiple of the pitch.
 */
namespace DreamListTouchInteractionTestLocal
{
	constexpr float RowHeight = 40.0f;
	const FVector2D ListSize(300.0, 400.0);

	/** A list of InItemCount items whose window shows ten rows at a time. */
	UDreamListView* MakeList(FDreamDriverRig& InRig, int32 InItemCount)
	{
		UDreamListView* List = InRig.MakeControl<UDreamListView>(TEXT("List"), nullptr, ListSize);
		if (List == nullptr)
		{
			return nullptr;
		}
		List->SetStyleSource(EDreamUIStyleSource::Inline);
		List->SetStyle(DreamListsInteraction::WithRows(List->GetStyle(), RowHeight));
		List->SetItemObjects(DreamListsInteraction::MakeItems(InItemCount));
		List->SetEnableTouchScrolling(true);
		InRig.PumpFrames(2);
		return List;
	}

	FDreamElementRef RowOf(FDreamDriverRig& InRig, UDreamListViewBase& InList, int32 InItemIndex)
	{
		return InRig.Driver()->Find(FDreamBy::Widget(InList.GetRowWidget(InItemIndex)));
	}

	/** Whether any row of InList is drawn pressed. */
	bool AnyRowLooksPressed(const UDreamListViewBase& InList)
	{
		for (const TObjectPtr<UDreamWidget>& Row : InList.RowNodes)
		{
			const UUIButton* RowButton = Row != nullptr ? Row->GetComponent<UUIButton>() : nullptr;
			if (RowButton != nullptr && RowButton->GetCurrentSelectionState() == EUISelectableSelectionState::Pressed)
			{
				return true;
			}
		}
		return false;
	}

	/** Pumped until the list has come to rest -- a finger lifted off it leaves it coasting -- for as long as its own glide takes. */
	bool WaitUntilStill(FDreamDriverRig& InRig, const UDreamListViewBase& InList)
	{
		const FWaitTimeout Timeout = FWaitTimeout::InSeconds(5.0);
		const UDreamListViewBase* List = &InList;
		return InRig.Driver()->Wait(FDreamUntil::Condition([List]()
		{
			return List->ScrollBehaviour == nullptr || !List->ScrollBehaviour->IsScrolling();
		}, Timeout), Timeout, TEXT("the list comes to rest"));
	}

	/** Whether exactly InExpected are selected, said item by item so a failure names the one that is wrong. */
	void ExpectSelection(FAutomationTestBase& InTest, const UDreamListViewBase& InList, const TCHAR* InWhen, TSet<int32> InExpected)
	{
		for (int32 Item = 0; Item < InList.GetItemCount(); ++Item)
		{
			const bool bExpected = InExpected.Contains(Item);
			if (InList.IsItemSelected(Item) != bExpected)
			{
				InTest.AddError(FString::Printf(TEXT("%s: item %d should %sbe selected"), InWhen, Item, bExpected ? TEXT("") : TEXT("not ")));
			}
		}
		InTest.TestEqual(FString::Printf(TEXT("%s: the number selected"), InWhen), InList.GetSelectedIndices().Num(), InExpected.Num());
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTileViewFingerTapMultiTest,
	"DreamGUI.TileView.AFingerTapInMultiModeAddsTheTileItLandsOnAndNeverTakesOneAway",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamTileViewFingerTapMultiTest, "DreamGUI.TileView.AFingerTapInMultiModeAddsTheTileItLandsOnAndNeverTakesOneAway", "[Touch][Animated]")

/*
 * STileView's tiles are STableRows like a list's rows, so STableRow::OnTouchEnded is the rule: in Multi mode a tap adds the
 * tile it lifts on without clearing the others, and a tap on a chosen tile sets it chosen again. Each tap is a click of
 * its tile.
 */
bool FDreamTileViewFingerTapMultiTest::RunTest(const FString& Parameters)
{
	using namespace DreamListTouchInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(DreamListsInteraction::ViewportSize());
	Rig.BindTest(this);
	UDreamTileView* Tiles = Rig.IsUsable() ? Rig.MakeControl<UDreamTileView>(TEXT("Tiles"), nullptr, FVector2D(360.0, 300.0)) : nullptr;
	if (!TestNotNull(TEXT("The rig and a tile view came up"), Tiles))
	{
		return false;
	}
	// Eighty by sixty with no gaps in a view 360 wide: four to a line, five lines on show.
	Tiles->SetStyleSource(EDreamUIStyleSource::Inline);
	FDreamTileViewStyle TileStyle = Tiles->GetStyle();
	TileStyle.List = DreamListsInteraction::WithRows(TileStyle.List, 60.0f);
	TileStyle.TileWidth = 80.0f;
	TileStyle.TileSpacing = 0.0f;
	Tiles->SetStyle(TileStyle);
	Tiles->SetItemObjects(DreamListsInteraction::MakeItems(20));
	Tiles->SetSelectionMode(EUIListSelectionMode::Multi);
	Rig.PumpFrames(2);
	TStrongObjectPtr<UDreamListsInteractionProbe> Probe(NewObject<UDreamListsInteractionProbe>());
	DreamListsInteraction::ListenToList(*Tiles, *Probe);
	if (!TestEqual(TEXT("Four tiles fit across"), Tiles->GetColumnCount(), 4))
	{
		return false;
	}

	TestTrue(TEXT("A tap on the sixth tile completes"), RowOf(Rig, *Tiles, 5)->Tap());
	ExpectSelection(*this, *Tiles, TEXT("After the first tap"), { 5 });
	TestTrue(TEXT("A tap on the tenth tile completes"), RowOf(Rig, *Tiles, 9)->Tap());
	ExpectSelection(*this, *Tiles, TEXT("After a tap on a tile of another line"), { 5, 9 });
	TestTrue(TEXT("A tap on the sixth tile again completes"), RowOf(Rig, *Tiles, 5)->Tap());
	ExpectSelection(*this, *Tiles, TEXT("After a tap on a chosen tile"), { 5, 9 });
	TestEqual(TEXT("Each tap was a click of its tile"), Probe->ClickedItems.Num(), 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTreeViewFingerTapTest,
	"DreamGUI.TreeView.AFingerTapOnARowChoosesItAndATapOnItsTwistyOpensItWithoutChoosingIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamTreeViewFingerTapTest, "DreamGUI.TreeView.AFingerTapOnARowChoosesItAndATapOnItsTwistyOpensItWithoutChoosingIt", "[Touch][Animated]")

/*
 * A tree row is an STableRow, and its twisty an SExpanderArrow -- a button of its own inside the row, which takes the
 * touch it is tapped with. A tap on the row away from the twisty chooses the row (OnTouchEnded); a tap on the twisty opens
 * the row (SExpanderArrow::OnArrowClicked) and leaves the choice where it was.
 */
bool FDreamTreeViewFingerTapTest::RunTest(const FString& Parameters)
{
	using namespace DreamListTouchInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(DreamListsInteraction::ViewportSize());
	Rig.BindTest(this);
	UDreamTreeView* Tree = Rig.IsUsable() ? Rig.MakeControl<UDreamTreeView>(TEXT("Tree"), nullptr, ListSize) : nullptr;
	if (!TestNotNull(TEXT("The rig and a tree came up"), Tree))
	{
		return false;
	}
	Tree->SetStyleSource(EDreamUIStyleSource::Inline);
	FDreamTreeViewStyle TreeStyle = Tree->GetStyle();
	TreeStyle.List = DreamListsInteraction::WithRows(TreeStyle.List, RowHeight);
	Tree->SetStyle(TreeStyle);
	// Fruit (Apple, Pear) folded, then Veg (Leek): three rows on show.
	Tree->SetItemsWithDepths(
		TArray<FText>{ FText::AsCultureInvariant(TEXT("Fruit")), FText::AsCultureInvariant(TEXT("Apple")), FText::AsCultureInvariant(TEXT("Pear")),
			FText::AsCultureInvariant(TEXT("Veg")), FText::AsCultureInvariant(TEXT("Leek")) },
		TArray<int32>{ 0, 1, 1, 0, 1 });
	Tree->SetCollapsedItems(TSet<int32>{ 0 });
	Rig.PumpFrames(2);
	TStrongObjectPtr<UDreamListsInteractionProbe> Probe(NewObject<UDreamListsInteractionProbe>());
	DreamListsInteraction::ListenToList(*Tree, *Probe);
	Tree->OnItemExpansionChanged.AddDynamic(Probe.Get(), &UDreamListsInteractionProbe::RecordExpansionChanged);
	if (!TestEqual(TEXT("Fruit is folded, so three rows show"), Tree->GetRowCount(), 3))
	{
		return false;
	}

	TestTrue(TEXT("A tap on Veg's row completes"), RowOf(Rig, *Tree, 3)->Tap());
	TestEqual(TEXT("The tap chose Veg"), Tree->GetSelectedIndex(), 3);
	TestEqual(TEXT("...and was a click of its row"), Probe->ClickedItems.Num(), 1);

	UDreamWidget* FruitRow = Tree->GetRowWidget(0);
	UDreamWidget* Twisty = FruitRow != nullptr ? FruitRow->FindChildByDisplayName(TEXT("Twisty")) : nullptr;
	if (!TestNotNull(TEXT("Fruit's row has a twisty"), Twisty))
	{
		return false;
	}
	Probe->ClearRecords();
	TestTrue(TEXT("A tap on Fruit's twisty completes"), Rig.Driver()->Find(FDreamBy::Widget(Twisty))->Tap());
	TestTrue(TEXT("The tap opened Fruit"), Tree->IsItemExpanded(0));
	TestEqual(TEXT("...so its two children show"), Tree->GetRowCount(), 5);
	TestEqual(TEXT("...announced once"), Probe->ExpansionItems.Num(), 1);
	TestEqual(TEXT("The choice is still Veg"), Tree->GetSelectedIndex(), 3);
	TestEqual(TEXT("...and the row under the twisty heard no click"), Probe->ClickedItems.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamListViewFingerDragScrollsTest,
	"DreamGUI.ListView.AFingerDraggedAlongTheListScrollsItAndLeavesNoRowChosenOrPressed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamListViewFingerDragScrollsTest, "DreamGUI.ListView.AFingerDraggedAlongTheListScrollsItAndLeavesNoRowChosenOrPressed", "[Touch][Animated]")

/*
 * STableRow::OnDragDetected ends a selection touch that turned into a drag and hands the touch to the list
 * (STableViewBase scrolls by the finger's travel); OnTouchEnded then has no selection touch to choose a row with. A finger
 * that lands on a row and drags up the list therefore moves the content with it, and when it lifts nothing is chosen,
 * nothing is clicked, and no row -- the one it landed on, nor the rows the scroll brought under it -- is left drawn pressed.
 */
bool FDreamListViewFingerDragScrollsTest::RunTest(const FString& Parameters)
{
	using namespace DreamListTouchInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(DreamListsInteraction::ViewportSize());
	Rig.BindTest(this);
	UDreamListView* List = Rig.IsUsable() ? MakeList(Rig, 30) : nullptr;
	if (!TestNotNull(TEXT("The rig and a list came up"), List)
		|| !TestEqual(TEXT("The list starts at the top"), List->GetScrollOffset(), 0.0f, 0.01f))
	{
		return false;
	}
	TStrongObjectPtr<UDreamListsInteractionProbe> Probe(NewObject<UDreamListsInteractionProbe>());
	DreamListsInteraction::ListenToList(*List, *Probe);

	TestTrue(TEXT("A finger lands on the seventh row and drags four rows up the list"), RowOf(Rig, *List, 6)->TouchDragBy(FVector2D(0.0, -4.0 * RowHeight)));
	TestTrue(TEXT("...and the list comes to rest"), WaitUntilStill(Rig, *List));
	TestTrue(FString::Printf(TEXT("The list moved with the finger (it is %.1f down)"), List->GetScrollOffset()), List->GetScrollOffset() > 2.0f * RowHeight);
	TestEqual(TEXT("Nothing was chosen"), List->GetNumItemsSelected(), 0);
	TestEqual(TEXT("...the choice never changed"), Probe->SelectionChanges.Num(), 0);
	TestEqual(TEXT("...and no row was clicked"), Probe->ClickedItems.Num(), 0);
	TestFalse(TEXT("No row is left drawn pressed"), AnyRowLooksPressed(*List));

	// And a tap still chooses, on whatever row is under it now.
	const int32 TopShown = FMath::FloorToInt32(List->GetScrollOffset() / RowHeight) + 1;
	TestTrue(TEXT("A tap on a row on show completes"), RowOf(Rig, *List, TopShown)->Tap());
	TestEqual(TEXT("The tap chose that row"), List->GetSelectedIndex(), TopShown);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamListRowFingerDragScrollsInsteadTest,
	"DreamGUI.ListRowDragDrop.AFingerDraggedAlongAListOfDraggableRowsScrollsItInsteadOfPickingARowUp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamListRowFingerDragScrollsInsteadTest, "DreamGUI.ListRowDragDrop.AFingerDraggedAlongAListOfDraggableRowsScrollsItInsteadOfPickingARowUp", "[Touch][Animated]")

/*
 * The other half of STableRow::OnDragDetected: for a selection touch it captures the touch for the list and returns
 * before OnDragDetected_Handler -- the row's own drag, the one UListView's entries pick an item up with -- is ever asked.
 * So rows that the mouse can pick up and drop are scrolled past by a finger, exactly as rows that cannot be dragged are:
 * the list moves, and no drag of an item is detected, cancelled or dropped.
 */
bool FDreamListRowFingerDragScrollsInsteadTest::RunTest(const FString& Parameters)
{
	using namespace DreamListTouchInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(DreamListsInteraction::ViewportSize());
	Rig.BindTest(this);
	UDreamListView* List = Rig.IsUsable() ? MakeList(Rig, 30) : nullptr;
	if (!TestNotNull(TEXT("The rig and a list came up"), List))
	{
		return false;
	}
	List->SetAllowDragging(true);
	List->SetAllowDragDrop(true);
	Rig.PumpFrames(1);
	TStrongObjectPtr<UDreamListsInteractionProbe> Probe(NewObject<UDreamListsInteractionProbe>());
	DreamListsInteraction::ListenToList(*List, *Probe);

	TestTrue(TEXT("A finger lands on the seventh row and drags four rows up the list"), RowOf(Rig, *List, 6)->TouchDragBy(FVector2D(0.0, -4.0 * RowHeight)));
	TestTrue(TEXT("...and the list comes to rest"), WaitUntilStill(Rig, *List));
	TestEqual(TEXT("No row was picked up by the finger"), Probe->DragDetectedItems.Num(), 0);
	TestEqual(TEXT("...so no drag was dropped"), Probe->DropItems.Num(), 0);
	TestEqual(TEXT("...or cancelled"), Probe->DragCancelledItems.Num(), 0);
	TestFalse(TEXT("The list is not dragging an item"), List->GetIsDraggingListItem());
	TestTrue(FString::Printf(TEXT("The list moved with the finger instead (it is %.1f down)"), List->GetScrollOffset()), List->GetScrollOffset() > 2.0f * RowHeight);
	TestEqual(TEXT("...and nothing was chosen"), List->GetNumItemsSelected(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamListRowMouseDragPicksUpTest,
	"DreamGUI.ListRowDragDrop.AMouseDragOnADraggableRowPicksItsItemUpAndLeavesTheListWhereItWas",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamListRowMouseDragPicksUpTest, "DreamGUI.ListRowDragDrop.AMouseDragOnADraggableRowPicksItsItemUpAndLeavesTheListWhereItWas", "[Pointer][Animated]")

/*
 * The same drag with the mouse is the row's: STableRow::OnDragDetected, with the row holding the mouse capture its press
 * took, calls OnDragDetected_Handler -- SObjectTableRow's, which asks the entry for its drag operation -- and STableViewBase
 * scrolls a list by a left-button drag not at all (only a right-button drag, or a finger, scrolls it). The item under the
 * press is picked up once, and the list stays where it was.
 */
bool FDreamListRowMouseDragPicksUpTest::RunTest(const FString& Parameters)
{
	using namespace DreamListTouchInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(DreamListsInteraction::ViewportSize());
	Rig.BindTest(this);
	UDreamListView* List = Rig.IsUsable() ? MakeList(Rig, 30) : nullptr;
	if (!TestNotNull(TEXT("The rig and a list came up"), List))
	{
		return false;
	}
	List->SetAllowDragging(true);
	List->SetAllowDragDrop(true);
	Rig.PumpFrames(1);
	TStrongObjectPtr<UDreamListsInteractionProbe> Probe(NewObject<UDreamListsInteractionProbe>());
	DreamListsInteraction::ListenToList(*List, *Probe);

	TestTrue(TEXT("The mouse presses the seventh row and drags four rows up"), RowOf(Rig, *List, 6)->DragBy(FVector2D(0.0, -4.0 * RowHeight)));
	TestTrue(TEXT("...and the list is at rest"), WaitUntilStill(Rig, *List));
	if (TestEqual(TEXT("One row was picked up"), Probe->DragDetectedItems.Num(), 1))
	{
		TestEqual(TEXT("...the seventh, which the press went down on"), Probe->DragDetectedItems[0], 6);
	}
	TestEqual(TEXT("The list did not scroll under the drag"), List->GetScrollOffset(), 0.0f, 0.5f);
	return true;
}

#endif
