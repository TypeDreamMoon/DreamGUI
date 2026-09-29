// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamWidget.h"
#include "Event/DreamPointerEventData.h"
#include "Interaction/UIEventTrigger.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"

/*
 * WHAT A GESTURE ACTUALLY DELIVERS.
 *
 * The drag-threshold tests next door drive the same state machine, but they hand it its hit result by
 * hand: the raycast is replaced by "assume the pointer is over this widget". That is the right shape
 * for asking what the state machine decides, and it walks straight past the question of whether the
 * pointer was ever over anything.
 *
 * These close that gap. Every assertion below is downstream of a REAL trace: a pixel is worked out
 * from the widget's transform, the pointer is put there through the production input entry points,
 * and the raycaster finds whatever it finds. The counts are read from UUIEventTrigger, a production
 * component sitting on the widget that is supposed to have been sent the event -- so "the button was
 * clicked" means the button was sent a click, not that a click was computed somewhere.
 */
namespace DreamDriverInputTestLocal
{
	const FIntPoint ViewportSize(1280, 720);

	/** What one widget was sent, counted by the production component that exists to be told. */
	struct FWidgetEventLog
	{
		int32 Enter = 0;
		int32 Exit = 0;
		int32 Down = 0;
		int32 Up = 0;
		int32 Click = 0;
		int32 BeginDrag = 0;
		int32 Drag = 0;
		int32 EndDrag = 0;
		int32 DragDrop = 0;
		int32 Scroll = 0;
		FVector2D LastScrollAxis = FVector2D::ZeroVector;

		void Observe(UDreamWidget* InWidget)
		{
			UUIEventTrigger* Trigger = InWidget != nullptr ? InWidget->AddComponent<UUIEventTrigger>() : nullptr;
			if (Trigger == nullptr)
			{
				return;
			}
			Trigger->GetOnPointerEnterEvent().AddLambda([this](UDreamPointerEventData*) { ++Enter; });
			Trigger->GetOnPointerExitEvent().AddLambda([this](UDreamPointerEventData*) { ++Exit; });
			Trigger->GetOnPointerDownEvent().AddLambda([this](UDreamPointerEventData*) { ++Down; });
			Trigger->GetOnPointerUpEvent().AddLambda([this](UDreamPointerEventData*) { ++Up; });
			Trigger->GetOnPointerClickEvent().AddLambda([this](UDreamPointerEventData*) { ++Click; });
			Trigger->GetOnPointerBeginDragEvent().AddLambda([this](UDreamPointerEventData*) { ++BeginDrag; });
			Trigger->GetOnPointerDragEvent().AddLambda([this](UDreamPointerEventData*) { ++Drag; });
			Trigger->GetOnPointerEndDragEvent().AddLambda([this](UDreamPointerEventData*) { ++EndDrag; });
			Trigger->GetOnPointerDragDropEvent().AddLambda([this](UDreamPointerEventData*) { ++DragDrop; });
			Trigger->GetOnPointerScrollEvent().AddLambda([this](UDreamPointerEventData* InEventData)
			{
				++Scroll;
				if (InEventData != nullptr)
				{
					LastScrollAxis = InEventData->ScrollAxisValue;
				}
			});
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverClickTest,
	"DreamGUI.Driver.ClickingAButtonsCentreDeliversOnePointerClick",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverClickTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverInputTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}

	UDreamWidget* Button = Rig.MakeWidget(TEXT("Play"), nullptr, FVector2D(200.0, 100.0), FVector2D::ZeroVector);
	FWidgetEventLog ButtonLog;
	ButtonLog.Observe(Button);
	Rig.PumpFrames(1);

	FDreamDriverRef Driver = Rig.Driver();
	FDreamElementRef Play = Driver->Find(FDreamBy::Name(TEXT("Play")));
	if (!TestTrue(TEXT("The button is findable by display name"), Play->Exists()))
	{
		return false;
	}

	TestTrue(TEXT("Clicking the button's centre completes"), Play->Click());

	// One of each, and not two: a click is one press and one release, and the pointer stayed still,
	// so nothing about it is a drag.
	TestEqual(TEXT("The button was told about one press"), ButtonLog.Down, 1);
	TestEqual(TEXT("The button was told about one release"), ButtonLog.Up, 1);
	TestEqual(TEXT("The button was told about one click"), ButtonLog.Click, 1);
	TestEqual(TEXT("A click that never moved is not a drag"), ButtonLog.BeginDrag, 0);
	// The pointer had to arrive before it could press, which is a real trace finding the widget.
	TestEqual(TEXT("The pointer entered the button on its way in"), ButtonLog.Enter, 1);
	TestTrue(TEXT("The pointer is still over the button afterwards"), Play->IsHovered());
	TestFalse(TEXT("The trigger came back up"), Play->IsPressed());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverHoverTest,
	"DreamGUI.Driver.MovingOnAndOffAWidgetSendsOneEnterAndOneExit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverHoverTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverInputTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}

	UDreamWidget* Card = Rig.MakeWidget(TEXT("Card"), nullptr, FVector2D(200.0, 100.0), FVector2D::ZeroVector);
	FWidgetEventLog CardLog;
	CardLog.Observe(Card);
	Rig.PumpFrames(1);

	FDreamDriverRef Driver = Rig.Driver();
	FDreamElementRef CardElement = Driver->Find(FDreamBy::Name(TEXT("Card")));

	TestTrue(TEXT("Hovering the card completes"), CardElement->Hover());
	TestEqual(TEXT("Arriving sends one enter"), CardLog.Enter, 1);
	TestEqual(TEXT("Arriving sends no exit"), CardLog.Exit, 0);
	TestTrue(TEXT("The card reports itself hovered"), CardElement->IsHovered());

	// Far enough to be off it: the card is 200 wide, so 400 pixels sideways is outside by any measure.
	TestTrue(TEXT("Moving away completes"), CardElement->MoveBy(FVector2D(400.0, 0.0)));
	TestEqual(TEXT("Leaving sends one exit"), CardLog.Exit, 1);
	TestEqual(TEXT("Leaving does not send a second enter"), CardLog.Enter, 1);
	TestFalse(TEXT("The card no longer reports itself hovered"), CardElement->IsHovered());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverDragTest,
	"DreamGUI.Driver.DraggingOneWidgetOntoAnotherDropsOnTheSecondAndClicksNeither",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverDragTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverInputTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}

	// Well apart, so the drag crosses real ground and the drop target is never under the source.
	UDreamWidget* Source = Rig.MakeWidget(TEXT("Card"), nullptr, FVector2D(160.0, 120.0), FVector2D(-300.0, 0.0));
	UDreamWidget* Target = Rig.MakeWidget(TEXT("Slot"), nullptr, FVector2D(160.0, 120.0), FVector2D(300.0, 0.0));
	FWidgetEventLog SourceLog;
	FWidgetEventLog TargetLog;
	SourceLog.Observe(Source);
	TargetLog.Observe(Target);
	Rig.PumpFrames(1);

	FDreamDriverRef Driver = Rig.Driver();
	FDreamElementRef CardElement = Driver->Find(FDreamBy::Name(TEXT("Card")));
	FDreamElementRef SlotElement = Driver->Find(FDreamBy::Name(TEXT("Slot")));
	if (!TestTrue(TEXT("Both widgets are findable"), CardElement->Exists() && SlotElement->Exists()))
	{
		return false;
	}

	TestTrue(TEXT("The drag completes"), CardElement->DragTo(SlotElement));

	// The source is what was dragged, start to finish.
	TestEqual(TEXT("The source was told the drag began once"), SourceLog.BeginDrag, 1);
	TestEqual(TEXT("The source was told the drag ended once"), SourceLog.EndDrag, 1);
	TestTrue(TEXT("The source saw the pointer move while dragging"), SourceLog.Drag > 0);
	// And a press that became a drag is not a click. This is the distinction the whole gesture exists
	// to make, and the one a single-frame jump from A to B would get wrong in both directions.
	TestEqual(TEXT("A press that became a drag is not a click"), SourceLog.Click, 0);

	// The target is what it was dropped on -- not the thing being dragged, which is directly under the
	// cursor the whole way and would win its own hit test if the pipeline did not exclude it.
	TestEqual(TEXT("The target was dropped on once"), TargetLog.DragDrop, 1);
	TestEqual(TEXT("The target was not itself dragged"), TargetLog.BeginDrag, 0);
	TestEqual(TEXT("The target was not clicked"), TargetLog.Click, 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverScrollTest,
	"DreamGUI.Driver.ScrollingOverAWidgetReachesTheWidgetUnderThePointer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverScrollTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverInputTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}

	UDreamWidget* List = Rig.MakeWidget(TEXT("List"), nullptr, FVector2D(300.0, 400.0), FVector2D::ZeroVector);
	FWidgetEventLog ListLog;
	ListLog.Observe(List);
	Rig.PumpFrames(1);

	FDreamDriverRef Driver = Rig.Driver();
	FDreamElementRef ListElement = Driver->Find(FDreamBy::Name(TEXT("List")));

	// A wheel is delivered to whatever the pointer is over, so the hover is half of the act.
	TestTrue(TEXT("Scrolling over the list completes"), ListElement->ScrollBy(FVector2D(0.0, 3.0)));
	TestEqual(TEXT("The list was sent one scroll"), ListLog.Scroll, 1);
	TestEqual(TEXT("The list was sent the value that was turned"), ListLog.LastScrollAxis.Y, 3.0, 0.001);

	return true;
}

#endif
