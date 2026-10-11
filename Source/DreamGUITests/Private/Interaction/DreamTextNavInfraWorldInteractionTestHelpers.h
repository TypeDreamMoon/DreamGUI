// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Templates/UnrealTemplate.h"

#include "Core/Components/DreamWidget.h"
#include "Core/DreamGUISettings.h"
#include "Core/DreamUIInputServices.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamWorldSpaceRaycaster.h"
#include "Interaction/UINavigationInputSelectionHandler.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverProjection.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Driver/DreamDriverTypes.h"
#include "Driver/DreamDriverUntil.h"
#include "Driver/DreamDriverVirtualCamera.h"
#include "Driver/DreamDriverWorldSpace.h"

/**
 * What the world-space tests of the text fields, the key binder, rich text, dialogs, modals, popups, tooltips, drag and drop,
 * the event components, navigation, the prompt bar, the virtual cursor and the UMG host share.
 *
 * The world is the one the driver's own world-space suites stand up (DreamGUI.Driver.WorldSpace.*): the eye at the origin
 * looking down +X, ninety degrees across a 1280 by 720 viewport, and a panel 300 cm ahead seen face on -- one centimetre of
 * panel is 640 / 300 = 2.13 pixels there. The world pointer is the production world-space raycaster with its ray made from
 * the rig's virtual camera (DreamDriverWorld::AttachWorldPointer), attached before the panel exists, as it asks. Its source
 * is the pointer's own screen position, which is the mouse's, a finger's or the virtual cursor's alike, so every pointer the
 * driver moves reaches the panel through the same ray (UDreamWorldSpaceRaycaster::GenerateRay).
 *
 * The other kind of world-space UI, a canvas drawn into a render target that a mesh in the level shows, is stood up by
 * SetUpSurface (DreamDriverWorld::MakeRenderTargetMesh), the surface three metres ahead and face on.
 *
 * Widgets on a panel or a surface are not under the rig's root, so they are found with FDreamBy::Widget and their pixels are
 * the camera's (FDreamDriverProjection's camera overloads), which every helper below goes through.
 *
 * Navigation on a panel is DreamGUI's directional scan among the world-space widgets (DreamUINavigationScan::ScanDirectional:
 * a world-space widget is compared with world-space widgets only, in the panel's own right and up directions), the focus and
 * the confirm as on a screen; the focus ring hangs on the panel's own root canvas (UUINavigationInputSelectionHandler's ring
 * host is the focused widget's root canvas).
 *
 * Plain inline functions in a namespace of their own, as DreamControlsConfigTest's are. Every test binds its rig itself.
 */
namespace DreamTextNavInfraWorldTest
{
	inline FIntPoint ViewportSize()
	{
		return FIntPoint(1280, 720);
	}

	/** How far ahead of the eye the panel and the surface stand. */
	inline double SceneDistance()
	{
		return 300.0;
	}

	/** Pixels per centimetre of anything facing the eye at SceneDistance: half the viewport across over tan(45) times the distance. */
	inline double PixelsPerCentimetre()
	{
		return 0.5 * ViewportSize().X / SceneDistance();
	}

	/** The panel's size in centimetres: inside the 600 by 337.5 the eye sees across and up at that distance. */
	inline FVector2D PanelSize()
	{
		return FVector2D(560.0, 320.0);
	}

	/** The texture a surface shows, in texels -- one texel to the centimetre of surface. */
	inline FIntPoint SurfaceSize()
	{
		return FIntPoint(400, 300);
	}

	inline FMinimalViewInfo EyeAtTheOrigin()
	{
		return DreamDriverWorld::MakeView(FVector::ZeroVector, FRotator::ZeroRotator, 90.0f, ViewportSize());
	}

	/** The rig's options: the viewport above, input entering where InHost says. */
	inline FDreamRigOptions RigOptions(EDreamRigInputHost InHost = EDreamRigInputHost::ModuleOnly)
	{
		FDreamRigOptions Options;
		Options.ViewportSize = ViewportSize();
		Options.InputHost = InHost;
		return Options;
	}

	/** The world pointer and the panel in front of it. */
	struct FWorldStage
	{
		UDreamDriverWorldSpaceRaycaster* Pointer = nullptr;
		UDreamWidget* Panel = nullptr;

		bool IsReady() const { return Pointer != nullptr && Panel != nullptr; }
	};

	/** The world pointer first, then the panel, PanelSize() across, SceneDistance() ahead of it and facing it. */
	inline FWorldStage SetUpPanel(FAutomationTestBase& InTest, FDreamDriverRig& InRig)
	{
		FWorldStage Stage;
		Stage.Pointer = DreamDriverWorld::AttachWorldPointer(InRig, EyeAtTheOrigin(), EDreamWorldPointerSource::Mouse);
		Stage.Panel = DreamDriverWorld::MakeWorldPanel(InRig, TEXT("Panel"), FTransform(FVector(SceneDistance(), 0.0, 0.0)), PanelSize());
		InTest.TestNotNull(TEXT("A world pointer was attached to the rig"), Stage.Pointer);
		InTest.TestNotNull(TEXT("A world-space panel was built in front of it"), Stage.Panel);
		return Stage;
	}

	/** The world pointer and a render-target surface showing a canvas. */
	struct FSurfaceStage
	{
		UDreamDriverWorldSpaceRaycaster* Pointer = nullptr;
		DreamDriverWorld::FDreamRenderTargetMesh Screen;

		bool IsReady() const { return Pointer != nullptr && Screen.IsComplete(); }
	};

	/** The world pointer first, then a SurfaceSize() texture shown on a plane SceneDistance() ahead, facing it. */
	inline FSurfaceStage SetUpSurface(FAutomationTestBase& InTest, FDreamDriverRig& InRig)
	{
		FSurfaceStage Stage;
		Stage.Pointer = DreamDriverWorld::AttachWorldPointer(InRig, EyeAtTheOrigin(), EDreamWorldPointerSource::Mouse);
		Stage.Screen = DreamDriverWorld::MakeRenderTargetMesh(InRig, TEXT("Screen"), FTransform(FVector(SceneDistance(), 0.0, 0.0)), SurfaceSize());
		if (InTest.TestNotNull(TEXT("A world pointer was attached to the rig"), Stage.Pointer))
		{
			// The only way a world pointer's trace meets a surface: the surface is a blocking body.
			Stage.Pointer->SetOccludeByWorld(true);
		}
		InTest.TestTrue(TEXT("The render-target canvas and the surface showing it were built"), Stage.Screen.IsComplete());
		return Stage;
	}

	inline const FDreamDriverVirtualCamera* CameraOf(const FDreamDriverRig& InRig)
	{
		return InRig.Context().Camera.Get();
	}

	/** The pixel InWidget's centre is seen at, through the rig's camera when it has one -- a screen widget's own otherwise. */
	inline TOptional<FVector2D> PixelOf(const FDreamDriverRig& InRig, const UDreamWidget* InWidget)
	{
		return InWidget != nullptr ? FDreamDriverProjection::WidgetCentrePixel(InWidget, CameraOf(InRig)) : TOptional<FVector2D>();
	}

	/** The pixel box InWidget is seen in, the same way. */
	inline TOptional<FBox2D> RectOf(const FDreamDriverRig& InRig, const UDreamWidget* InWidget)
	{
		return InWidget != nullptr ? FDreamDriverProjection::WidgetToPixelRect(InWidget, CameraOf(InRig)) : TOptional<FBox2D>();
	}

	/** The pixel a point in InWidget's own space (X right, Y up, from its pivot) is seen at, the same way. */
	inline TOptional<FVector2D> LocalPointPixel(const FDreamDriverRig& InRig, const UDreamWidget* InWidget, const FVector2D& InLocalPoint)
	{
		return InWidget != nullptr ? FDreamDriverProjection::WidgetLocalPointToPixel(InWidget, InLocalPoint, CameraOf(InRig)) : TOptional<FVector2D>();
	}

	/** "(x0, y0)-(x1, y1)", for a failure that has to say where something was seen. */
	inline FString Describe(const FBox2D& InRect)
	{
		return FString::Printf(TEXT("(%.1f, %.1f)-(%.1f, %.1f)"), InRect.Min.X, InRect.Min.Y, InRect.Max.X, InRect.Max.Y);
	}

	inline bool IsPartOf(const UDreamWidget* InWidget, const UDreamWidget* InControl)
	{
		return InWidget != nullptr && InControl != nullptr && (InWidget == InControl || InWidget->IsChildOf(InControl));
	}

	/** Player 0's focus now, or null. */
	inline UDreamWidget* FocusOf(const FDreamDriverRig& InRig)
	{
		const UDreamUIInputServices* Services = UDreamUIInputServices::Get(InRig.GetWorld());
		return Services != nullptr ? Services->GetFocusedWidget(0) : nullptr;
	}

	inline bool FocusIsIn(const FDreamDriverRig& InRig, const UDreamWidget* InControl)
	{
		return IsPartOf(FocusOf(InRig), InControl);
	}

	/** Player 0's focus put on InWidget the way a screen puts it there as it opens: code, not a key. */
	inline bool PutFocusOn(FDreamDriverRig& InRig, UDreamWidget* InWidget)
	{
		UDreamUIInputServices* Services = UDreamUIInputServices::Get(InRig.GetWorld());
		return Services != nullptr && InWidget != nullptr && Services->FocusForNavigation(InWidget, 0);
	}

	/** One key pressed and let go through the rig's input host, and a frame for what it asked for to land. */
	inline bool PressKey(FDreamDriverRig& InRig, const FKey& InKey)
	{
		return InRig.Driver()->Sequence().Key(InKey).WaitFrames(1).Perform();
	}

	/** The pad's confirm, pressed and let go, and a frame after. */
	inline bool PadConfirm(FDreamDriverRig& InRig)
	{
		return InRig.Driver()->Sequence().NavigationTrigger(true).NavigationTrigger(false).WaitFrames(1).Perform();
	}

	inline float DoubleClickTimeOf(const FDreamDriverRig& InRig)
	{
		return InRig.EventSystem() != nullptr ? InRig.EventSystem()->GetDoubleClickTime() : 0.0f;
	}

	/** Left alone past the double-click time, so the next press is a press of its own. */
	inline bool WaitOutDoubleClick(FDreamDriverRig& InRig)
	{
		return InRig.Driver()->Sequence().WaitSeconds(DoubleClickTimeOf(InRig) + 0.1f).Perform();
	}

	/** The settings the focus-ring checks run under, whatever the project's config says: keys draw the focus. */
	struct FFocusDrawnByKeys
	{
		TGuardValue<bool> FocusVisibleOnlyFromKeys;

		FFocusDrawnByKeys()
			: FocusVisibleOnlyFromKeys(GetMutableDefault<UDreamGUISettings>()->bFocusVisibleOnlyFromKeys, true)
		{
		}
	};

	/**
	 * Whether the focus ring marking InMarked is shown and sits on it as the camera sees both: the same centre and the same
	 * size, each to within a centimetre and a half of panel. OutWhere says where both were seen.
	 */
	inline bool RingSitsOn(const FDreamDriverRig& InRig, const UDreamWidget* InMarked, FString& OutWhere)
	{
		const UUINavigationInputSelectionHandler* Ring = InMarked != nullptr ? UUINavigationInputSelectionHandler::FindFor(InMarked) : nullptr;
		const UDreamWidget* RingWidget = Ring != nullptr ? Ring->GetWidget() : nullptr;
		if (RingWidget == nullptr)
		{
			OutWhere = InMarked != nullptr ? FString::Printf(TEXT("no ring for '%s'"), *InMarked->GetDisplayName()) : FString(TEXT("nothing focused"));
			return false;
		}
		const TOptional<FBox2D> RingRect = RectOf(InRig, RingWidget);
		const TOptional<FBox2D> MarkedRect = RectOf(InRig, InMarked);
		if (!RingRect.IsSet() || !MarkedRect.IsSet())
		{
			OutWhere = FString::Printf(TEXT("no pixels for %s"), !RingRect.IsSet() ? TEXT("the ring") : TEXT("the focused control"));
			return false;
		}
		OutWhere = FString::Printf(TEXT("the ring at %s, opacity %.2f; '%s' at %s"), *Describe(RingRect.GetValue()), RingWidget->GetRenderOpacity(),
			*InMarked->GetDisplayName(), *Describe(MarkedRect.GetValue()));
		const double Tolerance = 1.5 * PixelsPerCentimetre();
		return RingWidget->GetRenderOpacity() > 0.99f
			&& RingRect->GetCenter().Equals(MarkedRect->GetCenter(), Tolerance)
			&& FMath::Abs(RingRect->GetSize().X - MarkedRect->GetSize().X) <= Tolerance
			&& FMath::Abs(RingRect->GetSize().Y - MarkedRect->GetSize().Y) <= Tolerance;
	}

	/** Pumped until the ring is shown on player 0's focus and sits on it as seen, for its own flight and fade with room to spare. */
	inline bool WaitForRingOnFocus(FDreamDriverRig& InRig, FString& OutWhere)
	{
		const FWaitTimeout Timeout = FWaitTimeout::InSeconds(2.0);
		FDreamDriverRig* Rig = &InRig;
		const bool bSits = InRig.Driver()->Wait(FDreamUntil::Condition([Rig]()
		{
			FString Ignored;
			return RingSitsOn(*Rig, FocusOf(*Rig), Ignored);
		}, Timeout), Timeout, TEXT("the focus ring shown on the focused control where the camera sees it"));
		RingSitsOn(InRig, FocusOf(InRig), OutWhere);
		return bSits;
	}
}
