// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Controls/DreamButton.h"
#include "Controls/DreamTextInput.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamGUISettings.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamPointerEventData.h"
#include "Event/DreamScreenSpaceRaycaster.h"
#include "Interaction/DreamUIVirtualCursor.h"
#include "Interaction/UIEventTrigger.h"
#include "Interaction/UITextInput.h"
#include "UObject/StrongObjectPtr.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverInputModule.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "DreamPointerEventTestTypes.h"
#include "Interaction/DreamTextInteractionTestTypes.h"

/*
 * THE INPUTS A MOUSE IS NOT.
 *
 * Fingers, time, a gamepad's Back button and the virtual cursor a gamepad drives: each has its own
 * entry into the standalone module, and until these steps existed none of them could be put through
 * the real pipeline from a test. A finger is a pointer of its own (the module keys touches by index);
 * a long press is time on the world clock; Back is the key the standalone actor treats as Back; the
 * virtual cursor is a stick integrated into the module's substituted pointer. Every assertion here is
 * about what the widget under it was told.
 */
namespace DreamDriverTouchAndGamepadTestLocal
{
	const FIntPoint ViewportSize(1280, 720);

	/** What one widget was sent, counted by the production component that exists to be told. */
	struct FTouchWidgetLog
	{
		int32 Down = 0;
		int32 Up = 0;
		int32 Click = 0;
		int32 BeginDrag = 0;
		int32 EndDrag = 0;
		int32 LastPointerID = INDEX_NONE;

		void Observe(UDreamWidget* InWidget)
		{
			UUIEventTrigger* Trigger = InWidget != nullptr ? InWidget->AddComponent<UUIEventTrigger>() : nullptr;
			if (Trigger == nullptr)
			{
				return;
			}
			Trigger->GetOnPointerDownEvent().AddLambda([this](UDreamPointerEventData* InEventData)
			{
				++Down;
				LastPointerID = InEventData != nullptr ? InEventData->PointerID : INDEX_NONE;
			});
			Trigger->GetOnPointerUpEvent().AddLambda([this](UDreamPointerEventData*) { ++Up; });
			Trigger->GetOnPointerClickEvent().AddLambda([this](UDreamPointerEventData*) { ++Click; });
			Trigger->GetOnPointerBeginDragEvent().AddLambda([this](UDreamPointerEventData*) { ++BeginDrag; });
			Trigger->GetOnPointerEndDragEvent().AddLambda([this](UDreamPointerEventData*) { ++EndDrag; });
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverTouchTapTest,
	"DreamGUI.Driver.Touch.ATapOnAButtonClicksItOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverTouchTapTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverTouchAndGamepadTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamButton* Button = Rig.MakeControl<UDreamButton>(TEXT("Play"), nullptr, FVector2D(200.0, 80.0));
	if (!TestNotNull(TEXT("A button can be made on the rig"), Button))
	{
		return false;
	}
	TStrongObjectPtr<UDreamTextInteractionListener> Listener(NewObject<UDreamTextInteractionListener>());
	Button->OnClicked.AddDynamic(Listener.Get(), &UDreamTextInteractionListener::HandleClicked);
	Rig.PumpFrames(1);

	// A button's touch rule is DownAndUp by default -- touch it and lift off it -- which a tap is.
	FDreamElementRef Play = Rig.Driver()->Find(FDreamBy::Name(TEXT("Play")));
	TestTrue(TEXT("Tapping the button completes"), Play->Tap());
	TestEqual(TEXT("One tap is one click"), Listener->ClickedCount, 1);
	TestTrue(TEXT("A second tap completes"), Play->Tap());
	// Two taps, two clicks: the button clicks on every tap, whether the second is taken as a fresh
	// press (a lifted finger's pointer is retired) or as the second half of a pair (UUIButton treats a
	// double click as a press, as SButton does).
	TestEqual(TEXT("Every tap is a click"), Listener->ClickedCount, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverTouchDragTest,
	"DreamGUI.Driver.Touch.AFingerDraggedPastTheThresholdDragsAndOneThatStopsShortTaps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverTouchDragTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverTouchAndGamepadTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamWidget* Card = Rig.MakeWidget(TEXT("Card"), nullptr, FVector2D(300.0, 200.0));
	FTouchWidgetLog Log;
	Log.Observe(Card);
	Rig.PumpFrames(1);
	FDreamElementRef CardElement = Rig.Driver()->Find(FDreamBy::Name(TEXT("Card")));
	const double ThresholdPixels = FMath::Sqrt((double)Rig.Raycaster()->GetScaledDragThresholdSquare());

	// Short of the threshold: a finger that barely moved is a tap, with a click at the end of it.
	TestTrue(TEXT("A touch that moves less than the threshold completes"),
		CardElement->TouchDragBy(FVector2D(ThresholdPixels * 0.5, 0.0), 1));
	TestEqual(TEXT("A finger that stayed inside the threshold did not drag"), Log.BeginDrag, 0);
	TestEqual(TEXT("It is a tap, and a tap is a click"), Log.Click, 1);
	TestEqual(TEXT("The press came from the finger's own pointer"), Log.LastPointerID, 1);

	// Well past it: the same finger now drags, begins once and ends once, and clicks nothing.
	TestTrue(TEXT("A touch drag well past the threshold completes"),
		CardElement->TouchDragBy(FVector2D(ThresholdPixels * 10.0 + 40.0, 0.0), 1));
	TestEqual(TEXT("The drag began once"), Log.BeginDrag, 1);
	TestEqual(TEXT("And ended once, when the finger lifted"), Log.EndDrag, 1);
	TestEqual(TEXT("A press that became a drag is not a click"), Log.Click, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverTouchTwoFingersTest,
	"DreamGUI.Driver.Touch.TwoFingersAreTwoPointersEachWithItsOwnPressAndItsOwnClick",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverTouchTwoFingersTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverTouchAndGamepadTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamWidget* Left = Rig.MakeWidget(TEXT("Left"), nullptr, FVector2D(200.0, 150.0), FVector2D(-300.0, 0.0));
	UDreamWidget* Right = Rig.MakeWidget(TEXT("Right"), nullptr, FVector2D(200.0, 150.0), FVector2D(300.0, 0.0));
	FTouchWidgetLog LeftLog;
	FTouchWidgetLog RightLog;
	LeftLog.Observe(Left);
	RightLog.Observe(Right);
	Rig.PumpFrames(1);

	const TOptional<FVector2D> LeftCentre = Rig.Driver()->Find(FDreamBy::Name(TEXT("Left")))->GetCentrePixel();
	const TOptional<FVector2D> RightCentre = Rig.Driver()->Find(FDreamBy::Name(TEXT("Right")))->GetCentrePixel();
	if (!TestTrue(TEXT("Both targets have a centre pixel"), LeftCentre.IsSet() && RightCentre.IsSet()))
	{
		return false;
	}

	const bool bPerformed = Rig.Driver()->Sequence()
		.TouchDown(1, LeftCentre.GetValue())
		.TouchDown(2, RightCentre.GetValue())
		.Then([this, Left, Right](FDreamDriverContext& InContext)
		{
			const UDreamPointerEventData* First = InContext.GetPointerEventData(1);
			const UDreamPointerEventData* Second = InContext.GetPointerEventData(2);
			if (!TestTrue(TEXT("Each finger has a pointer of its own"), First != nullptr && Second != nullptr && First != Second))
			{
				return;
			}
			TestTrue(TEXT("The first finger is pressing the left target"), First->bNowIsTriggerPressed && First->PressWidget == Left);
			TestTrue(TEXT("The second finger is pressing the right target"), Second->bNowIsTriggerPressed && Second->PressWidget == Right);
		})
		.TouchUp(1)
		.Then([this](FDreamDriverContext& InContext)
		{
			// The first finger lifted and is gone; the second is exactly where it was.
			TestNull(TEXT("A lifted finger's pointer is retired"), InContext.GetPointerEventData(1));
			const UDreamPointerEventData* Second = InContext.GetPointerEventData(2);
			TestTrue(TEXT("Lifting one finger leaves the other pressing"), Second != nullptr && Second->bNowIsTriggerPressed);
		})
		.TouchUp(2)
		.Perform();
	TestTrue(TEXT("The two-finger gesture completes"), bPerformed);

	TestEqual(TEXT("The left target was pressed once"), LeftLog.Down, 1);
	TestEqual(TEXT("By the first finger"), LeftLog.LastPointerID, 1);
	TestEqual(TEXT("The right target was pressed once"), RightLog.Down, 1);
	TestEqual(TEXT("By the second finger"), RightLog.LastPointerID, 2);
	TestEqual(TEXT("Each target was clicked once, by its own finger"), LeftLog.Click + RightLog.Click, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverTouchLongPressTest,
	"DreamGUI.Driver.Touch.AFingerHeldForTheLongPressTimeIsALongPressAndAShorterHoldIsNot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverTouchLongPressTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverTouchAndGamepadTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamWidget* Pad = Rig.MakeWidget(TEXT("Pad"), nullptr, FVector2D(200.0, 120.0));
	UDreamLongPressCounter* Counter = Pad != nullptr ? Pad->AddComponent<UDreamLongPressCounter>() : nullptr;
	if (!TestNotNull(TEXT("A widget that counts long presses can be made"), Counter))
	{
		return false;
	}
	Rig.PumpFrames(1);
	const float LongPressTime = Rig.EventSystem()->GetLongPressTime();
	const TOptional<FVector2D> Centre = Rig.Driver()->Find(FDreamBy::Name(TEXT("Pad")))->GetCentrePixel();
	if (!TestTrue(TEXT("The event system has a long press time"), LongPressTime > 0.0f)
		|| !TestTrue(TEXT("The pad has a centre pixel"), Centre.IsSet()))
	{
		return false;
	}

	TestTrue(TEXT("A finger held for half the long press time completes"),
		Rig.Driver()->Sequence().TouchDown(1, Centre.GetValue()).WaitSeconds(LongPressTime * 0.5f).TouchUp(1).Perform());
	TestEqual(TEXT("A hold shorter than the long press time is not a long press"), Counter->LongPressCount, 0);

	TestTrue(TEXT("A finger held past the long press time completes"),
		Rig.Driver()->Sequence().TouchDown(1, Centre.GetValue()).WaitSeconds(LongPressTime + 0.1f).TouchUp(1).Perform());
	TestEqual(TEXT("A hold that reached the long press time is one long press"), Counter->LongPressCount, 1);
	TestEqual(TEXT("Carried by the finger that held"), Counter->LastPointerID, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverGamepadBackTest,
	"DreamGUI.Driver.Gamepad.BackEndsTheEditOfTheFieldBeingEdited",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverGamepadBackTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverTouchAndGamepadTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamTextInput* Field = Rig.MakeControl<UDreamTextInput>(TEXT("Callsign"), nullptr, FVector2D(320.0, 40.0));
	if (!TestNotNull(TEXT("A text field can be made"), Field) || !TestNotNull(TEXT("It has its input behaviour"), Field->InputBehaviour.Get()))
	{
		return false;
	}
	Rig.PumpFrames(1);

	FDreamElementRef FieldElement = Rig.Driver()->Find(FDreamBy::Name(TEXT("Callsign")));
	TestTrue(TEXT("Clicking in and typing completes"), FieldElement->Type(TEXT("abc")));
	if (!TestTrue(TEXT("The field is being edited"), Field->InputBehaviour->IsInputActive()))
	{
		return false;
	}

	// Back -- what the pad's B / Circle and the keyboard's Escape both are to the standalone actor --
	// reaches an edit in progress through the navigation stack and cancels it.
	TestTrue(TEXT("Pressing Back completes"), Rig.Driver()->Sequence().Back().Perform());
	TestFalse(TEXT("Back ended the edit"), Field->InputBehaviour->IsInputActive());
	// Revert on Escape is off by default, so the text stays what was typed.
	TestEqual(TEXT("And kept what was typed"), Field->GetText(), FString(TEXT("abc")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverGamepadVirtualCursorMoveTest,
	"DreamGUI.Driver.Gamepad.TheVirtualCursorPushedRightForHalfASecondMovesThePointerRight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverGamepadVirtualCursorMoveTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverTouchAndGamepadTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	// The cursor reads the stick from player 0's controller, so the rig needs one with its input up.
	Rig.EnsureGameInputHost();
	Rig.PumpFrames(1);

	const FVector2D Start(200.0, 360.0);
	const float PushSeconds = 0.5f;
	const bool bPerformed = Rig.Driver()->Sequence()
		.MoveToPixel(Start)
		.ActivateVirtualCursor()
		.VirtualCursorStick(FVector2D(1.0, 0.0), PushSeconds)
		.Perform();
	if (!TestTrue(TEXT("Activating the cursor and pushing the stick completes"), bPerformed))
	{
		return false;
	}

	UDreamUIVirtualCursorSubsystem* Cursor = UDreamUIVirtualCursorSubsystem::Get(Rig.GetWorld());
	if (!TestNotNull(TEXT("The world has a virtual cursor"), Cursor))
	{
		return false;
	}
	TestTrue(TEXT("The virtual cursor is active"), Cursor->IsVirtualCursorActive());
	const FVector2D Moved = Cursor->GetVirtualCursorPosition() - Start;
	// Speed times time with the stick fully over, give or take what the stick's dead zone and the
	// last partial frame take off it. The direction and the order of magnitude are the point.
	const double Expected = UDreamGUISettings::Get()->VirtualCursorSpeed * PushSeconds;
	TestTrue(FString::Printf(TEXT("Pushed right, the cursor moved right by most of %.0f pixels (it moved %.1f)"), Expected, Moved.X),
		Moved.X > Expected * 0.5 && Moved.X <= Expected * 1.05);
	TestNearlyEqual(TEXT("Pushed straight right, it did not move up or down"), Moved.Y, 0.0, 1.0);
	// And the pointer is where the cursor is: the cursor moves the module's substituted pointer, which
	// is the pointer everything downstream reads.
	TestTrue(TEXT("The module's pointer is where the cursor is"),
		Rig.InputModule()->GetVirtualCursor().Equals(Cursor->GetVirtualCursorPosition(), 0.01));

	// Let go, and it stays put.
	const FVector2D Resting = Cursor->GetVirtualCursorPosition();
	Rig.PumpFrames(10);
	TestTrue(TEXT("With the stick released the cursor stays where it was"), Cursor->GetVirtualCursorPosition().Equals(Resting, 0.01));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverGamepadVirtualCursorClickTest,
	"DreamGUI.Driver.Gamepad.TheVirtualCursorsConfirmButtonClicksTheButtonUnderIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverGamepadVirtualCursorClickTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverTouchAndGamepadTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamButton* Button = Rig.MakeControl<UDreamButton>(TEXT("Play"), nullptr, FVector2D(200.0, 80.0));
	if (!TestNotNull(TEXT("A button can be made on the rig"), Button))
	{
		return false;
	}
	TStrongObjectPtr<UDreamTextInteractionListener> Listener(NewObject<UDreamTextInteractionListener>());
	Button->OnClicked.AddDynamic(Listener.Get(), &UDreamTextInteractionListener::HandleClicked);
	Rig.PumpFrames(1);

	// The cursor takes over from wherever the pointer is, so the pointer goes to the button first --
	// then the confirm button, which the cursor delivers as the left mouse button at the cursor.
	const bool bPerformed = Rig.Driver()->Sequence()
		.MoveTo(FDreamBy::Name(TEXT("Play")))
		.ActivateVirtualCursor()
		.VirtualCursorPress()
		.VirtualCursorRelease()
		.Perform();
	TestTrue(TEXT("Pointing the cursor at the button and pressing confirm completes"), bPerformed);
	TestEqual(TEXT("The confirm press and release under the cursor click the button once"), Listener->ClickedCount, 1);
	return true;
}

#endif
