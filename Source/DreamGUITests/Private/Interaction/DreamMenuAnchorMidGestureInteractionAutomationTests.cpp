// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamButton.h"
#include "Controls/DreamMenuAnchor.h"
#include "Core/DreamUIInputServices.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamEventSystem.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverProjection.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Driver/DreamDriverUntil.h"
#include "Interaction/DreamPressInteractionTestTypes.h"

/*
 * UDreamMenuAnchor under a finger and the pad, caught while its menu fades in, and with a submenu open inside it -- in UMG's
 * arrangement, where the menu opens from a trigger button's click handler that asks ShouldOpenDueToClick first (see the
 * listener's HandleTriggerClicked).
 *
 * The reference is the Slate menu stack under SMenuAnchor (Slate/Private/Framework/Application/MenuStack.cpp): a menu is up
 * from its push and gone at its dismissal; every push makes a new menu window that starts transparent and fades in
 * (:490-503); a press outside every menu dismisses the stack, newest first (:667-676), and a press inside a menu dismisses the
 * menus opened from it; Escape dismisses the top menu.
 *
 * The menu's fade is measured where a wait needs its length: its duration lives in a style the anchor resolves privately.
 */
namespace DreamMenuAnchorMidGestureTestLocal
{
	const FIntPoint ViewportSize(1280, 720);
	const FVector2D TriggerPosition(-300.0, 150.0);
	const FVector2D TriggerSize(160.0, 40.0);
	const FVector2D FarFromTheMenu(1200.0, 680.0);

	/** A stop for waits on a fade, far past any menu's. */
	const double FadeStopSeconds = 1.0;

	struct FPlacedMenu
	{
		UDreamButton* Trigger = nullptr;
		UDreamMenuAnchor* Anchor = nullptr;
		TSharedPtr<FDreamDriverElement> TriggerElement;

		bool IsReady() const
		{
			return Trigger != nullptr && Anchor != nullptr && Anchor->PopupNode != nullptr && Anchor->MenuNode != nullptr
				&& TriggerElement.IsValid() && TriggerElement->Exists();
		}
	};

	/** A trigger button and a menu anchor over it, the trigger opening the anchor the UMG way through InListener. */
	FPlacedMenu PlaceMenu(FDreamDriverRig& InRig, UDreamWidget* InParent, const TCHAR* InName, const FVector2D& InPosition,
		UDreamPressInteractionListener* InListener)
	{
		FPlacedMenu Placed;
		Placed.Trigger = InRig.MakeControl<UDreamButton>(FString::Printf(TEXT("%s_Trigger"), InName), InParent, TriggerSize, InPosition);
		Placed.Anchor = InRig.MakeControl<UDreamMenuAnchor>(FString::Printf(TEXT("%s_Anchor"), InName), InParent, TriggerSize, InPosition);
		if (Placed.Trigger == nullptr || Placed.Anchor == nullptr)
		{
			return Placed;
		}
		InListener->MenuAnchorToOpen = Placed.Anchor;
		Placed.Trigger->OnClicked.AddDynamic(InListener, &UDreamPressInteractionListener::HandleTriggerClicked);
		Placed.Anchor->OnMenuOpenChanged.AddDynamic(InListener, &UDreamPressInteractionListener::HandleMenuOpenChanged);
		Placed.TriggerElement = InRig.Driver()->Find(FDreamBy::Widget(Placed.Trigger));
		return Placed;
	}

	/** The usual single menu: one plain item in it, laid out. */
	FPlacedMenu PlaceSingleMenu(FAutomationTestBase& InTest, FDreamDriverRig& InRig, UDreamPressInteractionListener* InListener)
	{
		InRig.BindTest(&InTest);
		if (!InTest.TestTrue(TEXT("The headless rig came up"), InRig.IsUsable()))
		{
			return FPlacedMenu();
		}
		FPlacedMenu Placed = PlaceMenu(InRig, nullptr, TEXT("File"), TriggerPosition, InListener);
		if (InTest.TestTrue(TEXT("A trigger and a menu anchor can be made on the rig"), Placed.IsReady()))
		{
			InRig.MakeWidget(TEXT("MenuItem"), Placed.Anchor->MenuNode.Get(), FVector2D(160.0, 40.0));
		}
		// Every click below is a click of its own, never the second half of a double click.
		InRig.EventSystem()->SetDoubleClickTime(0.0f);
		InRig.PumpFrames(1);
		return Placed;
	}

	float MenuOpacity(const FPlacedMenu& InPlaced)
	{
		return InPlaced.Anchor->PopupNode->GetRenderOpacity();
	}

	/** Pump until InCondition holds, for at most FadeStopSeconds. */
	bool PumpUntil(FDreamDriverRig& InRig, TFunction<bool()> InCondition, const TCHAR* InWhat)
	{
		const FWaitTimeout Timeout = FWaitTimeout::InSeconds(FadeStopSeconds);
		return InRig.Driver()->Wait(FDreamUntil::Condition(MoveTemp(InCondition), Timeout), Timeout, InWhat);
	}

	/**
	 * Pump until the menu just opened is on its way in: neither clear nor fully in. Its fade takes its first step on the
	 * frame after the open, not the open's own: a tween reads where it starts from on its first update
	 * (UDreamTweener::BeginTween), and the open's frame has had its DuringPhysics tween step before the input frame that
	 * opened it. The first frame this holds is the frame the fade first shows.
	 */
	bool FadeHasBegun(FDreamDriverRig& InRig, const FPlacedMenu& InPlaced)
	{
		return PumpUntil(InRig, [&InPlaced]() { const float Now = MenuOpacity(InPlaced); return Now > 0.0f && Now < 1.0f; },
			TEXT("the menu on its way in"));
	}

	/** Frames until the menu is fully in, pumping one at a time; INDEX_NONE when it never got there. */
	int32 FramesUntilOpaque(FDreamDriverRig& InRig, const FPlacedMenu& InPlaced)
	{
		const int32 Ceiling = FMath::CeilToInt(static_cast<float>(FadeStopSeconds) / InRig.Context().FrameSeconds);
		for (int32 Frame = 0; Frame <= Ceiling; ++Frame)
		{
			if (MenuOpacity(InPlaced) >= 1.0f)
			{
				return Frame;
			}
			InRig.PumpFrames(1);
		}
		return INDEX_NONE;
	}

	bool ClickAtPixel(FDreamDriverRig& InRig, const FVector2D& InPixel)
	{
		return InRig.Driver()->Sequence().MoveToPixel(InPixel).Press().Release().Perform();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamMenuAnchorTapTest,
	"DreamGUI.MenuAnchor.ATapOnTheTriggerOpensTheMenuAndATapOutsideClosesIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamMenuAnchorTapTest, "DreamGUI.MenuAnchor.ATapOnTheTriggerOpensTheMenuAndATapOutsideClosesIt", "[Touch][Animated]")

/*
 * A tap on the trigger is a click of the trigger (SButton takes a touch as it takes the left button, SButton.cpp:354, :410),
 * and its handler opens the menu; a tap outside the open menu is a press outside it, which dismisses it. Each is announced
 * once.
 */
bool FDreamMenuAnchorTapTest::RunTest(const FString& Parameters)
{
	using namespace DreamMenuAnchorMidGestureTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FPlacedMenu Placed = PlaceSingleMenu(*this, Rig, Listener.Get());
	if (!Placed.IsReady())
	{
		return false;
	}

	TestTrue(TEXT("Tapping the trigger completes"), Placed.TriggerElement->Tap());
	TestTrue(TEXT("The tap opened the menu"), Placed.Anchor->IsOpen());
	Rig.PumpFrames(2);
	TestTrue(TEXT("Tapping far from the menu completes"),
		Rig.Driver()->Sequence().TouchDown(0, FarFromTheMenu).TouchUp(0).Perform());
	TestFalse(TEXT("The tap outside closed the menu"), Placed.Anchor->IsOpen());
	if (TestEqual(TEXT("Open and closed were each announced once"), Listener->MenuOpenStates.Num(), 2))
	{
		TestTrue(TEXT("Open first"), Listener->MenuOpenStates[0]);
		TestFalse(TEXT("Then closed"), Listener->MenuOpenStates[1]);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamMenuAnchorCloseDuringFadeInTest,
	"DreamGUI.MenuAnchor.AClickOnTheTriggerWhileTheMenuFadesInClosesItAtOnceAndForGood",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamMenuAnchorCloseDuringFadeInTest, "DreamGUI.MenuAnchor.AClickOnTheTriggerWhileTheMenuFadesInClosesItAtOnceAndForGood", "[Pointer][Animated]")

/*
 * The menu opened and, partway through its fade in, the trigger clicked again. That press is outside the menu and on its
 * opener, so it dismisses the menu and opens nothing (the ShouldOpenDueToClick arrangement). The menu is closed at once --
 * announced, off screen -- and stays closed: the fade it was in the middle of does not carry on and bring it back. The
 * fade's length is measured first, on an open nobody interrupts.
 */
bool FDreamMenuAnchorCloseDuringFadeInTest::RunTest(const FString& Parameters)
{
	using namespace DreamMenuAnchorMidGestureTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FPlacedMenu Placed = PlaceSingleMenu(*this, Rig, Listener.Get());
	if (!Placed.IsReady())
	{
		return false;
	}

	// The measure: one whole fade in, then closed from outside.
	TestTrue(TEXT("Clicking the trigger completes"), Placed.TriggerElement->Click());
	const int32 FadeFrames = FramesUntilOpaque(Rig, Placed);
	if (!TestTrue(FString::Printf(TEXT("The menu fades in over several frames (%d)"), FadeFrames), FadeFrames >= 3))
	{
		return false;
	}
	TestTrue(TEXT("Clicking far from the menu completes"), ClickAtPixel(Rig, FarFromTheMenu));
	if (!TestFalse(TEXT("...which closed it"), Placed.Anchor->IsOpen()))
	{
		return false;
	}

	TestTrue(TEXT("Clicking the trigger again completes"), Placed.TriggerElement->Click());
	if (!TestTrue(TEXT("The click opened the menu"), Placed.Anchor->IsOpen()))
	{
		return false;
	}
	// Waited for until its fade has truly begun: see FadeHasBegun.
	const bool bFadingIn = FadeHasBegun(Rig, Placed);
	const float Partway = MenuOpacity(Placed);
	if (!TestTrue(FString::Printf(TEXT("...which is on its way in (opacity %.3f)"), Partway), bFadingIn && Partway > 0.0f && Partway < 1.0f))
	{
		return false;
	}

	TestTrue(TEXT("Clicking the trigger while the menu fades in completes"), Placed.TriggerElement->Click());
	TestFalse(TEXT("The click closed the menu rather than opening it again"), Placed.Anchor->IsOpen());
	TestFalse(TEXT("...and took it off the screen"), Placed.Anchor->PopupNode->GetWidgetActive());
	TestEqual(TEXT("Each open and each close was announced: two of each"), Listener->MenuOpenStates.Num(), 4);
	if (Listener->MenuOpenStates.Num() >= 4)
	{
		TestFalse(TEXT("The last announcement is the close"), Listener->MenuOpenStates[3]);
	}

	TestTrue(TEXT("Waiting out the rest of the fade completes"), Rig.Driver()->Sequence().WaitFrames(FadeFrames + 2).Perform());
	TestFalse(TEXT("The menu is still closed"), Placed.Anchor->IsOpen());
	TestFalse(TEXT("...and still off the screen"), Placed.Anchor->PopupNode->GetWidgetActive());
	TestEqual(TEXT("...and nothing more was announced"), Listener->MenuOpenStates.Num(), 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamMenuAnchorReopenAfterMidFadeCloseTest,
	"DreamGUI.MenuAnchor.AMenuReopenedJustAfterAMidFadeCloseFadesInFromClearAgain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamMenuAnchorReopenAfterMidFadeCloseTest, "DreamGUI.MenuAnchor.AMenuReopenedJustAfterAMidFadeCloseFadesInFromClearAgain", "[Pointer][Animated]")

/*
 * Opened, closed by a click on the trigger partway through its fade in, and opened again by the next click, all within one
 * fade's length. Every push of a Slate menu is a new window that starts transparent and fades in (MenuStack.cpp:490-503), so
 * the second open starts from clear exactly as the first did -- not from wherever the first, abandoned fade has got to by
 * then -- and comes all the way in.
 */
bool FDreamMenuAnchorReopenAfterMidFadeCloseTest::RunTest(const FString& Parameters)
{
	using namespace DreamMenuAnchorMidGestureTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FPlacedMenu Placed = PlaceSingleMenu(*this, Rig, Listener.Get());
	if (!Placed.IsReady())
	{
		return false;
	}

	TestTrue(TEXT("Clicking the trigger completes"), Placed.TriggerElement->Click());
	if (!TestTrue(TEXT("The click opened the menu"), Placed.Anchor->IsOpen()))
	{
		return false;
	}
	const bool bFadingIn = FadeHasBegun(Rig, Placed);
	const float OneFrameIn = MenuOpacity(Placed);
	if (!TestTrue(FString::Printf(TEXT("...which is one frame into its fade (opacity %.3f)"), OneFrameIn), bFadingIn && OneFrameIn > 0.0f && OneFrameIn < 1.0f))
	{
		return false;
	}
	TestTrue(TEXT("Clicking the trigger while it fades in completes"), Placed.TriggerElement->Click());
	if (!TestFalse(TEXT("...which closed the menu"), Placed.Anchor->IsOpen()))
	{
		return false;
	}

	TestTrue(TEXT("Clicking the trigger once more completes"), Placed.TriggerElement->Click());
	TestTrue(TEXT("The click opened the menu again"), Placed.Anchor->IsOpen());
	// Measured as the first open was: on the first frame its fade shows. A fade that carried on from the abandoned one
	// shows on that frame too, as far in as the abandoned one had got.
	TestTrue(TEXT("...and its fade begins"), FadeHasBegun(Rig, Placed));
	const float ReopenedOneFrameIn = MenuOpacity(Placed);
	TestTrue(FString::Printf(TEXT("One frame into the second open the menu is as clear as one frame into the first (%.3f against %.3f)"),
		ReopenedOneFrameIn, OneFrameIn), ReopenedOneFrameIn <= OneFrameIn + 0.02f);
	TestTrue(TEXT("...and it comes all the way in"), PumpUntil(Rig, [&Placed]() { return MenuOpacity(Placed) >= 1.0f; }, TEXT("the reopened menu fully in")));
	TestTrue(TEXT("...staying open"), Placed.Anchor->IsOpen());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamMenuAnchorPadConfirmAndBackTest,
	"DreamGUI.MenuAnchor.ThePadsConfirmOnTheTriggerOpensTheMenuAndBackClosesItWithTheFocusBackOnTheTrigger",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamMenuAnchorPadConfirmAndBackTest, "DreamGUI.MenuAnchor.ThePadsConfirmOnTheTriggerOpensTheMenuAndBackClosesItWithTheFocusBackOnTheTrigger", "[Nav][Animated]")

/*
 * The pad's focus on the trigger (the first stick press lands on the only control there is), Accept presses it
 * (SButton::OnKeyDown / OnKeyUp, SButton.cpp:296-340) and its click opens the menu; Back then dismisses the top menu, as
 * Escape does on the Slate menu stack, and the focus goes back where it was when the menu opened -- the trigger.
 */
bool FDreamMenuAnchorPadConfirmAndBackTest::RunTest(const FString& Parameters)
{
	using namespace DreamMenuAnchorMidGestureTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FPlacedMenu Placed = PlaceSingleMenu(*this, Rig, Listener.Get());
	UDreamUIInputServices* Services = Placed.IsReady() ? UDreamUIInputServices::Get(Rig.GetWorld()) : nullptr;
	if (!Placed.IsReady() || !TestNotNull(TEXT("The rig's world has input"), Services))
	{
		return false;
	}
	FDreamDriverRef Driver = Rig.Driver();

	TestTrue(TEXT("A stick press completes"), Driver->Sequence().Navigate(EDreamUINavigationDirection::Right).Perform());
	const UDreamWidget* Highlighted = Rig.EventSystem()->GetHighlightedComponentForNavigation(0);
	if (!TestTrue(TEXT("The stick press landed the pad's focus on the trigger"),
		Highlighted != nullptr && (Highlighted == Placed.Trigger || Highlighted->IsChildOf(Placed.Trigger))))
	{
		return false;
	}

	TestTrue(TEXT("Pressing and releasing the pad's confirm completes"), Driver->Sequence().NavigationTrigger(true).NavigationTrigger(false).Perform());
	TestTrue(TEXT("The confirm clicked the trigger, which opened the menu"), Placed.Anchor->IsOpen());
	Rig.PumpFrames(2);

	TestTrue(TEXT("Pressing Back completes"), Driver->Sequence().Back().Perform());
	TestFalse(TEXT("Back closed the menu"), Placed.Anchor->IsOpen());
	if (TestEqual(TEXT("Open and closed were each announced once"), Listener->MenuOpenStates.Num(), 2))
	{
		TestFalse(TEXT("The last is the close"), Listener->MenuOpenStates[1]);
	}
	const UDreamWidget* Focused = Services->GetFocusedWidget(0);
	TestTrue(TEXT("...and the focus is back on the trigger"), Focused != nullptr && (Focused == Placed.Trigger || Focused->IsChildOf(Placed.Trigger)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamMenuAnchorSubmenuOutsideTest,
	"DreamGUI.MenuAnchor.APressOutsideAMenuAndItsOpenSubmenuClosesThemBoth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamMenuAnchorSubmenuOutsideTest, "DreamGUI.MenuAnchor.APressOutsideAMenuAndItsOpenSubmenuClosesThemBoth", "[Pointer][Animated]")

/*
 * A menu with a submenu trigger along its top; the menu opened by its trigger, the submenu by its own, opening to the
 * menu's right. A press outside both is outside every menu on the stack, and dismisses the stack (MenuStack.cpp:667-676):
 * both close, each announcing it once, and the menu has no submenu open any more.
 */
bool FDreamMenuAnchorSubmenuOutsideTest::RunTest(const FString& Parameters)
{
	using namespace DreamMenuAnchorMidGestureTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> MenuListener(NewObject<UDreamPressInteractionListener>());
	TStrongObjectPtr<UDreamPressInteractionListener> SubListener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	const FPlacedMenu Menu = PlaceMenu(Rig, nullptr, TEXT("File"), TriggerPosition, MenuListener.Get());
	if (!TestTrue(TEXT("A trigger and a menu anchor can be made on the rig"), Menu.IsReady()))
	{
		return false;
	}
	const FPlacedMenu Sub = PlaceMenu(Rig, Menu.Anchor->MenuNode.Get(), TEXT("Recent"), FVector2D::ZeroVector, SubListener.Get());
	if (!TestTrue(TEXT("A submenu trigger and anchor can be put in the menu"), Sub.IsReady()))
	{
		return false;
	}
	// Along the top of the menu, full width, rather than filling it; and the submenu opening to the right of it.
	for (UDreamWidget* Part : { static_cast<UDreamWidget*>(Sub.Trigger), static_cast<UDreamWidget*>(Sub.Anchor) })
	{
		if (UDreamPanelSlot* Slot = Part->GetPanelSlot())
		{
			Slot->SetHorizontalAlignment(EDreamPanelHorizontalAlignment::Fill);
			Slot->SetVerticalAlignment(EDreamPanelVerticalAlignment::Top);
		}
	}
	Sub.Anchor->SetPlacement(EDreamMenuPlacement::MenuRight);
	Rig.MakeWidget(TEXT("RecentItem"), Sub.Anchor->MenuNode.Get(), FVector2D(160.0, 40.0));
	Rig.EventSystem()->SetDoubleClickTime(0.0f);
	Rig.PumpFrames(1);

	TestTrue(TEXT("Clicking the menu's trigger completes"), Menu.TriggerElement->Click());
	Rig.PumpFrames(2);
	TestTrue(TEXT("The menu is open"), Menu.Anchor->IsOpen());
	TestTrue(TEXT("Clicking the submenu's trigger completes"), Sub.TriggerElement->Click());
	Rig.PumpFrames(2);
	if (!TestTrue(TEXT("The submenu is open"), Sub.Anchor->IsOpen())
		|| !TestTrue(TEXT("...inside the menu, which says so"), Menu.Anchor->HasOpenSubMenus()))
	{
		return false;
	}

	TestTrue(TEXT("Clicking far from both completes"), ClickAtPixel(Rig, FarFromTheMenu));
	TestFalse(TEXT("The press outside closed the submenu"), Sub.Anchor->IsOpen());
	TestFalse(TEXT("...and the menu"), Menu.Anchor->IsOpen());
	TestFalse(TEXT("...which has no submenu open any more"), Menu.Anchor->HasOpenSubMenus());
	TestEqual(TEXT("The submenu announced its open and its close"), SubListener->MenuOpenStates.Num(), 2);
	TestEqual(TEXT("...and so did the menu"), MenuListener->MenuOpenStates.Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamMenuAnchorPressInParentMenuTest,
	"DreamGUI.MenuAnchor.APressInTheMenuASubmenuCameFromClosesTheSubmenuAndLeavesTheMenuOpen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamMenuAnchorPressInParentMenuTest, "DreamGUI.MenuAnchor.APressInTheMenuASubmenuCameFromClosesTheSubmenuAndLeavesTheMenuOpen", "[Pointer][Animated]")

/*
 * The same chain, and a press in the menu below its submenu trigger -- in the menu, not in the submenu. On the Slate menu
 * stack that dismisses the menus opened from the one pressed and leaves that one up: the submenu closes, the menu stays
 * open and says nothing.
 */
bool FDreamMenuAnchorPressInParentMenuTest::RunTest(const FString& Parameters)
{
	using namespace DreamMenuAnchorMidGestureTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> MenuListener(NewObject<UDreamPressInteractionListener>());
	TStrongObjectPtr<UDreamPressInteractionListener> SubListener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	const FPlacedMenu Menu = PlaceMenu(Rig, nullptr, TEXT("File"), TriggerPosition, MenuListener.Get());
	const FPlacedMenu Sub = Menu.IsReady() ? PlaceMenu(Rig, Menu.Anchor->MenuNode.Get(), TEXT("Recent"), FVector2D::ZeroVector, SubListener.Get()) : FPlacedMenu();
	if (!TestTrue(TEXT("A menu with a submenu trigger and anchor in it can be made on the rig"), Menu.IsReady() && Sub.IsReady()))
	{
		return false;
	}
	for (UDreamWidget* Part : { static_cast<UDreamWidget*>(Sub.Trigger), static_cast<UDreamWidget*>(Sub.Anchor) })
	{
		if (UDreamPanelSlot* Slot = Part->GetPanelSlot())
		{
			Slot->SetHorizontalAlignment(EDreamPanelHorizontalAlignment::Fill);
			Slot->SetVerticalAlignment(EDreamPanelVerticalAlignment::Top);
		}
	}
	Sub.Anchor->SetPlacement(EDreamMenuPlacement::MenuRight);
	Rig.MakeWidget(TEXT("RecentItem"), Sub.Anchor->MenuNode.Get(), FVector2D(160.0, 40.0));
	Rig.EventSystem()->SetDoubleClickTime(0.0f);
	Rig.PumpFrames(1);

	TestTrue(TEXT("Clicking the menu's trigger completes"), Menu.TriggerElement->Click());
	Rig.PumpFrames(2);
	TestTrue(TEXT("Clicking the submenu's trigger completes"), Sub.TriggerElement->Click());
	Rig.PumpFrames(2);
	if (!TestTrue(TEXT("The menu and its submenu are open"), Menu.Anchor->IsOpen() && Sub.Anchor->IsOpen()))
	{
		return false;
	}
	const TOptional<FBox2D> MenuRect = FDreamDriverProjection::WidgetToPixelRect(Menu.Anchor->PopupNode.Get());
	const TOptional<FBox2D> SubTriggerRect = Sub.TriggerElement->GetPixelRect();
	if (!TestTrue(TEXT("The menu and the submenu trigger are on the viewport"), MenuRect.IsSet() && SubTriggerRect.IsSet()))
	{
		return false;
	}
	// In the menu, well below the submenu trigger along its top.
	const FVector2D InTheMenu(MenuRect->GetCenter().X, MenuRect->Max.Y - 12.0);
	if (!TestTrue(TEXT("There is menu below the submenu trigger to press on"), InTheMenu.Y > SubTriggerRect->Max.Y + 4.0))
	{
		return false;
	}

	TestTrue(TEXT("Clicking in the menu below the submenu trigger completes"), ClickAtPixel(Rig, InTheMenu));
	TestFalse(TEXT("The press in the menu closed the submenu"), Sub.Anchor->IsOpen());
	TestTrue(TEXT("...and left the menu it was pressed in open"), Menu.Anchor->IsOpen());
	TestEqual(TEXT("The submenu announced its close"), SubListener->MenuOpenStates.Num(), 2);
	TestEqual(TEXT("The menu announced nothing past its open"), MenuListener->MenuOpenStates.Num(), 1);
	return true;
}

#endif
