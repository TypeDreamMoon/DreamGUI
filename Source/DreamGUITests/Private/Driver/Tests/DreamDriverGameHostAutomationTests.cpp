// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamButton.h"
#include "Controls/DreamScrollBox.h"
#include "Controls/DreamTextInput.h"
#include "Core/Components/DreamWidget.h"
#include "Engine/World.h"
#include "Event/DreamBaseEventData.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamPointerEventData.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "InputCoreTypes.h"
#include "Interaction/UITextInput.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverInputModule.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Driver/DreamDriverTypes.h"
#include "Interaction/DreamPressInteractionTestTypes.h"
#include "Interaction/DreamTextInteractionTestTypes.h"

/*
 * THE SAME GESTURES, ENTERING WHERE A PLAYER'S DO.
 *
 * Every other driver test puts its input straight into the input module. In a game nothing does: a
 * key or a button reaches the player controller, waits in its UPlayerInput for the controller's
 * input frame, and is dispatched down the input stack to the preset input actor's bindings, which
 * are what call the module -- and characters come through UDreamGameViewportClient::InputChar. The
 * rigs here are built with InputHost set to one of the two preset actors (Driver/DreamDriverGameHost.h
 * builds the local player, the controller and the actor), so every step below travels that road, and
 * every test runs once per preset.
 *
 * What a test here can see that a module test cannot is exactly that stretch of road: that the
 * preset binds the key at all, that the controller's input frame dispatches it in the order and the
 * frame a game would, that a text field's key agent sits above the actor on the stack, that the
 * preset reads modifiers from the player's input, and what happens to all of it when the game is
 * paused. The expected behaviour is UMG's and Slate's (5.8) wherever the test names one; where this
 * library documents its own design the comment says so.
 */
namespace DreamGameHostTestLocal
{
	const FIntPoint ViewportSize(1280, 720);
	const FVector2D ButtonSize(200.0, 60.0);

	struct FHostCase
	{
		EDreamRigInputHost Host;
		const TCHAR* Name;
	};

	/** Both presets. Every test below runs its body once through each. */
	const FHostCase ActorHosts[] = {
		{ EDreamRigInputHost::StandaloneActor, TEXT("standalone input actor") },
		{ EDreamRigInputHost::EnhancedActor, TEXT("Enhanced Input actor") },
	};

	FDreamRigOptions OptionsFor(EDreamRigInputHost InHost)
	{
		FDreamRigOptions Options;
		Options.ViewportSize = ViewportSize;
		Options.InputHost = InHost;
		return Options;
	}

	/** "[standalone input actor] what", so a failure says which preset it failed under. */
	FString Under(const FHostCase& InCase, const TCHAR* InWhat)
	{
		return FString::Printf(TEXT("[%s] %s"), InCase.Name, InWhat);
	}

	/** The rig-came-up claim, carrying the rig's own reason when it did not. */
	FString RigCameUp(const FHostCase& InCase, const FDreamDriverRig& InRig)
	{
		const FString& WhyNot = InRig.GetBuildFailure();
		return WhyNot.IsEmpty()
			? Under(InCase, TEXT("The rig came up"))
			: FString::Printf(TEXT("[%s] The rig came up -- it did not: %s"), InCase.Name, *WhyNot);
	}

	UDreamButton* MakeListenedButton(FDreamDriverRig& InRig, const TCHAR* InName, const FVector2D& InPosition,
		UDreamPressInteractionListener* InListener)
	{
		UDreamButton* Button = InRig.MakeControl<UDreamButton>(InName, nullptr, ButtonSize, InPosition);
		if (Button != nullptr && InListener != nullptr)
		{
			Button->OnClicked.AddDynamic(InListener, &UDreamPressInteractionListener::HandleClicked);
			Button->OnPressed.AddDynamic(InListener, &UDreamPressInteractionListener::HandlePressed);
			Button->OnReleased.AddDynamic(InListener, &UDreamPressInteractionListener::HandleReleased);
		}
		return Button;
	}

	/** Whether InWidget is InControl or one of its parts -- a button's selectable lives on its face. */
	bool IsPartOf(const UDreamWidget* InWidget, const UDreamWidget* InControl)
	{
		return InWidget != nullptr && InControl != nullptr && (InWidget == InControl || InWidget->IsChildOf(InControl));
	}

	UDreamWidget* Highlighted(const FDreamDriverRig& InRig)
	{
		const UDreamEventSystem* EventSystem = InRig.EventSystem();
		return EventSystem != nullptr ? EventSystem->GetHighlightedComponentForNavigation(0) : nullptr;
	}

	UDreamWidget* Selected(const FDreamDriverRig& InRig)
	{
		const UDreamEventSystem* EventSystem = InRig.EventSystem();
		return EventSystem != nullptr ? EventSystem->GetCurrentSelectedComponent(0) : nullptr;
	}

	/**
	 * Whether InKey reached the rig's player controller. UPlayerInput keeps a key state for every key
	 * it has been sent, so this is the proof that a step went the long way round rather than into the
	 * module.
	 */
	bool ControllerSawKey(const FDreamDriverRig& InRig, const FKey& InKey)
	{
		const APlayerController* Controller = InRig.GetPlayerController();
		return Controller != nullptr && Controller->PlayerInput != nullptr && Controller->PlayerInput->GetKeyState(InKey) != nullptr;
	}

	/** Twenty rows of 100 in a 300-by-400 window, measured and laid out; see the scroll box's own tests. */
	UDreamScrollBox* MakeFilledBox(FDreamDriverRig& InRig)
	{
		UDreamScrollBox* Box = InRig.MakeControl<UDreamScrollBox>(TEXT("Box"), nullptr, FVector2D(300.0, 400.0));
		if (Box == nullptr || Box->GetContentNode() == nullptr)
		{
			return Box;
		}
		for (int32 RowIndex = 0; RowIndex < 20; ++RowIndex)
		{
			InRig.MakeWidget(FString::Printf(TEXT("Box_Row%02d"), RowIndex), Box->GetContentNode(), FVector2D(300.0, 100.0));
		}
		Box->RefreshContentExtent();
		InRig.PumpFrames(2);
		return Box;
	}

	UDreamTextInput* MakeObservedField(FDreamDriverRig& InRig, UDreamTextInteractionListener* InListener)
	{
		UDreamTextInput* Field = InRig.MakeControl<UDreamTextInput>(TEXT("Username"), nullptr, FVector2D(320.0, 40.0));
		if (Field != nullptr && InListener != nullptr)
		{
			Field->OnTextChanged.AddDynamic(InListener, &UDreamTextInteractionListener::HandleTextChanged);
			Field->OnTextCommitted.AddDynamic(InListener, &UDreamTextInteractionListener::HandleTextCommitted);
		}
		return Field;
	}

	bool IsEditing(const UDreamTextInput* InField)
	{
		return InField != nullptr && InField->InputBehaviour != nullptr && InField->InputBehaviour->IsInputActive();
	}

	/**
	 * Two buttons side by side and the navigation state between them, for the navigation tests: which
	 * one the first press landed on, and the other.
	 */
	struct FTwoButtons
	{
		UDreamButton* West = nullptr;
		UDreamButton* East = nullptr;
		TStrongObjectPtr<UDreamPressInteractionListener> WestListener;
		TStrongObjectPtr<UDreamPressInteractionListener> EastListener;

		bool IsReady() const { return West != nullptr && East != nullptr; }

		UDreamPressInteractionListener* ListenerOf(const UDreamButton* InButton) const
		{
			return InButton == West ? WestListener.Get() : EastListener.Get();
		}
	};

	FTwoButtons MakeTwoButtons(FDreamDriverRig& InRig)
	{
		FTwoButtons Buttons;
		Buttons.WestListener.Reset(NewObject<UDreamPressInteractionListener>());
		Buttons.EastListener.Reset(NewObject<UDreamPressInteractionListener>());
		// 400 canvas units apart on one row: far enough that each is the other's only neighbour to the
		// side, and nothing above or below either.
		Buttons.West = MakeListenedButton(InRig, TEXT("West"), FVector2D(-200.0, 0.0), Buttons.WestListener.Get());
		Buttons.East = MakeListenedButton(InRig, TEXT("East"), FVector2D(200.0, 0.0), Buttons.EastListener.Get());
		InRig.PumpFrames(1);
		return Buttons;
	}

	/**
	 * Which of the two buttons the highlight is on, or null. The first navigation press of all lands on
	 * the default selectable rather than moving (UDreamPointerInputModule::Navigate falls back to
	 * UUISelectable::FindDefaultSelectable with nothing highlighted), and which one that is is the
	 * registration order's business, not this test's -- so the tests start from wherever it landed.
	 */
	UDreamButton* HighlightedButton(const FDreamDriverRig& InRig, const FTwoButtons& InButtons)
	{
		UDreamWidget* Current = Highlighted(InRig);
		if (IsPartOf(Current, InButtons.West))
		{
			return InButtons.West;
		}
		if (IsPartOf(Current, InButtons.East))
		{
			return InButtons.East;
		}
		return nullptr;
	}
}

/**
 * SButton's order (OnMouseButtonDown presses, OnMouseButtonUp releases and then clicks), now with the
 * button key entering through the player controller: the preset binds LeftMouseButton pressed and
 * released (the Enhanced preset maps it to its trigger action, bound Started and Completed) and calls
 * InputTrigger at the position the module reports. Without the binding, or without the controller's
 * input frame, nothing reaches the module and every count is zero.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGameHostClickTest,
	"DreamGUI.Driver.GameHost.AClickThroughThePlayerControllerPressesReleasesAndClicksTheButtonOnceEachInOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamGameHostClickTest::RunTest(const FString& Parameters)
{
	using namespace DreamGameHostTestLocal;
	for (const FHostCase& Case : ActorHosts)
	{
		TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
		FDreamDriverRig Rig = FDreamDriverRig::Headless(OptionsFor(Case.Host));
		Rig.BindTest(this);
		if (!TestTrue(RigCameUp(Case, Rig), Rig.IsUsable()))
		{
			continue;
		}
		UDreamButton* Button = MakeListenedButton(Rig, TEXT("Play"), FVector2D::ZeroVector, Listener.Get());
		if (!TestNotNull(*Under(Case, TEXT("A button can be made on the rig")), Button))
		{
			continue;
		}
		Rig.PumpFrames(1);

		TestTrue(Under(Case, TEXT("Clicking the button's centre completes")), Rig.Driver()->Find(FDreamBy::Widget(Button))->Click());

		TestEqual(Under(Case, TEXT("One press")), Listener->PressedCount, 1);
		TestEqual(Under(Case, TEXT("One release")), Listener->ReleasedCount, 1);
		TestEqual(Under(Case, TEXT("One click")), Listener->ClickedCount, 1);
		const TArray<FName> Expected = { FName(TEXT("Pressed")), FName(TEXT("Released")), FName(TEXT("Clicked")) };
		TestTrue(Under(Case, TEXT("The press comes first, then the release, then the click -- SButton's order")),
			Listener->LogOnly(Expected) == Expected);
		TestTrue(Under(Case, TEXT("The left mouse button went through the player controller")),
			ControllerSawKey(Rig, EKeys::LeftMouseButton));
	}
	return true;
}

/**
 * SScrollBox::OnMouseWheel scrolls one notch per wheel delta. The wheel arrives as the key events
 * FSceneViewport sends for it; the preset listens to MouseWheelAxis (the Enhanced one through its
 * Axis1D action) and forwards the value to InputScroll as (v, v) -- the shape the scroll box's own
 * wheel tests use, so the distances match theirs exactly.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGameHostWheelTest,
	"DreamGUI.Driver.GameHost.AWheelNotchThroughThePlayerControllerScrollsTheBoxUnderThePointer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamGameHostWheelTest::RunTest(const FString& Parameters)
{
	using namespace DreamGameHostTestLocal;
	for (const FHostCase& Case : ActorHosts)
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(OptionsFor(Case.Host));
		Rig.BindTest(this);
		if (!TestTrue(RigCameUp(Case, Rig), Rig.IsUsable()))
		{
			continue;
		}
		UDreamScrollBox* Box = MakeFilledBox(Rig);
		if (!TestTrue(Under(Case, TEXT("The box came up with a viewport and a content node")),
			Box != nullptr && Box->ViewportNode != nullptr && Box->GetContentNode() != nullptr))
		{
			continue;
		}
		const float Notch = Box->GetScrollSensitivity() * Box->GetWheelScrollMultiplier();
		if (!TestTrue(Under(Case, TEXT("There is more than three notches to scroll")),
			Notch > 0.0f && Box->GetScrollOffsetOfEnd() > 3.0f * Notch))
		{
			continue;
		}

		FDreamElementRef Viewport = Rig.Driver()->Find(FDreamBy::Widget(Box->ViewportNode.Get()));
		for (int32 NotchIndex = 0; NotchIndex < 3; ++NotchIndex)
		{
			// Toward the user, the way the scroll box's tests turn it.
			TestTrue(Under(Case, TEXT("A notch toward the user completes")), Viewport->ScrollBy(FVector2D(-1.0, -1.0)));
		}
		TestNearlyEqual(Under(Case, TEXT("Three notches through the controller scrolled three notches' distance")),
			Box->GetScrollOffset(), 3.0f * Notch, 0.5f);
		TestTrue(Under(Case, TEXT("The wheel went through the player controller")), ControllerSawKey(Rig, EKeys::MouseWheelAxis));
	}
	return true;
}

/**
 * The pad's road: a stick direction moves the highlight to the neighbour on that side and selects it,
 * and the bottom face button presses and clicks whatever is highlighted -- SButton's Accept, which
 * Slate maps from Virtual_Gamepad_Accept (NavigationConfig.cpp). The preset binds the left stick's
 * direction keys and Gamepad_FaceButton_Bottom.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGameHostStickNavigationTest,
	"DreamGUI.Driver.GameHost.TheStickMovesTheSelectionBetweenTwoButtonsAndTheAcceptButtonClicksTheSelectedOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamGameHostStickNavigationTest::RunTest(const FString& Parameters)
{
	using namespace DreamGameHostTestLocal;
	for (const FHostCase& Case : ActorHosts)
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(OptionsFor(Case.Host));
		Rig.BindTest(this);
		if (!TestTrue(RigCameUp(Case, Rig), Rig.IsUsable()))
		{
			continue;
		}
		const FTwoButtons Buttons = MakeTwoButtons(Rig);
		if (!TestTrue(Under(Case, TEXT("Both buttons can be made on the rig")), Buttons.IsReady()))
		{
			continue;
		}

		TestTrue(Under(Case, TEXT("The first stick press completes")),
			Rig.Driver()->Sequence().Navigate(EDreamUINavigationDirection::Right).Perform());
		UDreamButton* First = HighlightedButton(Rig, Buttons);
		if (!TestNotNull(*Under(Case, TEXT("The first stick press highlighted one of the two buttons")), First))
		{
			continue;
		}
		UDreamButton* Target = First == Buttons.West ? Buttons.East : Buttons.West;
		const EDreamUINavigationDirection TowardTarget = First == Buttons.West
			? EDreamUINavigationDirection::Right
			: EDreamUINavigationDirection::Left;

		TestTrue(Under(Case, TEXT("The stick toward the other button completes")),
			Rig.Driver()->Sequence().Navigate(TowardTarget).Perform());
		TestTrue(Under(Case, TEXT("The stick moved the highlight to the other button")), HighlightedButton(Rig, Buttons) == Target);
		TestTrue(Under(Case, TEXT("and the selection with it")), IsPartOf(Selected(Rig), Target));

		TestTrue(Under(Case, TEXT("Pressing and releasing the accept button completes")),
			Rig.Driver()->Sequence().NavigationTrigger(true).NavigationTrigger(false).Perform());
		TestEqual(Under(Case, TEXT("The accept button pressed the highlighted button once")), Buttons.ListenerOf(Target)->PressedCount, 1);
		TestEqual(Under(Case, TEXT("released it once")), Buttons.ListenerOf(Target)->ReleasedCount, 1);
		TestEqual(Under(Case, TEXT("and clicked it once")), Buttons.ListenerOf(Target)->ClickedCount, 1);
		TestEqual(Under(Case, TEXT("The button the highlight left was not clicked")), Buttons.ListenerOf(First)->ClickedCount, 0);
		TestTrue(Under(Case, TEXT("The stick and the accept button went through the player controller")),
			ControllerSawKey(Rig, EKeys::Gamepad_LeftStick_Right) && ControllerSawKey(Rig, EKeys::Gamepad_FaceButton_Bottom));
	}
	return true;
}

/**
 * The keyboard's road: the arrow keys move the highlight (Slate's FNavigationConfig KeyEventRules) and
 * Enter is Accept (its KeyActionRules). Each is one keystroke through the controller -- pressed in one
 * input frame, released in the next, as a hand does it -- so the Enter's click, which comes on the
 * release, needs the frame after the keystroke.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGameHostArrowNavigationTest,
	"DreamGUI.Driver.GameHost.TheArrowKeysMoveTheSelectionBetweenTwoButtonsAndEnterClicksTheSelectedOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamGameHostArrowNavigationTest::RunTest(const FString& Parameters)
{
	using namespace DreamGameHostTestLocal;
	for (const FHostCase& Case : ActorHosts)
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(OptionsFor(Case.Host));
		Rig.BindTest(this);
		if (!TestTrue(RigCameUp(Case, Rig), Rig.IsUsable()))
		{
			continue;
		}
		const FTwoButtons Buttons = MakeTwoButtons(Rig);
		if (!TestTrue(Under(Case, TEXT("Both buttons can be made on the rig")), Buttons.IsReady()))
		{
			continue;
		}

		TestTrue(Under(Case, TEXT("The first arrow keystroke completes")), Rig.Driver()->Sequence().Type(EKeys::Right).Perform());
		UDreamButton* First = HighlightedButton(Rig, Buttons);
		if (!TestNotNull(*Under(Case, TEXT("The first arrow keystroke highlighted one of the two buttons")), First))
		{
			continue;
		}
		UDreamButton* Target = First == Buttons.West ? Buttons.East : Buttons.West;
		const FKey TowardTarget = First == Buttons.West ? EKeys::Right : EKeys::Left;

		TestTrue(Under(Case, TEXT("The arrow toward the other button completes")), Rig.Driver()->Sequence().Type(TowardTarget).Perform());
		TestTrue(Under(Case, TEXT("The arrow moved the highlight to the other button")), HighlightedButton(Rig, Buttons) == Target);

		TestTrue(Under(Case, TEXT("Enter, and the frame its release lands in, complete")),
			Rig.Driver()->Sequence().Type(EKeys::Enter).WaitFrames(1).Perform());
		TestEqual(Under(Case, TEXT("Enter pressed the highlighted button once")), Buttons.ListenerOf(Target)->PressedCount, 1);
		TestEqual(Under(Case, TEXT("and clicked it once")), Buttons.ListenerOf(Target)->ClickedCount, 1);
		TestEqual(Under(Case, TEXT("The button the highlight left was not clicked")), Buttons.ListenerOf(First)->ClickedCount, 0);
	}
	return true;
}

/**
 * Slate navigates with the D-pad exactly as with the arrow keys (FNavigationConfig maps
 * Gamepad_DPad_Left/Right/Up/Down beside Left/Right/Up/Down), and this library says the same of
 * itself: "one navigation road for the keyboard and the gamepad, so a D-pad gets the same answer"
 * (Docs/Reference/DreamTreeView.md). The preset's direction table binds the arrows, Tab, the left
 * stick and the D-pad; it used to have no D-pad key.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGameHostDPadNavigationTest,
	"DreamGUI.Driver.GameHost.TheDPadMovesTheSelectionBetweenTwoButtonsAsItDoesInSlate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamGameHostDPadNavigationTest::RunTest(const FString& Parameters)
{
	using namespace DreamGameHostTestLocal;
	for (const FHostCase& Case : ActorHosts)
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(OptionsFor(Case.Host));
		Rig.BindTest(this);
		if (!TestTrue(RigCameUp(Case, Rig), Rig.IsUsable()))
		{
			continue;
		}
		const FTwoButtons Buttons = MakeTwoButtons(Rig);
		if (!TestTrue(Under(Case, TEXT("Both buttons can be made on the rig")), Buttons.IsReady()))
		{
			continue;
		}

		TestTrue(Under(Case, TEXT("The first D-pad press completes")), Rig.Driver()->Sequence().Type(EKeys::Gamepad_DPad_Right).Perform());
		UDreamButton* First = HighlightedButton(Rig, Buttons);
		if (!TestNotNull(*Under(Case, TEXT("The first D-pad press highlighted one of the two buttons, as an arrow key does")), First))
		{
			continue;
		}
		UDreamButton* Target = First == Buttons.West ? Buttons.East : Buttons.West;
		const FKey TowardTarget = First == Buttons.West ? EKeys::Gamepad_DPad_Right : EKeys::Gamepad_DPad_Left;
		TestTrue(Under(Case, TEXT("The D-pad toward the other button completes")), Rig.Driver()->Sequence().Type(TowardTarget).Perform());
		TestTrue(Under(Case, TEXT("The D-pad moved the highlight to the other button")), HighlightedButton(Rig, Buttons) == Target);
	}
	return true;
}

/**
 * Slate's Accept is Enter, SpaceBar or the pad's accept button (FNavigationConfig KeyActionRules), and
 * SButton::OnKeyDown presses on Accept -- so a focused UMG button clicks on the space bar. The preset's
 * confirm table has Enter, SpaceBar and Gamepad_FaceButton_Bottom; it used to lack the space bar. The
 * highlight is put on a button with the stick first, the road the test above shows works, so this
 * asks about the space bar alone.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGameHostSpaceBarAcceptTest,
	"DreamGUI.Driver.GameHost.TheSpaceBarClicksTheSelectedButtonAsSlatesAcceptKeyDoes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamGameHostSpaceBarAcceptTest::RunTest(const FString& Parameters)
{
	using namespace DreamGameHostTestLocal;
	for (const FHostCase& Case : ActorHosts)
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(OptionsFor(Case.Host));
		Rig.BindTest(this);
		if (!TestTrue(RigCameUp(Case, Rig), Rig.IsUsable()))
		{
			continue;
		}
		const FTwoButtons Buttons = MakeTwoButtons(Rig);
		if (!TestTrue(Under(Case, TEXT("Both buttons can be made on the rig")), Buttons.IsReady()))
		{
			continue;
		}
		TestTrue(Under(Case, TEXT("A stick press completes")), Rig.Driver()->Sequence().Navigate(EDreamUINavigationDirection::Right).Perform());
		UDreamButton* Current = HighlightedButton(Rig, Buttons);
		if (!TestNotNull(*Under(Case, TEXT("The stick highlighted one of the two buttons")), Current))
		{
			continue;
		}

		TestTrue(Under(Case, TEXT("The space bar, and the frame its release lands in, complete")),
			Rig.Driver()->Sequence().Type(EKeys::SpaceBar).WaitFrames(1).Perform());
		TestEqual(Under(Case, TEXT("The space bar clicked the highlighted button once")), Buttons.ListenerOf(Current)->ClickedCount, 1);
	}
	return true;
}

/**
 * The preset reads Tab as Next and, with shift held, as Prev -- from the live shift state on the
 * player's UPlayerInput at the moment Tab's binding runs (ADreamStandaloneInputEventSystemActor::
 * ResolveNavigationDirection), which is a question only a real controller can answer. Next is the
 * neighbour to the right (or below), Prev the one to the left (or above), so the test steps toward
 * the other button and back again, whichever the first press landed on.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGameHostTabNavigationTest,
	"DreamGUI.Driver.GameHost.TabAndShiftTabThroughTheControllerStepForwardAndBackBetweenTwoButtons",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamGameHostTabNavigationTest::RunTest(const FString& Parameters)
{
	using namespace DreamGameHostTestLocal;
	for (const FHostCase& Case : ActorHosts)
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(OptionsFor(Case.Host));
		Rig.BindTest(this);
		if (!TestTrue(RigCameUp(Case, Rig), Rig.IsUsable()))
		{
			continue;
		}
		const FTwoButtons Buttons = MakeTwoButtons(Rig);
		if (!TestTrue(Under(Case, TEXT("Both buttons can be made on the rig")), Buttons.IsReady()))
		{
			continue;
		}

		TestTrue(Under(Case, TEXT("The first Tab completes")), Rig.Driver()->Sequence().Navigate(EDreamUINavigationDirection::Next).Perform());
		UDreamButton* First = HighlightedButton(Rig, Buttons);
		if (!TestNotNull(*Under(Case, TEXT("The first Tab highlighted one of the two buttons")), First))
		{
			continue;
		}
		UDreamButton* Other = First == Buttons.West ? Buttons.East : Buttons.West;
		// West to East is Next and East to West is Prev; the way there and the way back are the other pair.
		const EDreamUINavigationDirection There = First == Buttons.West ? EDreamUINavigationDirection::Next : EDreamUINavigationDirection::Prev;
		const EDreamUINavigationDirection Back = First == Buttons.West ? EDreamUINavigationDirection::Prev : EDreamUINavigationDirection::Next;

		TestTrue(Under(Case, TEXT("The step to the other button completes")), Rig.Driver()->Sequence().Navigate(There).Perform());
		TestTrue(Under(Case, TEXT("It reached the other button")), HighlightedButton(Rig, Buttons) == Other);
		TestTrue(Under(Case, TEXT("The step back completes")), Rig.Driver()->Sequence().Navigate(Back).Perform());
		TestTrue(Under(Case, TEXT("It came back -- with shift read from the player, Tab and Shift+Tab mean opposite things")),
			HighlightedButton(Rig, Buttons) == First);
		TestTrue(Under(Case, TEXT("Tab and shift went through the player controller")),
			ControllerSawKey(Rig, EKeys::Tab) && ControllerSawKey(Rig, EKeys::LeftShift));
	}
	return true;
}

/**
 * The game's road for a character: UDreamGameViewportClient::InputChar hands it, after the console,
 * to UUITextInput::RouteCharacterInputToActiveInput, which gives it to whichever field owns the
 * keyboard. So what is asserted is that the click through the controller made THIS field the one
 * that owns it, and that every character then landed in it, one change each (SEditableText raises
 * OnTextChanged once per edit).
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGameHostCharacterRoadTest,
	"DreamGUI.Driver.GameHost.CharactersByTheGameRoadLandInTheFieldBeingEdited",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamGameHostCharacterRoadTest::RunTest(const FString& Parameters)
{
	using namespace DreamGameHostTestLocal;
	for (const FHostCase& Case : ActorHosts)
	{
		TStrongObjectPtr<UDreamTextInteractionListener> Listener(NewObject<UDreamTextInteractionListener>());
		FDreamDriverRig Rig = FDreamDriverRig::Headless(OptionsFor(Case.Host));
		Rig.BindTest(this);
		if (!TestTrue(RigCameUp(Case, Rig), Rig.IsUsable()))
		{
			continue;
		}
		UDreamTextInput* Field = MakeObservedField(Rig, Listener.Get());
		if (!TestNotNull(*Under(Case, TEXT("A field can be made on the rig")), Field))
		{
			continue;
		}
		Rig.PumpFrames(1);

		TestTrue(Under(Case, TEXT("Clicking the field completes")), Rig.Driver()->Find(FDreamBy::Name(TEXT("Username")))->Click());
		if (!TestTrue(Under(Case, TEXT("The click through the controller began an edit")), IsEditing(Field)))
		{
			continue;
		}
		TestTrue(Under(Case, TEXT("and made this field the one that owns the keyboard")),
			UUITextInput::GetActiveTextInput() == Field->InputBehaviour.Get());

		TestTrue(Under(Case, TEXT("Typing by the game's road completes")), Rig.Driver()->Sequence().Type(TEXT("hello")).Perform());
		TestEqual(Under(Case, TEXT("The field holds what was typed")), Field->GetText(), FString(TEXT("hello")));
		TestEqual(Under(Case, TEXT("Each character was announced on its own")), Listener->TextChangedCount, 5);
		TestEqual(Under(Case, TEXT("Typing is not committing")), Listener->TextCommittedCount, 0);
		// A real character arrived, which is what switches the key road's guessing off (for good, in a
		// game; the rig puts the switch back when it is torn down).
		TestTrue(Under(Case, TEXT("The field now knows a host delivers characters")), UUITextInput::IsHostDeliveringCharacterEvents());
	}
	return true;
}

/**
 * The road DevTest itself is on: no viewport client delivers characters, so a field turns the KEY
 * into a character with its own table (UUITextInput::ProcessKeyPressed). That table is US QWERTY and
 * nothing else -- on AZERTY, a dead key or AltGr it types the wrong thing, which is what the field's
 * one-time Warning says -- so this pins down only what the table promises: A is 'a', Shift+A is 'A'.
 *
 * The key reaches the field through the field's own key agent, whose input component ActivateInput
 * pushes on player 0's stack above the input actor; shift is read from the player's UPlayerInput,
 * which is why it has to be HELD through the controller's input frame rather than merely sent. Caps
 * lock is the one modifier the field reads from Slate -- the real keyboard of the machine running the
 * test -- so the expected case follows it.
 *
 * The "a host delivers characters" switch is process-wide and flips for good on the first real
 * character; it is turned off here for the test and put back after it.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGameHostKeyFallbackTest,
	"DreamGUI.Driver.GameHost.WithNoHostDeliveringCharactersAKeyThroughTheControllerTypesItsUSLayoutCharacter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamGameHostKeyFallbackTest::RunTest(const FString& Parameters)
{
	using namespace DreamGameHostTestLocal;
	for (const FHostCase& Case : ActorHosts)
	{
		TStrongObjectPtr<UDreamTextInteractionListener> Listener(NewObject<UDreamTextInteractionListener>());
		FDreamDriverRig Rig = FDreamDriverRig::Headless(OptionsFor(Case.Host));
		Rig.BindTest(this);
		if (!TestTrue(RigCameUp(Case, Rig), Rig.IsUsable()))
		{
			continue;
		}
		const bool bWasDelivering = UUITextInput::IsHostDeliveringCharacterEvents();
		UUITextInput::SetHostDeliversCharacterEventsForTesting(false);
		ON_SCOPE_EXIT
		{
			UUITextInput::SetHostDeliversCharacterEventsForTesting(bWasDelivering);
		};

		UDreamTextInput* Field = MakeObservedField(Rig, Listener.Get());
		if (!TestNotNull(*Under(Case, TEXT("A field can be made on the rig")), Field))
		{
			continue;
		}
		Rig.PumpFrames(1);
		TestTrue(Under(Case, TEXT("Clicking the field completes")), Rig.Driver()->Find(FDreamBy::Name(TEXT("Username")))->Click());
		if (!TestTrue(Under(Case, TEXT("The click through the controller began an edit")), IsEditing(Field)))
		{
			continue;
		}

		const bool bCapsLocked = FSlateApplication::IsInitialized() && FSlateApplication::Get().GetModifierKeys().AreCapsLocked();
		const FString Lower = bCapsLocked ? TEXT("A") : TEXT("a");
		const FString Upper = bCapsLocked ? TEXT("a") : TEXT("A");

		TestTrue(Under(Case, TEXT("The A key through the controller completes")), Rig.Driver()->Sequence().Type(EKeys::A).Perform());
		TestEqual(Under(Case, TEXT("The table typed the key's own letter")), Field->GetText(), Lower);

		TestTrue(Under(Case, TEXT("Shift+A through the controller completes")),
			Rig.Driver()->Sequence().TypeChord(EKeys::LeftShift, EKeys::A).Perform());
		TestEqual(Under(Case, TEXT("With shift held through the input frame the table typed the other case")), Field->GetText(), Lower + Upper);
		TestEqual(Under(Case, TEXT("Each key was one edit")), Listener->TextChangedCount, 2);

		TestFalse(Under(Case, TEXT("Keys are not characters: the key road never tells the field a host delivers them")),
			UUITextInput::IsHostDeliveringCharacterEvents());
		TestTrue(Under(Case, TEXT("The keys went through the player controller")),
			ControllerSawKey(Rig, EKeys::A) && ControllerSawKey(Rig, EKeys::LeftShift));
	}
	return true;
}

/**
 * Escape and the pad's right face button are the preset's Back keys, handled in its AnyKey binding
 * when nothing claimed them: a drag in flight is cancelled first, otherwise
 * UDreamUINavigationStack::HandleBack ends the edit of the field that has focus. The field's key agent
 * deliberately binds neither (UUITextInput::BindKeys), so they fall through to the actor. What ending
 * an edit does is this library's documented design (Docs/Reference/DreamTextInput.md: Escape is Back,
 * and an edit that ends without Enter commits once) -- the same three claims the module-level Escape
 * test makes, now with the key entering where a player's does.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGameHostBackEndsEditTest,
	"DreamGUI.Driver.GameHost.EscapeAndThePadsBackButtonThroughTheControllerEndTheEditKeepingTheText",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamGameHostBackEndsEditTest::RunTest(const FString& Parameters)
{
	using namespace DreamGameHostTestLocal;
	const FKey BackKeys[] = { EKeys::Escape, EKeys::Gamepad_FaceButton_Right };
	for (const FHostCase& Case : ActorHosts)
	{
		for (const FKey& BackKey : BackKeys)
		{
			const FString KeyName = BackKey.ToString();
			TStrongObjectPtr<UDreamTextInteractionListener> Listener(NewObject<UDreamTextInteractionListener>());
			FDreamDriverRig Rig = FDreamDriverRig::Headless(OptionsFor(Case.Host));
			Rig.BindTest(this);
			if (!TestTrue(RigCameUp(Case, Rig), Rig.IsUsable()))
			{
				continue;
			}
			UDreamTextInput* Field = MakeObservedField(Rig, Listener.Get());
			if (!TestNotNull(*Under(Case, TEXT("A field can be made on the rig")), Field))
			{
				continue;
			}
			Rig.PumpFrames(1);

			TestTrue(Under(Case, TEXT("Clicking in and typing completes")), Rig.Driver()->Find(FDreamBy::Name(TEXT("Username")))->Type(TEXT("abc")));
			if (!TestTrue(Under(Case, TEXT("The field is being edited")), IsEditing(Field)))
			{
				continue;
			}
			TestTrue(FString::Printf(TEXT("[%s] %s through the controller completes"), Case.Name, *KeyName),
				Rig.Driver()->Sequence().Type(BackKey).Perform());

			TestEqual(FString::Printf(TEXT("[%s] %s keeps what was typed (no revert by default, as in UMG)"), Case.Name, *KeyName),
				Field->GetText(), FString(TEXT("abc")));
			TestFalse(FString::Printf(TEXT("[%s] %s ended the edit"), Case.Name, *KeyName), IsEditing(Field));
			TestEqual(FString::Printf(TEXT("[%s] and the edit that ended committed once"), Case.Name), Listener->TextCommittedCount, 1);
			TestTrue(FString::Printf(TEXT("[%s] %s went through the player controller"), Case.Name, *KeyName), ControllerSawKey(Rig, BackKey));
		}
	}
	return true;
}

/**
 * A finger on a UMG button is a press and its lifting is a release and a click (SButton handles touch
 * as the pointer it is). The finger enters through APlayerController::InputTouch, as FSceneViewport
 * sends it; the preset binds touch pressed, released and moved and hands the finger's index to the
 * module as the pointer id -- so the second finger is a second pointer with its own press, and its
 * tap is a click of its own rather than half of a double click.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGameHostTapTest,
	"DreamGUI.Driver.GameHost.AFingerTappedThroughTheControllerClicksTheButtonAndEachFingerIsItsOwnPointer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamGameHostTapTest::RunTest(const FString& Parameters)
{
	using namespace DreamGameHostTestLocal;
	for (const FHostCase& Case : ActorHosts)
	{
		TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
		FDreamDriverRig Rig = FDreamDriverRig::Headless(OptionsFor(Case.Host));
		Rig.BindTest(this);
		if (!TestTrue(RigCameUp(Case, Rig), Rig.IsUsable()))
		{
			continue;
		}
		UDreamButton* Button = MakeListenedButton(Rig, TEXT("Play"), FVector2D::ZeroVector, Listener.Get());
		if (!TestNotNull(*Under(Case, TEXT("A button can be made on the rig")), Button))
		{
			continue;
		}
		Rig.PumpFrames(1);
		FDreamElementRef ButtonElement = Rig.Driver()->Find(FDreamBy::Widget(Button));

		TestTrue(Under(Case, TEXT("A tap with the first finger completes")), ButtonElement->Tap(0));
		TestEqual(Under(Case, TEXT("The finger landing pressed the button once")), Listener->PressedCount, 1);
		TestEqual(Under(Case, TEXT("Lifting it released the button once")), Listener->ReleasedCount, 1);
		TestEqual(Under(Case, TEXT("and clicked it once")), Listener->ClickedCount, 1);
		TestTrue(Under(Case, TEXT("The first finger went through the player controller")), ControllerSawKey(Rig, EKeys::TouchKeys[0]));

		TestTrue(Under(Case, TEXT("A tap with the second finger completes")), ButtonElement->Tap(1));
		TestEqual(Under(Case, TEXT("The second finger's tap is a click of its own")), Listener->ClickedCount, 2);
		TestTrue(Under(Case, TEXT("The second finger went through the player controller")), ControllerSawKey(Rig, EKeys::TouchKeys[1]));
	}
	return true;
}

/**
 * A finger dragged up a scroll box pulls its content up (SScrollBox pans on touch, and this library's
 * scroll view takes a finger's drag). The point of this one is WHERE the drag is: the preset re-reads
 * the "mouse" every frame and writes it into pointer 0, which the first finger shares. On a touch
 * screen that position is the finger's -- FSceneViewport moves its cached cursor with every touch --
 * so the drag follows the finger. The mouse is parked far from the box first; a drag that followed it
 * instead would pull nothing, or pull it the wrong way.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGameHostTouchDragTest,
	"DreamGUI.Driver.GameHost.AFingerDraggedThroughTheControllerScrollsTheBoxItLandedOnWhereverTheMouseWas",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamGameHostTouchDragTest::RunTest(const FString& Parameters)
{
	using namespace DreamGameHostTestLocal;
	for (const FHostCase& Case : ActorHosts)
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(OptionsFor(Case.Host));
		Rig.BindTest(this);
		if (!TestTrue(RigCameUp(Case, Rig), Rig.IsUsable()))
		{
			continue;
		}
		UDreamScrollBox* Box = MakeFilledBox(Rig);
		if (!TestTrue(Under(Case, TEXT("The box came up with a viewport and a content node")),
			Box != nullptr && Box->ViewportNode != nullptr && Box->GetContentNode() != nullptr))
		{
			continue;
		}
		FDreamElementRef Viewport = Rig.Driver()->Find(FDreamBy::Widget(Box->ViewportNode.Get()));
		const TOptional<FBox2D> ViewportRect = Viewport->GetPixelRect();
		if (!TestTrue(Under(Case, TEXT("The viewport is on screen and has a height")),
			ViewportRect.IsSet() && ViewportRect->Max.Y - ViewportRect->Min.Y > 1.0))
		{
			continue;
		}
		const double UnitsPerPixel = Box->ViewportNode->GetHeight() / (ViewportRect->Max.Y - ViewportRect->Min.Y);

		// The mouse in the far corner, nowhere near the box.
		TestTrue(Under(Case, TEXT("Parking the mouse completes")), Rig.Driver()->Sequence().MoveToPixel(FVector2D(5.0, 5.0)).Perform());

		TestTrue(Under(Case, TEXT("Dragging a finger 150 pixels up the box completes")), Viewport->TouchDragBy(FVector2D(0.0, -150.0), 0));
		const float Offset = Box->GetScrollOffset();
		TestTrue(FString::Printf(TEXT("[%s] The drag pulled the content up with the finger (offset %.1f)"), Case.Name, Offset),
			Offset > static_cast<float>(0.5 * 150.0 * UnitsPerPixel));
		// And the lifted finger took the cursor with it: FSceneViewport parks its cached cursor at
		// (-1, -1) when the last finger leaves the glass, so nothing stays hovered under it.
		const FVector2D Cursor = Rig.InputModule() != nullptr ? Rig.InputModule()->GetVirtualCursor() : FVector2D::ZeroVector;
		TestTrue(FString::Printf(TEXT("[%s] With no finger down the cursor is off the viewport (%s)"), Case.Name, *Cursor.ToString()),
			Cursor.Equals(FVector2D(-1.0, -1.0)));
	}
	return true;
}

/**
 * The chain an input actor needs is local player, then controller, then actor, and local players
 * live on a game instance -- so a rig built without one (the designer preview's kind of world) cannot
 * have an actor host, and must say so rather than come up half-built.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGameHostNoGameInstanceTest,
	"DreamGUI.Driver.GameHost.WithoutAGameInstanceNeitherActorHostCanBeBuiltAndTheRigSaysWhy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamGameHostNoGameInstanceTest::RunTest(const FString& Parameters)
{
	using namespace DreamGameHostTestLocal;
	for (const FHostCase& Case : ActorHosts)
	{
		FDreamRigOptions Options = OptionsFor(Case.Host);
		Options.bWithGameInstance = false;
		FDreamDriverRig Rig = FDreamDriverRig::Headless(Options);
		Rig.BindTest(this);

		TestFalse(Under(Case, TEXT("A rig with an actor host and no game instance is not usable")), Rig.IsUsable());
		TestTrue(FString::Printf(TEXT("[%s] It says the game instance is what is missing (it said: '%s')"), Case.Name, *Rig.GetBuildFailure()),
			Rig.GetBuildFailure().Contains(TEXT("GameInstance")));
		TestNull(*Under(Case, TEXT("It made no player controller on the way")), Rig.GetPlayerController());
		TestNull(*Under(Case, TEXT("and no game instance")), Rig.GetGameInstance());
	}
	return true;
}

/**
 * Pause menus: this library's own setting says screen-space UI keeps working while the game is paused
 * (UDreamUISettings::bScreenSpaceUIAffectByGamePause, "If false, ScreenSpaceUI can still do
 * interaction and animation when GamePause", false by default), and Slate UI is not paused by a game
 * pause at all. The module path honours that -- the screen raycaster reads the setting, the event
 * system ticks while paused -- which the module-only case below shows. A paused world runs the
 * controller's input frame with bGamePaused set, where UPlayerInput swaps the delegate of every binding
 * whose bExecuteWhenPaused is false for an unbound one (GetChordForKey), and Enhanced Input drops every
 * action whose bTriggerWhenPaused is false. So the presets bind with bExecuteWhenPaused on and apply the
 * setting themselves, and the Enhanced preset triggers runtime copies of its actions whose
 * bTriggerWhenPaused follows the setting; before that, no click reached the module while paused.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGameHostPausedClickTest,
	"DreamGUI.Driver.GameHost.WhileTheGameIsPausedAClickThroughThePlayerControllerStillReachesTheButton",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamGameHostPausedClickTest::RunTest(const FString& Parameters)
{
	using namespace DreamGameHostTestLocal;
	const FHostCase Cases[] = {
		{ EDreamRigInputHost::ModuleOnly, TEXT("input module only") },
		ActorHosts[0],
		ActorHosts[1],
	};
	for (const FHostCase& Case : Cases)
	{
		TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
		FDreamDriverRig Rig = FDreamDriverRig::Headless(OptionsFor(Case.Host));
		Rig.BindTest(this);
		if (!TestTrue(RigCameUp(Case, Rig), Rig.IsUsable()))
		{
			continue;
		}
		UDreamButton* Button = MakeListenedButton(Rig, TEXT("Resume"), FVector2D::ZeroVector, Listener.Get());
		if (!TestNotNull(*Under(Case, TEXT("A button can be made on the rig")), Button))
		{
			continue;
		}
		Rig.PumpFrames(1);

		// Paused the way a game is: a player state standing as the pauser (UWorld::IsPaused asks the
		// world settings for one). There is no game mode here to call SetPause on.
		UWorld* World = Rig.GetWorld();
		AWorldSettings* WorldSettings = World != nullptr ? World->GetWorldSettings() : nullptr;
		APlayerState* Pauser = World != nullptr ? World->SpawnActor<APlayerState>() : nullptr;
		if (!TestTrue(Under(Case, TEXT("The world has settings and a player state to pause it with")), WorldSettings != nullptr && Pauser != nullptr))
		{
			continue;
		}
		WorldSettings->SetPauserPlayerState(Pauser);
		ON_SCOPE_EXIT
		{
			WorldSettings->SetPauserPlayerState(nullptr);
		};
		if (!TestTrue(Under(Case, TEXT("The world is paused")), World->IsPaused()))
		{
			continue;
		}

		TestTrue(Under(Case, TEXT("Clicking the button while paused completes")), Rig.Driver()->Find(FDreamBy::Widget(Button))->Click());
		TestEqual(Under(Case, TEXT("The paused game's button was pressed once")), Listener->PressedCount, 1);
		TestEqual(Under(Case, TEXT("and clicked once")), Listener->ClickedCount, 1);
	}
	return true;
}

#endif
