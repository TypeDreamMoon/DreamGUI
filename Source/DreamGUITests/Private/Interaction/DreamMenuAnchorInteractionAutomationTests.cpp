// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamButton.h"
#include "Controls/DreamMenuAnchor.h"
#include "Core/Components/DreamWidget.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Interaction/DreamPressInteractionTestTypes.h"

/*
 * UDreamMenuAnchor, opened and closed through the real pointer pipeline, in UMG's arrangement.
 *
 * A UMenuAnchor never opens itself on a click: the menu opens from a button's click handler, and the
 * handler asks ShouldOpenDueToClick first, because the click that lands on the button while the menu
 * is up is the click that dismissed it (the menu stack closes a menu on any press outside it). The
 * listener's HandleTriggerClicked is that handler, bound to a Native.Button placed over the anchor --
 * the anchor's own default slot is the MENU, not a trigger, so the trigger sits beside it.
 *
 * Content is put in the menu's slot the way a host nests it: a widget hung under the menu node. It is
 * a plain rig widget with an empty visual, which is hit-testable and has no behaviour of its own -- so
 * "clicking the menu content" means exactly that and nothing a content widget could have decided.
 */
namespace DreamPressMenuAnchorTestLocal
{
	const FIntPoint ViewportSize(1280, 720);

	/** Where the trigger and the anchor both sit, in canvas units with Y up: left of centre, high. */
	const FVector2D TriggerPosition(-300.0, 150.0);
	const FVector2D TriggerSize(160.0, 40.0);

	/** The bottom-right corner of the viewport, which neither the trigger nor any placement reaches. */
	const FVector2D FarFromTheMenu(1200.0, 680.0);

	struct FPlacedMenu
	{
		UDreamButton* Trigger = nullptr;
		UDreamMenuAnchor* Anchor = nullptr;
		UDreamWidget* MenuItem = nullptr;
		TSharedPtr<FDreamDriverElement> TriggerElement;

		bool IsReady() const
		{
			return Trigger != nullptr && Anchor != nullptr && MenuItem != nullptr
				&& TriggerElement.IsValid() && TriggerElement->Exists();
		}
	};

	/**
	 * A trigger button, a menu anchor over it with one item in its menu, the trigger wired to open the
	 * anchor the UMG way, and the anchor's open/closed announcements going to InListener.
	 */
	FPlacedMenu PlaceMenu(FAutomationTestBase& InTest, FDreamDriverRig& InRig, UDreamPressInteractionListener* InListener)
	{
		FPlacedMenu Placed;
		InRig.BindTest(&InTest);
		if (!InTest.TestTrue(TEXT("The headless rig came up"), InRig.IsUsable()))
		{
			return Placed;
		}
		UDreamButton* Trigger = InRig.MakeControl<UDreamButton>(TEXT("Trigger"), nullptr, TriggerSize, TriggerPosition);
		UDreamMenuAnchor* Anchor = InRig.MakeControl<UDreamMenuAnchor>(TEXT("Anchor"), nullptr, TriggerSize, TriggerPosition);
		if (!InTest.TestNotNull(TEXT("A trigger button can be made on the rig"), Trigger)
			|| !InTest.TestNotNull(TEXT("A menu anchor can be made on the rig"), Anchor)
			|| !InTest.TestNotNull(TEXT("The anchor has a menu node to put content in"), Anchor->MenuNode.Get()))
		{
			return Placed;
		}
		UDreamWidget* MenuItem = InRig.MakeWidget(TEXT("MenuItem"), Anchor->MenuNode.Get(), FVector2D(160.0, 40.0));
		if (!InTest.TestNotNull(TEXT("An item can be put in the menu"), MenuItem))
		{
			return Placed;
		}

		InListener->MenuAnchorToOpen = Anchor;
		Trigger->OnClicked.AddDynamic(InListener, &UDreamPressInteractionListener::HandleTriggerClicked);
		Anchor->OnMenuOpenChanged.AddDynamic(InListener, &UDreamPressInteractionListener::HandleMenuOpenChanged);
		InRig.PumpFrames(1);

		Placed.Trigger = Trigger;
		Placed.Anchor = Anchor;
		Placed.MenuItem = MenuItem;
		Placed.TriggerElement = InRig.Driver()->Find(FDreamBy::Widget(Trigger));
		InTest.TestTrue(TEXT("The driver can find the trigger it is about to act on"), Placed.IsReady());
		return Placed;
	}

	/** Click the trigger and let the popup the open lifted to the screen settle before anything aims at it. */
	bool OpenByClicking(FAutomationTestBase& InTest, FDreamDriverRig& InRig, const FPlacedMenu& InPlaced)
	{
		InTest.TestTrue(TEXT("Clicking the trigger completes"), InPlaced.TriggerElement->Click());
		InRig.PumpFrames(2);
		return InTest.TestTrue(TEXT("The trigger's click opened the menu"), InPlaced.Anchor->IsOpen());
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPressMenuAnchorToggleTest,
	"DreamGUI.MenuAnchor.ClickingTheTriggerOpensTheMenuAndClickingItAgainClosesIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPressMenuAnchorToggleTest::RunTest(const FString& Parameters)
{
	using namespace DreamPressMenuAnchorTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FPlacedMenu Placed = PlaceMenu(*this, Rig, Listener.Get());
	if (!Placed.IsReady())
	{
		return false;
	}
	TestFalse(TEXT("The menu starts closed"), Placed.Anchor->IsOpen());

	TestTrue(TEXT("Clicking the trigger completes"), Placed.TriggerElement->Click());
	TestTrue(TEXT("The first click opens the menu"), Placed.Anchor->IsOpen());
	if (TestEqual(TEXT("Opening was announced once"), Listener->MenuOpenStates.Num(), 1))
	{
		TestTrue(TEXT("As open"), Listener->MenuOpenStates[0]);
	}
	Rig.PumpFrames(2);

	TestTrue(TEXT("Clicking the trigger again completes"), Placed.TriggerElement->Click());
	TestFalse(TEXT("The second click closes the menu rather than opening it again"), Placed.Anchor->IsOpen());
	if (TestEqual(TEXT("Closing was announced once more"), Listener->MenuOpenStates.Num(), 2))
	{
		TestFalse(TEXT("As closed"), Listener->MenuOpenStates[1]);
	}
	return true;
}

/**
 * A press inside the menu is the menu's own business: the menu stack dismisses only on a press
 * OUTSIDE every open menu, so clicking an item that does nothing leaves the menu up.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPressMenuAnchorClickInsideTest,
	"DreamGUI.MenuAnchor.ClickingInsideTheOpenMenuLeavesItOpen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPressMenuAnchorClickInsideTest::RunTest(const FString& Parameters)
{
	using namespace DreamPressMenuAnchorTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FPlacedMenu Placed = PlaceMenu(*this, Rig, Listener.Get());
	if (!Placed.IsReady() || !OpenByClicking(*this, Rig, Placed))
	{
		return false;
	}

	FDreamElementRef Item = Rig.Driver()->Find(FDreamBy::Widget(Placed.MenuItem));
	TestTrue(TEXT("Clicking the item inside the open menu completes"), Item->Click());

	TestTrue(TEXT("A click inside the menu leaves it open"), Placed.Anchor->IsOpen());
	TestEqual(TEXT("And nothing announced a close"), Listener->MenuOpenStates.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPressMenuAnchorClickOutsideTest,
	"DreamGUI.MenuAnchor.ClickingOutsideTheOpenMenuClosesIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPressMenuAnchorClickOutsideTest::RunTest(const FString& Parameters)
{
	using namespace DreamPressMenuAnchorTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FPlacedMenu Placed = PlaceMenu(*this, Rig, Listener.Get());
	if (!Placed.IsReady() || !OpenByClicking(*this, Rig, Placed))
	{
		return false;
	}
	// The premise, stated: dismissing on an outside click is this anchor's default, as it is UMG's.
	TestTrue(TEXT("The anchor closes on a click outside by default"), Placed.Anchor->GetCloseOnClickOutside());

	TestTrue(TEXT("Clicking far from the menu completes"),
		Rig.Driver()->Sequence().MoveToPixel(FarFromTheMenu).Press().Release().Perform());

	TestFalse(TEXT("A click outside the open menu closes it"), Placed.Anchor->IsOpen());
	if (TestEqual(TEXT("The close was announced"), Listener->MenuOpenStates.Num(), 2))
	{
		TestFalse(TEXT("As closed"), Listener->MenuOpenStates[1]);
	}
	return true;
}

#endif
