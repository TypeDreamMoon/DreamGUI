// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraTypes.h"
#include "Core/Components/DreamCanvas.h"
#include "Event/DreamWorldSpaceRaycaster.h"
#include "Templates/SharedPointer.h"

#include "Driver/DreamDriverVirtualCamera.h"

#include "DreamDriverWorldSpace.generated.h"

class AActor;
class APlayerController;
class FDreamDriverRig;
class UActorComponent;
class UDreamUIRenderTargetGeometrySource;
class UDreamUIRenderTargetInteraction;
class UDreamWidget;
class USceneComponent;
class UTextureRenderTarget2D;
struct FDreamDriverContext;

/**
 * The production world-space raycaster with one thing swapped: where its ray comes from.
 *
 * UDreamWorldSpaceRaycaster::GenerateRay deprojects the pointer through its player's LocalPlayer, and a
 * headless world has no LocalPlayer with a viewport -- so without this every world-space trace comes
 * back empty for a reason that has nothing to do with the UI being traced. This override makes the
 * same ray from FDreamDriverVirtualCamera instead (see that struct for the step-by-step equivalence),
 * and it is the ONLY override: which canvases are visited and how their hits are ordered (Raycast), when
 * a press becomes a drag (ShouldStartDrag, bHoldToDrag), how far apart two presses may be and still be a
 * double click (IsWithinDoubleClickDistance), world occlusion -- all of it is the production class's.
 *
 * The camera is held by shared pointer and it is the same object as FDreamDriverContext::Camera, which
 * is what FDreamDriverProjection reads to aim: one camera, read by both halves of the round trip. It is
 * not a UPROPERTY, because a camera is a property of the run, not of anything that could be saved.
 */
UCLASS(NotBlueprintable, NotBlueprintType, Transient, HideDropdown)
class UDreamDriverWorldSpaceRaycaster : public UDreamWorldSpaceRaycaster
{
	GENERATED_BODY()

public:
	/** Shared with FDreamDriverContext::Camera. Null makes this raycaster refuse to produce a ray, as a player with no viewport does. */
	TSharedPtr<FDreamDriverVirtualCamera> VirtualCamera;

	/**
	 * The production GenerateRay's shape with the camera in place of the LocalPlayer: the configured ray
	 * length first, then the screen position -- the pointer's own for Mouse, the middle of the viewport
	 * for ScreenCenter -- deprojected, and the end RayLength along it.
	 */
	virtual bool GenerateRay(UDreamPointerEventData* InPointerEventData, FVector& OutRayOrigin, FVector& OutRayDirection,
		FVector& OutRayEnd, float& OutRayLength) override;
};

/**
 * Standing up the world a world-space pointer needs, on a headless rig.
 *
 * Everything here builds through the runtime's own verbs -- UDreamUIBPLibrary::ConstructWidget and
 * AttachWidgetToSceneComponent for a panel, the geometry source's SetCanvas for a render-target surface,
 * the raycaster registered the way UDreamUIManagerWorldSubsystem::EnsureInteractionForPlayer registers
 * one -- so what a test drives is what a game would have built, not a fixture's idea of it.
 *
 * CONTROLS ON A PANEL are made with the rig's own MakeControl / MakeWidget, passing the panel's root as
 * the parent: both parent under any registered widget, and the control inherits the panel's canvas like
 * any child. They are NOT under the rig's root, so a locator that searches from the root (FDreamBy::Name,
 * Path, Class, Predicate) will not find them; aim at them with FDreamBy::Widget.
 */
namespace DreamDriverWorld
{
	/**
	 * A perspective view from InLocation along InRotation that sees InHorizontalFieldOfView degrees
	 * across a viewport of InViewportSize.
	 *
	 * The view's aspect ratio is set to the viewport's, which is what makes FOV mean "degrees across
	 * this viewport" under every axis constraint -- FMinimalViewInfo's own 4:3 default would stretch it;
	 * see FDreamDriverVirtualCamera.
	 */
	FMinimalViewInfo MakeView(const FVector& InLocation, const FRotator& InRotation, float InHorizontalFieldOfView,
		const FIntPoint& InViewportSize);

	/**
	 * An orthographic view InOrthoWidth world units across, with the engine's automatic near and far
	 * planes switched off (near 0, far the engine's default), so every ray starts on the camera's own
	 * plane and a test can say where. A camera manager's orthographic view keeps its automatic planes;
	 * mirror one with MirrorPlayerView rather than with this.
	 */
	FMinimalViewInfo MakeOrthographicView(const FVector& InLocation, const FRotator& InRotation, float InOrthoWidth,
		const FIntPoint& InViewportSize);

	/**
	 * Copy what a live local player sees into OutCamera: the camera manager's cached view with its FOV
	 * and the controller's view point on top -- ULocalPlayer::GetViewPoint's own three lines, as the
	 * engine's protected function cannot be called from here -- and the viewport client's size. The
	 * player's own aspect-ratio axis constraint is carried over when the view does not state one.
	 *
	 * For a PIE player. False, with OutWhyNot saying why, when there is no controller, no local player,
	 * no viewport client or no viewport, and for a split-screen player (Origin/Size not the whole
	 * viewport), whose view rect the camera does not model. Scene view extensions that rewrite a view
	 * point, and a locked view (FLockedViewState), are not applied.
	 */
	bool MirrorPlayerView(const APlayerController* InController, FDreamDriverVirtualCamera& OutCamera, FString& OutWhyNot);

	/**
	 * Give the rig a world-space pointer: a UDreamDriverWorldSpaceRaycaster on the rig's host actor,
	 * looking through InView at the rig's viewport, answering to InSource, and the rig context's Camera
	 * set to the same camera so the driver aims through it. Returns the raycaster for the test to tune
	 * (SetDragThreshold, SetHoldToDrag, SetOccludeByWorld, SetPointerSource...).
	 *
	 * Registered as EnsureInteractionForPlayer registers its own -- NewObject on a host, the event
	 * system's UserIndex, AddInstanceComponent, RegisterComponent -- and then enrolled explicitly with
	 * ActivateRaycaster, because a component on an actor nobody initialized for play never auto-activates
	 * (the rig's own screen raycaster is enrolled the same way). Enrolled, it is the answer
	 * EnsureInteractionForPlayer finds when anything later asks for a world pointer for this player, so it
	 * is never doubled. Attach it BEFORE anything that would ask -- a UDreamWorldWidgetComponent beginning
	 * play -- because if the manager has already made one, two rays would trace for one player; that case
	 * is reported to the running test and nothing is attached.
	 */
	UDreamDriverWorldSpaceRaycaster* AttachWorldPointer(FDreamDriverRig& InRig, const FMinimalViewInfo& InView, EDreamWorldPointerSource InSource);

	/**
	 * The same for any context and host -- a rig that is not an FDreamDriverRig. InViewportSize is the
	 * viewport the pointer's pixels are measured on, which for a headless rig is its root canvas's.
	 */
	UDreamDriverWorldSpaceRaycaster* AttachWorldPointer(FDreamDriverContext& InContext, AActor* InHost, const FMinimalViewInfo& InView,
		const FIntPoint& InViewportSize, EDreamWorldPointerSource InSource);

	/**
	 * A world-space panel: a root widget InSize world units across (1 canvas unit = 1 cm), carrying a
	 * canvas in InRenderMode, attached to its own host actor's scene component at InTransform. Returns
	 * the root; the panel's plane is the host's local X = 0 plane, the panel's 2D X runs along the host's
	 * local Y and its 2D Y along local Z, so an unrotated host is seen face-on by a camera looking down
	 * +X. Move it through GetPanelHost.
	 *
	 * bInHitTestableBackground gives the root a raycast-target visual, as a panel with a background
	 * image has. Off by default so that a test about a control is not also a test about what the panel
	 * behind it does -- which has a test of its own.
	 *
	 * WorldSpace_DreamUI by default, because that is what UDreamWorldWidgetComponent's default backend
	 * maps to; WorldSpace (the engine renderer) is traced identically.
	 */
	UDreamWidget* MakeWorldPanel(FDreamDriverRig& InRig, const FString& InName, const FTransform& InTransform, const FVector2D& InSize,
		bool bInHitTestableBackground = false, EDreamRenderMode InRenderMode = EDreamRenderMode::WorldSpace_DreamUI);

	/** The scene component a panel made by MakeWorldPanel follows, or null. Moving it moves the panel, at once. */
	USceneComponent* GetPanelHost(const UDreamWidget* InPanelRoot);

	/** A render-target canvas and the surface that shows it in the world. */
	struct FDreamRenderTargetMesh
	{
		/** The canvas's root widget. Controls go under it with MakeControl / MakeWidget, like a panel's. */
		UDreamWidget* CanvasRoot = nullptr;
		UDreamCanvas* Canvas = nullptr;
		/** The texture the canvas draws into and the surface shows. One texel is one canvas unit is one centimetre of surface. */
		UTextureRenderTarget2D* RenderTarget = nullptr;
		/** The plane that shows the texture, and the actor's root, and what the tree follows. */
		UDreamUIRenderTargetGeometrySource* Surface = nullptr;
		/** The component that carries a world pointer's hit on the surface into the canvas. See TickLikeAnEngineFrame. */
		UDreamUIRenderTargetInteraction* Interaction = nullptr;
		AActor* Actor = nullptr;

		bool IsComplete() const;
	};

	/**
	 * A render target of InRenderTargetSize texels, a RenderTarget-mode canvas that fits itself to it,
	 * and a Plane geometry source showing it at InTransform on an actor that also carries a
	 * UDreamUIRenderTargetInteraction -- the arrangement a presenter, a geometry source and an interaction
	 * component make on one actor in a level, with the geometry source handed its canvas directly rather
	 * than through a presenter reference.
	 *
	 * The surface blocks every trace channel (query only), so a world pointer with bOccludeByWorld finds
	 * it. The canvas's tree is attached to the surface the way a presenter attaches the tree it loads,
	 * which is also what takes the root out of the parked state.
	 */
	FDreamRenderTargetMesh MakeRenderTargetMesh(FDreamDriverRig& InRig, const FString& InName, const FTransform& InTransform,
		const FIntPoint& InRenderTargetSize);

	/**
	 * One engine frame's tick of a single actor component, through the base class where the engine calls it.
	 *
	 * For UDreamUIRenderTargetInteraction, which does all its work -- tracing its surface, driving the
	 * pointer it synthesises for the canvas -- in its own TickComponent. A game's tick manager calls that
	 * every frame; the headless pump ticks only the event system, so a test that wants the interaction to
	 * run calls this once per frame, after the frame's input has been processed, which is where a
	 * component in the same tick group as the event system would run.
	 */
	void TickLikeAnEngineFrame(UActorComponent* InComponent, float InDeltaSeconds);
}
