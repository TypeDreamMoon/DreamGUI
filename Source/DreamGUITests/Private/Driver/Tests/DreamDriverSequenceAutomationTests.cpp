// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamWidget.h"
#include "Engine/World.h"
#include "Event/DreamPointerEventData.h"
#include "Interaction/DreamUIDragDrop.h"
#include "Interaction/UIEventTrigger.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Driver/DreamDriverUntil.h"

/*
 * THE PUMP ITSELF: does a frame happen, and does a failure say what failed.
 *
 * A pump that quietly did nothing would let every other driver test pass for the wrong reason -- the
 * gestures would be queued, never drained, and every count would be zero, which reads exactly like a
 * feature that does not work. So the frame has to be observable, and the clock is what makes it so:
 * the pipeline reads the WORLD's time for long presses, click runs and navigation repeat, and a pump
 * that did not advance it would be a pump that repeated one instant.
 *
 * The other half is the failure path. A driver that gave up silently would turn a broken test into a
 * passing one, so a step that cannot finish reports through the running test, and what it reports has
 * to name the thing that never happened.
 */
namespace DreamDriverSequenceTestLocal
{
	const FIntPoint ViewportSize(1280, 720);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverWaitFramesTest,
	"DreamGUI.Driver.Sequence.WaitFramesAdvancesTheWorldClockByThatManyFrames",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverWaitFramesTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverSequenceTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}

	const double FrameSeconds = Rig.Context().FrameSeconds;
	const double BeforeWait = Rig.GetWorld()->TimeSeconds;

	TestTrue(TEXT("A sequence of nothing but waiting completes"),
		Rig.Driver()->Sequence().WaitFrames(5).Perform());
	TestEqual(TEXT("Five frames of waiting move the world clock by five frames"),
		Rig.GetWorld()->TimeSeconds - BeforeWait, FrameSeconds * 5.0, FrameSeconds * 0.5);

	// A Then is a reading, not an act, and readings do not cost frames -- otherwise every assertion
	// in a sequence would be made one frame later than the step it is about.
	const double BeforeThen = Rig.GetWorld()->TimeSeconds;
	bool bThenRan = false;
	TestTrue(TEXT("A sequence of nothing but a Then completes"),
		Rig.Driver()->Sequence().Then([&bThenRan](FDreamDriverContext&) { bThenRan = true; }).Perform());
	TestTrue(TEXT("The Then ran"), bThenRan);
	TestEqual(TEXT("A Then costs no frame"), Rig.GetWorld()->TimeSeconds, BeforeThen, FrameSeconds * 0.5);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverThenSeesThePreviousStepTest,
	"DreamGUI.Driver.Sequence.ThenSeesWhatTheStepBeforeItDelivered",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverThenSeesThePreviousStepTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverSequenceTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}

	UDreamWidget* Button = Rig.MakeWidget(TEXT("Play"), nullptr, FVector2D(200.0, 100.0));
	UUIEventTrigger* Trigger = Button != nullptr ? Button->AddComponent<UUIEventTrigger>() : nullptr;
	if (!TestNotNull(TEXT("The button can be observed"), Trigger))
	{
		return false;
	}
	int32 ClickCount = 0;
	Trigger->GetOnPointerClickEvent().AddLambda([&ClickCount](UDreamPointerEventData*) { ++ClickCount; });
	Rig.PumpFrames(1);

	int32 ClicksSeenByTheFirstThen = -1;
	int32 ClicksSeenByTheSecondThen = -1;
	const bool bPerformed = Rig.Driver()->Sequence()
		.Then([&ClicksSeenByTheFirstThen, &ClickCount](FDreamDriverContext&) { ClicksSeenByTheFirstThen = ClickCount; })
		.Click(FDreamBy::Name(TEXT("Play")))
		.Then([&ClicksSeenByTheSecondThen, &ClickCount](FDreamDriverContext&) { ClicksSeenByTheSecondThen = ClickCount; })
		.Perform();

	TestTrue(TEXT("The sequence completes"), bPerformed);
	TestEqual(TEXT("The Then before the click saw no click"), ClicksSeenByTheFirstThen, 0);
	TestEqual(TEXT("The Then after the click saw it"), ClicksSeenByTheSecondThen, 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverWaitPassesTest,
	"DreamGUI.Driver.Sequence.AWaitPassesAsSoonAsWhatItIsWaitingForHappens",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverWaitPassesTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverSequenceTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}

	Rig.MakeWidget(TEXT("Card"), nullptr, FVector2D(200.0, 100.0));
	Rig.PumpFrames(1);

	FDreamDriverRef Driver = Rig.Driver();
	FDreamElementRef Card = Driver->Find(FDreamBy::Name(TEXT("Card")));

	// Nothing has moved the pointer yet, so this is a claim about the wait and not about the setup.
	TestFalse(TEXT("The card starts out unhovered"), Card->IsHovered());

	const bool bReached = Driver->Sequence()
		.MoveTo(FDreamBy::Name(TEXT("Card")))
		.Wait(FDreamUntil::ElementIsHovered(Card, FWaitTimeout::InSeconds(1.0)),
			FWaitTimeout::InSeconds(2.0), TEXT("the card becoming hovered"))
		.Perform();

	TestTrue(TEXT("The wait passes once the pointer has arrived"), bReached);
	TestTrue(TEXT("And the card really is hovered"), Card->IsHovered());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverWaitTimesOutTest,
	"DreamGUI.Driver.Sequence.AWaitThatNeverPassesFailsAndNamesWhatItWasWaitingFor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverWaitTimesOutTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverSequenceTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}

	// The message is the point of this test, so it is spelled out rather than matched loosely: a
	// timeout that reported only a number of seconds would tell a reader nothing about what never
	// happened, which is the only thing they need.
	AddExpectedErrorPlain(TEXT("the counter reaching three did not happen"));

	int32 Counter = 0;
	const bool bPassed = Rig.Driver()->Wait(
		FDreamUntil::Condition([&Counter]() { return Counter >= 3; }, FWaitTimeout::InSeconds(0.1)),
		FWaitTimeout::InSeconds(2.0),
		TEXT("the counter reaching three"));

	TestFalse(TEXT("A wait for something that never happens does not pass"), bPassed);
	// It gave up on its own rather than hanging, which is what the frame budget and the timeout are
	// between them for.
	TestEqual(TEXT("And nothing made the condition true behind its back"), Counter, 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverFailStopsTest,
	"DreamGUI.Driver.Sequence.AFailedStepStopsTheStepsAfterItFromRunning",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverFailStopsTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverSequenceTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}

	AddExpectedErrorPlain(TEXT("this branch must not be reached"));

	bool bBeforeRan = false;
	bool bAfterRan = false;
	const bool bPerformed = Rig.Driver()->Sequence()
		.Then([&bBeforeRan](FDreamDriverContext&) { bBeforeRan = true; })
		.Fail(TEXT("this branch must not be reached"))
		.Then([&bAfterRan](FDreamDriverContext&) { bAfterRan = true; })
		.Perform();

	TestFalse(TEXT("A sequence containing a failure does not report success"), bPerformed);
	TestTrue(TEXT("The steps before the failure ran"), bBeforeRan);
	// A sequence that carried on past a failure would go on driving input against a state nobody
	// expected, and the assertions after it would be about that state rather than the one under test.
	TestFalse(TEXT("The steps after the failure did not"), bAfterRan);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverMissingLocatorFailsTest,
	"DreamGUI.Driver.Sequence.AimingAtAWidgetThatIsNotThereFailsRatherThanClickingNowhere",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverMissingLocatorFailsTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverSequenceTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}

	AddExpectedErrorPlain(TEXT("could not work out a viewport pixel"));

	bool bAfterRan = false;
	// Moving to a widget that is not there has no pixel to move to. Moving to pixel zero instead --
	// or doing nothing and calling it done -- would let the click land on whatever is at the corner
	// of the screen, and the test would fail somewhere else entirely.
	const bool bPerformed = Rig.Driver()->Sequence()
		.MoveTo(FDreamBy::Name(TEXT("NotHere")))
		.Then([&bAfterRan](FDreamDriverContext&) { bAfterRan = true; })
		.Perform();

	TestFalse(TEXT("A move to nothing does not report success"), bPerformed);
	TestFalse(TEXT("And the rest of the sequence did not run"), bAfterRan);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverPumpTicksTheDragDropSubsystemTest,
	"DreamGUI.Driver.Sequence.APumpedFrameTicksTheDragDropSubsystemSoItFollowsADrag",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverPumpTicksTheDragDropSubsystemTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverSequenceTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	// The drag-drop subsystem finds its event system through the UI manager's registration -- what a
	// game world gives it at BeginPlay, and what this asks the rig for.
	Rig.EnsureGameInputHost();
	UDreamWidget* Card = Rig.MakeWidget(TEXT("Card"), nullptr, FVector2D(160.0, 120.0), FVector2D(-200.0, 0.0));
	UDreamUIDragDropSubsystem* DragDrop = UDreamUIDragDropSubsystem::Get(Rig.GetWorld());
	if (!TestNotNull(TEXT("The card was built"), Card) || !TestNotNull(TEXT("A game world has a drag-drop subsystem"), DragDrop))
	{
		return false;
	}
	// A source that puts an operation on the pointer when a drag begins -- the only kind of drag the
	// subsystem follows; a drag with no operation is pure geometry to it, a scroll.
	Card->AddComponent<UDreamUIDragSource>();

	// Three frames with nothing happening in them. All a frame does for this subsystem is TICK it,
	// and a tick is the only thing that subscribes it to the event system's input: without the pump
	// ticking it, it would sit unsubscribed however many frames passed and never see the drag below.
	Rig.PumpFrames(3);

	int32 DragsFollowedMidDrag = -1;
	bool bOperationSeenOnThePointer = false;
	const bool bPerformed = Rig.Driver()->Sequence()
		.MoveTo(FDreamBy::Name(TEXT("Card")))
		.Press()
		// Forty pixels is well past the drag threshold, so the drag begins on this move.
		.MoveBy(FVector2D(40.0, 0.0))
		.MoveBy(FVector2D(40.0, 0.0))
		.Then([&DragsFollowedMidDrag, &bOperationSeenOnThePointer, DragDrop](FDreamDriverContext&)
		{
			DragsFollowedMidDrag = DragDrop->GetDragCount();
			bOperationSeenOnThePointer = DragDrop->GetDragOperationForPointer(0) != nullptr;
		})
		.Release()
		.Perform();

	TestTrue(TEXT("The drag completes"), bPerformed);
	TestEqual(TEXT("The subsystem was following the drag while it was in the air"), DragsFollowedMidDrag, 1);
	TestTrue(TEXT("And held the operation the source put on the pointer"), bOperationSeenOnThePointer);

	return true;
}

#endif
