// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Components/SceneComponent.h"
#include "Controls/DreamButton.h"
#include "Controls/DreamSlider.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamPointerEventData.h"
#include "Event/DreamWorldSpaceRaycaster.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverProjection.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Driver/DreamDriverWorldSpace.h"
#include "Interaction/DreamDragInteractionTestTypes.h"
#include "Interaction/DreamPressInteractionTestTypes.h"
#include "Lifecycle/DreamLifecycleFixtures.h"

/*
 * CONTROLS THAT MOVE WHILE THEY ARE POINTED AT.
 *
 * Slate hit-tests the geometry a widget was arranged at for the frame being drawn (FHittestGrid is filled as the frame is
 * painted), so a widget that moves every frame -- a render transform an animation keeps writing -- is pressed where it is
 * drawn that frame, and a press that stays on it while it moves is still a press of it. DreamGUI draws such a widget as a
 * render layer of its canvas: the geometry under it is kept relative to it and its transform is applied on the GPU, so
 * nothing under it is transformed on the CPU as it moves; the hit test reads the widgets' own transforms instead, and has
 * to land where the layer puts them (DreamGUI.RenderLayer.AButtonInATurnedLayerIsClickedWhereItIsDrawn did so for a layer
 * at rest). A widget's world transform is composed lazily, when it is read, and the input tick traces before the UI manager
 * flushes the frame's moves -- so a panel moved by code is hit at its new place by the very next press, before any flush.
 *
 * All on a world-space panel drawn by DreamGUI's renderer, the one kind of canvas a layer is drawn in, three metres ahead of
 * the eye; every aim is the driver's, through the rig's camera, at the moment the step runs.
 */
namespace DreamMovingWidgetsTestLocal
{
	using DreamTests::Lifecycle::FScopedConsoleVariable;

	const FIntPoint ViewportSize(1280, 720);

	FMinimalViewInfo EyeAtTheOrigin()
	{
		return DreamDriverWorld::MakeView(FVector::ZeroVector, FRotator::ZeroRotator, 90.0f, ViewportSize);
	}

	struct FLayerStage
	{
		UDreamDriverWorldSpaceRaycaster* Pointer = nullptr;
		UDreamWidget* Panel = nullptr;
		/** The card that moves, drawn as a layer, and what is put on it. */
		UDreamWidget* Card = nullptr;

		bool IsReady() const { return Pointer != nullptr && Panel != nullptr && Card != nullptr; }
	};

	/** The pointer, a panel 300 cm ahead, and a card on it that is always a layer. */
	FLayerStage SetUpLayerStage(FAutomationTestBase& InTest, FDreamDriverRig& InRig)
	{
		FLayerStage Stage;
		Stage.Pointer = DreamDriverWorld::AttachWorldPointer(InRig, EyeAtTheOrigin(), EDreamWorldPointerSource::Mouse);
		Stage.Panel = DreamDriverWorld::MakeWorldPanel(InRig, TEXT("Panel"), FTransform(FVector(300.0, 0.0, 0.0)), FVector2D(500.0, 400.0));
		Stage.Card = Stage.Panel != nullptr ? InRig.MakeWidget(TEXT("Card"), Stage.Panel, FVector2D(360.0, 200.0)) : nullptr;
		if (Stage.Card != nullptr)
		{
			Stage.Card->SetRenderLayerMode(EDreamWidgetRenderLayer::Always);
		}
		InTest.TestTrue(TEXT("A world pointer, a panel in front of it and a card on the panel"), Stage.IsReady());
		return Stage;
	}

	/** The card turned to InYaw degrees: one frame's worth of an animation that keeps turning it. */
	void TurnCard(UDreamWidget* InCard, double InYaw)
	{
		InCard->SetRenderRotation(FRotator(0.0, InYaw, 0.0));
	}

	/** Whether InWidget is InAncestor or somewhere inside it: a control's pointer events land on its parts. */
	bool IsWithin(const UDreamWidget* InWidget, const UDreamWidget* InAncestor)
	{
		for (const UDreamWidget* Walk = InWidget; Walk != nullptr; Walk = Walk->GetParent())
		{
			if (Walk == InAncestor)
			{
				return true;
			}
		}
		return false;
	}

	/** The card slid InAcross centimetres along the panel: one frame of an animation that keeps sliding it. */
	void SlideCard(UDreamWidget* InCard, double InAcross)
	{
		InCard->SetRenderTranslation(FVector(0.0, InAcross, 0.0));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTurningLayerButtonClickTest,
	"DreamGUI.Button.OnALayerThatTurnsEveryFrameAPressAndReleaseFollowingItLandWhereTheButtonIsDrawnAndClickItOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamTurningLayerButtonClickTest, "DreamGUI.Button.OnALayerThatTurnsEveryFrameAPressAndReleaseFollowingItLandWhereTheButtonIsDrawnAndClickItOnce", "[Pointer][World]")

/*
 * The card turns by two degrees a frame, all through the gesture, and the pointer follows the button to where it is drawn
 * each frame -- a few millimetres at a time, never near the world pointer's drag threshold: onto it, press, follow it two
 * frames, let go. SButton's click is a press and a release on the button, and the button is where the hit test finds it
 * every frame, so that is one press and one click. The card is a layer throughout, with the button in it.
 */
bool FDreamTurningLayerButtonClickTest::RunTest(const FString& Parameters)
{
	using namespace DreamMovingWidgetsTestLocal;
	const FScopedConsoleVariable Layers(TEXT("r.DreamUI.RenderLayers"), 1);
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	const FLayerStage Stage = SetUpLayerStage(*this, Rig);
	if (!Stage.IsReady())
	{
		return false;
	}
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	UDreamButton* Button = Rig.MakeControl<UDreamButton>(TEXT("Play"), Stage.Card, FVector2D(120.0, 60.0), FVector2D(70.0, 30.0));
	if (!TestNotNull(TEXT("A button on the card, off its middle"), Button))
	{
		return false;
	}
	Button->OnPressed.AddDynamic(Listener.Get(), &UDreamPressInteractionListener::HandlePressed);
	Button->OnClicked.AddDynamic(Listener.Get(), &UDreamPressInteractionListener::HandleClicked);
	UDreamWidget* Card = Stage.Card;
	TurnCard(Card, 20.0);
	Rig.PumpFrames(3);
	if (!TestTrue(TEXT("The turned card is a layer"), Card->IsRenderLayer())
		|| !TestTrue(TEXT("...and the button is in it"), Button->GetRenderLayer() == Card))
	{
		return false;
	}
	const TOptional<FVector2D> Before = Rig.Driver()->Find(FDreamBy::Widget(Button))->GetCentrePixel();

	double Yaw = 20.0;
	const auto TurnOn = [Card, &Yaw](FDreamDriverContext&)
	{
		Yaw += 2.0;
		TurnCard(Card, Yaw);
	};
	TestTrue(TEXT("Following the turning button onto it, pressing, following it and letting go completes"),
		Rig.Driver()->Sequence()
			.Then(TurnOn).MoveTo(FDreamBy::Widget(Button))
			.Then(TurnOn).MoveTo(FDreamBy::Widget(Button)).Press()
			.Then(TurnOn).MoveTo(FDreamBy::Widget(Button))
			.Then(TurnOn).MoveTo(FDreamBy::Widget(Button)).Release()
			.Then([this, Card, Button](FDreamDriverContext&)
			{
				TestTrue(TEXT("The card was still a layer as it was let go"), Card->IsRenderLayer() && Button->GetRenderLayer() == Card);
			})
			.Perform());
	const TOptional<FVector2D> After = Rig.Driver()->Find(FDreamBy::Widget(Button))->GetCentrePixel();
	TestTrue(TEXT("The button is drawn somewhere else than it was when the gesture began"),
		Before.IsSet() && After.IsSet() && !Before->Equals(After.GetValue(), 1.0));
	TestEqual(TEXT("The press landed on the button"), Listener->PressedCount, 1);
	TestEqual(TEXT("...and the release clicked it, once"), Listener->ClickedCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSlidingLayerSliderDragTest,
	"DreamGUI.Slider.OnALayerThatSlidesEveryFrameADragEndsAtTheValueUnderThePointerOnTheTrackAsDrawn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamSlidingLayerSliderDragTest, "DreamGUI.Slider.OnALayerThatSlidesEveryFrameADragEndsAtTheValueUnderThePointerOnTheTrackAsDrawn", "[Pointer][World]")

/*
 * SSlider commits PositionToValue(pointer) on every move while it holds the capture, measured through its geometry as it
 * is arranged that frame. The card slides two centimetres a frame along the panel through the whole drag; the drag ends
 * on the middle of the handle's travel as it is drawn on the last frame, and the value is one half -- not the value the
 * same pixel had where the travel was a few frames earlier.
 */
bool FDreamSlidingLayerSliderDragTest::RunTest(const FString& Parameters)
{
	using namespace DreamMovingWidgetsTestLocal;
	const FScopedConsoleVariable Layers(TEXT("r.DreamUI.RenderLayers"), 1);
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	const FLayerStage Stage = SetUpLayerStage(*this, Rig);
	if (!Stage.IsReady())
	{
		return false;
	}
	UDreamSlider* Slider = Rig.MakeControl<UDreamSlider>(TEXT("Volume"), Stage.Card, FVector2D(300.0, 40.0));
	Rig.PumpFrames(2);
	if (!TestTrue(TEXT("The slider came up on the card with a handle and a handle area"),
		Slider != nullptr && Slider->HandleNode != nullptr && Slider->HandleAreaNode != nullptr))
	{
		return false;
	}
	UDreamWidget* Card = Stage.Card;
	UDreamWidget* HandleArea = Slider->HandleAreaNode.Get();
	SlideCard(Card, 0.0);
	Rig.PumpFrames(2);
	TestTrue(TEXT("The card is a layer, with the slider in it"), Card->IsRenderLayer() && Slider->GetRenderLayer() == Card);
	const TOptional<FBox2D> Travel = Rig.Driver()->Find(FDreamBy::Widget(HandleArea))->GetPixelRect();
	if (!TestTrue(TEXT("The handle's travel is seen, long enough to aim at"), Travel.IsSet() && Travel->Max.X - Travel->Min.X > 100.0))
	{
		return false;
	}
	const float OnePixel = static_cast<float>(1.0 / (Travel->Max.X - Travel->Min.X));
	TStrongObjectPtr<UDreamDragInteractionProbe> Values(NewObject<UDreamDragInteractionProbe>());
	Slider->OnValueChanged.AddDynamic(Values.Get(), &UDreamDragInteractionProbe::RecordFloat);

	double Across = 0.0;
	const auto SlideOn = [Card, &Across](FDreamDriverContext&)
	{
		Across += 2.0;
		SlideCard(Card, Across);
	};
	// The middle of the travel, wherever the card has taken it by the time the step runs.
	const auto MiddleOfTheTravel = [HandleArea](FDreamDriverContext& InContext) -> TOptional<FVector2D>
	{
		return FDreamDriverProjection::WidgetCentrePixel(HandleArea, InContext.Camera.Get());
	};
	TestTrue(TEXT("Grabbing the handle and dragging it to the middle of the sliding travel completes"),
		Rig.Driver()->Sequence()
			.MoveTo(FDreamBy::Widget(Slider->HandleNode.Get()))
			.Press()
			.Then(SlideOn).MoveBy(FVector2D(40.0, 0.0))
			.Then(SlideOn).MoveToResolvedPixel(MiddleOfTheTravel, TEXT("the middle of the slider's travel as drawn"))
			.Then(SlideOn).MoveToResolvedPixel(MiddleOfTheTravel, TEXT("the middle of the slider's travel as drawn"))
			.Release()
			.Perform());
	const TOptional<FBox2D> TravelAfter = Rig.Driver()->Find(FDreamBy::Widget(HandleArea))->GetPixelRect();
	TestTrue(TEXT("The travel is drawn further along than where the drag began"),
		TravelAfter.IsSet() && TravelAfter->Min.X > Travel->Min.X + 2.0);
	TestTrue(TEXT("The slider reported its value changing"), Values->NumFloats() > 0);
	TestNearlyEqual(TEXT("The value is the one under the pointer on the travel as drawn: halfway"), Slider->GetValue(), 0.5f, 2.0f * OnePixel);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamMovedPanelNextPressTest,
	"DreamGUI.Button.OnAPanelMovedUnderTheRestingPointerByCodeThePressOfTheVeryNextFrameLandsOnIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamMovedPanelNextPressTest, "DreamGUI.Button.OnAPanelMovedUnderTheRestingPointerByCodeThePressOfTheVeryNextFrameLandsOnIt", "[Pointer][World]")

/*
 * The pointer rests in the middle of the view, over nothing; a panel with a button on it stands four metres to the side.
 * Code moves the panel's scene component onto the view axis between two frames, and the press made right after it is
 * traced in the next input tick -- before the UI manager has flushed the move -- against the panel where it now stands:
 * the button is pressed and the release clicks it. What the frame before the move saw under the pointer was nothing.
 */
bool FDreamMovedPanelNextPressTest::RunTest(const FString& Parameters)
{
	using namespace DreamMovingWidgetsTestLocal;
	const FScopedConsoleVariable Defer(TEXT("r.DreamUI.DeferTransformNotifications"), 1);
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamDriverWorldSpaceRaycaster* Pointer = DreamDriverWorld::AttachWorldPointer(Rig, EyeAtTheOrigin(), EDreamWorldPointerSource::Mouse);
	UDreamWidget* Panel = DreamDriverWorld::MakeWorldPanel(Rig, TEXT("Panel"), FTransform(FVector(300.0, 400.0, 0.0)), FVector2D(300.0, 200.0));
	USceneComponent* PanelHost = DreamDriverWorld::GetPanelHost(Panel);
	if (!TestNotNull(TEXT("A world pointer"), Pointer) || !TestNotNull(TEXT("A panel off to the side, on a scene component"), PanelHost))
	{
		return false;
	}
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	UDreamButton* Button = Rig.MakeControl<UDreamButton>(TEXT("Play"), Panel, FVector2D(200.0, 100.0));
	if (!TestNotNull(TEXT("A button in the middle of the panel"), Button))
	{
		return false;
	}
	Button->OnPressed.AddDynamic(Listener.Get(), &UDreamPressInteractionListener::HandlePressed);
	Button->OnClicked.AddDynamic(Listener.Get(), &UDreamPressInteractionListener::HandleClicked);
	Rig.PumpFrames(2);
	const FVector2D Middle(ViewportSize.X * 0.5, ViewportSize.Y * 0.5);

	TestTrue(TEXT("Resting the pointer in the middle of the view, moving the panel under it and clicking completes"),
		Rig.Driver()->Sequence()
			.MoveToPixel(Middle)
			.WaitFrames(1)
			.Then([this, Button](FDreamDriverContext& InContext)
			{
				const UDreamPointerEventData* Mouse = InContext.GetPointerEventData(0);
				TestFalse(TEXT("Before the move the pointer is over nothing of the panel"),
					Mouse != nullptr && IsWithin(Mouse->EnterWidget.Get(), Button));
			})
			.Then([PanelHost](FDreamDriverContext&)
			{
				PanelHost->SetWorldLocation(FVector(300.0, 0.0, 0.0));
			})
			.Press()
			.Then([this, Listener](FDreamDriverContext&)
			{
				TestEqual(TEXT("The press of the very next frame landed on the moved panel's button"), Listener->PressedCount, 1);
			})
			.Release()
			.Perform());
	TestEqual(TEXT("...and the release clicked it once"), Listener->ClickedCount, 1);
	return true;
}

#endif
