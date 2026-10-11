// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"

#include "Controls/DreamButton.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIInputServices.h"
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

/*
 * UUIEventBlocker AND UUIEventTrigger ON A DISABLED WIDGET, AND ON A SCALED CANVAS.
 *
 * Their reference is Slate's bubbling (see the bubbling tests next door): a pointer event goes to the widget under the
 * pointer and on up through its parents until one answers Handled. A disabled widget is cut out of that path together with
 * everything under it -- FHittestGrid::GetBubblePath removes the path from the first disabled widget down
 * (SlateCore/Private/Input/HittestGrid.cpp:236-247) -- and what is left still starts at the enabled parent. So a click or a
 * finger on a disabled patch inside an enabled card is not heard by the patch's trigger and not kept by the patch's
 * blocker: it is the card's, as if it had landed on the card itself. Enabled again, the patch hears it and its blocker
 * keeps it.
 *
 * A trigger also hears the focus coming and going (OnPointerSelect / OnPointerDeselect, UMG's OnFocusReceived and
 * OnFocusLost). The D-pad passes over a disabled widget (FHittestGrid::FindNextFocusableWidget), so its trigger never hears
 * the focus arrive; on a canvas scaled to half and to double the D-pad picks the next widget by where it is drawn.
 *
 * On a scaled canvas the click and the finger land on the patch or beside it where the patch is drawn, and the blocker keeps
 * or does not keep the event accordingly.
 */
namespace DreamEventBlockerTriggerCoverageTestLocal
{
	const FIntPoint ViewportSize(1280, 720);
	const double Scales[] = { 0.5, 2.0 };

	FDreamRigOptions ScaledOptions(double InScale)
	{
		FDreamRigOptions Options;
		Options.ViewportSize = ViewportSize;
		Options.CanvasScaleMode = EDreamCanvasScaleMode::ScaleWithScreenSize;
		Options.ReferenceResolution = FVector2D(ViewportSize.X / InScale, ViewportSize.Y / InScale);
		Options.MatchFromWidthToHeight = 1.0f;
		return Options;
	}

	FString At(double InScale, const FString& InWhat)
	{
		return FString::Printf(TEXT("[scale %.1f] %s"), InScale, *InWhat);
	}

	bool ComeUp(FAutomationTestBase& InTest, FDreamDriverRig& InRig, double InScale)
	{
		InRig.BindTest(&InTest);
		return InTest.TestTrue(At(InScale, TEXT("The rig came up")), InRig.IsUsable())
			&& InTest.TestNearlyEqual(*At(InScale, TEXT("The canvas is at the scale asked for")),
				static_cast<double>(InRig.RootCanvas()->GetCanvasScale()), InScale, 1.0e-3);
	}

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

	/** A card 400 by 240 units with a patch 120 by 60 in its middle, both hit-testable. */
	bool MakeCardAndPatch(FAutomationTestBase& InTest, FDreamDriverRig& InRig, UDreamWidget*& OutCard, UDreamWidget*& OutPatch)
	{
		OutCard = InRig.MakeWidget(TEXT("Card"), nullptr, FVector2D(400.0, 240.0));
		OutPatch = OutCard != nullptr ? InRig.MakeWidget(TEXT("Patch"), OutCard, FVector2D(120.0, 60.0)) : nullptr;
		if (InRig.EventSystem() != nullptr)
		{
			InRig.EventSystem()->SetDoubleClickTime(0.0f);
		}
		return InTest.TestTrue(TEXT("A card with a patch in it can be made on the rig"), OutCard != nullptr && OutPatch != nullptr);
	}

	UDreamButton* PlaceButton(FAutomationTestBase& InTest, FDreamDriverRig& InRig, const TCHAR* InName, const FVector2D& InPosition)
	{
		UDreamButton* Button = InRig.MakeControl<UDreamButton>(InName, nullptr, FVector2D(160.0, 50.0), InPosition);
		const bool bMade = Button != nullptr && Button->FaceNode != nullptr;
		InTest.TestTrue(*FString::Printf(TEXT("A button '%s' with a face can be made"), InName), bMade);
		return bMade ? Button : nullptr;
	}

	bool IsPartOf(const UDreamWidget* InWidget, const UDreamWidget* InControl)
	{
		return InWidget != nullptr && InControl != nullptr && (InWidget == InControl || InWidget->IsChildOf(InControl));
	}

	UDreamWidget* FocusOf(const FDreamDriverRig& InRig)
	{
		const UDreamUIInputServices* Services = UDreamUIInputServices::Get(InRig.GetWorld());
		return Services != nullptr ? Services->GetFocusedWidget(0) : nullptr;
	}

	bool FocusOn(FAutomationTestBase& InTest, FDreamDriverRig& InRig, UDreamButton* InButton, const FString& InWhat)
	{
		UDreamUIInputServices* Services = UDreamUIInputServices::Get(InRig.GetWorld());
		return InTest.TestTrue(InWhat, Services != nullptr && InButton != nullptr && Services->FocusForNavigation(InButton->FaceNode, 0));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCoverageEventBlockerDisabledTest,
	"DreamGUI.EventBlocker.AClickOrATapOnADisabledPatchGoesPastItsBlockerToTheCardAroundIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamCoverageEventBlockerDisabledTest, "DreamGUI.EventBlocker.AClickOrATapOnADisabledPatchGoesPastItsBlockerToTheCardAroundIt", "[Pointer][Touch][Disabled]")

/*
 * A blocker on the patch and a trigger on the card. The patch disabled: its path is cut off at it, the blocker with it, so a
 * click on the patch is the card's -- its press and its click -- and so is a finger's tap. Enabled again, the blocker keeps
 * the next click from the card.
 */
bool FDreamCoverageEventBlockerDisabledTest::RunTest(const FString& Parameters)
{
	using namespace DreamEventBlockerTriggerCoverageTestLocal;
	FHeard Card;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	UDreamWidget* CardWidget = nullptr;
	UDreamWidget* Patch = nullptr;
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()) || !MakeCardAndPatch(*this, Rig, CardWidget, Patch))
	{
		return false;
	}
	if (!TestNotNull(TEXT("The card carries a trigger"), Listen(CardWidget, Card))
		|| !TestNotNull(TEXT("The patch carries a blocker"), Patch->AddComponent<UUIEventBlocker>()))
	{
		return false;
	}
	Patch->SetIsEnabled(false);
	Rig.PumpFrames(1);
	FDreamElementRef PatchElement = Rig.Driver()->Find(FDreamBy::Widget(Patch));

	TestTrue(TEXT("Clicking the disabled patch completes"), PatchElement->Click());
	TestEqual(TEXT("The click on the disabled patch was the card's: its press"), Card.Down, 1);
	TestEqual(TEXT("...and its click"), Card.Click, 1);
	TestTrue(TEXT("A finger tapped on the disabled patch completes"), PatchElement->Tap(0));
	TestEqual(TEXT("The tap was the card's too"), Card.Click, 2);

	Patch->SetIsEnabled(true);
	Rig.PumpFrames(1);
	TestTrue(TEXT("Clicking the patch once it is enabled completes"), PatchElement->Click());
	TestEqual(TEXT("Enabled, the patch's blocker kept the click from the card"), Card.Click, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCoverageEventBlockerScaledTest,
	"DreamGUI.EventBlocker.OnACanvasScaledToHalfAndToDoubleABlockerKeepsAClickOrATapOnItsPatchButNotOneBesideItFromTheCard",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamCoverageEventBlockerScaledTest, "DreamGUI.EventBlocker.OnACanvasScaledToHalfAndToDoubleABlockerKeepsAClickOrATapOnItsPatchButNotOneBesideItFromTheCard", "[Pointer][Touch][Scaled]")

/*
 * At half and at double: a click and a tap on the patch, where it is drawn, are kept from the card; a click and a tap on the
 * card 130 units beside the patch -- 65 pixels at half, 260 at double -- are the card's.
 */
bool FDreamCoverageEventBlockerScaledTest::RunTest(const FString& Parameters)
{
	using namespace DreamEventBlockerTriggerCoverageTestLocal;
	for (const double Scale : Scales)
	{
		FHeard Card;
		FDreamDriverRig Rig = FDreamDriverRig::Headless(ScaledOptions(Scale));
		UDreamWidget* CardWidget = nullptr;
		UDreamWidget* Patch = nullptr;
		if (!ComeUp(*this, Rig, Scale) || !MakeCardAndPatch(*this, Rig, CardWidget, Patch))
		{
			return false;
		}
		if (!TestNotNull(*At(Scale, TEXT("The card carries a trigger")), Listen(CardWidget, Card))
			|| !TestNotNull(*At(Scale, TEXT("The patch carries a blocker")), Patch->AddComponent<UUIEventBlocker>()))
		{
			return false;
		}
		Rig.PumpFrames(1);
		FDreamElementRef PatchElement = Rig.Driver()->Find(FDreamBy::Widget(Patch));
		const TOptional<FVector2D> OnPatch = PatchElement->GetCentrePixel();
		if (!TestTrue(At(Scale, TEXT("The patch is on the viewport")), OnPatch.IsSet()))
		{
			return false;
		}
		const FVector2D BesideThePatch = OnPatch.GetValue() + FVector2D(130.0 * Scale, 0.0);

		TestTrue(At(Scale, TEXT("Clicking the patch completes")), PatchElement->Click());
		TestTrue(At(Scale, TEXT("A finger tapped on the patch completes")), PatchElement->Tap(0));
		TestEqual(*At(Scale, TEXT("The blocker kept the click and the tap from the card")), Card.Click, 0);
		TestEqual(*At(Scale, TEXT("...and their presses")), Card.Down, 0);

		TestTrue(At(Scale, TEXT("Clicking the card beside the patch completes")), Rig.Driver()->Sequence().MoveToPixel(BesideThePatch).Press().Release().Perform());
		TestEqual(*At(Scale, TEXT("A click on the card beside the patch is the card's")), Card.Click, 1);
		TestTrue(At(Scale, TEXT("A finger tapped beside the patch completes")), Rig.Driver()->Sequence().TouchDown(1, BesideThePatch).TouchUp(1).Perform());
		TestEqual(*At(Scale, TEXT("...and so is a tap there")), Card.Click, 2);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCoverageEventTriggerDisabledPressTest,
	"DreamGUI.EventTrigger.AClickOrATapOnADisabledPatchIsNotHeardByItsTriggerButByTheCardAroundIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamCoverageEventTriggerDisabledPressTest, "DreamGUI.EventTrigger.AClickOrATapOnADisabledPatchIsNotHeardByItsTriggerButByTheCardAroundIt", "[Pointer][Touch][Disabled]")

/*
 * A trigger on the patch and one on the card. The patch disabled: a click and a tap on it are not heard by the patch's
 * trigger, and are the card's. Enabled again, the patch's trigger hears the click and, keeping it, the card does not.
 */
bool FDreamCoverageEventTriggerDisabledPressTest::RunTest(const FString& Parameters)
{
	using namespace DreamEventBlockerTriggerCoverageTestLocal;
	FHeard Card;
	FHeard PatchHeard;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	UDreamWidget* CardWidget = nullptr;
	UDreamWidget* Patch = nullptr;
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()) || !MakeCardAndPatch(*this, Rig, CardWidget, Patch))
	{
		return false;
	}
	if (!TestTrue(TEXT("Both carry a trigger"), Listen(CardWidget, Card) != nullptr && Listen(Patch, PatchHeard) != nullptr))
	{
		return false;
	}
	Patch->SetIsEnabled(false);
	Rig.PumpFrames(1);
	FDreamElementRef PatchElement = Rig.Driver()->Find(FDreamBy::Widget(Patch));

	TestTrue(TEXT("Clicking the disabled patch completes"), PatchElement->Click());
	TestTrue(TEXT("A finger tapped on it completes"), PatchElement->Tap(0));
	TestEqual(TEXT("The disabled patch's trigger heard no press"), PatchHeard.Down, 0);
	TestEqual(TEXT("...and no click"), PatchHeard.Click, 0);
	TestEqual(TEXT("The card around it heard both presses"), Card.Down, 2);
	TestEqual(TEXT("...and both clicks"), Card.Click, 2);

	Patch->SetIsEnabled(true);
	Rig.PumpFrames(1);
	TestTrue(TEXT("Clicking the patch once it is enabled completes"), PatchElement->Click());
	TestEqual(TEXT("Enabled, the patch's trigger heard the click"), PatchHeard.Click, 1);
	TestEqual(TEXT("...and kept it from the card"), Card.Click, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCoverageEventTriggerScaledTapTest,
	"DreamGUI.EventTrigger.OnACanvasScaledToHalfAndToDoubleAFingerTapOnAWidgetsCentreIsHeardByItAndNotByTheOneBesideIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamCoverageEventTriggerScaledTapTest, "DreamGUI.EventTrigger.OnACanvasScaledToHalfAndToDoubleAFingerTapOnAWidgetsCentreIsHeardByItAndNotByTheOneBesideIt", "[Touch][Scaled]")

/*
 * At half and at double: a target off the canvas's centre and a decoy across from it. A finger tapped on the target's centre
 * as drawn is heard by the target's trigger -- a press and a click -- and the decoy hears nothing.
 */
bool FDreamCoverageEventTriggerScaledTapTest::RunTest(const FString& Parameters)
{
	using namespace DreamEventBlockerTriggerCoverageTestLocal;
	for (const double Scale : Scales)
	{
		FHeard Target;
		FHeard Decoy;
		FDreamDriverRig Rig = FDreamDriverRig::Headless(ScaledOptions(Scale));
		if (!ComeUp(*this, Rig, Scale))
		{
			return false;
		}
		UDreamWidget* TargetWidget = Rig.MakeWidget(TEXT("Target"), nullptr, FVector2D(160.0, 80.0), FVector2D(150.0, 60.0));
		UDreamWidget* DecoyWidget = Rig.MakeWidget(TEXT("Decoy"), nullptr, FVector2D(160.0, 80.0), FVector2D(-150.0, -60.0));
		if (!TestTrue(At(Scale, TEXT("Both widgets carry a trigger")), Listen(TargetWidget, Target) != nullptr && Listen(DecoyWidget, Decoy) != nullptr))
		{
			return false;
		}
		Rig.PumpFrames(1);

		TestTrue(At(Scale, TEXT("A finger tapped on the target's centre completes")), Rig.Driver()->Find(FDreamBy::Widget(TargetWidget))->Tap(0));
		TestEqual(*At(Scale, TEXT("The target's trigger heard the press")), Target.Down, 1);
		TestEqual(*At(Scale, TEXT("...and the click")), Target.Click, 1);
		TestEqual(*At(Scale, TEXT("The decoy heard nothing")), Decoy.Down + Decoy.Click, 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCoverageEventTriggerDisabledPadTest,
	"DreamGUI.EventTrigger.TheDPadPassesOverADisabledButtonWhoseTriggerHearsNoSelectUntilItIsEnabled",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamCoverageEventTriggerDisabledPadTest, "DreamGUI.EventTrigger.TheDPadPassesOverADisabledButtonWhoseTriggerHearsNoSelectUntilItIsEnabled", "[Nav][Disabled]")

/*
 * Three buttons in a row, a trigger on the middle one's face, the middle one disabled. The D-pad right from the first lands
 * on the last; the middle one's trigger heard the focus neither arrive nor leave. Enabled again, the D-pad right from the
 * first stops on it, and its trigger hears the focus arrive once.
 */
bool FDreamCoverageEventTriggerDisabledPadTest::RunTest(const FString& Parameters)
{
	using namespace DreamEventBlockerTriggerCoverageTestLocal;
	FHeard MiddleHeard;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	UDreamButton* First = Rig.IsUsable() ? PlaceButton(*this, Rig, TEXT("First"), FVector2D(-300.0, 0.0)) : nullptr;
	UDreamButton* Middle = First != nullptr ? PlaceButton(*this, Rig, TEXT("Middle"), FVector2D(0.0, 0.0)) : nullptr;
	UDreamButton* Last = Middle != nullptr ? PlaceButton(*this, Rig, TEXT("Last"), FVector2D(300.0, 0.0)) : nullptr;
	if (Last == nullptr || !TestNotNull(TEXT("The middle button's face carries a trigger"), Listen(Middle->FaceNode, MiddleHeard)))
	{
		return false;
	}
	Middle->SetIsEnabled(false);
	Rig.PumpFrames(1);
	if (!FocusOn(*this, Rig, First, TEXT("Player 0's focus can be put on the first button")))
	{
		return false;
	}

	TestTrue(TEXT("The D-pad right completes"), Rig.Driver()->Sequence().Key(EKeys::Gamepad_DPad_Right).WaitFrames(1).Perform());
	TestTrue(TEXT("The D-pad passed over the disabled button to the last"), IsPartOf(FocusOf(Rig), Last));
	TestEqual(TEXT("The disabled button's trigger heard the focus neither arrive"), MiddleHeard.Select, 0);
	TestEqual(TEXT("...nor leave"), MiddleHeard.Deselect, 0);

	Middle->SetIsEnabled(true);
	Rig.PumpFrames(1);
	if (!FocusOn(*this, Rig, First, TEXT("Player 0's focus can be put back on the first button")))
	{
		return false;
	}
	TestTrue(TEXT("The D-pad right completes"), Rig.Driver()->Sequence().Key(EKeys::Gamepad_DPad_Right).WaitFrames(1).Perform());
	TestTrue(TEXT("Enabled, the D-pad stopped on the middle button"), IsPartOf(FocusOf(Rig), Middle));
	TestEqual(TEXT("...and its trigger heard the focus arrive once"), MiddleHeard.Select, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCoverageEventTriggerScaledPadTest,
	"DreamGUI.EventTrigger.OnACanvasScaledToHalfAndToDoubleTheDPadSelectsTheButtonDrawnThatWayAndOnlyItsTriggerHearsTheFocusArrive",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamCoverageEventTriggerScaledPadTest, "DreamGUI.EventTrigger.OnACanvasScaledToHalfAndToDoubleTheDPadSelectsTheButtonDrawnThatWayAndOnlyItsTriggerHearsTheFocusArrive", "[Nav][Scaled]")

/*
 * At half and at double: a button with a trigger on its face, one below it near, and one further below. The D-pad down from
 * the upper one stops on the near one as drawn: its trigger hears the focus arrive, the upper one's hears it leave, and the
 * far one's hears nothing.
 */
bool FDreamCoverageEventTriggerScaledPadTest::RunTest(const FString& Parameters)
{
	using namespace DreamEventBlockerTriggerCoverageTestLocal;
	for (const double Scale : Scales)
	{
		FHeard UpperHeard;
		FHeard NearHeard;
		FHeard FarHeard;
		FDreamDriverRig Rig = FDreamDriverRig::Headless(ScaledOptions(Scale));
		if (!ComeUp(*this, Rig, Scale))
		{
			return false;
		}
		UDreamButton* Upper = PlaceButton(*this, Rig, TEXT("Upper"), FVector2D(0.0, 110.0));
		UDreamButton* Near = Upper != nullptr ? PlaceButton(*this, Rig, TEXT("Near"), FVector2D(0.0, 20.0)) : nullptr;
		UDreamButton* Far = Near != nullptr ? PlaceButton(*this, Rig, TEXT("Far"), FVector2D(0.0, -140.0)) : nullptr;
		if (Far == nullptr
			|| !TestTrue(At(Scale, TEXT("Each button's face carries a trigger")),
				Listen(Upper->FaceNode, UpperHeard) != nullptr && Listen(Near->FaceNode, NearHeard) != nullptr && Listen(Far->FaceNode, FarHeard) != nullptr))
		{
			return false;
		}
		Rig.PumpFrames(1);
		if (!FocusOn(*this, Rig, Upper, At(Scale, TEXT("Player 0's focus can be put on the upper button"))))
		{
			return false;
		}
		UpperHeard = FHeard();

		TestTrue(At(Scale, TEXT("The D-pad down completes")), Rig.Driver()->Sequence().Key(EKeys::Gamepad_DPad_Down).WaitFrames(1).Perform());
		TestTrue(At(Scale, TEXT("The D-pad down stopped on the near button")), IsPartOf(FocusOf(Rig), Near));
		TestEqual(*At(Scale, TEXT("Its trigger heard the focus arrive once")), NearHeard.Select, 1);
		TestEqual(*At(Scale, TEXT("The upper button's trigger heard it leave once")), UpperHeard.Deselect, 1);
		TestEqual(*At(Scale, TEXT("The far button's trigger heard nothing")), FarHeard.Select + FarHeard.Deselect, 0);
	}
	return true;
}

#endif
