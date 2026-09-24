// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamSlider.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamPointerEventData.h"
#include "Event/DreamWorldSpaceRaycaster.h"
#include "Interaction/UIEventTrigger.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Driver/DreamDriverWorldSpace.h"
#include "Interaction/DreamDragInteractionTestTypes.h"

/*
 * DRAGGING IN THE WORLD, AND WHO DECIDES THAT A PRESS HAS BECOME ONE.
 *
 * A press becomes a drag when the raycaster the press landed through says so: the input module asks
 * EventData->PressRaycaster->ShouldStartDrag every frame the trigger is held. For a world-space press
 * that is UDreamWorldSpaceRaycaster's rule, not the screen raycaster's -- its own DragThreshold, in the
 * pointer's viewport pixels for a Mouse source and never scaled by any canvas, and its own
 * bHoldToDrag, which turns a press held still for HoldToDragTime into a drag. The tests here pin that it
 * is THAT raycaster's rule a world drag obeys, by setting the world pointer's numbers to something the
 * screen raycaster would never produce and watching where the drag begins.
 *
 * bHoldToDrag has no UMG counterpart (SSlider and the drag-drop operation both start on movement), so
 * its reading is the property's own: "hold press for a little while to entering drag mode", with the
 * drag winning over a long press because a drag is visible while it happens (see
 * UDreamPointerInputModule::ProcessPointerEvent).
 *
 * The same eye and the same panel as the other world-space tests: the origin looking down +X, a panel
 * 300 cm ahead, 2.13 pixels to the centimetre.
 */
namespace DreamDriverWorldSpaceDragTestLocal
{
	const FIntPoint ViewportSize(1280, 720);

	/** Past the event system's double-click time (0.3 s) at the pump's 1/60 s frames, so the next press starts a new click run. */
	constexpr int32 FramesToForgetAClick = 30;

	FMinimalViewInfo EyeAtTheOrigin()
	{
		return DreamDriverWorld::MakeView(FVector::ZeroVector, FRotator::ZeroRotator, 90.0f, ViewportSize);
	}

	/** Down, Click and the three drag moments of one widget, counted by the production component that exists to be told. */
	struct FDragEvents
	{
		int32 Down = 0;
		int32 Click = 0;
		int32 BeginDrag = 0;
		int32 EndDrag = 0;

		void Observe(UDreamWidget* InWidget)
		{
			UUIEventTrigger* Trigger = InWidget != nullptr ? InWidget->AddComponent<UUIEventTrigger>() : nullptr;
			if (Trigger == nullptr)
			{
				return;
			}
			Trigger->GetOnPointerDownEvent().AddLambda([this](UDreamPointerEventData*) { ++Down; });
			Trigger->GetOnPointerClickEvent().AddLambda([this](UDreamPointerEventData*) { ++Click; });
			Trigger->GetOnPointerBeginDragEvent().AddLambda([this](UDreamPointerEventData*) { ++BeginDrag; });
			Trigger->GetOnPointerEndDragEvent().AddLambda([this](UDreamPointerEventData*) { ++EndDrag; });
		}
	};

	/** The world pointer and a panel facing it. */
	struct FWorldStage
	{
		UDreamDriverWorldSpaceRaycaster* Pointer = nullptr;
		UDreamWidget* Panel = nullptr;

		bool IsReady() const { return Pointer != nullptr && Panel != nullptr; }
	};

	FWorldStage SetUpStage(FAutomationTestBase& InTest, FDreamDriverRig& InRig)
	{
		FWorldStage Stage;
		Stage.Pointer = DreamDriverWorld::AttachWorldPointer(InRig, EyeAtTheOrigin(), EDreamWorldPointerSource::Mouse);
		Stage.Panel = DreamDriverWorld::MakeWorldPanel(InRig, TEXT("Panel"), FTransform(FVector(300.0, 0.0, 0.0)), FVector2D(400.0, 300.0));
		InTest.TestNotNull(TEXT("A world pointer was attached to the rig"), Stage.Pointer);
		InTest.TestNotNull(TEXT("A world-space panel was built in front of it"), Stage.Panel);
		return Stage;
	}

	/** What the slider said while a gesture ran, one probe per event. Bound after setup, so setup is not counted. */
	struct FSliderLog
	{
		TStrongObjectPtr<UDreamDragInteractionProbe> Values;
		TStrongObjectPtr<UDreamDragInteractionProbe> CaptureBegins;
		TStrongObjectPtr<UDreamDragInteractionProbe> CaptureEnds;

		explicit FSliderLog(UDreamSlider* InSlider)
			: Values(NewObject<UDreamDragInteractionProbe>())
			, CaptureBegins(NewObject<UDreamDragInteractionProbe>())
			, CaptureEnds(NewObject<UDreamDragInteractionProbe>())
		{
			InSlider->OnValueChanged.AddDynamic(Values.Get(), &UDreamDragInteractionProbe::RecordFloat);
			InSlider->OnMouseCaptureBegin.AddDynamic(CaptureBegins.Get(), &UDreamDragInteractionProbe::RecordSignal);
			InSlider->OnMouseCaptureEnd.AddDynamic(CaptureEnds.Get(), &UDreamDragInteractionProbe::RecordSignal);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverWorldSpaceSliderDragTest,
	"DreamGUI.Driver.WorldSpace.DraggingASlidersHandleHalfwayAlongAWorldPanelMovesTheValueHalfway",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverWorldSpaceSliderDragTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverWorldSpaceDragTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	const FWorldStage Stage = SetUpStage(*this, Rig);
	if (!Stage.IsReady())
	{
		return false;
	}
	UDreamSlider* Slider = Rig.MakeControl<UDreamSlider>(TEXT("Volume"), Stage.Panel, FVector2D(300.0, 40.0));
	// Two frames, as for a slider on the screen: the first lays the control out, the second lets the
	// behaviour place the handle against the handle area the first resolved.
	Rig.PumpFrames(2);
	if (!TestTrue(TEXT("The slider came up with a handle and a handle area"),
		Slider != nullptr && Slider->HandleNode != nullptr && Slider->HandleAreaNode != nullptr))
	{
		return false;
	}

	FDreamDriverRef Driver = Rig.Driver();
	FDreamElementRef Handle = Driver->Find(FDreamBy::Widget(Slider->HandleNode.Get()));
	// Face-on and centred, so the projected travel is the travel scaled: a plane parallel to the image
	// plane projects affinely, and halfway in pixels is halfway along the panel.
	const TOptional<FBox2D> Travel = Driver->Find(FDreamBy::Widget(Slider->HandleAreaNode.Get()))->GetPixelRect();
	const TOptional<FVector2D> Grip = Handle->GetCentrePixel();
	if (!TestTrue(TEXT("The handle's travel is on screen"), Travel.IsSet())
		|| !TestTrue(TEXT("The handle is on screen"), Grip.IsSet()))
	{
		return false;
	}
	const double TravelLength = Travel->Max.X - Travel->Min.X;
	if (!TestTrue(FString::Printf(TEXT("The travel is long enough to aim at (%.1f pixels)"), TravelLength), TravelLength > 100.0))
	{
		return false;
	}
	// Two pixels' worth of value: one for the projection round trip and one for the world ray being
	// made from the pixel's integer corner.
	const float TwoPixels = static_cast<float>(2.0 / TravelLength);
	TestNearlyEqual(TEXT("The slider starts at its minimum"), Slider->GetValue(), 0.0f, TwoPixels);

	FSliderLog Log(Slider);
	const double TargetX = Travel->Min.X + 0.5 * TravelLength;
	TestTrue(TEXT("The drag completes"), Handle->DragBy(FVector2D(TargetX - Grip->X, 0.0)));

	TestNearlyEqual(TEXT("The value is halfway"), Slider->GetValue(), 0.5f, TwoPixels);
	TestTrue(FString::Printf(TEXT("The value changed on at least two frames of the drag (it changed on %d)"), Log.Values->NumFloats()),
		Log.Values->NumFloats() >= 2);
	TestEqual(TEXT("The press began one mouse capture"), Log.CaptureBegins->Signals, 1);
	TestEqual(TEXT("Letting go ended it, once"), Log.CaptureEnds->Signals, 1);
	// And the ray the value was read from was the world pointer's: the slider reads the press
	// raycaster's current ray against the plane it was pressed on.
	const UDreamPointerEventData* EventData = Rig.Context().GetPointerEventData(0);
	TestTrue(TEXT("The drag was carried by the world pointer"),
		EventData != nullptr && EventData->PressRaycaster.Get() == static_cast<UDreamBaseRaycaster*>(Stage.Pointer));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverWorldSpaceDragThresholdTest,
	"DreamGUI.Driver.WorldSpace.AWorldDragBeginsOnlyPastTheWorldPointersOwnThreshold",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverWorldSpaceDragThresholdTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverWorldSpaceDragTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	const FWorldStage Stage = SetUpStage(*this, Rig);
	if (!Stage.IsReady())
	{
		return false;
	}
	// A threshold the screen raycaster does not have (its default is 5 at a canvas scale of 1), so which
	// raycaster's rule a drag obeys is visible in where it begins.
	Stage.Pointer->SetDragThreshold(24.0f);

	// One card in the world, dead ahead; one on the screen overlay, far to the left of anything on the
	// panel, so each gesture below is judged by one raycaster only.
	UDreamWidget* WorldCard = Rig.MakeWidget(TEXT("WorldCard"), Stage.Panel, FVector2D(100.0, 60.0), FVector2D::ZeroVector);
	UDreamWidget* ScreenCard = Rig.MakeWidget(TEXT("ScreenCard"), nullptr, FVector2D(160.0, 100.0), FVector2D(-400.0, 0.0));
	if (!TestNotNull(TEXT("A card on the world panel"), WorldCard) || !TestNotNull(TEXT("A card on the screen"), ScreenCard))
	{
		return false;
	}
	FDragEvents WorldEvents;
	FDragEvents ScreenEvents;
	WorldEvents.Observe(WorldCard);
	ScreenEvents.Observe(ScreenCard);
	Rig.PumpFrames(1);
	FDreamDriverRef Driver = Rig.Driver();

	// The screen first: twelve pixels is past the screen raycaster's five, so there it is a drag. This is
	// what makes twelve a meaningful distance to try in the world below.
	TestTrue(TEXT("A twelve-pixel drag on the screen card completes"), Driver->Sequence()
		.MoveTo(FDreamBy::Widget(ScreenCard))
		.Press()
		.MoveBy(FVector2D(12.0, 0.0))
		.Release()
		.WaitFrames(FramesToForgetAClick)
		.Perform());
	TestEqual(TEXT("On the screen, twelve pixels is a drag"), ScreenEvents.BeginDrag, 1);

	// The world: the same twelve pixels is short of the world pointer's 24 -- still a press -- and
	// another 24 (36 in all) is past it.
	int32 BeginDragsAfterTwelve = -1;
	TestTrue(TEXT("A drag on the world card in two moves completes"), Driver->Sequence()
		.MoveTo(FDreamBy::Widget(WorldCard))
		.Press()
		.MoveBy(FVector2D(12.0, 0.0))
		.Then([&BeginDragsAfterTwelve, &WorldEvents](FDreamDriverContext&) { BeginDragsAfterTwelve = WorldEvents.BeginDrag; })
		.MoveBy(FVector2D(24.0, 0.0))
		.Release()
		.Perform());
	TestEqual(TEXT("In the world, twelve pixels short of the world pointer's threshold is still a press"), BeginDragsAfterTwelve, 0);
	TestEqual(TEXT("Thirty-six pixels, past it, is a drag"), WorldEvents.BeginDrag, 1);
	TestEqual(TEXT("... which ended when the button came up"), WorldEvents.EndDrag, 1);
	TestEqual(TEXT("A press that became a drag is not a click"), WorldEvents.Click, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverWorldSpaceHoldToDragOnTest,
	"DreamGUI.Driver.WorldSpace.WithHoldToDragOnAStillPressHeldLongEnoughBecomesADrag",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverWorldSpaceHoldToDragOnTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverWorldSpaceDragTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	const FWorldStage Stage = SetUpStage(*this, Rig);
	if (!Stage.IsReady())
	{
		return false;
	}
	Stage.Pointer->SetHoldToDrag(true);
	// Shorter than the event system's long press (0.5 s), so what a still hold turns into is a drag and
	// nothing else is competing for it.
	Stage.Pointer->SetHoldToDragTime(0.25f);

	UDreamWidget* Card = Rig.MakeWidget(TEXT("Card"), Stage.Panel, FVector2D(100.0, 60.0), FVector2D::ZeroVector);
	if (!TestNotNull(TEXT("A card on the world panel"), Card))
	{
		return false;
	}
	FDragEvents Events;
	Events.Observe(Card);
	Rig.PumpFrames(1);
	FDreamDriverRef Driver = Rig.Driver();

	// A quick press is still a click: one frame down is far short of the hold time.
	TestTrue(TEXT("A quick click completes"), Driver->Sequence()
		.Click(FDreamBy::Widget(Card))
		.WaitFrames(FramesToForgetAClick)
		.Perform());
	TestEqual(TEXT("A quick press is a click"), Events.Click, 1);
	TestEqual(TEXT("... and not a drag"), Events.BeginDrag, 0);

	// Held still for 0.5 s. Nothing moves; the world pointer's clock does.
	int32 BeginDragsWhileHeld = -1;
	TestTrue(TEXT("A still press held for half a second completes"), Driver->Sequence()
		.Press()
		.WaitFrames(30)
		.Then([&BeginDragsWhileHeld, &Events](FDreamDriverContext&) { BeginDragsWhileHeld = Events.BeginDrag; })
		.Release()
		.Perform());
	TestEqual(TEXT("Held past the hold time without moving, the press became a drag"), BeginDragsWhileHeld, 1);
	TestEqual(TEXT("... which ended at the release"), Events.EndDrag, 1);
	TestEqual(TEXT("... and a press that became a drag is not a click"), Events.Click, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverWorldSpaceHoldToDragOffTest,
	"DreamGUI.Driver.WorldSpace.WithHoldToDragOffAStillPressHeldJustAsLongIsStillAClick",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverWorldSpaceHoldToDragOffTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverWorldSpaceDragTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	const FWorldStage Stage = SetUpStage(*this, Rig);
	if (!Stage.IsReady())
	{
		return false;
	}
	// The same hold time as the test above, so that it is the switch and not the time that differs.
	Stage.Pointer->SetHoldToDrag(false);
	Stage.Pointer->SetHoldToDragTime(0.25f);

	UDreamWidget* Card = Rig.MakeWidget(TEXT("Card"), Stage.Panel, FVector2D(100.0, 60.0), FVector2D::ZeroVector);
	if (!TestNotNull(TEXT("A card on the world panel"), Card))
	{
		return false;
	}
	FDragEvents Events;
	Events.Observe(Card);
	Rig.PumpFrames(1);

	TestTrue(TEXT("A still press held for half a second completes"), Rig.Driver()->Sequence()
		.MoveTo(FDreamBy::Widget(Card))
		.Press()
		.WaitFrames(30)
		.Release()
		.Perform());
	TestEqual(TEXT("The press was delivered"), Events.Down, 1);
	TestEqual(TEXT("With hold-to-drag off, holding still never begins a drag"), Events.BeginDrag, 0);
	TestEqual(TEXT("... so the release is a click"), Events.Click, 1);
	return true;
}

#endif
