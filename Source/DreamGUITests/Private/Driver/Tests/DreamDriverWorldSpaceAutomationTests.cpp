// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Components/SceneComponent.h"
#include "Controls/DreamButton.h"
#include "Controls/DreamTextInput.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamPointerEventData.h"
#include "Event/DreamWorldSpaceRaycaster.h"
#include "InputCoreTypes.h"
#include "Interaction/UIEventTrigger.h"
#include "Interaction/UITextInput.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverProjection.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Driver/DreamDriverVirtualCamera.h"
#include "Driver/DreamDriverWorldSpace.h"
#include "Interaction/DreamPressInteractionTestTypes.h"

/*
 * A POINTER IN THE WORLD.
 *
 * Every other driver test points at a screen: the rig's root canvas is a ScreenSpaceOverlay, and the
 * screen raycaster deprojects through the canvas's own matrix. A panel standing in the level is seen
 * through a PLAYER's eye instead, and its ray is made by UDreamWorldSpaceRaycaster from the player's
 * LocalPlayer -- which a headless world has not got, so until now nothing world-space was ever
 * pointed at by a test at all. These attach a world pointer (DreamDriverWorld::AttachWorldPointer):
 * the production raycaster with its ray made from a virtual camera, and the driver aiming through the
 * same camera. Everything after the ray -- which canvases are traced, how their hits are ordered, how
 * the input module arbitrates between the world pointer and the screen raycaster, dispatch -- is the
 * production pipeline.
 *
 * The geometry is the same throughout unless a test says otherwise: the eye at the origin looking down
 * +X with ninety degrees across a 1280x720 viewport, and a panel 300 cm ahead whose plane crosses the
 * view axis face-on. At that distance one centimetre of panel is 640/300 = 2.13 pixels.
 *
 * The expected behaviour is UMG's where UMG has one -- a widget in the world clicked through its
 * interaction is clicked like any widget, the nearest thing along the ray takes the pointer (a
 * UWidgetInteractionComponent's trace stops at the first blocking hit), and a widget on the screen is
 * above anything in the scene (Slate hit-tests the viewport's widgets before the game viewport ever
 * sees the pointer). DreamGUI states the last one outright: UDreamPointerInputModule::LineTrace sorts
 * the screen raycasters' hits ahead of every other raycaster's before it looks at distance.
 *
 * Controls on a panel are not under the rig's root, so they are aimed at with FDreamBy::Widget.
 */
namespace DreamDriverWorldSpaceTestLocal
{
	const FIntPoint ViewportSize(1280, 720);
	/**
	 * One pixel. FSceneView::DeprojectScreenToWorld makes the world ray from the pixel's integer corner,
	 * so a projected pixel comes back to within one of itself -- and a real mouse only has whole pixels.
	 */
	const double PixelTolerance = 1.0;

	/** The eye every test here looks through. */
	FMinimalViewInfo EyeAtTheOrigin()
	{
		return DreamDriverWorld::MakeView(FVector::ZeroVector, FRotator::ZeroRotator, 90.0f, ViewportSize);
	}

	/** InDistance down the view axis, InOffset (right, up) off it, turned by InRotation. Unturned, a panel here faces the eye. */
	FTransform AheadOfTheEye(double InDistance, const FRotator& InRotation = FRotator::ZeroRotator, const FVector2D& InOffset = FVector2D::ZeroVector)
	{
		return FTransform(InRotation, FVector(InDistance, InOffset.X, InOffset.Y));
	}

	/** A button whose five public events all go to InListener, so a test asserts on the ones it is about and the rest can be asserted as zero. */
	UDreamButton* MakeObservedButton(FDreamDriverRig& InRig, UDreamWidget* InParent, const FString& InName,
		UDreamPressInteractionListener* InListener, const FVector2D& InSize = FVector2D(120.0, 60.0),
		const FVector2D& InPosition = FVector2D::ZeroVector)
	{
		UDreamButton* Button = InRig.MakeControl<UDreamButton>(InName, InParent, InSize, InPosition);
		if (Button != nullptr && InListener != nullptr)
		{
			Button->OnClicked.AddDynamic(InListener, &UDreamPressInteractionListener::HandleClicked);
			Button->OnPressed.AddDynamic(InListener, &UDreamPressInteractionListener::HandlePressed);
			Button->OnReleased.AddDynamic(InListener, &UDreamPressInteractionListener::HandleReleased);
			Button->OnHovered.AddDynamic(InListener, &UDreamPressInteractionListener::HandleHovered);
			Button->OnUnhovered.AddDynamic(InListener, &UDreamPressInteractionListener::HandleUnhovered);
		}
		return Button;
	}

	/** What one plain widget was sent, counted by the production component that exists to be told. */
	struct FWorldWidgetEvents
	{
		int32 Enter = 0;
		int32 Scroll = 0;
		FVector2D LastScrollAxis = FVector2D::ZeroVector;

		void Observe(UDreamWidget* InWidget)
		{
			UUIEventTrigger* Trigger = InWidget != nullptr ? InWidget->AddComponent<UUIEventTrigger>() : nullptr;
			if (Trigger == nullptr)
			{
				return;
			}
			Trigger->GetOnPointerEnterEvent().AddLambda([this](UDreamPointerEventData*) { ++Enter; });
			Trigger->GetOnPointerScrollEvent().AddLambda([this](UDreamPointerEventData* InEventData)
			{
				++Scroll;
				if (InEventData != nullptr)
				{
					LastScrollAxis = InEventData->ScrollAxisValue;
				}
			});
		}
	};

	/** Whether InWidget is InAncestor or somewhere inside it. A control's pointer events land on its parts. */
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

	/** The world pointer and one panel facing it: the part nearly every test shares. */
	struct FWorldStage
	{
		UDreamDriverWorldSpaceRaycaster* Pointer = nullptr;
		UDreamWidget* Panel = nullptr;

		bool IsReady() const { return Pointer != nullptr && Panel != nullptr; }
	};

	FWorldStage SetUpStage(FAutomationTestBase& InTest, FDreamDriverRig& InRig,
		EDreamWorldPointerSource InSource = EDreamWorldPointerSource::Mouse, bool bInHitTestableBackground = false)
	{
		FWorldStage Stage;
		// The pointer before the panel, as in a game where the player exists before the level's panels
		// begin play -- and before anything could ask the manager to make a world pointer of its own.
		Stage.Pointer = DreamDriverWorld::AttachWorldPointer(InRig, EyeAtTheOrigin(), InSource);
		Stage.Panel = DreamDriverWorld::MakeWorldPanel(InRig, TEXT("Panel"), AheadOfTheEye(300.0), FVector2D(400.0, 300.0),
			bInHitTestableBackground);
		InTest.TestNotNull(TEXT("A world pointer was attached to the rig"), Stage.Pointer);
		InTest.TestNotNull(TEXT("A world-space panel was built in front of it"), Stage.Panel);
		return Stage;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverWorldSpaceClickTest,
	"DreamGUI.Driver.WorldSpace.ClickingAButtonOnAPanelFacingTheCameraClicksItOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverWorldSpaceClickTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverWorldSpaceTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	const FWorldStage Stage = SetUpStage(*this, Rig);
	if (!Stage.IsReady())
	{
		return false;
	}
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	UDreamButton* Button = MakeObservedButton(Rig, Stage.Panel, TEXT("Play"), Listener.Get());
	if (!TestNotNull(TEXT("A button was made on the panel"), Button))
	{
		return false;
	}
	Rig.PumpFrames(2);

	FDreamElementRef Play = Rig.Driver()->Find(FDreamBy::Widget(Button));
	const TOptional<FVector2D> Centre = Play->GetCentrePixel();
	if (!TestTrue(TEXT("Seen through the world pointer's camera, the button has a pixel"), Centre.IsSet()))
	{
		return false;
	}
	// On the view axis, so in the middle of the viewport: the pixel is the camera's, not the panel
	// canvas's own virtual screen, which would put it wherever that canvas's matrix says.
	TestEqual(TEXT("A button on the view axis is in the middle of the viewport, across"), Centre->X, 640.0, PixelTolerance);
	TestEqual(TEXT("... and down"), Centre->Y, 360.0, PixelTolerance);

	TestTrue(TEXT("Clicking the button completes"), Play->Click());
	TestEqual(TEXT("One press"), Listener->PressedCount, 1);
	TestEqual(TEXT("One release"), Listener->ReleasedCount, 1);
	TestEqual(TEXT("One click"), Listener->ClickedCount, 1);

	// And it was the world pointer that found it: the press is recorded against the raycaster whose hit
	// it was, and the rig's screen raycaster has nothing under that pixel to have found.
	const UDreamPointerEventData* EventData = Rig.Context().GetPointerEventData(0);
	TestTrue(TEXT("The press was judged by the world pointer"),
		EventData != nullptr && EventData->PressRaycaster.Get() == static_cast<UDreamBaseRaycaster*>(Stage.Pointer));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverWorldSpaceHoverTest,
	"DreamGUI.Driver.WorldSpace.MovingOnAndOffAWorldButtonHoversAndUnhoversItOnceEach",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverWorldSpaceHoverTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverWorldSpaceTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	const FWorldStage Stage = SetUpStage(*this, Rig);
	if (!Stage.IsReady())
	{
		return false;
	}
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	UDreamButton* Button = MakeObservedButton(Rig, Stage.Panel, TEXT("Play"), Listener.Get());
	if (!TestNotNull(TEXT("A button was made on the panel"), Button))
	{
		return false;
	}
	Rig.PumpFrames(2);

	FDreamElementRef Play = Rig.Driver()->Find(FDreamBy::Widget(Button));
	TestTrue(TEXT("Moving onto the button completes"), Play->Hover());
	TestEqual(TEXT("Arriving is one OnHovered"), Listener->HoveredCount, 1);
	TestEqual(TEXT("... and no OnUnhovered"), Listener->UnhoveredCount, 0);
	TestTrue(TEXT("While the pointer rests on it the button says it is hovered"), Button->IsHovered());

	// Five hundred pixels is 234 cm across the panel at this distance: off the 120 cm button, and off
	// the panel's own edge 200 cm out as well, so nothing world-space is under the pointer any more.
	TestTrue(TEXT("Moving away completes"), Play->MoveBy(FVector2D(500.0, 0.0)));
	TestEqual(TEXT("Leaving is one OnUnhovered"), Listener->UnhoveredCount, 1);
	TestEqual(TEXT("... and no second OnHovered"), Listener->HoveredCount, 1);
	TestFalse(TEXT("The button no longer says it is hovered"), Button->IsHovered());
	TestEqual(TEXT("Hovering is not clicking"), Listener->ClickedCount, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverWorldSpaceNearerPanelTest,
	"DreamGUI.Driver.WorldSpace.WhereTwoPanelsOverlapTheNearerOneTakesTheClick",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverWorldSpaceNearerPanelTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverWorldSpaceTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamDriverWorldSpaceRaycaster* Pointer = DreamDriverWorld::AttachWorldPointer(Rig, EyeAtTheOrigin(), EDreamWorldPointerSource::Mouse);
	// The far panel is twice as far and everything on it twice as large, so its button covers exactly
	// the pixels the near one does: which one answers is decided by distance and nothing else.
	UDreamWidget* Near = DreamDriverWorld::MakeWorldPanel(Rig, TEXT("Near"), AheadOfTheEye(300.0), FVector2D(400.0, 300.0));
	UDreamWidget* Far = DreamDriverWorld::MakeWorldPanel(Rig, TEXT("Far"), AheadOfTheEye(600.0), FVector2D(800.0, 600.0));
	if (!TestNotNull(TEXT("A world pointer"), Pointer) || !TestNotNull(TEXT("The near panel"), Near) || !TestNotNull(TEXT("The far panel"), Far))
	{
		return false;
	}
	TStrongObjectPtr<UDreamPressInteractionListener> NearListener(NewObject<UDreamPressInteractionListener>());
	TStrongObjectPtr<UDreamPressInteractionListener> FarListener(NewObject<UDreamPressInteractionListener>());
	UDreamButton* NearButton = MakeObservedButton(Rig, Near, TEXT("NearButton"), NearListener.Get(), FVector2D(120.0, 60.0));
	UDreamButton* FarButton = MakeObservedButton(Rig, Far, TEXT("FarButton"), FarListener.Get(), FVector2D(240.0, 120.0));
	if (!TestNotNull(TEXT("The near button"), NearButton) || !TestNotNull(TEXT("The far button"), FarButton))
	{
		return false;
	}
	Rig.PumpFrames(2);

	FDreamElementRef NearElement = Rig.Driver()->Find(FDreamBy::Widget(NearButton));
	FDreamElementRef FarElement = Rig.Driver()->Find(FDreamBy::Widget(FarButton));
	const TOptional<FVector2D> NearCentre = NearElement->GetCentrePixel();
	const TOptional<FBox2D> FarRect = FarElement->GetPixelRect();
	if (!TestTrue(TEXT("Both buttons are on screen"), NearCentre.IsSet() && FarRect.IsSet()))
	{
		return false;
	}
	// The fixture's own claim, asserted rather than assumed: without it "the near one won" could be the
	// far one simply not being under the pointer.
	TestTrue(TEXT("The far button is under the near button's pixel too"), FarRect->IsInside(NearCentre.GetValue()));

	TestTrue(TEXT("Clicking where both are completes"), NearElement->Click());
	TestEqual(TEXT("The nearer button took the click"), NearListener->ClickedCount, 1);
	TestEqual(TEXT("The one behind it did not"), FarListener->ClickedCount, 0);
	TestTrue(TEXT("The nearer button is the hovered one"), NearButton->IsHovered());
	TestFalse(TEXT("The one behind it is not hovered"), FarButton->IsHovered());

	// The control: with the near panel lifted out of view, the same pixel reaches the far button -- so
	// the far one was reachable all along, and it was distance that kept the click from it.
	USceneComponent* NearHost = DreamDriverWorld::GetPanelHost(Near);
	if (!TestNotNull(TEXT("The near panel has a host to move"), NearHost))
	{
		return false;
	}
	NearHost->SetWorldLocation(FVector(300.0, 0.0, 1000.0));
	Rig.PumpFrames(1);
	TestTrue(TEXT("Clicking the far button completes"), FarElement->Click());
	TestEqual(TEXT("With nothing in front of it the far button is clicked"), FarListener->ClickedCount, 1);
	TestEqual(TEXT("... and the moved one is not clicked again"), NearListener->ClickedCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverWorldSpaceOverlayWinsTest,
	"DreamGUI.Driver.WorldSpace.AnOverlayControlInFrontOfAWorldPanelTakesTheClick",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverWorldSpaceOverlayWinsTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverWorldSpaceTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	const FWorldStage Stage = SetUpStage(*this, Rig);
	if (!Stage.IsReady())
	{
		return false;
	}
	TStrongObjectPtr<UDreamPressInteractionListener> WorldListener(NewObject<UDreamPressInteractionListener>());
	TStrongObjectPtr<UDreamPressInteractionListener> OverlayListener(NewObject<UDreamPressInteractionListener>());
	UDreamButton* WorldButton = MakeObservedButton(Rig, Stage.Panel, TEXT("WorldPlay"), WorldListener.Get());
	// Null parent: the rig's own ScreenSpaceOverlay root, i.e. the HUD. Centred, so it sits over the
	// middle of the viewport, where the world button is seen.
	UDreamButton* Overlay = MakeObservedButton(Rig, nullptr, TEXT("OverlayPlay"), OverlayListener.Get(), FVector2D(200.0, 100.0));
	if (!TestNotNull(TEXT("The world button"), WorldButton) || !TestNotNull(TEXT("The overlay button"), Overlay))
	{
		return false;
	}
	Rig.PumpFrames(2);

	FDreamElementRef World = Rig.Driver()->Find(FDreamBy::Widget(WorldButton));
	const TOptional<FVector2D> WorldCentre = World->GetCentrePixel();
	const TOptional<FBox2D> OverlayRect = Rig.Driver()->Find(FDreamBy::Widget(Overlay))->GetPixelRect();
	if (!TestTrue(TEXT("Both buttons are on screen"), WorldCentre.IsSet() && OverlayRect.IsSet()))
	{
		return false;
	}
	TestTrue(TEXT("The overlay button covers the pixel the world button is seen at"), OverlayRect->IsInside(WorldCentre.GetValue()));

	// Aimed at the WORLD button. Both raycasters find something under that pixel, and the screen's hit
	// is the one that counts, whatever the distances say.
	TestTrue(TEXT("Clicking at the world button's pixel completes"), World->Click());
	TestEqual(TEXT("The overlay in front took the click"), OverlayListener->ClickedCount, 1);
	TestEqual(TEXT("The world button behind it did not"), WorldListener->ClickedCount, 0);

	// The control: the overlay moved aside, the same click is the world button's.
	Overlay->SetAnchoredPosition(FVector2D(400.0, 0.0));
	Rig.PumpFrames(1);
	TestTrue(TEXT("Clicking the world button again completes"), World->Click());
	TestEqual(TEXT("With the overlay out of the way the world button is clicked"), WorldListener->ClickedCount, 1);
	TestEqual(TEXT("... and the overlay is not clicked again"), OverlayListener->ClickedCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverWorldSpaceReticleTest,
	"DreamGUI.Driver.WorldSpace.InReticleModeTheClickLandsUnderTheViewportCentreWhereverThePointerIs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverWorldSpaceReticleTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverWorldSpaceTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	const FWorldStage Stage = SetUpStage(*this, Rig, EDreamWorldPointerSource::ScreenCenter);
	if (!Stage.IsReady())
	{
		return false;
	}
	TStrongObjectPtr<UDreamPressInteractionListener> CentreListener(NewObject<UDreamPressInteractionListener>());
	TStrongObjectPtr<UDreamPressInteractionListener> AsideListener(NewObject<UDreamPressInteractionListener>());
	UDreamButton* Centre = MakeObservedButton(Rig, Stage.Panel, TEXT("Centre"), CentreListener.Get());
	// 130 cm to the right: from 70 to 190 cm, clear of the centre button's 60 and inside the panel's 200.
	UDreamButton* Aside = MakeObservedButton(Rig, Stage.Panel, TEXT("Aside"), AsideListener.Get(), FVector2D(120.0, 60.0), FVector2D(130.0, 0.0));
	if (!TestNotNull(TEXT("The centre button"), Centre) || !TestNotNull(TEXT("The button to the side"), Aside))
	{
		return false;
	}
	Rig.PumpFrames(2);

	FDreamElementRef AsideElement = Rig.Driver()->Find(FDreamBy::Widget(Aside));
	const TOptional<FVector2D> AsidePixel = AsideElement->GetCentrePixel();
	if (!TestTrue(TEXT("The button to the side is on screen"), AsidePixel.IsSet()))
	{
		return false;
	}
	TestTrue(FString::Printf(TEXT("... and well away from the middle of the viewport (at %s)"), *AsidePixel->ToString()),
		AsidePixel->X > 640.0 + 150.0);

	// The pointer goes to the side button and the press is made there. A reticle does not care: the ray
	// is the middle of the view, and so is what it lands on.
	TestTrue(TEXT("Clicking with the pointer over the side button completes"), AsideElement->Click());
	TestEqual(TEXT("The button under the middle of the view took the click"), CentreListener->ClickedCount, 1);
	TestEqual(TEXT("The button under the pointer did not"), AsideListener->ClickedCount, 0);
	TestTrue(TEXT("The centre button is the hovered one"), Centre->IsHovered());
	TestFalse(TEXT("The button under the pointer is not hovered"), Aside->IsHovered());

	// The control: the same raycaster aiming with the pointer again, the same click is the side button's.
	Stage.Pointer->SetPointerSource(EDreamWorldPointerSource::Mouse);
	TestTrue(TEXT("Clicking the side button with a mouse pointer completes"), AsideElement->Click());
	TestEqual(TEXT("A mouse pointer clicks the button it is over"), AsideListener->ClickedCount, 1);
	TestEqual(TEXT("... and not the centre one again"), CentreListener->ClickedCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverWorldSpaceYawedPanelTest,
	"DreamGUI.Driver.WorldSpace.AButtonOnAPanelYawedFortyFiveDegreesIsHitAtItsProjectedCentre",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverWorldSpaceYawedPanelTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverWorldSpaceTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamDriverWorldSpaceRaycaster* Pointer = DreamDriverWorld::AttachWorldPointer(Rig, EyeAtTheOrigin(), EDreamWorldPointerSource::Mouse);
	UDreamWidget* Panel = DreamDriverWorld::MakeWorldPanel(Rig, TEXT("Yawed"), AheadOfTheEye(300.0, FRotator(0.0, 45.0, 0.0)), FVector2D(400.0, 300.0));
	if (!TestNotNull(TEXT("A world pointer"), Pointer) || !TestNotNull(TEXT("A yawed panel"), Panel))
	{
		return false;
	}
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	// A hundred centimetres along the turned panel: the yaw swings that end of the panel toward the eye,
	// to (229, 71) -- nearer than the panel's centre, and off the view axis.
	UDreamButton* Tilted = MakeObservedButton(Rig, Panel, TEXT("Tilted"), Listener.Get(), FVector2D(120.0, 60.0), FVector2D(100.0, 0.0));
	if (!TestNotNull(TEXT("A button on the yawed panel"), Tilted))
	{
		return false;
	}
	Rig.PumpFrames(2);

	FDreamElementRef TiltedElement = Rig.Driver()->Find(FDreamBy::Widget(Tilted));
	const TOptional<FVector2D> Pixel = TiltedElement->GetCentrePixel();
	const TOptional<FBox2D> Rect = TiltedElement->GetPixelRect();
	if (!TestTrue(TEXT("The yawed button has a pixel and a pixel rect"), Pixel.IsSet() && Rect.IsSet()))
	{
		return false;
	}
	// Where the button would be seen if its depth were taken to be the panel's -- the answer of a
	// projection that ignored perspective. The real one is nearer, so it is further from the middle.
	const FVector2D LocalCentre = Tilted->GetLocalSpaceCenter();
	const FVector WorldCentre = Tilted->GetWorldTransform().TransformPosition(FVector(0.0, LocalCentre.X, LocalCentre.Y));
	const double FlatX = 640.0 + WorldCentre.Y / 300.0 * 640.0;
	TestTrue(FString::Printf(TEXT("Perspective moved the button's pixel (at %.1f, depth ignored would be %.1f)"), Pixel->X, FlatX),
		Pixel->X > FlatX + 20.0);
	TestTrue(TEXT("The projected centre is inside the projected button"), Rect->IsInside(Pixel.GetValue()));

	TestTrue(TEXT("Clicking the yawed button's projected centre completes"), TiltedElement->Click());
	TestEqual(TEXT("It was clicked, once"), Listener->ClickedCount, 1);
	TestEqual(TEXT("... having been pressed there"), Listener->PressedCount, 1);
	return true;
}

/**
 * A button's face and the hit-testable panel behind it are coplanar: their distances along the ray are
 * the same float. UDreamWorldSpaceRaycaster::Raycast collects each canvas's hits in the canvas's own
 * order -- canvas sort order, then hierarchy, the button's face ahead of the panel behind it -- and then
 * re-sorts ALL of them by distance, so only a STABLE sort keeps the face ahead of its panel. It used to
 * be TArray::Sort, which is not: the engine's small-range sort moved the first of two equal elements to
 * the end, the panel came out ahead of the button and took the press. The design comment in Raycast
 * says the distance sort is for SEPARATE panels; this is what it must not do to one.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverWorldSpaceBackgroundTest,
	"DreamGUI.Driver.WorldSpace.AButtonOnAPanelWithAHitTestableBackgroundTakesTheClickNotTheBackground",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverWorldSpaceBackgroundTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverWorldSpaceTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	const FWorldStage Stage = SetUpStage(*this, Rig, EDreamWorldPointerSource::Mouse, /*bInHitTestableBackground*/ true);
	if (!Stage.IsReady())
	{
		return false;
	}
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	UDreamButton* Button = MakeObservedButton(Rig, Stage.Panel, TEXT("Play"), Listener.Get());
	if (!TestNotNull(TEXT("A button on a panel with a background"), Button))
	{
		return false;
	}
	Rig.PumpFrames(2);

	TestTrue(TEXT("Clicking the button completes"), Rig.Driver()->Find(FDreamBy::Widget(Button))->Click());
	TestEqual(TEXT("The button in front of the background took the click"), Listener->ClickedCount, 1);
	// Where the click actually landed, so a red here says which widget took it. The pointer records the
	// widget each click was dispatched to; a press that landed on the background records the panel.
	const UDreamPointerEventData* EventData = Rig.Context().GetPointerEventData(0);
	const UDreamWidget* Clicked = EventData != nullptr ? EventData->LastClickWidget.Get() : nullptr;
	TestTrue(FString::Printf(TEXT("The click landed inside the button, not on the panel behind it (it landed on %s)"),
		*GetNameSafe(Clicked)), IsWithin(Clicked, Button));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverWorldSpaceScrollTest,
	"DreamGUI.Driver.WorldSpace.TurningTheWheelOverAWorldWidgetReachesThatWidget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverWorldSpaceScrollTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverWorldSpaceTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	const FWorldStage Stage = SetUpStage(*this, Rig);
	if (!Stage.IsReady())
	{
		return false;
	}
	UDreamWidget* List = Rig.MakeWidget(TEXT("List"), Stage.Panel, FVector2D(200.0, 200.0), FVector2D::ZeroVector);
	if (!TestNotNull(TEXT("A widget on the panel"), List))
	{
		return false;
	}
	FWorldWidgetEvents ListEvents;
	ListEvents.Observe(List);
	Rig.PumpFrames(1);

	// A wheel is delivered to whatever the pointer is over, and over a world panel "over" is the world
	// pointer's answer -- so the hover is half of what is being tested.
	TestTrue(TEXT("Scrolling over the widget completes"), Rig.Driver()->Find(FDreamBy::Widget(List))->ScrollBy(FVector2D(0.0, 3.0)));
	TestEqual(TEXT("The widget was entered on the way"), ListEvents.Enter, 1);
	TestEqual(TEXT("The widget was sent one scroll"), ListEvents.Scroll, 1);
	TestEqual(TEXT("... carrying the value that was turned"), ListEvents.LastScrollAxis.Y, 3.0, 0.001);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverWorldSpaceTypingTest,
	"DreamGUI.Driver.WorldSpace.ClickingAFieldOnAWorldPanelAndTypingEntersTheText",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverWorldSpaceTypingTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverWorldSpaceTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	const FWorldStage Stage = SetUpStage(*this, Rig);
	if (!Stage.IsReady())
	{
		return false;
	}
	UDreamTextInput* Field = Rig.MakeControl<UDreamTextInput>(TEXT("Username"), Stage.Panel, FVector2D(320.0, 40.0));
	if (!TestNotNull(TEXT("A text field on the panel"), Field))
	{
		return false;
	}
	Rig.PumpFrames(2);

	// The element clicks the field first -- through the world pointer, which is the part under test --
	// and then types into whatever took the keyboard. A field that was not focused by that click would
	// leave the characters nowhere to go, and the step would fail saying so.
	FDreamElementRef FieldElement = Rig.Driver()->Find(FDreamBy::Widget(Field));
	TestTrue(TEXT("Clicking into the field and typing completes"), FieldElement->Type(TEXT("hi")));
	TestEqual(TEXT("The field holds what was typed"), Field->GetText(), FString(TEXT("hi")));

	// Enter ends the edit, as it does on a screen. Ended here rather than left to the tear-down: a
	// panel is not under the rig's root, and a field still being edited when the rig checks for leaked
	// edits would be reported against this test.
	TestTrue(TEXT("Pressing Enter in the field completes"), FieldElement->Type(EKeys::Enter));
	TestTrue(TEXT("Enter ended the edit"), Field->InputBehaviour != nullptr && !Field->InputBehaviour->IsInputActive());
	TestEqual(TEXT("... keeping the text"), Field->GetText(), FString(TEXT("hi")));
	return true;
}

#endif
