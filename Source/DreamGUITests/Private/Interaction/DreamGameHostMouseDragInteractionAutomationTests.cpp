// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamScrollBox.h"
#include "Controls/DreamSlider.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamScreenSpaceRaycaster.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "InputCoreTypes.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Driver/DreamDriverTypes.h"
#include "Interaction/DreamDragInteractionTestTypes.h"

/*
 * A MOUSE DRAG THAT ENTERS WHERE A PLAYER'S DOES.
 *
 * Driver.GameHost.* drives the controller's road with clicks, the wheel, keys and a finger's drag; a mouse button held
 * across moves never went that way. In a game the button reaches the player controller (UGameViewportClient::InputKey
 * hands it on), waits for the controller's input frame and is dispatched to the preset actor's binding, which presses the
 * module; the moves are the cursor the module reads every frame. So the button is down in the controller's input while
 * the pointer travels, and the drag is the control's from the first move past the drag distance to the release.
 *
 * What the drag does is UMG's, and the same claims the module-level tests make: SSlider follows the pointer absolutely
 * while it holds the capture, so a drag to the middle of the travel is a value of one half; SScrollBox's right-button
 * drag scrolls by every move's delta past the drag distance, the crossing move included. Every test runs once per preset.
 */
namespace DreamGameHostMouseDragTestLocal
{
	const FIntPoint ViewportSize(1280, 720);

	struct FHostCase
	{
		EDreamRigInputHost Host;
		const TCHAR* Name;
	};

	const FHostCase ActorHosts[] = {
		{ EDreamRigInputHost::StandaloneActor, TEXT("standalone input actor") },
		{ EDreamRigInputHost::EnhancedActor, TEXT("Enhanced Input actor") },
	};

	FDreamRigOptions OptionsFor(EDreamRigInputHost InHost)
	{
		FDreamRigOptions Options;
		Options.ViewportSize = ViewportSize;
		Options.InputHost = InHost;
		return Options;
	}

	FString Under(const FHostCase& InCase, const TCHAR* InWhat)
	{
		return FString::Printf(TEXT("[%s] %s"), InCase.Name, InWhat);
	}

	bool ComeUp(FAutomationTestBase& InTest, const FHostCase& InCase, FDreamDriverRig& InRig)
	{
		InRig.BindTest(&InTest);
		const FString& WhyNot = InRig.GetBuildFailure();
		return InTest.TestTrue(WhyNot.IsEmpty() ? Under(InCase, TEXT("The rig came up"))
			: FString::Printf(TEXT("[%s] The rig came up -- it did not: %s"), InCase.Name, *WhyNot), InRig.IsUsable());
	}

	/** Whether InKey is held down in the rig's player controller's own input right now. */
	bool ControllerHoldsKey(const FDreamDriverContext& InContext, const FKey& InKey)
	{
		return InContext.PlayerController != nullptr && InContext.PlayerController->IsInputKeyDown(InKey);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGameHostSliderMouseDragTest,
	"DreamGUI.Slider.ThroughThePlayerControllerDraggingTheHandleHalfwayAlongItsTravelMovesTheValueHalfway",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamGameHostSliderMouseDragTest, "DreamGUI.Slider.ThroughThePlayerControllerDraggingTheHandleHalfwayAlongItsTravelMovesTheValueHalfway", "[Pointer][Animated]")

/*
 * The handle pressed with the left button through the controller, carried to the middle of its travel over three moves,
 * and let go: the value is one half, it changed on the way, the slider's capture began once and ended once, and the left
 * mouse button was down in the controller's own input for the whole of the travel.
 */
bool FDreamGameHostSliderMouseDragTest::RunTest(const FString& Parameters)
{
	using namespace DreamGameHostMouseDragTestLocal;
	for (const FHostCase& Case : ActorHosts)
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(OptionsFor(Case.Host));
		if (!ComeUp(*this, Case, Rig))
		{
			continue;
		}
		UDreamSlider* Slider = Rig.MakeControl<UDreamSlider>(TEXT("Volume"), nullptr, FVector2D(400.0, 40.0));
		Rig.PumpFrames(2);
		if (!TestTrue(Under(Case, TEXT("The slider came up with a handle and a handle area")),
			Slider != nullptr && Slider->HandleNode != nullptr && Slider->HandleAreaNode != nullptr))
		{
			continue;
		}
		const TOptional<FBox2D> Travel = Rig.Driver()->Find(FDreamBy::Widget(Slider->HandleAreaNode.Get()))->GetPixelRect();
		const TOptional<FVector2D> Grip = Rig.Driver()->Find(FDreamBy::Widget(Slider->HandleNode.Get()))->GetCentrePixel();
		if (!TestTrue(Under(Case, TEXT("The handle and its travel are on screen")), Travel.IsSet() && Grip.IsSet()))
		{
			continue;
		}
		const double TravelLength = Travel->Max.X - Travel->Min.X;
		const float OnePixel = static_cast<float>(1.0 / FMath::Max(TravelLength, 1.0));
		TStrongObjectPtr<UDreamDragInteractionProbe> Values(NewObject<UDreamDragInteractionProbe>());
		TStrongObjectPtr<UDreamDragInteractionProbe> CaptureBegins(NewObject<UDreamDragInteractionProbe>());
		TStrongObjectPtr<UDreamDragInteractionProbe> CaptureEnds(NewObject<UDreamDragInteractionProbe>());
		Slider->OnValueChanged.AddDynamic(Values.Get(), &UDreamDragInteractionProbe::RecordFloat);
		Slider->OnMouseCaptureBegin.AddDynamic(CaptureBegins.Get(), &UDreamDragInteractionProbe::RecordSignal);
		Slider->OnMouseCaptureEnd.AddDynamic(CaptureEnds.Get(), &UDreamDragInteractionProbe::RecordSignal);

		const FVector2D Halfway(Travel->Min.X + 0.5 * TravelLength, Grip->Y);
		const FVector2D FirstMove = Grip.GetValue() + FVector2D(20.0, 0.0);
		bool bHeldThroughTheTravel = true;
		TestTrue(Under(Case, TEXT("The drag through the controller completes")),
			Rig.Driver()->Sequence()
				.MoveToPixel(Grip.GetValue())
				.Press()
				.MoveToPixel(FirstMove)
				.Then([&bHeldThroughTheTravel](FDreamDriverContext& InContext) { bHeldThroughTheTravel = bHeldThroughTheTravel && ControllerHoldsKey(InContext, EKeys::LeftMouseButton); })
				.MoveToPixel((FirstMove + Halfway) * 0.5)
				.Then([&bHeldThroughTheTravel](FDreamDriverContext& InContext) { bHeldThroughTheTravel = bHeldThroughTheTravel && ControllerHoldsKey(InContext, EKeys::LeftMouseButton); })
				.MoveToPixel(Halfway)
				.WaitFrames(1)
				.Release()
				.Then([this, &Case](FDreamDriverContext& InContext)
				{
					TestFalse(Under(Case, TEXT("After the release the left button is up in the controller's input")),
						ControllerHoldsKey(InContext, EKeys::LeftMouseButton));
				})
				.Perform());
		TestTrue(Under(Case, TEXT("The left button was down in the controller's own input all through the travel")), bHeldThroughTheTravel);
		TestNearlyEqual(*Under(Case, TEXT("The value is halfway")), Slider->GetValue(), 0.5f, 1.5f * OnePixel);
		TestTrue(Under(Case, TEXT("...having changed on the way")), Values->NumFloats() >= 2);
		TestEqual(Under(Case, TEXT("The press began one capture")), CaptureBegins->Signals, 1);
		TestEqual(Under(Case, TEXT("...and the release ended it, once")), CaptureEnds->Signals, 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGameHostScrollBoxRightDragTest,
	"DreamGUI.ScrollBox.ThroughThePlayerControllerARightButtonDragKeepsTheContentUnderThePointer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamGameHostScrollBoxRightDragTest, "DreamGUI.ScrollBox.ThroughThePlayerControllerARightButtonDragKeepsTheContentUnderThePointer", "[Pointer][Animated]")

/*
 * The right button through the controller -- the preset binds RightMouseButton pressed and released as it binds the left --
 * held while the pointer pulls the content up 150 pixels: the offset is the whole of the pull, read with the button still
 * down, and the button went through the controller.
 */
bool FDreamGameHostScrollBoxRightDragTest::RunTest(const FString& Parameters)
{
	using namespace DreamGameHostMouseDragTestLocal;
	for (const FHostCase& Case : ActorHosts)
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(OptionsFor(Case.Host));
		if (!ComeUp(*this, Case, Rig)
			|| !TestTrue(Under(Case, TEXT("Its UI has begun play, as a game's has")), DreamDragInteraction::BeginPlayForUI(Rig.GetWorld())))
		{
			continue;
		}
		UDreamScrollBox* Box = Rig.MakeControl<UDreamScrollBox>(TEXT("Box"), nullptr, FVector2D(300.0, 400.0));
		if (!TestTrue(Under(Case, TEXT("The box came up with a viewport and a content node")),
			Box != nullptr && Box->ViewportNode != nullptr && Box->GetContentNode() != nullptr))
		{
			continue;
		}
		for (int32 RowIndex = 0; RowIndex < 20; ++RowIndex)
		{
			Rig.MakeWidget(FString::Printf(TEXT("Box_Row%02d"), RowIndex), Box->GetContentNode(), FVector2D(300.0, 100.0));
		}
		Box->RefreshContentExtent();
		Rig.PumpFrames(2);
		const TOptional<FBox2D> ViewportRect = Rig.Driver()->Find(FDreamBy::Widget(Box->ViewportNode.Get()))->GetPixelRect();
		if (!TestTrue(Under(Case, TEXT("The viewport is on screen and has a height")),
			ViewportRect.IsSet() && ViewportRect->Max.Y - ViewportRect->Min.Y > 1.0))
		{
			continue;
		}
		const double UnitsPerPixel = Box->ViewportNode->GetHeight() / (ViewportRect->Max.Y - ViewportRect->Min.Y);
		const double FirstMove = FMath::Sqrt(static_cast<double>(Rig.Raycaster()->GetScaledDragThresholdSquare())) + 2.0;
		const double LaterMove = (150.0 - FirstMove) * 0.5;
		bool bHeldThroughTheTravel = true;
		TestTrue(Under(Case, TEXT("The right-button drag through the controller completes")),
			Rig.Driver()->Sequence()
				.MoveTo(FDreamBy::Widget(Box->ViewportNode.Get()))
				.Press(EDreamUIMouseButtonType::Right)
				.MoveBy(FVector2D(0.0, -FirstMove))
				.Then([&bHeldThroughTheTravel](FDreamDriverContext& InContext) { bHeldThroughTheTravel = bHeldThroughTheTravel && ControllerHoldsKey(InContext, EKeys::RightMouseButton); })
				.MoveBy(FVector2D(0.0, -LaterMove))
				.MoveBy(FVector2D(0.0, -LaterMove))
				.WaitFrames(1)
				.Then([&bHeldThroughTheTravel](FDreamDriverContext& InContext) { bHeldThroughTheTravel = bHeldThroughTheTravel && ControllerHoldsKey(InContext, EKeys::RightMouseButton); })
				.Perform());
		const float Offset = Box->GetScrollOffset();
		TestTrue(Under(Case, TEXT("Letting go of the right button completes")),
			Rig.Driver()->Sequence().Release(EDreamUIMouseButtonType::Right).Perform());
		TestTrue(Under(Case, TEXT("The right button was down in the controller's own input all through the pull")), bHeldThroughTheTravel);
		TestNearlyEqual(*Under(Case, TEXT("The content moved the full 150 pixels, staying under the pointer that grabbed it")),
			Offset, static_cast<float>(150.0 * UnitsPerPixel), static_cast<float>(1.5 * UnitsPerPixel));
	}
	return true;
}

#endif
