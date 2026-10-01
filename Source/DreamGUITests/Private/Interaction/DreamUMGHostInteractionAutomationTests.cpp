// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamWidget.h"
#include "Event/DreamScreenSpaceRaycaster.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverProjection.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Interaction/DreamUMGHostInteractionTestTypes.h"

/*
 * UDreamUMGWidgetInteraction -- the bridge that carries the pointer into a hosted UMG widget -- under a
 * real pointer that presses on the surface it bridges and drags.
 *
 * The UMG half cannot be watched here: a hosted widget's hit grid is filled by drawing it, and a headless
 * world draws nothing. So the bridge is put on a plain surface and judged by what it decides, which is the
 * half that broke: whether it keeps following the pointer, keeps the cursor its virtual user shares, and
 * keeps ticking the moves through for as long as the press that started on it is held.
 */
namespace DreamUMGHostInteractionTestLocal
{
	const FIntPoint ViewportSize(1280, 720);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUMGHostDragKeepsCursorTest,
	"DreamGUI.Interaction.UMG.ADragThatStartsOnTheSurfaceKeepsItsCursorUntilTheRelease",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamUMGHostDragKeepsCursorTest, "DreamGUI.Interaction.UMG.ADragThatStartsOnTheSurfaceKeepsItsCursorUntilTheRelease", "[Pointer][Animated]")

/*
 * A press that travels past the drag threshold becomes a drag, and the event system takes the dragged
 * widget out of its own hit test -- so the bridge heard an exit a few pixels into every drag that began on
 * it. It acted on that exit: gave the shared cursor back, stopped following the pointer and stopped
 * ticking, and a UMG slider dragged by its thumb froze where the drag began. Here the surface is pressed
 * and dragged across itself and then off it: the exit arrives, and the bridge goes on following and
 * ticking until the release, after which -- the pointer now being elsewhere -- it lets go.
 */
bool FDreamUMGHostDragKeepsCursorTest::RunTest(const FString& Parameters)
{
	using namespace DreamUMGHostInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamWidget* Surface = Rig.MakeWidget(TEXT("Surface"), nullptr, FVector2D(300.0, 200.0));
	if (!TestNotNull(TEXT("A surface can be made on the rig"), Surface))
	{
		return false;
	}
	UDreamUMGDragInteractionProbe* Probe = Surface->AddComponent<UDreamUMGDragInteractionProbe>();
	if (!TestNotNull(TEXT("The surface carries the bridge"), Probe))
	{
		Surface->DestroyWidget();
		return false;
	}
	Rig.PumpFrames(1);
	const TOptional<FVector2D> Centre = FDreamDriverProjection::WidgetCentrePixel(Surface);
	if (!TestTrue(TEXT("The bridge has a virtual Slate user to forward through"), Probe->IsEnrolled())
		|| !TestTrue(TEXT("The surface is somewhere the pointer can reach"), Centre.IsSet()))
	{
		Surface->DestroyWidget();
		return false;
	}
	// Past the raycaster's own threshold, read rather than assumed, as the other drag tests read it.
	const double ThresholdPixels = FMath::Sqrt(static_cast<double>(Rig.Raycaster()->GetScaledDragThresholdSquare()));

	int32 ExitsWhilePressed = 0;
	int32 TicksBefore = 0;
	int32 TicksWhileDragging = 0;
	bool bFollowingWhileDragging = false;
	bool bHoldingWhileDragging = false;
	TestTrue(TEXT("Pressing on the surface, dragging across it and off it, and letting go completes"),
		Rig.Driver()->Sequence()
			.MoveToPixel(Centre.GetValue())
			.Press()
			.MoveToPixel(Centre.GetValue() + FVector2D(ThresholdPixels + 2.0, 0.0))
			.MoveToPixel(Centre.GetValue() + FVector2D(40.0, 0.0))
			.Then([&](FDreamDriverContext&)
			{
				ExitsWhilePressed = Probe->ExitCount;
				TicksBefore = Probe->TickCount;
			})
			.MoveToPixel(Centre.GetValue() + FVector2D(80.0, 0.0))
			.WaitFrames(1)
			.Then([&](FDreamDriverContext&)
			{
				TicksWhileDragging = Probe->TickCount - TicksBefore;
				bFollowingWhileDragging = Probe->IsFollowingPointer();
				bHoldingWhileDragging = Probe->HoldsSharedCursor();
			})
			.MoveToPixel(Centre.GetValue() + FVector2D(400.0, 0.0))
			.Release()
			.Perform());
	Rig.PumpFrames(1);

	TestTrue(TEXT("The drag took the surface out of the hit test, so an exit arrived while it was pressed"), ExitsWhilePressed >= 1);
	TestTrue(TEXT("The bridge went on following the pointer through the drag"), bFollowingWhileDragging);
	TestTrue(TEXT("And kept the shared cursor"), bHoldingWhileDragging);
	TestTrue(FString::Printf(TEXT("And went on ticking the moves through (%d ticks)"), TicksWhileDragging), TicksWhileDragging > 0);
	TestFalse(TEXT("Released off the surface, the bridge stops following the pointer"), Probe->IsFollowingPointer());
	TestFalse(TEXT("And gives the shared cursor back"), Probe->HoldsSharedCursor());

	// The bridge hands its virtual Slate user back as it goes; a surface left to the world's teardown
	// would leave that to whenever the teardown reached it.
	Surface->DestroyWidget();
	return true;
}

#endif
