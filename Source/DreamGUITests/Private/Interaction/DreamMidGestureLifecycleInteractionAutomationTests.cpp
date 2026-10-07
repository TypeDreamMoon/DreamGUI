// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamButton.h"
#include "Controls/DreamSlider.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamPointerEventData.h"
#include "Event/DreamScreenSpaceRaycaster.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Driver/DreamDriverUntil.h"
#include "Interaction/DreamDragInteractionTestTypes.h"
#include "Interaction/DreamPressInteractionTestTypes.h"

/*
 * A CONTROL THAT CHANGES UNDER THE POINTER: DISABLED, COLLAPSED, DESTROYED OR MOVED TO ANOTHER PARENT WHILE IT IS HOVERED,
 * HELD OR DRAGGED.
 *
 * What UMG does in each case is Slate's, and three rules of Slate's cover all of it:
 *  - The widgets under the pointer are worked out again whenever what is drawn changes under a cursor that does not move
 *    (FSlateUser::SynthesizeCursorMoveIfNeeded, SlateUser.cpp:736-770), and that path leaves out every widget that is
 *    disabled (FHittestGrid::GetBubblePath, HittestGrid.cpp:236) or not drawn. A widget that drops out of it is left:
 *    SButton::OnMouseLeave (SButton.cpp:544-562) un-hovers it, once.
 *  - A pressed SButton holds the mouse capture, as a path from the window down to it. When that path no longer leads to
 *    it -- the button collapsed, destroyed, or moved under another parent -- the path resolves Truncated and Slate lets the
 *    capture go (FSlateUser::GetCaptorPath, SlateUser.cpp:351-360), which SButton answers by releasing itself
 *    (SButton::OnMouseCaptureLost, :564-567). Its click needs the press it has just lost (OnMouseButtonUp's
 *    bMeetsPressedRequirements, :403-445), so the release that follows clicks nothing -- not the button, and not whatever
 *    was under it, which never had the press.
 *  - SSlider's drag is that same capture: lost, it ends with OnMouseCaptureEnd (SSlider::OnMouseCaptureLost), and moves
 *    after it are not the slider's (OnMouseMove acts only while it has the capture).
 * And once the gesture is over, the control is a control like any other: the next click on it is a click of its own.
 */
namespace DreamMidGestureLifecycleTestLocal
{
	const FIntPoint ViewportSize(1280, 720);
	const FVector2D ButtonSize(200.0, 60.0);

	FWaitTimeout ShortWait()
	{
		return FWaitTimeout::InSeconds(0.5);
	}

	/** A button whose five public events go to InListener, laid out. */
	UDreamButton* MakeObservedButton(FDreamDriverRig& InRig, const TCHAR* InName, UDreamWidget* InParent,
		UDreamPressInteractionListener* InListener, const FVector2D& InPosition = FVector2D::ZeroVector)
	{
		UDreamButton* Button = InRig.MakeControl<UDreamButton>(InName, InParent, ButtonSize, InPosition);
		if (Button != nullptr && InListener != nullptr)
		{
			Button->OnClicked.AddDynamic(InListener, &UDreamPressInteractionListener::HandleClicked);
			Button->OnPressed.AddDynamic(InListener, &UDreamPressInteractionListener::HandlePressed);
			Button->OnReleased.AddDynamic(InListener, &UDreamPressInteractionListener::HandleReleased);
			Button->OnHovered.AddDynamic(InListener, &UDreamPressInteractionListener::HandleHovered);
			Button->OnUnhovered.AddDynamic(InListener, &UDreamPressInteractionListener::HandleUnhovered);
		}
		return Button;
	}

	/** Whether the mouse's pointer still holds anything pressed. */
	bool HoldsAPress(const FDreamDriverContext& InContext)
	{
		const UDreamPointerEventData* Mouse = InContext.GetPointerEventData(0);
		return Mouse != nullptr && (Mouse->bNowIsTriggerPressed || Mouse->PressWidget != nullptr);
	}

	/** The pointer resting on InButton, and its hover announced. */
	bool HoverAndSettle(FAutomationTestBase& InTest, FDreamDriverRig& InRig, UDreamButton* InButton, const UDreamPressInteractionListener* InListener)
	{
		FDreamElementRef Element = InRig.Driver()->Find(FDreamBy::Widget(InButton));
		return InTest.TestTrue(TEXT("Moving onto the button completes"), Element->Hover())
			&& InTest.TestTrue(TEXT("...and hovers it once"), InRig.Driver()->Wait(
				FDreamUntil::Condition([InListener]() { return InListener->HoveredCount == 1; }, ShortWait()), ShortWait(),
				TEXT("the button's hover")));
	}

	/**
	 * A plain panel at InPosition for a control to be moved between, which the pointer passes through as it passes through
	 * a layout panel with no background: two of them on one place, neither hides what the other holds.
	 */
	UDreamWidget* MakeHolder(FDreamDriverRig& InRig, const TCHAR* InName, const FVector2D& InPosition)
	{
		UDreamWidget* Holder = InRig.MakeWidget(InName, nullptr, FVector2D(500.0, 200.0), InPosition);
		if (Holder != nullptr && Holder->GetVisual() != nullptr)
		{
			Holder->GetVisual()->SetRaycastTarget(false);
		}
		return Holder;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamButtonDisabledUnderRestingPointerTest,
	"DreamGUI.Button.AButtonDisabledUnderTheRestingPointerIsUnhoveredOnceWithoutThePointerMoving",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamButtonDisabledUnderRestingPointerTest, "DreamGUI.Button.AButtonDisabledUnderTheRestingPointerIsUnhoveredOnceWithoutThePointerMoving", "[Pointer][Disabled]")

/*
 * The pointer rests on the button and does not move. Disabled, the button drops out of the path under the cursor at the
 * next synthesized move and is left: one OnUnhovered, with nothing more over the frames after.
 */
bool FDreamButtonDisabledUnderRestingPointerTest::RunTest(const FString& Parameters)
{
	using namespace DreamMidGestureLifecycleTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	UDreamButton* Button = Rig.IsUsable() ? MakeObservedButton(Rig, TEXT("Play"), nullptr, Listener.Get()) : nullptr;
	if (!TestNotNull(TEXT("The rig and a button came up"), Button))
	{
		return false;
	}
	Rig.PumpFrames(1);
	if (!HoverAndSettle(*this, Rig, Button, Listener.Get()))
	{
		return false;
	}

	// UMG's SetIsEnabled, which is what a game flips to grey a button out.
	Button->SetIsEnabled(false);
	const UDreamPressInteractionListener* Heard = Listener.Get();
	TestTrue(TEXT("Disabled under the resting pointer, the button is unhovered"), Rig.Driver()->Wait(
		FDreamUntil::Condition([Heard]() { return Heard->UnhoveredCount > 0; }, ShortWait()), ShortWait(),
		TEXT("the disabled button's unhover")));
	Rig.PumpFrames(3);
	TestEqual(TEXT("...once"), Listener->UnhoveredCount, 1);
	TestFalse(TEXT("...and the driver no longer finds it hovered"), Rig.Driver()->Find(FDreamBy::Widget(Button))->IsHovered());
	TestEqual(TEXT("Nothing was pressed or clicked"), Listener->PressedCount + Listener->ClickedCount, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamButtonCollapsedUnderRestingPointerTest,
	"DreamGUI.Button.AButtonCollapsedUnderTheRestingPointerIsUnhoveredOnceWithoutThePointerMoving",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamButtonCollapsedUnderRestingPointerTest, "DreamGUI.Button.AButtonCollapsedUnderTheRestingPointerIsUnhoveredOnceWithoutThePointerMoving", "[Pointer][Animated]")

/*
 * The same with the button collapsed: no longer drawn, it is not in the path under the cursor, and is left once.
 */
bool FDreamButtonCollapsedUnderRestingPointerTest::RunTest(const FString& Parameters)
{
	using namespace DreamMidGestureLifecycleTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	UDreamButton* Button = Rig.IsUsable() ? MakeObservedButton(Rig, TEXT("Play"), nullptr, Listener.Get()) : nullptr;
	if (!TestNotNull(TEXT("The rig and a button came up"), Button))
	{
		return false;
	}
	Rig.PumpFrames(1);
	if (!HoverAndSettle(*this, Rig, Button, Listener.Get()))
	{
		return false;
	}

	Button->SetVisibility(EDreamWidgetVisibility::Collapsed);
	const UDreamPressInteractionListener* Heard = Listener.Get();
	TestTrue(TEXT("Collapsed under the resting pointer, the button is unhovered"), Rig.Driver()->Wait(
		FDreamUntil::Condition([Heard]() { return Heard->UnhoveredCount > 0; }, ShortWait()), ShortWait(),
		TEXT("the collapsed button's unhover")));
	Rig.PumpFrames(3);
	TestEqual(TEXT("...once"), Listener->UnhoveredCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamButtonCollapsedWhileHeldTest,
	"DreamGUI.Button.AButtonCollapsedWhileHeldIsReleasedWithoutAClickAndClicksAgainOnceShown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamButtonCollapsedWhileHeldTest, "DreamGUI.Button.AButtonCollapsedWhileHeldIsReleasedWithoutAClickAndClicksAgainOnceShown", "[Pointer][Animated]")

/*
 * Held, then collapsed: the capture path no longer leads to the button, Slate lets the capture go and the button releases
 * itself, so the release that follows -- at the same pixel, the button not there -- clicks nothing. Shown again, the next
 * click on it is a click: the press it lost is not still waiting for its release.
 */
bool FDreamButtonCollapsedWhileHeldTest::RunTest(const FString& Parameters)
{
	using namespace DreamMidGestureLifecycleTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	UDreamButton* Button = Rig.IsUsable() ? MakeObservedButton(Rig, TEXT("Play"), nullptr, Listener.Get()) : nullptr;
	if (!TestNotNull(TEXT("The rig and a button came up"), Button))
	{
		return false;
	}
	Rig.PumpFrames(1);
	const UDreamPressInteractionListener* Heard = Listener.Get();

	TestTrue(TEXT("Pressing the button, collapsing it and letting go completes"),
		Rig.Driver()->Sequence()
			.MoveTo(FDreamBy::Widget(Button))
			.Press()
			.Wait(FDreamUntil::Condition([Heard]() { return Heard->PressedCount == 1; }, ShortWait()), ShortWait(), TEXT("the press"))
			.Then([Button](FDreamDriverContext&) { Button->SetVisibility(EDreamWidgetVisibility::Collapsed); })
			.WaitFrames(1)
			.Release()
			.WaitFrames(1)
			.Then([this](FDreamDriverContext& InContext)
			{
				TestFalse(TEXT("Nothing is left held"), HoldsAPress(InContext));
			})
			.Perform());
	TestEqual(TEXT("The press was let go of once"), Listener->ReleasedCount, 1);
	TestEqual(TEXT("...and the release clicked nothing"), Listener->ClickedCount, 0);

	Button->SetVisibility(EDreamWidgetVisibility::Visible);
	Rig.PumpFrames(2);
	TestTrue(TEXT("Clicking the button shown again completes"), Rig.Driver()->Find(FDreamBy::Widget(Button))->Click());
	TestEqual(TEXT("...and is a click of its own, once"), Listener->ClickedCount, 1);
	TestEqual(TEXT("...from a press of its own"), Listener->PressedCount, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamButtonDestroyedWhileHeldTest,
	"DreamGUI.Button.AButtonDestroyedWhileHeldLeavesNothingHeldAndTheReleaseClicksNothingNotEvenTheButtonBeneathIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamButtonDestroyedWhileHeldTest, "DreamGUI.Button.AButtonDestroyedWhileHeldLeavesNothingHeldAndTheReleaseClicksNothingNotEvenTheButtonBeneathIt", "[Pointer][Animated]")

/*
 * A button on top of another, pressed, then destroyed with the press held. The release lands where the button beneath now
 * is, and that button never had the press: SButton::OnMouseButtonUp asks for its own press before it clicks, so nothing
 * is clicked and nothing is left held. The button beneath then takes a click of its own, once.
 */
bool FDreamButtonDestroyedWhileHeldTest::RunTest(const FString& Parameters)
{
	using namespace DreamMidGestureLifecycleTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> BeneathListener(NewObject<UDreamPressInteractionListener>());
	TStrongObjectPtr<UDreamPressInteractionListener> OnTopListener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	// Beneath first, so the one made after it is in front.
	UDreamButton* Beneath = Rig.IsUsable() ? MakeObservedButton(Rig, TEXT("Beneath"), nullptr, BeneathListener.Get()) : nullptr;
	UDreamButton* OnTop = Beneath != nullptr ? MakeObservedButton(Rig, TEXT("OnTop"), nullptr, OnTopListener.Get()) : nullptr;
	if (!TestTrue(TEXT("The rig and two buttons, one over the other, came up"), Beneath != nullptr && OnTop != nullptr))
	{
		return false;
	}
	Rig.PumpFrames(1);
	const UDreamPressInteractionListener* OnTopHeard = OnTopListener.Get();

	TestTrue(TEXT("Pressing the button on top, destroying it and letting go completes"),
		Rig.Driver()->Sequence()
			.MoveTo(FDreamBy::Widget(OnTop))
			.Press()
			.Wait(FDreamUntil::Condition([OnTopHeard]() { return OnTopHeard->PressedCount == 1; }, ShortWait()), ShortWait(),
				TEXT("the press of the button on top"))
			.Then([OnTop](FDreamDriverContext&) { OnTop->DestroyWidget(); })
			.WaitFrames(1)
			.Release()
			.WaitFrames(1)
			.Then([this](FDreamDriverContext& InContext)
			{
				TestFalse(TEXT("Nothing is left held"), HoldsAPress(InContext));
			})
			.Perform());
	TestEqual(TEXT("The button on top, destroyed, was never clicked"), OnTopListener->ClickedCount, 0);
	TestEqual(TEXT("The button beneath was not pressed by a press it never had"), BeneathListener->PressedCount, 0);
	TestEqual(TEXT("...nor clicked by its release"), BeneathListener->ClickedCount, 0);

	TestTrue(TEXT("Clicking the button beneath completes"), Rig.Driver()->Find(FDreamBy::Widget(Beneath))->Click());
	TestEqual(TEXT("...and is one click of its own"), BeneathListener->ClickedCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamButtonReparentedWhileHeldTest,
	"DreamGUI.Button.AButtonMovedToAnotherParentWhileHeldIsReleasedWithoutAClickThoughThePointerNeverLeftIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamButtonReparentedWhileHeldTest, "DreamGUI.Button.AButtonMovedToAnotherParentWhileHeldIsReleasedWithoutAClickThoughThePointerNeverLeftIt", "[Pointer][Animated]")

/*
 * Two panels on the same place; the button on the first, pressed, then moved to the second keeping where it stands, so the
 * pointer is over it from the press to the release. The capture path went through the first panel and leads to the button
 * no longer: Slate lets the capture go and the button releases itself, and the release over it clicks nothing (the press
 * belongs to the parent it was taken from, not to where it is now). On its new parent the next click is a click, once.
 */
bool FDreamButtonReparentedWhileHeldTest::RunTest(const FString& Parameters)
{
	using namespace DreamMidGestureLifecycleTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	UDreamWidget* First = Rig.IsUsable() ? MakeHolder(Rig, TEXT("First"), FVector2D::ZeroVector) : nullptr;
	UDreamWidget* Second = First != nullptr ? MakeHolder(Rig, TEXT("Second"), FVector2D::ZeroVector) : nullptr;
	UDreamButton* Button = Second != nullptr ? MakeObservedButton(Rig, TEXT("Play"), First, Listener.Get()) : nullptr;
	if (!TestTrue(TEXT("The rig, two panels on one place and a button on the first came up"), Button != nullptr))
	{
		return false;
	}
	Rig.PumpFrames(1);
	const TOptional<FVector2D> Before = Rig.Driver()->Find(FDreamBy::Widget(Button))->GetCentrePixel();
	const UDreamPressInteractionListener* Heard = Listener.Get();

	TestTrue(TEXT("Pressing the button, moving it to the other panel and letting go over it completes"),
		Rig.Driver()->Sequence()
			.MoveTo(FDreamBy::Widget(Button))
			.Press()
			.Wait(FDreamUntil::Condition([Heard]() { return Heard->PressedCount == 1; }, ShortWait()), ShortWait(), TEXT("the press"))
			.Then([Button, Second](FDreamDriverContext&) { Button->SetParent(Second, /*InKeepWorldPosition*/ true); })
			.WaitFrames(1)
			.Release()
			.WaitFrames(1)
			.Then([this](FDreamDriverContext& InContext)
			{
				TestFalse(TEXT("Nothing is left held"), HoldsAPress(InContext));
			})
			.Perform());
	const TOptional<FVector2D> After = Rig.Driver()->Find(FDreamBy::Widget(Button))->GetCentrePixel();
	TestTrue(TEXT("The button is on the second panel now"), Button->GetParent() == Second);
	TestTrue(TEXT("...standing where it stood, under the pointer"), Before.IsSet() && After.IsSet() && Before->Equals(After.GetValue(), 1.0));
	TestEqual(TEXT("The press taken from it with its old parent was let go of once"), Listener->ReleasedCount, 1);
	TestEqual(TEXT("...and the release over it clicked nothing"), Listener->ClickedCount, 0);

	TestTrue(TEXT("Clicking the button on its new parent completes"), Rig.Driver()->Find(FDreamBy::Widget(Button))->Click());
	TestEqual(TEXT("...and is a click of its own, once"), Listener->ClickedCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSliderReparentedMidDragTest,
	"DreamGUI.Slider.ASliderMovedToAnotherParentPartwayThroughADragEndsItsCaptureOnceAndTheRestOfTheDragMovesNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamSliderReparentedMidDragTest, "DreamGUI.Slider.ASliderMovedToAnotherParentPartwayThroughADragEndsItsCaptureOnceAndTheRestOfTheDragMovesNothing", "[Pointer][Animated]")

/*
 * Dragged a quarter of the way, then moved to another panel at the same place keeping where it stands, then dragged on:
 * the capture the drag was went with the old parent's path, so the slider says its capture ended, once, and the rest of
 * the pointer's travel is no longer the slider's -- the value stays where the drag had taken it. The release adds no
 * second end.
 */
bool FDreamSliderReparentedMidDragTest::RunTest(const FString& Parameters)
{
	using namespace DreamMidGestureLifecycleTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	UDreamWidget* First = Rig.IsUsable() ? MakeHolder(Rig, TEXT("First"), FVector2D::ZeroVector) : nullptr;
	UDreamWidget* Second = First != nullptr ? MakeHolder(Rig, TEXT("Second"), FVector2D::ZeroVector) : nullptr;
	UDreamSlider* Slider = Second != nullptr ? Rig.MakeControl<UDreamSlider>(TEXT("Volume"), First, FVector2D(400.0, 40.0)) : nullptr;
	Rig.PumpFrames(2);
	if (!TestTrue(TEXT("The rig, two panels on one place and a slider on the first came up"),
		Slider != nullptr && Slider->HandleNode != nullptr && Slider->HandleAreaNode != nullptr))
	{
		return false;
	}
	const TOptional<FBox2D> Travel = Rig.Driver()->Find(FDreamBy::Widget(Slider->HandleAreaNode.Get()))->GetPixelRect();
	const TOptional<FVector2D> Grip = Rig.Driver()->Find(FDreamBy::Widget(Slider->HandleNode.Get()))->GetCentrePixel();
	if (!TestTrue(TEXT("The handle and its travel are on screen"), Travel.IsSet() && Grip.IsSet()))
	{
		return false;
	}
	const double TravelLength = Travel->Max.X - Travel->Min.X;
	const float OnePixel = static_cast<float>(1.0 / FMath::Max(TravelLength, 1.0));
	TStrongObjectPtr<UDreamDragInteractionProbe> CaptureEnds(NewObject<UDreamDragInteractionProbe>());
	Slider->OnMouseCaptureEnd.AddDynamic(CaptureEnds.Get(), &UDreamDragInteractionProbe::RecordSignal);
	const FVector2D Quarter(Travel->Min.X + 0.25 * TravelLength, Grip->Y);
	const FVector2D ThreeQuarters(Travel->Min.X + 0.75 * TravelLength, Grip->Y);

	float ValueAtTheMove = -1.0f;
	TestTrue(TEXT("Dragging a quarter of the way, moving the slider to the other panel and dragging on completes"),
		Rig.Driver()->Sequence()
			.MoveToPixel(Grip.GetValue())
			.Press()
			.MoveBy(FVector2D(20.0, 0.0))
			.MoveToPixel(Quarter)
			.Then([Slider, Second, &ValueAtTheMove](FDreamDriverContext&)
			{
				ValueAtTheMove = Slider->GetValue();
				Slider->SetParent(Second, /*InKeepWorldPosition*/ true);
			})
			.WaitFrames(1)
			.MoveToPixel((Quarter + ThreeQuarters) * 0.5)
			.MoveToPixel(ThreeQuarters)
			.WaitFrames(1)
			.Then([this, &CaptureEnds](FDreamDriverContext&)
			{
				TestEqual(TEXT("Moved to another parent mid-drag, the slider ended its capture, once"), CaptureEnds->Signals, 1);
			})
			.Release()
			.Perform());
	TestNearlyEqual(TEXT("The drag had taken the value a quarter of the way before the move"), ValueAtTheMove, 0.25f, 1.5f * OnePixel);
	TestNearlyEqual(TEXT("...and the rest of the pointer's travel left it there"), Slider->GetValue(), ValueAtTheMove, 1.5f * OnePixel);
	TestEqual(TEXT("The release ended no second capture"), CaptureEnds->Signals, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSliderDisabledMidDragTest,
	"DreamGUI.Slider.ASliderDisabledPartwayThroughADragStopsFollowingThePointerAndEndsItsCaptureOnceAtTheRelease",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamSliderDisabledMidDragTest, "DreamGUI.Slider.ASliderDisabledPartwayThroughADragStopsFollowingThePointerAndEndsItsCaptureOnceAtTheRelease", "[Pointer][Disabled]")

/*
 * Dragged a quarter of the way, then disabled with the button still down: a disabled control moves for nothing, so the
 * rest of the drag leaves the value where it was, and the capture the press announced is ended once, at the release, so a
 * consumer waiting for the end of the drag hears it. That is the rule a held button and a held check box keep in this
 * library (CommonUI's: CommonButtonTypes.cpp, a disabled button's release is no click); SSlider itself goes on
 * following a captured pointer when disabled (SSlider::OnMouseMove asks only IsLocked), which a game would not want.
 */
bool FDreamSliderDisabledMidDragTest::RunTest(const FString& Parameters)
{
	using namespace DreamMidGestureLifecycleTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	UDreamSlider* Slider = Rig.IsUsable() ? Rig.MakeControl<UDreamSlider>(TEXT("Volume"), nullptr, FVector2D(400.0, 40.0)) : nullptr;
	Rig.PumpFrames(2);
	if (!TestTrue(TEXT("The rig and a slider came up"), Slider != nullptr && Slider->HandleNode != nullptr && Slider->HandleAreaNode != nullptr))
	{
		return false;
	}
	const TOptional<FBox2D> Travel = Rig.Driver()->Find(FDreamBy::Widget(Slider->HandleAreaNode.Get()))->GetPixelRect();
	const TOptional<FVector2D> Grip = Rig.Driver()->Find(FDreamBy::Widget(Slider->HandleNode.Get()))->GetCentrePixel();
	if (!TestTrue(TEXT("The handle and its travel are on screen"), Travel.IsSet() && Grip.IsSet()))
	{
		return false;
	}
	const double TravelLength = Travel->Max.X - Travel->Min.X;
	const float OnePixel = static_cast<float>(1.0 / FMath::Max(TravelLength, 1.0));
	TStrongObjectPtr<UDreamDragInteractionProbe> CaptureEnds(NewObject<UDreamDragInteractionProbe>());
	Slider->OnMouseCaptureEnd.AddDynamic(CaptureEnds.Get(), &UDreamDragInteractionProbe::RecordSignal);
	const FVector2D Quarter(Travel->Min.X + 0.25 * TravelLength, Grip->Y);
	const FVector2D ThreeQuarters(Travel->Min.X + 0.75 * TravelLength, Grip->Y);

	float ValueWhenDisabled = -1.0f;
	TestTrue(TEXT("Dragging a quarter of the way, disabling the slider and dragging on completes"),
		Rig.Driver()->Sequence()
			.MoveToPixel(Grip.GetValue())
			.Press()
			.MoveBy(FVector2D(20.0, 0.0))
			.MoveToPixel(Quarter)
			.Then([Slider, &ValueWhenDisabled](FDreamDriverContext&)
			{
				ValueWhenDisabled = Slider->GetValue();
				Slider->SetIsEnabled(false);
			})
			.MoveToPixel((Quarter + ThreeQuarters) * 0.5)
			.MoveToPixel(ThreeQuarters)
			.WaitFrames(1)
			.Then([this, &CaptureEnds](FDreamDriverContext&)
			{
				TestEqual(TEXT("While the button is still down no capture has ended"), CaptureEnds->Signals, 0);
			})
			.Release()
			.WaitFrames(1)
			.Perform());
	TestNearlyEqual(TEXT("The drag had taken the value a quarter of the way"), ValueWhenDisabled, 0.25f, 1.5f * OnePixel);
	TestNearlyEqual(TEXT("...and, disabled, the slider stayed there"), Slider->GetValue(), ValueWhenDisabled, 1.5f * OnePixel);
	TestEqual(TEXT("The release ended the capture, once"), CaptureEnds->Signals, 1);
	return true;
}

#endif
