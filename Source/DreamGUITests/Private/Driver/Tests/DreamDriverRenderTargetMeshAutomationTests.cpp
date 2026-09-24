// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

#include "CollisionQueryParams.h"
#include "Controls/DreamButton.h"
#include "Controls/DreamSlider.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamWidget.h"
#include "Engine/EngineTypes.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "Event/DreamPointerEventData.h"
#include "Event/DreamScreenSpaceRaycaster.h"
#include "Event/DreamWorldSpaceRaycaster.h"
#include "Extensions/DreamUIRenderTargetGeometrySource.h"
#include "Extensions/DreamUIRenderTargetInteraction.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverProjection.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Driver/DreamDriverVirtualCamera.h"
#include "Driver/DreamDriverWorldSpace.h"
#include "Interaction/DreamDragInteractionTestTypes.h"
#include "Interaction/DreamPressInteractionTestTypes.h"

/*
 * A CANVAS DRAWN INTO A TEXTURE AND SHOWN ON A MESH IN THE WORLD.
 *
 * The road a world pointer is meant to take to reach it, as the three components involved lay it out:
 *   1. the world pointer's ray hits the surface -- a UDreamUIRenderTargetGeometrySource, whose body is
 *      a thin box the size of the texture -- which a world-space raycaster only sees as an occluder
 *      (bOccludeByWorld: UDreamBaseRaycaster::RaycastWorld, a hit that carries no widget);
 *   2. the UDreamUIRenderTargetInteraction on the same actor is told the pointer entered, pressed and
 *      released through its IDreamPointer* handlers -- a hit that carries no widget is dispatched to
 *      the actor that owns the hit primitive and to its components that implement the pointer
 *      interfaces (UDreamEventSystem::CallOnWorldTarget*, from UDreamPointerInputModule) -- and keeps
 *      the world pointer's event data;
 *   3. every frame, in its own TickComponent, it asks the surface for the hit's UV
 *      (IDreamUIRenderTargetInteractionSourceInterface::PerformLineTrace), treats the UV as the canvas's
 *      view point, deprojects it through the canvas's matrix, traces the canvas, and drives a pointer
 *      of its own through UDreamPointerInputModule::ProcessPointerEvent -- which is what clicks the
 *      button drawn on the texture.
 *
 * The driver aims by running that backwards (FDreamDriverProjection's camera overloads): the widget's
 * canvas point -> its view point, which is the UV -> the point of the surface's mesh carrying that UV
 * -> that world point through the camera. The first test pins the aim against the production pieces it
 * has to agree with -- the camera's own ray, the surface's own UV function, the canvas's own
 * deprojection -- and is expected green. The world trace test pins step 1's precondition.
 *
 * THE CLICK AND THE DRAG ARE THE ROAD END TO END. Both were red for two runtime reasons, not driver
 * ones. Step 2 had no caller: the event system dispatched only to UDreamUIBehaviours on UDreamWidgets
 * (ExecuteDreamUIInterface), the interaction is an actor component, and the only hit a world pointer
 * gets from the surface carries no widget, so nothing ever reached it and its tick returned before
 * doing anything. And the interaction's own trace (LineTrace) ended the canvas ray at the WORLD ray's
 * end point rather than at its own, which bent the canvas ray toward wherever the world ray was going
 * -- invisible for a point straight ahead, visible anywhere else, which is what the drag's halfway
 * value catches.
 *
 * The interaction does its work in TickComponent, which a game's tick manager calls and the headless
 * pump does not; these tests tick it once after every pumped frame (DreamDriverWorld::
 * TickLikeAnEngineFrame), the place in a frame a component in the event system's tick group would run.
 */
namespace DreamDriverRenderTargetMeshTestLocal
{
	const FIntPoint ViewportSize(1280, 720);
	/** One texel per canvas unit per centimetre of surface: 400 x 300 cm, three metres ahead. */
	const FIntPoint TargetSize(400, 300);

	FMinimalViewInfo EyeAtTheOrigin()
	{
		return DreamDriverWorld::MakeView(FVector::ZeroVector, FRotator::ZeroRotator, 90.0f, ViewportSize);
	}

	/** The surface stands 300 cm down the view axis, facing the eye. */
	FTransform SurfaceAhead()
	{
		return FTransform(FVector(300.0, 0.0, 0.0));
	}

	/** A widget's own centre, as a point in whatever space its canvas lives in. */
	FVector CentreOf(const UDreamWidget* InWidget)
	{
		const FVector2D LocalCentre = InWidget->GetLocalSpaceCenter();
		return InWidget->GetWorldTransform().TransformPosition(FVector(0.0, LocalCentre.X, LocalCentre.Y));
	}

	/** A Then step that gives the render-target interaction the tick an engine frame would have given it. */
	TFunction<void(FDreamDriverContext&)> TickTheInteraction(UDreamUIRenderTargetInteraction* InInteraction)
	{
		return [InInteraction](FDreamDriverContext& InContext)
		{
			DreamDriverWorld::TickLikeAnEngineFrame(InInteraction, InContext.FrameSeconds);
		};
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverRenderTargetMeshAimTest,
	"DreamGUI.Driver.RenderTargetMesh.APointOnTheCanvasIsAimedAtThePixelWhoseRayLandsOnItsOwnUV",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverRenderTargetMeshAimTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverRenderTargetMeshTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamDriverWorldSpaceRaycaster* Pointer = DreamDriverWorld::AttachWorldPointer(Rig, EyeAtTheOrigin(), EDreamWorldPointerSource::Mouse);
	const DreamDriverWorld::FDreamRenderTargetMesh Screen = DreamDriverWorld::MakeRenderTargetMesh(Rig, TEXT("Screen"), SurfaceAhead(), TargetSize);
	if (!TestNotNull(TEXT("A world pointer"), Pointer) || !TestTrue(TEXT("The render-target canvas and its surface were built"), Screen.IsComplete()))
	{
		return false;
	}
	// Off-centre on both axes, so a flipped or mirrored mapping lands somewhere else.
	UDreamWidget* Target = Rig.MakeWidget(TEXT("Target"), Screen.CanvasRoot, FVector2D(80.0, 40.0), FVector2D(100.0, 50.0));
	if (!TestNotNull(TEXT("A widget on the render-target canvas"), Target))
	{
		return false;
	}
	Rig.PumpFrames(2);
	const FDreamDriverVirtualCamera* Camera = Rig.Context().Camera.Get();
	if (!TestNotNull(TEXT("Attaching the world pointer gave the rig a camera"), Camera))
	{
		return false;
	}

	TestTrue(TEXT("The surface found for the canvas is the one that was built for it"),
		FDreamDriverProjection::FindSurfaceShowing(Screen.Canvas) == Screen.Surface);
	TestEqual(TEXT("The canvas fitted itself to its texture, across"), Screen.CanvasRoot->GetWidth(), static_cast<float>(TargetSize.X));
	TestEqual(TEXT("... and up"), Screen.CanvasRoot->GetHeight(), static_cast<float>(TargetSize.Y));

	const TOptional<FVector2D> Pixel = FDreamDriverProjection::WidgetCentrePixel(Target, Camera);
	if (!TestTrue(TEXT("Seen through the camera, the widget on the texture has a pixel"), Pixel.IsSet()))
	{
		return false;
	}

	// The camera's own ray through that pixel, onto the surface's plane: the surface's plane is its local
	// X = 0, facing along its forward vector, which is where UpdateMeshData lays the quad.
	FVector RayOrigin = FVector::ZeroVector;
	FVector RayDirection = FVector::ForwardVector;
	if (!TestTrue(TEXT("The camera deprojects the pixel"), Camera->Deproject(Pixel.GetValue(), RayOrigin, RayDirection)))
	{
		return false;
	}
	const FVector OnPlane = FMath::RayPlaneIntersection(RayOrigin, RayDirection,
		FPlane(Screen.Surface->GetComponentLocation(), Screen.Surface->GetForwardVector()));

	// ...read back by the surface's OWN UV function, the one the interaction asks.
	FVector2D SurfaceUV = FVector2D::ZeroVector;
	TestTrue(TEXT("The surface reports a UV for the point the ray lands on"),
		Screen.Surface->LineTraceHitUV(INDEX_NONE, OnPlane, RayOrigin, RayOrigin + RayDirection * 100000.0, SurfaceUV));

	// ...against the widget's own view point on its canvas. A texel and a half: a screen pixel here is
	// under half a centimetre of surface, and the ray is made from the pixel's integer corner.
	const TOptional<FVector2D> CanvasUV = FDreamDriverProjection::WorldPointToViewPoint01(Screen.Canvas, CentreOf(Target));
	if (!TestTrue(TEXT("The widget has a view point on its canvas"), CanvasUV.IsSet()))
	{
		return false;
	}
	TestEqual(TEXT("The ray lands on the widget's own UV, across"), SurfaceUV.X, CanvasUV->X, 1.5 / TargetSize.X);
	TestEqual(TEXT("... and up"), SurfaceUV.Y, CanvasUV->Y, 1.5 / TargetSize.Y);

	// And the canvas's own deprojection of that UV finds the widget: the interaction's second half,
	// through a raycaster of the canvas's own kind (the interaction is one), with the pointer position
	// the screen raycaster expects -- target pixels, Y down, which it flips back to the UV.
	UDreamScreenSpaceRaycaster* CanvasRaycaster = NewObject<UDreamScreenSpaceRaycaster>(GetTransientPackage());
	CanvasRaycaster->SetRootCanvas(Screen.Canvas);
	UDreamPointerEventData* Probe = NewObject<UDreamPointerEventData>(GetTransientPackage());
	Probe->PointerPosition = FVector(SurfaceUV.X * TargetSize.X, (1.0 - SurfaceUV.Y) * TargetSize.Y, 0.0);
	FVector CanvasOrigin = FVector::ZeroVector;
	FVector CanvasDirection = FVector::ForwardVector;
	FVector CanvasEnd = FVector::ForwardVector;
	TArray<FDreamUIHitResult> Hits;
	CanvasRaycaster->Raycast(Probe, CanvasOrigin, CanvasDirection, CanvasEnd, Hits);
	TestTrue(TEXT("The canvas's own ray for that UV finds the widget it was aimed at"),
		Hits.Num() > 0 && Hits[0].Widget.Get() == Target);

	// Without the surface in the picture the widget's pixel is the texture's, which is where the canvas's
	// own raycaster would want the pointer -- and it agrees with the UV above.
	const TOptional<FVector2D> TexturePixel = FDreamDriverProjection::WidgetCentrePixel(Target);
	if (TestTrue(TEXT("Without a camera the widget has a pixel on its texture"), TexturePixel.IsSet()))
	{
		TestEqual(TEXT("... the UV's, across"), TexturePixel->X, SurfaceUV.X * TargetSize.X, 1.5);
		TestEqual(TEXT("... and down"), TexturePixel->Y, (1.0 - SurfaceUV.Y) * TargetSize.Y, 1.5);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverRenderTargetMeshBlocksTest,
	"DreamGUI.Driver.RenderTargetMesh.TheSurfaceBlocksAWorldTraceAndTheWorldPointerSeesItAsAnOccluder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverRenderTargetMeshBlocksTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverRenderTargetMeshTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamDriverWorldSpaceRaycaster* Pointer = DreamDriverWorld::AttachWorldPointer(Rig, EyeAtTheOrigin(), EDreamWorldPointerSource::Mouse);
	const DreamDriverWorld::FDreamRenderTargetMesh Screen = DreamDriverWorld::MakeRenderTargetMesh(Rig, TEXT("Screen"), SurfaceAhead(), TargetSize);
	if (!TestNotNull(TEXT("A world pointer"), Pointer) || !TestTrue(TEXT("The render-target canvas and its surface were built"), Screen.IsComplete()))
	{
		return false;
	}
	Rig.PumpFrames(2);
	const FDreamDriverVirtualCamera* Camera = Rig.Context().Camera.Get();
	const TOptional<FVector2D> Pixel = Camera != nullptr ? Camera->Project(Screen.Surface->GetComponentLocation()) : TOptional<FVector2D>();
	if (!TestTrue(TEXT("The middle of the surface is on screen"), Pixel.IsSet()))
	{
		return false;
	}

	// Step 1's precondition, asked of the engine directly: the surface's body is where it is drawn.
	FVector RayOrigin = FVector::ZeroVector;
	FVector RayDirection = FVector::ForwardVector;
	Camera->Deproject(Pixel.GetValue(), RayOrigin, RayDirection);
	FHitResult WorldHit;
	const bool bBlocked = Rig.GetWorld()->LineTraceSingleByChannel(WorldHit, RayOrigin, RayOrigin + RayDirection * 100000.0,
		UEngineTypes::ConvertToCollisionChannel(TraceTypeQuery1), FCollisionQueryParams(SCENE_QUERY_STAT(DreamDriverRenderTargetMesh), true));
	TestTrue(TEXT("A world trace through the middle of the surface is blocked"), bBlocked);
	TestTrue(TEXT("... by the surface"), bBlocked && WorldHit.GetComponent() == Screen.Surface);

	// And through the world pointer, which is the only way it sees a surface at all: with occlusion on,
	// its trace reports the surface as a hit that carries no widget, at the surface's distance.
	Pointer->SetOccludeByWorld(true);
	UDreamPointerEventData* Probe = NewObject<UDreamPointerEventData>(GetTransientPackage());
	Probe->PointerPosition = FVector(Pixel->X, Pixel->Y, 0.0);
	FVector TraceOrigin = FVector::ZeroVector;
	FVector TraceDirection = FVector::ForwardVector;
	FVector TraceEnd = FVector::ForwardVector;
	TArray<FDreamUIHitResult> Hits;
	Pointer->Raycast(Probe, TraceOrigin, TraceDirection, TraceEnd, Hits);
	if (TestEqual(TEXT("The world pointer's trace has one hit, the surface"), Hits.Num(), 1))
	{
		TestNull(TEXT("... carrying no widget: to the pointer, a surface is an occluder"), Hits[0].Widget.Get());
		// Measured from where the ray starts, which is the near plane and not the eye; two centimetres of
		// slack for the body, which UpdateBodySetup centres half a centimetre off the drawn quad.
		const double SurfaceDistance = FVector::Dist(TraceOrigin, Screen.Surface->GetComponentLocation());
		TestEqual(TEXT("... at the surface's distance along the ray"), static_cast<double>(Hits[0].Distance), SurfaceDistance, 2.0);
	}
	return true;
}

/** The click end to end: world pointer, surface, interaction, the button on the canvas (see the file comment). */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverRenderTargetMeshClickTest,
	"DreamGUI.Driver.RenderTargetMesh.ClickingAButtonShownOnTheSurfaceClicksItOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverRenderTargetMeshClickTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverRenderTargetMeshTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamDriverWorldSpaceRaycaster* Pointer = DreamDriverWorld::AttachWorldPointer(Rig, EyeAtTheOrigin(), EDreamWorldPointerSource::Mouse);
	const DreamDriverWorld::FDreamRenderTargetMesh Screen = DreamDriverWorld::MakeRenderTargetMesh(Rig, TEXT("Screen"), SurfaceAhead(), TargetSize);
	if (!TestNotNull(TEXT("A world pointer"), Pointer) || !TestTrue(TEXT("The render-target canvas and its surface were built"), Screen.IsComplete()))
	{
		return false;
	}
	// The only way a world pointer's trace meets a surface at all.
	Pointer->SetOccludeByWorld(true);

	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	UDreamButton* Button = Rig.MakeControl<UDreamButton>(TEXT("Play"), Screen.CanvasRoot, FVector2D(160.0, 60.0));
	if (!TestNotNull(TEXT("A button on the render-target canvas"), Button))
	{
		return false;
	}
	Button->OnClicked.AddDynamic(Listener.Get(), &UDreamPressInteractionListener::HandleClicked);
	Button->OnPressed.AddDynamic(Listener.Get(), &UDreamPressInteractionListener::HandlePressed);
	Rig.PumpFrames(2);

	const TOptional<FVector2D> Pixel = FDreamDriverProjection::WidgetCentrePixel(Button, Rig.Context().Camera.Get());
	if (!TestTrue(TEXT("The button shown on the surface has a pixel"), Pixel.IsSet()))
	{
		return false;
	}

	// Move, press, release, one frame each, with the interaction's tick after every frame.
	const TFunction<void(FDreamDriverContext&)> Tick = TickTheInteraction(Screen.Interaction);
	TestTrue(TEXT("The click at the button's pixel completes"), Rig.Driver()->Sequence()
		.MoveToPixel(Pixel.GetValue())
		.Then(Tick)
		.Press()
		.Then(Tick)
		.Release()
		.Then(Tick)
		.WaitFrames(1)
		.Then(Tick)
		.Perform());
	TestEqual(TEXT("The button shown on the surface was pressed once"), Listener->PressedCount, 1);
	TestEqual(TEXT("... and clicked once"), Listener->ClickedCount, 1);
	return true;
}

/** The drag end to end, off the view axis, so it also holds the canvas ray to its own end point (see the file comment). */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverRenderTargetMeshSliderTest,
	"DreamGUI.Driver.RenderTargetMesh.DraggingASliderShownOnTheSurfaceHalfwayMovesItsValueHalfway",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverRenderTargetMeshSliderTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverRenderTargetMeshTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamDriverWorldSpaceRaycaster* Pointer = DreamDriverWorld::AttachWorldPointer(Rig, EyeAtTheOrigin(), EDreamWorldPointerSource::Mouse);
	const DreamDriverWorld::FDreamRenderTargetMesh Screen = DreamDriverWorld::MakeRenderTargetMesh(Rig, TEXT("Screen"), SurfaceAhead(), TargetSize);
	if (!TestNotNull(TEXT("A world pointer"), Pointer) || !TestTrue(TEXT("The render-target canvas and its surface were built"), Screen.IsComplete()))
	{
		return false;
	}
	Pointer->SetOccludeByWorld(true);

	UDreamSlider* Slider = Rig.MakeControl<UDreamSlider>(TEXT("Volume"), Screen.CanvasRoot, FVector2D(300.0, 40.0));
	Rig.PumpFrames(2);
	if (!TestTrue(TEXT("The slider came up with a handle and a handle area"),
		Slider != nullptr && Slider->HandleNode != nullptr && Slider->HandleAreaNode != nullptr))
	{
		return false;
	}
	const FDreamDriverVirtualCamera* Camera = Rig.Context().Camera.Get();
	const TOptional<FBox2D> Travel = FDreamDriverProjection::WidgetToPixelRect(Slider->HandleAreaNode.Get(), Camera);
	const TOptional<FVector2D> Grip = FDreamDriverProjection::WidgetCentrePixel(Slider->HandleNode.Get(), Camera);
	if (!TestTrue(TEXT("The handle's travel and the handle are on screen"), Travel.IsSet() && Grip.IsSet()))
	{
		return false;
	}
	const double TravelLength = Travel->Max.X - Travel->Min.X;
	if (!TestTrue(FString::Printf(TEXT("The travel is long enough to aim at (%.1f pixels)"), TravelLength), TravelLength > 100.0))
	{
		return false;
	}
	TStrongObjectPtr<UDreamDragInteractionProbe> Values(NewObject<UDreamDragInteractionProbe>());
	Slider->OnValueChanged.AddDynamic(Values.Get(), &UDreamDragInteractionProbe::RecordFloat);

	// Past any threshold on the first move (forty pixels is nineteen texels, against the canvas pointer's
	// five), halfway, then there; the interaction ticked after every frame.
	const FVector2D Halfway(Travel->Min.X + 0.5 * TravelLength, Grip->Y);
	const FVector2D FirstMove = Grip.GetValue() + FVector2D(40.0, 0.0);
	const TFunction<void(FDreamDriverContext&)> Tick = TickTheInteraction(Screen.Interaction);
	TestTrue(TEXT("The drag along the slider shown on the surface completes"), Rig.Driver()->Sequence()
		.MoveToPixel(Grip.GetValue())
		.Then(Tick)
		.Press()
		.Then(Tick)
		.MoveToPixel(FirstMove)
		.Then(Tick)
		.MoveToPixel((FirstMove + Halfway) * 0.5)
		.Then(Tick)
		.MoveToPixel(Halfway)
		.Then(Tick)
		.WaitFrames(1)
		.Then(Tick)
		.Release()
		.Then(Tick)
		.Perform());
	TestTrue(TEXT("The slider reported a value change"), Values->NumFloats() > 0);
	// Two percent: a pixel here is a fraction of a texel, so anything further off is the canvas ray
	// landing somewhere other than where the surface was hit.
	TestNearlyEqual(TEXT("The value is halfway"), Slider->GetValue(), 0.5f, 0.02f);
	return true;
}

#endif
