// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamButton.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamEventSystem.h"
#include "Interaction/UIButton.h"
#include "Interaction/UISelectable.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Driver/DreamDriverUntil.h"
#include "Interaction/DreamPressInteractionTestTypes.h"

/*
 * UDreamButton caught halfway through a gesture: the pointer leaving while the hover fade is still on its way, sliding
 * off while the press fade is, and the button being disabled while it is held down.
 *
 * What a press and a release mean is SButton's (Slate/Private/Widgets/Input/SButton.cpp). Its look is a function of its
 * state and nothing else -- UpdateBorderImage (:221-238) picks Pressed while IsPressed(), then Hovered while IsHovered(),
 * then Normal -- so whatever the button is in the middle of, the look it ends on is the look of the state it is in now,
 * never of a state it has left. DreamGUI fades between those looks over the style's transition time (UUISelectable), and a
 * fade is held to the same rule: once the state changes it turns toward the new look from wherever it had got, and never
 * finishes the one it abandoned.
 *
 * Every colour is read off the face the selectable tints, and every wait is the selectable's own transition time.
 */
namespace DreamButtonMidGestureTestLocal
{
	const FIntPoint ViewportSize(1280, 720);

	/** Far enough sideways to be off a 200-wide button, and far past any drag threshold. */
	const FVector2D OffTheButton(400.0, 0.0);

	/** The largest difference over the four channels: zero for the same colour. */
	int32 ColourDistance(const FColor& InA, const FColor& InB)
	{
		const int32 R = FMath::Abs(static_cast<int32>(InA.R) - static_cast<int32>(InB.R));
		const int32 G = FMath::Abs(static_cast<int32>(InA.G) - static_cast<int32>(InB.G));
		const int32 B = FMath::Abs(static_cast<int32>(InA.B) - static_cast<int32>(InB.B));
		const int32 A = FMath::Abs(static_cast<int32>(InA.A) - static_cast<int32>(InB.A));
		return FMath::Max(FMath::Max(R, G), FMath::Max(B, A));
	}

	struct FPlacedButton
	{
		UDreamButton* Button = nullptr;
		UDreamVisual* Face = nullptr;
		TSharedPtr<FDreamDriverElement> Element;
		FColor Normal;
		FColor Hovered;
		FColor Pressed;
		/** The selectable's transition time, in seconds. */
		float Duration = 0.0f;

		bool IsReady() const { return Button != nullptr && Face != nullptr && Element.IsValid() && Element->Exists(); }
	};

	/** The button with its press pair and its click going to InListener, laid out, and the colours its face fades between. */
	FPlacedButton PlaceButton(FAutomationTestBase& InTest, FDreamDriverRig& InRig, UDreamPressInteractionListener* InListener)
	{
		FPlacedButton Placed;
		InRig.BindTest(&InTest);
		if (!InTest.TestTrue(TEXT("The headless rig came up"), InRig.IsUsable()))
		{
			return Placed;
		}
		UDreamButton* Button = InRig.MakeControl<UDreamButton>(TEXT("Play"), nullptr, FVector2D(200.0, 60.0));
		if (!InTest.TestNotNull(TEXT("A button can be made on the rig"), Button)
			|| !InTest.TestNotNull(TEXT("It carries its UIButton"), Button->ButtonBehaviour.Get()))
		{
			return Placed;
		}
		Button->OnClicked.AddDynamic(InListener, &UDreamPressInteractionListener::HandleClicked);
		Button->OnPressed.AddDynamic(InListener, &UDreamPressInteractionListener::HandlePressed);
		Button->OnReleased.AddDynamic(InListener, &UDreamPressInteractionListener::HandleReleased);
		InRig.PumpFrames(1);

		const UUIButton* Selectable = Button->ButtonBehaviour;
		Placed.Button = Button;
		Placed.Face = Selectable->GetTransitionTarget();
		Placed.Normal = Selectable->GetNormalColor();
		Placed.Hovered = Selectable->GetHoveredColor();
		Placed.Pressed = Selectable->GetPressedColor();
		Placed.Duration = Selectable->GetAnimDuration();
		Placed.Element = InRig.Driver()->Find(FDreamBy::Widget(Button));
		InTest.TestTrue(TEXT("The button has a face its transition tints, and the driver can find it"), Placed.IsReady());
		return Placed;
	}

	/** Frames that cover InSeconds of the pump, with one for the frame the change was seen in and one to spare. */
	int32 FramesFor(const FDreamDriverRig& InRig, float InSeconds)
	{
		return FMath::CeilToInt(InSeconds / InRig.Context().FrameSeconds) + 2;
	}

	/** Pump until the face's colour passes InCondition, for at most the transition time and two frames. */
	bool WaitForFace(FDreamDriverRig& InRig, const FPlacedButton& InPlaced, TFunction<bool(const FColor&)> InCondition, const TCHAR* InWhat)
	{
		const FWaitTimeout Timeout = FWaitTimeout::InSeconds(InPlaced.Duration + 2.0 * InRig.Context().FrameSeconds);
		const UDreamVisual* Face = InPlaced.Face;
		return InRig.Driver()->Wait(
			FDreamUntil::Condition([Face, InCondition]() { return InCondition(Face->GetColor()); }, Timeout),
			Timeout, InWhat);
	}

	/**
	 * The preconditions every fade test reads from, stated rather than assumed: three distinct looks and a transition that
	 * takes several frames. A style that made two of them equal, or the transition instant, would make the test vacuous.
	 */
	bool HasAFadeToCatch(FAutomationTestBase& InTest, const FDreamDriverRig& InRig, const FPlacedButton& InPlaced)
	{
		return InTest.TestTrue(TEXT("The hovered colour differs from the normal one"), InPlaced.Hovered != InPlaced.Normal)
			&& InTest.TestTrue(TEXT("The pressed colour differs from the hovered one"), InPlaced.Pressed != InPlaced.Hovered)
			&& InTest.TestTrue(FString::Printf(TEXT("The transition takes several frames (%.3f seconds)"), InPlaced.Duration),
				InPlaced.Duration > 4.0f * InRig.Context().FrameSeconds);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamButtonHoverFadeInterruptedTest,
	"DreamGUI.Button.LeavingPartwayThroughTheHoverFadeTurnsTheFaceBackFromWhereItHadGot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamButtonHoverFadeInterruptedTest, "DreamGUI.Button.LeavingPartwayThroughTheHoverFadeTurnsTheFaceBackFromWhereItHadGot", "[Pointer][Animated]")

/*
 * The pointer arrives, the face sets off toward the hovered colour, and the pointer leaves before it gets there. SButton's
 * look follows the state it is in (UpdateBorderImage, SButton.cpp:221-238), and it is in Normal again: so the face turns
 * back, from the colour it had reached, toward the normal colour -- it never goes on toward hovered, never jumps, and is
 * back on normal within its transition time.
 */
bool FDreamButtonHoverFadeInterruptedTest::RunTest(const FString& Parameters)
{
	using namespace DreamButtonMidGestureTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FPlacedButton Placed = PlaceButton(*this, Rig, Listener.Get());
	if (!Placed.IsReady() || !HasAFadeToCatch(*this, Rig, Placed))
	{
		return false;
	}
	const FColor Normal = Placed.Normal;
	const FColor Hovered = Placed.Hovered;
	const int32 Span = ColourDistance(Hovered, Normal);
	// The style push is itself a transition, so a new button's face is still on its way to Normal: start from rest.
	TestTrue(TEXT("The new button's face settles on the normal colour"),
		WaitForFace(Rig, Placed, [Normal](const FColor& InColour) { return InColour == Normal; }, TEXT("the face settling on the normal colour")));

	TestTrue(TEXT("Moving onto the button completes"), Placed.Element->Hover());
	TestTrue(TEXT("The face gets a third of the way toward the hovered colour"),
		WaitForFace(Rig, Placed, [Normal, Span](const FColor& InColour) { return ColourDistance(InColour, Normal) * 3 >= Span; },
			TEXT("the face a third of the way to the hovered colour")));
	if (!TestTrue(TEXT("...and is not there yet, which is the moment this test is about"), Placed.Face->GetColor() != Hovered))
	{
		return false;
	}

	// The frame the pointer moves off is one more step of the hover fade before it is one of leaving: the selectable's
	// tween is a DuringPhysics one and the input frame that sees the pointer leave runs after it (TG_PostPhysics, the
	// order FDreamDriverContext::PumpOneFrame keeps), and the leave starts its fade back from the colour the face has at
	// that moment (UUISelectable kills the running tween, and the new one reads where it starts from on its first step).
	// So the colour the face turns back from is the one it shows at the end of that frame.
	TestTrue(TEXT("Moving off the button completes"), Placed.Element->MoveBy(OffTheButton));
	TestFalse(TEXT("Off the button, it is no longer hovered"), Placed.Button->IsHovered());
	const FColor AtLeaving = Placed.Face->GetColor();
	const int32 LeftAt = ColourDistance(AtLeaving, Normal);
	if (!TestTrue(FString::Printf(TEXT("...and the face had still not reached the hovered colour when it left (%d from normal)"), LeftAt),
		AtLeaving != Hovered))
	{
		return false;
	}
	Rig.PumpFrames(1);
	const FColor FirstFrameBack = Placed.Face->GetColor();
	TestTrue(FString::Printf(TEXT("The first frame after the pointer left, the face did not go on toward the hovered colour (%d from normal, %d when it left)"),
		ColourDistance(FirstFrameBack, Normal), LeftAt), ColourDistance(FirstFrameBack, Normal) < LeftAt);
	TestTrue(TEXT("...nor jump to the normal colour: it sets off back from where it had got"), FirstFrameBack != Normal);

	int32 Previous = ColourDistance(FirstFrameBack, Normal);
	bool bMovedAwayAgain = false;
	bool bWoreHovered = false;
	const int32 MaxFrames = FramesFor(Rig, Placed.Duration);
	for (int32 Frame = 0; Frame < MaxFrames && Placed.Face->GetColor() != Normal; ++Frame)
	{
		Rig.PumpFrames(1);
		const int32 Now = ColourDistance(Placed.Face->GetColor(), Normal);
		bMovedAwayAgain |= Now > Previous;
		bWoreHovered |= Placed.Face->GetColor() == Hovered;
		Previous = Now;
	}
	TestFalse(TEXT("On the way back the face never moved away from the normal colour again"), bMovedAwayAgain);
	TestFalse(TEXT("...and never wore the hovered colour it had been heading for"), bWoreHovered);
	TestEqual(TEXT("Within its transition time the face is back on the normal colour"), Placed.Face->GetColor(), Normal);
	TestEqual(TEXT("Hovering and leaving pressed nothing"), Listener->PressedCount, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamButtonPressFadeSlideOffTest,
	"DreamGUI.Button.SlidingOffPartwayThroughThePressFadeKeepsThePressedLookUntilLetGoAndClicksNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamButtonPressFadeSlideOffTest, "DreamGUI.Button.SlidingOffPartwayThroughThePressFadeKeepsThePressedLookUntilLetGoAndClicksNothing", "[Pointer][Animated]")

/*
 * Pressed on, and slid off while the face is still fading to the pressed colour. Under the default DownAndUp method SButton
 * captures the pointer on the press and OnMouseLeave does not release it (SButton.cpp:544-560), so the button is still
 * pressed and UpdateBorderImage still picks the pressed look: the fade goes on to the pressed colour off the button. Let go
 * off it, the button is released without a click (OnMouseButtonUp clicks only while hovered, :414-440) and, neither
 * pressed nor hovered, fades to the normal colour -- not to hovered.
 */
bool FDreamButtonPressFadeSlideOffTest::RunTest(const FString& Parameters)
{
	using namespace DreamButtonMidGestureTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FPlacedButton Placed = PlaceButton(*this, Rig, Listener.Get());
	if (!Placed.IsReady() || !HasAFadeToCatch(*this, Rig, Placed))
	{
		return false;
	}
	TestTrue(TEXT("The button clicks on press-and-release, as UButton does by default"),
		Placed.Button->GetClickMethod() == EDreamUIClickMethod::DownAndUp);
	const FColor Normal = Placed.Normal;
	const FColor Hovered = Placed.Hovered;
	const FColor Pressed = Placed.Pressed;
	const int32 Span = ColourDistance(Pressed, Hovered);

	TestTrue(TEXT("Moving onto the button completes"), Placed.Element->Hover());
	TestTrue(TEXT("The face settles on the hovered colour"),
		WaitForFace(Rig, Placed, [Hovered](const FColor& InColour) { return InColour == Hovered; }, TEXT("the face settling on the hovered colour")));

	TestTrue(TEXT("Pressing completes"), Rig.Driver()->Sequence().Press().Perform());
	TestTrue(TEXT("The face gets a third of the way toward the pressed colour"),
		WaitForFace(Rig, Placed, [Hovered, Span](const FColor& InColour) { return ColourDistance(InColour, Hovered) * 3 >= Span; },
			TEXT("the face a third of the way to the pressed colour")));
	const FColor AtSliding = Placed.Face->GetColor();
	if (!TestTrue(TEXT("...and is not there yet, which is the moment this test is about"), AtSliding != Pressed))
	{
		return false;
	}

	TestTrue(TEXT("Sliding off the button with it held completes"), Placed.Element->MoveBy(OffTheButton));
	TestTrue(TEXT("Held off the button, the button is still pressed"), Placed.Button->IsPressed());
	TestTrue(TEXT("...and the face went on toward the pressed colour rather than turning back"),
		ColourDistance(Placed.Face->GetColor(), Pressed) <= ColourDistance(AtSliding, Pressed));
	TestTrue(TEXT("Held off the button, the face arrives at the pressed colour and stays there"),
		WaitForFace(Rig, Placed, [Pressed](const FColor& InColour) { return InColour == Pressed; }, TEXT("the face reaching the pressed colour off the button")));

	TestTrue(TEXT("Letting go off the button completes"), Placed.Element->Release());
	TestFalse(TEXT("Let go, the button is no longer pressed"), Placed.Button->IsPressed());
	TestTrue(TEXT("...and, under no pointer, its face fades to the normal colour"),
		WaitForFace(Rig, Placed, [Normal](const FColor& InColour) { return InColour == Normal; }, TEXT("the face returning to the normal colour")));
	TestEqual(TEXT("One press"), Listener->PressedCount, 1);
	TestEqual(TEXT("One release"), Listener->ReleasedCount, 1);
	TestEqual(TEXT("A press let go off the button is no click"), Listener->ClickedCount, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamButtonDisabledWhileHeldTest,
	"DreamGUI.Button.AButtonDisabledWhileHeldIsReleasedWithoutAClickAndClicksAgainOnceEnabled",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamButtonDisabledWhileHeldTest, "DreamGUI.Button.AButtonDisabledWhileHeldIsReleasedWithoutAClickAndClicksAgainOnceEnabled", "[Pointer][Disabled]")

/*
 * Pressed, then greyed out by the game while the mouse button is still down, then let go over it. SButton::OnMouseButtonUp
 * releases a press it took whatever the button is now (it asks bIsPressed, not IsEnabled) and clicks only if IsEnabled()
 * (SButton.cpp:408-416); CommonUI's button says the same in as many words -- interaction disabled while it held the
 * capture is released "without acknowledging the click" (CommonUI/Private/CommonButtonTypes.cpp:50-62). So: one press, one
 * release, no click, and nothing left pressed -- enabled again, the next click is an ordinary one.
 */
bool FDreamButtonDisabledWhileHeldTest::RunTest(const FString& Parameters)
{
	using namespace DreamButtonMidGestureTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FPlacedButton Placed = PlaceButton(*this, Rig, Listener.Get());
	if (!Placed.IsReady())
	{
		return false;
	}

	TestTrue(TEXT("Pressing on the button completes"), Placed.Element->Press());
	TestTrue(TEXT("The button is held"), Placed.Button->IsPressed());
	// UMG's SetIsEnabled, which is what a game flips to grey a button out.
	Placed.Button->SetIsEnabled(false);
	Rig.PumpFrames(1);
	TestTrue(TEXT("Letting go over the disabled button completes"), Placed.Element->Release());

	TestEqual(TEXT("The press was a press"), Listener->PressedCount, 1);
	TestEqual(TEXT("...and the release ended it"), Listener->ReleasedCount, 1);
	TestEqual(TEXT("A button disabled while held is not clicked by the release"), Listener->ClickedCount, 0);
	TestFalse(TEXT("...and is not left pressed"), Placed.Button->IsPressed());

	Placed.Button->SetIsEnabled(true);
	Rig.PumpFrames(1);
	TestTrue(TEXT("Clicking the button enabled again completes"), Placed.Element->Click());
	TestEqual(TEXT("Enabled again, a click clicks it once"), Listener->ClickedCount, 1);
	TestEqual(TEXT("...with a press"), Listener->PressedCount, 2);
	TestEqual(TEXT("...and a release of its own"), Listener->ReleasedCount, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamButtonDisabledUnderFingerTest,
	"DreamGUI.Button.AButtonDisabledUnderAFingerIsReleasedWithoutAClickWhenTheFingerLifts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamButtonDisabledUnderFingerTest, "DreamGUI.Button.AButtonDisabledUnderAFingerIsReleasedWithoutAClickWhenTheFingerLifts", "[Touch][Disabled]")

/*
 * The test above with a finger. A touch reaches SButton through OnMouseButtonDown and OnMouseButtonUp (Slate falls a touch
 * nobody handles as a touch back to the mouse handlers, FSlateApplication::RoutePointerUpEvent), under the touch method,
 * DownAndUp by default -- so the rule is the same one: the lift releases the press the finger made and clicks only an
 * enabled button. A tap once it is enabled again clicks it.
 */
bool FDreamButtonDisabledUnderFingerTest::RunTest(const FString& Parameters)
{
	using namespace DreamButtonMidGestureTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FPlacedButton Placed = PlaceButton(*this, Rig, Listener.Get());
	if (!Placed.IsReady())
	{
		return false;
	}
	const TOptional<FVector2D> Centre = Placed.Element->GetCentrePixel();
	if (!TestTrue(TEXT("The button is somewhere a finger can reach"), Centre.IsSet()))
	{
		return false;
	}
	FDreamDriverRef Driver = Rig.Driver();

	TestTrue(TEXT("A finger lands on the button"), Driver->Sequence().TouchDown(0, Centre.GetValue()).Perform());
	TestTrue(TEXT("...and holds it"), Placed.Button->IsPressed());
	Placed.Button->SetIsEnabled(false);
	Rig.PumpFrames(1);
	TestTrue(TEXT("The finger lifts off the disabled button"), Driver->Sequence().TouchUp(0).Perform());

	TestEqual(TEXT("The finger's press was a press"), Listener->PressedCount, 1);
	TestEqual(TEXT("...and the lift ended it"), Listener->ReleasedCount, 1);
	TestEqual(TEXT("A button disabled under the finger is not clicked by the lift"), Listener->ClickedCount, 0);
	TestFalse(TEXT("...and is not left pressed"), Placed.Button->IsPressed());

	Placed.Button->SetIsEnabled(true);
	Rig.PumpFrames(1);
	TestTrue(TEXT("Tapping the button enabled again completes"), Placed.Element->Tap());
	TestEqual(TEXT("Enabled again, a tap clicks it once"), Listener->ClickedCount, 1);
	return true;
}

#endif
