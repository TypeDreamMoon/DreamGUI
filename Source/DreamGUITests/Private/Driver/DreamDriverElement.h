// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Event/DreamBaseEventData.h"
#include "Event/DreamPointerEventData.h"
#include "InputCoreTypes.h"
#include "Templates/SharedPointer.h"
#include "UObject/WeakObjectPtr.h"

#include "Driver/DreamDriverLocators.h"

class FDreamDriver;
class UDreamWidget;

/**
 * One widget, as something a test can act on and ask about.
 *
 * It holds BOTH the widget it last resolved to and the question that found it. The widget alone would
 * go stale on anything that rebuilds the tree -- a list recycling entries, a prefab reloading, a
 * Blueprint recompiling -- and every test would then have to re-find by hand after each of those. The
 * question alone would be re-answered on every call, which for an ambiguous locator means an action
 * could land on a different widget than the assertion that follows it. So: use the cached widget while
 * it is alive, and ask again exactly once when it is not. This is the engine driver's implicit wait,
 * minus the waiting, which a synchronous pump does not need.
 *
 * ACTIONS ARE HEADLESS ONLY. Each of them pumps frames of its own and returns when the whole gesture
 * has been delivered, which is only meaningful when this driver owns the frames. Under the engine
 * pump the frames belong to the engine, so build a FDreamDriverSequence and PerformLatent it instead;
 * the queries below are safe under either pump.
 */
class FDreamDriverElement : public TSharedFromThis<FDreamDriverElement>
{
public:
	FDreamDriverElement(const TWeakPtr<FDreamDriver>& InDriver, const FDreamLocatorRef& InLocator);

	/** The widget this element stands for, re-located once if the cached one has gone. Null when there is none. */
	UDreamWidget* GetWidget() const;

	/** The question this element was created from, for an error message. */
	FString Describe() const { return Locator->Describe(); }

	/** Whether a widget answers the locator at all. Never an error; an element may legitimately not exist yet. */
	bool Exists() const;
	/** Whether it is drawn -- render visibility all the way up the hierarchy, not just its own flag. */
	bool IsVisible() const;
	/** Whether input would reach it -- interactability all the way up, which is what the hit test reads. */
	bool IsInteractable() const;
	/** Whether the pointer is over it, either as the entered widget or anywhere in the entered chain. */
	bool IsHovered() const;
	/** Whether the trigger went down on it and has not come up. */
	bool IsPressed() const;
	/** Whether the event system's selection is on it. */
	bool IsSelected() const;

	/** Its four corners' bound, in viewport pixels. See FDreamDriverProjection for the Y convention. */
	TOptional<FBox2D> GetPixelRect() const;
	/** The pixel its centre lands on -- the one every action here aims at. */
	TOptional<FVector2D> GetCentrePixel() const;

	/** Move the pointer onto its centre and let a frame pass. */
	bool Hover();
	/** Hover, and the alias the engine driver's vocabulary uses for it. */
	bool MoveTo() { return Hover(); }
	/** Move the pointer by a pixel offset from wherever it is, and let a frame pass. */
	bool MoveBy(const FVector2D& InPixelDelta);

	/** Move to the centre, frame, press, frame, release, frame. */
	bool Click(EDreamUIMouseButtonType InButton = EDreamUIMouseButtonType::Left);
	/** Two clicks close enough together, in the pumped clock, to be one run. */
	bool DoubleClick(EDreamUIMouseButtonType InButton = EDreamUIMouseButtonType::Left);
	/**
	 * Two clicks with InFramesBetween extra frames between the first release and the second click.
	 * Zero is exactly the overload above; enough frames to outlast the event system's double-click
	 * time turns the pair into two single clicks, which is the other half of what a test of the
	 * interval needs.
	 */
	bool DoubleClick(EDreamUIMouseButtonType InButton, int32 InFramesBetween);

	/**
	 * Press on the centre, hold for InSeconds, release where the pointer is. Long enough and the
	 * pointer module reports a long press before the release (the event system's LongPressTime); not
	 * long enough and it is an ordinary press and release.
	 */
	bool LongPress(float InSeconds, EDreamUIMouseButtonType InButton = EDreamUIMouseButtonType::Left);
	/** Press on the centre and hold for InSeconds without letting go; the release is a separate act. */
	bool Hold(float InSeconds, EDreamUIMouseButtonType InButton = EDreamUIMouseButtonType::Left);

	/** Move to the centre and hold the trigger down; the release is a separate act. */
	bool Press(EDreamUIMouseButtonType InButton = EDreamUIMouseButtonType::Left);
	/** Let the trigger up where the pointer currently is. */
	bool Release(EDreamUIMouseButtonType InButton = EDreamUIMouseButtonType::Left);

	/** Press here and let go over InTarget, crossing the drag threshold on the first move. */
	bool DragTo(const TSharedRef<FDreamDriverElement>& InTarget);
	/** The same, ending at a pixel offset from the press rather than over another element. */
	bool DragBy(const FVector2D& InPixelDelta);

	/** Hover, then turn the wheel. Hovering first is what decides who the wheel is delivered to. */
	bool ScrollBy(const FVector2D& InAxisValue);

	/** Press and release a navigation direction, from wherever focus currently is. */
	bool Navigate(EDreamUINavigationDirection InDirection);

	/** Make this the event system's selection, the way a press would. */
	bool Select();

	/**
	 * Type into this element, the way a player would: click it to give it the keyboard, then press
	 * the characters one frame apart. The click is skipped when this element already has the
	 * keyboard -- it is the field being edited, or the key selector that is listening -- because a
	 * player typing into a field does not click it again before every key, and a second click would
	 * move the caret (in a field) or disarm it (on a selector). See FDreamDriverSequence::Type for
	 * where the characters and keys go.
	 */
	bool Type(const FString& InText);
	/** See FDreamDriverSequence::Type(const TCHAR*) for why this overload exists. */
	bool Type(const TCHAR* InText);
	/** One key -- Backspace, Enter, an arrow, Escape -- with the same click-unless-focused rule. */
	bool Type(const FKey& InKey);
	/** One key with a modifier held -- Ctrl+A, Shift+Left -- with the same click-unless-focused rule. */
	bool TypeChord(const FKey& InModifier, const FKey& InKey);

	/** Whether this element is what the keyboard reaches right now: the field being edited or the armed selector, or inside one. */
	bool HasKeyboard() const;

private:
	/** The driver, and through it the context and the pump. Weak, because the rig owns both. */
	TWeakPtr<FDreamDriver> Driver;
	FDreamLocatorRef Locator;
	/** Mutable so a query can pay for the one re-location a stale cache costs. */
	mutable TWeakObjectPtr<UDreamWidget> CachedWidget;
};

using FDreamElementRef = TSharedRef<FDreamDriverElement>;
