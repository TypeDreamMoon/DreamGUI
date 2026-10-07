// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Controls/DreamListView.h"
#include "Controls/DreamTileView.h"
#include "Controls/DreamTreeView.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIInputServices.h"
#include "InputCoreTypes.h"
#include "UObject/StrongObjectPtr.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Driver/DreamDriverTypes.h"
#include "Interaction/DreamListsInteractionTestTypes.h"

/*
 * CHOOSING SEVERAL ROWS, AS SListView CHOOSES THEM.
 *
 * In Multi mode STableRow::OnMouseButtonDown and OnMouseButtonUp (Slate/Public/Widgets/Views/STableRow.h) make a plain
 * click choose one row, Ctrl add or take away one, and Shift add the rows from the range anchor to the one clicked --
 * Shift first, so Ctrl with Shift ranges too. STableRow::OnTouchEnded makes a finger's tap add the row and never take one
 * away. SListView::NavigationSelect makes an arrow with Shift select from the anchor to where it lands, clearing the rest
 * unless Ctrl is held too, and an arrow with Ctrl alone add the row it lands on; OnKeyDown_Internal makes Ctrl+A select
 * every row. The anchor (RangeSelectionStart) is moved by a plain click, a Ctrl click, a tap and a plain step, never by a
 * range. Tile and tree views choose the same way, over the rows they show in the order they show them.
 *
 * A click reads its modifier keys from the clicking player's controller, so the clicks with Ctrl or Shift held are made
 * on a rig whose input goes through one (the standalone input actor), with the modifier pressed through it around the
 * click. A key carries its own chord, and the arrow tests run on the plain module rig.
 */
namespace DreamListMultiSelectInteractionTestLocal
{
	constexpr float RowHeight = 40.0f;
	const FVector2D ListSize(300.0, 400.0);

	FDreamRigOptions OptionsFor(EDreamRigInputHost InHost)
	{
		FDreamRigOptions Options;
		Options.ViewportSize = DreamListsInteraction::ViewportSize();
		Options.InputHost = InHost;
		return Options;
	}

	/** A Multi list of InItemCount items, rows RowHeight apart: ten rows on screen in a 400-unit window. */
	UDreamListView* MakeMultiList(FDreamDriverRig& InRig, int32 InItemCount)
	{
		UDreamListView* List = InRig.MakeControl<UDreamListView>(TEXT("List"), nullptr, ListSize);
		if (List == nullptr)
		{
			return nullptr;
		}
		// Inline, so a project sheet in the running editor cannot decide how tall a row is.
		List->SetStyleSource(EDreamUIStyleSource::Inline);
		List->SetStyle(DreamListsInteraction::WithRows(List->GetStyle(), RowHeight));
		List->SetItemObjects(DreamListsInteraction::MakeItems(InItemCount));
		List->SetSelectionMode(EUIListSelectionMode::Multi);
		InRig.PumpFrames(2);
		return List;
	}

	/** The locator for the row standing for an item. */
	FDreamLocatorRef RowOf(UDreamListViewBase& InList, int32 InItemIndex)
	{
		return FDreamBy::Widget(InList.GetRowWidget(InItemIndex));
	}

	/** A click on an item's row, with InModifier held through the rig's input host around it. */
	bool ClickHolding(FDreamDriverRig& InRig, UDreamListViewBase& InList, int32 InItemIndex, const FKey& InModifier)
	{
		return InRig.Driver()->Sequence()
			.KeyDown(InModifier)
			.Click(RowOf(InList, InItemIndex))
			.KeyUp(InModifier)
			.Perform();
	}

	/** One key, pressed and let go of through the rig's input host, then a frame for navigation to land. */
	bool PressKey(FDreamDriverRig& InRig, const FKey& InKey, EDreamDriverModifierKeys InModifiers = EDreamDriverModifierKeys::None)
	{
		return InRig.Driver()->Sequence().Key(InKey, InModifiers).WaitFrames(1).Perform();
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

	/** Whether player 0's focus is on the row standing for InItemIndex. */
	bool IsFocusOnRow(FDreamDriverRig& InRig, const UDreamListViewBase& InList, int32 InItemIndex)
	{
		const UDreamUIInputServices* Services = UDreamUIInputServices::Get(InRig.GetWorld());
		const UDreamWidget* Row = InList.GetRowWidget(InItemIndex);
		return Services != nullptr && Row != nullptr && Services->GetFocusedWidget(0) == Row;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamListMultiSelectCtrlClickTest,
	"DreamGUI.ListView.ACtrlClickInMultiModeAddsTheRowAndASecondOneTakesItAway",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamListMultiSelectCtrlClickTest, "DreamGUI.ListView.ACtrlClickInMultiModeAddsTheRowAndASecondOneTakesItAway", "[Pointer][Animated]")

bool FDreamListMultiSelectCtrlClickTest::RunTest(const FString& Parameters)
{
	using namespace DreamListMultiSelectInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(OptionsFor(EDreamRigInputHost::StandaloneActor));
	Rig.BindTest(this);
	UDreamListView* List = Rig.IsUsable() ? MakeMultiList(Rig, 30) : nullptr;
	if (!TestNotNull(TEXT("The rig and a Multi list came up"), List))
	{
		return false;
	}
	TStrongObjectPtr<UDreamListsInteractionProbe> Probe(NewObject<UDreamListsInteractionProbe>());
	DreamListsInteraction::ListenToList(*List, *Probe);

	TestTrue(TEXT("A plain click on the third row completes"), Rig.Driver()->Find(RowOf(*List, 2))->Click());
	ExpectSelection(*this, *List, TEXT("After a plain click"), { 2 });

	TestTrue(TEXT("A Ctrl click on the fifth row completes"), ClickHolding(Rig, *List, 4, EKeys::LeftControl));
	ExpectSelection(*this, *List, TEXT("After a Ctrl click on another row"), { 2, 4 });
	TestEqual(TEXT("The row it added is the one SelectedIndex names"), List->GetSelectedIndex(), 4);

	TestTrue(TEXT("A Ctrl click on the eighth row completes"), ClickHolding(Rig, *List, 7, EKeys::RightControl));
	ExpectSelection(*this, *List, TEXT("After a Ctrl click with the other Ctrl"), { 2, 4, 7 });

	TestTrue(TEXT("A Ctrl click on the third row again completes"), ClickHolding(Rig, *List, 2, EKeys::LeftControl));
	ExpectSelection(*this, *List, TEXT("After a Ctrl click on a chosen row"), { 4, 7 });

	Probe->ClearRecords();
	TestTrue(TEXT("A plain click on the fifth row completes"), Rig.Driver()->Find(RowOf(*List, 4))->Click());
	ExpectSelection(*this, *List, TEXT("After a plain click on one of the chosen rows"), { 4 });
	TestEqual(TEXT("The narrowing was announced once"), Probe->SelectionChanges.Num(), 1);
	TestEqual(TEXT("...and the click was announced once"), Probe->ClickedItems.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamListMultiSelectShiftClickTest,
	"DreamGUI.ListView.AShiftClickInMultiModeAddsEveryRowFromTheAnchorToTheOneClicked",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamListMultiSelectShiftClickTest, "DreamGUI.ListView.AShiftClickInMultiModeAddsEveryRowFromTheAnchorToTheOneClicked", "[Pointer][Animated]")

/*
 * Private_SelectRangeFromCurrentTo adds the rows from RangeSelectionStart to the one clicked and clears nothing, and the
 * anchor does not move: a second Shift click ranges from the same row, the other way this time. A Ctrl click moves the
 * anchor, and a Shift click with Ctrl held is a Shift click -- STableRow asks Shift first.
 */
bool FDreamListMultiSelectShiftClickTest::RunTest(const FString& Parameters)
{
	using namespace DreamListMultiSelectInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(OptionsFor(EDreamRigInputHost::StandaloneActor));
	Rig.BindTest(this);
	UDreamListView* List = Rig.IsUsable() ? MakeMultiList(Rig, 30) : nullptr;
	if (!TestNotNull(TEXT("The rig and a Multi list came up"), List))
	{
		return false;
	}

	TestTrue(TEXT("A plain click on the fourth row completes"), Rig.Driver()->Find(RowOf(*List, 3))->Click());
	TestTrue(TEXT("A Shift click on the seventh row completes"), ClickHolding(Rig, *List, 6, EKeys::LeftShift));
	ExpectSelection(*this, *List, TEXT("After a Shift click down the list"), { 3, 4, 5, 6 });
	TestEqual(TEXT("The row the range ended on is the one SelectedIndex names"), List->GetSelectedIndex(), 6);

	TestTrue(TEXT("A Shift click on the second row completes"), ClickHolding(Rig, *List, 1, EKeys::RightShift));
	ExpectSelection(*this, *List, TEXT("After a Shift click up the list, from the same anchor"), { 1, 2, 3, 4, 5, 6 });

	TestTrue(TEXT("A Ctrl click on the ninth row completes"), ClickHolding(Rig, *List, 8, EKeys::LeftControl));
	TestTrue(TEXT("A Ctrl+Shift click on the tenth row completes"),
		Rig.Driver()->Sequence()
			.KeyDown(EKeys::LeftControl)
			.KeyDown(EKeys::LeftShift)
			.Click(RowOf(*List, 9))
			.KeyUp(EKeys::LeftShift)
			.KeyUp(EKeys::LeftControl)
			.Perform());
	ExpectSelection(*this, *List, TEXT("After a Ctrl+Shift click from the row the Ctrl click anchored"), { 1, 2, 3, 4, 5, 6, 8, 9 });
	TestFalse(TEXT("The row between the two ranges was never chosen"), List->IsItemSelected(7));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamListMultiSelectShiftArrowTest,
	"DreamGUI.ListView.ShiftWithAnArrowKeyInMultiModeSelectsFromTheAnchorToTheRowTheFocusMovesTo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamListMultiSelectShiftArrowTest, "DreamGUI.ListView.ShiftWithAnArrowKeyInMultiModeSelectsFromTheAnchorToTheRowTheFocusMovesTo", "[Nav][Animated]")

/*
 * NavigationSelect with Shift held clears the selection and selects from RangeSelectionStart to the row the selector
 * moves to, so the range grows and shrinks around the anchor as the arrows go, and crosses it.
 */
bool FDreamListMultiSelectShiftArrowTest::RunTest(const FString& Parameters)
{
	using namespace DreamListMultiSelectInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(DreamListsInteraction::ViewportSize());
	Rig.BindTest(this);
	UDreamListView* List = Rig.IsUsable() ? MakeMultiList(Rig, 30) : nullptr;
	if (!TestNotNull(TEXT("The rig and a Multi list came up"), List))
	{
		return false;
	}
	if (!TestTrue(TEXT("A click on the third row completes"), Rig.Driver()->Find(RowOf(*List, 2))->Click())
		|| !TestTrue(TEXT("...and puts the focus on it"), IsFocusOnRow(Rig, *List, 2)))
	{
		return false;
	}

	TestTrue(TEXT("Shift+Down completes"), PressKey(Rig, EKeys::Down, EDreamDriverModifierKeys::Shift));
	ExpectSelection(*this, *List, TEXT("After Shift+Down"), { 2, 3 });
	TestTrue(TEXT("The focus moved down with it"), IsFocusOnRow(Rig, *List, 3));
	TestTrue(TEXT("Shift+Down again completes"), PressKey(Rig, EKeys::Down, EDreamDriverModifierKeys::Shift));
	ExpectSelection(*this, *List, TEXT("After a second Shift+Down"), { 2, 3, 4 });

	TestTrue(TEXT("Shift+Up completes"), PressKey(Rig, EKeys::Up, EDreamDriverModifierKeys::Shift));
	ExpectSelection(*this, *List, TEXT("After Shift+Up, the range shrinks back towards the anchor"), { 2, 3 });
	TestTrue(TEXT("The focus moved up with it"), IsFocusOnRow(Rig, *List, 3));
	TestTrue(TEXT("Shift+Up to the anchor completes"), PressKey(Rig, EKeys::Up, EDreamDriverModifierKeys::Shift));
	TestTrue(TEXT("Shift+Up past it completes"), PressKey(Rig, EKeys::Up, EDreamDriverModifierKeys::Shift));
	ExpectSelection(*this, *List, TEXT("After Shift+Up past the anchor, the range is on its other side"), { 1, 2 });
	TestTrue(TEXT("...with the focus on the row above the anchor"), IsFocusOnRow(Rig, *List, 1));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamListMultiSelectCtrlArrowTest,
	"DreamGUI.ListView.CtrlWithAnArrowKeyInMultiModeAddsTheRowItMovesToAndCtrlASelectsEveryRow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamListMultiSelectCtrlArrowTest, "DreamGUI.ListView.CtrlWithAnArrowKeyInMultiModeAddsTheRowItMovesToAndCtrlASelectsEveryRow", "[Nav][Animated]")

/*
 * NavigationSelect with Ctrl alone adds the row the selector moves to and makes it the anchor; a plain arrow still selects
 * only where it lands; Ctrl with Shift adds the range from the anchor without clearing; and Ctrl+A selects every row.
 */
bool FDreamListMultiSelectCtrlArrowTest::RunTest(const FString& Parameters)
{
	using namespace DreamListMultiSelectInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(DreamListsInteraction::ViewportSize());
	Rig.BindTest(this);
	UDreamListView* List = Rig.IsUsable() ? MakeMultiList(Rig, 30) : nullptr;
	if (!TestNotNull(TEXT("The rig and a Multi list came up"), List)
		|| !TestTrue(TEXT("A click on the third row completes"), Rig.Driver()->Find(RowOf(*List, 2))->Click()))
	{
		return false;
	}

	TestTrue(TEXT("Ctrl+Down completes"), PressKey(Rig, EKeys::Down, EDreamDriverModifierKeys::Ctrl));
	ExpectSelection(*this, *List, TEXT("After Ctrl+Down"), { 2, 3 });
	TestTrue(TEXT("The focus moved down"), IsFocusOnRow(Rig, *List, 3));
	TestTrue(TEXT("Ctrl+Down again completes"), PressKey(Rig, EKeys::Down, EDreamDriverModifierKeys::Ctrl));
	ExpectSelection(*this, *List, TEXT("After a second Ctrl+Down"), { 2, 3, 4 });

	TestTrue(TEXT("A plain Down completes"), PressKey(Rig, EKeys::Down));
	ExpectSelection(*this, *List, TEXT("After a plain Down, only the row it moved to"), { 5 });
	TestTrue(TEXT("...where the focus is"), IsFocusOnRow(Rig, *List, 5));

	TestTrue(TEXT("Ctrl+Shift+Down completes"), PressKey(Rig, EKeys::Down, EDreamDriverModifierKeys::Ctrl | EDreamDriverModifierKeys::Shift));
	ExpectSelection(*this, *List, TEXT("After Ctrl+Shift+Down, the range from the anchor the plain step left"), { 5, 6 });

	TestTrue(TEXT("Ctrl+A completes"), PressKey(Rig, EKeys::A, EDreamDriverModifierKeys::Ctrl));
	TestEqual(TEXT("Ctrl+A selected every row"), List->GetSelectedIndices().Num(), List->GetItemCount());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamListMultiSelectFingerTapTest,
	"DreamGUI.ListView.AFingerTapInMultiModeAddsTheRowItLandsOnAndNeverTakesOneAway",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamListMultiSelectFingerTapTest, "DreamGUI.ListView.AFingerTapInMultiModeAddsTheRowItLandsOnAndNeverTakesOneAway", "[Pointer][Touch][Animated]")

/*
 * STableRow::OnTouchEnded in Multi mode: an unchosen row is added without clearing the rest, and a chosen one is set
 * chosen again -- a finger has no Ctrl to take one away with. The mouse is still the mouse: a plain click after the taps
 * chooses its row alone.
 */
bool FDreamListMultiSelectFingerTapTest::RunTest(const FString& Parameters)
{
	using namespace DreamListMultiSelectInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(DreamListsInteraction::ViewportSize());
	Rig.BindTest(this);
	UDreamListView* List = Rig.IsUsable() ? MakeMultiList(Rig, 30) : nullptr;
	if (!TestNotNull(TEXT("The rig and a Multi list came up"), List))
	{
		return false;
	}
	TStrongObjectPtr<UDreamListsInteractionProbe> Probe(NewObject<UDreamListsInteractionProbe>());
	DreamListsInteraction::ListenToList(*List, *Probe);

	TestTrue(TEXT("A tap on the third row completes"), Rig.Driver()->Find(RowOf(*List, 2))->Tap());
	ExpectSelection(*this, *List, TEXT("After the first tap"), { 2 });
	TestTrue(TEXT("A tap on the sixth row completes"), Rig.Driver()->Find(RowOf(*List, 5))->Tap());
	ExpectSelection(*this, *List, TEXT("After a tap on another row"), { 2, 5 });
	TestTrue(TEXT("A tap on the third row again completes"), Rig.Driver()->Find(RowOf(*List, 2))->Tap());
	ExpectSelection(*this, *List, TEXT("After a tap on a chosen row"), { 2, 5 });
	TestEqual(TEXT("Each tap was a click of its row"), Probe->ClickedItems.Num(), 3);

	TestTrue(TEXT("A mouse click on the eighth row completes"), Rig.Driver()->Find(RowOf(*List, 7))->Click());
	ExpectSelection(*this, *List, TEXT("After a plain mouse click"), { 7 });
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTileMultiSelectShiftArrowTest,
	"DreamGUI.TileView.ShiftWithAnArrowKeyInMultiModeSelectsTheTilesBetweenInSourceOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamTileMultiSelectShiftArrowTest, "DreamGUI.TileView.ShiftWithAnArrowKeyInMultiModeSelectsTheTilesBetweenInSourceOrder", "[Nav][Animated]")

/*
 * STileView ranges over its items in source order, so Shift+Down from the second tile -- a whole line, four tiles on --
 * selects the five tiles from it to the one below it, the rest of its line and the start of the next included. Shift+Right
 * then grows it by one.
 */
bool FDreamTileMultiSelectShiftArrowTest::RunTest(const FString& Parameters)
{
	using namespace DreamListMultiSelectInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(DreamListsInteraction::ViewportSize());
	Rig.BindTest(this);
	UDreamTileView* Tiles = Rig.IsUsable() ? Rig.MakeControl<UDreamTileView>(TEXT("Tiles"), nullptr, FVector2D(360.0, 300.0)) : nullptr;
	if (!TestNotNull(TEXT("The rig and a tile view came up"), Tiles))
	{
		return false;
	}
	// Eighty by sixty with no gaps in a view 360 wide: four to a line.
	Tiles->SetStyleSource(EDreamUIStyleSource::Inline);
	FDreamTileViewStyle TileStyle = Tiles->GetStyle();
	TileStyle.List = DreamListsInteraction::WithRows(TileStyle.List, 60.0f);
	TileStyle.TileWidth = 80.0f;
	TileStyle.TileSpacing = 0.0f;
	Tiles->SetStyle(TileStyle);
	Tiles->SetItemObjects(DreamListsInteraction::MakeItems(40));
	Tiles->SetSelectionMode(EUIListSelectionMode::Multi);
	Rig.PumpFrames(2);
	if (!TestEqual(TEXT("Four tiles fit across"), Tiles->GetColumnCount(), 4)
		|| !TestTrue(TEXT("A click on the second tile completes"), Rig.Driver()->Find(RowOf(*Tiles, 1))->Click()))
	{
		return false;
	}

	TestTrue(TEXT("Shift+Down completes"), PressKey(Rig, EKeys::Down, EDreamDriverModifierKeys::Shift));
	ExpectSelection(*this, *Tiles, TEXT("After Shift+Down"), { 1, 2, 3, 4, 5 });
	TestTrue(TEXT("The focus is on the tile below the first"), IsFocusOnRow(Rig, *Tiles, 5));
	TestTrue(TEXT("Shift+Right completes"), PressKey(Rig, EKeys::Right, EDreamDriverModifierKeys::Shift));
	ExpectSelection(*this, *Tiles, TEXT("After Shift+Right"), { 1, 2, 3, 4, 5, 6 });
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTreeMultiSelectShiftArrowTest,
	"DreamGUI.TreeView.AShiftRangeInMultiModeSelectsTheRowsThatShowAndNotTheFoldedOnesBetweenThem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamTreeMultiSelectShiftArrowTest, "DreamGUI.TreeView.AShiftRangeInMultiModeSelectsTheRowsThatShowAndNotTheFoldedOnesBetweenThem", "[Nav][Animated]")

/*
 * STreeView ranges over its linearized items -- the rows that show. Fruit (Apple, Pear) folded, then Veg (Leek): Shift+Down
 * twice from Fruit selects Fruit, Veg and Leek, and neither of Fruit's folded children.
 */
bool FDreamTreeMultiSelectShiftArrowTest::RunTest(const FString& Parameters)
{
	using namespace DreamListMultiSelectInteractionTestLocal;
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
	Tree->SetItemsWithDepths(
		TArray<FText>{ FText::AsCultureInvariant(TEXT("Fruit")), FText::AsCultureInvariant(TEXT("Apple")), FText::AsCultureInvariant(TEXT("Pear")),
			FText::AsCultureInvariant(TEXT("Veg")), FText::AsCultureInvariant(TEXT("Leek")) },
		TArray<int32>{ 0, 1, 1, 0, 1 });
	// After the source, which drops every fold (a text source has no identity to carry one).
	Tree->SetCollapsedItems(TSet<int32>{ 0 });
	Tree->SetSelectionMode(EUIListSelectionMode::Multi);
	Rig.PumpFrames(2);
	if (!TestEqual(TEXT("Fruit is folded, so three rows show"), Tree->GetRowCount(), 3)
		|| !TestTrue(TEXT("A click on Fruit's row completes"), Rig.Driver()->Find(RowOf(*Tree, 0))->Click()))
	{
		return false;
	}

	TestTrue(TEXT("Shift+Down completes"), PressKey(Rig, EKeys::Down, EDreamDriverModifierKeys::Shift));
	ExpectSelection(*this, *Tree, TEXT("After Shift+Down from Fruit"), { 0, 3 });
	TestTrue(TEXT("Shift+Down again completes"), PressKey(Rig, EKeys::Down, EDreamDriverModifierKeys::Shift));
	ExpectSelection(*this, *Tree, TEXT("After a second Shift+Down"), { 0, 3, 4 });
	TestFalse(TEXT("Fruit is still folded"), Tree->IsItemExpanded(0));
	return true;
}

#endif
