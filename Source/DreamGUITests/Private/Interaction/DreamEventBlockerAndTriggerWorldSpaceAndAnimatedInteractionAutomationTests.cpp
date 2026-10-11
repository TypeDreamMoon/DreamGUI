// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"

#include "Controls/DreamButton.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamBaseEventData.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamPointerEventData.h"
#include "InputCoreTypes.h"
#include "Interaction/UIEventBlocker.h"
#include "Interaction/UIEventTrigger.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Driver/DreamDriverTypes.h"
#include "Driver/DreamDriverWorldSpace.h"
#include "Interaction/DreamTextNavInfraWorldInteractionTestHelpers.h"

/*
 * UUIEventBlocker AND UUIEventTrigger IN THE WORLD, AND A FINGER AND THE PAD ON THEM AS SHIPPED.
 *
 * Their reference is Slate's bubbling (see the bubbling tests next door): a pointer event goes to the widget under the
 * pointer and on up through its parents until one answers Handled (FEventRouter::Route with FBubblePolicy, RoutePointerDownEvent
 * and RoutePointerUpEvent). Both components answer Handled unless AllowEventBubbleUp says otherwise -- the trigger after it
 * has told its own listeners, the blocker having told nobody. A finger is the same press: FSlateApplication hands an
 * unanswered touch on as a mouse press, down the same path. In a widget component the path starts at the widget the
 * interaction's ray lands on, so a blocker on a panel in the level keeps a press on it from the card around it as it does on
 * the screen, and a trigger on a render-target surface hears the press that lands on it and not the one beside it.
 *
 * A trigger also hears the focus coming and going (OnPointerSelect / OnPointerDeselect, UMG's OnFocusReceived and
 * OnFocusLost): the D-pad moving the focus from one button to the next is heard by the trigger it arrives at and the one it
 * leaves, on the screen and among the widgets of a panel.
 */
namespace DreamEventBlockerTriggerWorldTestLocal
{
	using namespace DreamTextNavInfraWorldTest;

	struct FHeard
	{
		int32 Down = 0;
		int32 Click = 0;
		int32 Select = 0;
		int32 Deselect = 0;
	};

	/** A trigger on InWidget counting its presses, clicks, selects and deselects into OutHeard. */
	UUIEventTrigger* Listen(UDreamWidget* InWidget, FHeard& OutHeard)
	{
		UUIEventTrigger* Trigger = InWidget != nullptr ? InWidget->AddComponent<UUIEventTrigger>() : nullptr;
		if (Trigger != nullptr)
		{
			Trigger->GetOnPointerDownEvent().AddLambda([&OutHeard](UDreamPointerEventData*) { ++OutHeard.Down; });
			Trigger->GetOnPointerClickEvent().AddLambda([&OutHeard](UDreamPointerEventData*) { ++OutHeard.Click; });
			Trigger->GetOnPointerSelectEvent().AddLambda([&OutHeard](UDreamBaseEventData*) { ++OutHeard.Select; });
			Trigger->GetOnPointerDeselectEvent().AddLambda([&OutHeard](UDreamBaseEventData*) { ++OutHeard.Deselect; });
		}
		return Trigger;
	}

	/** A card 400 by 240 with a patch 120 by 60 in its middle, both hit-testable, under InParent (the rig's root when null). */
	bool MakeCardAndPatch(FAutomationTestBase& InTest, FDreamDriverRig& InRig, UDreamWidget* InParent, UDreamWidget*& OutCard, UDreamWidget*& OutPatch)
	{
		OutCard = InRig.MakeWidget(TEXT("Card"), InParent, FVector2D(400.0, 240.0));
		OutPatch = OutCard != nullptr ? InRig.MakeWidget(TEXT("Patch"), OutCard, FVector2D(120.0, 60.0)) : nullptr;
		if (InRig.EventSystem() != nullptr)
		{
			InRig.EventSystem()->SetDoubleClickTime(0.0f);
		}
		return InTest.TestTrue(TEXT("A card with a patch in it can be made"), OutCard != nullptr && OutPatch != nullptr);
	}

	/** The blocker's AllowEventBubbleUp, which has no setter: set the way the details panel sets it, through its property. */
	bool SetBlockerBubbleUp(FAutomationTestBase& InTest, UUIEventBlocker* InBlocker, bool bInBubbleUp)
	{
		FBoolProperty* BubbleUp = FindFProperty<FBoolProperty>(UUIEventBlocker::StaticClass(), TEXT("AllowEventBubbleUp"));
		if (!InTest.TestNotNull(TEXT("The blocker's AllowEventBubbleUp is a property"), BubbleUp))
		{
			return false;
		}
		BubbleUp->SetPropertyValue_InContainer(InBlocker, bInBubbleUp);
		return true;
	}

	UDreamButton* PlaceButton(FAutomationTestBase& InTest, FDreamDriverRig& InRig, const TCHAR* InName, UDreamWidget* InParent, const FVector2D& InPosition)
	{
		UDreamButton* Button = InRig.MakeControl<UDreamButton>(InName, InParent, FVector2D(160.0, 50.0), InPosition);
		const bool bMade = Button != nullptr && Button->FaceNode != nullptr;
		InTest.TestTrue(*FString::Printf(TEXT("A button '%s' with a face can be made"), InName), bMade);
		return bMade ? Button : nullptr;
	}

	/**
	 * Three buttons one above the other, under InParent (the rig's root when null), a trigger on each face: the upper one, one
	 * near below it and one further down. The D-pad down from the upper one stops on the near one: its trigger hears the focus
	 * arrive once, the upper one's hears it leave once, the far one's hears nothing. On down again, the near one's hears it
	 * leave and the far one's arrive.
	 */
	bool RunPadSelects(FAutomationTestBase& InTest, bool bInOnPanel)
	{
		FHeard UpperHeard;
		FHeard NearHeard;
		FHeard FarHeard;
		FDreamDriverRig Rig = FDreamDriverRig::Headless(RigOptions());
		Rig.BindTest(&InTest);
		if (!InTest.TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
		{
			return false;
		}
		UDreamWidget* Parent = nullptr;
		if (bInOnPanel)
		{
			const FWorldStage Stage = SetUpPanel(InTest, Rig);
			if (!Stage.IsReady())
			{
				return false;
			}
			Parent = Stage.Panel;
		}
		UDreamButton* Upper = PlaceButton(InTest, Rig, TEXT("Upper"), Parent, FVector2D(0.0, 110.0));
		UDreamButton* Near = Upper != nullptr ? PlaceButton(InTest, Rig, TEXT("Near"), Parent, FVector2D(0.0, 20.0)) : nullptr;
		UDreamButton* Far = Near != nullptr ? PlaceButton(InTest, Rig, TEXT("Far"), Parent, FVector2D(0.0, -120.0)) : nullptr;
		if (Far == nullptr
			|| !InTest.TestTrue(TEXT("Each button's face carries a trigger"),
				Listen(Upper->FaceNode, UpperHeard) != nullptr && Listen(Near->FaceNode, NearHeard) != nullptr && Listen(Far->FaceNode, FarHeard) != nullptr))
		{
			return false;
		}
		Rig.PumpFrames(2);
		if (!InTest.TestTrue(TEXT("Player 0's focus can be put on the upper button"), PutFocusOn(Rig, Upper->FaceNode)))
		{
			return false;
		}
		UpperHeard = FHeard();

		InTest.TestTrue(TEXT("The D-pad down completes"), PressKey(Rig, EKeys::Gamepad_DPad_Down));
		InTest.TestTrue(TEXT("The D-pad down stopped on the near button"), FocusIsIn(Rig, Near));
		InTest.TestEqual(TEXT("Its trigger heard the focus arrive once"), NearHeard.Select, 1);
		InTest.TestEqual(TEXT("The upper button's trigger heard it leave once"), UpperHeard.Deselect, 1);
		InTest.TestEqual(TEXT("The far button's trigger heard nothing"), FarHeard.Select + FarHeard.Deselect, 0);

		InTest.TestTrue(TEXT("The D-pad down again completes"), PressKey(Rig, EKeys::Gamepad_DPad_Down));
		InTest.TestTrue(TEXT("The second D-pad down went on to the far button"), FocusIsIn(Rig, Far));
		InTest.TestEqual(TEXT("The near button's trigger heard the focus leave once"), NearHeard.Deselect, 1);
		InTest.TestEqual(TEXT("...and the far one's heard it arrive once"), FarHeard.Select, 1);
		InTest.TestEqual(TEXT("Moving the focus clicked nothing"), UpperHeard.Click + NearHeard.Click + FarHeard.Click, 0);
		return true;
	}
}

// ------------------------------------------------------------------------------------------------ the blocker

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWorldEventBlockerPressTest,
	"DreamGUI.EventBlocker.OnAWorldPanelABlockerKeepsAClickOrATapOnItsPatchFromTheCardButNotOneBesideIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamWorldEventBlockerPressTest, "DreamGUI.EventBlocker.OnAWorldPanelABlockerKeepsAClickOrATapOnItsPatchFromTheCardButNotOneBesideIt", "[Pointer][Touch][World]")

/*
 * On a panel, a blocker on the patch and a trigger on the card. A click and a finger's tap on the patch through the world
 * pointer are kept from the card -- the presses and the clicks; a click and a tap on the card 130 cm beside the patch are the
 * card's.
 */
bool FDreamWorldEventBlockerPressTest::RunTest(const FString& Parameters)
{
	using namespace DreamEventBlockerTriggerWorldTestLocal;
	FHeard Card;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(RigOptions());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	const FWorldStage Stage = SetUpPanel(*this, Rig);
	UDreamWidget* CardWidget = nullptr;
	UDreamWidget* Patch = nullptr;
	if (!Stage.IsReady() || !MakeCardAndPatch(*this, Rig, Stage.Panel, CardWidget, Patch))
	{
		return false;
	}
	if (!TestNotNull(TEXT("The card carries a trigger"), Listen(CardWidget, Card))
		|| !TestNotNull(TEXT("The patch carries a blocker"), Patch->AddComponent<UUIEventBlocker>()))
	{
		return false;
	}
	Rig.PumpFrames(2);
	FDreamElementRef PatchElement = Rig.Driver()->Find(FDreamBy::Widget(Patch));
	const TOptional<FVector2D> OnPatch = PatchElement->GetCentrePixel();
	if (!TestTrue(TEXT("The camera sees the patch on the panel"), OnPatch.IsSet()))
	{
		return false;
	}
	const FVector2D BesideThePatch = OnPatch.GetValue() + FVector2D(130.0 * PixelsPerCentimetre(), 0.0);

	TestTrue(TEXT("Clicking the patch completes"), PatchElement->Click());
	TestTrue(TEXT("A finger tapped on the patch completes"), PatchElement->Tap(0));
	TestEqual(TEXT("The blocker kept the click and the tap from the card"), Card.Click, 0);
	TestEqual(TEXT("...and their presses"), Card.Down, 0);

	TestTrue(TEXT("Clicking the card beside the patch completes"), Rig.Driver()->Sequence().MoveToPixel(BesideThePatch).Press().Release().Perform());
	TestEqual(TEXT("A click on the card beside the patch is the card's"), Card.Click, 1);
	TestTrue(TEXT("A finger tapped beside the patch completes"), Rig.Driver()->Sequence().TouchDown(1, BesideThePatch).TouchUp(1).Perform());
	TestEqual(TEXT("...and so is a tap there"), Card.Click, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamAnimatedEventBlockerTapTest,
	"DreamGUI.EventBlocker.ABlockerKeepsAFingersTapFromItsParentUnlessItLetsTheEventBubbleUp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamAnimatedEventBlockerTapTest, "DreamGUI.EventBlocker.ABlockerKeepsAFingersTapFromItsParentUnlessItLetsTheEventBubbleUp", "[Touch][Animated]")

/*
 * A blocker on the patch and a trigger on the card. A finger tapped on the patch reaches the card not at all -- neither the
 * press nor the click. With the blocker letting the event bubble up, the same tap reaches the card.
 */
bool FDreamAnimatedEventBlockerTapTest::RunTest(const FString& Parameters)
{
	using namespace DreamEventBlockerTriggerWorldTestLocal;
	FHeard Card;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(RigOptions());
	Rig.BindTest(this);
	UDreamWidget* CardWidget = nullptr;
	UDreamWidget* Patch = nullptr;
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()) || !MakeCardAndPatch(*this, Rig, nullptr, CardWidget, Patch))
	{
		return false;
	}
	UUIEventBlocker* Blocker = Patch->AddComponent<UUIEventBlocker>();
	if (!TestNotNull(TEXT("The card carries a trigger"), Listen(CardWidget, Card)) || !TestNotNull(TEXT("The patch carries a blocker"), Blocker))
	{
		return false;
	}
	Rig.PumpFrames(1);
	FDreamElementRef PatchElement = Rig.Driver()->Find(FDreamBy::Widget(Patch));

	TestTrue(TEXT("A finger tapped on the patch completes"), PatchElement->Tap(0));
	TestEqual(TEXT("The blocker kept the tap's press from the card"), Card.Down, 0);
	TestEqual(TEXT("...and its click"), Card.Click, 0);

	if (!SetBlockerBubbleUp(*this, Blocker, true))
	{
		return false;
	}
	TestTrue(TEXT("A finger tapped on the patch again completes"), PatchElement->Tap(1));
	TestEqual(TEXT("Let bubble up, the tap's press reached the card"), Card.Down, 1);
	TestEqual(TEXT("...and its click"), Card.Click, 1);
	return true;
}

// ------------------------------------------------------------------------------------------------ the trigger

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamAnimatedEventTriggerTapTest,
	"DreamGUI.EventTrigger.AFingerTapATriggerHearsGoesNoFurtherUnlessItLetsTheEventBubbleUp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamAnimatedEventTriggerTapTest, "DreamGUI.EventTrigger.AFingerTapATriggerHearsGoesNoFurtherUnlessItLetsTheEventBubbleUp", "[Touch][Animated]")

/*
 * A trigger on the patch and one on the card. A finger tapped on the patch: its trigger hears the press and the click, and
 * by default the card's hears neither. With the patch's trigger letting the event bubble up, the same tap is heard by both.
 */
bool FDreamAnimatedEventTriggerTapTest::RunTest(const FString& Parameters)
{
	using namespace DreamEventBlockerTriggerWorldTestLocal;
	FHeard Card;
	FHeard PatchHeard;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(RigOptions());
	Rig.BindTest(this);
	UDreamWidget* CardWidget = nullptr;
	UDreamWidget* Patch = nullptr;
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()) || !MakeCardAndPatch(*this, Rig, nullptr, CardWidget, Patch))
	{
		return false;
	}
	UUIEventTrigger* PatchTrigger = Listen(Patch, PatchHeard);
	if (!TestTrue(TEXT("Both carry a trigger"), Listen(CardWidget, Card) != nullptr && PatchTrigger != nullptr))
	{
		return false;
	}
	Rig.PumpFrames(1);
	FDreamElementRef PatchElement = Rig.Driver()->Find(FDreamBy::Widget(Patch));

	TestTrue(TEXT("A finger tapped on the patch completes"), PatchElement->Tap(0));
	TestEqual(TEXT("The patch's trigger heard the press"), PatchHeard.Down, 1);
	TestEqual(TEXT("...and the click"), PatchHeard.Click, 1);
	TestEqual(TEXT("The card around it heard no press"), Card.Down, 0);
	TestEqual(TEXT("...and no click"), Card.Click, 0);

	PatchTrigger->SetAllowEventBubbleUp(true);
	TestTrue(TEXT("A finger tapped on the patch again completes"), PatchElement->Tap(1));
	TestEqual(TEXT("The patch's trigger heard this tap too"), PatchHeard.Click, 2);
	TestEqual(TEXT("...and, let bubble up, so did the card: the press"), Card.Down, 1);
	TestEqual(TEXT("...and the click"), Card.Click, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSurfaceEventTriggerTapTest,
	"DreamGUI.EventTrigger.ShownOnARenderTargetSurfaceAFingerTapIsHeardByTheWidgetItLandsOnAndNotByTheOneBesideIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamSurfaceEventTriggerTapTest, "DreamGUI.EventTrigger.ShownOnARenderTargetSurfaceAFingerTapIsHeardByTheWidgetItLandsOnAndNotByTheOneBesideIt", "[Touch][World]")

/*
 * A target and a decoy across from it on a canvas a mesh in the level shows. A finger tapped through the surface on the
 * target's centre is heard by the target's trigger -- a press and a click -- and the decoy hears nothing; a tap on the decoy
 * is the decoy's and not the target's.
 */
bool FDreamSurfaceEventTriggerTapTest::RunTest(const FString& Parameters)
{
	using namespace DreamEventBlockerTriggerWorldTestLocal;
	FHeard Target;
	FHeard Decoy;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(RigOptions());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	const FSurfaceStage Stage = SetUpSurface(*this, Rig);
	UDreamWidget* TargetWidget = Stage.IsReady() ? Rig.MakeWidget(TEXT("Target"), Stage.Screen.CanvasRoot, FVector2D(120.0, 60.0), FVector2D(90.0, 50.0)) : nullptr;
	UDreamWidget* DecoyWidget = Stage.IsReady() ? Rig.MakeWidget(TEXT("Decoy"), Stage.Screen.CanvasRoot, FVector2D(120.0, 60.0), FVector2D(-90.0, -50.0)) : nullptr;
	if (!TestTrue(TEXT("Both widgets on the surface carry a trigger"), Listen(TargetWidget, Target) != nullptr && Listen(DecoyWidget, Decoy) != nullptr))
	{
		return false;
	}
	if (Rig.EventSystem() != nullptr)
	{
		Rig.EventSystem()->SetDoubleClickTime(0.0f);
	}
	Rig.PumpFrames(2);
	const TOptional<FVector2D> OnTarget = PixelOf(Rig, TargetWidget);
	const TOptional<FVector2D> OnDecoy = PixelOf(Rig, DecoyWidget);
	if (!TestTrue(TEXT("The camera sees both on the surface"), OnTarget.IsSet() && OnDecoy.IsSet()))
	{
		return false;
	}

	TestTrue(TEXT("A finger tapped on the target through the surface completes"),
		Rig.Driver()->Sequence().TouchDown(0, OnTarget.GetValue()).TouchUp(0).WaitFrames(1).Perform());
	TestEqual(TEXT("The target's trigger heard the press"), Target.Down, 1);
	TestEqual(TEXT("...and the click"), Target.Click, 1);
	TestEqual(TEXT("The decoy heard nothing"), Decoy.Down + Decoy.Click, 0);

	TestTrue(TEXT("A finger tapped on the decoy through the surface completes"),
		Rig.Driver()->Sequence().TouchDown(1, OnDecoy.GetValue()).TouchUp(1).WaitFrames(1).Perform());
	TestEqual(TEXT("The decoy's trigger heard that tap"), Decoy.Click, 1);
	TestEqual(TEXT("...and the target's did not"), Target.Click, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWorldEventTriggerPadTest,
	"DreamGUI.EventTrigger.OnAWorldPanelTheDPadMovingTheFocusIsHeardByTheTriggerItArrivesAtAndTheOneItLeaves",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamWorldEventTriggerPadTest, "DreamGUI.EventTrigger.OnAWorldPanelTheDPadMovingTheFocusIsHeardByTheTriggerItArrivesAtAndTheOneItLeaves", "[Nav][World]")

bool FDreamWorldEventTriggerPadTest::RunTest(const FString& Parameters)
{
	using namespace DreamEventBlockerTriggerWorldTestLocal;
	return RunPadSelects(*this, true);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamAnimatedEventTriggerPadTest,
	"DreamGUI.EventTrigger.TheDPadMovingTheFocusIsHeardByTheTriggerItArrivesAtAndTheOneItLeaves",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamAnimatedEventTriggerPadTest, "DreamGUI.EventTrigger.TheDPadMovingTheFocusIsHeardByTheTriggerItArrivesAtAndTheOneItLeaves", "[Nav][Animated]")

bool FDreamAnimatedEventTriggerPadTest::RunTest(const FString& Parameters)
{
	using namespace DreamEventBlockerTriggerWorldTestLocal;
	return RunPadSelects(*this, false);
}

#endif
