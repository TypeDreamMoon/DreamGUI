// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamWidget.h"
#include "Engine/World.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamPointerEventData.h"
#include "Event/DreamScreenSpaceRaycaster.h"
#include "Event/InputModule/DreamPointerInputModule.h"
#include "Event/InputModule/DreamStandaloneInputModule.h"
#include "GameFramework/Actor.h"
#include "Interaction/DreamDragDropOperation.h"
#include "Interaction/DreamUIDragDrop.h"
#include "Interaction/UIEventTrigger.h"
#include "DreamPointerEventTestTypes.h"
#include "UObject/StrongObjectPtr.h"
#include "DreamScopedWorld.h"

/*
 * The decision that A DRAG HAS STARTED.
 *
 * The drag-drop tests next door drive the framework by calling its interfaces directly --
 * IDreamPointerDragInterface::Execute_OnPointerBeginDrag and friends -- which is the right shape for
 * asking what a source writes and what a target accepts, and which walks straight past the code that
 * decides a drag happened at all. UDreamPointerInputModule::ProcessPointerEvent's trigger state
 * machine and UDreamScreenSpaceRaycaster::ShouldStartDrag have therefore never been executed by the
 * suite: a press that never became a drag, a drag that began on the first pixel of movement, or a
 * drop dispatched to the thing being dragged would all have passed it.
 *
 * ProcessPointerEvent is static and takes its hit result by parameter, so the whole state machine
 * can be driven frame by frame with the raycast answered by hand -- no viewport, no mouse, no
 * raycast, nothing that needs an RHI. What is NOT hand-made is anything the assertions depend on:
 * the pipeline dispatches through the real event system into real behaviours, and every claim below
 * is read from the pointer's own state or from a production UUIEventTrigger sitting on the widget
 * that is supposed to have been sent the event.
 */

namespace DreamDragThresholdTestLocal
{
	using DreamTests::FScopedGameWorld;

	UDreamWidget* MakeWidget(UWorld* World, UDreamWidget* Parent, const TCHAR* Name, float W = 100.0f, float H = 100.0f)
	{
		UDreamWidget* Widget = NewObject<UDreamWidget>(World, NAME_None, RF_Public | RF_Transactional);
		Widget->SetDisplayName(Name);
		Widget->SetWidth(W);
		Widget->SetHeight(H);
		Widget->OnRegister();
		if (Parent)
		{
			Widget->TrySetParent(Parent, false);
		}
		return Widget;
	}

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
	 * Everything ProcessPointerEvent reads that a real level would have supplied: an event system to
	 * dispatch through and to own the pointer's event data (the click path reads EventData->GetWorld(),
	 * which resolves through that outer), a standalone module for the real pointer-move entry point,
	 * and a screen-space raycaster to answer ShouldStartDrag.
	 *
	 * The raycaster is deliberately left unregistered. ShouldStartDrag reads only the event data's two
	 * pointer positions and its own DragThresholdSquare -- which the constructor fills from
	 * DragThreshold, so it is correct without BeginPlay -- and it needs no canvas; registering it would
	 * only enrol it in the UI manager's raycaster list, which serves the line trace this test replaces.
	 */
	struct FPointerRig
	{
		AActor* Host = nullptr;
		UDreamEventSystem* EventSystem = nullptr;
		UDreamStandaloneInputModule* Module = nullptr;
		UDreamScreenSpaceRaycaster* Raycaster = nullptr;
		UDreamPointerEventData* EventData = nullptr;

		explicit FPointerRig(UWorld* InWorld)
		{
			Host = InWorld->SpawnActor<AActor>();
			EventSystem = NewObject<UDreamEventSystem>(Host);
			EventSystem->RegisterComponent();
			Module = NewObject<UDreamStandaloneInputModule>(Host);
			Module->RegisterComponent();
			Module->RegisterInputModuleToEventSystem(EventSystem);
			Raycaster = NewObject<UDreamScreenSpaceRaycaster>(Host);
			EventData = EventSystem->GetPointerEventData(0, true);
		}

		bool IsUsable() const
		{
			return Host != nullptr && EventSystem != nullptr && Module != nullptr
				&& Raycaster != nullptr && EventData != nullptr;
		}

		/**
		 * The trigger going down. Mirrors what UDreamStandaloneInputModule::ProcessInput writes when it
		 * drains a queued press -- position, the now-pressed bit, the press time and the press position
		 * the threshold is later measured from. The queue itself is private to the module, so the four
		 * writes are reproduced here rather than reached for.
		 */
		void Press(const FVector2D& Position)
		{
			Module->InputMouseMove(FVector(Position.X, Position.Y, 0.0));
			EventData->bNowIsTriggerPressed = true;
			EventData->PressTime = Host->GetWorld()->TimeSeconds;
			EventData->PressPointerPosition = FVector(Position.X, Position.Y, 0.0);
		}

		/**
		 * The pointer moving. This is the real entry point, and it deliberately leaves
		 * PressPointerPosition alone -- that is what makes the threshold a distance from the press.
		 */
		void MoveTo(const FVector2D& Position)
		{
			Module->InputMouseMove(FVector(Position.X, Position.Y, 0.0));
		}

		/** The trigger coming up, again mirroring the module's drain. */
		void Release(const FVector2D& Position)
		{
			Module->InputMouseMove(FVector(Position.X, Position.Y, 0.0));
			EventData->bNowIsTriggerPressed = false;
			EventData->ReleaseTime = Host->GetWorld()->TimeSeconds;
		}

		/** One pipeline frame, with the line trace's answer handed in instead of traced. */
		void Frame(UDreamWidget* WidgetUnderPointer)
		{
			FDreamUIHitResultContainer HitContainer;
			HitContainer.Raycaster = Raycaster;
			if (WidgetUnderPointer != nullptr)
			{
				HitContainer.HitResult.Widget = WidgetUnderPointer;
				HitContainer.HoverArray.Add(WidgetUnderPointer);
			}
			bool bOutIsHitSomething = false;
			FDreamUIHitResult OutHitResult;
			UDreamPointerInputModule::ProcessPointerEvent(
				EventSystem, EventData, WidgetUnderPointer != nullptr,
				HitContainer, bOutIsHitSomething, OutHitResult);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDragThresholdCrossingStartsTheDragTest,
	"DreamGUI.Input.DragThreshold.APressBecomesADragOnlyOncePastTheThreshold",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDragThresholdCrossingStartsTheDragTest::RunTest(const FString& Parameters)
{
	using namespace DreamDragThresholdTestLocal;

	FScopedGameWorld Scope;
	if (!TestTrue(TEXT("A world to host the rig"), Scope.World != nullptr))
	{
		return false;
	}
	FPointerRig Rig(Scope.World);
	if (!TestTrue(TEXT("The pointer rig came up"), Rig.IsUsable()))
	{
		return false;
	}

	UDreamWidget* Root = MakeWidget(Scope.World, nullptr, TEXT("Root"), 800.0f, 600.0f);
	UDreamWidget* Card = MakeWidget(Scope.World, Root, TEXT("Card"));
	UDreamUIDragSource* Source = Card->AddComponent<UDreamUIDragSource>();
	if (!TestNotNull(TEXT("The card is a drag source"), Source))
	{
		return false;
	}
	Source->Tag = TEXT("Item");
	Source->Payload = Card;

	FWidgetEventLog CardLog;
	CardLog.Observe(Card);

	// Read from the raycaster rather than written as 5: the point is the boundary, not the number, and
	// a test that hardcodes the default stops testing the day the default moves.
	const double Threshold = Rig.Raycaster->GetDragThreshold();
	if (!TestTrue(TEXT("The raycaster has a positive drag threshold"), Threshold > 0.0))
	{
		return false;
	}
	const FVector2D PressPos(200.0, 200.0);

	// ---- Frame 1: the trigger goes down over the card.
	Rig.Press(PressPos);
	Rig.Frame(Card);

	TestTrue(TEXT("The press landed on the card"), Rig.EventData->PressWidget == Card);
	TestEqual(TEXT("...and the card was sent PointerDown"), CardLog.Down, 1);
	TestFalse(TEXT("A press alone is not a drag"), Rig.EventData->bIsDragging);
	TestTrue(TEXT("...so nothing is being dragged"), Rig.EventData->DragWidget == nullptr);
	TestEqual(TEXT("...and no BeginDrag was sent"), CardLog.BeginDrag, 0);

	// ---- Frame 2: the pointer moves, but stays inside the threshold. This is the frame that decides
	// whether a click can survive a shaky hand.
	Rig.MoveTo(PressPos + FVector2D(Threshold * 0.5, 0.0));
	Rig.Frame(Card);

	TestFalse(TEXT("Movement under the threshold does not start a drag"), Rig.EventData->bIsDragging);
	TestTrue(TEXT("...nothing is being dragged"), Rig.EventData->DragWidget == nullptr);
	TestEqual(TEXT("...and still no BeginDrag"), CardLog.BeginDrag, 0);
	TestTrue(TEXT("...while the press is still held on the card"), Rig.EventData->PressWidget == Card);
	TestTrue(TEXT("...and no drag operation exists yet"), Rig.EventData->DragOperation == nullptr);

	// ---- Frame 3: past the threshold. The drag starts here and nowhere else.
	Rig.MoveTo(PressPos + FVector2D(Threshold * 4.0, 0.0));
	Rig.Frame(Card);

	TestTrue(TEXT("Crossing the threshold starts a drag"), Rig.EventData->bIsDragging);
	TestTrue(TEXT("...and the pressed widget is the one being dragged"), Rig.EventData->DragWidget == Card);
	TestEqual(TEXT("...and the card was sent BeginDrag"), CardLog.BeginDrag, 1);
	TestTrue(TEXT("...while the press itself is untouched"), Rig.EventData->PressWidget == Card);

	// The drag really reached the card's drag source, not merely a flag somewhere: the source is what
	// writes the operation, so an operation carrying its tag is proof of delivery.
	UDreamDragDropOperation* Operation = Rig.EventData->DragOperation.Get();
	if (TestTrue(TEXT("BeginDrag reached the card's drag source"), IsValid(Operation)))
	{
		TestEqual(TEXT("...and the operation carries the source's tag"), Operation->Tag, FName(TEXT("Item")));
		TestTrue(TEXT("...and names the card as the drag's origin"), Operation->SourceWidget.Get() == Card);
	}

	// ---- Frame 4: still held, still moving. A drag begins once; every later frame is a drag frame.
	Rig.MoveTo(PressPos + FVector2D(Threshold * 8.0, 0.0));
	Rig.Frame(Card);

	TestEqual(TEXT("BeginDrag fires once, not on every frame past the threshold"), CardLog.BeginDrag, 1);
	TestEqual(TEXT("...and the following frame is a drag frame"), CardLog.Drag, 1);
	TestTrue(TEXT("...still dragging the card"), Rig.EventData->DragWidget == Card);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDragThresholdReleaseRoutesTheDropTest,
	"DreamGUI.Input.DragThreshold.ReleasingEndsTheDragAndDropsOnTheWidgetUnderThePointer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDragThresholdReleaseRoutesTheDropTest::RunTest(const FString& Parameters)
{
	using namespace DreamDragThresholdTestLocal;

	FScopedGameWorld Scope;
	if (!TestTrue(TEXT("A world to host the rig"), Scope.World != nullptr))
	{
		return false;
	}
	FPointerRig Rig(Scope.World);
	if (!TestTrue(TEXT("The pointer rig came up"), Rig.IsUsable()))
	{
		return false;
	}

	UDreamWidget* Root = MakeWidget(Scope.World, nullptr, TEXT("Root"), 800.0f, 600.0f);
	UDreamWidget* Card = MakeWidget(Scope.World, Root, TEXT("Card"));
	UDreamWidget* Slot = MakeWidget(Scope.World, Root, TEXT("Slot"));

	UDreamUIDragSource* Source = Card->AddComponent<UDreamUIDragSource>();
	UDreamUIDropTarget* Target = Slot->AddComponent<UDreamUIDropTarget>();
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

	const double Threshold = Rig.Raycaster->GetDragThreshold();
	if (!TestTrue(TEXT("The raycaster has a positive drag threshold"), Threshold > 0.0))
	{
		return false;
	}
	const FVector2D PressPos(200.0, 200.0);
	const FVector2D DropPos(500.0, 200.0);

	// Press on the card and pull it past the threshold, which is the only way to reach a live drag.
	Rig.Press(PressPos);
	Rig.Frame(Card);
	Rig.MoveTo(PressPos + FVector2D(Threshold * 4.0, 0.0));
	Rig.Frame(Card);
	if (!TestTrue(TEXT("The card is being dragged before the release"), Rig.EventData->bIsDragging))
	{
		return false;
	}

	// The operation is cleared when the drag ends -- it lives exactly as long as the drag -- so hold it
	// now to read the verdict afterwards.
	TStrongObjectPtr<UDreamDragDropOperation> Operation(Rig.EventData->DragOperation.Get());
	if (!TestTrue(TEXT("The drag carries an operation"), Operation.IsValid()))
	{
		return false;
	}

	// Drag over the slot, then let go there. While a drag is live the line trace hides the dragged
	// widget, so what the pointer is over is the slot underneath -- which is what is handed in here.
	Rig.MoveTo(DropPos);
	Rig.Frame(Slot);
	Rig.Release(DropPos);
	Rig.Frame(Slot);

	TestFalse(TEXT("Releasing ends the drag"), Rig.EventData->bIsDragging);
	TestTrue(TEXT("...and nothing is being dragged any more"), Rig.EventData->DragWidget == nullptr);
	TestTrue(TEXT("...and the press is released"), Rig.EventData->PressWidget == nullptr);
	TestTrue(TEXT("...and the operation dies with the drag"), Rig.EventData->DragOperation == nullptr);

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

	FScopedGameWorld Scope;
	if (!TestTrue(TEXT("A world to host the rig"), Scope.World != nullptr))
	{
		return false;
	}
	FPointerRig Rig(Scope.World);
	if (!TestTrue(TEXT("The pointer rig came up"), Rig.IsUsable()))
	{
		return false;
	}

	UDreamWidget* Root = MakeWidget(Scope.World, nullptr, TEXT("Root"), 800.0f, 600.0f);
	UDreamWidget* Card = MakeWidget(Scope.World, Root, TEXT("Card"));
	UDreamUIDragSource* Source = Card->AddComponent<UDreamUIDragSource>();
	if (!TestNotNull(TEXT("The card is a drag source"), Source))
	{
		return false;
	}
	Source->Tag = TEXT("Item");

	FWidgetEventLog CardLog;
	CardLog.Observe(Card);

	const FVector2D PressPos(200.0, 200.0);

	// The most ordinary interaction there is: down and up in the same place.
	Rig.Press(PressPos);
	Rig.Frame(Card);
	Rig.Release(PressPos);
	Rig.Frame(Card);

	TestEqual(TEXT("A motionless press and release is a click"), CardLog.Click, 1);
	TestEqual(TEXT("...preceded by PointerUp"), CardLog.Up, 1);
	TestEqual(TEXT("...and it never became a drag"), CardLog.BeginDrag, 0);
	TestEqual(TEXT("...so nothing ended a drag"), CardLog.EndDrag, 0);
	TestEqual(TEXT("...and nothing was dropped"), CardLog.DragDrop, 0);
	TestFalse(TEXT("...and the pointer is not dragging"), Rig.EventData->bIsDragging);
	TestTrue(TEXT("...with no drag widget"), Rig.EventData->DragWidget == nullptr);
	TestTrue(TEXT("...and no operation was ever created"), Rig.EventData->DragOperation == nullptr);
	TestTrue(TEXT("...and the press is released"), Rig.EventData->PressWidget == nullptr);

	// Moving far afterwards is hovering, not dragging. The threshold is only ever consulted while the
	// trigger is held, and a stale PressPointerPosition sitting a long way from the pointer must not be
	// enough to start one.
	Rig.MoveTo(PressPos + FVector2D(400.0, 0.0));
	Rig.Frame(Card);

	TestFalse(TEXT("Moving with the trigger released does not start a drag"), Rig.EventData->bIsDragging);
	TestEqual(TEXT("...and sends no BeginDrag"), CardLog.BeginDrag, 0);
	TestTrue(TEXT("...and presses nothing"), Rig.EventData->PressWidget == nullptr);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPointerDoubleClickTest,
	"DreamGUI.Input.Click.TwoClicksOnTheSameWidgetInTimeAreADoubleClick",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPointerDoubleClickTest::RunTest(const FString& Parameters)
{
	using namespace DreamDragThresholdTestLocal;

	FScopedGameWorld Scope;
	if (!TestTrue(TEXT("A world to host the rig"), Scope.World != nullptr))
	{
		return false;
	}
	FPointerRig Rig(Scope.World);
	if (!TestTrue(TEXT("The pointer rig came up"), Rig.IsUsable()))
	{
		return false;
	}

	UDreamWidget* Root = MakeWidget(Scope.World, nullptr, TEXT("Root"), 800.0f, 600.0f);
	UDreamWidget* Card = MakeWidget(Scope.World, Root, TEXT("Card"));
	UDreamWidget* Other = MakeWidget(Scope.World, Root, TEXT("Other"));
	UDreamDoubleClickCounter* CardCounter = Card->AddComponent<UDreamDoubleClickCounter>();
	UDreamDoubleClickCounter* OtherCounter = Other->AddComponent<UDreamDoubleClickCounter>();
	if (!TestTrue(TEXT("Both widgets are listening for double clicks"),
		CardCounter != nullptr && OtherCounter != nullptr))
	{
		return false;
	}

	FWidgetEventLog CardLog;
	CardLog.Observe(Card);

	const FVector2D Pos(200.0, 200.0);
	auto ClickOn = [&Rig, &Pos](UDreamWidget* Widget)
	{
		Rig.Press(Pos);
		Rig.Frame(Widget);
		Rig.Release(Pos);
		Rig.Frame(Widget);
	};

	// ClickTime has been written on every click since the beginning, and its comment has said "can be
	// used to tell double click" for just as long. Nothing read it, and there was no event to raise.
	ClickOn(Card);
	TestEqual(TEXT("One click is one click"), Rig.EventData->ClickCount, 1);
	TestEqual(TEXT("...and is not a double click"), CardCounter->DoubleClickCount, 0);

	ClickOn(Card);
	TestEqual(TEXT("The second click on the same widget continues the run"), Rig.EventData->ClickCount, 2);
	TestEqual(TEXT("...and is dispatched as a double click"), CardCounter->DoubleClickCount, 1);
	TestEqual(TEXT("...telling the handler which click it was"), CardCounter->LastReportedClickCount, 2);
	// Never instead of the single click: a row that opens on double click usually also selects on
	// single, and making every handler re-implement that would be the cost of swallowing it.
	TestEqual(TEXT("...on top of the ordinary click, not in place of it"), CardLog.Click, 2);

	// A click elsewhere is a new run, not the third of this one.
	ClickOn(Other);
	TestEqual(TEXT("A click on another widget starts over"), Rig.EventData->ClickCount, 1);
	TestEqual(TEXT("...and is nobody's double click"), OtherCounter->DoubleClickCount, 0);
	TestEqual(TEXT("...least of all the first widget's"), CardCounter->DoubleClickCount, 1);

	// And so is a click that comes too late. The world clock is what the window is measured against, so
	// moving it past DoubleClickTime is exactly what a slow player does.
	ClickOn(Card);
	TestEqual(TEXT("Back on the card, a fresh run"), Rig.EventData->ClickCount, 1);
	Scope.World->TimeSeconds += (double)Rig.EventSystem->GetDoubleClickTime() + 1.0;
	ClickOn(Card);
	TestEqual(TEXT("A click after the window starts a new run rather than completing a pair"),
		Rig.EventData->ClickCount, 1);
	TestEqual(TEXT("...and raises no double click"), CardCounter->DoubleClickCount, 1);

	// Turning the window off is a supported way to say "this game has no double clicks".
	Rig.EventSystem->SetDoubleClickTime(0.0f);
	ClickOn(Card);
	ClickOn(Card);
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

	FScopedGameWorld Scope;
	if (!TestTrue(TEXT("A world to host the rig"), Scope.World != nullptr))
	{
		return false;
	}
	FPointerRig Rig(Scope.World);
	if (!TestTrue(TEXT("The pointer rig came up"), Rig.IsUsable()))
	{
		return false;
	}

	UDreamWidget* Root = MakeWidget(Scope.World, nullptr, TEXT("Root"), 800.0f, 600.0f);
	UDreamWidget* Card = MakeWidget(Scope.World, Root, TEXT("Card"));
	UDreamLongPressCounter* Counter = Card->AddComponent<UDreamLongPressCounter>();
	if (!TestNotNull(TEXT("The card is listening for long presses"), Counter))
	{
		return false;
	}

	const FVector2D PressPos(200.0, 200.0);
	const float LongPressTime = Rig.EventSystem->GetLongPressTime();
	if (!TestTrue(TEXT("Long press is on by default"), LongPressTime > 0.0f))
	{
		return false;
	}

	// Down, and held for less than the threshold.
	Rig.Press(PressPos);
	Rig.Frame(Card);
	TestEqual(TEXT("A press is not yet a long press"), Counter->LongPressCount, 0);
	Scope.World->TimeSeconds += (double)LongPressTime * 0.5;
	Rig.Frame(Card);
	TestEqual(TEXT("...halfway through, still not"), Counter->LongPressCount, 0);

	// Past the threshold. It fires while the finger is still down, which is the whole point: a context
	// menu that waits for the release cannot be the thing the player is still holding.
	Scope.World->TimeSeconds += (double)LongPressTime;
	Rig.Frame(Card);
	TestEqual(TEXT("Held past the threshold, it fires"), Counter->LongPressCount, 1);
	TestEqual(TEXT("...carrying the pointer that was held"), Counter->LastPointerID, 0);

	Scope.World->TimeSeconds += (double)LongPressTime;
	Rig.Frame(Card);
	TestEqual(TEXT("...once per press, not once per frame"), Counter->LongPressCount, 1);

	Rig.Release(PressPos);
	Rig.Frame(Card);

	// A press that turns into a drag is the same gesture read another way, and only one of the two
	// readings may win. The drag does, because the player can see a drag happening.
	const double Threshold = Rig.Raycaster->GetDragThreshold();
	Rig.Press(PressPos);
	Rig.Frame(Card);
	Rig.MoveTo(PressPos + FVector2D(Threshold * 4.0, 0.0));
	Rig.Frame(Card);
	if (!TestTrue(TEXT("The second press became a drag"), Rig.EventData->bIsDragging))
	{
		return false;
	}
	Scope.World->TimeSeconds += (double)LongPressTime * 3.0;
	Rig.Frame(Card);
	TestEqual(TEXT("A drag being held is not a long press"), Counter->LongPressCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPointerSwipeGestureTest,
	"DreamGUI.Input.Gesture.AFastTravelAcrossAWidgetIsASwipe",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPointerSwipeGestureTest::RunTest(const FString& Parameters)
{
	using namespace DreamDragThresholdTestLocal;

	FScopedGameWorld Scope;
	if (!TestTrue(TEXT("A world to host the rig"), Scope.World != nullptr))
	{
		return false;
	}
	FPointerRig Rig(Scope.World);
	if (!TestTrue(TEXT("The pointer rig came up"), Rig.IsUsable()))
	{
		return false;
	}

	UDreamWidget* Root = MakeWidget(Scope.World, nullptr, TEXT("Root"), 800.0f, 600.0f);
	UDreamWidget* Card = MakeWidget(Scope.World, Root, TEXT("Card"));
	UDreamGestureCounter* Counter = Card->AddComponent<UDreamGestureCounter>();
	if (!TestNotNull(TEXT("The card is listening for gestures"), Counter))
	{
		return false;
	}

	const FVector2D PressPos(200.0, 200.0);
	const float MinDistance = Rig.EventSystem->GetSwipeMinDistance();

	// A short movement is not a swipe, however fast.
	Rig.Press(PressPos);
	Rig.Frame(Card);
	Rig.MoveTo(PressPos + FVector2D(MinDistance * 0.25, 0.0));
	Rig.Release(PressPos + FVector2D(MinDistance * 0.25, 0.0));
	Rig.Frame(Card);
	TestEqual(TEXT("A short travel is not a swipe"), Counter->SwipeCount, 0);

	// Far enough, fast enough, to the right.
	Rig.Press(PressPos);
	Rig.Frame(Card);
	const FVector2D EndPos = PressPos + FVector2D(MinDistance * 3.0, 0.0);
	Rig.MoveTo(EndPos);
	Rig.Release(EndPos);
	Rig.Frame(Card);
	TestEqual(TEXT("A long, fast travel is a swipe"), Counter->SwipeCount, 1);
	TestEqual(TEXT("...snapped to the dominant axis"), Counter->LastSwipeDirection, EDreamUINavigationDirection::Right);
	TestTrue(TEXT("...carrying how far it went"), Counter->LastSwipeDelta.X >= MinDistance);

	// Downward, to prove the axis and the sign are both read. Viewport Y grows downward.
	Rig.Press(PressPos);
	Rig.Frame(Card);
	const FVector2D DownPos = PressPos + FVector2D(0.0, MinDistance * 3.0);
	Rig.MoveTo(DownPos);
	Rig.Release(DownPos);
	Rig.Frame(Card);
	TestEqual(TEXT("A downward travel swipes down"), Counter->LastSwipeDirection, EDreamUINavigationDirection::Down);

	// The same movement taken slowly is a drag, not a swipe. This is the only thing separating them.
	Rig.Press(PressPos);
	Rig.Frame(Card);
	Scope.World->TimeSeconds += (double)Rig.EventSystem->GetSwipeMaxDuration() + 1.0;
	Rig.MoveTo(EndPos);
	Rig.Release(EndPos);
	Rig.Frame(Card);
	TestEqual(TEXT("A slow travel is a drag, not a swipe"), Counter->SwipeCount, 2);
	return true;
}

#endif
