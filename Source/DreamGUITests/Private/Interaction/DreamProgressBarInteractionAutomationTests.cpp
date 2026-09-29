// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Controls/DreamButton.h"
#include "Controls/DreamProgressBar.h"
#include "Core/Components/DreamWidget.h"
#include "UObject/StrongObjectPtr.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Interaction/DreamDragInteractionTestTypes.h"

/*
 * A PROGRESS BAR IS NOT AN INPUT, BUT IT IS STILL IN THE WAY.
 *
 * UMG's UProgressBar has no events and SProgressBar handles no pointer input: progress is written by
 * code. What a pointer can still do to one is land on it. UProgressBar leaves UWidget's default
 * visibility, Visible, which is hit-testable -- so Slate's hit test picks the bar as the topmost widget
 * under the pointer, and an unhandled press bubbles up the BAR's ancestors, never sideways to a widget
 * drawn beneath it. A button under a visible bar does not get the click. Making the bar
 * Not Hit-Testable (Self & Children) is how UMG lets the click through.
 *
 * Here every visual is a raycast target by default (UDreamVisual::bRaycastTarget), which is the same
 * answer, and EDreamWidgetVisibility spells UMG's HitTestInvisible the same way.
 */
namespace DreamProgressBarInteractionTestLocal
{
	const FIntPoint ViewportSize(1280, 720);

	/** One notch toward the user, in the shape the production input actors send a wheel: InputScroll(FVector2D(Axis, Axis)). */
	const FVector2D WheelTowardUser(-1.0, -1.0);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamProgressBarInteractionPointerTest,
	"DreamGUI.ProgressBar.ClickingDraggingAndScrollingOverABarLeaveItsPercentAlone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamProgressBarInteractionPointerTest::RunTest(const FString& Parameters)
{
	using namespace DreamProgressBarInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}

	UDreamProgressBar* Bar = Rig.MakeControl<UDreamProgressBar>(TEXT("Loading"), nullptr, FVector2D(300.0, 30.0));
	if (!TestNotNull(TEXT("The progress bar was built"), Bar))
	{
		return false;
	}
	Bar->SetPercent(0.4f);
	Rig.PumpFrames(2);

	FDreamElementRef BarElement = Rig.Driver()->Find(FDreamBy::Name(TEXT("Loading")));
	if (!TestTrue(TEXT("The bar is findable by its name"), BarElement->Exists()))
	{
		return false;
	}
	// Everything a pointer can do over it: a click, a drag along it (the gesture that would move a
	// slider), and a wheel notch.
	TestTrue(TEXT("Clicking the bar completes"), BarElement->Click());
	TestTrue(TEXT("Dragging along the bar completes"), BarElement->DragBy(FVector2D(120.0, 0.0)));
	TestTrue(TEXT("Turning the wheel over the bar completes"), BarElement->ScrollBy(WheelTowardUser));

	TestNearlyEqual(TEXT("The percent is what code set; nothing the pointer did moved it"), Bar->GetPercent(), 0.4f, 0.000001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamProgressBarInteractionCoversButtonTest,
	"DreamGUI.ProgressBar.ABarDrawnOverAButtonTakesItsClickUntilTheBarIsNotHitTestable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamProgressBarInteractionCoversButtonTest::RunTest(const FString& Parameters)
{
	using namespace DreamProgressBarInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}

	// Both centred on the root; the bar is made SECOND, so it is the later sibling -- drawn over the
	// button and, for the raycaster's ordering (the flattened hierarchy index), in front of it.
	UDreamButton* Button = Rig.MakeControl<UDreamButton>(TEXT("Submit"), nullptr, FVector2D(200.0, 60.0));
	UDreamProgressBar* Bar = Rig.MakeControl<UDreamProgressBar>(TEXT("Loading"), nullptr, FVector2D(300.0, 40.0));
	if (!TestNotNull(TEXT("The button was built"), Button)
		|| !TestNotNull(TEXT("The progress bar was built"), Bar))
	{
		return false;
	}
	Bar->SetPercent(0.5f);
	Rig.PumpFrames(2);
	TestTrue(TEXT("The bar starts Visible, which is hit-testable, as UMG's does"), Bar->GetVisibility() == EDreamWidgetVisibility::Visible);

	TStrongObjectPtr<UDreamDragInteractionProbe> Clicks(NewObject<UDreamDragInteractionProbe>());
	Button->OnClicked.AddDynamic(Clicks.Get(), &UDreamDragInteractionProbe::RecordSignal);

	// Aimed at the BUTTON: the user clicks where the button is, and whatever is on top there answers.
	FDreamElementRef ButtonElement = Rig.Driver()->Find(FDreamBy::Widget(Button));
	TestTrue(TEXT("Clicking where the button is completes"), ButtonElement->Click());
	TestEqual(TEXT("The visible bar on top took the click, and the button heard nothing"), Clicks->Signals, 0);

	// UMG's Not Hit-Testable (Self & Children): the bar is still drawn, but the pointer no longer finds it.
	Bar->SetVisibility(EDreamWidgetVisibility::HitTestInvisible);
	Rig.PumpFrames(1);
	TestTrue(TEXT("Clicking the same place again completes"), ButtonElement->Click());
	TestEqual(TEXT("With the bar not hit-testable, the click fell through to the button"), Clicks->Signals, 1);
	return true;
}

#endif
