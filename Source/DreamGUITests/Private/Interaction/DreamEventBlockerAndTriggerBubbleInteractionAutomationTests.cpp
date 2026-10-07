// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"

#include "Core/Components/DreamWidget.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamPointerEventData.h"
#include "Interaction/UIEventBlocker.h"
#include "Interaction/UIEventTrigger.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"

/*
 * Whether a click goes on past the widget it landed on: UUIEventTrigger and UUIEventBlocker, and their AllowEventBubbleUp.
 *
 * The reference is Slate's bubbling: a pointer event is routed from the widget under the pointer up through its parents and
 * stops at the first one whose handler answers Handled (FEventRouter::Route with FBubblePolicy, Slate/Private/Framework/
 * Application/SlateApplication.cpp, RoutePointerDownEvent and RoutePointerUpEvent). Both components answer Handled unless
 * AllowEventBubbleUp says otherwise -- the trigger after it has told its own listeners, the blocker having told nobody. A
 * parent with a trigger of its own is where a click that went on would be heard.
 */
namespace DreamEventBubbleTestLocal
{
	const FIntPoint ViewportSize(1280, 720);

	struct FClicks
	{
		int32 Down = 0;
		int32 Click = 0;
	};

	/** A trigger on InWidget counting its presses and clicks into OutClicks. */
	UUIEventTrigger* CountClicks(UDreamWidget* InWidget, FClicks& OutClicks)
	{
		UUIEventTrigger* Trigger = InWidget != nullptr ? InWidget->AddComponent<UUIEventTrigger>() : nullptr;
		if (Trigger != nullptr)
		{
			Trigger->GetOnPointerDownEvent().AddLambda([&OutClicks](UDreamPointerEventData*) { ++OutClicks.Down; });
			Trigger->GetOnPointerClickEvent().AddLambda([&OutClicks](UDreamPointerEventData*) { ++OutClicks.Click; });
		}
		return Trigger;
	}

	/** A card with a smaller button-sized patch in its middle, both hit-testable. */
	bool MakeCardAndPatch(FAutomationTestBase& InTest, FDreamDriverRig& InRig, UDreamWidget*& OutCard, UDreamWidget*& OutPatch)
	{
		InRig.BindTest(&InTest);
		if (!InTest.TestTrue(TEXT("The headless rig came up"), InRig.IsUsable()))
		{
			return false;
		}
		OutCard = InRig.MakeWidget(TEXT("Card"), nullptr, FVector2D(400.0, 300.0));
		OutPatch = OutCard != nullptr ? InRig.MakeWidget(TEXT("Patch"), OutCard, FVector2D(120.0, 60.0)) : nullptr;
		InRig.EventSystem()->SetDoubleClickTime(0.0f);
		return InTest.TestTrue(TEXT("A card with a patch in it can be made on the rig"), OutCard != nullptr && OutPatch != nullptr);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamEventTriggerBubbleTest,
	"DreamGUI.EventTrigger.AClickATriggerHearsGoesNoFurtherUnlessItLetsTheEventBubbleUp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamEventTriggerBubbleTest, "DreamGUI.EventTrigger.AClickATriggerHearsGoesNoFurtherUnlessItLetsTheEventBubbleUp", "[Pointer][Animated]")

/*
 * A trigger on the patch and one on the card around it. A click on the patch: its trigger hears it and, by default, the
 * card's does not. With the patch's trigger letting the event bubble up, the same click is heard by both.
 */
bool FDreamEventTriggerBubbleTest::RunTest(const FString& Parameters)
{
	using namespace DreamEventBubbleTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	UDreamWidget* Card = nullptr;
	UDreamWidget* Patch = nullptr;
	if (!MakeCardAndPatch(*this, Rig, Card, Patch))
	{
		return false;
	}
	FClicks CardClicks;
	FClicks PatchClicks;
	UUIEventTrigger* CardTrigger = CountClicks(Card, CardClicks);
	UUIEventTrigger* PatchTrigger = CountClicks(Patch, PatchClicks);
	if (!TestTrue(TEXT("Both carry a trigger"), CardTrigger != nullptr && PatchTrigger != nullptr))
	{
		return false;
	}
	Rig.PumpFrames(1);
	TestFalse(TEXT("A trigger keeps the events it hears by default"), PatchTrigger->GetAllowEventBubbleUp());
	FDreamElementRef PatchElement = Rig.Driver()->Find(FDreamBy::Widget(Patch));

	TestTrue(TEXT("Clicking the patch completes"), PatchElement->Click());
	TestEqual(TEXT("The patch's trigger heard the press"), PatchClicks.Down, 1);
	TestEqual(TEXT("...and the click"), PatchClicks.Click, 1);
	TestEqual(TEXT("The card around it heard no press"), CardClicks.Down, 0);
	TestEqual(TEXT("...and no click"), CardClicks.Click, 0);

	PatchTrigger->SetAllowEventBubbleUp(true);
	TestTrue(TEXT("Clicking the patch again completes"), PatchElement->Click());
	TestEqual(TEXT("The patch's trigger heard this click too"), PatchClicks.Click, 2);
	TestEqual(TEXT("...and, let bubble up, so did the card: the press"), CardClicks.Down, 1);
	TestEqual(TEXT("...and the click"), CardClicks.Click, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamEventBlockerBubbleTest,
	"DreamGUI.EventBlocker.ABlockerKeepsAClickFromItsParentUnlessItLetsTheEventBubbleUp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamEventBlockerBubbleTest, "DreamGUI.EventBlocker.ABlockerKeepsAClickFromItsParentUnlessItLetsTheEventBubbleUp", "[Pointer][Animated]")

/*
 * A blocker on the patch and a trigger on the card. A click on the patch reaches the card by default not at all. The
 * blocker's AllowEventBubbleUp has no setter; it is set the way the details panel sets it, through its property, and with it
 * on the same click reaches the card.
 */
bool FDreamEventBlockerBubbleTest::RunTest(const FString& Parameters)
{
	using namespace DreamEventBubbleTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	UDreamWidget* Card = nullptr;
	UDreamWidget* Patch = nullptr;
	if (!MakeCardAndPatch(*this, Rig, Card, Patch))
	{
		return false;
	}
	FClicks CardClicks;
	UUIEventTrigger* CardTrigger = CountClicks(Card, CardClicks);
	UUIEventBlocker* Blocker = Patch->AddComponent<UUIEventBlocker>();
	FBoolProperty* BubbleUp = FindFProperty<FBoolProperty>(UUIEventBlocker::StaticClass(), TEXT("AllowEventBubbleUp"));
	if (!TestTrue(TEXT("The card carries a trigger and the patch a blocker"), CardTrigger != nullptr && Blocker != nullptr)
		|| !TestNotNull(TEXT("The blocker's AllowEventBubbleUp is a property"), BubbleUp))
	{
		return false;
	}
	Rig.PumpFrames(1);
	TestFalse(TEXT("A blocker keeps the events it hears by default"), BubbleUp->GetPropertyValue_InContainer(Blocker));
	FDreamElementRef PatchElement = Rig.Driver()->Find(FDreamBy::Widget(Patch));

	TestTrue(TEXT("Clicking the patch completes"), PatchElement->Click());
	TestEqual(TEXT("The blocker kept the press from the card"), CardClicks.Down, 0);
	TestEqual(TEXT("...and the click"), CardClicks.Click, 0);

	BubbleUp->SetPropertyValue_InContainer(Blocker, true);
	TestTrue(TEXT("Clicking the patch again completes"), PatchElement->Click());
	TestEqual(TEXT("Let bubble up, the press reached the card"), CardClicks.Down, 1);
	TestEqual(TEXT("...and the click"), CardClicks.Click, 1);

	// Around the patch the card is clicked directly, blocker or not.
	TestTrue(TEXT("Clicking the card beside the patch completes"),
		Rig.Driver()->Sequence().MoveTo(FDreamBy::Widget(Card)).MoveBy(FVector2D(150.0, 0.0)).Press().Release().Perform());
	TestEqual(TEXT("A click on the card itself is the card's"), CardClicks.Click, 2);
	return true;
}

#endif
