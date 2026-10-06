// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamSpinBox.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamScreenSpaceRaycaster.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Interaction/DreamDragInteractionTestTypes.h"

/*
 * THE SPIN BOX SCRUB UNDER A FINGER, AND DISABLED.
 *
 * SSpinBox has no touch handlers: a finger reaches OnMouseButtonDown, OnMouseMove and OnMouseButtonUp through Slate's touch
 * fallback (bTouchFallbackToMouse), and those treat it as the mouse -- the only branch on IsTouchEvent is that the cursor is
 * not warped back on release (Slate/Private/Widgets/Input/SSpinBox.cpp:375). So a finger scrubs by the same rule as the mouse
 * (see DreamSpinBoxInteractionAutomationTests.cpp): the move that crosses the drag distance begins the scrub and moves
 * nothing, every pixel after moves the value by (max - min) / max(width, 100), and the lift commits once. A disabled spin box
 * is not on Slate's hit path at all (FHittestGrid::GetBubblePath), so a scrub across it changes nothing.
 */
namespace DreamSpinBoxTouchTestLocal
{
	const FIntPoint ViewportSize(1280, 720);

	/** A spin box 400 wide over 0..400 at InStartValue: one unit of value per pixel of scrub. */
	UDreamSpinBox* MakeSpinBox(FAutomationTestBase& InTest, FDreamDriverRig& InRig, float InStartValue)
	{
		InRig.BindTest(&InTest);
		if (!InTest.TestTrue(TEXT("The headless rig came up"), InRig.IsUsable()))
		{
			return nullptr;
		}
		UDreamSpinBox* SpinBox = InRig.MakeControl<UDreamSpinBox>(TEXT("Amount"), nullptr, FVector2D(400.0, 40.0));
		if (SpinBox != nullptr)
		{
			SpinBox->SetMinValue(0.0f);
			SpinBox->SetMaxValue(400.0f);
			SpinBox->SetValue(InStartValue);
		}
		InRig.PumpFrames(2);
		return InTest.TestTrue(TEXT("The spin box was built with a field"), SpinBox != nullptr && SpinBox->FieldNode != nullptr) ? SpinBox : nullptr;
	}

	struct FSpinBoxLog
	{
		TStrongObjectPtr<UDreamDragInteractionProbe> Changes;
		TStrongObjectPtr<UDreamDragInteractionProbe> Commits;
		TStrongObjectPtr<UDreamDragInteractionProbe> ScrubBegins;

		explicit FSpinBoxLog(UDreamSpinBox* InSpinBox)
			: Changes(NewObject<UDreamDragInteractionProbe>())
			, Commits(NewObject<UDreamDragInteractionProbe>())
			, ScrubBegins(NewObject<UDreamDragInteractionProbe>())
		{
			InSpinBox->OnValueChanged.AddDynamic(Changes.Get(), &UDreamDragInteractionProbe::RecordFloat);
			InSpinBox->OnValueCommitted.AddDynamic(Commits.Get(), &UDreamDragInteractionProbe::RecordFloat);
			InSpinBox->OnBeginSliderMovement.AddDynamic(ScrubBegins.Get(), &UDreamDragInteractionProbe::RecordFloat);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSpinBoxFingerScrubTest,
	"DreamGUI.SpinBox.AFingerScrubbingTheFieldMovesTheValueAsTheMouseDoesAndCommitsOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamSpinBoxFingerScrubTest, "DreamGUI.SpinBox.AFingerScrubbingTheFieldMovesTheValueAsTheMouseDoesAndCommitsOnce", "[Touch][Animated]")

bool FDreamSpinBoxFingerScrubTest::RunTest(const FString& Parameters)
{
	using namespace DreamSpinBoxTouchTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const float StartValue = 100.0f;
	UDreamSpinBox* SpinBox = MakeSpinBox(*this, Rig, StartValue);
	if (SpinBox == nullptr)
	{
		return false;
	}
	FDreamDriverRef Driver = Rig.Driver();
	const TOptional<FBox2D> Whole = Driver->Find(FDreamBy::Widget(SpinBox))->GetPixelRect();
	const TOptional<FVector2D> Field = Driver->Find(FDreamBy::Widget(SpinBox->FieldNode.Get()))->GetCentrePixel();
	if (!TestTrue(TEXT("The spin box and its field are on screen"), Whole.IsSet() && Field.IsSet()))
	{
		return false;
	}
	const double ValuePerPixel = (SpinBox->GetSliderMaxValue() - SpinBox->GetSliderMinValue()) / FMath::Max(Whole->Max.X - Whole->Min.X, 100.0);
	const double FirstMove = FMath::Sqrt(static_cast<double>(Rig.Raycaster()->GetScaledDragThresholdSquare())) + 2.0;
	const FVector2D Landed = Field.GetValue();

	FSpinBoxLog Log(SpinBox);
	TestTrue(TEXT("The finger's scrub completes"),
		Driver->Sequence()
			.TouchDown(0, Landed)
			.TouchMoveTo(0, Landed + FVector2D(FirstMove, 0.0))
			.TouchMoveTo(0, Landed + FVector2D(FirstMove + 45.0, 0.0))
			.TouchMoveTo(0, Landed + FVector2D(FirstMove + 90.0, 0.0))
			.WaitFrames(1)
			.TouchUp(0)
			.Perform());

	const float Expected = StartValue + static_cast<float>(90.0 * ValuePerPixel);
	TestNearlyEqual(FString::Printf(TEXT("The value moved by the ninety pixels the finger travelled after the scrub began (%.3f a pixel)"), ValuePerPixel),
		SpinBox->GetValue(), Expected, static_cast<float>(ValuePerPixel));
	TestEqual(TEXT("The scrub began once"), Log.ScrubBegins->NumFloats(), 1);
	TestEqual(TEXT("The lift committed once"), Log.Commits->NumFloats(), 1);
	TestNearlyEqual(TEXT("...the value the scrub ended on"), Log.Commits->LastFloat(-1.0f), SpinBox->GetValue(), 0.0001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSpinBoxDisabledScrubTest,
	"DreamGUI.SpinBox.ADisabledSpinBoxIgnoresAScrubAcrossIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamSpinBoxDisabledScrubTest, "DreamGUI.SpinBox.ADisabledSpinBoxIgnoresAScrubAcrossIt", "[Pointer][Disabled]")

bool FDreamSpinBoxDisabledScrubTest::RunTest(const FString& Parameters)
{
	using namespace DreamSpinBoxTouchTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const float StartValue = 100.0f;
	UDreamSpinBox* SpinBox = MakeSpinBox(*this, Rig, StartValue);
	if (SpinBox == nullptr)
	{
		return false;
	}
	// UMG's SetIsEnabled.
	SpinBox->SetIsEnabled(false);
	Rig.PumpFrames(1);
	FSpinBoxLog Log(SpinBox);
	FDreamElementRef Field = Rig.Driver()->Find(FDreamBy::Widget(SpinBox->FieldNode.Get()));

	TestTrue(TEXT("A scrub across the disabled spin box completes"), Field->DragBy(FVector2D(120.0, 0.0)));
	TestNearlyEqual(TEXT("The value is where it was"), SpinBox->GetValue(), StartValue, 0.0001f);
	TestEqual(TEXT("No change was reported"), Log.Changes->NumFloats(), 0);
	TestEqual(TEXT("Nothing was committed"), Log.Commits->NumFloats(), 0);
	TestEqual(TEXT("No scrub began"), Log.ScrubBegins->NumFloats(), 0);

	SpinBox->SetIsEnabled(true);
	Rig.PumpFrames(1);
	TestTrue(TEXT("The same scrub with the spin box enabled again completes"), Field->DragBy(FVector2D(120.0, 0.0)));
	TestTrue(TEXT("Enabled again, the scrub moves the value"), SpinBox->GetValue() > StartValue + 1.0f);
	return true;
}

#endif
