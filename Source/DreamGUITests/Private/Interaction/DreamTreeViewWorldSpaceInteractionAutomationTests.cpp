// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamButton.h"
#include "Controls/DreamTreeView.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamEventSystem.h"
#include "InputCoreTypes.h"

#include "Driver/DreamDriverRig.h"
#include "Interaction/DreamControlsConfigInteractionTestHelpers.h"
#include "Interaction/DreamControlsWorldSpaceInteractionTestHelpers.h"
#include "Interaction/DreamListsConfigInteractionTestHelpers.h"
#include "Interaction/DreamListsInteractionTestTypes.h"

/*
 * A TREE ON A PANEL IN THE LEVEL.
 *
 * UMG's tree is STreeView: an SListView of the rows that show, each an STableRow with an SExpanderArrow -- a button of its own
 * -- for a twisty. In a UWidgetComponent each row and each twisty is hit where the interaction's ray -- the mouse's, or a
 * finger's pointer index's -- lands on the component's window, and the D-pad walks the rows that show in the order they show,
 * scrolling the row it reaches into the window (SListView::NavigationSelect, RequestNavigateToItem), the window standing in the
 * level.
 *
 * Rows forty units tall with nothing between them.
 */
namespace DreamTreeViewWorldTestLocal
{
	constexpr float RowHeight = 40.0f;

	/** A tree InHeight tall at InPosition on InPanel (panel units, Y up) with rows RowHeight apart and nothing in it yet. */
	UDreamTreeView* MakeTree(FDreamDriverRig& InRig, UDreamWidget* InPanel, double InHeight, const FVector2D& InPosition)
	{
		UDreamTreeView* Tree = InRig.MakeControl<UDreamTreeView>(TEXT("Tree"), InPanel, FVector2D(300.0, InHeight), InPosition);
		if (Tree == nullptr)
		{
			return nullptr;
		}
		Tree->SetStyleSource(EDreamUIStyleSource::Inline);
		FDreamTreeViewStyle TreeStyle = Tree->GetStyle();
		TreeStyle.List = DreamListsInteraction::WithRows(TreeStyle.List, RowHeight);
		Tree->SetStyle(TreeStyle);
		if (InRig.EventSystem() != nullptr)
		{
			InRig.EventSystem()->SetDoubleClickTime(0.0f);
		}
		return Tree;
	}

	/** Fruit (Apple, Pear) folded, then Veg (Leek): three rows on show -- Fruit, Veg, Leek. */
	void FillProduce(FDreamDriverRig& InRig, UDreamTreeView& InTree)
	{
		InTree.SetItemsWithDepths(
			TArray<FText>{ FText::AsCultureInvariant(TEXT("Fruit")), FText::AsCultureInvariant(TEXT("Apple")), FText::AsCultureInvariant(TEXT("Pear")),
				FText::AsCultureInvariant(TEXT("Veg")), FText::AsCultureInvariant(TEXT("Leek")) },
			TArray<int32>{ 0, 1, 1, 0, 1 });
		InTree.SetCollapsedItems(TSet<int32>{ 0 });
		InRig.PumpFrames(2);
	}

	/** Ten groups of a parent and two children, all open: thirty rows. */
	void FillGroups(FDreamDriverRig& InRig, UDreamTreeView& InTree)
	{
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
		InTree.SetItemsWithDepths(Labels, Depths);
		InRig.PumpFrames(2);
	}

	UDreamWidget* TwistyOf(const UDreamTreeView& InTree, int32 InItemIndex)
	{
		UDreamWidget* Row = InTree.GetRowWidget(InItemIndex);
		return Row != nullptr ? Row->FindChildByDisplayName(TEXT("Twisty")) : nullptr;
	}

	/** Whether player 0's focus is on item InItemIndex's row, and that row is seen inside InWindow, to within a pixel and a half. */
	bool IsFocusOnRowSeenInside(const FDreamDriverRig& InRig, const UDreamListViewBase& InTree, int32 InItemIndex, const FBox2D& InWindow)
	{
		const UDreamWidget* Row = InTree.GetRowWidget(InItemIndex);
		const TOptional<FBox2D> Rect = DreamControlsWorldTest::SeenRect(InRig, Row);
		return Row != nullptr && DreamControlsConfigTest::FocusIsIn(InRig, Row) && Row->GetRenderVisibleInHierarchy() && Rect.IsSet()
			&& Rect->Min.Y >= InWindow.Min.Y - 1.5 && Rect->Max.Y <= InWindow.Max.Y + 1.5;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTreeViewWorldPressTest,
	"DreamGUI.TreeView.OnAWorldPanelAClickOrATapReachesTheRowOrTwistySeenUnderIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamTreeViewWorldPressTest, "DreamGUI.TreeView.OnAWorldPanelAClickOrATapReachesTheRowOrTwistySeenUnderIt", "[Pointer][Touch][World]")

/*
 * Fruit folded and Veg open, on a panel three metres ahead. As the camera sees them: a click three pixels above Veg's bottom
 * edge chooses Veg; a tap three pixels inside the bottom-right corner of Fruit's twisty opens Fruit and leaves Veg chosen; a
 * click three pixels below Apple's top edge -- Apple showing now, under Fruit -- chooses Apple; and a tap three pixels above
 * Leek's bottom edge chooses Leek.
 */
bool FDreamTreeViewWorldPressTest::RunTest(const FString& Parameters)
{
	using namespace DreamTreeViewWorldTestLocal;
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
	UDreamTreeView* Tree = MakeTree(Rig, Stage.Panel, 280.0, FVector2D(-60.0, 0.0));
	if (!TestNotNull(TEXT("A tree came up on the panel"), Tree))
	{
		return false;
	}
	FillProduce(Rig, *Tree);
	const TOptional<FBox2D> Veg = SeenRect(Rig, Tree->GetRowWidget(3));
	const TOptional<FBox2D> Twisty = SeenRect(Rig, TwistyOf(*Tree, 0));
	if (!TestTrue(TEXT("The camera sees Veg's row and Fruit's twisty"), Veg.IsSet() && Twisty.IsSet()))
	{
		return false;
	}
	TestTrue(FString::Printf(TEXT("The twisty is seen at its size in panel units (%s)"), *Describe(Twisty.GetValue())),
		IsSeenAtItsSize(Twisty.GetValue(), TwistyOf(*Tree, 0)->GetSize()));

	TestTrue(TEXT("A click just above Veg's bottom edge completes"), ClickAt(Rig, FVector2D(Veg->GetCenter().X, Veg->Max.Y - 3.0)));
	TestEqual(TEXT("It chose Veg"), Tree->GetSelectedIndex(), 3);
	TestTrue(TEXT("A tap just inside the twisty's bottom-right corner completes"), TapAt(Rig, InsideBottomRight(Twisty.GetValue(), 3.0)));
	TestTrue(TEXT("It opened Fruit"), Tree->IsItemExpanded(0));
	TestEqual(TEXT("...and left Veg chosen"), Tree->GetSelectedIndex(), 3);
	Rig.PumpFrames(1);

	const TOptional<FBox2D> Apple = SeenRect(Rig, Tree->GetRowWidget(1));
	const TOptional<FBox2D> Leek = SeenRect(Rig, Tree->GetRowWidget(4));
	if (!TestTrue(TEXT("The camera sees Apple and Leek"), Apple.IsSet() && Leek.IsSet()))
	{
		return false;
	}
	TestTrue(TEXT("A click just below Apple's top edge completes"), ClickAt(Rig, FVector2D(Apple->GetCenter().X, Apple->Min.Y + 3.0)));
	TestEqual(TEXT("It chose Apple"), Tree->GetSelectedIndex(), 1);
	TestTrue(TEXT("A tap just above Leek's bottom edge completes"), TapAt(Rig, FVector2D(Leek->GetCenter().X, Leek->Max.Y - 3.0)));
	TestEqual(TEXT("It chose Leek"), Tree->GetSelectedIndex(), 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTreeViewWorldPadTest,
	"DreamGUI.TreeView.OnAWorldPanelTheDPadDownPastTheLastRowOnShowScrollsTheNextRowIntoTheWindowAsSeenWithTheRingOnIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamTreeViewWorldPadTest, "DreamGUI.TreeView.OnAWorldPanelTheDPadDownPastTheLastRowOnShowScrollsTheNextRowIntoTheWindowAsSeenWithTheRingOnIt", "[Nav][World]")

/*
 * A button above a tree on the panel -- ten groups of three, all open, thirty rows, in a window five rows tall -- the focus on
 * the button. D-pad down enters the tree at its first row; then D-pad down eight times: STreeView walks the rows that show in
 * the order they show, so every press is the next row, seen inside the window, the tree having scrolled it in once it was
 * below. The ring sits on the last row reached where the camera sees it, and every group the walk went through is still open.
 */
bool FDreamTreeViewWorldPadTest::RunTest(const FString& Parameters)
{
	using namespace DreamTreeViewWorldTestLocal;
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
	UDreamButton* Above = Rig.MakeControl<UDreamButton>(TEXT("Above"), Stage.Panel, FVector2D(160.0, 40.0), FVector2D(0.0, 115.0));
	UDreamTreeView* Tree = MakeTree(Rig, Stage.Panel, 5.0 * RowHeight, FVector2D(0.0, -40.0));
	if (!TestTrue(TEXT("A button and, below it, a tree came up on the panel"), Above != nullptr && Above->FaceNode != nullptr && Tree != nullptr))
	{
		return false;
	}
	FillGroups(Rig, *Tree);
	const TOptional<FBox2D> Window = SeenRect(Rig, Tree->ViewportNode.Get());
	if (!TestEqual(TEXT("Every group is open, so thirty rows show"), Tree->GetRowCount(), 30)
		|| !TestTrue(TEXT("The camera sees the tree's window"), Window.IsSet())
		|| !TestTrue(TEXT("The focus goes onto the button"), PutFocusOn(Rig, Above->FaceNode.Get())))
	{
		return false;
	}
	TestTrue(TEXT("D-pad down completes"), PressKey(Rig, EKeys::Gamepad_DPad_Down));
	if (!TestTrue(TEXT("D-pad down entered the tree at its first row"), IsFocusOnRowSeenInside(Rig, *Tree, 0, Window.GetValue())))
	{
		return false;
	}

	for (int32 Press = 1; Press <= 8; ++Press)
	{
		TestTrue(FString::Printf(TEXT("D-pad down %d completes"), Press), PressKey(Rig, EKeys::Gamepad_DPad_Down));
		DreamListsConfigTest::WaitUntilStill(Rig, *Tree);
		if (!TestTrue(FString::Printf(TEXT("After D-pad down %d the focus is on row %d, seen inside the window %s"), Press, Press,
			*Describe(Window.GetValue())), IsFocusOnRowSeenInside(Rig, *Tree, Press, Window.GetValue())))
		{
			return false;
		}
	}
	TestTrue(FString::Printf(TEXT("The tree scrolled to show the rows the pad went to (offset %.1f)"), Tree->GetScrollOffset()),
		Tree->GetScrollOffset() > 1.0f);
	TestTrue(TEXT("...and the groups the walk went through are still open"), Tree->IsItemExpanded(0) && Tree->IsItemExpanded(3) && Tree->IsItemExpanded(6));
	FString Where;
	const bool bRingSits = WaitForRingOnFocusAsSeen(Rig, Where);
	TestTrue(FString::Printf(TEXT("The ring sits on the last row reached where the camera sees it (%s)"), *Where), bRingSits);
	return true;
}

#endif
