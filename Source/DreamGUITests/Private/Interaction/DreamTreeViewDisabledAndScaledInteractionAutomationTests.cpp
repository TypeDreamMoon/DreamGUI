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
#include "Interaction/DreamListsConfigInteractionTestHelpers.h"
#include "Interaction/DreamListsInteractionTestTypes.h"

/*
 * A TREE DISABLED, AND ON A CANVAS SCALED TO HALF AND TO DOUBLE.
 *
 * UMG's tree is STreeView: an SListView of the rows that show, each an STableRow with an SExpanderArrow -- a button of its own
 * -- for a twisty. Disabled, the tree, its rows and its twisties are off the hit path (FHittestGrid::GetBubblePath,
 * SlateCore/Private/Input/HittestGrid.cpp:241) and passed over by navigation (FindFocusableWidget asks every parent,
 * HittestGrid.cpp:336, :471): no click chooses, no tap opens. On a scaled layer each row and each twisty is hit where it is
 * drawn, and the D-pad scrolls the row it reaches into the window as drawn (SListView::NavigationSelect, RequestNavigateToItem).
 *
 * Rows forty units tall with nothing between them.
 */
namespace DreamTreeViewConfigTestLocal
{
	constexpr float RowHeight = 40.0f;

	/** A tree InHeight tall at InPosition (canvas units, Y up) with rows RowHeight apart and nothing in it yet. */
	UDreamTreeView* MakeTree(FDreamDriverRig& InRig, double InHeight, const FVector2D& InPosition)
	{
		UDreamTreeView* Tree = InRig.MakeControl<UDreamTreeView>(TEXT("Tree"), nullptr, FVector2D(300.0, InHeight), InPosition);
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
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTreeViewDisabledPressTest,
	"DreamGUI.TreeView.ADisabledTreeIsChosenFromByNoClickOrTapAndItsTwistiesOpenNothingUntilItIsEnabledAgain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamTreeViewDisabledPressTest, "DreamGUI.TreeView.ADisabledTreeIsChosenFromByNoClickOrTapAndItsTwistiesOpenNothingUntilItIsEnabledAgain", "[Pointer][Touch][Disabled]")

/*
 * Fruit folded and Veg open, the tree disabled. A click on Veg's row and a tap on Leek's choose nothing; a click on Fruit's
 * twisty and a tap on it open nothing. Enabled again, a tap on Fruit's twisty opens Fruit and a click on Veg's row chooses it.
 */
bool FDreamTreeViewDisabledPressTest::RunTest(const FString& Parameters)
{
	using namespace DreamTreeViewConfigTestLocal;
	using namespace DreamControlsConfigTest;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize());
	Rig.BindTest(this);
	UDreamTreeView* Tree = Rig.IsUsable() ? MakeTree(Rig, 400.0, FVector2D::ZeroVector) : nullptr;
	if (!TestNotNull(TEXT("The rig and a tree came up"), Tree))
	{
		return false;
	}
	FillProduce(Rig, *Tree);
	UDreamWidget* Twisty = TwistyOf(*Tree, 0);
	if (!TestEqual(TEXT("Fruit is folded, so three rows show"), Tree->GetRowCount(), 3)
		|| !TestNotNull(TEXT("Fruit's row has a twisty"), Twisty))
	{
		return false;
	}
	Tree->SetIsEnabled(false);
	Rig.PumpFrames(1);
	TStrongObjectPtr<UDreamListsInteractionProbe> Probe(NewObject<UDreamListsInteractionProbe>());
	DreamListsInteraction::ListenToList(*Tree, *Probe);
	Tree->OnItemExpansionChanged.AddDynamic(Probe.Get(), &UDreamListsInteractionProbe::RecordExpansionChanged);
	FDreamElementRef TwistyElement = Rig.Driver()->Find(FDreamBy::Widget(Twisty));

	TestTrue(TEXT("Clicking Veg's row completes"), Rig.Driver()->Find(FDreamBy::Widget(Tree->GetRowWidget(3)))->Click());
	TestTrue(TEXT("Tapping Leek's row completes"), Rig.Driver()->Find(FDreamBy::Widget(Tree->GetRowWidget(4)))->Tap());
	TestTrue(TEXT("Clicking Fruit's twisty completes"), TwistyElement->Click());
	TestTrue(TEXT("Tapping it completes"), TwistyElement->Tap());
	TestEqual(TEXT("Nothing was chosen"), Tree->GetNumItemsSelected(), 0);
	TestEqual(TEXT("...no row was clicked"), Probe->ClickedItems.Num(), 0);
	TestFalse(TEXT("...and Fruit is still folded"), Tree->IsItemExpanded(0));
	TestEqual(TEXT("...having announced nothing"), Probe->ExpansionItems.Num(), 0);

	Tree->SetIsEnabled(true);
	Rig.PumpFrames(1);
	TestTrue(TEXT("Tapping Fruit's twisty with the tree enabled again completes"), TwistyElement->Tap());
	TestTrue(TEXT("Enabled again, the tap opens Fruit"), Tree->IsItemExpanded(0));
	TestTrue(TEXT("Clicking Veg's row completes"), Rig.Driver()->Find(FDreamBy::Widget(Tree->GetRowWidget(3)))->Click());
	TestEqual(TEXT("...and a click chooses Veg"), Tree->GetSelectedIndex(), 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTreeViewDisabledPadTest,
	"DreamGUI.TreeView.TabAndTheDPadPassOverADisabledTreeAndOnceEnabledTabEntersItAtItsFirstRow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamTreeViewDisabledPadTest, "DreamGUI.TreeView.TabAndTheDPadPassOverADisabledTreeAndOnceEnabledTabEntersItAtItsFirstRow", "[Nav][Disabled]")

/*
 * A button above a disabled tree and one below it, the focus on the top button. Tab passes over the tree to the bottom button,
 * and D-pad up comes back over it. Enabled again, Tab from the top button enters the tree at its first row, Fruit, and D-pad
 * down goes to the next row that shows, Veg.
 */
bool FDreamTreeViewDisabledPadTest::RunTest(const FString& Parameters)
{
	using namespace DreamTreeViewConfigTestLocal;
	using namespace DreamControlsConfigTest;
	FFocusDrawnByKeys Settings;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize());
	Rig.BindTest(this);
	UDreamButton* Above = Rig.IsUsable() ? Rig.MakeControl<UDreamButton>(TEXT("Above"), nullptr, FVector2D(160.0, 40.0), FVector2D(0.0, 260.0)) : nullptr;
	UDreamTreeView* Tree = Rig.IsUsable() ? MakeTree(Rig, 400.0, FVector2D::ZeroVector) : nullptr;
	UDreamButton* Below = Rig.IsUsable() ? Rig.MakeControl<UDreamButton>(TEXT("Below"), nullptr, FVector2D(160.0, 40.0), FVector2D(0.0, -260.0)) : nullptr;
	if (!TestTrue(TEXT("A button, a tree and a button came up"), Above != nullptr && Tree != nullptr && Below != nullptr
		&& Above->FaceNode != nullptr && Below->FaceNode != nullptr))
	{
		return false;
	}
	FillProduce(Rig, *Tree);
	Tree->SetIsEnabled(false);
	Rig.PumpFrames(1);
	if (!TestTrue(TEXT("The focus goes onto the top button"), PutFocusOn(Rig, Above->FaceNode.Get())))
	{
		return false;
	}

	TestTrue(TEXT("Tab completes"), Rig.Driver()->Sequence().Tab().WaitFrames(1).Perform());
	TestTrue(TEXT("Tab passes over the disabled tree to the bottom button"), FocusIsIn(Rig, Below));
	TestTrue(TEXT("D-pad up completes"), PressKey(Rig, EKeys::Gamepad_DPad_Up));
	TestTrue(TEXT("D-pad up passes back over it to the top button"), FocusIsIn(Rig, Above));

	Tree->SetIsEnabled(true);
	Rig.PumpFrames(1);
	TestTrue(TEXT("Tab completes once the tree is enabled"), Rig.Driver()->Sequence().Tab().WaitFrames(1).Perform());
	TestTrue(TEXT("Enabled again, Tab enters the tree at Fruit"), Tree->GetRowWidget(0) != nullptr && FocusIsIn(Rig, Tree->GetRowWidget(0)));
	TestTrue(TEXT("D-pad down completes"), PressKey(Rig, EKeys::Gamepad_DPad_Down));
	TestTrue(TEXT("...and D-pad down goes to Veg, the next row that shows"), Tree->GetRowWidget(3) != nullptr && FocusIsIn(Rig, Tree->GetRowWidget(3)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTreeViewScaledPressTest,
	"DreamGUI.TreeView.OnACanvasScaledToHalfAndToDoubleAClickOrATapReachesTheRowOrTwistyDrawnUnderIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamTreeViewScaledPressTest, "DreamGUI.TreeView.OnACanvasScaledToHalfAndToDoubleAClickOrATapReachesTheRowOrTwistyDrawnUnderIt", "[Pointer][Touch][Scaled]")

/*
 * Fruit folded and Veg open, off the middle, at each scale. A click three pixels above Veg's drawn bottom edge chooses Veg. A
 * tap three pixels inside the bottom-right corner of Fruit's twisty, as drawn, opens Fruit and leaves Veg chosen. A click three
 * pixels below Apple's drawn top edge -- Apple showing now, under Fruit -- chooses Apple, and a tap three pixels above Leek's
 * drawn bottom edge chooses Leek.
 */
bool FDreamTreeViewScaledPressTest::RunTest(const FString& Parameters)
{
	using namespace DreamTreeViewConfigTestLocal;
	using namespace DreamControlsConfigTest;
	for (const double Scale : Scales())
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(ScaledOptions(Scale));
		Rig.BindTest(this);
		if (!CameUpAt(*this, Rig, Scale))
		{
			return false;
		}
		UDreamTreeView* Tree = MakeTree(Rig, 400.0 / Scale, FVector2D(-100.0, 0.0));
		if (!TestNotNull(*At(Scale, TEXT("A tree came up")), Tree))
		{
			return false;
		}
		FillProduce(Rig, *Tree);
		const TOptional<FBox2D> Veg = DreamListsConfigTest::RowRect(*Tree, 3);
		const TOptional<FBox2D> Twisty = DrawnRect(TwistyOf(*Tree, 0));
		if (!TestTrue(At(Scale, TEXT("Veg's row and Fruit's twisty are drawn")), Veg.IsSet() && Twisty.IsSet()))
		{
			return false;
		}
		TestTrue(At(Scale, FString::Printf(TEXT("The twisty is drawn at the canvas's scale (%s)"), *Describe(Twisty.GetValue()))),
			IsDrawnAtScale(Twisty.GetValue(), TwistyOf(*Tree, 0)->GetSize(), Scale));

		TestTrue(At(Scale, TEXT("A click just above Veg's bottom edge completes")), ClickAt(Rig, FVector2D(Veg->GetCenter().X, Veg->Max.Y - 3.0)));
		TestEqual(*At(Scale, TEXT("It chose Veg")), Tree->GetSelectedIndex(), 3);
		TestTrue(At(Scale, TEXT("A tap just inside the twisty's bottom-right corner completes")), TapAt(Rig, InsideBottomRight(Twisty.GetValue(), 3.0)));
		TestTrue(At(Scale, TEXT("It opened Fruit")), Tree->IsItemExpanded(0));
		TestEqual(*At(Scale, TEXT("...and left Veg chosen")), Tree->GetSelectedIndex(), 3);
		Rig.PumpFrames(1);

		const TOptional<FBox2D> Apple = DreamListsConfigTest::RowRect(*Tree, 1);
		const TOptional<FBox2D> Leek = DreamListsConfigTest::RowRect(*Tree, 4);
		if (!TestTrue(At(Scale, TEXT("Apple and Leek are drawn")), Apple.IsSet() && Leek.IsSet()))
		{
			return false;
		}
		TestTrue(At(Scale, TEXT("A click just below Apple's top edge completes")), ClickAt(Rig, FVector2D(Apple->GetCenter().X, Apple->Min.Y + 3.0)));
		TestEqual(*At(Scale, TEXT("It chose Apple")), Tree->GetSelectedIndex(), 1);
		TestTrue(At(Scale, TEXT("A tap just above Leek's bottom edge completes")), TapAt(Rig, FVector2D(Leek->GetCenter().X, Leek->Max.Y - 3.0)));
		TestEqual(*At(Scale, TEXT("It chose Leek")), Tree->GetSelectedIndex(), 4);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTreeViewScaledPadTest,
	"DreamGUI.TreeView.OnACanvasScaledToHalfAndToDoubleTheDPadDownPastTheLastRowOnShowScrollsTheNextRowIntoTheDrawnWindowWithTheRingOnIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamTreeViewScaledPadTest, "DreamGUI.TreeView.OnACanvasScaledToHalfAndToDoubleTheDPadDownPastTheLastRowOnShowScrollsTheNextRowIntoTheDrawnWindowWithTheRingOnIt", "[Nav][Scaled]")

/*
 * Ten groups of three, all open -- thirty rows -- in a window 300 pixels tall at either scale. Tab enters the tree at its first
 * row; then D-pad down eighteen times: STreeView walks the rows that show in the order they show, so every press is the next
 * row, drawn inside the window as drawn, the tree having scrolled it in once it was below. The ring sits on the last row
 * reached where it is drawn, and every group the walk went through is still open.
 */
bool FDreamTreeViewScaledPadTest::RunTest(const FString& Parameters)
{
	using namespace DreamTreeViewConfigTestLocal;
	using namespace DreamControlsConfigTest;
	FFocusDrawnByKeys Settings;
	for (const double Scale : Scales())
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(ScaledOptions(Scale));
		Rig.BindTest(this);
		if (!CameUpAt(*this, Rig, Scale))
		{
			return false;
		}
		UDreamTreeView* Tree = MakeTree(Rig, 300.0 / Scale, FVector2D(100.0, 0.0));
		if (!TestNotNull(*At(Scale, TEXT("A tree came up")), Tree))
		{
			return false;
		}
		FillGroups(Rig, *Tree);
		const TOptional<FBox2D> Window = DreamListsConfigTest::WindowRect(*Tree);
		if (!TestEqual(*At(Scale, TEXT("Every group is open, so thirty rows show")), Tree->GetRowCount(), 30)
			|| !TestTrue(At(Scale, TEXT("The tree's window is on the viewport")), Window.IsSet()))
		{
			return false;
		}
		TestTrue(At(Scale, TEXT("Tab completes")), Rig.Driver()->Sequence().Tab().WaitFrames(1).Perform());
		if (!TestTrue(At(Scale, TEXT("Tab entered the tree at its first row")), DreamListsConfigTest::IsFocusOnRowInside(Rig, *Tree, 0, Window.GetValue())))
		{
			return false;
		}

		for (int32 Press = 1; Press <= 18; ++Press)
		{
			TestTrue(At(Scale, FString::Printf(TEXT("D-pad down %d completes"), Press)), PressKey(Rig, EKeys::Gamepad_DPad_Down));
			DreamListsConfigTest::WaitUntilStill(Rig, *Tree);
			if (!TestTrue(At(Scale, FString::Printf(TEXT("After D-pad down %d the focus is on row %d, drawn inside the window %s"), Press, Press,
				*Describe(Window.GetValue()))), DreamListsConfigTest::IsFocusOnRowInside(Rig, *Tree, Press, Window.GetValue())))
			{
				return false;
			}
		}
		TestTrue(At(Scale, FString::Printf(TEXT("The tree scrolled to show the rows the pad went to (offset %.1f)"), Tree->GetScrollOffset())),
			Tree->GetScrollOffset() > 1.0f);
		TestTrue(At(Scale, TEXT("...and the groups the walk went through are still open")), Tree->IsItemExpanded(0) && Tree->IsItemExpanded(15));
		FString Where;
		const bool bRingSits = WaitForRingOnFocus(Rig, Where);
		TestTrue(At(Scale, FString::Printf(TEXT("The ring sits on the last row reached where it is drawn (%s)"), *Where)), bRingSits);
	}
	return true;
}

#endif
