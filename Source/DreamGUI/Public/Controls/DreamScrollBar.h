// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Controls/DreamUIControl.h"
#include "Event/DreamBaseEventData.h"
#include "Interaction/UIButton.h"
#include "Interaction/UIScrollbar.h"
#include "Interaction/UIScrollView.h"
#include "DreamScrollBar.generated.h"

class UDreamWidget;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDreamScrollBarValueChangedEvent, float, Value);

/**
 * A scroll bar whose hierarchy is code, not an asset.
 *
 * Two nodes: a track, and a handle inside it. BP_HorizontalScrollbar and BP_VerticalScrollbar are two
 * assets because an asset cannot branch on a property; this is one control because code can, and
 * Direction is the only thing they disagreed about. The handle's parent IS the track, which is what
 * the behaviour measures the value against -- a scroll bar handle rides the whole track (unlike a
 * slider's, which is inset by its own size), because its LENGTH already shrinks the travel.
 *
 * WHAT THIS CLASS STOPPED DOING
 * -----------------------------
 * It used to carry a UUIScrollbar SUBCLASS and re-place the handle itself, because the component
 * wrote ratio anchors and exposed neither Handle nor DirectionType to anything but a details panel.
 * All three of those are fixed at the source now: UUIScrollbar places the handle with absolute
 * geometry, takes both writes through public setters, and owns the minimum-length floor together
 * with the drag scale it has to agree with. What is left here is what a control is for -- style,
 * properties a designer and a `<->` binding can reach, and the link to a scroll view.
 *
 * A bar with no scroll view is a value control in its own right: its value is a position from 0 to 1
 * and HandleSize is how much of the track the handle covers. Point it at a UUIScrollView and both
 * numbers become the view's -- progress in, progress out -- which is how the standalone bar drives a
 * scroll box.
 */
UCLASS(BlueprintType, Blueprintable, DisplayName = "Dream Scroll Bar")
class DREAMGUI_API UDreamScrollBar : public UDreamUIControl
{
	GENERATED_BODY()

public:
	/**
	 * This instance's own look. The project sheet wins while StyleSource says so AND a sheet actually
	 * exists; with no sheet in the project this IS the look in effect. A scroll box overwrites it
	 * wholesale with its own style's Bar, so one style edit dresses the box and its bar together.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetStyle", BlueprintSetter = "SetStyle", Category = "Scroll Bar")
	FDreamScrollBarStyle Style;

	/** Which way it runs, and which end is zero. One property instead of two Blueprint assets. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetDirection", BlueprintSetter = "SetDirection", Category = "Scroll Bar")
	EUIScrollbarDirectionType Direction = EUIScrollbarDirectionType::TopToBottom;

	/** Authored position in; mirror of the behaviour's out. A property so .dui and bindings can see it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetValue", BlueprintSetter = "SetValue", Category = "Scroll Bar", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Value = 0.0f;

	/**
	 * How much of the track the handle covers, 0 to 1 -- the visible fraction of whatever is being
	 * scrolled. A bar attached to a scroll view has this rewritten from the view on every push; a
	 * quarter is what a bare bar shows so it reads as a bar and not as a filled rail.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetHandleSize", BlueprintSetter = "SetHandleSize", Category = "Scroll Bar", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float HandleSize = 0.25f;

	/**
	 * Whether the bar stays put with nothing to scroll -- UMG's bAlwaysShowScrollbar.
	 *
	 * True, not UMG's false: this bar has always drawn whatever it was placed into, and an existing
	 * screen whose bar quietly vanished the moment its list fitted would be a layout that changed
	 * under everyone. Only a bar ATTACHED to a scroll view can hide itself -- a bare bar is a value
	 * control, and it has no "nothing to scroll" to be in.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetAlwaysShowScrollbar", BlueprintSetter = "SetAlwaysShowScrollbar", Category = "Scroll Bar")
	bool bAlwaysShowScrollbar = true;

	/**
	 * Whether the TRACK survives the bar hiding itself -- UMG's bAlwaysShowScrollbarTrack, and the
	 * groove a desktop scroll bar leaves behind when its thumb has nothing to say.
	 *
	 * Only consulted while bAlwaysShowScrollbar is off and the attached view fits: then the bar stays
	 * on screen with its HANDLE asleep, rather than the whole bar going and the layout reflowing.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetAlwaysShowScrollbarTrack", BlueprintSetter = "SetAlwaysShowScrollbarTrack", Category = "Scroll Bar")
	bool bAlwaysShowScrollbarTrack = false;

	/**
	 * Floor on the handle's drawn length, in local units. Pushed into the behaviour, which applies it
	 * to the drawn length and to the drag scale together -- a handle drawn longer than the fraction
	 * it drags with would run at the wrong rate for the whole of a long list.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetMinHandleLength", BlueprintSetter = "SetMinHandleLength", Category = "Scroll Bar", meta = (ClampMin = "0.0"))
	float MinHandleLength = 24.0f;

	/** How far one navigation press moves the value. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetNavigationChangeInterval", BlueprintSetter = "SetNavigationChangeInterval", Category = "Scroll Bar", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float NavigationChangeInterval = 0.1f;

	/**
	 * A step button at each end of the track -- the desktop scroll bar's arrows.
	 *
	 * Off by default, which is what this bar has always drawn and what a touch-first or console UI
	 * wants; on, an arrow is pinned to each end, the track is inset between them so the handle never
	 * travels underneath one, and a click steps the value by ArrowStepSize.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetShowArrows", BlueprintSetter = "SetShowArrows", Category = "Scroll Bar")
	bool bShowArrows = false;

	/** How far one arrow click moves the value, as a fraction of the whole range. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetArrowStepSize", BlueprintSetter = "SetArrowStepSize", Category = "Scroll Bar", meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "bShowArrows"))
	float ArrowStepSize = 0.1f;

	/**
	 * WHICH mouse buttons move this bar -- a bitmask over EDreamUIMouseButtonType, the left button alone
	 * by default, which is SScrollBar's rule: its OnMouseButtonDown answers EKeys::LeftMouseButton and
	 * nothing else, so a right drag on the handle or a right click on the track leaves the bar where it
	 * is and goes on to whatever is behind it. The two arrows answer the same buttons.
	 *
	 * UMG has no knob for this; widen it for a bar that should also answer another button. A touch is
	 * not a mouse button and always counts. Pushed onto the bar's UUIScrollbar and the two arrow
	 * buttons -- UUISelectable::AcceptedMouseButtons is what they consult, the same field UDreamButton
	 * narrows. In .dui it is a number, one bit per EDreamUIMouseButtonType value: 1 is Left, 4 is Right,
	 * 5 is both.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetAcceptedMouseButtons", BlueprintSetter = "SetAcceptedMouseButtons", Category = "Scroll Bar",
		meta = (Bitmask, BitmaskEnum = "/Script/DreamGUI.EDreamUIMouseButtonType"))
	int32 AcceptedMouseButtons = 1 << static_cast<int32>(EDreamUIMouseButtonType::Left);

	/** Re-broadcast from the behaviour, so a consumer binds to the control, not to a part of it. */
	UPROPERTY(BlueprintAssignable, Category = "Scroll Bar")
	FDreamScrollBarValueChangedEvent OnValueChanged;

	/**
	 * The `<->` convention: two-way bindings synthesize their reverse route against this exact name,
	 * so a value control carries it alongside its spoken events. Fires with them.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Scroll Bar")
	FDreamScrollBarValueChangedEvent OnValueChangedBP;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Scroll Bar")
	TObjectPtr<UDreamWidget> TrackNode = nullptr;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Scroll Bar")
	TObjectPtr<UDreamWidget> HandleNode = nullptr;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Scroll Bar")
	TObjectPtr<UUIScrollbar> BarBehaviour = nullptr;

	/** The step buttons. Present in the built-in tree and asleep until bShowArrows wakes them. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Scroll Bar")
	TObjectPtr<UDreamWidget> ArrowStartNode = nullptr;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Scroll Bar")
	TObjectPtr<UDreamWidget> ArrowEndNode = nullptr;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Scroll Bar")
	TObjectPtr<UDreamWidget> ArrowStartGlyphNode = nullptr;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Scroll Bar")
	TObjectPtr<UDreamWidget> ArrowEndGlyphNode = nullptr;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Scroll Bar")
	TObjectPtr<UUIButton> ArrowStartBehaviour = nullptr;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Scroll Bar")
	TObjectPtr<UUIButton> ArrowEndBehaviour = nullptr;

	/** By value, not by reference: a UFUNCTION return has to be a value, and a style is a small struct. */
	UFUNCTION(BlueprintPure, Category = "Scroll Bar")
	FDreamScrollBarStyle GetStyle() const { return Style; }

	/** Replace the whole look and re-push it -- thickness and margin are geometry, not paint. */
	UFUNCTION(BlueprintCallable, Category = "Scroll Bar")
	void SetStyle(const FDreamScrollBarStyle& InStyle);

	UFUNCTION(BlueprintPure, Category = "Scroll Bar")
	EUIScrollbarDirectionType GetDirection() const { return Direction; }

	/** Which way it runs decides the whole rect, so this is a style push and not a field write. */
	UFUNCTION(BlueprintCallable, Category = "Scroll Bar")
	void SetDirection(EUIScrollbarDirectionType InDirection);

	UFUNCTION(BlueprintPure, Category = "Scroll Bar")
	float GetMinHandleLength() const { return MinHandleLength; }

	UFUNCTION(BlueprintCallable, Category = "Scroll Bar")
	void SetMinHandleLength(float InLength);

	UFUNCTION(BlueprintPure, Category = "Scroll Bar")
	float GetNavigationChangeInterval() const { return NavigationChangeInterval; }

	UFUNCTION(BlueprintCallable, Category = "Scroll Bar")
	void SetNavigationChangeInterval(float InInterval);

	UFUNCTION(BlueprintPure, Category = "Scroll Bar")
	float GetArrowStepSize() const { return ArrowStepSize; }

	UFUNCTION(BlueprintCallable, Category = "Scroll Bar")
	void SetArrowStepSize(float InStep);

	UFUNCTION(BlueprintPure, Category = "Scroll Bar")
	int32 GetAcceptedMouseButtons() const { return AcceptedMouseButtons; }

	/** Writes the bitmask and pushes it onto the bar's behaviours at once -- the next press consults it. */
	UFUNCTION(BlueprintCallable, Category = "Scroll Bar")
	void SetAcceptedMouseButtons(UPARAM(meta = (Bitmask, BitmaskEnum = "/Script/DreamGUI.EDreamUIMouseButtonType")) int32 InAcceptedMouseButtons);

	UFUNCTION(BlueprintPure, Category = "Scroll Bar")
	bool GetAlwaysShowScrollbar() const { return bAlwaysShowScrollbar; }

	UFUNCTION(BlueprintCallable, Category = "Scroll Bar")
	void SetAlwaysShowScrollbar(bool bInAlwaysShow);

	UFUNCTION(BlueprintPure, Category = "Scroll Bar")
	bool GetAlwaysShowScrollbarTrack() const { return bAlwaysShowScrollbarTrack; }

	UFUNCTION(BlueprintCallable, Category = "Scroll Bar")
	void SetAlwaysShowScrollbarTrack(bool bInAlwaysShow);

	/**
	 * Position and visible fraction in one call -- UMG's SetState, and the shape a scroll view
	 * actually pushes: the two numbers always move together, and writing them one at a time lays the
	 * handle out twice for one change.
	 *
	 * @param bInCollapseIfNecessary Hide the bar when the fraction says everything fits, as
	 *                               bAlwaysShowScrollbar = false does for an attached view.
	 */
	UFUNCTION(BlueprintCallable, Category = "Scroll Bar")
	void SetState(float InOffsetFraction, float InThumbSizeFraction, bool bInCollapseIfNecessary = false);

	UFUNCTION(BlueprintCallable, Category = "Scroll Bar")
	bool GetShowArrows() const { return bShowArrows; }

	/** Wakes or sleeps the arrows AND re-insets the track, which is why it is a whole style push. */
	UFUNCTION(BlueprintCallable, Category = "Scroll Bar")
	void SetShowArrows(bool bInShowArrows);

	UFUNCTION(BlueprintCallable, Category = "Scroll Bar")
	float GetValue() const;

	UFUNCTION(BlueprintCallable, Category = "Scroll Bar")
	void SetValue(float InValue);

	/** For a follower: moves the handle without telling anyone, so a two-way link cannot ring. */
	UFUNCTION(BlueprintCallable, Category = "Scroll Bar")
	void SetValueWithoutNotify(float InValue);

	UFUNCTION(BlueprintCallable, Category = "Scroll Bar")
	float GetHandleSize() const;

	UFUNCTION(BlueprintCallable, Category = "Scroll Bar")
	void SetHandleSize(float InFraction);

	/** True for LeftToRight and RightToLeft. Everything axis-dependent in here asks this. */
	UFUNCTION(BlueprintPure, Category = "Scroll Bar")
	bool IsHorizontal() const;

	/**
	 * Drive a scroll view with this bar, both ways.
	 *
	 * UUIScrollViewWithScrollbar exists for exactly this and would have been the thing to compose,
	 * but its Viewport and its two scrollbar fields are private with no setters, so a control that
	 * builds its own tree cannot hand it its parts. What it does internally is two subscriptions and
	 * a visible-fraction ratio; that is what this is, and it works against a plain UUIScrollView.
	 */
	UFUNCTION(BlueprintCallable, Category = "Scroll Bar")
	void SetScrollView(UUIScrollView* InView);

	UFUNCTION(BlueprintCallable, Category = "Scroll Bar")
	UUIScrollView* GetScrollView() const;

	/** Take position and visible fraction from the attached view. Called for you whenever it moves. */
	UFUNCTION(BlueprintCallable, Category = "Scroll Bar")
	void RefreshFromScrollView();

	virtual void ApplyStyle() override;

protected:
	virtual void CollectParts(TArray<FDreamControlPart>& OutParts) override;
	virtual void RealizeBuiltIn() override;
	virtual void WireParts() override;

private:
	void HandleValueChanged(float InValue);

	/** Wake or sleep the two step buttons, place them, and inset the track between them. */
	void ApplyArrows(const FDreamScrollBarStyle& InActive);

	void HandleArrowStartClicked();
	void HandleArrowEndClicked();
	void HandleScrollViewProgress(FVector2D InProgress);

	/** AcceptedMouseButtons onto the three behaviours that take a press: the bar and its two arrows. */
	void PushAcceptedMouseButtons();

	/** The one writer of Value/HandleSize and the behaviour's copy of them. */
	void PushValueAndSize(float InValue, float InFraction, bool bInBroadcast);

	/**
	 * Show or hide this bar (and its handle) for a visible fraction of InFraction.
	 *
	 * One place, because three callers ask the same question: the attached view's refresh, SetState's
	 * collapse flag, and the two always-show setters. Everything fits means fraction one.
	 */
	void ApplyAutoHide(float InFraction);

	UPROPERTY(Transient)
	TWeakObjectPtr<UUIScrollView> ScrollView;

	FDelegateHandle ScrollViewDelegateHandle;
};
