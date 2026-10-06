// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamButton.h"
#include "Controls/DreamMenuAnchor.h"
#include "Core/DreamGUISettings.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamEventSystem.h"
#include "Interaction/DreamUIPopupLayer.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Interaction/DreamPressInteractionTestTypes.h"

/*
 * WHERE A PRESS OUTSIDE AN OPEN MENU GOES.
 *
 * The Slate menu stack closes the menus on a press outside them and leaves the press unhandled
 * (Slate/Private/Framework/Application/MenuStack.cpp, the dismissal on a press outside every menu, :667-676): the press goes
 * on to the widget it landed on. So a player with a menu open who clicks a button elsewhere closes the menu AND clicks the
 * button. A press on the menu's own trigger is the one press that must not reopen what it closed: UMG's trigger asks
 * SMenuAnchor::ShouldOpenDueToClick, which says no for the click of the press that dismissed the menu.
 *
 * DreamGUI's menus -- a Dream Menu Anchor, and a menu anchor panel -- do that by default; a project can have them keep the
 * press instead (UDreamGUISettings::bMenusConsumeOutsideClick), which is what they all did before. Each test sets the
 * switch it is about, so the project's configuration decides nothing here.
 */
namespace DreamMenuOutsideClickTestLocal
{
	const FIntPoint ViewportSize(1280, 720);
	const FVector2D TriggerPosition(-300.0, 150.0);
	const FVector2D TriggerSize(160.0, 40.0);
	/** Low on the right: clear of the trigger and of any placement the menu takes. */
	const FVector2D OtherButtonPosition(350.0, -250.0);
	const FVector2D OtherButtonSize(160.0, 40.0);

	/** The switch these tests are about, set for the length of one test and put back after. */
	struct FOutsideClickSetting
	{
		TGuardValue<bool> Consume;
		explicit FOutsideClickSetting(bool bInConsume)
			: Consume(GetMutableDefault<UDreamGUISettings>()->bMenusConsumeOutsideClick, bInConsume)
		{
		}
	};

	struct FPlacedMenu
	{
		UDreamButton* Trigger = nullptr;
		UDreamMenuAnchor* Anchor = nullptr;
		UDreamButton* Other = nullptr;
		TSharedPtr<FDreamDriverElement> TriggerElement;
		TSharedPtr<FDreamDriverElement> OtherElement;

		bool IsReady() const
		{
			return Trigger != nullptr && Anchor != nullptr && Anchor->MenuNode != nullptr && Other != nullptr
				&& TriggerElement.IsValid() && TriggerElement->Exists() && OtherElement.IsValid() && OtherElement->Exists();
		}
	};

	/**
	 * A trigger opening a menu anchor over it the UMG way (the listener's HandleTriggerClicked asks ShouldOpenDueToClick),
	 * one plain item in the menu, and another button elsewhere whose clicks go to InOtherListener.
	 */
	FPlacedMenu PlaceMenu(FAutomationTestBase& InTest, FDreamDriverRig& InRig, UDreamPressInteractionListener* InMenuListener,
		UDreamPressInteractionListener* InOtherListener)
	{
		FPlacedMenu Placed;
		InRig.BindTest(&InTest);
		if (!InTest.TestTrue(TEXT("The headless rig came up"), InRig.IsUsable()))
		{
			return Placed;
		}
		Placed.Trigger = InRig.MakeControl<UDreamButton>(TEXT("Trigger"), nullptr, TriggerSize, TriggerPosition);
		Placed.Anchor = InRig.MakeControl<UDreamMenuAnchor>(TEXT("Anchor"), nullptr, TriggerSize, TriggerPosition);
		Placed.Other = InRig.MakeControl<UDreamButton>(TEXT("Other"), nullptr, OtherButtonSize, OtherButtonPosition);
		if (Placed.Trigger == nullptr || Placed.Anchor == nullptr || Placed.Anchor->MenuNode == nullptr || Placed.Other == nullptr)
		{
			InTest.AddError(TEXT("A trigger, a menu anchor with a menu node and another button can be made on the rig"));
			return Placed;
		}
		InRig.MakeWidget(TEXT("MenuItem"), Placed.Anchor->MenuNode.Get(), FVector2D(160.0, 40.0));
		InMenuListener->MenuAnchorToOpen = Placed.Anchor;
		Placed.Trigger->OnClicked.AddDynamic(InMenuListener, &UDreamPressInteractionListener::HandleTriggerClicked);
		Placed.Anchor->OnMenuOpenChanged.AddDynamic(InMenuListener, &UDreamPressInteractionListener::HandleMenuOpenChanged);
		Placed.Other->OnClicked.AddDynamic(InOtherListener, &UDreamPressInteractionListener::HandleClicked);
		Placed.Other->OnPressed.AddDynamic(InOtherListener, &UDreamPressInteractionListener::HandlePressed);
		// Every click below is a click of its own, never the second half of a double click.
		InRig.EventSystem()->SetDoubleClickTime(0.0f);
		InRig.PumpFrames(1);
		Placed.TriggerElement = InRig.Driver()->Find(FDreamBy::Widget(Placed.Trigger));
		Placed.OtherElement = InRig.Driver()->Find(FDreamBy::Widget(Placed.Other));
		InTest.TestTrue(TEXT("The driver can find the trigger and the other button"), Placed.IsReady());
		return Placed;
	}

	/** The trigger clicked and the menu it opens let settle, before anything aims past it. */
	bool OpenByClicking(FAutomationTestBase& InTest, FDreamDriverRig& InRig, const FPlacedMenu& InPlaced)
	{
		InTest.TestTrue(TEXT("Clicking the trigger completes"), InPlaced.TriggerElement->Click());
		InRig.PumpFrames(2);
		return InTest.TestTrue(TEXT("The trigger's click opened the menu"), InPlaced.Anchor->IsOpen());
	}

	struct FPlacedPanelMenu
	{
		UDreamWidget* AnchorWidget = nullptr;
		UDreamWidget* Menu = nullptr;
		UDreamLayoutContainerMenuAnchor* MenuAnchor = nullptr;
		UDreamButton* Other = nullptr;
		TSharedPtr<FDreamDriverElement> OtherElement;

		bool IsReady() const { return MenuAnchor != nullptr && Menu != nullptr && Other != nullptr && OtherElement.IsValid() && OtherElement->Exists(); }
	};

	/**
	 * The panel spelling: a widget holding a face button and then the menu -- a widget with one button in it -- carrying a
	 * menu anchor panel on the popup layer, and another button elsewhere whose clicks go to InOtherListener.
	 */
	FPlacedPanelMenu PlacePanelMenu(FAutomationTestBase& InTest, FDreamDriverRig& InRig, UDreamPressInteractionListener* InOtherListener)
	{
		FPlacedPanelMenu Placed;
		InRig.BindTest(&InTest);
		if (!InTest.TestTrue(TEXT("The headless rig came up"), InRig.IsUsable()))
		{
			return Placed;
		}
		Placed.AnchorWidget = InRig.MakeWidget(TEXT("PanelAnchor"), nullptr, FVector2D(200.0, 40.0), TriggerPosition);
		UDreamButton* Face = Placed.AnchorWidget != nullptr ? InRig.MakeControl<UDreamButton>(TEXT("PanelFace"), Placed.AnchorWidget, TriggerSize) : nullptr;
		Placed.Menu = Face != nullptr ? InRig.MakeWidget(TEXT("PanelMenu"), Placed.AnchorWidget, FVector2D(160.0, 100.0)) : nullptr;
		UDreamButton* Item = Placed.Menu != nullptr ? InRig.MakeControl<UDreamButton>(TEXT("PanelItem"), Placed.Menu, TriggerSize) : nullptr;
		Placed.MenuAnchor = Item != nullptr ? Placed.AnchorWidget->CreateNewLayoutContainer<UDreamLayoutContainerMenuAnchor>() : nullptr;
		Placed.Other = InRig.MakeControl<UDreamButton>(TEXT("Other"), nullptr, OtherButtonSize, OtherButtonPosition);
		if (Placed.MenuAnchor == nullptr || Placed.Other == nullptr)
		{
			InTest.AddError(TEXT("A menu anchor panel with a face and a menu, and another button, can be built on the rig"));
			return Placed;
		}
		Placed.MenuAnchor->SetUseApplicationMenuStack(true);
		Placed.Other->OnClicked.AddDynamic(InOtherListener, &UDreamPressInteractionListener::HandleClicked);
		Placed.Other->OnPressed.AddDynamic(InOtherListener, &UDreamPressInteractionListener::HandlePressed);
		InRig.EventSystem()->SetDoubleClickTime(0.0f);
		InRig.PumpFrames(1);
		Placed.OtherElement = InRig.Driver()->Find(FDreamBy::Widget(Placed.Other));
		InTest.TestTrue(TEXT("The driver can find the other button"), Placed.IsReady());
		return Placed;
	}

	/** The panel's menu opened and up on the popup layer, settled. */
	bool OpenPanelMenu(FAutomationTestBase& InTest, FDreamDriverRig& InRig, const FPlacedPanelMenu& InPlaced)
	{
		InPlaced.MenuAnchor->SetIsOpen(true);
		InRig.PumpFrames(2);
		const UDreamUIPopupLayer* Popups = UDreamUIPopupLayer::Get(InRig.GetWorld());
		return InTest.TestTrue(TEXT("The panel's menu is open, up on the popup layer"),
			InPlaced.MenuAnchor->IsOpen() && Popups != nullptr && Popups->IsOpen(InPlaced.Menu));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamMenuOutsideClickPassesThroughTest,
	"DreamGUI.MenuAnchor.APressOutsideTheOpenMenuClosesItAndGoesOnToClickTheButtonItLandedOn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamMenuOutsideClickPassesThroughTest, "DreamGUI.MenuAnchor.APressOutsideTheOpenMenuClosesItAndGoesOnToClickTheButtonItLandedOn", "[Pointer][Animated]")

/*
 * The menu open, and a click on a button elsewhere on the screen. The menu stack dismisses the menu on the press and leaves
 * the press to the button (MenuStack.cpp:667-676), so the menu closes -- announced once -- and the button is pressed and
 * clicked, once each, by the one click.
 */
bool FDreamMenuOutsideClickPassesThroughTest::RunTest(const FString& Parameters)
{
	using namespace DreamMenuOutsideClickTestLocal;
	FOutsideClickSetting PassThrough(false);
	TStrongObjectPtr<UDreamPressInteractionListener> MenuListener(NewObject<UDreamPressInteractionListener>());
	TStrongObjectPtr<UDreamPressInteractionListener> OtherListener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FPlacedMenu Placed = PlaceMenu(*this, Rig, MenuListener.Get(), OtherListener.Get());
	if (!Placed.IsReady() || !OpenByClicking(*this, Rig, Placed))
	{
		return false;
	}

	TestTrue(TEXT("Clicking the other button while the menu is open completes"), Placed.OtherElement->Click());
	TestFalse(TEXT("The press closed the menu"), Placed.Anchor->IsOpen());
	if (TestEqual(TEXT("...announced: opened, then closed"), MenuListener->MenuOpenStates.Num(), 2))
	{
		TestFalse(TEXT("...closed last"), MenuListener->MenuOpenStates[1]);
	}
	TestEqual(TEXT("...and went on to press the button it landed on"), OtherListener->PressedCount, 1);
	TestEqual(TEXT("...which the click clicked, once"), OtherListener->ClickedCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamMenuOutsideClickConsumedTest,
	"DreamGUI.MenuAnchor.WithMenusSetToKeepTheOutsidePressItClosesTheMenuAndClicksNothingUnderIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamMenuOutsideClickConsumedTest, "DreamGUI.MenuAnchor.WithMenusSetToKeepTheOutsidePressItClosesTheMenuAndClicksNothingUnderIt", "[Pointer][Animated]")

/*
 * The same click with the project's menus set to keep the press outside them (UDreamGUISettings::
 * bMenusConsumeOutsideClick): the press closes the menu and goes no further, so the button it landed on is neither pressed
 * nor clicked. The next click on the button, with no menu open, is the button's.
 */
bool FDreamMenuOutsideClickConsumedTest::RunTest(const FString& Parameters)
{
	using namespace DreamMenuOutsideClickTestLocal;
	FOutsideClickSetting Consume(true);
	TStrongObjectPtr<UDreamPressInteractionListener> MenuListener(NewObject<UDreamPressInteractionListener>());
	TStrongObjectPtr<UDreamPressInteractionListener> OtherListener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FPlacedMenu Placed = PlaceMenu(*this, Rig, MenuListener.Get(), OtherListener.Get());
	if (!Placed.IsReady() || !OpenByClicking(*this, Rig, Placed))
	{
		return false;
	}

	TestTrue(TEXT("Clicking the other button while the menu is open completes"), Placed.OtherElement->Click());
	TestFalse(TEXT("The press closed the menu"), Placed.Anchor->IsOpen());
	TestEqual(TEXT("...and went no further: the button it landed on was not pressed"), OtherListener->PressedCount, 0);
	TestEqual(TEXT("...nor clicked"), OtherListener->ClickedCount, 0);

	TestTrue(TEXT("Clicking the other button again, with no menu open, completes"), Placed.OtherElement->Click());
	TestEqual(TEXT("That click is the button's"), OtherListener->ClickedCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamMenuOutsideClickOnTriggerTest,
	"DreamGUI.MenuAnchor.AClickOnTheTriggerOfTheOpenMenuReachesTheTriggerAndClosesTheMenuWithoutOpeningItAgain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamMenuOutsideClickOnTriggerTest, "DreamGUI.MenuAnchor.AClickOnTheTriggerOfTheOpenMenuReachesTheTriggerAndClosesTheMenuWithoutOpeningItAgain", "[Pointer][Animated]")

/*
 * The trigger sits outside the menu, so a click on it while the menu is open is a press outside the menu: it closes the
 * menu and goes on to the trigger, whose click comes as the press is let go. The trigger's handler asks
 * ShouldOpenDueToClick, which says no for the click of the press that closed the menu -- SMenuAnchor's bDismissedThisTick
 * -- so the menu stays closed. That answer is for that press alone: the next click on the trigger opens the menu again.
 */
bool FDreamMenuOutsideClickOnTriggerTest::RunTest(const FString& Parameters)
{
	using namespace DreamMenuOutsideClickTestLocal;
	FOutsideClickSetting PassThrough(false);
	TStrongObjectPtr<UDreamPressInteractionListener> MenuListener(NewObject<UDreamPressInteractionListener>());
	TStrongObjectPtr<UDreamPressInteractionListener> OtherListener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FPlacedMenu Placed = PlaceMenu(*this, Rig, MenuListener.Get(), OtherListener.Get());
	if (!Placed.IsReady() || !OpenByClicking(*this, Rig, Placed))
	{
		return false;
	}
	TestEqual(TEXT("The trigger heard the click that opened the menu"), MenuListener->TriggerClickedCount, 1);

	TestTrue(TEXT("Clicking the trigger while the menu is open completes"), Placed.TriggerElement->Click());
	TestEqual(TEXT("The press went on to the trigger, whose click came"), MenuListener->TriggerClickedCount, 2);
	TestFalse(TEXT("...and the menu is closed, not opened again by that click"), Placed.Anchor->IsOpen());
	if (TestEqual(TEXT("...announced: opened, then closed, and nothing more"), MenuListener->MenuOpenStates.Num(), 2))
	{
		TestFalse(TEXT("...closed last"), MenuListener->MenuOpenStates[1]);
	}
	Rig.PumpFrames(2);
	TestFalse(TEXT("A moment later it is still closed"), Placed.Anchor->IsOpen());

	TestTrue(TEXT("Clicking the trigger once more completes"), Placed.TriggerElement->Click());
	TestEqual(TEXT("The trigger heard that click too"), MenuListener->TriggerClickedCount, 3);
	TestTrue(TEXT("...and, the press that closed the menu long let go, it opened the menu"), Placed.Anchor->IsOpen());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPanelMenuOutsideClickPassesThroughTest,
	"DreamGUI.MenuAnchor.APressOutsideAPanelsOpenMenuClosesItAndGoesOnToClickTheButtonItLandedOn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamPanelMenuOutsideClickPassesThroughTest, "DreamGUI.MenuAnchor.APressOutsideAPanelsOpenMenuClosesItAndGoesOnToClickTheButtonItLandedOn", "[Pointer][Animated]")

/*
 * The menu anchor panel's menu is a menu on the same stack, and a press outside it is the Slate menu stack's to answer in
 * the same way: the menu closes and the button the press landed on is clicked.
 */
bool FDreamPanelMenuOutsideClickPassesThroughTest::RunTest(const FString& Parameters)
{
	using namespace DreamMenuOutsideClickTestLocal;
	FOutsideClickSetting PassThrough(false);
	TStrongObjectPtr<UDreamPressInteractionListener> OtherListener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FPlacedPanelMenu Placed = PlacePanelMenu(*this, Rig, OtherListener.Get());
	if (!Placed.IsReady() || !OpenPanelMenu(*this, Rig, Placed))
	{
		return false;
	}

	TestTrue(TEXT("Clicking the other button while the panel's menu is open completes"), Placed.OtherElement->Click());
	TestFalse(TEXT("The press closed the panel's menu"), Placed.MenuAnchor->IsOpen());
	TestEqual(TEXT("...and went on to click the button it landed on, once"), OtherListener->ClickedCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPanelMenuOutsideClickConsumedTest,
	"DreamGUI.MenuAnchor.WithMenusSetToKeepTheOutsidePressAPanelsMenuClosesAndTheButtonUnderItIsNotClicked",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamPanelMenuOutsideClickConsumedTest, "DreamGUI.MenuAnchor.WithMenusSetToKeepTheOutsidePressAPanelsMenuClosesAndTheButtonUnderItIsNotClicked", "[Pointer][Animated]")

/*
 * The panel's menu reads the same project switch as the control's: set to keep the press, a press outside the menu closes it
 * and the button it landed on is neither pressed nor clicked.
 */
bool FDreamPanelMenuOutsideClickConsumedTest::RunTest(const FString& Parameters)
{
	using namespace DreamMenuOutsideClickTestLocal;
	FOutsideClickSetting Consume(true);
	TStrongObjectPtr<UDreamPressInteractionListener> OtherListener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FPlacedPanelMenu Placed = PlacePanelMenu(*this, Rig, OtherListener.Get());
	if (!Placed.IsReady() || !OpenPanelMenu(*this, Rig, Placed))
	{
		return false;
	}

	TestTrue(TEXT("Clicking the other button while the panel's menu is open completes"), Placed.OtherElement->Click());
	TestFalse(TEXT("The press closed the panel's menu"), Placed.MenuAnchor->IsOpen());
	TestEqual(TEXT("...and went no further: the button it landed on was not pressed"), OtherListener->PressedCount, 0);
	TestEqual(TEXT("...nor clicked"), OtherListener->ClickedCount, 0);
	return true;
}

#endif
