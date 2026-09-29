// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamDropdown.h"
#include "Core/Components/DreamWidget.h"
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
 * layer, and "outside" is the full-screen blocker UUIDropdown puts behind it -- the mechanism is
 * different, and the claims below are only about what the player sees happen.
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
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPressDropdownOpenTest,
	"DreamGUI.Dropdown.ClickingTheDropdownOpensItsListAndSaysItIsOpening",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

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

#endif
