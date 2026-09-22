// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Controls/DreamSpinBox.h"
#include "Core/Components/DreamWidget.h"
#include "UObject/StrongObjectPtr.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Interaction/DreamDragInteractionTestTypes.h"

/*
 * THE SPIN BOX SCRUB UNDER A REAL POINTER.
 *
 * Dragging only: typing into the field belongs to the text tests (DreamSpinBoxTypingInteractionAutomationTests.cpp).
 * The reference is Slate's SSpinBox, which UMG's USpinBox wraps. For a bounded range its OnMouseMove:
 *  - before the drag has begun, only adds |dx| to DistanceDragged; once that passes the drag trigger
 *    distance it begins the drag (OnBeginSliderMovement) -- and THAT move does not change the value;
 *  - after, adds dx * Step to the filled fraction of a slider max(width, 100) wide and reads the value
 *    back off it: (max - min) / max(width, 100) of value per pixel, where Step is 1 for any range wider
 *    than 10 (GetDefaultStepSize) and the fraction is clamped, so the value stops at either end;
 *  - on release, commits once (OnValueCommitted, then OnEndSliderMovement). A press that never
 *    travelled the trigger distance is not a scrub at all and commits nothing.
 *
 * Snapping to Delta is deliberately out of this file. SSpinBox snaps a spun value to Delta; this
 * library's StepSize is UMG's Delta, and its documentation says that with bAlwaysUsesDeltaSnap off (the
 * default) a drag is free. The range and width below make every expected value a whole number anyway,
 * so neither reading changes an assertion here.
 */
namespace DreamSpinBoxInteractionTestLocal
{
	const FIntPoint ViewportSize(1280, 720);

	/** A spin box 400 wide over 0..400: one unit of value per pixel of scrub, whichever rule counts it. */
	UDreamSpinBox* MakeSpinBox(FDreamDriverRig& InRig, float InStartValue)
	{
		UDreamSpinBox* SpinBox = InRig.MakeControl<UDreamSpinBox>(TEXT("Amount"), nullptr, FVector2D(400.0, 40.0));
		if (SpinBox != nullptr)
		{
			// Range before value, so the value is clamped against the range it is meant for.
			SpinBox->SetMinValue(0.0f);
			SpinBox->SetMaxValue(400.0f);
			SpinBox->SetValue(InStartValue);
		}
		InRig.PumpFrames(2);
		return SpinBox;
	}

	/** What the spin box said while a gesture ran, one probe per event. Bound AFTER setup. */
	struct FSpinBoxLog
	{
		TStrongObjectPtr<UDreamDragInteractionProbe> Changes;
		TStrongObjectPtr<UDreamDragInteractionProbe> Commits;
		TStrongObjectPtr<UDreamDragInteractionProbe> ScrubBegins;
		TStrongObjectPtr<UDreamDragInteractionProbe> ScrubEnds;

		explicit FSpinBoxLog(UDreamSpinBox* InSpinBox)
			: Changes(NewObject<UDreamDragInteractionProbe>())
			, Commits(NewObject<UDreamDragInteractionProbe>())
			, ScrubBegins(NewObject<UDreamDragInteractionProbe>())
			, ScrubEnds(NewObject<UDreamDragInteractionProbe>())
		{
			InSpinBox->OnValueChanged.AddDynamic(Changes.Get(), &UDreamDragInteractionProbe::RecordFloat);
			InSpinBox->OnValueCommitted.AddDynamic(Commits.Get(), &UDreamDragInteractionProbe::RecordFloat);
			InSpinBox->OnBeginSliderMovement.AddDynamic(ScrubBegins.Get(), &UDreamDragInteractionProbe::RecordFloat);
			InSpinBox->OnEndSliderMovement.AddDynamic(ScrubEnds.Get(), &UDreamDragInteractionProbe::RecordFloat);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSpinBoxInteractionScrubTest,
	"DreamGUI.SpinBox.ScrubbingMovesTheValueByTheDistanceTravelledAfterTheDragBeganAndCommitsOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamSpinBoxInteractionScrubTest::RunTest(const FString& Parameters)
{
	using namespace DreamSpinBoxInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}

	const float StartValue = 100.0f;
	UDreamSpinBox* SpinBox = MakeSpinBox(Rig, StartValue);
	if (!TestNotNull(TEXT("The spin box was built"), SpinBox)
		|| !TestNotNull(TEXT("It has a field"), SpinBox->FieldNode.Get()))
	{
		return false;
	}
	TestNearlyEqual(TEXT("The spin box starts where it was set"), SpinBox->GetValue(), StartValue, 0.0001f);
	TestTrue(TEXT("Scrubbing is on by default"), SpinBox->GetEnableSlider());

	FDreamDriverRef Driver = Rig.Driver();
	const TOptional<FBox2D> Whole = Driver->Find(FDreamBy::Widget(SpinBox))->GetPixelRect();
	if (!TestTrue(TEXT("The spin box is on screen"), Whole.IsSet()))
	{
		return false;
	}
	const double WidthPixels = Whole->Max.X - Whole->Min.X;
	const double ValuePerPixel = (SpinBox->GetSliderMaxValue() - SpinBox->GetSliderMinValue()) / FMath::Max(WidthPixels, 100.0);

	FSpinBoxLog Log(SpinBox);
	// A hundred pixels to the right in three moves: 10, then 45, then 45. The first is past both drag
	// thresholds at once -- the raycaster's 5 canvas units and Slate's 5 pixel drag trigger distance --
	// so it is the move SSpinBox spends deciding this IS a drag, and the ninety after it move the value.
	TestTrue(TEXT("The scrub completes"),
		Driver->Sequence()
			.MoveTo(FDreamBy::Widget(SpinBox->FieldNode.Get()))
			.Press()
			.MoveBy(FVector2D(10.0, 0.0))
			.MoveBy(FVector2D(45.0, 0.0))
			.MoveBy(FVector2D(45.0, 0.0))
			.WaitFrames(1)
			.Release()
			.Perform());

	const float Expected = StartValue + static_cast<float>(90.0 * ValuePerPixel);
	TestNearlyEqual(
		FString::Printf(TEXT("The value moved by the ninety pixels travelled after the drag began, at %.3f per pixel"), ValuePerPixel),
		SpinBox->GetValue(), Expected, static_cast<float>(ValuePerPixel));
	TestTrue(TEXT("The value changed while the scrub ran"), Log.Changes->NumFloats() >= 1);
	TestEqual(TEXT("Letting go committed once"), Log.Commits->NumFloats(), 1);
	TestNearlyEqual(TEXT("The commit carried the value the scrub ended on"), Log.Commits->LastFloat(-1.0f), SpinBox->GetValue(), 0.0001f);
	TestEqual(TEXT("The scrub began once"), Log.ScrubBegins->NumFloats(), 1);
	TestEqual(TEXT("and ended once"), Log.ScrubEnds->NumFloats(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSpinBoxInteractionScrubClampTest,
	"DreamGUI.SpinBox.ScrubbingPastEitherEndOfTheRangeStopsAtThatEnd",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamSpinBoxInteractionScrubClampTest::RunTest(const FString& Parameters)
{
	using namespace DreamSpinBoxInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}

	// Fifty short of the top, so the first scrub has far more travel than range left.
	UDreamSpinBox* SpinBox = MakeSpinBox(Rig, 350.0f);
	if (!TestNotNull(TEXT("The spin box was built"), SpinBox)
		|| !TestNotNull(TEXT("It has a field"), SpinBox->FieldNode.Get()))
	{
		return false;
	}
	const float Low = SpinBox->GetMinValue();
	const float High = SpinBox->GetMaxValue();

	FSpinBoxLog Log(SpinBox);
	FDreamElementRef Field = Rig.Driver()->Find(FDreamBy::Widget(SpinBox->FieldNode.Get()));

	// Three hundred pixels right: two hundred and fifty more than there is range for.
	TestTrue(TEXT("The scrub to the right completes"), Field->DragBy(FVector2D(300.0, 0.0)));
	TestNearlyEqual(TEXT("It stopped at the maximum"), SpinBox->GetValue(), High, 0.0001f);

	// Six hundred pixels left from the field's middle: still inside the viewport, far past the bottom.
	TestTrue(TEXT("The scrub to the left completes"), Field->DragBy(FVector2D(-600.0, 0.0)));
	TestNearlyEqual(TEXT("It stopped at the minimum"), SpinBox->GetValue(), Low, 0.0001f);

	TestTrue(TEXT("No value reported on the way left the range"), Log.Changes->AllFloatsWithin(Low, High, 0.0001f));
	TestTrue(TEXT("No value committed left the range"), Log.Commits->AllFloatsWithin(Low, High, 0.0001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSpinBoxInteractionClickIsNotScrubTest,
	"DreamGUI.SpinBox.AClickThatNeverMovesIsNotAScrubAndLeavesTheValueAlone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamSpinBoxInteractionClickIsNotScrubTest::RunTest(const FString& Parameters)
{
	using namespace DreamSpinBoxInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}

	const float StartValue = 100.0f;
	UDreamSpinBox* SpinBox = MakeSpinBox(Rig, StartValue);
	if (!TestNotNull(TEXT("The spin box was built"), SpinBox)
		|| !TestNotNull(TEXT("It has a field"), SpinBox->FieldNode.Get()))
	{
		return false;
	}

	FSpinBoxLog Log(SpinBox);
	// A press and a release in the same place. What the click DOES to the field -- SSpinBox enters text
	// mode -- is the text tests' business; this pins what it does not do.
	TestTrue(TEXT("The click completes"), Rig.Driver()->Find(FDreamBy::Widget(SpinBox->FieldNode.Get()))->Click());

	TestNearlyEqual(TEXT("The value is where it was"), SpinBox->GetValue(), StartValue, 0.0001f);
	TestEqual(TEXT("No value change was reported"), Log.Changes->NumFloats(), 0);
	TestEqual(TEXT("Nothing was committed"), Log.Commits->NumFloats(), 0);
	TestEqual(TEXT("No scrub began"), Log.ScrubBegins->NumFloats(), 0);
	return true;
}

#endif
