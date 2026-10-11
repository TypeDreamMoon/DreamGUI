// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamWidget.h"
#include "Event/DreamBaseRaycaster.h"
#include "Event/DreamPointerEventData.h"
#include "Event/DreamScreenSpaceRaycaster.h"
#include "Event/DreamWorldSpaceRaycaster.h"
#include "Interaction/UIEventTrigger.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverWorldSpace.h"

/*
 * A FINGER'S DRAG ON A WORLD PANEL, AND WHOSE THRESHOLD ITS FIRST MOVE CROSSES.
 *
 * FDreamDriverElement::TouchDragBy has the mouse drag's shape: the finger's first move clears the drag threshold, then a
 * midpoint and the destination follow on frames of their own. Which threshold is the question here. A press becomes a drag
 * when the raycaster that took it says so (EventData->PressRaycaster->ShouldStartDrag), and for a press on a world panel
 * that is the world pointer's own DragThreshold, in viewport pixels and never scaled by a canvas -- not the screen
 * raycaster's. The mouse's DragCrossThreshold step already reads the raycaster that took the press; the finger's drag reads
 * the same one, so a world pointer whose threshold the screen raycaster does not share is crossed on the first move, as it
 * would be on a screen.
 */
namespace DreamDriverWorldSpaceTouchDragTestLocal
{
	const FIntPoint ViewportSize(1280, 720);

	/** The world pointer's threshold, in pixels: far past the screen raycaster's, so whose was crossed shows in where the drag began. */
	constexpr float WorldThreshold = 24.0f;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverWorldSpaceTouchDragThresholdTest,
	"DreamGUI.Driver.WorldSpace.AFingerDraggedOnAWorldPanelBeginsItsDragOnTheFirstMoveJustPastTheWorldPointersThreshold",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A card on a panel three metres ahead, the world pointer's threshold 24 pixels. A finger dragged 120 pixels across it begins
 * one drag, on its first move: where the drag began is just past 24 pixels from where the finger landed, not halfway along
 * -- which is where it began while the driver aimed the first move past the screen raycaster's threshold of 5, short of the
 * world pointer's. The press was the world pointer's, the drag ended when the finger lifted, and a press that became a drag
 * is no click.
 */
bool FDreamDriverWorldSpaceTouchDragThresholdTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverWorldSpaceTouchDragTestLocal;
	// What the card heard, declared before the rig so that it outlives the trigger that writes it.
	int32 BeginDrags = 0;
	int32 EndDrags = 0;
	int32 Clicks = 0;
	double TravelAtBegin = -1.0;
	const UDreamBaseRaycaster* PressedThrough = nullptr;

	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamDriverWorldSpaceRaycaster* Pointer = DreamDriverWorld::AttachWorldPointer(Rig,
		DreamDriverWorld::MakeView(FVector::ZeroVector, FRotator::ZeroRotator, 90.0f, ViewportSize), EDreamWorldPointerSource::Mouse);
	UDreamWidget* Panel = DreamDriverWorld::MakeWorldPanel(Rig, TEXT("Panel"), FTransform(FVector(300.0, 0.0, 0.0)), FVector2D(400.0, 300.0));
	if (!TestNotNull(TEXT("A world pointer was attached to the rig"), Pointer)
		|| !TestNotNull(TEXT("A world-space panel was built in front of it"), Panel))
	{
		return false;
	}
	Pointer->SetDragThreshold(WorldThreshold);
	UDreamWidget* Card = Rig.MakeWidget(TEXT("Card"), Panel, FVector2D(160.0, 100.0), FVector2D::ZeroVector);
	UUIEventTrigger* Trigger = Card != nullptr ? Card->AddComponent<UUIEventTrigger>() : nullptr;
	if (!TestNotNull(TEXT("A card on the panel that reports its pointer events"), Trigger))
	{
		return false;
	}
	Trigger->GetOnPointerBeginDragEvent().AddLambda([&BeginDrags, &TravelAtBegin, &PressedThrough](UDreamPointerEventData* InEventData)
	{
		++BeginDrags;
		if (InEventData != nullptr)
		{
			TravelAtBegin = FVector2D::Distance(FVector2D(InEventData->PointerPosition), FVector2D(InEventData->PressPointerPosition));
			PressedThrough = InEventData->PressRaycaster.Get();
		}
	});
	Trigger->GetOnPointerEndDragEvent().AddLambda([&EndDrags](UDreamPointerEventData*) { ++EndDrags; });
	Trigger->GetOnPointerClickEvent().AddLambda([&Clicks](UDreamPointerEventData*) { ++Clicks; });
	Rig.PumpFrames(1);

	const double ScreenThreshold = FMath::Sqrt(static_cast<double>(Rig.Raycaster()->GetScaledDragThresholdSquare()));
	TestTrue(FString::Printf(TEXT("The screen raycaster's threshold (%.1f px) is well short of the world pointer's"), ScreenThreshold),
		ScreenThreshold + 8.0 < WorldThreshold);

	TestTrue(TEXT("A finger dragged 120 pixels across the card completes"),
		Rig.Driver()->Find(FDreamBy::Widget(Card))->TouchDragBy(FVector2D(120.0, 0.0), 0));
	TestEqual(TEXT("The drag began once"), BeginDrags, 1);
	TestTrue(TEXT("...through the world pointer"), PressedThrough == static_cast<const UDreamBaseRaycaster*>(Pointer));
	TestTrue(FString::Printf(TEXT("...on the first move, just past the world pointer's threshold (%.1f px from where the finger landed)"), TravelAtBegin),
		TravelAtBegin > WorldThreshold && TravelAtBegin <= WorldThreshold + 3.0);
	TestEqual(TEXT("It ended once, when the finger lifted"), EndDrags, 1);
	TestEqual(TEXT("A press that became a drag is not a click"), Clicks, 0);
	return true;
}

#endif
