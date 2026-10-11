// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Core/Components/DreamWidget.h"
#include "Event/DreamWorldSpaceRaycaster.h"
#include "Interaction/UINavigationInputSelectionHandler.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverProjection.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Driver/DreamDriverUntil.h"
#include "Driver/DreamDriverVirtualCamera.h"
#include "Driver/DreamDriverWorldSpace.h"
#include "Interaction/DreamControlsConfigInteractionTestHelpers.h"

/**
 * What the tests of a control on a WORLD-SPACE PANEL share, for the controls those tests are about.
 *
 * UMG puts a widget in the level with UWidgetComponent and points at it with UWidgetInteractionComponent: the interaction's
 * trace finds the component, turns the hit into a point on the widget's own window (its draw size, one Slate unit a texel),
 * and hands Slate an ordinary pointer there -- a mouse, or a touch for each pointer index. From there on the widget is what
 * it is on a screen: hit where it is painted, dragged and scrolled by distances in its own units, focused and navigated by
 * Slate's rules. So every claim a control's screen tests make is made again here, unchanged, with the pointer coming through
 * a world pointer -- the production world-space raycaster with its ray made from the rig's virtual camera
 * (DreamDriverWorld::AttachWorldPointer), a finger's ray from the finger's own pixel as a mouse's from the mouse's.
 *
 * The stage is always the same: the eye at the origin looking down +X, ninety degrees across a 1280 by 720 viewport, and a
 * panel PanelDistance ahead whose plane faces it. One canvas unit on a panel is a centimetre, so at that distance a unit is
 * seen 640 / 300 pixels across, the same everywhere on the face-on plane -- which is what lets a gesture be written in pixels
 * and its effect read in the panel's units. Points are aimed near a control's edges as the camera sees them, just inside and
 * just past, because a centre click lands on the control whether or not the ray reached the right place on it.
 *
 * Two things decide how near an edge "just" can be. The ray goes through the whole pixel the pointer is in: the engine's
 * deprojection truncates the screen position (FSceneView::DeprojectScreenToWorld), so a ray aimed at a fractional pixel lands
 * up to a pixel up and to the left of it -- three pixels in and four past leave room for that. And a rounded face takes no
 * hit in the corners its radius cuts off (UDreamRectBlock::bRaycastSupportCornerRadius): three pixels in from a corner, at a
 * little over two pixels to the unit, is off a five-unit arc, so a point near an edge is aimed level with the edge's middle.
 *
 * Controls on a panel are not under the rig's root, so locators that search from the root do not find them; everything here
 * takes the widget itself.
 *
 * Plain inline functions rather than a fixture: each test still reads top to bottom. Every test binds its rig itself.
 */
namespace DreamControlsWorldTest
{
	/** The viewport every test here drives. */
	inline FIntPoint ViewportSize()
	{
		return FIntPoint(1280, 720);
	}

	/** How far down the view axis the panel's plane stands, in centimetres -- in its own units. */
	constexpr double PanelDistance = 300.0;

	/** Pixels a panel unit is seen across, face on at PanelDistance: half the viewport over the half-width a ninety-degree view sees there. */
	inline double PixelsPerUnit()
	{
		return static_cast<double>(ViewportSize().X) * 0.5 / PanelDistance;
	}

	/** The world pointer, and the panel it looks at. */
	struct FWorldStage
	{
		UDreamDriverWorldSpaceRaycaster* Pointer = nullptr;
		UDreamWidget* Panel = nullptr;

		bool IsReady() const { return Pointer != nullptr && Panel != nullptr; }
	};

	/**
	 * The world pointer first -- as in a game, where the player exists before the level's panels begin play -- then a panel
	 * InPanelSize units across PanelDistance ahead of it, facing it, with no hit-testable background: a press that misses
	 * every control on it reaches nothing.
	 */
	inline FWorldStage SetUpPanel(FAutomationTestBase& InTest, FDreamDriverRig& InRig, const FVector2D& InPanelSize = FVector2D(560.0, 320.0))
	{
		FWorldStage Stage;
		Stage.Pointer = DreamDriverWorld::AttachWorldPointer(InRig,
			DreamDriverWorld::MakeView(FVector::ZeroVector, FRotator::ZeroRotator, 90.0f, ViewportSize()), EDreamWorldPointerSource::Mouse);
		Stage.Panel = DreamDriverWorld::MakeWorldPanel(InRig, TEXT("Panel"), FTransform(FVector(PanelDistance, 0.0, 0.0)), InPanelSize);
		InTest.TestNotNull(TEXT("A world pointer was attached to the rig"), Stage.Pointer);
		InTest.TestNotNull(TEXT("A world-space panel was built in front of it"), Stage.Panel);
		return Stage;
	}

	/** The camera the world pointer makes its rays from, which the driver aims through. */
	inline const FDreamDriverVirtualCamera* CameraOf(const FDreamDriverRig& InRig)
	{
		return InRig.Context().Camera.Get();
	}

	/** Where InWidget is seen, in viewport pixels, or unset. */
	inline TOptional<FBox2D> SeenRect(const FDreamDriverRig& InRig, const UDreamWidget* InWidget)
	{
		return InWidget != nullptr ? FDreamDriverProjection::WidgetToPixelRect(InWidget, CameraOf(InRig)) : TOptional<FBox2D>();
	}

	/** The pixel a point of InWidget's own space (X right, Y up, the units GetLocalSpaceLeft answers in) is seen at, or unset. */
	inline TOptional<FVector2D> SeenLocalPoint(const FDreamDriverRig& InRig, const UDreamWidget* InWidget, const FVector2D& InLocalPoint)
	{
		return InWidget != nullptr ? FDreamDriverProjection::WidgetLocalPointToPixel(InWidget, InLocalPoint, CameraOf(InRig)) : TOptional<FVector2D>();
	}

	/** Whether InRect is InUnits panel units across and high as the camera sees them, to within a pixel and a half. */
	inline bool IsSeenAtItsSize(const FBox2D& InRect, const FVector2D& InUnits)
	{
		return FMath::Abs(InRect.GetSize().X - InUnits.X * PixelsPerUnit()) <= 1.5
			&& FMath::Abs(InRect.GetSize().Y - InUnits.Y * PixelsPerUnit()) <= 1.5;
	}

	/**
	 * The world pointer's drag threshold and two pixels past it: a first move that is a drag. A world pointer following the
	 * mouse or a finger measures the move in viewport pixels, unscaled (UDreamWorldSpaceRaycaster::ShouldStartDrag).
	 */
	inline double PastDragThreshold(const UDreamWorldSpaceRaycaster* InPointer)
	{
		return InPointer != nullptr ? FMath::Sqrt(static_cast<double>(InPointer->GetDragThresholdSquare())) + 2.0 : 12.0;
	}

	/**
	 * Where InWidget's centre lies on InPanel, in the panel's units from its middle, X across and Y up: the anchored position
	 * that puts a neighbour of the default pivot level with it.
	 */
	inline FVector2D OnPanel(const UDreamWidget* InPanel, const UDreamWidget* InWidget)
	{
		const FVector2D LocalCentre = InWidget->GetLocalSpaceCenter();
		const FVector WorldCentre = InWidget->GetWorldTransform().TransformPosition(FVector(0.0, LocalCentre.X, LocalCentre.Y));
		const FVector InPanelSpace = InPanel->GetWorldTransform().InverseTransformPosition(WorldCentre);
		const FVector2D PanelCentre = InPanel->GetLocalSpaceCenter();
		return FVector2D(InPanelSpace.Y - PanelCentre.X, InPanelSpace.Z - PanelCentre.Y);
	}

	// ---------------------------------------------------------------- focus

	/**
	 * Whether the focus ring marking InMarked is shown and sits on it as the camera sees both: the same centre within a pixel
	 * and a half, the same size within two pixels. The ring of a panel's screen is the panel's own -- one ring per root canvas
	 * (UUINavigationInputSelectionHandler::FindOrCreateFor) -- and hangs under the control it marks, so it is drawn in the
	 * panel's plane as the control is. OutWhere says where both were.
	 */
	inline bool RingSitsOnAsSeen(const FDreamDriverRig& InRig, const UDreamWidget* InMarked, FString& OutWhere)
	{
		const UUINavigationInputSelectionHandler* Ring = InMarked != nullptr ? UUINavigationInputSelectionHandler::FindFor(InMarked) : nullptr;
		const UDreamWidget* RingWidget = Ring != nullptr ? Ring->GetWidget() : nullptr;
		if (RingWidget == nullptr)
		{
			OutWhere = InMarked != nullptr ? FString::Printf(TEXT("no ring for '%s'"), *InMarked->GetDisplayName()) : FString(TEXT("nothing focused"));
			return false;
		}
		const TOptional<FBox2D> RingRect = SeenRect(InRig, RingWidget);
		const TOptional<FBox2D> MarkedRect = SeenRect(InRig, InMarked);
		if (!RingRect.IsSet() || !MarkedRect.IsSet())
		{
			OutWhere = FString::Printf(TEXT("no pixels for %s"), !RingRect.IsSet() ? TEXT("the ring") : TEXT("the focused control"));
			return false;
		}
		OutWhere = FString::Printf(TEXT("the ring seen at %s, opacity %.2f; '%s' seen at %s"),
			*DreamControlsConfigTest::Describe(RingRect.GetValue()), RingWidget->GetRenderOpacity(),
			*InMarked->GetDisplayName(), *DreamControlsConfigTest::Describe(MarkedRect.GetValue()));
		return RingWidget->GetRenderOpacity() > 0.99f
			&& RingRect->GetCenter().Equals(MarkedRect->GetCenter(), 1.5)
			&& FMath::Abs(RingRect->GetSize().X - MarkedRect->GetSize().X) <= 2.0
			&& FMath::Abs(RingRect->GetSize().Y - MarkedRect->GetSize().Y) <= 2.0;
	}

	/**
	 * Pumped until the ring is shown on player 0's focus and sits on it as the camera sees it. Its flight and fade are its
	 * own (a quarter of a second, UUINavigationInputSelectionHandler's AnimDuration), so the wait is for the ring.
	 */
	inline bool WaitForRingOnFocusAsSeen(FDreamDriverRig& InRig, FString& OutWhere)
	{
		const FWaitTimeout Timeout = FWaitTimeout::InSeconds(2.0);
		const FDreamDriverRig* Rig = &InRig;
		const bool bSits = InRig.Driver()->Wait(FDreamUntil::Condition([Rig]()
		{
			FString Ignored;
			return RingSitsOnAsSeen(*Rig, DreamControlsConfigTest::FocusOf(*Rig), Ignored);
		}, Timeout), Timeout, TEXT("the focus ring shown on the focused control where the camera sees it"));
		RingSitsOnAsSeen(InRig, DreamControlsConfigTest::FocusOf(InRig), OutWhere);
		return bSits;
	}
}
