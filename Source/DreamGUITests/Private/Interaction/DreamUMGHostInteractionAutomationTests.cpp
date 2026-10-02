// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamWidget.h"
#include "Event/DreamScreenSpaceRaycaster.h"
#include "Event/DreamUIInputTypes.h"
#include "UMG/DreamUMGWidget.h"

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUMGHostFingersTest,
	"DreamGUI.Interaction.UMG.EachFingerReachesTheHostedWidgetAsATouchOfItsOwn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamUMGHostFingersTest, "DreamGUI.Interaction.UMG.EachFingerReachesTheHostedWidgetAsATouchOfItsOwn", "[Touch][Animated]")

/*
 * The bridge sent every finger to Slate as the left mouse button: its press and release took a mouse key from the
 * pointer's button, so the touch branch never ran, every move went as a mouse move, and one host followed one pointer --
 * a second finger's release let go of the first. A UMG ScrollBox pans on touch only, so a finger never scrolled one. And
 * the first tap on a fresh host was sent nowhere, because the pointer's index was learned only in Tick.
 *
 * Each finger is forwarded as a touch of its own now: its own index, a start at full force sent at the press, moves
 * while it is down with the first flagged as Slate's first move, and a release with no force -- and lifting one leaves
 * the other as it was. Checked on a surface whose visual is a UMG widget, the probe catching what would reach Slate: two
 * fingers down, the second moved and lifted, then the first moved and lifted.
 */
bool FDreamUMGHostFingersTest::RunTest(const FString& Parameters)
{
	using namespace DreamUMGHostInteractionTestLocal;
	using EKind = FDreamUMGSentPointerEvent::EKind;
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
	// The bridge forwards into a UMG widget's visual and nothing else, so the surface wears one -- hosting no class, it
	// draws nothing, which is all a headless world could draw of it anyway.
	Surface->RemoveVisual();
	if (!TestNotNull(TEXT("The surface's visual is a UMG widget"), Surface->CreateNewVisual<UDreamUMGWidget>()))
	{
		Surface->DestroyWidget();
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
		|| !TestTrue(TEXT("The surface is somewhere a finger can reach"), Centre.IsSet()))
	{
		Surface->DestroyWidget();
		return false;
	}
	const FVector2D FirstFinger = Centre.GetValue() + FVector2D(-60.0, 0.0);
	const FVector2D SecondFinger = Centre.GetValue() + FVector2D(60.0, 0.0);

	// The first finger down on a fresh host: its press is sent at once, before any tick has run for it.
	TestTrue(TEXT("The first finger goes down"), Rig.Driver()->Sequence().TouchDown(0, FirstFinger).Perform());
	TArray<FDreamUMGSentPointerEvent> Downs = Probe->SentOfKind(EKind::Down);
	if (TestEqual(TEXT("Its press reached Slate"), Downs.Num(), 1))
	{
		TestTrue(TEXT("as a touch"), Downs[0].bTouch);
		TestEqual(TEXT("of the first finger"), Downs[0].PointerIndex, 0);
		TestEqual(TEXT("at full force"), Downs[0].Force, 1.0f);
	}
	TestTrue(TEXT("The second finger goes down"), Rig.Driver()->Sequence().TouchDown(1, SecondFinger).Perform());
	Downs = Probe->SentOfKind(EKind::Down);
	if (TestEqual(TEXT("Its press reached Slate as well"), Downs.Num(), 2))
	{
		TestTrue(TEXT("as a touch"), Downs[1].bTouch);
		TestEqual(TEXT("of the second finger, an index of its own"), Downs[1].PointerIndex, 1);
	}

	// The second finger travels: touch moves of its own, the first flagged.
	Probe->Sent.Reset();
	TestTrue(TEXT("The second finger moves"),
		Rig.Driver()->Sequence().TouchMoveTo(1, SecondFinger + FVector2D(0.0, 30.0)).WaitFrames(2).Perform());
	const TArray<FDreamUMGSentPointerEvent> SecondMoves = Probe->SentOfKind(EKind::Move);
	if (TestTrue(TEXT("Its travel was sent"), SecondMoves.Num() >= 2))
	{
		TestTrue(TEXT("its first move flagged as Slate's first move"), SecondMoves[0].bFirstMove);
		TestFalse(TEXT("and the ordinary move after it not"), SecondMoves.Last().bFirstMove);
		TestTrue(TEXT("every one a touch of the second finger"), SecondMoves.FindByPredicate([](const FDreamUMGSentPointerEvent& InEvent)
		{
			return !InEvent.bTouch || InEvent.PointerIndex != 1;
		}) == nullptr);
	}

	// The second finger lifts: its release, and the first finger untouched by it.
	Probe->Sent.Reset();
	TestTrue(TEXT("The second finger lifts"), Rig.Driver()->Sequence().TouchUp(1).Perform());
	const TArray<FDreamUMGSentPointerEvent> SecondUps = Probe->SentOfKind(EKind::Up);
	if (TestEqual(TEXT("Lifting it sent one release"), SecondUps.Num(), 1))
	{
		TestTrue(TEXT("a touch"), SecondUps[0].bTouch);
		TestEqual(TEXT("of the second finger"), SecondUps[0].PointerIndex, 1);
		TestEqual(TEXT("with no force left"), SecondUps[0].Force, 0.0f);
	}
	TestTrue(TEXT("The first finger is still followed"), Probe->IsForwarding(DreamUIPointerIds::ForTouch(0)));

	// The first finger, still down, travels and lifts as itself.
	Probe->Sent.Reset();
	TestTrue(TEXT("The first finger moves"),
		Rig.Driver()->Sequence().TouchMoveTo(0, FirstFinger + FVector2D(0.0, -30.0)).WaitFrames(2).Perform());
	const TArray<FDreamUMGSentPointerEvent> FirstMoves = Probe->SentOfKind(EKind::Move);
	if (TestTrue(TEXT("Its travel was sent"), FirstMoves.Num() >= 1))
	{
		TestTrue(TEXT("as a touch of the first finger"), FirstMoves[0].bTouch && FirstMoves[0].PointerIndex == 0);
	}
	TestTrue(TEXT("The first finger lifts"), Rig.Driver()->Sequence().TouchUp(0).Perform());
	const TArray<FDreamUMGSentPointerEvent> FirstUps = Probe->SentOfKind(EKind::Up);
	if (TestEqual(TEXT("Lifting it sent one release"), FirstUps.Num(), 1))
	{
		TestEqual(TEXT("of the first finger"), FirstUps[0].PointerIndex, 0);
		TestEqual(TEXT("with no force left"), FirstUps[0].Force, 0.0f);
	}

	Surface->DestroyWidget();
	return true;
}

#endif
