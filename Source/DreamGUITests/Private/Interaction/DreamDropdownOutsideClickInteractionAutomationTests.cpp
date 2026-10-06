// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamButton.h"
#include "Controls/DreamDropdown.h"
#include "Core/DreamGUISettings.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamEventSystem.h"
#include "Interaction/UIDropdown.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Interaction/DreamPressInteractionTestTypes.h"

/*
 * WHERE A PRESS OUTSIDE A DROPDOWN'S OPEN LIST GOES.
 *
 * UMG's combo box is an SComboBox, an SComboButton over an SMenuAnchor: its list is a menu on the Slate menu stack, which
 * closes the menus on a press outside them and leaves the press unhandled (Slate/Private/Framework/Application/
 * MenuStack.cpp, the dismissal on a press outside every menu, :667-676), so the press goes on to the widget it landed on.
 * The press on the combo box's own button is the one that must not reopen what it closed: SComboButton::OnButtonClicked
 * opens only when SMenuAnchor::ShouldOpenDueToClick says so (SComboButton.cpp:138-144), which it does not for the press that
 * dismissed the menu.
 *
 * DreamGUI's dropdown follows the project's menus (UDreamGUISettings::bMenusConsumeOutsideClick, off by default), and a
 * single dropdown can keep the press whatever the project says (UUIDropdown::bUseInteractionBlock). Each test sets what it
 * is about, so the project's configuration decides nothing here.
 */
namespace DreamDropdownOutsideClickTestLocal
{
	const FIntPoint ViewportSize(1280, 720);
	/** Low on the right: clear of the dropdown, high in the middle, and of the list that opens below it. */
	const FVector2D OtherButtonPosition(350.0, -250.0);
	const FVector2D OtherButtonSize(160.0, 40.0);

	/** The project's switch, set for the length of one test and put back after. */
	struct FMenusConsumeSetting
	{
		TGuardValue<bool> Consume;
		explicit FMenusConsumeSetting(bool bInConsume)
			: Consume(GetMutableDefault<UDreamGUISettings>()->bMenusConsumeOutsideClick, bInConsume)
		{
		}
	};

	struct FPlacedDropdown
	{
		UDreamDropdown* Dropdown = nullptr;
		UDreamButton* Other = nullptr;
		TSharedPtr<FDreamDriverElement> FaceElement;
		TSharedPtr<FDreamDriverElement> OtherElement;

		bool IsReady() const
		{
			return Dropdown != nullptr && Dropdown->DropdownBehaviour != nullptr && Dropdown->FaceNode != nullptr && Other != nullptr
				&& FaceElement.IsValid() && FaceElement->Exists() && OtherElement.IsValid() && OtherElement->Exists();
		}
	};

	/**
	 * A dropdown high on the screen with three options, the first selected, its events going to InListener, and a button
	 * elsewhere whose presses and clicks go to InOtherListener.
	 */
	FPlacedDropdown PlaceDropdown(FAutomationTestBase& InTest, FDreamDriverRig& InRig, UDreamPressInteractionListener* InListener,
		UDreamPressInteractionListener* InOtherListener)
	{
		FPlacedDropdown Placed;
		InRig.BindTest(&InTest);
		if (!InTest.TestTrue(TEXT("The headless rig came up"), InRig.IsUsable()))
		{
			return Placed;
		}
		Placed.Dropdown = InRig.MakeControl<UDreamDropdown>(TEXT("Quality"), nullptr, FVector2D(200.0, 40.0), FVector2D(0.0, 200.0));
		Placed.Other = InRig.MakeControl<UDreamButton>(TEXT("Other"), nullptr, OtherButtonSize, OtherButtonPosition);
		if (Placed.Dropdown == nullptr || Placed.Other == nullptr)
		{
			InTest.AddError(TEXT("A dropdown and another button can be made on the rig"));
			return Placed;
		}
		Placed.Dropdown->SetOptions({ FText::AsCultureInvariant(TEXT("Low")), FText::AsCultureInvariant(TEXT("Medium")), FText::AsCultureInvariant(TEXT("High")) });
		Placed.Dropdown->SetSelectedIndex(0);
		Placed.Dropdown->OnSelectionChanged.AddDynamic(InListener, &UDreamPressInteractionListener::HandleSelectionChanged);
		Placed.Dropdown->OnOpening.AddDynamic(InListener, &UDreamPressInteractionListener::HandleOpening);
		Placed.Dropdown->OnClosed.AddDynamic(InListener, &UDreamPressInteractionListener::HandleClosed);
		Placed.Other->OnClicked.AddDynamic(InOtherListener, &UDreamPressInteractionListener::HandleClicked);
		Placed.Other->OnPressed.AddDynamic(InOtherListener, &UDreamPressInteractionListener::HandlePressed);
		// Every click below is a click of its own, never the second half of a double click.
		InRig.EventSystem()->SetDoubleClickTime(0.0f);
		InRig.PumpFrames(1);
		if (Placed.Dropdown->FaceNode != nullptr)
		{
			Placed.FaceElement = InRig.Driver()->Find(FDreamBy::Widget(Placed.Dropdown->FaceNode.Get()));
		}
		Placed.OtherElement = InRig.Driver()->Find(FDreamBy::Widget(Placed.Other));
		InTest.TestTrue(TEXT("The dropdown has its face and behaviour, and the driver can find the face and the other button"), Placed.IsReady());
		return Placed;
	}

	/** The face clicked and the list it opens let settle, before anything aims past it. */
	bool OpenByClicking(FAutomationTestBase& InTest, FDreamDriverRig& InRig, const FPlacedDropdown& InPlaced)
	{
		InTest.TestTrue(TEXT("Clicking the dropdown's face completes"), InPlaced.FaceElement->Click());
		InRig.PumpFrames(2);
		return InTest.TestTrue(TEXT("The click opened the list"), InPlaced.Dropdown->IsOpen());
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDropdownOutsideClickPassesThroughTest,
	"DreamGUI.Dropdown.APressOutsideTheOpenListClosesItChoosesNothingAndGoesOnToClickTheButtonItLandedOn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamDropdownOutsideClickPassesThroughTest, "DreamGUI.Dropdown.APressOutsideTheOpenListClosesItChoosesNothingAndGoesOnToClickTheButtonItLandedOn", "[Pointer][Animated]")

/*
 * The list open, and a click on a button elsewhere on the screen. The menu stack dismisses SComboBox's list on the press and
 * leaves the press to the button (MenuStack.cpp:667-676): the list closes -- once, choosing nothing -- and the one click
 * presses and clicks the button, once each.
 */
bool FDreamDropdownOutsideClickPassesThroughTest::RunTest(const FString& Parameters)
{
	using namespace DreamDropdownOutsideClickTestLocal;
	FMenusConsumeSetting PassThrough(false);
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	TStrongObjectPtr<UDreamPressInteractionListener> OtherListener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FPlacedDropdown Placed = PlaceDropdown(*this, Rig, Listener.Get(), OtherListener.Get());
	if (!Placed.IsReady())
	{
		return false;
	}
	TestFalse(TEXT("A dropdown leaves the press outside its list to the project by default"), Placed.Dropdown->DropdownBehaviour->GetUseInteractionBlock());
	if (!OpenByClicking(*this, Rig, Placed))
	{
		return false;
	}

	TestTrue(TEXT("Clicking the other button while the list is open completes"), Placed.OtherElement->Click());
	TestFalse(TEXT("The press closed the list"), Placed.Dropdown->IsOpen());
	TestEqual(TEXT("...saying so once"), Listener->ClosedCount, 1);
	TestEqual(TEXT("...choosing nothing"), Listener->SelectionIndices.Num(), 0);
	TestEqual(TEXT("...the selection where it was"), Placed.Dropdown->GetSelectedIndex(), 0);
	TestEqual(TEXT("And the press went on to the button it landed on"), OtherListener->PressedCount, 1);
	TestEqual(TEXT("...which the click clicked, once"), OtherListener->ClickedCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDropdownOutsideClickProjectConsumesTest,
	"DreamGUI.Dropdown.WithMenusSetToKeepTheOutsidePressItClosesTheListAndClicksNothingUnderIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamDropdownOutsideClickProjectConsumesTest, "DreamGUI.Dropdown.WithMenusSetToKeepTheOutsidePressItClosesTheListAndClicksNothingUnderIt", "[Pointer][Animated]")

/*
 * The same click with the project's menus set to keep the press outside them (UDreamGUISettings::
 * bMenusConsumeOutsideClick), which a dropdown's list follows as a menu: the press closes the list and goes no further, so
 * the button it landed on is neither pressed nor clicked. With the list closed, the next click is the button's.
 */
bool FDreamDropdownOutsideClickProjectConsumesTest::RunTest(const FString& Parameters)
{
	using namespace DreamDropdownOutsideClickTestLocal;
	FMenusConsumeSetting Consume(true);
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	TStrongObjectPtr<UDreamPressInteractionListener> OtherListener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FPlacedDropdown Placed = PlaceDropdown(*this, Rig, Listener.Get(), OtherListener.Get());
	if (!Placed.IsReady() || !OpenByClicking(*this, Rig, Placed))
	{
		return false;
	}

	TestTrue(TEXT("Clicking the other button while the list is open completes"), Placed.OtherElement->Click());
	TestFalse(TEXT("The press closed the list"), Placed.Dropdown->IsOpen());
	TestEqual(TEXT("...choosing nothing"), Listener->SelectionIndices.Num(), 0);
	TestEqual(TEXT("And went no further: the button it landed on was not pressed"), OtherListener->PressedCount, 0);
	TestEqual(TEXT("...nor clicked"), OtherListener->ClickedCount, 0);

	TestTrue(TEXT("Clicking the other button again, with the list closed, completes"), Placed.OtherElement->Click());
	TestEqual(TEXT("That click is the button's"), OtherListener->ClickedCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDropdownOutsideClickBlockedTest,
	"DreamGUI.Dropdown.ADropdownSetToBlockInteractionKeepsTheOutsidePressFromTheButtonUnderItWhateverTheProjectSays",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamDropdownOutsideClickBlockedTest, "DreamGUI.Dropdown.ADropdownSetToBlockInteractionKeepsTheOutsidePressFromTheButtonUnderItWhateverTheProjectSays", "[Pointer][Animated]")

/*
 * The project lets presses outside its menus through, and this one dropdown is set to keep them (UUIDropdown::
 * bUseInteractionBlock): its list's outside press closes it and goes no further, so the button under it is neither pressed
 * nor clicked.
 */
bool FDreamDropdownOutsideClickBlockedTest::RunTest(const FString& Parameters)
{
	using namespace DreamDropdownOutsideClickTestLocal;
	FMenusConsumeSetting PassThrough(false);
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	TStrongObjectPtr<UDreamPressInteractionListener> OtherListener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FPlacedDropdown Placed = PlaceDropdown(*this, Rig, Listener.Get(), OtherListener.Get());
	if (!Placed.IsReady())
	{
		return false;
	}
	Placed.Dropdown->DropdownBehaviour->SetUseInteractionBlock(true);
	if (!OpenByClicking(*this, Rig, Placed))
	{
		return false;
	}

	TestTrue(TEXT("Clicking the other button while the list is open completes"), Placed.OtherElement->Click());
	TestFalse(TEXT("The press closed the list"), Placed.Dropdown->IsOpen());
	TestEqual(TEXT("...choosing nothing"), Listener->SelectionIndices.Num(), 0);
	TestEqual(TEXT("And went no further: the button it landed on was not pressed"), OtherListener->PressedCount, 0);
	TestEqual(TEXT("...nor clicked"), OtherListener->ClickedCount, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDropdownFaceClickClosesTest,
	"DreamGUI.Dropdown.AClickOnTheFaceOfTheOpenDropdownClosesTheListWithoutOpeningItAgain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamDropdownFaceClickClosesTest, "DreamGUI.Dropdown.AClickOnTheFaceOfTheOpenDropdownClosesTheListWithoutOpeningItAgain", "[Pointer][Animated]")

/*
 * The face is outside the list, so a click on it while the list is open is a press outside the list: with presses let
 * through, as by default, it closes the list -- and must not open it again, as SComboButton's click does not
 * (SMenuAnchor::ShouldOpenDueToClick says no for the press that dismissed the menu). The list is closed, said once, opened
 * only the once, and still closed a moment later; the next click on the face opens it again.
 */
bool FDreamDropdownFaceClickClosesTest::RunTest(const FString& Parameters)
{
	using namespace DreamDropdownOutsideClickTestLocal;
	FMenusConsumeSetting PassThrough(false);
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	TStrongObjectPtr<UDreamPressInteractionListener> OtherListener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FPlacedDropdown Placed = PlaceDropdown(*this, Rig, Listener.Get(), OtherListener.Get());
	if (!Placed.IsReady() || !OpenByClicking(*this, Rig, Placed))
	{
		return false;
	}
	TestEqual(TEXT("The open said it was opening, once"), Listener->OpeningCount, 1);

	TestTrue(TEXT("Clicking the face while the list is open completes"), Placed.FaceElement->Click());
	TestFalse(TEXT("The click closed the list"), Placed.Dropdown->IsOpen());
	TestEqual(TEXT("...saying so once"), Listener->ClosedCount, 1);
	TestEqual(TEXT("...and did not open it again"), Listener->OpeningCount, 1);
	TestEqual(TEXT("...choosing nothing"), Listener->SelectionIndices.Num(), 0);
	Rig.PumpFrames(2);
	TestFalse(TEXT("A moment later the list is still closed"), Placed.Dropdown->IsOpen());

	TestTrue(TEXT("Clicking the face once more completes"), Placed.FaceElement->Click());
	TestTrue(TEXT("...and that click opens the list again"), Placed.Dropdown->IsOpen());
	TestEqual(TEXT("...saying it is opening, a second time"), Listener->OpeningCount, 2);
	return true;
}

#endif
