// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamBorder.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamPointerEventData.h"
#include "Event/DreamScreenSpaceRaycaster.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverProjection.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Interaction/DreamPressInteractionTestTypes.h"

/*
 * UDreamBorder's four pointer events, produced by real gestures and held to UMG's UBorder.
 *
 * UBorder binds SBorder's OnMouseButtonDown / OnMouseButtonUp / OnMouseMove / OnMouseDoubleClick
 * straight to its four events, so SBorder's input routing is the reference. Two facts of that routing
 * matter below:
 *
 *   - Slate delivers the SECOND press of a double click as OnMouseButtonDoubleClick, not as another
 *     OnMouseButtonDown (FSlateApplication::ProcessMouseButtonDoubleClickEvent; only a widget holding
 *     mouse capture gets it as a down instead, and a UBorder does not capture). So a double click on
 *     a UBorder is one down, one double click and two ups.
 *   - OnMouseMove in UMG fires for any motion over the border. This control's reference page says
 *     otherwise, deliberately (OnMouseMoveEvent is "fed by the DRAG seam ... a hover that never
 *     pressed is OnPointerEnter/Exit"), and where the page states a design of its own, the page is
 *     what is tested: a drag across the border reports movement, and plain hovering is left alone.
 *
 * Reporting is off by default (a listening border is a consuming border), so every test turns it on.
 */
namespace DreamPressBorderTestLocal
{
	const FIntPoint ViewportSize(1280, 720);

	struct FPlacedBorder
	{
		UDreamBorder* Border = nullptr;
		TSharedPtr<FDreamDriverElement> Element;

		bool IsReady() const { return Border != nullptr && Element.IsValid() && Element->Exists(); }
	};

	FPlacedBorder PlaceBorder(FAutomationTestBase& InTest, FDreamDriverRig& InRig, UDreamPressInteractionListener* InListener)
	{
		FPlacedBorder Placed;
		InRig.BindTest(&InTest);
		if (!InTest.TestTrue(TEXT("The headless rig came up"), InRig.IsUsable()))
		{
			return Placed;
		}
		UDreamBorder* Border = InRig.MakeControl<UDreamBorder>(TEXT("Frame"), nullptr, FVector2D(300.0, 200.0));
		if (!InTest.TestNotNull(TEXT("A border can be made on the rig"), Border))
		{
			return Placed;
		}
		Border->SetReportMouseEvents(true);
		Border->OnMouseButtonDownEvent.AddDynamic(InListener, &UDreamPressInteractionListener::HandleBorderButtonDown);
		Border->OnMouseButtonUpEvent.AddDynamic(InListener, &UDreamPressInteractionListener::HandleBorderButtonUp);
		Border->OnMouseMoveEvent.AddDynamic(InListener, &UDreamPressInteractionListener::HandleBorderMove);
		Border->OnMouseDoubleClickEvent.AddDynamic(InListener, &UDreamPressInteractionListener::HandleBorderDoubleClick);
		InRig.PumpFrames(1);

		Placed.Border = Border;
		Placed.Element = InRig.Driver()->Find(FDreamBy::Widget(Border));
		InTest.TestTrue(TEXT("The driver can find the border it is about to act on"), Placed.IsReady());
		return Placed;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPressBorderClickTest,
	"DreamGUI.Border.AClickReportsOneButtonDownAndOneButtonUp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPressBorderClickTest::RunTest(const FString& Parameters)
{
	using namespace DreamPressBorderTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FPlacedBorder Placed = PlaceBorder(*this, Rig, Listener.Get());
	if (!Placed.IsReady())
	{
		return false;
	}

	TestTrue(TEXT("Clicking the border completes"), Placed.Element->Click());

	TestEqual(TEXT("One button down"), Listener->BorderButtonDownCount, 1);
	TestEqual(TEXT("One button up"), Listener->BorderButtonUpCount, 1);
	TestEqual(TEXT("A single click is not a double click"), Listener->BorderDoubleClickCount, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPressBorderDragMoveTest,
	"DreamGUI.Border.DraggingAcrossTheBorderReportsItsMovement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPressBorderDragMoveTest::RunTest(const FString& Parameters)
{
	using namespace DreamPressBorderTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FPlacedBorder Placed = PlaceBorder(*this, Rig, Listener.Get());
	if (!Placed.IsReady())
	{
		return false;
	}

	// Sixty pixels to the right: well past the drag threshold, and still inside a 300-wide border.
	TestTrue(TEXT("Dragging across the border completes"), Placed.Element->DragBy(FVector2D(60.0, 0.0)));

	TestTrue(TEXT("The drag across the border was reported as movement"), Listener->BorderMoveCount >= 1);
	TestEqual(TEXT("It began with one button down"), Listener->BorderButtonDownCount, 1);
	TestEqual(TEXT("And ended with one button up"), Listener->BorderButtonUpCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPressBorderDoubleClickTest,
	"DreamGUI.Border.ADoubleClickReportsOneDoubleClick",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPressBorderDoubleClickTest::RunTest(const FString& Parameters)
{
	using namespace DreamPressBorderTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FPlacedBorder Placed = PlaceBorder(*this, Rig, Listener.Get());
	if (!Placed.IsReady())
	{
		return false;
	}

	TestTrue(TEXT("Double-clicking the border completes"), Placed.Element->DoubleClick());

	TestEqual(TEXT("One double click"), Listener->BorderDoubleClickCount, 1);
	TestEqual(TEXT("Both presses came back up"), Listener->BorderButtonUpCount, 2);
	return true;
}

/**
 * The second press of a double click is not a button down in Slate: it is routed as
 * OnMouseButtonDoubleClick instead (see the note at the top). A UBorder handler that selects on down
 * and opens on double click therefore runs its down half once per double click, not twice.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPressBorderDoubleClickDownTest,
	"DreamGUI.Border.TheSecondPressOfADoubleClickIsTheDoubleClickNotASecondButtonDown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPressBorderDoubleClickDownTest::RunTest(const FString& Parameters)
{
	using namespace DreamPressBorderTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FPlacedBorder Placed = PlaceBorder(*this, Rig, Listener.Get());
	if (!Placed.IsReady())
	{
		return false;
	}

	TestTrue(TEXT("Double-clicking the border completes"), Placed.Element->DoubleClick());

	TestEqual(TEXT("A double click is one button down, as UBorder reports it"), Listener->BorderButtonDownCount, 1);
	TestEqual(TEXT("Followed by one double click"), Listener->BorderDoubleClickCount, 1);
	return true;
}

/**
 * A double click is two presses in one place, not merely on one widget. Slate's double click is the
 * platform's, and the platform also wants the second press near the first: Windows' double-click
 * rectangle (SM_CXDOUBLECLK) is the same 4 pixels as its drag threshold (SM_CXDRAG). So two quick
 * clicks farther apart than the pointer's drag threshold are two presses, each with its own button
 * down, and two inside it are one double click. The threshold is read off the rig's raycaster rather
 * than assumed, so the test follows whatever that raycaster is set to.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPressBorderDoubleClickDistanceTest,
	"DreamGUI.Border.TwoQuickClicksAreADoubleClickOnlyWithinTheDragThresholdOfEachOther",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPressBorderDoubleClickDistanceTest::RunTest(const FString& Parameters)
{
	using namespace DreamPressBorderTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FPlacedBorder Placed = PlaceBorder(*this, Rig, Listener.Get());
	if (!Placed.IsReady())
	{
		return false;
	}
	UDreamScreenSpaceRaycaster* Raycaster = Rig.Raycaster();
	const TOptional<FVector2D> Centre = FDreamDriverProjection::WidgetCentrePixel(Placed.Border);
	const TOptional<FBox2D> Rect = FDreamDriverProjection::WidgetToPixelRect(Placed.Border);
	if (!TestNotNull(TEXT("The rig has a raycaster to measure the drag threshold with"), Raycaster)
		|| !TestTrue(TEXT("The border is on screen"), Centre.IsSet() && Rect.IsSet()))
	{
		return false;
	}

	// The threshold in the pixels the pointer is measured in. Four of them, and never under 20 pixels,
	// is well past it; half of one is well inside.
	const double ThresholdSquare = static_cast<double>(Raycaster->GetScaledDragThresholdSquare());
	const double Threshold = FMath::Sqrt(FMath::Max(ThresholdSquare, 0.0));
	const FVector2D First = Centre.GetValue();
	const FVector2D Far = First + FVector2D(FMath::Max(Threshold * 4.0, 20.0), 0.0);
	const FVector2D Near = First + FVector2D(Threshold * 0.5, 0.0);
	if (!TestTrue(TEXT("The far point is past the drag threshold"), FVector2D::DistSquared(First, Far) > ThresholdSquare)
		|| !TestTrue(TEXT("The near point is inside it"), FVector2D::DistSquared(First, Near) <= ThresholdSquare)
		|| !TestTrue(TEXT("Both points are on the border"), Rect->IsInside(Far) && Rect->IsInside(Near)))
	{
		return false;
	}

	// Each pair is one sequence of six frames at the driver's 1/60 s: the second press lands about
	// 0.03 s after the first click, far inside the double-click time. Only the distance differs.
	if (!TestNotNull(TEXT("The rig has an event system"), Rig.EventSystem())
		|| !TestTrue(TEXT("The double-click time is longer than a pair takes"), Rig.EventSystem()->GetDoubleClickTime() > 0.1f))
	{
		return false;
	}

	TestTrue(TEXT("Two quick clicks farther apart than the threshold complete"),
		Rig.Driver()->Sequence().MoveToPixel(First).Press().Release().MoveToPixel(Far).Press().Release().Perform());
	const UDreamPointerEventData* Pointer = Rig.Context().GetPointerEventData(0);
	TestTrue(TEXT("The presses were measured by the rig's raycaster"),
		Pointer != nullptr && Pointer->PressRaycaster.Get() == Raycaster);
	TestEqual(TEXT("Apart, the second press is an ordinary button down"), Listener->BorderButtonDownCount, 2);
	TestEqual(TEXT("And neither press is a double click"), Listener->BorderDoubleClickCount, 0);

	// Past the double-click time, so the next pair starts a run of its own.
	Rig.PumpFrames(30);
	const int32 DownsBeforeNearPair = Listener->BorderButtonDownCount;
	TestTrue(TEXT("Two quick clicks within the threshold complete"),
		Rig.Driver()->Sequence().MoveToPixel(First).Press().Release().MoveToPixel(Near).Press().Release().Perform());
	TestEqual(TEXT("Together, the second press is the double click"), Listener->BorderDoubleClickCount, 1);
	TestEqual(TEXT("And only the first press of the pair is a button down"),
		Listener->BorderButtonDownCount - DownsBeforeNearPair, 1);
	return true;
}

#endif
