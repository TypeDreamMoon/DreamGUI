// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "Misc/AutomationTest.h"
#include "Templates/UnrealTemplate.h"

#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamGUISettings.h"
#include "Core/DreamUIInputServices.h"
#include "Event/DreamScreenSpaceRaycaster.h"
#include "Interaction/UINavigationInputSelectionHandler.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverProjection.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Driver/DreamDriverTypes.h"
#include "Driver/DreamDriverUntil.h"

/**
 * What the tests of a control DISABLED and of a control on a SCALED CANVAS share, for the controls those tests are about.
 *
 * Two configurations, two sets of helpers:
 *
 *  - A scaled canvas is the rig's root canvas scaled with the screen from a reference resolution chosen to make the 720
 *    pixels of the viewport come out at 0.5 and at 2 (DreamGUI.Driver.CanvasScale.* checks the scale itself). UMG lays a
 *    widget out in the DPI-scaled units of its layer and hands it pointer events in desktop pixels; every widget reads
 *    them through its geometry (FGeometry::AbsoluteToLocal), so a gesture lands where the widget is DRAWN and a distance
 *    the widget measures is in its own units. The helpers aim at points near a control's drawn edges -- just inside and
 *    just past -- because a centre click would land on the same control whether or not the scale had reached it.
 *
 *  - A disabled control is off Slate's hit path (FHittestGrid::GetBubblePath, SlateCore/Private/Input/HittestGrid.cpp:241,
 *    cuts the path at the first disabled widget), is passed over by navigation (FindFocusableWidget, HittestGrid.cpp:471)
 *    and hears no key (FSlateApplication routes OnKeyDown and OnKeyUp only to enabled widgets on the focus path,
 *    Slate/Private/Framework/Application/SlateApplication.cpp:5049 and :5117).
 *
 * The focus ring is what a player sees of keyboard and pad focus; on a scaled canvas it has to sit on the focused control
 * where the control is drawn, at the control's drawn size (UUINavigationInputSelectionHandler sizes it to the control,
 * DreamGUI.Navigation.Focus.TheRingCoversTheControlItMarksAndKeepsOffOneThatMarksItsOwn), as Slate draws a focused
 * widget's focus brush on its own geometry.
 *
 * Plain inline functions rather than a fixture: each test still reads top to bottom. Every test binds its rig itself.
 */
namespace DreamControlsConfigTest
{
	/** The viewport every test here drives. */
	inline FIntPoint ViewportSize()
	{
		return FIntPoint(1280, 720);
	}

	/** The two canvas scales every scaled test runs at, half and double. */
	inline TArray<double> Scales()
	{
		return { 0.5, 2.0 };
	}

	/** A 1280x720 viewport whose root canvas scales with the screen to InScale, height-matched. */
	inline FDreamRigOptions ScaledOptions(double InScale)
	{
		FDreamRigOptions Options;
		Options.ViewportSize = ViewportSize();
		Options.CanvasScaleMode = EDreamCanvasScaleMode::ScaleWithScreenSize;
		Options.ReferenceResolution = FVector2D(ViewportSize().X / InScale, ViewportSize().Y / InScale);
		Options.MatchFromWidthToHeight = 1.0f;
		return Options;
	}

	/** "[scale 0.5] what", so a failure says which scale it failed at. */
	inline FString At(double InScale, const FString& InWhat)
	{
		return FString::Printf(TEXT("[scale %.1f] %s"), InScale, *InWhat);
	}

	/** Whether the rig came up at the scale it was asked for. The test binds the rig before asking. */
	inline bool CameUpAt(FAutomationTestBase& InTest, FDreamDriverRig& InRig, double InScale)
	{
		return InTest.TestTrue(At(InScale, TEXT("The rig came up")), InRig.IsUsable())
			&& InTest.TestNearlyEqual(*At(InScale, TEXT("The canvas is at the scale asked for")),
				static_cast<double>(InRig.RootCanvas()->GetCanvasScale()), InScale, 1.0e-3);
	}

	/** "(x0, y0)-(x1, y1)", for a failure that has to say where something was drawn. */
	inline FString Describe(const FBox2D& InRect)
	{
		return FString::Printf(TEXT("(%.1f, %.1f)-(%.1f, %.1f)"), InRect.Min.X, InRect.Min.Y, InRect.Max.X, InRect.Max.Y);
	}

	/** Where InWidget is drawn, in viewport pixels, or unset. */
	inline TOptional<FBox2D> DrawnRect(const UDreamWidget* InWidget)
	{
		return InWidget != nullptr ? FDreamDriverProjection::WidgetToPixelRect(InWidget) : TOptional<FBox2D>();
	}

	/** Whether InRect is InUnits canvas units across and high at InScale, to within a pixel and a half. */
	inline bool IsDrawnAtScale(const FBox2D& InRect, const FVector2D& InUnits, double InScale)
	{
		return FMath::Abs(InRect.GetSize().X - InUnits.X * InScale) <= 1.5 && FMath::Abs(InRect.GetSize().Y - InUnits.Y * InScale) <= 1.5;
	}

	/** The smallest rect holding InPart and, when it is set, InWhole: for a press past a part that must be past the whole control too. */
	inline FBox2D Covering(const FBox2D& InPart, const TOptional<FBox2D>& InWhole)
	{
		FBox2D Result = InPart;
		if (InWhole.IsSet())
		{
			Result += InWhole.GetValue();
		}
		return Result;
	}

	/** InInset pixels in from InRect's top-left corner, and from its bottom-right: on the control, near where it ends as drawn. */
	inline FVector2D InsideTopLeft(const FBox2D& InRect, double InInset)
	{
		return InRect.Min + FVector2D(InInset, InInset);
	}

	inline FVector2D InsideBottomRight(const FBox2D& InRect, double InInset)
	{
		return InRect.Max - FVector2D(InInset, InInset);
	}

	/** InGap pixels past InRect's drawn right edge and past its drawn bottom edge, level with its centre. */
	inline FVector2D PastRightEdge(const FBox2D& InRect, double InGap)
	{
		return FVector2D(InRect.Max.X + InGap, InRect.GetCenter().Y);
	}

	inline FVector2D PastBottomEdge(const FBox2D& InRect, double InGap)
	{
		return FVector2D(InRect.GetCenter().X, InRect.Max.Y + InGap);
	}

	/** A click at a pixel: the pointer there, pressed and let go, a frame each. */
	inline bool ClickAt(FDreamDriverRig& InRig, const FVector2D& InPixel)
	{
		return InRig.Driver()->Sequence().MoveToPixel(InPixel).Press().Release().Perform();
	}

	/** A tap at a pixel: finger InFinger down there and lifted, a frame each. */
	inline bool TapAt(FDreamDriverRig& InRig, const FVector2D& InPixel, int32 InFinger = 0)
	{
		return InRig.Driver()->Sequence().TouchDown(InFinger, InPixel).TouchUp(InFinger).Perform();
	}

	/** The raycaster's drag threshold in pixels on this rig's canvas, and two pixels past it: a first move that is a drag. */
	inline double PastDragThreshold(const FDreamDriverRig& InRig)
	{
		return FMath::Sqrt(static_cast<double>(InRig.Raycaster()->GetScaledDragThresholdSquare())) + 2.0;
	}

	// ---------------------------------------------------------------- focus

	/** Player 0's focus now, or null. */
	inline UDreamWidget* FocusOf(const FDreamDriverRig& InRig)
	{
		const UDreamUIInputServices* Services = UDreamUIInputServices::Get(InRig.GetWorld());
		return Services != nullptr ? Services->GetFocusedWidget(0) : nullptr;
	}

	inline bool IsPartOf(const UDreamWidget* InWidget, const UDreamWidget* InControl)
	{
		return InWidget != nullptr && InControl != nullptr && (InWidget == InControl || InWidget->IsChildOf(InControl));
	}

	/** Whether player 0's focus is on InControl or on something inside it. */
	inline bool FocusIsIn(const FDreamDriverRig& InRig, const UDreamWidget* InControl)
	{
		return IsPartOf(FocusOf(InRig), InControl);
	}

	/** Player 0's focus put on InWidget the way a screen puts it there when it opens (code, not a key). */
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

	/** The pad's confirm, Slate's Accept on a pad, pressed and let go. */
	inline bool PadConfirm(FDreamDriverRig& InRig)
	{
		return PressKey(InRig, EKeys::Gamepad_FaceButton_Bottom);
	}

	/** The settings the ring tests run under, whatever the project's config says: Tab navigates, and keys draw the focus. */
	struct FFocusDrawnByKeys
	{
		TGuardValue<bool> TabNavigation;
		TGuardValue<bool> FocusVisibleOnlyFromKeys;

		FFocusDrawnByKeys()
			: TabNavigation(GetMutableDefault<UDreamGUISettings>()->bTabNavigation, true)
			, FocusVisibleOnlyFromKeys(GetMutableDefault<UDreamGUISettings>()->bFocusVisibleOnlyFromKeys, true)
		{
		}
	};

	/**
	 * Whether the focus ring marking InMarked is shown and sits on it as drawn: the same centre within a pixel and a half,
	 * the same size within two pixels. OutWhere says where both were.
	 */
	inline bool RingSitsOn(const UDreamWidget* InMarked, FString& OutWhere)
	{
		const UUINavigationInputSelectionHandler* Ring = InMarked != nullptr ? UUINavigationInputSelectionHandler::FindFor(InMarked) : nullptr;
		const UDreamWidget* RingWidget = Ring != nullptr ? Ring->GetWidget() : nullptr;
		if (RingWidget == nullptr)
		{
			OutWhere = InMarked != nullptr ? FString::Printf(TEXT("no ring for '%s'"), *InMarked->GetDisplayName()) : FString(TEXT("nothing focused"));
			return false;
		}
		const TOptional<FBox2D> RingRect = DrawnRect(RingWidget);
		const TOptional<FBox2D> MarkedRect = DrawnRect(InMarked);
		if (!RingRect.IsSet() || !MarkedRect.IsSet())
		{
			OutWhere = FString::Printf(TEXT("no pixels for %s"), !RingRect.IsSet() ? TEXT("the ring") : TEXT("the focused control"));
			return false;
		}
		OutWhere = FString::Printf(TEXT("the ring at %s, opacity %.2f; '%s' at %s"), *Describe(RingRect.GetValue()), RingWidget->GetRenderOpacity(),
			*InMarked->GetDisplayName(), *Describe(MarkedRect.GetValue()));
		return RingWidget->GetRenderOpacity() > 0.99f
			&& RingRect->GetCenter().Equals(MarkedRect->GetCenter(), 1.5)
			&& FMath::Abs(RingRect->GetSize().X - MarkedRect->GetSize().X) <= 2.0
			&& FMath::Abs(RingRect->GetSize().Y - MarkedRect->GetSize().Y) <= 2.0;
	}

	/**
	 * Pumped until the ring is shown on player 0's focus and sits on it as drawn. Its flight and fade are its own (a quarter
	 * of a second, UUINavigationInputSelectionHandler's AnimDuration), so the wait is for the ring, with room to spare.
	 */
	inline bool WaitForRingOnFocus(FDreamDriverRig& InRig, FString& OutWhere)
	{
		const FWaitTimeout Timeout = FWaitTimeout::InSeconds(2.0);
		FDreamDriverRig* Rig = &InRig;
		const bool bSits = InRig.Driver()->Wait(FDreamUntil::Condition([Rig]()
		{
			FString Ignored;
			return RingSitsOn(FocusOf(*Rig), Ignored);
		}, Timeout), Timeout, TEXT("the focus ring shown on the focused control where it is drawn"));
		RingSitsOn(FocusOf(InRig), OutWhere);
		return bSits;
	}
}
