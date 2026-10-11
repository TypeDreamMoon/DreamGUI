// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamButton.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamGUISettings.h"
#include "Interaction/DreamUIVirtualCursor.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Driver/DreamDriverTypes.h"
#include "Interaction/DreamPressInteractionTestTypes.h"

/*
 * THE GAMEPAD'S CURSOR OVER A DISABLED BUTTON, AND ON A SCALED CANVAS.
 *
 * Slate's analog cursor (FAnalogCursor, Slate/Private/Framework/Application/AnalogCursor.cpp) moves the user's one cursor by
 * the stick -- MaxSpeed in desktop pixels a second, whatever the DPI scale (UpdateCursorPosition, :365-376) -- and turns the
 * pad's accept into the left mouse button at the cursor (HandleKeyDownEvent). It is the same cursor the mouse moves, so the
 * cursor starts wherever the mouse left it. What the press lands on is decided by the hit test like any mouse press, which
 * cuts the path at a disabled widget (FHittestGrid::GetBubblePath): the confirm over a disabled button clicks nothing, and
 * clicks it once it is enabled.
 *
 * On a canvas scaled to half and to double, the stick moves the cursor by the same pixels -- the cursor's speed is the
 * settings' VirtualCursorSpeed, viewport pixels a second, like MaxSpeed -- and the confirm clicks what is drawn under it.
 */
namespace DreamVirtualCursorCoverageTestLocal
{
	const FIntPoint ViewportSize(1280, 720);
	const double Scales[] = { 0.5, 2.0 };

	FDreamRigOptions ScaledOptions(double InScale)
	{
		FDreamRigOptions Options;
		Options.ViewportSize = ViewportSize;
		Options.CanvasScaleMode = EDreamCanvasScaleMode::ScaleWithScreenSize;
		Options.ReferenceResolution = FVector2D(ViewportSize.X / InScale, ViewportSize.Y / InScale);
		Options.MatchFromWidthToHeight = 1.0f;
		return Options;
	}

	FString At(double InScale, const FString& InWhat)
	{
		return FString::Printf(TEXT("[scale %.1f] %s"), InScale, *InWhat);
	}

	bool ComeUp(FAutomationTestBase& InTest, FDreamDriverRig& InRig, double InScale)
	{
		InRig.BindTest(&InTest);
		return InTest.TestTrue(At(InScale, TEXT("The rig came up")), InRig.IsUsable())
			&& InTest.TestNearlyEqual(*At(InScale, TEXT("The canvas is at the scale asked for")),
				static_cast<double>(InRig.RootCanvas()->GetCanvasScale()), InScale, 1.0e-3);
	}

	/** The canvas's anchored position (centre-relative, Y up) of the viewport pixel InPixel, at InScale. */
	FVector2D CanvasPositionOfPixel(const FVector2D& InPixel, double InScale)
	{
		return FVector2D((InPixel.X - ViewportSize.X * 0.5) / InScale, (ViewportSize.Y * 0.5 - InPixel.Y) / InScale);
	}

	/** A button whose clicks InListener counts, InPixelSize pixels across at InScale, centred on the viewport pixel InPixel. */
	UDreamButton* PlaceButtonAtPixel(FDreamDriverRig& InRig, const TCHAR* InName, const FVector2D& InPixel, const FVector2D& InPixelSize,
		double InScale, UDreamPressInteractionListener* InListener)
	{
		UDreamButton* Button = InRig.MakeControl<UDreamButton>(InName, nullptr, InPixelSize / InScale, CanvasPositionOfPixel(InPixel, InScale));
		if (Button != nullptr && InListener != nullptr)
		{
			Button->OnClicked.AddDynamic(InListener, &UDreamPressInteractionListener::HandleClicked);
		}
		return Button;
	}

	float CursorSpeed()
	{
		return UDreamGUISettings::Get()->VirtualCursorSpeed;
	}

	/** Player 0's cursor position, or unset while there is no cursor service or the cursor is off. */
	TOptional<FVector2D> CursorPixel(const FDreamDriverRig& InRig)
	{
		const UDreamUIVirtualCursorSubsystem* Cursor = UDreamUIVirtualCursorSubsystem::Get(InRig.GetWorld());
		if (Cursor == nullptr || !Cursor->IsVirtualCursorActiveForUser(0))
		{
			return TOptional<FVector2D>();
		}
		return Cursor->GetVirtualCursorPositionForUser(0);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCoverageVirtualCursorDisabledStickTest,
	"DreamGUI.VirtualCursor.TheStickTakesTheCursorOntoADisabledButtonWhoseConfirmClicksNothingUntilTheButtonIsEnabled",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamCoverageVirtualCursorDisabledStickTest, "DreamGUI.VirtualCursor.TheStickTakesTheCursorOntoADisabledButtonWhoseConfirmClicksNothingUntilTheButtonIsEnabled", "[Nav][Disabled]")

/*
 * The cursor turned on left of a disabled button and the stick pushed right long enough to carry it onto the button. The
 * confirm there clicks nothing. Enabled, the same confirm clicks it once.
 */
bool FDreamCoverageVirtualCursorDisabledStickTest::RunTest(const FString& Parameters)
{
	using namespace DreamVirtualCursorCoverageTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	// The cursor reads the stick from player 0's controller.
	Rig.EnsureGameInputHost();
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	const FVector2D Start(400.0, 360.0);
	const FVector2D Target(800.0, 360.0);
	UDreamButton* Button = PlaceButtonAtPixel(Rig, TEXT("Locked"), Target, FVector2D(240.0, 120.0), 1.0, Listener.Get());
	if (!TestNotNull(TEXT("A button came up where the cursor is going"), Button))
	{
		return false;
	}
	Button->SetIsEnabled(false);
	Rig.PumpFrames(2);
	const float Seconds = static_cast<float>((Target.X - Start.X) / CursorSpeed());

	TestTrue(TEXT("The cursor turned on left of the button and pushed right onto it completes"),
		Rig.Driver()->Sequence().MoveToPixel(Start).ActivateVirtualCursor().VirtualCursorStick(FVector2D(1.0, 0.0), Seconds).WaitFrames(1).Perform());
	const TOptional<FBox2D> ButtonRect = Rig.Driver()->Find(FDreamBy::Widget(Button))->GetPixelRect();
	const TOptional<FVector2D> Cursor = CursorPixel(Rig);
	if (!TestTrue(TEXT("The cursor is on and over the button"), ButtonRect.IsSet() && Cursor.IsSet() && ButtonRect->IsInside(Cursor.GetValue())))
	{
		return false;
	}
	TestTrue(TEXT("The confirm pressed and let go completes"), Rig.Driver()->Sequence().VirtualCursorPress().VirtualCursorRelease().WaitFrames(1).Perform());
	TestEqual(TEXT("The confirm over the disabled button clicked nothing"), Listener->ClickedCount, 0);

	Button->SetIsEnabled(true);
	Rig.PumpFrames(1);
	TestTrue(TEXT("The confirm pressed and let go again completes"), Rig.Driver()->Sequence().VirtualCursorPress().VirtualCursorRelease().WaitFrames(1).Perform());
	TestEqual(TEXT("Enabled, the confirm clicked it once"), Listener->ClickedCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCoverageVirtualCursorScaledStickTest,
	"DreamGUI.VirtualCursor.OnACanvasScaledToHalfAndToDoubleTheStickMovesTheCursorByTheSamePixelsAndConfirmClicksWhatIsDrawnThere",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamCoverageVirtualCursorScaledStickTest, "DreamGUI.VirtualCursor.OnACanvasScaledToHalfAndToDoubleTheStickMovesTheCursorByTheSamePixelsAndConfirmClicksWhatIsDrawnThere", "[Nav][Scaled]")

/*
 * At half and at double: a button where the cursor starts and one a quarter of a second of full stick to its right, both
 * placed in pixels. The stick moves the cursor about a quarter of a second's worth of pixels -- the same at either scale --
 * and the confirm clicks the button drawn there, not the one it left.
 */
bool FDreamCoverageVirtualCursorScaledStickTest::RunTest(const FString& Parameters)
{
	using namespace DreamVirtualCursorCoverageTestLocal;
	const float Seconds = 0.25f;
	for (const double Scale : Scales)
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(ScaledOptions(Scale));
		if (!ComeUp(*this, Rig, Scale))
		{
			return false;
		}
		Rig.EnsureGameInputHost();
		TStrongObjectPtr<UDreamPressInteractionListener> StartListener(NewObject<UDreamPressInteractionListener>());
		TStrongObjectPtr<UDreamPressInteractionListener> TargetListener(NewObject<UDreamPressInteractionListener>());
		const double Travel = CursorSpeed() * Seconds;
		const FVector2D Start(320.0, 360.0);
		const FVector2D Target(Start.X + Travel, 360.0);
		// Wide enough to hold the cursor a frame or two either side of where a quarter of a second takes it.
		UDreamButton* StartButton = PlaceButtonAtPixel(Rig, TEXT("Start"), Start, FVector2D(120.0, 80.0), Scale, StartListener.Get());
		UDreamButton* TargetButton = PlaceButtonAtPixel(Rig, TEXT("Target"), Target, FVector2D(160.0, 80.0), Scale, TargetListener.Get());
		if (!TestTrue(At(Scale, TEXT("A button where the cursor starts and one where it is going came up")), StartButton != nullptr && TargetButton != nullptr))
		{
			return false;
		}
		Rig.PumpFrames(2);

		TestTrue(At(Scale, TEXT("The cursor turned on at the start and pushed right completes")),
			Rig.Driver()->Sequence().MoveToPixel(Start).ActivateVirtualCursor().VirtualCursorStick(FVector2D(1.0, 0.0), Seconds).WaitFrames(1).Perform());
		const TOptional<FVector2D> Cursor = CursorPixel(Rig);
		if (!TestTrue(At(Scale, TEXT("The cursor is on")), Cursor.IsSet()))
		{
			return false;
		}
		const double Moved = Cursor->X - Start.X;
		TestTrue(*At(Scale, FString::Printf(TEXT("The cursor moved about %.0f pixels, the same at any scale (it moved %.1f)"), Travel, Moved)),
			FMath::Abs(Moved - Travel) <= 0.2 * Travel);
		TestNearlyEqual(*At(Scale, TEXT("...straight across")), Cursor->Y, Start.Y, 1.0);

		TestTrue(At(Scale, TEXT("The confirm pressed and let go completes")), Rig.Driver()->Sequence().VirtualCursorPress().VirtualCursorRelease().WaitFrames(1).Perform());
		TestEqual(*At(Scale, TEXT("The confirm clicked the button drawn under the cursor")), TargetListener->ClickedCount, 1);
		TestEqual(*At(Scale, TEXT("...and not the one it started on")), StartListener->ClickedCount, 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCoverageVirtualCursorDisabledFromMouseTest,
	"DreamGUI.VirtualCursor.TurnedOnWhereTheMouseRestsOnADisabledButtonItsConfirmClicksNothingUntilTheButtonIsEnabled",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamCoverageVirtualCursorDisabledFromMouseTest, "DreamGUI.VirtualCursor.TurnedOnWhereTheMouseRestsOnADisabledButtonItsConfirmClicksNothingUntilTheButtonIsEnabled", "[Pointer][Disabled]")

/*
 * The mouse rests on a disabled button and clicks it: nothing. The cursor turned on there starts at the mouse's pixel, and
 * its confirm clicks nothing either. Enabled, the confirm clicks the button once.
 */
bool FDreamCoverageVirtualCursorDisabledFromMouseTest::RunTest(const FString& Parameters)
{
	using namespace DreamVirtualCursorCoverageTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	Rig.EnsureGameInputHost();
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	UDreamButton* Button = PlaceButtonAtPixel(Rig, TEXT("Locked"), FVector2D(700.0, 300.0), FVector2D(200.0, 80.0), 1.0, Listener.Get());
	if (!TestNotNull(TEXT("A button came up"), Button))
	{
		return false;
	}
	Button->SetIsEnabled(false);
	Rig.PumpFrames(2);
	FDreamElementRef ButtonElement = Rig.Driver()->Find(FDreamBy::Widget(Button));
	const TOptional<FVector2D> OnButton = ButtonElement->GetCentrePixel();
	if (!TestTrue(TEXT("The button is on the viewport"), OnButton.IsSet()))
	{
		return false;
	}

	TestTrue(TEXT("The mouse clicking the disabled button completes"), ButtonElement->Click());
	TestEqual(TEXT("The mouse's click did nothing"), Listener->ClickedCount, 0);
	TestTrue(TEXT("The cursor turned on where the mouse rests completes"), Rig.Driver()->Sequence().ActivateVirtualCursor().WaitFrames(1).Perform());
	const TOptional<FVector2D> Cursor = CursorPixel(Rig);
	if (!TestTrue(TEXT("The cursor is on, at the mouse's pixel"), Cursor.IsSet() && Cursor->Equals(OnButton.GetValue(), 1.0)))
	{
		return false;
	}
	TestTrue(TEXT("The confirm pressed and let go completes"), Rig.Driver()->Sequence().VirtualCursorPress().VirtualCursorRelease().WaitFrames(1).Perform());
	TestEqual(TEXT("The cursor's confirm over the disabled button clicked nothing"), Listener->ClickedCount, 0);

	Button->SetIsEnabled(true);
	Rig.PumpFrames(1);
	TestTrue(TEXT("The confirm pressed and let go again completes"), Rig.Driver()->Sequence().VirtualCursorPress().VirtualCursorRelease().WaitFrames(1).Perform());
	TestEqual(TEXT("Enabled, the confirm clicked it once"), Listener->ClickedCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCoverageVirtualCursorScaledFromMouseTest,
	"DreamGUI.VirtualCursor.OnACanvasScaledToHalfAndToDoubleTheCursorTurnedOnWhereTheMouseRestsClicksTheButtonUnderIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamCoverageVirtualCursorScaledFromMouseTest, "DreamGUI.VirtualCursor.OnACanvasScaledToHalfAndToDoubleTheCursorTurnedOnWhereTheMouseRestsClicksTheButtonUnderIt", "[Pointer][Scaled]")

/*
 * At half and at double: the mouse rests on a button off the canvas's centre, a second button beside it. The cursor turned on
 * there starts at the mouse's pixel and its confirm clicks the button under it, not the one beside it.
 */
bool FDreamCoverageVirtualCursorScaledFromMouseTest::RunTest(const FString& Parameters)
{
	using namespace DreamVirtualCursorCoverageTestLocal;
	for (const double Scale : Scales)
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(ScaledOptions(Scale));
		if (!ComeUp(*this, Rig, Scale))
		{
			return false;
		}
		Rig.EnsureGameInputHost();
		TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
		TStrongObjectPtr<UDreamPressInteractionListener> BesideListener(NewObject<UDreamPressInteractionListener>());
		UDreamButton* Button = Rig.MakeControl<UDreamButton>(TEXT("Under"), nullptr, FVector2D(140.0, 60.0), FVector2D(-100.0, 50.0));
		UDreamButton* Beside = Rig.MakeControl<UDreamButton>(TEXT("Beside"), nullptr, FVector2D(140.0, 60.0), FVector2D(100.0, 50.0));
		if (!TestTrue(At(Scale, TEXT("Two buttons came up")), Button != nullptr && Beside != nullptr))
		{
			return false;
		}
		Button->OnClicked.AddDynamic(Listener.Get(), &UDreamPressInteractionListener::HandleClicked);
		Beside->OnClicked.AddDynamic(BesideListener.Get(), &UDreamPressInteractionListener::HandleClicked);
		Rig.PumpFrames(2);
		FDreamElementRef ButtonElement = Rig.Driver()->Find(FDreamBy::Widget(Button));
		const TOptional<FVector2D> OnButton = ButtonElement->GetCentrePixel();
		if (!TestTrue(At(Scale, TEXT("The button is on the viewport")), OnButton.IsSet()))
		{
			return false;
		}

		TestTrue(At(Scale, TEXT("The mouse moving onto the button completes")), ButtonElement->Hover());
		TestTrue(At(Scale, TEXT("The cursor turned on there completes")), Rig.Driver()->Sequence().ActivateVirtualCursor().WaitFrames(1).Perform());
		const TOptional<FVector2D> Cursor = CursorPixel(Rig);
		TestTrue(At(Scale, TEXT("The cursor starts at the mouse's pixel")), Cursor.IsSet() && Cursor->Equals(OnButton.GetValue(), 1.0));
		TestTrue(At(Scale, TEXT("The confirm pressed and let go completes")), Rig.Driver()->Sequence().VirtualCursorPress().VirtualCursorRelease().WaitFrames(1).Perform());
		TestEqual(*At(Scale, TEXT("The confirm clicked the button under the cursor once")), Listener->ClickedCount, 1);
		TestEqual(*At(Scale, TEXT("...and not the one beside it")), BesideListener->ClickedCount, 0);
	}
	return true;
}

#endif
