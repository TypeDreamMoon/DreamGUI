// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamWidget.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamPointerEventData.h"
#include "Event/DreamScreenSpaceRaycaster.h"
#include "Interaction/DreamDragDropOperation.h"
#include "Interaction/DreamUIDragDrop.h"
#include "Interaction/UIEventTrigger.h"
#include "DreamPointerEventTestTypes.h"
#include "UObject/StrongObjectPtr.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"

/*
 * The decision that A DRAG HAS STARTED.
 *
 * The drag-drop tests next door drive the framework by calling its interfaces directly --
 * IDreamPointerDragInterface::Execute_OnPointerBeginDrag and friends -- which is the right shape for
 * asking what a source writes and what a target accepts, and which walks straight past the code that
 * decides a drag happened at all: UDreamPointerInputModule::ProcessPointerEvent's trigger state
 * machine and UDreamScreenSpaceRaycaster::ShouldStartDrag. A press that never became a drag, a drag
 * that began on the first pixel of movement, or a drop dispatched to the thing being dragged would
 * all have passed them.
 *
 * These drive that state machine the way a game does, through the headless rig: every pixel is worked
 * out from a widget's transform, the pointer is put there and pressed through the driver's input
 * module, the rig's own screen-space raycaster decides what is under it, and the event system's tick
 * runs the pipeline. Nothing is handed a hit result, so a widget that was never under the pointer can
 * never be told it was pressed. What the pipeline decided -- a press, a drag, a click -- is read from
 * the pointer's own state, or from a production UUIEventTrigger sitting on the widget that is supposed
 * to have been sent the event.
 *
 * Every input step takes one frame, which is why the claims below are made between sequences: a
 * drag's first frame is the frame of the move that crossed the threshold, and the frame after it is
 * its first drag frame.
 */

namespace DreamDragThresholdTestLocal
{
	const FIntPoint ViewportSize(1280, 720);
	const FVector2D CardSize(100.0, 100.0);

	/**
	 * What one widget was actually sent, counted by the production component that exists for exactly
	 * this -- UUIEventTrigger, whose native delegates are reachable from C++. A count on THIS widget
	 * is the observable answer to "who received the drop", which is the whole question when a drag
	 * ends over something other than what is being dragged.
	 */
	struct FWidgetEventLog
	{
		int32 Down = 0;
		int32 Up = 0;
		int32 Click = 0;
		int32 BeginDrag = 0;
		int32 Drag = 0;
		int32 EndDrag = 0;
		int32 DragDrop = 0;

		void Observe(UDreamWidget* Widget)
		{
			UUIEventTrigger* Trigger = Widget->AddComponent<UUIEventTrigger>();
			if (Trigger == nullptr)
			{
				return;
			}
			Trigger->GetOnPointerDownEvent().AddLambda([this](UDreamPointerEventData*) { ++Down; });
			Trigger->GetOnPointerUpEvent().AddLambda([this](UDreamPointerEventData*) { ++Up; });
			Trigger->GetOnPointerClickEvent().AddLambda([this](UDreamPointerEventData*) { ++Click; });
			Trigger->GetOnPointerBeginDragEvent().AddLambda([this](UDreamPointerEventData*) { ++BeginDrag; });
			Trigger->GetOnPointerDragEvent().AddLambda([this](UDreamPointerEventData*) { ++Drag; });
			Trigger->GetOnPointerEndDragEvent().AddLambda([this](UDreamPointerEventData*) { ++EndDrag; });
			Trigger->GetOnPointerDragDropEvent().AddLambda([this](UDreamPointerEventData*) { ++DragDrop; });
		}
	};

	/**
	 * The drag threshold in the units the pointer is measured in.
	 *
	 * Read from the rig's raycaster rather than written as 5: the point is the boundary, not the
	 * number, and a test that hardcodes the default stops testing the day the default moves. Scaled,
	 * because DragThreshold is authored in canvas units and ShouldStartDrag compares viewport pixels
	 * against GetScaledDragThresholdSquare -- the two agree only while the canvas scale is one.
	 */
	double DragThresholdPixels(const FDreamDriverRig& InRig)
	{
		const UDreamScreenSpaceRaycaster* RigRaycaster = InRig.Raycaster();
		return RigRaycaster != nullptr
			? FMath::Sqrt(FMath::Max(static_cast<double>(RigRaycaster->GetScaledDragThresholdSquare()), 0.0))
			: 0.0;
	}

	/** Where a widget's centre lands on the viewport: the pixel the raycaster finds it at again. */
	TOptional<FVector2D> CentrePixelOf(const FDreamDriverRig& InRig, UDreamWidget* InWidget)
	{
		return InRig.Driver()->Find(FDreamBy::Widget(InWidget))->GetCentrePixel();
	}

	/**
	 * Pointer 0's own state -- press, drag, click count -- where every decision below is read from.
	 * Null until the pointer has moved at least once: asking where a pointer is does not bring one
	 * into existence.
	 */
	UDreamPointerEventData* PointerOf(const FDreamDriverRig& InRig)
	{
		return InRig.Context().GetPointerEventData(0);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDragThresholdCrossingStartsTheDragTest,
	"DreamGUI.Input.DragThreshold.APressBecomesADragOnlyOncePastTheThreshold",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDragThresholdCrossingStartsTheDragTest::RunTest(const FString& Parameters)
{
	using namespace DreamDragThresholdTestLocal;

	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}

	UDreamWidget* Card = Rig.MakeWidget(TEXT("Card"), nullptr, CardSize);
	UDreamUIDragSource* Source = Card != nullptr ? Card->AddComponent<UDreamUIDragSource>() : nullptr;
	if (!TestNotNull(TEXT("The card is a drag source"), Source))
	{
		return false;
	}
	Source->Tag = TEXT("Item");
	Source->Payload = Card;

	FWidgetEventLog CardLog;
	CardLog.Observe(Card);
	Rig.PumpFrames(1);

	const double Threshold = DragThresholdPixels(Rig);
	const TOptional<FVector2D> CardCentre = CentrePixelOf(Rig, Card);
	if (!TestTrue(TEXT("The raycaster has a positive drag threshold"), Threshold > 0.0)
		|| !TestTrue(TEXT("The card is somewhere the pointer can reach"), CardCentre.IsSet()))
	{
		return false;
	}
	const FVector2D PressPos = CardCentre.GetValue();
	FDreamDriverRef Driver = Rig.Driver();

	// ---- The trigger goes down over the card.
	TestTrue(TEXT("Pressing on the card completes"), Driver->Sequence().MoveToPixel(PressPos).Press().Perform());
	UDreamPointerEventData* Pointer = PointerOf(Rig);
	if (!TestNotNull(TEXT("The pointer has a state to read"), Pointer))
	{
		return false;
	}

	TestTrue(TEXT("The press landed on the card"), Pointer->PressWidget == Card);
	TestEqual(TEXT("...and the card was sent PointerDown"), CardLog.Down, 1);
	TestFalse(TEXT("A press alone is not a drag"), Pointer->bIsDragging);
	TestTrue(TEXT("...so nothing is being dragged"), Pointer->DragWidget == nullptr);
	TestEqual(TEXT("...and no BeginDrag was sent"), CardLog.BeginDrag, 0);

	// ---- The pointer moves, but stays inside the threshold. This is the frame that decides whether a
	// click can survive a shaky hand.
	TestTrue(TEXT("A move inside the threshold completes"),
		Driver->Sequence().MoveToPixel(PressPos + FVector2D(Threshold * 0.5, 0.0)).Perform());

	TestFalse(TEXT("Movement under the threshold does not start a drag"), Pointer->bIsDragging);
	TestTrue(TEXT("...nothing is being dragged"), Pointer->DragWidget == nullptr);
	TestEqual(TEXT("...and still no BeginDrag"), CardLog.BeginDrag, 0);
	TestTrue(TEXT("...while the press is still held on the card"), Pointer->PressWidget == Card);
	TestTrue(TEXT("...and no drag operation exists yet"), Pointer->DragOperation == nullptr);

	// ---- Past the threshold. The drag starts here and nowhere else.
	TestTrue(TEXT("A move past the threshold completes"),
		Driver->Sequence().MoveToPixel(PressPos + FVector2D(Threshold * 4.0, 0.0)).Perform());

	TestTrue(TEXT("Crossing the threshold starts a drag"), Pointer->bIsDragging);
	TestTrue(TEXT("...and the pressed widget is the one being dragged"), Pointer->DragWidget == Card);
	TestEqual(TEXT("...and the card was sent BeginDrag"), CardLog.BeginDrag, 1);
	TestTrue(TEXT("...while the press itself is untouched"), Pointer->PressWidget == Card);

	// The drag really reached the card's drag source, not merely a flag somewhere: the source is what
	// writes the operation, so an operation carrying its tag is proof of delivery.
	UDreamDragDropOperation* Operation = Pointer->DragOperation.Get();
	if (TestTrue(TEXT("BeginDrag reached the card's drag source"), IsValid(Operation)))
	{
		TestEqual(TEXT("...and the operation carries the source's tag"), Operation->Tag, FName(TEXT("Item")));
		TestTrue(TEXT("...and names the card as the drag's origin"), Operation->SourceWidget.Get() == Card);
	}

	// ---- Still held, still moving. A drag begins once; every later frame is a drag frame -- and this
	// move is exactly one frame after the one that began it.
	TestTrue(TEXT("A further move completes"),
		Driver->Sequence().MoveToPixel(PressPos + FVector2D(Threshold * 8.0, 0.0)).Perform());

	TestEqual(TEXT("BeginDrag fires once, not on every frame past the threshold"), CardLog.BeginDrag, 1);
	TestEqual(TEXT("...and the following frame is a drag frame"), CardLog.Drag, 1);
	TestTrue(TEXT("...still dragging the card"), Pointer->DragWidget == Card);

	// Let go, so the rig is taken down with nothing held rather than with a drag in flight.
	TestTrue(TEXT("Letting go completes"), Driver->Sequence().Release().Perform());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDragThresholdReleaseRoutesTheDropTest,
	"DreamGUI.Input.DragThreshold.ReleasingEndsTheDragAndDropsOnTheWidgetUnderThePointer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDragThresholdReleaseRoutesTheDropTest::RunTest(const FString& Parameters)
{
	using namespace DreamDragThresholdTestLocal;

	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}

	// Well apart, so the drop target is never under the card and the drag crosses real ground.
	UDreamWidget* Card = Rig.MakeWidget(TEXT("Card"), nullptr, CardSize, FVector2D(-200.0, 0.0));
	UDreamWidget* Slot = Rig.MakeWidget(TEXT("Slot"), nullptr, CardSize, FVector2D(200.0, 0.0));

	UDreamUIDragSource* Source = Card != nullptr ? Card->AddComponent<UDreamUIDragSource>() : nullptr;
	UDreamUIDropTarget* Target = Slot != nullptr ? Slot->AddComponent<UDreamUIDropTarget>() : nullptr;
	if (!TestTrue(TEXT("A drag source on the card and a drop target on the slot"),
		Source != nullptr && Target != nullptr))
	{
		return false;
	}
	Source->Tag = TEXT("Item");
	Source->Payload = Card;
	Target->RequiredTag = TEXT("Item");

	FWidgetEventLog CardLog;
	FWidgetEventLog SlotLog;
	CardLog.Observe(Card);
	SlotLog.Observe(Slot);
	Rig.PumpFrames(1);

	const double Threshold = DragThresholdPixels(Rig);
	const TOptional<FVector2D> CardCentre = CentrePixelOf(Rig, Card);
	const TOptional<FVector2D> SlotCentre = CentrePixelOf(Rig, Slot);
	if (!TestTrue(TEXT("The raycaster has a positive drag threshold"), Threshold > 0.0)
		|| !TestTrue(TEXT("The card and the slot are somewhere the pointer can reach"), CardCentre.IsSet() && SlotCentre.IsSet()))
	{
		return false;
	}
	const FVector2D PressPos = CardCentre.GetValue();
	const FVector2D DropPos = SlotCentre.GetValue();
	FDreamDriverRef Driver = Rig.Driver();

	// Press on the card and pull it past the threshold, which is the only way to reach a live drag.
	TestTrue(TEXT("Pressing on the card and pulling it past the threshold completes"),
		Driver->Sequence()
			.MoveToPixel(PressPos)
			.Press()
			.MoveToPixel(PressPos + FVector2D(Threshold * 4.0, 0.0))
			.Perform());
	UDreamPointerEventData* Pointer = PointerOf(Rig);
	if (!TestNotNull(TEXT("The pointer has a state to read"), Pointer)
		|| !TestTrue(TEXT("The card is being dragged before the release"), Pointer->bIsDragging))
	{
		return false;
	}

	// The operation is cleared when the drag ends -- it lives exactly as long as the drag -- so hold it
	// now to read the verdict afterwards.
	TStrongObjectPtr<UDreamDragDropOperation> Operation(Pointer->DragOperation.Get());
	if (!TestTrue(TEXT("The drag carries an operation"), Operation.IsValid()))
	{
		return false;
	}

	// Drag over the slot, rest there a frame, then let go. While a drag is live the line trace hides the
	// dragged widget, so what the ray finds under the pointer is the slot -- found, not handed in.
	TestTrue(TEXT("Carrying the drag over the slot and letting go there completes"),
		Driver->Sequence().MoveToPixel(DropPos).Release().Perform());

	TestFalse(TEXT("Releasing ends the drag"), Pointer->bIsDragging);
	TestTrue(TEXT("...and nothing is being dragged any more"), Pointer->DragWidget == nullptr);
	TestTrue(TEXT("...and the press is released"), Pointer->PressWidget == nullptr);
	TestTrue(TEXT("...and the operation dies with the drag"), Pointer->DragOperation == nullptr);

	// The routing question. The drop goes to what the pointer is over; the end of the drag goes to what
	// was being dragged. Getting these confused is what made the drop unreachable in the ordinary case.
	TestEqual(TEXT("The drop was dispatched to the widget under the pointer"), SlotLog.DragDrop, 1);
	TestEqual(TEXT("...and NOT to the widget being dragged"), CardLog.DragDrop, 0);
	TestEqual(TEXT("EndDrag was dispatched to the dragged widget"), CardLog.EndDrag, 1);
	TestEqual(TEXT("...and not to the drop target"), SlotLog.EndDrag, 0);
	TestEqual(TEXT("PointerUp went to the widget the press started on"), CardLog.Up, 1);
	TestEqual(TEXT("A drag does not end in a click"), CardLog.Click, 0);

	// And the drop target actually took it, which is what the source's end-of-drag reads to tell a
	// landed drag from a cancelled one.
	TestTrue(TEXT("The slot's drop target accepted the operation"), Operation->bDropWasHandled);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDragThresholdMotionlessPressClicksTest,
	"DreamGUI.Input.DragThreshold.APressAndReleaseWithoutMovementClicksAndNeverDrags",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDragThresholdMotionlessPressClicksTest::RunTest(const FString& Parameters)
{
	using namespace DreamDragThresholdTestLocal;

	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}

	UDreamWidget* Card = Rig.MakeWidget(TEXT("Card"), nullptr, CardSize);
	UDreamUIDragSource* Source = Card != nullptr ? Card->AddComponent<UDreamUIDragSource>() : nullptr;
	if (!TestNotNull(TEXT("The card is a drag source"), Source))
	{
		return false;
	}
	Source->Tag = TEXT("Item");

	FWidgetEventLog CardLog;
	CardLog.Observe(Card);
	Rig.PumpFrames(1);

	const TOptional<FVector2D> CardCentre = CentrePixelOf(Rig, Card);
	if (!TestTrue(TEXT("The card is somewhere the pointer can reach"), CardCentre.IsSet()))
	{
		return false;
	}
	const FVector2D PressPos = CardCentre.GetValue();
	FDreamDriverRef Driver = Rig.Driver();

	// The most ordinary interaction there is: down and up in the same place.
	TestTrue(TEXT("Pressing and letting go on the card completes"),
		Driver->Sequence().MoveToPixel(PressPos).Press().Release().Perform());
	UDreamPointerEventData* Pointer = PointerOf(Rig);
	if (!TestNotNull(TEXT("The pointer has a state to read"), Pointer))
	{
		return false;
	}

	TestEqual(TEXT("A motionless press and release is a click"), CardLog.Click, 1);
	TestEqual(TEXT("...preceded by PointerUp"), CardLog.Up, 1);
	TestEqual(TEXT("...and it never became a drag"), CardLog.BeginDrag, 0);
	TestEqual(TEXT("...so nothing ended a drag"), CardLog.EndDrag, 0);
	TestEqual(TEXT("...and nothing was dropped"), CardLog.DragDrop, 0);
	TestFalse(TEXT("...and the pointer is not dragging"), Pointer->bIsDragging);
	TestTrue(TEXT("...with no drag widget"), Pointer->DragWidget == nullptr);
	TestTrue(TEXT("...and no operation was ever created"), Pointer->DragOperation == nullptr);
	TestTrue(TEXT("...and the press is released"), Pointer->PressWidget == nullptr);

	// Moving far afterwards is hovering, not dragging. The threshold is only ever consulted while the
	// trigger is held, and a stale PressPointerPosition sitting a long way from the pointer must not be
	// enough to start one. Four hundred pixels is off the card and still on the viewport.
	TestTrue(TEXT("Moving far away with the trigger up completes"),
		Driver->Sequence().MoveToPixel(PressPos + FVector2D(400.0, 0.0)).Perform());

	TestFalse(TEXT("Moving with the trigger released does not start a drag"), Pointer->bIsDragging);
	TestEqual(TEXT("...and sends no BeginDrag"), CardLog.BeginDrag, 0);
	TestTrue(TEXT("...and presses nothing"), Pointer->PressWidget == nullptr);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPointerDoubleClickTest,
	"DreamGUI.Input.Click.TwoClicksOnTheSameWidgetInTimeAreADoubleClick",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPointerDoubleClickTest::RunTest(const FString& Parameters)
{
	using namespace DreamDragThresholdTestLocal;

	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}

	UDreamWidget* Card = Rig.MakeWidget(TEXT("Card"), nullptr, CardSize, FVector2D(-150.0, 0.0));
	UDreamWidget* Other = Rig.MakeWidget(TEXT("Other"), nullptr, CardSize, FVector2D(150.0, 0.0));
	UDreamDoubleClickCounter* CardCounter = Card != nullptr ? Card->AddComponent<UDreamDoubleClickCounter>() : nullptr;
	UDreamDoubleClickCounter* OtherCounter = Other != nullptr ? Other->AddComponent<UDreamDoubleClickCounter>() : nullptr;
	UDreamEventSystem* Events = Rig.EventSystem();
	if (!TestTrue(TEXT("Both widgets are listening for double clicks"),
		CardCounter != nullptr && OtherCounter != nullptr)
		|| !TestNotNull(TEXT("The rig has an event system, whose clock says how quick a double click is"), Events))
	{
		return false;
	}

	FWidgetEventLog CardLog;
	CardLog.Observe(Card);
	Rig.PumpFrames(1);

	FDreamDriverRef Driver = Rig.Driver();
	// Onto the widget's centre, down, up: three frames, so a second click on the same widget follows the
	// first well inside the window, as a player's double click does.
	auto ClickOn = [&Driver](UDreamWidget* InWidget) -> bool
	{
		return Driver->Sequence().MoveTo(FDreamBy::Widget(InWidget)).Press().Release().Perform();
	};

	// ClickTime has been written on every click since the beginning, and its comment has said "can be
	// used to tell double click" for just as long. Nothing read it, and there was no event to raise.
	TestTrue(TEXT("The first click on the card completes"), ClickOn(Card));
	UDreamPointerEventData* Pointer = PointerOf(Rig);
	if (!TestNotNull(TEXT("The pointer has a state to read"), Pointer))
	{
		return false;
	}
	TestEqual(TEXT("One click is one click"), Pointer->ClickCount, 1);
	TestEqual(TEXT("...and is not a double click"), CardCounter->DoubleClickCount, 0);

	TestTrue(TEXT("The second click on the card completes"), ClickOn(Card));
	TestEqual(TEXT("The second click on the same widget continues the run"), Pointer->ClickCount, 2);
	TestEqual(TEXT("...and is dispatched as a double click"), CardCounter->DoubleClickCount, 1);
	TestEqual(TEXT("...telling the handler which click it was"), CardCounter->LastReportedClickCount, 2);
	// Never instead of the single click: a row that opens on double click usually also selects on
	// single, and making every handler re-implement that would be the cost of swallowing it.
	TestEqual(TEXT("...on top of the ordinary click, not in place of it"), CardLog.Click, 2);

	// A click elsewhere is a new run, not the third of this one.
	TestTrue(TEXT("A click on the other widget completes"), ClickOn(Other));
	TestEqual(TEXT("A click on another widget starts over"), Pointer->ClickCount, 1);
	TestEqual(TEXT("...and is nobody's double click"), OtherCounter->DoubleClickCount, 0);
	TestEqual(TEXT("...least of all the first widget's"), CardCounter->DoubleClickCount, 1);

	// And so is a click that comes too late. The world clock is what the window is measured against, so
	// letting that clock run past DoubleClickTime -- the pump advances it frame by frame -- is exactly
	// what a slow player does.
	TestTrue(TEXT("Back on the card, a click completes"), ClickOn(Card));
	TestEqual(TEXT("Back on the card, a fresh run"), Pointer->ClickCount, 1);
	TestTrue(TEXT("Letting the double-click window pass completes"),
		Driver->Sequence().WaitSeconds(Events->GetDoubleClickTime() + 1.0f).Perform());
	TestTrue(TEXT("A late click on the card completes"), ClickOn(Card));
	TestEqual(TEXT("A click after the window starts a new run rather than completing a pair"),
		Pointer->ClickCount, 1);
	TestEqual(TEXT("...and raises no double click"), CardCounter->DoubleClickCount, 1);

	// Turning the window off is a supported way to say "this game has no double clicks".
	Events->SetDoubleClickTime(0.0f);
	TestTrue(TEXT("With the window closed, a click completes"), ClickOn(Card));
	TestTrue(TEXT("...and so does a second one straight after it"), ClickOn(Card));
	TestEqual(TEXT("With the window closed nothing is ever a double click"), CardCounter->DoubleClickCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPointerLongPressTest,
	"DreamGUI.Input.Click.HoldingOnAWidgetIsALongPressUnlessItBecameADrag",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPointerLongPressTest::RunTest(const FString& Parameters)
{
	using namespace DreamDragThresholdTestLocal;

	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}

	UDreamWidget* Card = Rig.MakeWidget(TEXT("Card"), nullptr, CardSize);
	UDreamLongPressCounter* Counter = Card != nullptr ? Card->AddComponent<UDreamLongPressCounter>() : nullptr;
	UDreamEventSystem* Events = Rig.EventSystem();
	if (!TestNotNull(TEXT("The card is listening for long presses"), Counter)
		|| !TestNotNull(TEXT("The rig has an event system, whose clock says how long a long press is"), Events))
	{
		return false;
	}
	Rig.PumpFrames(1);

	const float LongPressTime = Events->GetLongPressTime();
	const double Threshold = DragThresholdPixels(Rig);
	const TOptional<FVector2D> CardCentre = CentrePixelOf(Rig, Card);
	if (!TestTrue(TEXT("Long press is on by default"), LongPressTime > 0.0f)
		|| !TestTrue(TEXT("The raycaster has a positive drag threshold"), Threshold > 0.0)
		|| !TestTrue(TEXT("The card is somewhere the pointer can reach"), CardCentre.IsSet()))
	{
		return false;
	}
	const FVector2D PressPos = CardCentre.GetValue();
	FDreamDriverRef Driver = Rig.Driver();

	// Down, and held for less than the threshold. The hold is time passing on the world clock, which
	// is the clock the pipeline measures the press against; the pump advances it a frame at a time.
	TestTrue(TEXT("Pressing on the card completes"), Driver->Sequence().MoveToPixel(PressPos).Press().Perform());
	TestEqual(TEXT("A press is not yet a long press"), Counter->LongPressCount, 0);
	TestTrue(TEXT("Holding for half the long-press time completes"),
		Driver->Sequence().WaitSeconds(LongPressTime * 0.5f).Perform());
	TestEqual(TEXT("...halfway through, still not"), Counter->LongPressCount, 0);

	// Past the threshold. It fires while the finger is still down, which is the whole point: a context
	// menu that waits for the release cannot be the thing the player is still holding.
	TestTrue(TEXT("Holding past the long-press time completes"),
		Driver->Sequence().WaitSeconds(LongPressTime).Perform());
	TestEqual(TEXT("Held past the threshold, it fires"), Counter->LongPressCount, 1);
	TestEqual(TEXT("...carrying the pointer that was held"), Counter->LastPointerID, 0);

	TestTrue(TEXT("Holding on for as long again completes"),
		Driver->Sequence().WaitSeconds(LongPressTime).Perform());
	TestEqual(TEXT("...once per press, not once per frame"), Counter->LongPressCount, 1);

	TestTrue(TEXT("Letting go completes"), Driver->Sequence().Release().Perform());

	// A press that turns into a drag is the same gesture read another way, and only one of the two
	// readings may win. The drag does, because the player can see a drag happening.
	TestTrue(TEXT("Pressing again and pulling past the drag threshold completes"),
		Driver->Sequence()
			.Press()
			.MoveToPixel(PressPos + FVector2D(Threshold * 4.0, 0.0))
			.Perform());
	UDreamPointerEventData* Pointer = PointerOf(Rig);
	if (!TestNotNull(TEXT("The pointer has a state to read"), Pointer)
		|| !TestTrue(TEXT("The second press became a drag"), Pointer->bIsDragging))
	{
		return false;
	}
	TestTrue(TEXT("Holding the drag for three long-press times completes"),
		Driver->Sequence().WaitSeconds(LongPressTime * 3.0f).Perform());
	TestEqual(TEXT("A drag being held is not a long press"), Counter->LongPressCount, 1);

	TestTrue(TEXT("Letting go of the drag completes"), Driver->Sequence().Release().Perform());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPointerSwipeGestureTest,
	"DreamGUI.Input.Gesture.AFastTravelAcrossAWidgetIsASwipe",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPointerSwipeGestureTest::RunTest(const FString& Parameters)
{
	using namespace DreamDragThresholdTestLocal;

	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}

	// Up and to the left, so a swipe of three minimum distances rightward or downward still ends on
	// the viewport.
	UDreamWidget* Card = Rig.MakeWidget(TEXT("Card"), nullptr, CardSize, FVector2D(-300.0, 150.0));
	UDreamGestureCounter* Counter = Card != nullptr ? Card->AddComponent<UDreamGestureCounter>() : nullptr;
	UDreamEventSystem* Events = Rig.EventSystem();
	if (!TestNotNull(TEXT("The card is listening for gestures"), Counter)
		|| !TestNotNull(TEXT("The rig has an event system, whose settings say what a swipe is"), Events))
	{
		return false;
	}
	Rig.PumpFrames(1);

	const float MinDistance = Events->GetSwipeMinDistance();
	const TOptional<FVector2D> CardCentre = CentrePixelOf(Rig, Card);
	if (!TestTrue(TEXT("Swipes are on by default"), MinDistance > 0.0f && Events->GetSwipeMaxDuration() > 0.0f)
		|| !TestTrue(TEXT("The card is somewhere the pointer can reach"), CardCentre.IsSet()))
	{
		return false;
	}
	const FVector2D PressPos = CardCentre.GetValue();
	const FVector2D EndPos = PressPos + FVector2D(MinDistance * 3.0, 0.0);
	const FVector2D DownPos = PressPos + FVector2D(0.0, MinDistance * 3.0);
	if (!TestTrue(TEXT("Both long swipes end on the viewport"),
		EndPos.X < ViewportSize.X && DownPos.Y < ViewportSize.Y))
	{
		return false;
	}
	FDreamDriverRef Driver = Rig.Driver();

	// Each gesture is down, one move, up, a frame each: a thirtieth of a second from press to release,
	// well inside the swipe's time limit. The move is its own frame, so the long ones are also drags by
	// the time they are let go -- which is what a real flick across a widget is, and the swipe is
	// reported either way.

	// A short movement is not a swipe, however fast.
	TestTrue(TEXT("A short, quick travel completes"),
		Driver->Sequence()
			.MoveToPixel(PressPos)
			.Press()
			.MoveToPixel(PressPos + FVector2D(MinDistance * 0.25, 0.0))
			.Release()
			.Perform());
	TestEqual(TEXT("A short travel is not a swipe"), Counter->SwipeCount, 0);

	// Far enough, fast enough, to the right.
	TestTrue(TEXT("A long, quick travel to the right completes"),
		Driver->Sequence().MoveToPixel(PressPos).Press().MoveToPixel(EndPos).Release().Perform());
	TestEqual(TEXT("A long, fast travel is a swipe"), Counter->SwipeCount, 1);
	TestEqual(TEXT("...snapped to the dominant axis"), Counter->LastSwipeDirection, EDreamUINavigationDirection::Right);
	TestTrue(TEXT("...carrying how far it went"), Counter->LastSwipeDelta.X >= MinDistance);

	// Downward, to prove the axis and the sign are both read. Viewport Y grows downward.
	TestTrue(TEXT("A long, quick travel downward completes"),
		Driver->Sequence().MoveToPixel(PressPos).Press().MoveToPixel(DownPos).Release().Perform());
	TestEqual(TEXT("A downward travel swipes down"), Counter->LastSwipeDirection, EDreamUINavigationDirection::Down);

	// The same movement taken slowly is a drag, not a swipe. This is the only thing separating them.
	TestTrue(TEXT("The same long travel, held past the swipe's time limit first, completes"),
		Driver->Sequence()
			.MoveToPixel(PressPos)
			.Press()
			.WaitSeconds(Events->GetSwipeMaxDuration() + 1.0f)
			.MoveToPixel(EndPos)
			.Release()
			.Perform());
	TestEqual(TEXT("A slow travel is a drag, not a swipe"), Counter->SwipeCount, 2);
	return true;
}

#endif
