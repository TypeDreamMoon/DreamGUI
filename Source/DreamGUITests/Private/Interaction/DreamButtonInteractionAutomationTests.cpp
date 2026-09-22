// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamButton.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamBaseEventData.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Interaction/DreamPressInteractionTestTypes.h"

/*
 * UDreamButton, driven the way a player drives it, and held to what UMG's UButton does for the same
 * gesture.
 *
 * The reference for every count and every order below is SButton (Slate/Private/Widgets/Input/
 * SButton.cpp), because UButton adds nothing of its own: its five events are SButton's OnClicked,
 * OnPressed, OnReleased and hover-changed callbacks passed straight through. Where the reading of
 * that source is not obvious it is spelled out beside the assertion that depends on it.
 *
 * Every gesture goes through the real pipeline -- a pixel worked out from the button's transform, the
 * pointer put there through the production input module, the raycaster finding whatever it finds --
 * and every count is read off a listener bound to the control's own public events. So "the button
 * was clicked" means a Blueprint bound to OnClicked would have run.
 */
namespace DreamPressButtonTestLocal
{
	const FIntPoint ViewportSize(1280, 720);

	/** Far enough sideways to be off a 200-wide button whatever its height, and far past any drag threshold. */
	const FVector2D OffTheButton(400.0, 0.0);

	struct FPlacedButton
	{
		UDreamButton* Button = nullptr;
		TSharedPtr<FDreamDriverElement> Element;

		bool IsReady() const { return Button != nullptr && Element.IsValid() && Element->Exists(); }
	};

	/**
	 * Rig, control, a frame of layout, and the driver's handle on it: the part every test here shares.
	 * All five of the button's events go to InListener, so a test asserts on whichever it is about and
	 * the rest are there to be asserted as zero.
	 */
	FPlacedButton PlaceButton(FAutomationTestBase& InTest, FDreamDriverRig& InRig, UDreamPressInteractionListener* InListener)
	{
		FPlacedButton Placed;
		InRig.BindTest(&InTest);
		if (!InTest.TestTrue(TEXT("The headless rig came up"), InRig.IsUsable()))
		{
			return Placed;
		}
		UDreamButton* Button = InRig.MakeControl<UDreamButton>(TEXT("Play"), nullptr, FVector2D(200.0, 60.0));
		if (!InTest.TestNotNull(TEXT("A button can be made on the rig"), Button))
		{
			return Placed;
		}
		Button->OnClicked.AddDynamic(InListener, &UDreamPressInteractionListener::HandleClicked);
		Button->OnPressed.AddDynamic(InListener, &UDreamPressInteractionListener::HandlePressed);
		Button->OnReleased.AddDynamic(InListener, &UDreamPressInteractionListener::HandleReleased);
		Button->OnHovered.AddDynamic(InListener, &UDreamPressInteractionListener::HandleHovered);
		Button->OnUnhovered.AddDynamic(InListener, &UDreamPressInteractionListener::HandleUnhovered);
		InRig.PumpFrames(1);

		Placed.Button = Button;
		Placed.Element = InRig.Driver()->Find(FDreamBy::Widget(Button));
		InTest.TestTrue(TEXT("The driver can find the button it is about to act on"), Placed.IsReady());
		return Placed;
	}
}

/**
 * SButton::OnMouseButtonDown calls Press (OnPressed); OnMouseButtonUp calls Release (OnReleased) and
 * only then ExecuteOnClick (OnClicked). So the order is fixed, and a button that clicked before it
 * released would run a click handler while its own release handler still thinks it is held.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPressButtonClickOrderTest,
	"DreamGUI.Button.ClickingTheCentrePressesReleasesAndClicksOnceEachInThatOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPressButtonClickOrderTest::RunTest(const FString& Parameters)
{
	using namespace DreamPressButtonTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FPlacedButton Placed = PlaceButton(*this, Rig, Listener.Get());
	if (!Placed.IsReady())
	{
		return false;
	}

	TestTrue(TEXT("Clicking the button's centre completes"), Placed.Element->Click());

	TestEqual(TEXT("One press"), Listener->PressedCount, 1);
	TestEqual(TEXT("One release"), Listener->ReleasedCount, 1);
	TestEqual(TEXT("One click"), Listener->ClickedCount, 1);

	const TArray<FName> Expected = { FName(TEXT("Pressed")), FName(TEXT("Released")), FName(TEXT("Clicked")) };
	const TArray<FName> Order = Listener->LogOnly(Expected);
	TestTrue(TEXT("The press comes first, then the release, then the click -- SButton's order"), Order == Expected);
	return true;
}

/**
 * SButton::OnMouseEnter / OnMouseLeave fire the hover callback exactly on the hovered flag changing,
 * so one pass over the button is one OnHovered and one OnUnhovered.
 *
 * The hovered STATE is asked of the control through UMG's own question, UWidget::IsHovered, whose
 * counterpart here is UDreamWidget::IsHovered. That one finds the event system through the UI
 * manager's registry, which the rig fills in when it makes a control (a world that never began play
 * would otherwise leave it empty) -- so it is answering about the same pointer the gesture moved.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPressButtonHoverTest,
	"DreamGUI.Button.MovingOnAndOffTheButtonHoversAndUnhoversItOnceEach",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPressButtonHoverTest::RunTest(const FString& Parameters)
{
	using namespace DreamPressButtonTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FPlacedButton Placed = PlaceButton(*this, Rig, Listener.Get());
	if (!Placed.IsReady())
	{
		return false;
	}

	TestTrue(TEXT("Moving onto the button completes"), Placed.Element->Hover());
	TestEqual(TEXT("Arriving is one OnHovered"), Listener->HoveredCount, 1);
	TestEqual(TEXT("Arriving is no OnUnhovered"), Listener->UnhoveredCount, 0);
	TestTrue(TEXT("While the pointer rests on it the button says it is hovered"), Placed.Button->IsHovered());

	TestTrue(TEXT("Moving off the button completes"), Placed.Element->MoveBy(OffTheButton));
	TestEqual(TEXT("Leaving is one OnUnhovered"), Listener->UnhoveredCount, 1);
	TestEqual(TEXT("Leaving is not a second OnHovered"), Listener->HoveredCount, 1);
	TestFalse(TEXT("Once the pointer has left the button no longer says it is hovered"), Placed.Button->IsHovered());

	// Hovering is not pressing: nothing about moving over a button may look like a use of it.
	TestEqual(TEXT("Hovering presses nothing"), Listener->PressedCount, 0);
	TestEqual(TEXT("Hovering clicks nothing"), Listener->ClickedCount, 0);
	return true;
}

/**
 * The desktop's cancel gesture: press on the button, slide off, let go. Under the default
 * DownAndUp click method SButton captures the pointer on the press, so the release still reaches it
 * (OnReleased), but OnMouseButtonUp clicks only when IsHovered() -- and the pointer has left.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPressButtonSlideOffTest,
	"DreamGUI.Button.PressingThenSlidingOffBeforeLettingGoReleasesWithoutClicking",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPressButtonSlideOffTest::RunTest(const FString& Parameters)
{
	using namespace DreamPressButtonTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FPlacedButton Placed = PlaceButton(*this, Rig, Listener.Get());
	if (!Placed.IsReady())
	{
		return false;
	}
	// The premise, stated: this test is about the DEFAULT method, and it is UMG's default too.
	TestTrue(TEXT("A button clicks on press-and-release by default, as UButton does"),
		Placed.Button->GetClickMethod() == EDreamUIClickMethod::DownAndUp);

	TestTrue(TEXT("Pressing on the button completes"), Placed.Element->Press());
	TestTrue(TEXT("Sliding off it with the button held completes"), Placed.Element->MoveBy(OffTheButton));
	TestTrue(TEXT("Letting go off the button completes"), Placed.Element->Release());

	TestEqual(TEXT("The press was a press"), Listener->PressedCount, 1);
	TestEqual(TEXT("The button hears that it was let go of"), Listener->ReleasedCount, 1);
	TestEqual(TEXT("A press that slid off before letting go is not a click"), Listener->ClickedCount, 0);
	return true;
}

/**
 * EButtonClickMethod::MouseDown: SButton::OnMouseButtonDown calls Press and then ExecuteOnClick at
 * once, and the release clicks nothing more. The click is due BEFORE the pointer comes up, which is
 * the whole of what the method is for (a key on an on-screen keyboard).
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPressButtonMouseDownMethodTest,
	"DreamGUI.Button.WithTheMouseDownClickMethodThePressAloneClicks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPressButtonMouseDownMethodTest::RunTest(const FString& Parameters)
{
	using namespace DreamPressButtonTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FPlacedButton Placed = PlaceButton(*this, Rig, Listener.Get());
	if (!Placed.IsReady())
	{
		return false;
	}
	Placed.Button->SetClickMethod(EDreamUIClickMethod::MouseDown);

	TestTrue(TEXT("Pressing on the button completes"), Placed.Element->Press());
	TestEqual(TEXT("The press alone is the click, before anything is let go"), Listener->ClickedCount, 1);
	TestEqual(TEXT("And it is still a press"), Listener->PressedCount, 1);

	TestTrue(TEXT("Letting go completes"), Placed.Element->Release());
	TestEqual(TEXT("Letting go is a release"), Listener->ReleasedCount, 1);
	TestEqual(TEXT("Letting go does not click a second time"), Listener->ClickedCount, 1);
	return true;
}

/**
 * A disabled SButton is taken out of the hit path altogether: FHittestGrid::GetBubblePath truncates
 * the path at the first disabled widget, so the button is sent no enter, no press and no release --
 * and SButton::IsHovered stays false, because hover is only ever set by an enter.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPressButtonDisabledTest,
	"DreamGUI.Button.ADisabledButtonIsNeitherHoveredNorPressedNorClicked",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPressButtonDisabledTest::RunTest(const FString& Parameters)
{
	using namespace DreamPressButtonTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FPlacedButton Placed = PlaceButton(*this, Rig, Listener.Get());
	if (!Placed.IsReady())
	{
		return false;
	}
	// UMG's SetIsEnabled, which is what a game flips to grey a button out.
	Placed.Button->SetIsEnabled(false);
	Rig.PumpFrames(1);

	TestTrue(TEXT("Clicking the disabled button's centre completes"), Placed.Element->Click());

	TestEqual(TEXT("A disabled button is not pressed"), Listener->PressedCount, 0);
	TestEqual(TEXT("A disabled button is not released"), Listener->ReleasedCount, 0);
	TestEqual(TEXT("A disabled button is not clicked"), Listener->ClickedCount, 0);
	TestEqual(TEXT("A disabled button is not told it is hovered"), Listener->HoveredCount, 0);
	TestFalse(TEXT("And the pointer resting on it does not make it say it is hovered"), Placed.Button->IsHovered());
	return true;
}

/**
 * The second press of a double click reaches SButton as OnMouseButtonDoubleClick, which SButton
 * turns straight back into OnMouseButtonDown ("We didn't handle the double click, treat it as single
 * click"). So a double click is two ordinary clicks, each with its press and its release. UButton has
 * no double-click event of its own, and neither does UDreamButton.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPressButtonDoubleClickTest,
	"DreamGUI.Button.DoubleClickingIsTwoFullClicks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPressButtonDoubleClickTest::RunTest(const FString& Parameters)
{
	using namespace DreamPressButtonTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FPlacedButton Placed = PlaceButton(*this, Rig, Listener.Get());
	if (!Placed.IsReady())
	{
		return false;
	}

	TestTrue(TEXT("Double-clicking the button completes"), Placed.Element->DoubleClick());

	TestEqual(TEXT("A double click is two clicks"), Listener->ClickedCount, 2);
	TestEqual(TEXT("Each with its own press"), Listener->PressedCount, 2);
	TestEqual(TEXT("And its own release"), Listener->ReleasedCount, 2);
	return true;
}

/**
 * SButton answers only the left button (or a touch): OnMouseButtonDown and OnMouseButtonUp both test
 * GetEffectingButton() == EKeys::LeftMouseButton before doing anything, and neither Press nor Release
 * nor a click happens for the right one. A right click is how a player asks for a context menu, and a
 * button that also ran its action on one would do two things at once.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPressButtonRightClickTest,
	"DreamGUI.Button.ARightClickDoesNotPressReleaseOrClickTheButton",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPressButtonRightClickTest::RunTest(const FString& Parameters)
{
	using namespace DreamPressButtonTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FPlacedButton Placed = PlaceButton(*this, Rig, Listener.Get());
	if (!Placed.IsReady())
	{
		return false;
	}

	TestTrue(TEXT("Right-clicking the button completes"), Placed.Element->Click(EDreamUIMouseButtonType::Right));

	TestEqual(TEXT("The right button does not press a button"), Listener->PressedCount, 0);
	TestEqual(TEXT("The right button does not release one"), Listener->ReleasedCount, 0);
	TestEqual(TEXT("The right button does not click one"), Listener->ClickedCount, 0);
	return true;
}

/**
 * The other half of the same knob: UMG has no way to make a button answer the right button, and this
 * control does -- AcceptedMouseButtons. Widened to take the right button as well as the left, a right
 * click is then a whole click: pressed, released and clicked, once each.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPressButtonRightClickAcceptedTest,
	"DreamGUI.Button.WithTheRightButtonAcceptedARightClickPressesReleasesAndClicks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPressButtonRightClickAcceptedTest::RunTest(const FString& Parameters)
{
	using namespace DreamPressButtonTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FPlacedButton Placed = PlaceButton(*this, Rig, Listener.Get());
	if (!Placed.IsReady())
	{
		return false;
	}
	const int32 RightBit = 1 << static_cast<int32>(EDreamUIMouseButtonType::Right);
	Placed.Button->SetAcceptedMouseButtons(Placed.Button->GetAcceptedMouseButtons() | RightBit);

	TestTrue(TEXT("Right-clicking the button completes"), Placed.Element->Click(EDreamUIMouseButtonType::Right));

	TestEqual(TEXT("A button told to answer the right button is pressed by it"), Listener->PressedCount, 1);
	TestEqual(TEXT("And released"), Listener->ReleasedCount, 1);
	TestEqual(TEXT("And clicked"), Listener->ClickedCount, 1);
	return true;
}

/**
 * Held is pressed, not clicked: nothing in SButton fires while a press is simply held, IsPressed is
 * SButton's bIsPressed and stays true until Release, and under DownAndUp the release over the button
 * is what clicks -- however long the hold was.
 *
 * Thirty frames at the pump's sixtieth of a second is half a second, which is also the event system's
 * long-press time: a hold that long must still end in an ordinary click.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPressButtonHoldTest,
	"DreamGUI.Button.HoldingTheButtonDownIsAPressThatClicksOnlyWhenLetGo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPressButtonHoldTest::RunTest(const FString& Parameters)
{
	using namespace DreamPressButtonTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FPlacedButton Placed = PlaceButton(*this, Rig, Listener.Get());
	if (!Placed.IsReady())
	{
		return false;
	}

	TestTrue(TEXT("Pressing on the button completes"), Placed.Element->Press());
	Rig.PumpFrames(30);

	TestEqual(TEXT("Holding is one press"), Listener->PressedCount, 1);
	TestEqual(TEXT("Holding is not a click"), Listener->ClickedCount, 0);
	TestEqual(TEXT("Holding is not a release"), Listener->ReleasedCount, 0);
	TestTrue(TEXT("While held the button says it is pressed"), Placed.Button->IsPressed());

	TestTrue(TEXT("Letting go completes"), Placed.Element->Release());
	TestFalse(TEXT("Once let go the button no longer says it is pressed"), Placed.Button->IsPressed());
	TestEqual(TEXT("Letting go over the button is the one click"), Listener->ClickedCount, 1);
	return true;
}

#endif
