// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Controls/DreamButton.h"
#include "Controls/DreamListView.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamWidget.h"
#include "Interaction/UIButton.h"
#include "UObject/StrongObjectPtr.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Interaction/DreamListsInteractionTestTypes.h"
#include "Interaction/DreamPressInteractionTestTypes.h"

/*
 * WHAT A ROW SHOWS BELONGS TO ITS ITEM, AND WHAT IS INSIDE A ROW IS ITS OWN.
 *
 * SListView recycles its row widgets as it scrolls -- a row is handed whichever item comes into the window next -- and a
 * row draws itself selected by asking its list whether the item it stands for is selected (STableRow::IsItemSelected,
 * SObjectTableRow's style for the selected state). So the selected look goes with the item: a row that showed a chosen
 * item and now shows another is drawn unchosen, and the chosen item is drawn chosen again by whichever row shows it next.
 *
 * And a button inside a row is a widget of its own: it takes the press it is pressed with (SButton::OnMouseButtonDown
 * answers Handled with the capture), so STableRow::OnMouseButtonDown never sees it and the row is neither chosen nor
 * clicked. The tree's twisty is the same arrangement, and the list's must behave as it does.
 */
namespace DreamListRowStateInteractionTestLocal
{
	constexpr float RowHeight = 40.0f;
	const FVector2D ListSize(300.0, 400.0);
	const FColor RestingLook(40, 40, 40, 255);
	const FColor ChosenLook(200, 40, 40, 255);

	/** A list whose rows rest in RestingLook and are drawn chosen in ChosenLook, without stripes. */
	UDreamListView* MakeList(FDreamDriverRig& InRig, int32 InItemCount)
	{
		UDreamListView* List = InRig.MakeControl<UDreamListView>(TEXT("List"), nullptr, ListSize);
		if (List == nullptr)
		{
			return nullptr;
		}
		List->SetStyleSource(EDreamUIStyleSource::Inline);
		FDreamListStyle Style = DreamListsInteraction::WithRows(List->GetStyle(), RowHeight);
		Style.RowNormal = RestingLook;
		Style.RowSelected = ChosenLook;
		List->SetStyle(Style);
		List->SetAlternatingRowColors(false);
		List->SetItemObjects(DreamListsInteraction::MakeItems(InItemCount));
		InRig.PumpFrames(2);
		return List;
	}

	/** The colour a row rests in: the one it is drawn with whenever no pointer is on it. */
	TOptional<FColor> RestingColourOf(const UDreamWidget* InRow)
	{
		const UUIButton* RowButton = InRow != nullptr ? InRow->GetComponent<UUIButton>() : nullptr;
		return RowButton != nullptr ? TOptional<FColor>(RowButton->GetNormalColor()) : TOptional<FColor>();
	}

	/**
	 * Every row widget the list holds, checked against what its item is: drawn chosen exactly when the item it stands for
	 * is selected. Said row by row, so a failure names the item.
	 */
	void ExpectEveryRowLooksAsItsItemIs(FAutomationTestBase& InTest, const UDreamListView& InList, const TCHAR* InWhen)
	{
		int32 Checked = 0;
		for (int32 PoolIndex = 0; PoolIndex < InList.RowNodes.Num(); ++PoolIndex)
		{
			const int32 ItemIndex = InList.GetRowItemIndex(PoolIndex);
			const TOptional<FColor> Resting = RestingColourOf(InList.RowNodes[PoolIndex]);
			if (ItemIndex == INDEX_NONE || !Resting.IsSet())
			{
				continue;
			}
			++Checked;
			const FColor Expected = InList.IsItemSelected(ItemIndex) ? ChosenLook : RestingLook;
			if (Resting.GetValue() != Expected)
			{
				InTest.AddError(FString::Printf(TEXT("%s: the row showing item %d rests in %s, where its item says %s"), InWhen, ItemIndex,
					*Resting.GetValue().ToString(), *Expected.ToString()));
			}
		}
		InTest.TestTrue(FString::Printf(TEXT("%s: there were rows to look at"), InWhen), Checked > 0);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamListRecycledRowLookTest,
	"DreamGUI.ListView.ARowRecycledForAnotherItemDoesNotKeepTheChosenLookOfTheItemItShowedBefore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamListRecycledRowLookTest, "DreamGUI.ListView.ARowRecycledForAnotherItemDoesNotKeepTheChosenLookOfTheItemItShowedBefore", "[Pointer][Animated]")

/*
 * A hundred items in a recycling list, the third clicked and so chosen, then the wheel twelve rows down -- far enough
 * that the third item's row has gone round to an item arriving at the bottom -- and twelve back. Every row the list holds
 * is drawn as its item is at each stop: unchosen for every item on show down the list, the row that now shows the third
 * item chosen again on the way back. The choice itself never moved.
 */
bool FDreamListRecycledRowLookTest::RunTest(const FString& Parameters)
{
	using namespace DreamListRowStateInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(DreamListsInteraction::ViewportSize());
	Rig.BindTest(this);
	UDreamListView* List = Rig.IsUsable() ? MakeList(Rig, 100) : nullptr;
	if (!TestNotNull(TEXT("The rig and a list came up"), List))
	{
		return false;
	}
	List->SetVirtualizationThreshold(10);
	Rig.PumpFrames(2);
	if (!TestTrue(TEXT("The list is recycling its rows"), List->IsVirtualizing() && List->GetRealizedRowCount() < 100))
	{
		return false;
	}
	UDreamWidget* ThirdRow = List->GetRowWidget(2);
	TestTrue(TEXT("Clicking the third row completes"), Rig.Driver()->Find(FDreamBy::Widget(ThirdRow))->Click());
	if (!TestEqual(TEXT("...and chooses it"), List->GetSelectedIndex(), 2))
	{
		return false;
	}
	ExpectEveryRowLooksAsItsItemIs(*this, *List, TEXT("After the click"));
	TestEqual(TEXT("The third item's row is drawn chosen"), RestingColourOf(ThirdRow).Get(FColor::Black), ChosenLook);

	FDreamElementRef ListElement = Rig.Driver()->Find(FDreamBy::Widget(List));
	TestTrue(TEXT("Twelve notches down the list complete"), ListElement->ScrollBy(FVector2D(-12.0, -12.0)));
	Rig.PumpFrames(3);
	TestNull(TEXT("The third item is out of the window, its row gone round to another item"), List->GetRowWidget(2));
	ExpectEveryRowLooksAsItsItemIs(*this, *List, TEXT("Twelve rows down"));
	if (ThirdRow != nullptr && List->RowNodes.Contains(ThirdRow))
	{
		const int32 ShownNow = List->GetRowItemIndex(List->RowNodes.IndexOfByKey(ThirdRow));
		TestNotEqual(TEXT("The widget that showed the third item shows another now"), ShownNow, 2);
		TestEqual(TEXT("...and is drawn unchosen"), RestingColourOf(ThirdRow).Get(FColor::Black), RestingLook);
	}

	TestTrue(TEXT("Twelve notches back up complete"), ListElement->ScrollBy(FVector2D(12.0, 12.0)));
	Rig.PumpFrames(3);
	UDreamWidget* ShowingThirdAgain = List->GetRowWidget(2);
	if (TestNotNull(TEXT("The third item is on show again"), ShowingThirdAgain))
	{
		TestEqual(TEXT("...drawn chosen by whichever row shows it now"), RestingColourOf(ShowingThirdAgain).Get(FColor::Black), ChosenLook);
	}
	ExpectEveryRowLooksAsItsItemIs(*this, *List, TEXT("Back at the top"));
	TestEqual(TEXT("The choice is still the third item"), List->GetSelectedIndex(), 2);
	TestEqual(TEXT("...and only it"), List->GetNumItemsSelected(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamListRowInnerButtonTest,
	"DreamGUI.ListView.AClickOnAButtonInsideARowPressesTheButtonAndLeavesTheRowUnchosen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamListRowInnerButtonTest, "DreamGUI.ListView.AClickOnAButtonInsideARowPressesTheButtonAndLeavesTheRowUnchosen", "[Pointer][Animated]")

/*
 * An entry widget with a button in it -- a delete button on an inventory row -- is the ordinary UListView row. The button
 * takes its click (SButton handles the press with the capture), the row under it is never pressed, so nothing is chosen and
 * OnItemClicked does not fire. A click on the same row beside the button is the row's: it chooses the row and reports the
 * click, and the button hears nothing of it.
 */
bool FDreamListRowInnerButtonTest::RunTest(const FString& Parameters)
{
	using namespace DreamListRowStateInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(DreamListsInteraction::ViewportSize());
	Rig.BindTest(this);
	UDreamListView* List = Rig.IsUsable() ? MakeList(Rig, 8) : nullptr;
	UDreamWidget* Row = List != nullptr ? List->GetRowWidget(2) : nullptr;
	if (!TestNotNull(TEXT("The rig, a list and its third row came up"), Row))
	{
		return false;
	}
	// At the row's right end, well clear of its middle. A row is an overlay (its label sits in an overlay slot), and an
	// overlay places its children by their slots, not by their anchored positions: the button is put at the right end the
	// way an entry widget's author puts it there in UMG, with its overlay slot's alignment (UOverlaySlot's
	// HorizontalAlignment), at the size it asks for there. Left at the slot's Fill, it would cover the whole row.
	const FVector2D ButtonSize(60.0, 30.0);
	UDreamButton* RowAction = Rig.MakeControl<UDreamButton>(TEXT("RowAction"), Row, ButtonSize);
	UDreamPanelSlot* RowActionSlot = RowAction != nullptr ? RowAction->GetPanelSlot() : nullptr;
	if (!TestNotNull(TEXT("A button can be put inside the row, in a slot of the row's overlay"), RowActionSlot))
	{
		return false;
	}
	RowActionSlot->SetHorizontalAlignment(EDreamPanelHorizontalAlignment::Right);
	RowActionSlot->SetVerticalAlignment(EDreamPanelVerticalAlignment::Center);
	TStrongObjectPtr<UDreamPressInteractionListener> ButtonListener(NewObject<UDreamPressInteractionListener>());
	RowAction->OnClicked.AddDynamic(ButtonListener.Get(), &UDreamPressInteractionListener::HandleClicked);
	TStrongObjectPtr<UDreamListsInteractionProbe> Probe(NewObject<UDreamListsInteractionProbe>());
	DreamListsInteraction::ListenToList(*List, *Probe);
	Rig.PumpFrames(2);
	// The row's own part, a quarter of the way in from its left edge: where the second click goes.
	const TOptional<FBox2D> RowRect = Rig.Driver()->Find(FDreamBy::Widget(Row))->GetPixelRect();
	const TOptional<FBox2D> ActionRect = Rig.Driver()->Find(FDreamBy::Widget(RowAction))->GetPixelRect();
	if (!TestTrue(TEXT("The row and the button project to pixels"), RowRect.IsSet() && ActionRect.IsSet()))
	{
		return false;
	}
	const FVector2D BesideTheButton(RowRect->Min.X + RowRect->GetSize().X * 0.25, RowRect->GetCenter().Y);
	if (!TestTrue(FString::Printf(TEXT("The button sits in the row's right half, clear of the row's own part (%s in %s)"),
			*ActionRect->ToString(), *RowRect->ToString()),
		ActionRect->Min.X > RowRect->GetCenter().X && ActionRect->Max.X <= RowRect->Max.X + 0.5 && !ActionRect->IsInside(BesideTheButton)))
	{
		return false;
	}

	TestTrue(TEXT("Clicking the button inside the row completes"), Rig.Driver()->Find(FDreamBy::Widget(RowAction))->Click());
	TestEqual(TEXT("The button was clicked once"), ButtonListener->ClickedCount, 1);
	TestEqual(TEXT("...and the row under it was not chosen"), List->GetNumItemsSelected(), 0);
	TestEqual(TEXT("...nor clicked"), Probe->ClickedItems.Num(), 0);
	TestEqual(TEXT("...and the choice never changed"), Probe->SelectionChanges.Num(), 0);

	TestTrue(TEXT("Clicking the row beside the button completes"),
		Rig.Driver()->Sequence().WaitSeconds(1.0f).MoveToPixel(BesideTheButton).Press().Release().Perform());
	TestEqual(TEXT("That click chose the row"), List->GetSelectedIndex(), 2);
	TestEqual(TEXT("...and was reported as the row's click"), Probe->ClickedItems.Num(), 1);
	TestEqual(TEXT("...while the button heard nothing of it"), ButtonListener->ClickedCount, 1);
	return true;
}

#endif
