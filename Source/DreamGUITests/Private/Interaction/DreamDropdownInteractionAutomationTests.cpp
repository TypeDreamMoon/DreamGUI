// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamButton.h"
#include "Controls/DreamDropdown.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamEventSystem.h"
#include "Interaction/DreamUIPopupLayer.h"
#include "Interaction/UIDropdown.h"
#include "Interaction/UIScrollView.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Interaction/DreamPressInteractionTestTypes.h"

/*
 * UDreamDropdown, opened, chosen from and dismissed through the real pointer pipeline, and held to
 * UMG's UComboBoxString.
 *
 * The combo box is an SComboBox, which is an SComboButton over an SMenuAnchor: clicking the button
 * opens the menu (OnOpening fires as it does), clicking an item selects it (OnSelectionChanged with
 * the item) and dismisses the menu, and a click anywhere outside the menu dismisses it through the
 * menu stack without selecting anything. The dropdown's list is its own widget lifted to the screen
 * layer, as a popup on UDreamUIPopupLayer's per-player stack, and "outside" is a press that layer hears
 * before anything else does -- the mechanism is different, and the claims below are only about what
 * the player sees happen.
 *
 * The option rows are reached the way a consumer reaches them: OnItemGenerated hands each one over
 * as the list is built, and the listener keeps them by option index.
 */
namespace DreamPressDropdownTestLocal
{
	const FIntPoint ViewportSize(1280, 720);

	/**
	 * Well clear of the dropdown (which sits high on the screen) and of its list (which opens below
	 * it): the bottom-right corner of the viewport, where nothing but the rig's root is.
	 */
	const FVector2D FarFromTheDropdown(1200.0, 680.0);

	struct FPlacedDropdown
	{
		UDreamDropdown* Dropdown = nullptr;
		TSharedPtr<FDreamDriverElement> Element;

		bool IsReady() const { return Dropdown != nullptr && Element.IsValid() && Element->Exists(); }
	};

	TArray<FText> MakeOptions(int32 InCount)
	{
		TArray<FText> Options;
		for (int32 Index = 0; Index < InCount; ++Index)
		{
			Options.Add(FText::AsCultureInvariant(FString::Printf(TEXT("Option %d"), Index)));
		}
		return Options;
	}

	/**
	 * A dropdown near the top of the screen, so its list has room to open downward, holding
	 * InOptionCount options with the first selected, its events bound to InListener, laid out and
	 * found by the driver.
	 */
	FPlacedDropdown PlaceDropdown(FAutomationTestBase& InTest, FDreamDriverRig& InRig, UDreamPressInteractionListener* InListener,
		int32 InOptionCount)
	{
		FPlacedDropdown Placed;
		InRig.BindTest(&InTest);
		if (!InTest.TestTrue(TEXT("The headless rig came up"), InRig.IsUsable()))
		{
			return Placed;
		}
		UDreamDropdown* Dropdown = InRig.MakeControl<UDreamDropdown>(TEXT("Quality"), nullptr,
			FVector2D(200.0, 40.0), FVector2D(0.0, 200.0));
		if (!InTest.TestNotNull(TEXT("A dropdown can be made on the rig"), Dropdown))
		{
			return Placed;
		}
		Dropdown->SetOptions(MakeOptions(InOptionCount));
		Dropdown->SetSelectedIndex(0);
		Dropdown->OnSelectionChanged.AddDynamic(InListener, &UDreamPressInteractionListener::HandleSelectionChanged);
		Dropdown->OnOpening.AddDynamic(InListener, &UDreamPressInteractionListener::HandleOpening);
		Dropdown->OnClosed.AddDynamic(InListener, &UDreamPressInteractionListener::HandleClosed);
		Dropdown->OnItemGenerated.AddDynamic(InListener, &UDreamPressInteractionListener::HandleItemGenerated);
		InRig.PumpFrames(1);

		Placed.Dropdown = Dropdown;
		Placed.Element = InRig.Driver()->Find(FDreamBy::Widget(Dropdown));
		InTest.TestTrue(TEXT("The driver can find the dropdown it is about to act on"), Placed.IsReady());
		return Placed;
	}

	/** Click the dropdown open and let the list it built settle into place before anything aims at it. */
	bool OpenByClicking(FAutomationTestBase& InTest, FDreamDriverRig& InRig, const FPlacedDropdown& InPlaced)
	{
		InTest.TestTrue(TEXT("Clicking the dropdown completes"), InPlaced.Element->Click());
		// Two frames: the first lays out the rows the open just created, the second anything that
		// arranging them dirtied -- the same settling the rig itself does before its first action.
		InRig.PumpFrames(2);
		return InTest.TestTrue(TEXT("The click opened the list"), InPlaced.Dropdown->IsOpen());
	}

	/** Press and let go at a raw pixel, one frame each -- a click on nothing in particular. */
	bool ClickAtPixel(FDreamDriverRig& InRig, const FVector2D& InPixel)
	{
		return InRig.Driver()->Sequence().MoveToPixel(InPixel).Press().Release().Perform();
	}

	/** The scrolled column inside the list, which is where the rows are built. Null when there is none. */
	UDreamWidget* FindColumn(const UDreamDropdown* InDropdown)
	{
		return InDropdown->ListNode != nullptr ? InDropdown->ListNode->FindChildByDisplayName(TEXT("Column")) : nullptr;
	}

	/** How many rows the list holds: everything in the column but the template the rows are copied from. */
	int32 CountRows(const UDreamDropdown* InDropdown)
	{
		const UDreamWidget* Column = FindColumn(InDropdown);
		if (Column == nullptr)
		{
			return 0;
		}
		int32 Rows = 0;
		for (const UDreamWidget* Row : Column->GetChildren())
		{
			if (IsValid(Row) && Row != InDropdown->ItemTemplateNode.Get())
			{
				++Rows;
			}
		}
		return Rows;
	}

	/** Two options to replace the placed three with: a different count, and words that are easy to read back. */
	TArray<FText> TwoOtherOptions()
	{
		return { FText::AsCultureInvariant(TEXT("Windowed")), FText::AsCultureInvariant(TEXT("Fullscreen")) };
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPressDropdownOpenTest,
	"DreamGUI.Dropdown.ClickingTheDropdownOpensItsListAndSaysItIsOpening",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamPressDropdownOpenTest, "DreamGUI.Dropdown.ClickingTheDropdownOpensItsListAndSaysItIsOpening", "[Pointer][Animated]")

bool FDreamPressDropdownOpenTest::RunTest(const FString& Parameters)
{
	using namespace DreamPressDropdownTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FPlacedDropdown Placed = PlaceDropdown(*this, Rig, Listener.Get(), 3);
	if (!Placed.IsReady())
	{
		return false;
	}
	TestFalse(TEXT("The dropdown starts closed"), Placed.Dropdown->IsOpen());

	TestTrue(TEXT("Clicking the dropdown completes"), Placed.Element->Click());

	TestTrue(TEXT("One click opens the list"), Placed.Dropdown->IsOpen());
	TestEqual(TEXT("And OnOpening fired once, as UComboBoxString's does"), Listener->OpeningCount, 1);
	TestEqual(TEXT("Opening is not choosing"), Listener->SelectionIndices.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPressDropdownChooseTest,
	"DreamGUI.Dropdown.ClickingAnOptionInTheOpenListChoosesItAndClosesTheList",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamPressDropdownChooseTest, "DreamGUI.Dropdown.ClickingAnOptionInTheOpenListChoosesItAndClosesTheList", "[Pointer][Animated]")

bool FDreamPressDropdownChooseTest::RunTest(const FString& Parameters)
{
	using namespace DreamPressDropdownTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FPlacedDropdown Placed = PlaceDropdown(*this, Rig, Listener.Get(), 3);
	if (!Placed.IsReady() || !OpenByClicking(*this, Rig, Placed))
	{
		return false;
	}
	if (!TestTrue(TEXT("Opening the list generated a row for the second option"),
		Listener->GeneratedItems.IsValidIndex(1) && Listener->GeneratedItems[1] != nullptr))
	{
		return false;
	}

	FDreamElementRef SecondOption = Rig.Driver()->Find(FDreamBy::Widget(Listener->GeneratedItems[1].Get()));
	TestTrue(TEXT("Clicking the second option completes"), SecondOption->Click());

	TestEqual(TEXT("The second option is now the selection"), Placed.Dropdown->GetSelectedIndex(), 1);
	if (TestEqual(TEXT("And the change was announced once"), Listener->SelectionIndices.Num(), 1))
	{
		TestEqual(TEXT("Carrying the second option's index"), Listener->SelectionIndices[0], 1);
	}
	TestFalse(TEXT("Choosing closes the list, as a combo box's menu is dismissed on selection"), Placed.Dropdown->IsOpen());
	TestEqual(TEXT("And its closing was announced"), Listener->ClosedCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPressDropdownDismissTest,
	"DreamGUI.Dropdown.ClickingOutsideTheOpenListClosesItAndChoosesNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamPressDropdownDismissTest, "DreamGUI.Dropdown.ClickingOutsideTheOpenListClosesItAndChoosesNothing", "[Pointer][Animated]")

bool FDreamPressDropdownDismissTest::RunTest(const FString& Parameters)
{
	using namespace DreamPressDropdownTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FPlacedDropdown Placed = PlaceDropdown(*this, Rig, Listener.Get(), 3);
	if (!Placed.IsReady() || !OpenByClicking(*this, Rig, Placed))
	{
		return false;
	}

	TestTrue(TEXT("Clicking far from the dropdown and its list completes"), ClickAtPixel(Rig, FarFromTheDropdown));

	TestFalse(TEXT("A click outside the open list closes it"), Placed.Dropdown->IsOpen());
	TestEqual(TEXT("The selection is where it was"), Placed.Dropdown->GetSelectedIndex(), 0);
	TestEqual(TEXT("And no selection change was announced"), Listener->SelectionIndices.Num(), 0);
	return true;
}

/**
 * A disabled SComboButton is off the hit path (FHittestGrid::GetBubblePath drops disabled widgets),
 * so the click that would have opened the menu reaches nothing.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPressDropdownDisabledTest,
	"DreamGUI.Dropdown.ClickingADisabledDropdownDoesNotOpenIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamPressDropdownDisabledTest, "DreamGUI.Dropdown.ClickingADisabledDropdownDoesNotOpenIt", "[Pointer][Disabled]")

bool FDreamPressDropdownDisabledTest::RunTest(const FString& Parameters)
{
	using namespace DreamPressDropdownTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FPlacedDropdown Placed = PlaceDropdown(*this, Rig, Listener.Get(), 3);
	if (!Placed.IsReady())
	{
		return false;
	}
	Placed.Dropdown->SetIsEnabled(false);
	Rig.PumpFrames(1);

	TestTrue(TEXT("Clicking the disabled dropdown completes"), Placed.Element->Click());

	TestFalse(TEXT("A disabled dropdown stays closed"), Placed.Dropdown->IsOpen());
	TestEqual(TEXT("And never began opening"), Listener->OpeningCount, 0);
	return true;
}

/**
 * A combo box's menu is a scrolling list once it holds more than it shows: the wheel over the open
 * menu scrolls it, and scrolling is not choosing.
 *
 * Twenty options against the default six visible rows leaves fourteen rows' worth to scroll; three
 * notches down (a negative wheel axis, UMG's "towards the user") is well inside that.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPressDropdownWheelTest,
	"DreamGUI.Dropdown.TurningTheWheelOverTheOpenListScrollsItAndChoosesNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamPressDropdownWheelTest, "DreamGUI.Dropdown.TurningTheWheelOverTheOpenListScrollsItAndChoosesNothing", "[Pointer][Animated]")

bool FDreamPressDropdownWheelTest::RunTest(const FString& Parameters)
{
	using namespace DreamPressDropdownTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FPlacedDropdown Placed = PlaceDropdown(*this, Rig, Listener.Get(), 20);
	if (!Placed.IsReady() || !OpenByClicking(*this, Rig, Placed))
	{
		return false;
	}
	UUIScrollView* ListScroll = Placed.Dropdown->ListNode != nullptr
		? Placed.Dropdown->ListNode->GetComponent<UUIScrollView>()
		: nullptr;
	if (!TestNotNull(TEXT("The open list scrolls through a scroll view"), ListScroll))
	{
		return false;
	}
	const double OffsetBefore = ListScroll->GetScrollOffset().Y;

	FDreamElementRef List = Rig.Driver()->Find(FDreamBy::Widget(Placed.Dropdown->ListNode.Get()));
	TestTrue(TEXT("Turning the wheel over the open list completes"), List->ScrollBy(FVector2D(0.0, -3.0)));

	TestTrue(TEXT("The wheel moved the list on towards its later options"), ListScroll->GetScrollOffset().Y > OffsetBefore);
	TestTrue(TEXT("Scrolling leaves the list open"), Placed.Dropdown->IsOpen());
	TestEqual(TEXT("And the selection where it was"), Placed.Dropdown->GetSelectedIndex(), 0);
	TestEqual(TEXT("With no selection change announced"), Listener->SelectionIndices.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPressDropdownDestroyedOpenTest,
	"DreamGUI.Dropdown.DestroyingTheDropdownWithItsListOpenLeavesNothingOverTheScreen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamPressDropdownDestroyedOpenTest, "DreamGUI.Dropdown.DestroyingTheDropdownWithItsListOpenLeavesNothingOverTheScreen", "[Pointer][Animated]")

/*
 * A dropdown destroyed while its list was open took none of the list's furniture with it. The list,
 * lifted to the screen root, stayed up showing rows that called into the dead component, and whatever
 * closed it on an outside click -- once a full-screen blocker bound to the destroyed behaviour, now the
 * popup layer -- went on taking every click on the screen. The list is opened here, the dropdown
 * destroyed, and a button elsewhere on the screen clicked: the click reaches the button, nothing is
 * left open on the popup layer, and the list went with the dropdown.
 */
bool FDreamPressDropdownDestroyedOpenTest::RunTest(const FString& Parameters)
{
	using namespace DreamPressDropdownTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	TStrongObjectPtr<UDreamPressInteractionListener> ElsewhereListener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FPlacedDropdown Placed = PlaceDropdown(*this, Rig, Listener.Get(), 3);
	if (!Placed.IsReady())
	{
		return false;
	}
	// Low on the screen: clear of the dropdown, which sits high, and of the list that opens below it.
	UDreamButton* Elsewhere = Rig.MakeControl<UDreamButton>(TEXT("Elsewhere"), nullptr, FVector2D(160.0, 50.0), FVector2D(0.0, -250.0));
	if (!TestNotNull(TEXT("A button can be made elsewhere on the screen"), Elsewhere))
	{
		return false;
	}
	Elsewhere->OnClicked.AddDynamic(ElsewhereListener.Get(), &UDreamPressInteractionListener::HandleClicked);
	Rig.PumpFrames(1);
	if (!OpenByClicking(*this, Rig, Placed))
	{
		return false;
	}
	const UDreamUIPopupLayer* Popups = UDreamUIPopupLayer::Get(Rig.GetWorld());
	if (!TestTrue(TEXT("The open list is up on the popup layer"), Popups != nullptr && Popups->IsOpen(Placed.Dropdown->ListNode.Get())))
	{
		return false;
	}
	const TWeakObjectPtr<UDreamWidget> List(Placed.Dropdown->ListNode.Get());

	Placed.Dropdown->DestroyWidget();
	Rig.PumpFrames(1);

	TestNull(TEXT("Nothing is left open on the popup layer"), Popups->GetTopPopup(0));
	TestFalse(TEXT("And the list went with the dropdown rather than staying on the screen"), List.IsValid());
	TestTrue(TEXT("Clicking the button elsewhere completes"), Rig.Driver()->Find(FDreamBy::Widget(Elsewhere))->Click());
	TestEqual(TEXT("The click reached the button"), ElsewhereListener->ClickedCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPressDropdownRefreshOnOpeningTest,
	"DreamGUI.Dropdown.OptionsRefreshedAsTheListOpensAreTheRowsTheListShows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamPressDropdownRefreshOnOpeningTest, "DreamGUI.Dropdown.OptionsRefreshedAsTheListOpensAreTheRowsTheListShows", "[Pointer][Animated]")

/*
 * OnOpening is UMG's moment to refresh a combo box's options, and the header promises the options
 * written there are the ones the player sees. They were not: the rows had already been built from the
 * old options when OnOpening fired, and a new options push only marked them for the next open -- three
 * old rows squeezed into a list sized for the new count, each carrying an index the new options did not
 * have. Here a handler replaces three options with two as the list opens: the list holds two rows, and
 * clicking the second chooses the second of the new options.
 */
bool FDreamPressDropdownRefreshOnOpeningTest::RunTest(const FString& Parameters)
{
	using namespace DreamPressDropdownTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FPlacedDropdown Placed = PlaceDropdown(*this, Rig, Listener.Get(), 3);
	if (!Placed.IsReady())
	{
		return false;
	}
	UDreamDropdown* Dropdown = Placed.Dropdown;
	Listener->DuringOpening = [Dropdown]()
	{
		Dropdown->SetOptions(TwoOtherOptions());
	};
	if (!OpenByClicking(*this, Rig, Placed))
	{
		return false;
	}

	TestEqual(TEXT("The open list holds a row per option it was refreshed to"), CountRows(Dropdown), 2);
	if (!TestTrue(TEXT("The refresh generated a row for the second new option"),
		Listener->GeneratedItems.IsValidIndex(1) && IsValid(Listener->GeneratedItems[1])))
	{
		return false;
	}
	TestTrue(TEXT("Clicking that row completes"), Rig.Driver()->Find(FDreamBy::Widget(Listener->GeneratedItems[1].Get()))->Click());

	if (TestEqual(TEXT("The choice was announced once"), Listener->SelectionIndices.Num(), 1))
	{
		TestEqual(TEXT("Naming the second option"), Listener->SelectionIndices[0], 1);
	}
	TestEqual(TEXT("Which is the second of the new options"), Dropdown->GetSelectedOption().ToString(), FString(TEXT("Fullscreen")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPressDropdownOptionsWhileOpenTest,
	"DreamGUI.Dropdown.ChangingTheOptionsWhileTheListIsOpenRebuildsItsRowsAtOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamPressDropdownOptionsWhileOpenTest, "DreamGUI.Dropdown.ChangingTheOptionsWhileTheListIsOpenRebuildsItsRowsAtOnce", "[Pointer][Animated]")

/*
 * The same defect from the other side: options replaced while the list is up. The rows used to stay as
 * they were until the next open, so a row past the new end chose an index no option has -- a blank
 * caption and a selection event naming nothing. The list is opened with three options and given two:
 * it holds two rows at once, its column shrinks to two rows' height, and a click on a row chooses an
 * option that exists.
 */
bool FDreamPressDropdownOptionsWhileOpenTest::RunTest(const FString& Parameters)
{
	using namespace DreamPressDropdownTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FPlacedDropdown Placed = PlaceDropdown(*this, Rig, Listener.Get(), 3);
	if (!Placed.IsReady() || !OpenByClicking(*this, Rig, Placed))
	{
		return false;
	}
	UDreamWidget* Column = FindColumn(Placed.Dropdown);
	if (!TestNotNull(TEXT("The open list has a column of rows"), Column))
	{
		return false;
	}
	const float ThreeRowsHigh = Column->GetHeight();

	Placed.Dropdown->SetOptions(TwoOtherOptions());
	Rig.PumpFrames(2);

	TestTrue(TEXT("The list is still open"), Placed.Dropdown->IsOpen());
	TestEqual(TEXT("It holds a row per new option"), CountRows(Placed.Dropdown), 2);
	TestNearlyEqual(TEXT("And its column is two rows high where it was three"), Column->GetHeight(), ThreeRowsHigh * 2.0f / 3.0f, 0.5f);
	if (!TestTrue(TEXT("The rebuild generated a row for the second new option"),
		Listener->GeneratedItems.IsValidIndex(1) && IsValid(Listener->GeneratedItems[1])))
	{
		return false;
	}
	TestTrue(TEXT("Clicking that row completes"), Rig.Driver()->Find(FDreamBy::Widget(Listener->GeneratedItems[1].Get()))->Click());

	bool bEveryChoiceExists = true;
	for (const int32 Chosen : Listener->SelectionIndices)
	{
		bEveryChoiceExists &= (Chosen >= 0 && Chosen < Placed.Dropdown->GetOptionCount());
	}
	TestTrue(TEXT("Every choice announced names an option the dropdown has"), bEveryChoiceExists);
	TestEqual(TEXT("And the click chose the second new option"), Placed.Dropdown->GetSelectedOption().ToString(), FString(TEXT("Fullscreen")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPressDropdownRowDestroyedElsewhereTest,
	"DreamGUI.Dropdown.ARowDestroyedByOtherCodeDoesNotStopTheListFromBeingRebuilt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamPressDropdownRowDestroyedElsewhereTest, "DreamGUI.Dropdown.ARowDestroyedByOtherCodeDoesNotStopTheListFromBeingRebuilt", "[Pointer][Animated]")

/*
 * The rows are handed to the consumer as they are built, and a consumer can destroy one. The behaviour
 * keeps its rows weakly and, rebuilding, reached through every entry to destroy the row's widget
 * without asking whether the entry still had a row behind it -- a null dereference on the next open.
 * A row is destroyed here while the list is closed, the options are replaced, and the list opened
 * again: it opens, holding a row per new option.
 */
bool FDreamPressDropdownRowDestroyedElsewhereTest::RunTest(const FString& Parameters)
{
	using namespace DreamPressDropdownTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FPlacedDropdown Placed = PlaceDropdown(*this, Rig, Listener.Get(), 3);
	if (!Placed.IsReady() || !OpenByClicking(*this, Rig, Placed))
	{
		return false;
	}
	TestTrue(TEXT("Clicking far from the list to close it completes"), ClickAtPixel(Rig, FarFromTheDropdown));
	Rig.PumpFrames(1);
	if (!TestFalse(TEXT("The list closed"), Placed.Dropdown->IsOpen())
		|| !TestTrue(TEXT("Opening generated a second row"), Listener->GeneratedItems.IsValidIndex(1) && IsValid(Listener->GeneratedItems[1])))
	{
		return false;
	}

	Listener->GeneratedItems[1]->DestroyWidget();
	// Closed, so the new options only mark the rows for the next open -- which is where the rebuild runs.
	Placed.Dropdown->SetOptions(TwoOtherOptions());
	Rig.PumpFrames(1);

	if (!OpenByClicking(*this, Rig, Placed))
	{
		return false;
	}
	TestEqual(TEXT("The reopened list holds a row per new option"), CountRows(Placed.Dropdown), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPressDropdownBehaviourDisabledTest,
	"DreamGUI.Dropdown.ClickingADropdownSwitchedOffThroughItsBehaviourDoesNotOpenIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamPressDropdownBehaviourDisabledTest, "DreamGUI.Dropdown.ClickingADropdownSwitchedOffThroughItsBehaviourDoesNotOpenIt", "[Pointer][Disabled]")

/*
 * The behaviour's own switch, which leaves the face hit-testable -- a disabled control still stops a
 * click reaching what is behind it -- so, unlike SetIsEnabled, it does not keep the click from
 * arriving. The press was refused there and the click was not: a dropdown drawn disabled opened its
 * list anyway. Clicking it now opens nothing and says nothing.
 */
bool FDreamPressDropdownBehaviourDisabledTest::RunTest(const FString& Parameters)
{
	using namespace DreamPressDropdownTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FPlacedDropdown Placed = PlaceDropdown(*this, Rig, Listener.Get(), 3);
	if (!Placed.IsReady() || !TestNotNull(TEXT("The dropdown has its behaviour"), Placed.Dropdown->DropdownBehaviour.Get()))
	{
		return false;
	}
	Placed.Dropdown->DropdownBehaviour->SetInteractable(false);
	Rig.PumpFrames(1);

	TestTrue(TEXT("Clicking the switched-off dropdown completes"), Placed.Element->Click());

	TestFalse(TEXT("The list stays closed"), Placed.Dropdown->IsOpen());
	TestEqual(TEXT("And never began opening"), Listener->OpeningCount, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPressDropdownRightClickTest,
	"DreamGUI.Dropdown.ARightClickDoesNotOpenTheDropdown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamPressDropdownRightClickTest, "DreamGUI.Dropdown.ARightClickDoesNotOpenTheDropdown", "[Pointer][Animated]")

/*
 * UMG's combo box opens from an SButton, which answers the left mouse button alone. The behaviour
 * answers every button unless told otherwise and the control never told it, so a right click opened the
 * list. It opens nothing now, and the left button still does.
 */
bool FDreamPressDropdownRightClickTest::RunTest(const FString& Parameters)
{
	using namespace DreamPressDropdownTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FPlacedDropdown Placed = PlaceDropdown(*this, Rig, Listener.Get(), 3);
	if (!Placed.IsReady())
	{
		return false;
	}

	TestTrue(TEXT("Right-clicking the dropdown completes"), Placed.Element->Click(EDreamUIMouseButtonType::Right));
	TestFalse(TEXT("A right click leaves the list closed"), Placed.Dropdown->IsOpen());
	TestEqual(TEXT("And began no opening"), Listener->OpeningCount, 0);

	TestTrue(TEXT("Left-clicking it completes"), Placed.Element->Click());
	TestTrue(TEXT("The left button still opens it"), Placed.Dropdown->IsOpen());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPressDropdownPadIntoListTest,
	"DreamGUI.Dropdown.MovingPadFocusDownIntoTheOpenListKeepsItOpenAndChoosesFromIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamPressDropdownPadIntoListTest, "DreamGUI.Dropdown.MovingPadFocusDownIntoTheOpenListKeepsItOpenAndChoosesFromIt", "[Nav][Animated]")

/*
 * A pad opens the list with the accept button and moves down within it. The dropdown reads focus
 * leaving it as the cue to close, and it asked whether the new focus was inside its own widget -- which
 * a row is not once the list has been lifted to the screen layer, so a move into the list closed the
 * list. The face also never gave up its focused look, because the dropdown's deselect never reached the
 * selectable's own. The list now opens with the pad's cursor on the selected row -- the first, here --
 * as SComboBox's list takes the focus when it opens, so one step down is on the second row: the list
 * stays open, the face stops claiming focus, and the accept button chooses the second option.
 *
 * It used to take two steps, the first of them only getting from the face into the list: the list
 * opened with focus left on the face.
 */
bool FDreamPressDropdownPadIntoListTest::RunTest(const FString& Parameters)
{
	using namespace DreamPressDropdownTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FPlacedDropdown Placed = PlaceDropdown(*this, Rig, Listener.Get(), 3);
	if (!Placed.IsReady() || !TestNotNull(TEXT("The dropdown has its behaviour"), Placed.Dropdown->DropdownBehaviour.Get()))
	{
		return false;
	}

	// The first direction lands on the only control there is; the accept button opens it.
	TestTrue(TEXT("Moving onto the dropdown and accepting completes"),
		Rig.Driver()->Sequence()
			.Navigate(EDreamUINavigationDirection::Down)
			.NavigationTrigger(true)
			.NavigationTrigger(false)
			.Perform());
	Rig.PumpFrames(2);
	if (!TestTrue(TEXT("The accept button opened the list"), Placed.Dropdown->IsOpen())
		|| !TestTrue(TEXT("Opening generated the first two rows"), Listener->GeneratedItems.Num() >= 2))
	{
		return false;
	}
	TestEqual(TEXT("The pad's cursor opened on the selected row"),
		Rig.EventSystem()->GetHighlightedComponentForNavigation(0), Listener->GeneratedItems[0].Get());

	TestTrue(TEXT("Moving down completes"), Rig.Driver()->Sequence().Navigate(EDreamUINavigationDirection::Down).Perform());
	TestTrue(TEXT("Moving within the list leaves it open"), Placed.Dropdown->IsOpen());
	TestEqual(TEXT("With focus on its second row"), Rig.EventSystem()->GetCurrentSelectedComponent(0), Listener->GeneratedItems[1].Get());
	TestFalse(TEXT("And the face no longer claiming focus"), Placed.Dropdown->DropdownBehaviour->IsFocused());

	TestTrue(TEXT("Accepting completes"),
		Rig.Driver()->Sequence()
			.NavigationTrigger(true)
			.NavigationTrigger(false)
			.Perform());
	if (TestEqual(TEXT("The choice was announced once"), Listener->SelectionIndices.Num(), 1))
	{
		TestEqual(TEXT("Naming the second option"), Listener->SelectionIndices[0], 1);
	}
	TestFalse(TEXT("And choosing closed the list"), Placed.Dropdown->IsOpen());
	return true;
}

#endif
