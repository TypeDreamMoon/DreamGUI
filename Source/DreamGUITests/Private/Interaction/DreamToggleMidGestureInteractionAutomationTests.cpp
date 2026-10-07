// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamToggle.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamEventSystem.h"
#include "Interaction/UISelectable.h"
#include "Interaction/UIToggle.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Interaction/DreamPressInteractionTestTypes.h"

/*
 * UDreamToggle caught halfway through a gesture -- clicked again while its tick is still fading in, disabled while held --
 * and confirmed with the pad.
 *
 * UMG's check box is SCheckBox (Slate/Private/Widgets/Input/SCheckBox.cpp): a click flips it and announces the new state
 * (ToggleCheckedState, :373), and its tick image is the image of the state it is in, with no fade. DreamGUI fades the tick
 * between its checked and unchecked colours; the state a fade ends on is still the state the box is in, so a box clicked
 * twice ends unchecked and its tick turns back without ever finishing the check.
 */
namespace DreamToggleMidGestureTestLocal
{
	const FIntPoint ViewportSize(1280, 720);

	/** The largest difference over the four channels: zero for the same colour. */
	int32 ColourDistance(const FColor& InA, const FColor& InB)
	{
		const int32 R = FMath::Abs(static_cast<int32>(InA.R) - static_cast<int32>(InB.R));
		const int32 G = FMath::Abs(static_cast<int32>(InA.G) - static_cast<int32>(InB.G));
		const int32 B = FMath::Abs(static_cast<int32>(InA.B) - static_cast<int32>(InB.B));
		const int32 A = FMath::Abs(static_cast<int32>(InA.A) - static_cast<int32>(InB.A));
		return FMath::Max(FMath::Max(R, G), FMath::Max(B, A));
	}

	struct FPlacedToggle
	{
		UDreamToggle* Toggle = nullptr;
		TSharedPtr<FDreamDriverElement> Element;

		bool IsReady() const { return Toggle != nullptr && Toggle->ToggleBehaviour != nullptr && Element.IsValid() && Element->Exists(); }
	};

	/** An unchecked toggle, its check announcements going to InListener, laid out. */
	FPlacedToggle PlaceToggle(FAutomationTestBase& InTest, FDreamDriverRig& InRig, UDreamPressInteractionListener* InListener)
	{
		FPlacedToggle Placed;
		InRig.BindTest(&InTest);
		if (!InTest.TestTrue(TEXT("The headless rig came up"), InRig.IsUsable()))
		{
			return Placed;
		}
		UDreamToggle* Toggle = InRig.MakeControl<UDreamToggle>(TEXT("Mute"), nullptr, FVector2D(40.0, 40.0));
		if (!InTest.TestNotNull(TEXT("A toggle can be made on the rig"), Toggle))
		{
			return Placed;
		}
		Toggle->SetCheckedState(EDreamCheckState::Unchecked);
		if (InListener != nullptr)
		{
			Toggle->OnCheckStateChanged.AddDynamic(InListener, &UDreamPressInteractionListener::HandleCheckStateChanged);
		}
		// Every click below is a click of its own, never the second half of a double click.
		InRig.EventSystem()->SetDoubleClickTime(0.0f);
		InRig.PumpFrames(1);

		Placed.Toggle = Toggle;
		Placed.Element = InRig.Driver()->Find(FDreamBy::Widget(Toggle));
		InTest.TestTrue(TEXT("The toggle has its behaviour, and the driver can find it"), Placed.IsReady());
		return Placed;
	}

	/** Frames until InTick wears InColour, pumping one at a time for at most InMaxFrames; INDEX_NONE when it never did. */
	int32 FramesUntilColour(FDreamDriverRig& InRig, const UDreamVisual* InTick, const FColor& InColour, int32 InMaxFrames)
	{
		for (int32 Frame = 0; Frame <= InMaxFrames; ++Frame)
		{
			if (InTick->GetColor() == InColour)
			{
				return Frame;
			}
			InRig.PumpFrames(1);
		}
		return INDEX_NONE;
	}

	/** Whether InWidget is InControl or one of its parts -- a toggle's selectable lives on its box. */
	bool IsPartOf(const UDreamWidget* InWidget, const UDreamWidget* InControl)
	{
		return InWidget != nullptr && InControl != nullptr && (InWidget == InControl || InWidget->IsChildOf(InControl));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamToggleClickedDuringTickFadeTest,
	"DreamGUI.Toggle.ClickingAgainWhileTheTickFadesInTurnsTheTickBackAndEndsUnchecked",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamToggleClickedDuringTickFadeTest, "DreamGUI.Toggle.ClickingAgainWhileTheTickFadesInTurnsTheTickBackAndEndsUnchecked", "[Pointer][Animated]")

/*
 * Two clicks, the second while the tick is still on its way in. SCheckBox flips on each and says so (ToggleCheckedState,
 * SCheckBox.cpp:373-399): checked, then unchecked. The tick wears the colour of the state the box is in at the end, and on
 * the way it turns back from wherever the first fade had got -- it never reaches the checked colour.
 *
 * How long the tick's fade is belongs to the behaviour and is not readable from outside it, so it is measured: one whole
 * check, watched frame by frame, then one whole uncheck. The click under test waits by that measure.
 */
bool FDreamToggleClickedDuringTickFadeTest::RunTest(const FString& Parameters)
{
	using namespace DreamToggleMidGestureTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FPlacedToggle Placed = PlaceToggle(*this, Rig, nullptr);
	if (!Placed.IsReady())
	{
		return false;
	}
	const UUIToggle* Behaviour = Placed.Toggle->ToggleBehaviour;
	const UDreamVisual* Tick = Behaviour->GetToggleTransitionTarget();
	const FColor On = Behaviour->GetOnColor();
	const FColor Off = Behaviour->GetOffColor();
	if (!TestNotNull(TEXT("The tick is what the checked transition tints"), Tick)
		|| !TestTrue(TEXT("The checked and unchecked tick colours differ"), On != Off))
	{
		return false;
	}
	// Generous only as a stop: two seconds of frames, far past any tick fade.
	const int32 Ceiling = FMath::CeilToInt(2.0f / Rig.Context().FrameSeconds);
	if (!TestTrue(TEXT("The unchecked toggle's tick settles on the unchecked colour"), FramesUntilColour(Rig, Tick, Off, Ceiling) != INDEX_NONE))
	{
		return false;
	}

	// The measure: one whole check and one whole uncheck.
	TestTrue(TEXT("A first click completes"), Placed.Element->Click());
	const int32 FadeFrames = FramesUntilColour(Rig, Tick, On, Ceiling);
	if (!TestTrue(FString::Printf(TEXT("The tick fades in over several frames (%d)"), FadeFrames), FadeFrames >= 4))
	{
		return false;
	}
	TestTrue(TEXT("A second click completes"), Placed.Element->Click());
	TestTrue(TEXT("...and the tick fades out as long as it faded in"), FramesUntilColour(Rig, Tick, Off, FadeFrames + 2) != INDEX_NONE);

	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	Placed.Toggle->OnCheckStateChanged.AddDynamic(Listener.Get(), &UDreamPressInteractionListener::HandleCheckStateChanged);
	bool bWoreChecked = false;
	int32 AtSecondPress = 0;
	TestTrue(TEXT("Clicking, then pressing again while the tick fades in, completes"),
		Rig.Driver()->Sequence()
			.Press()
			.Release()
			.Then([Tick, On, &bWoreChecked](FDreamDriverContext&) { bWoreChecked |= Tick->GetColor() == On; })
			.Press()
			.Then([Tick, On, Off, &bWoreChecked, &AtSecondPress](FDreamDriverContext&)
			{
				bWoreChecked |= Tick->GetColor() == On;
				AtSecondPress = ColourDistance(Tick->GetColor(), Off);
			})
			.Perform());
	if (!TestTrue(TEXT("The second press caught the tick on its way in"), AtSecondPress > 0 && !bWoreChecked))
	{
		return false;
	}
	TestTrue(TEXT("Letting go completes"), Rig.Driver()->Sequence().Release().Perform());

	TestFalse(TEXT("Two clicks leave the toggle unchecked"), Placed.Toggle->IsChecked());
	if (TestEqual(TEXT("Each click announced its state"), Listener->CheckStates.Num(), 2))
	{
		TestEqual(TEXT("Checked first"), Listener->CheckStates[0], EDreamCheckState::Checked);
		TestEqual(TEXT("Then unchecked"), Listener->CheckStates[1], EDreamCheckState::Unchecked);
	}
	int32 Previous = ColourDistance(Tick->GetColor(), Off);
	bool bMovedAwayAgain = false;
	for (int32 Frame = 0; Frame < FadeFrames + 2 && Tick->GetColor() != Off; ++Frame)
	{
		Rig.PumpFrames(1);
		const int32 Now = ColourDistance(Tick->GetColor(), Off);
		bMovedAwayAgain |= Now > Previous;
		bWoreChecked |= Tick->GetColor() == On;
		Previous = Now;
	}
	TestFalse(TEXT("The tick turned back without ever wearing the checked colour"), bWoreChecked);
	TestFalse(TEXT("...and never moved away from the unchecked colour again on the way"), bMovedAwayAgain);
	TestEqual(TEXT("Within the fade's length the tick wears the unchecked colour"), Tick->GetColor(), Off);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamToggleDisabledWhileHeldTest,
	"DreamGUI.Toggle.AToggleDisabledWhileHeldDoesNotFlipWhenLetGo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamToggleDisabledWhileHeldTest, "DreamGUI.Toggle.AToggleDisabledWhileHeldDoesNotFlipWhenLetGo", "[Pointer][Disabled]")

/*
 * Pressed, greyed out while held, let go over it. A toggle in UMG's game UI is CommonUI's toggleable button, whose
 * SCommonButton::OnMouseButtonUp releases a press that lost its interaction "without acknowledging the click"
 * (CommonUI/Private/CommonButtonTypes.cpp:50-62); a disabled control is one the player cannot use. So the box stays
 * unchecked and says nothing, and once enabled again a click checks it.
 */
bool FDreamToggleDisabledWhileHeldTest::RunTest(const FString& Parameters)
{
	using namespace DreamToggleMidGestureTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FPlacedToggle Placed = PlaceToggle(*this, Rig, Listener.Get());
	if (!Placed.IsReady())
	{
		return false;
	}

	TestTrue(TEXT("Pressing on the toggle completes"), Placed.Element->Press());
	TestTrue(TEXT("The toggle is held"), Placed.Toggle->IsPressed());
	Placed.Toggle->SetIsEnabled(false);
	Rig.PumpFrames(1);
	TestTrue(TEXT("Letting go over the disabled toggle completes"), Placed.Element->Release());

	TestFalse(TEXT("A toggle disabled while held stays unchecked"), Placed.Toggle->IsChecked());
	TestEqual(TEXT("...and announces nothing"), Listener->CheckStates.Num(), 0);
	TestFalse(TEXT("...and is not left pressed"), Placed.Toggle->IsPressed());

	Placed.Toggle->SetIsEnabled(true);
	Rig.PumpFrames(1);
	TestTrue(TEXT("Clicking the toggle enabled again completes"), Placed.Element->Click());
	TestTrue(TEXT("Enabled again, a click checks it"), Placed.Toggle->IsChecked());
	TestEqual(TEXT("...and says so once"), Listener->CheckStates.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTogglePadConfirmTest,
	"DreamGUI.Toggle.ThePadsConfirmOnTheFocusedToggleFlipsItOnceWhenTheButtonComesUp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamTogglePadConfirmTest, "DreamGUI.Toggle.ThePadsConfirmOnTheFocusedToggleFlipsItOnceWhenTheButtonComesUp", "[Nav][Animated]")

/*
 * The pad's Accept on a focused check box: SCheckBox::OnKeyDown takes it as a press, and under the default press method,
 * DownAndUp, OnKeyUp is what flips the box (SCheckBox.cpp:102-160). So the confirm going down changes nothing, its coming up
 * checks the box, and a second confirm unchecks it -- one flip per press. The focus is the pad's own: the first stick
 * press lands on the only control there is.
 */
bool FDreamTogglePadConfirmTest::RunTest(const FString& Parameters)
{
	using namespace DreamToggleMidGestureTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FPlacedToggle Placed = PlaceToggle(*this, Rig, Listener.Get());
	if (!Placed.IsReady())
	{
		return false;
	}
	TestTrue(TEXT("The toggle flips when a press comes up, as SCheckBox does by default"),
		Placed.Toggle->GetPressMethod() == EDreamUIPressMethod::DownAndUp);
	FDreamDriverRef Driver = Rig.Driver();

	TestTrue(TEXT("A stick press completes"), Driver->Sequence().Navigate(EDreamUINavigationDirection::Right).Perform());
	if (!TestTrue(TEXT("The stick press landed the pad's focus on the toggle"),
		IsPartOf(Rig.EventSystem()->GetHighlightedComponentForNavigation(0), Placed.Toggle)))
	{
		return false;
	}

	TestTrue(TEXT("The confirm going down completes"), Driver->Sequence().NavigationTrigger(true).Perform());
	TestFalse(TEXT("The confirm going down has not flipped the toggle yet"), Placed.Toggle->IsChecked());
	TestTrue(TEXT("The confirm coming up completes"), Driver->Sequence().NavigationTrigger(false).Perform());
	TestTrue(TEXT("The confirm coming up checked it"), Placed.Toggle->IsChecked());
	if (TestEqual(TEXT("...and said so once"), Listener->CheckStates.Num(), 1))
	{
		TestEqual(TEXT("As checked"), Listener->CheckStates[0], EDreamCheckState::Checked);
	}

	TestTrue(TEXT("A second confirm completes"), Driver->Sequence().NavigationTrigger(true).NavigationTrigger(false).Perform());
	TestFalse(TEXT("The second confirm unchecked it"), Placed.Toggle->IsChecked());
	TestEqual(TEXT("...one flip for each confirm"), Listener->CheckStates.Num(), 2);
	return true;
}

#endif
