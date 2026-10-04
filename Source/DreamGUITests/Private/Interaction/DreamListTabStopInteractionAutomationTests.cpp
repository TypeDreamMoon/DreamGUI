// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Controls/DreamButton.h"
#include "Controls/DreamListView.h"
#include "Controls/DreamScrollBar.h"
#include "Controls/DreamScrollBox.h"
#include "Controls/DreamTileView.h"
#include "Controls/DreamTreeView.h"
#include "Core/DreamUIInputServices.h"
#include "Core/Components/DreamWidget.h"
#include "InputCoreTypes.h"
#include "Interaction/UIButton.h"
#include "Interaction/UIScrollbar.h"
#include "Interaction/UIScrollView.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Interaction/DreamListsInteractionTestTypes.h"

/*
 * A LIST IS ONE TAB STOP, AS A BROWSER'S LIST BOX IS.
 *
 * List, tile and tree views are TabNavigation Once: Tab enters at one row -- the selected item's, else the first
 * item's, the last's for Shift+Tab -- and the next Tab leaves; the arrows move inside. The entry is found by INDEX
 * (UDreamListViewBase::ResolveTabEntry), scrolled into view and built, because a recycling list has rows only for
 * what shows: before this, Tab walked the rows geometrically, could never reach one that was not built, and
 * stopped on the list's scroll bar on its way out. The bar is no stop at all now, and no focus target -- nor is a
 * standalone scroll bar, by default -- and a scroll box names its own viewport as what the keys scroll.
 *
 * Buttons stand above and below each list, made in that order, which is the hierarchy's and so Tab's.
 */
namespace DreamListTabStopTestLocal
{
	constexpr float RowHeight = 40.0f;
	const FVector2D ListSize(300.0, 400.0);
	const FVector2D ButtonSize(160.0, 40.0);

	UDreamWidget* FocusOf(FDreamDriverRig& InRig)
	{
		const UDreamUIInputServices* Services = UDreamUIInputServices::Get(InRig.GetWorld());
		return Services != nullptr ? Services->GetFocusedWidget(0) : nullptr;
	}

	bool IsFocusOnOrIn(FDreamDriverRig& InRig, const UDreamWidget* InWidget)
	{
		const UDreamWidget* Focused = FocusOf(InRig);
		return Focused != nullptr && InWidget != nullptr && (Focused == InWidget || Focused->IsChildOf(InWidget));
	}

	/** A list of InItemCount fresh items, rows RowHeight apart: ten rows show in its window. */
	template<class TList>
	TList* MakeList(FDreamDriverRig& InRig, const FString& InName, int32 InItemCount, const FVector2D& InPosition = FVector2D::ZeroVector)
	{
		TList* List = InRig.MakeControl<TList>(InName, nullptr, ListSize, InPosition);
		if (List == nullptr)
		{
			return nullptr;
		}
		List->SetStyleSource(EDreamUIStyleSource::Inline);
		List->SetItemObjects(DreamListsInteraction::MakeItems(InItemCount));
		InRig.PumpFrames(2);
		return List;
	}

	/** Above, the list, below: the order a Tab walks them in. */
	struct FStage
	{
		UDreamButton* Before = nullptr;
		UDreamButton* After = nullptr;
	};

	FStage MakeButtonAbove(FDreamDriverRig& InRig)
	{
		FStage Stage;
		Stage.Before = InRig.MakeControl<UDreamButton>(TEXT("Before"), nullptr, ButtonSize, FVector2D(0.0, 260.0));
		return Stage;
	}

	void MakeButtonBelow(FDreamDriverRig& InRig, FStage& InOutStage)
	{
		InOutStage.After = InRig.MakeControl<UDreamButton>(TEXT("After"), nullptr, ButtonSize, FVector2D(0.0, -260.0));
	}

	bool PressTab(FDreamDriverRig& InRig)
	{
		return InRig.Driver()->Sequence().Tab().WaitFrames(1).Perform();
	}

	bool PressShiftTab(FDreamDriverRig& InRig)
	{
		return InRig.Driver()->Sequence().ShiftTab().WaitFrames(1).Perform();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamListsAreOneTabStopTest,
	"DreamGUI.ListView.ListTileAndTreeViewsAreEachOneTabStop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamListsAreOneTabStopTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("A list view is one Tab stop"),
		GetDefault<UDreamListView>()->GetTabNavigation() == EDreamWidgetTabNavigation::Once);
	TestTrue(TEXT("...a tile view too"),
		GetDefault<UDreamTileView>()->GetTabNavigation() == EDreamWidgetTabNavigation::Once);
	TestTrue(TEXT("...and a tree view"),
		GetDefault<UDreamTreeView>()->GetTabNavigation() == EDreamWidgetTabNavigation::Once);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamListTabEntryByIndexTest,
	"DreamGUI.ListView.TheTabEntryIsTheSelectedRowElseTheEndTabCameInAtBuiltByIndex",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The entry hook on its own, on a thousand items -- far past the recycling threshold, so only a window of them has
 * rows. The selected item's row is the entry even while it is far out of view: it is scrolled to and built. With
 * nothing selected the entry is the first item's row, or the last item's for Shift+Tab. Entering selects nothing,
 * and an empty list has nothing to enter.
 */
bool FDreamListTabEntryByIndexTest::RunTest(const FString& Parameters)
{
	using namespace DreamListTabStopTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(DreamListsInteraction::ViewportSize());
	Rig.BindTest(this);
	UDreamListView* List = MakeList<UDreamListView>(Rig, TEXT("List"), 1000);
	if (!TestTrue(TEXT("The rig and the list came up"), Rig.IsUsable() && List != nullptr))
	{
		return false;
	}
	List->SetStyle(DreamListsInteraction::WithRows(List->GetStyle(), RowHeight));
	Rig.PumpFrames(2);
	if (!TestTrue(TEXT("A thousand items are a recycled window"), List->IsVirtualizing()))
	{
		return false;
	}

	List->SetSelectedIndex(500);
	List->ScrollToTop();
	Rig.PumpFrames(1);
	TestNull(TEXT("Scrolled to the top, item 500 has no row"), List->GetRowWidget(500));
	UDreamWidget* SelectedEntry = List->ResolveTabEntry(false);
	TestNotNull(TEXT("Tab enters at the selected item"), SelectedEntry);
	TestTrue(TEXT("...the row now standing for it"), SelectedEntry != nullptr && SelectedEntry == List->GetRowWidget(500));
	TestTrue(TEXT("...scrolled into the window"), List->IsItemVisible(500) && List->GetScrollOffset() > 0.0f);
	TestTrue(TEXT("Shift+Tab enters at the selected item as well"), List->ResolveTabEntry(true) == List->GetRowWidget(500));

	List->ClearSelection();
	List->ScrollToTop();
	Rig.PumpFrames(1);
	UDreamWidget* LastEntry = List->ResolveTabEntry(true);
	TestTrue(TEXT("With nothing selected Shift+Tab enters at the last item's row, built"),
		LastEntry != nullptr && LastEntry == List->GetRowWidget(999));
	UDreamWidget* FirstEntry = List->ResolveTabEntry(false);
	TestTrue(TEXT("...and Tab at the first item's"), FirstEntry != nullptr && FirstEntry == List->GetRowWidget(0));
	TestEqual(TEXT("Entering selected nothing"), List->GetSelectedIndex(), static_cast<int32>(INDEX_NONE));

	List->ClearListItems();
	Rig.PumpFrames(1);
	TestNull(TEXT("An empty list has nothing to enter"), List->ResolveTabEntry(false));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTreeTabEntryTest,
	"DreamGUI.TreeView.TheTabEntrySkipsASelectedItemFoldedAwayForTheFirstOrLastRowShowing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTreeTabEntryTest::RunTest(const FString& Parameters)
{
	using namespace DreamListTabStopTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(DreamListsInteraction::ViewportSize());
	Rig.BindTest(this);
	UDreamTreeView* Tree = Rig.MakeControl<UDreamTreeView>(TEXT("Tree"), nullptr, ListSize);
	if (!TestTrue(TEXT("The rig and the tree came up"), Rig.IsUsable() && Tree != nullptr))
	{
		return false;
	}
	Tree->SetItemsWithDepths({
		FText::AsCultureInvariant(TEXT("Fruit")), FText::AsCultureInvariant(TEXT("Apple")), FText::AsCultureInvariant(TEXT("Pear")),
		FText::AsCultureInvariant(TEXT("Greens")), FText::AsCultureInvariant(TEXT("Kale")) },
		{ 0, 1, 1, 0, 1 });
	Rig.PumpFrames(2);

	Tree->SetSelectedIndex(2);
	TestTrue(TEXT("The selected item's row is the entry while it shows"), Tree->ResolveTabEntry(false) == Tree->GetRowWidget(2));
	Tree->SetItemExpanded(0, false);
	Rig.PumpFrames(1);
	TestTrue(TEXT("Folded away, it is not: Tab enters at the first row showing"), Tree->ResolveTabEntry(false) == Tree->GetRowWidget(0));
	TestTrue(TEXT("...and Shift+Tab at the last"), Tree->ResolveTabEntry(true) == Tree->GetRowWidget(4));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamListTabThroughVirtualizedListTest,
	"DreamGUI.ListView.TabEntersAThousandRowListAtItsSelectedRowAndTheNextTabLeavesIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamListTabThroughVirtualizedListTest, "DreamGUI.ListView.TabEntersAThousandRowListAtItsSelectedRowAndTheNextTabLeavesIt", "[Nav][Animated]")

/*
 * A button, a list of a thousand items with item 500 selected and scrolled far out of view, a button. Tab from the
 * first button lands on item 500's row -- one stop, wherever the selection is -- and the next Tab leaves the list
 * for the second button. Shift+Tab comes back in at the same row and out again to the first button.
 */
bool FDreamListTabThroughVirtualizedListTest::RunTest(const FString& Parameters)
{
	using namespace DreamListTabStopTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(DreamListsInteraction::ViewportSize());
	Rig.BindTest(this);
	FStage Stage = MakeButtonAbove(Rig);
	UDreamListView* List = MakeList<UDreamListView>(Rig, TEXT("List"), 1000);
	MakeButtonBelow(Rig, Stage);
	if (!TestTrue(TEXT("The rig, the list and both buttons came up"),
		Rig.IsUsable() && List != nullptr && Stage.Before != nullptr && Stage.After != nullptr))
	{
		return false;
	}
	List->SetStyle(DreamListsInteraction::WithRows(List->GetStyle(), RowHeight));
	List->SetSelectedIndex(500);
	List->ScrollToTop();
	Rig.PumpFrames(2);
	TestNull(TEXT("The selected item starts with no row"), List->GetRowWidget(500));

	TestTrue(TEXT("Clicking the first button completes"), Rig.Driver()->Find(FDreamBy::Widget(Stage.Before))->Click());
	TestTrue(TEXT("Tab from the first button completes"), PressTab(Rig));
	UDreamWidget* SelectedRow = List->GetRowWidget(500);
	TestNotNull(TEXT("The selected item's row was built"), SelectedRow);
	TestTrue(TEXT("Tab entered the list at the selected row"), SelectedRow != nullptr && IsFocusOnOrIn(Rig, SelectedRow));

	TestTrue(TEXT("Tab from the row completes"), PressTab(Rig));
	TestTrue(TEXT("The next Tab left the list for the second button"), IsFocusOnOrIn(Rig, Stage.After));

	TestTrue(TEXT("Shift+Tab from the second button completes"), PressShiftTab(Rig));
	SelectedRow = List->GetRowWidget(500);
	TestTrue(TEXT("Shift+Tab came back in at the selected row"), SelectedRow != nullptr && IsFocusOnOrIn(Rig, SelectedRow));
	TestTrue(TEXT("Shift+Tab from the row completes"), PressShiftTab(Rig));
	TestTrue(TEXT("...and left for the first button"), IsFocusOnOrIn(Rig, Stage.Before));
	TestEqual(TEXT("Tabbing through changed no selection"), List->GetSelectedIndex(), 500);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTileTabEntryTest,
	"DreamGUI.TileView.TabEntersAtTheFirstTileAndShiftTabAtTheLast",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamTileTabEntryTest, "DreamGUI.TileView.TabEntersAtTheFirstTileAndShiftTabAtTheLast", "[Nav][Animated]")

/*
 * A tile view with nothing selected is entered in its source order: Tab at its first tile, Shift+Tab at its last --
 * a tile far below the window, scrolled to and built -- and either way the next press of the same key leaves it.
 */
bool FDreamTileTabEntryTest::RunTest(const FString& Parameters)
{
	using namespace DreamListTabStopTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(DreamListsInteraction::ViewportSize());
	Rig.BindTest(this);
	FStage Stage = MakeButtonAbove(Rig);
	UDreamTileView* Tiles = MakeList<UDreamTileView>(Rig, TEXT("Tiles"), 300);
	MakeButtonBelow(Rig, Stage);
	if (!TestTrue(TEXT("The rig, the tile view and both buttons came up"),
		Rig.IsUsable() && Tiles != nullptr && Stage.Before != nullptr && Stage.After != nullptr))
	{
		return false;
	}
	Rig.PumpFrames(2);
	const int32 Last = Tiles->GetNumItems() - 1;

	TestTrue(TEXT("Clicking the first button completes"), Rig.Driver()->Find(FDreamBy::Widget(Stage.Before))->Click());
	TestTrue(TEXT("Tab from the first button completes"), PressTab(Rig));
	UDreamWidget* FirstTile = Tiles->GetRowWidget(0);
	TestTrue(TEXT("Tab entered at the first tile"), FirstTile != nullptr && IsFocusOnOrIn(Rig, FirstTile));
	TestTrue(TEXT("Tab from the tile completes"), PressTab(Rig));
	TestTrue(TEXT("The next Tab left the tile view"), IsFocusOnOrIn(Rig, Stage.After));

	TestTrue(TEXT("Shift+Tab from the second button completes"), PressShiftTab(Rig));
	UDreamWidget* LastTile = Tiles->GetRowWidget(Last);
	TestNotNull(TEXT("The last tile was scrolled to and built"), LastTile);
	TestTrue(TEXT("Shift+Tab entered at the last tile"), LastTile != nullptr && IsFocusOnOrIn(Rig, LastTile));
	TestTrue(TEXT("Shift+Tab from the tile completes"), PressShiftTab(Rig));
	TestTrue(TEXT("...and left for the first button"), IsFocusOnOrIn(Rig, Stage.Before));
	TestEqual(TEXT("Nothing was selected on the way"), Tiles->GetSelectedIndex(), static_cast<int32>(INDEX_NONE));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamListScrollBarNoStopTest,
	"DreamGUI.ListView.TheListsScrollBarNeverTakesTheFocus",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamListScrollBarNoStopTest, "DreamGUI.ListView.TheListsScrollBarNeverTakesTheFocus", "[Nav][Animated]")

/*
 * An overflowing list shows its bar. The bar is out of Tab order altogether, navigation does not land on its track,
 * and the track refuses the focus: Tab goes from a row straight to what follows the list, and the arrow toward the
 * bar does not reach it.
 */
bool FDreamListScrollBarNoStopTest::RunTest(const FString& Parameters)
{
	using namespace DreamListTabStopTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(DreamListsInteraction::ViewportSize());
	Rig.BindTest(this);
	FStage Stage = MakeButtonAbove(Rig);
	UDreamListView* List = MakeList<UDreamListView>(Rig, TEXT("List"), 30);
	MakeButtonBelow(Rig, Stage);
	if (!TestTrue(TEXT("The rig, the list and both buttons came up"),
		Rig.IsUsable() && List != nullptr && Stage.Before != nullptr && Stage.After != nullptr))
	{
		return false;
	}
	List->SetStyle(DreamListsInteraction::WithRows(List->GetStyle(), RowHeight));
	Rig.PumpFrames(2);
	UDreamScrollBar* Bar = List->ScrollBarNode;
	if (!TestTrue(TEXT("The list has its bar, with a track"), Bar != nullptr && Bar->TrackNode != nullptr && Bar->BarBehaviour != nullptr))
	{
		return false;
	}
	TestTrue(TEXT("Nothing in the bar is a Tab stop"), Bar->GetTabNavigation() == EDreamWidgetTabNavigation::None);
	TestFalse(TEXT("Navigation does not land on the track"), Bar->BarBehaviour->GetCanNavigateHere());
	TestFalse(TEXT("The track is no focus target"), Bar->TrackNode->GetIsFocusable());
	TestFalse(TEXT("...and refuses the focus"), Bar->TrackNode->SetFocus(0));

	TestTrue(TEXT("Clicking the first button completes"), Rig.Driver()->Find(FDreamBy::Widget(Stage.Before))->Click());
	TestTrue(TEXT("Tab into the list completes"), PressTab(Rig));
	UDreamWidget* FirstRow = List->GetRowWidget(0);
	TestTrue(TEXT("Tab entered at the first row"), FirstRow != nullptr && IsFocusOnOrIn(Rig, FirstRow));
	TestTrue(TEXT("The arrow toward the bar completes"), Rig.Driver()->Sequence().Navigate(EDreamUINavigationDirection::Right).Perform());
	TestFalse(TEXT("The arrow did not put the focus on the bar"), IsFocusOnOrIn(Rig, Bar));
	TestTrue(TEXT("Tab out of the list completes"), PressTab(Rig));
	TestTrue(TEXT("Tab went past the bar to the second button"), IsFocusOnOrIn(Rig, Stage.After));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamScrollBarNoFocusTargetTest,
	"DreamGUI.ScrollBar.AScrollBarIsNoFocusTargetUnlessAskedToBeOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A standalone bar, as Slate's: the pointer drives it, navigation does not land on its track or its arrows, and its
 * track refuses the focus -- a bar that took the pad's focus used to swallow every press along its axis.
 */
bool FDreamScrollBarNoFocusTargetTest::RunTest(const FString& Parameters)
{
	FDreamDriverRig Rig = FDreamDriverRig::Headless(DreamListsInteraction::ViewportSize());
	Rig.BindTest(this);
	UDreamScrollBar* Bar = Rig.MakeControl<UDreamScrollBar>(TEXT("Bar"), nullptr, FVector2D(16.0, 300.0));
	if (!TestTrue(TEXT("The rig and the bar came up, with its parts"),
		Rig.IsUsable() && Bar != nullptr && Bar->BarBehaviour != nullptr && Bar->TrackNode != nullptr))
	{
		return false;
	}
	Bar->SetShowArrows(true);
	Rig.PumpFrames(2);
	TestFalse(TEXT("Navigation does not land on the track"), Bar->BarBehaviour->GetCanNavigateHere());
	TestFalse(TEXT("The track is no focus target"), Bar->TrackNode->GetIsFocusable());
	TestFalse(TEXT("...and SetFocus is refused"), Bar->TrackNode->SetFocus(0));
	TestTrue(TEXT("Navigation does not land on either arrow"),
		Bar->ArrowStartBehaviour != nullptr && !Bar->ArrowStartBehaviour->GetCanNavigateHere()
		&& Bar->ArrowEndBehaviour != nullptr && !Bar->ArrowEndBehaviour->GetCanNavigateHere());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamScrollBoxScrollTargetTest,
	"DreamGUI.ScrollBox.AScrollBoxNamesItsOwnViewportAsWhatTheKeysScrollAndItsBarIsNoStop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A focused scroll box has its focus on its face, and its scroll view on the viewport under the face: a search for
 * something to scroll that starts at the focus and walks up never meets it. The box answers with that viewport, so
 * paging, Home and End and the stick scroll the box itself -- a credits roll, an EULA. Its bar, like a list's, is
 * no stop.
 */
bool FDreamScrollBoxScrollTargetTest::RunTest(const FString& Parameters)
{
	FDreamDriverRig Rig = FDreamDriverRig::Headless(DreamListsInteraction::ViewportSize());
	Rig.BindTest(this);
	UDreamScrollBox* Box = Rig.MakeControl<UDreamScrollBox>(TEXT("Box"), nullptr, FVector2D(300.0, 200.0));
	if (!TestTrue(TEXT("The rig and the box came up, with a viewport and a view"),
		Rig.IsUsable() && Box != nullptr && Box->ViewportNode != nullptr && Box->GetScrollView() != nullptr))
	{
		return false;
	}
	Rig.PumpFrames(1);
	TestTrue(TEXT("The box names its viewport"), Box->GetScrollTargetForNavigation() == Box->ViewportNode);
	TestTrue(TEXT("...which carries the box's scroll view"), Box->ViewportNode->GetComponent<UUIScrollView>() == Box->GetScrollView());
	TestTrue(TEXT("A plain widget names nothing, and the search goes on as it always did"),
		Rig.Root() != nullptr && Rig.Root()->GetScrollTargetForNavigation() == nullptr);
	if (TestNotNull(TEXT("The box has its bar"), Box->ScrollBarNode.Get()))
	{
		TestTrue(TEXT("Nothing in the bar is a Tab stop"), Box->ScrollBarNode->GetTabNavigation() == EDreamWidgetTabNavigation::None);
		TestTrue(TEXT("...nor a place navigation lands"),
			Box->ScrollBarNode->BarBehaviour != nullptr && !Box->ScrollBarNode->BarBehaviour->GetCanNavigateHere());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamScrollBoxPagesItselfTest,
	"DreamGUI.ScrollBox.AFocusedScrollBoxPagesItselfWithPageDown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamScrollBoxPagesItselfTest, "DreamGUI.ScrollBox.AFocusedScrollBoxPagesItselfWithPageDown", "[Nav][Animated]")

/*
 * A text-only panel -- a credits roll -- made a focus target and given the focus: Page Down scrolls the box itself,
 * through the viewport it names, where the search used to start above the face holding the focus and find nothing.
 */
bool FDreamScrollBoxPagesItselfTest::RunTest(const FString& Parameters)
{
	using namespace DreamListTabStopTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(DreamListsInteraction::ViewportSize());
	Rig.BindTest(this);
	UDreamScrollBox* Box = Rig.MakeControl<UDreamScrollBox>(TEXT("Credits"), nullptr, FVector2D(300.0, 200.0));
	if (!TestTrue(TEXT("The rig and the box came up"), Rig.IsUsable() && Box != nullptr && Box->GetContentNode() != nullptr))
	{
		return false;
	}
	for (int32 LineIndex = 0; LineIndex < 20; ++LineIndex)
	{
		Rig.MakeWidget(FString::Printf(TEXT("Line%02d"), LineIndex), Box->GetContentNode(), FVector2D(280.0, 60.0));
	}
	Box->RefreshContentExtent();
	// A focus target from here on: the box puts its focus behaviour on its face at the next push.
	Box->SetIsFocusable(true);
	Box->ApplyStyle();
	Rig.PumpFrames(2);
	UDreamUIInputServices* Services = UDreamUIInputServices::Get(Rig.GetWorld());
	if (!TestTrue(TEXT("The box's face takes the focus"), Services != nullptr && Box->FaceNode != nullptr
		&& Services->FocusForNavigation(Box->FaceNode, 0)))
	{
		return false;
	}
	TestEqual(TEXT("The box starts at the top"), Box->GetScrollOffset(), 0.0f, 1.0e-3f);
	TestTrue(TEXT("Page Down completes"), Rig.Driver()->Sequence().Key(EKeys::PageDown).WaitSeconds(0.5f).Perform());
	TestTrue(TEXT("Page Down scrolled the focused box itself"), Box->GetScrollOffset() > 1.0f);
	return true;
}

#endif
