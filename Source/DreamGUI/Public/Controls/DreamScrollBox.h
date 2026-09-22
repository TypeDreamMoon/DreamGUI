// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Controls/DreamScrollBar.h"
#include "Controls/DreamUIControl.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Interaction/UIScrollView.h"
#include "Interaction/UISelectable.h"
#include "DreamScrollBox.generated.h"

class UDreamWidget;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDreamScrollBoxScrolledEvent, float, Progress);
/** The USER moved it, and where to -- an OFFSET in local units, which is what UMG reports. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDreamScrollBoxUserScrolledOffsetEvent, float, CurrentOffset);
/** The bar appeared or went away. A bool rather than a Slate visibility, which this library has none of. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDreamScrollBoxBarVisibilityChangedEvent, bool, bVisible);
/** Focus moved to a widget inside the box -- which one, so a consumer need not go looking. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDreamScrollBoxFocusUpdatedEvent, UDreamWidget*, FocusedWidget);

/**
 * A scroll box whose hierarchy is code, not an asset.
 *
 * Three nodes and a bar: a face that carries the box's look, a viewport clipped inside it that holds
 * the scrolling behaviour, and the content stack that slides within the viewport. It is the dropdown
 * list's arrangement -- a UUIScrollView on a clipping node with one scrolled column under it --
 * generalised until the column is anyone's to fill.
 *
 * BP_HorizontalScrollView and BP_VerticalScrollView are two assets because an asset cannot branch on
 * a property. Orientation is the branch: it picks the scrolling axis, the direction the content
 * stacks, and which edge the bar sits on. Deliberately not a third "both" case -- there is no such
 * preset, a content stack has one direction, and one axis means one bar that can actually reach
 * everything.
 *
 * The bar is a real UDreamScrollBar rather than a track and a handle rebuilt here, so the handle
 * geometry that the anchor-resolution rule dictates exists once. The box only tells it which view to
 * follow; the two-way link lives in the bar.
 */
UCLASS(BlueprintType, Blueprintable, DisplayName = "Dream Scroll Box")
class DREAMGUI_API UDreamScrollBox : public UDreamUIControl
{
	GENERATED_BODY()

public:
	/**
	 * This instance's own look. The project sheet wins while StyleSource says so AND a sheet actually
	 * exists; with no sheet in the project this IS the look in effect -- which is why it stays
	 * editable instead of being gated on the enum.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetStyle", BlueprintSetter = "SetStyle", Category = "Scroll Box")
	FDreamScrollBoxStyle Style;

	/**
	 * Which way it scrolls, and which way its content stacks. One property instead of two Blueprint
	 * assets.
	 *
	 * BlueprintSetter, like the four knobs below it: every one of these is read only by ApplyStyle,
	 * so a runtime write straight onto the variable used to change the number and nothing else --
	 * nothing in this family re-derives a control from a property that moved, which is the
	 * SynchronizeProperties tax UDreamUIControl documents. Through the setter the re-push is not
	 * something a caller has to know to make.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetOrientation", BlueprintSetter = "SetOrientation", Category = "Scroll Box")
	EDreamPanelOrientation Orientation = EDreamPanelOrientation::Vertical;

	/** Off means no bar at all, and the viewport keeps the gutter it would have cost. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetShowScrollBar", BlueprintSetter = "SetShowScrollBar", Category = "Scroll Box")
	bool bShowScrollBar = true;

	/**
	 * Whether the bar stays put or disappears while the content already fits. AutoHide follows the
	 * content at run time: every re-measure (AddContent, RefreshContentExtent, a resize) asks again, so
	 * a box that starts or stops overflowing brings its bar out or puts it away, as UMG's does.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetScrollBarVisibility", BlueprintSetter = "SetScrollBarVisibility", Category = "Scroll Box", meta = (EditCondition = "bShowScrollBar"))
	EDreamScrollBoxScrollbarVisibility ScrollBarVisibility = EDreamScrollBoxScrollbarVisibility::AutoHide;

	/**
	 * Local units travelled per mouse-wheel notch. It used to be documented as a MULTIPLIER and
	 * pushed straight into a view that multiplied the raw axis by it, so the default of 1 moved the
	 * content one unit a notch -- a wheel that visibly did nothing. 40 is what
	 * UDreamLayoutContainerScrollBox already uses for the same gesture.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetScrollSensitivity", BlueprintSetter = "SetScrollSensitivity", Category = "Scroll Box", meta = (ClampMin = "0.0"))
	float ScrollSensitivity = 40.0f;

	/** How quickly a flick stops. Zero never slows down; larger stops sooner. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetDecelerateRate", BlueprintSetter = "SetDecelerateRate", Category = "Scroll Box", meta = (ClampMin = "0.0"))
	float DecelerateRate = 0.135f;

	/**
	 * A wheel notch GLIDES instead of teleporting -- UMG's AnimateWheelScrolling. Off by default,
	 * which is what every existing box already does.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetAnimateWheelScrolling", BlueprintSetter = "SetAnimateWheelScrolling", Category = "Scroll Box")
	bool bAnimateWheelScrolling = false;

	/** How long one animated notch takes. Ignored while bAnimateWheelScrolling is off. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetWheelScrollAnimationDuration", BlueprintSetter = "SetWheelScrollAnimationDuration", Category = "Scroll Box", meta = (ClampMin = "0.0", EditCondition = "bAnimateWheelScrolling"))
	float WheelScrollAnimationDuration = 0.15f;

	/**
	 * A plain multiplier on whatever one wheel notch already travels -- UMG's name, and the list
	 * controls' too, so the three agree.
	 *
	 * ScrollSensitivity above is the DISTANCE a notch covers, in local units. This scales it, which is
	 * why both exist: a project that wants "the same feel, twice as fast" writes 2 here rather than
	 * re-tuning a distance that then disagrees with every other box in the game.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetWheelScrollMultiplier", BlueprintSetter = "SetWheelScrollMultiplier", Category = "Scroll Box", meta = (ClampMin = "0.0"))
	float WheelScrollMultiplier = 1.0f;

	/** Whether the wheel is swallowed here or handed to an outer scrolling ancestor at a limit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetConsumeMouseWheel", BlueprintSetter = "SetConsumeMouseWheel", Category = "Scroll Box")
	EDreamScrollBoxConsumeMouseWheel ConsumeMouseWheel = EDreamScrollBoxConsumeMouseWheel::WhenScrollingPossible;

	/**
	 * Whether the bar's TRACK survives the bar auto-hiding -- UMG's AlwaysShowScrollbarTrack, and the
	 * groove a desktop scroll bar leaves behind when its thumb has nothing to say.
	 *
	 * The gutter is spent either way while this is on, which is the point: a list that gains a row
	 * must not reflow its whole content because a bar appeared beside it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "IsAlwaysShowScrollbarTrack", BlueprintSetter = "SetAlwaysShowScrollbarTrack", Category = "Scroll Box")
	bool bAlwaysShowScrollbarTrack = false;

	/** Let the content be pulled past an end and spring back -- UMG's AllowOverscroll. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetAllowOverscroll", BlueprintSetter = "SetAllowOverscroll", Category = "Scroll Box")
	bool bAllowOverscroll = true;

	/**
	 * A whole window of empty space BEFORE the content, so the first item can be scrolled all the way
	 * to the trailing edge -- UMG's BackPadScrolling. A window, not half of one, which is Slate's
	 * arithmetic.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetBackPadScrolling", BlueprintSetter = "SetBackPadScrolling", Category = "Scroll Box")
	bool bBackPadScrolling = false;

	/** A whole window of empty space AFTER the content -- UMG's FrontPadScrolling. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetFrontPadScrolling", BlueprintSetter = "SetFrontPadScrolling", Category = "Scroll Box")
	bool bFrontPadScrolling = false;

	/** Whether a TOUCH drag scrolls this box -- UMG's bEnableTouchScrolling. A mouse drag is unaffected. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetEnableTouchScrolling", BlueprintSetter = "SetEnableTouchScrolling", Category = "Scroll Box")
	bool bEnableTouchScrolling = true;

	/**
	 * Whether a drag with the RIGHT button scrolls it -- UMG's bAllowRightClickDragScrolling. The drag
	 * scrolls from the press, the move that crossed the drag threshold included, as SScrollBox's does,
	 * so the content stays under the pointer that grabbed it. Over the content only: a right drag that
	 * starts on the box's own bar is the bar's, and the bar answers the left button alone (see
	 * UDreamScrollBar::AcceptedMouseButtons), so it scrolls nothing.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetAllowRightClickDragScrolling", BlueprintSetter = "SetAllowRightClickDragScrolling", Category = "Scroll Box")
	bool bAllowRightClickDragScrolling = true;

	/**
	 * Whether a pointer event this box acted on is swallowed -- UMG's bConsumePointerInput. Off lets
	 * it reach whatever is drawn behind the box.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetConsumePointerInput", BlueprintSetter = "SetConsumePointerInput", Category = "Scroll Box")
	bool bConsumePointerInput = true;

	/**
	 * The analog key that acts as this box's mouse wheel -- UMG's AnalogMouseWheelKey. Invalid (the
	 * default) means "whatever the input preset already routes here", which is the right stick and
	 * therefore exactly what every existing box does.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetAnalogMouseWheelKey", BlueprintSetter = "SetAnalogMouseWheelKey", Category = "Scroll Box")
	FKey AnalogMouseWheelKey;

	/**
	 * What this box does when user focus lands inside it -- UMG's ScrollWhenFocusChanges.
	 *
	 * AnimatedScroll rather than UMG's NoScroll, because it is what every box here already does:
	 * directional navigation reveals its target unconditionally, and shipping NoScroll would stop
	 * every gamepad-driven list in every existing project from following its focus.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetScrollWhenFocusChanges", BlueprintSetter = "SetScrollWhenFocusChanges", Category = "Scroll Box")
	EDreamUIScrollWhenFocusChanges ScrollWhenFocusChanges = EDreamUIScrollWhenFocusChanges::AnimatedScroll;

	/**
	 * Where a widget revealed by NAVIGATION ends up -- UMG's NavigationDestination. IntoView moves
	 * the least distance that shows it; the other two always frame it the same way.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetNavigationDestination", BlueprintSetter = "SetNavigationDestination", Category = "Scroll Box", meta = (InvalidEnumValues = "Configured"))
	EDreamUIScrollDestination NavigationDestination = EDreamUIScrollDestination::IntoView;

	/** How much of the window to keep clear around it -- UMG's NavigationScrollPadding. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetNavigationScrollPadding", BlueprintSetter = "SetNavigationScrollPadding", Category = "Scroll Box", meta = (ClampMin = "0.0"))
	float NavigationScrollPadding = 0.0f;

	/**
	 * Position along the scrolling axis, 0 to 1. Authored in; mirror of the behaviour's out, so a
	 * `.dui` binding and the designer can both see it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetScrollProgress", BlueprintSetter = "SetScrollProgress", Category = "Scroll Box", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ScrollProgress = 0.0f;

	/** Re-broadcast from the scroll behaviour, so a consumer binds to the control, not to a part of it. */
	UPROPERTY(BlueprintAssignable, Category = "Scroll Box")
	FDreamScrollBoxScrolledEvent OnScrolled;

	/**
	 * The USER moved it -- UMG's OnUserScrolled, and its unit, which is an OFFSET rather than a
	 * progress.
	 *
	 * Separate from OnScrolled because the question is different: OnScrolled fires for every change,
	 * including the ones this control made itself (a style push re-states the progress; a setter
	 * writes it). A consumer saving "where the player left the list" wants the ones the player made,
	 * and nothing else could tell them apart afterwards.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Scroll Box")
	FDreamScrollBoxUserScrolledOffsetEvent OnUserScrolled;

	/**
	 * The bar appeared or went away -- UMG's OnScrollBarVisibilityChanged. Fired from the style push,
	 * which is the one place that decides, and only when the answer actually moved.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Scroll Box")
	FDreamScrollBoxBarVisibilityChangedEvent OnScrollBarVisibilityChanged;

	/*
	 * The box ITSELF taking and giving up user focus -- UMG's OnFocusReceived and OnFocusLost -- are
	 * not declared here, because every widget already has them: UDreamWidget::OnFocusReceived and
	 * OnFocusLost. What this control adds is the wiring. The event system focuses the FACE, a child
	 * node, so left alone those two events would fire on a part nobody outside holds a pointer to;
	 * the box relays them onto itself, which is where an author binds.
	 *
	 * Only for a box that is a focus target, which means the base's bIsFocusable is on. That gate is
	 * the whole reason this could not simply be switched on: a box that became navigable would change
	 * where every directional press in an existing screen lands, and bIsFocusable is off by default,
	 * so nothing moves until an author asks for it.
	 */

	/**
	 * Focus moved to something INSIDE the box -- UMG's OnFocusUpdated.
	 *
	 * Not gated on bIsFocusable, and that is the point: this is about the box's CONTENT, which is
	 * focusable whether or not the box is. Raised from the navigation reveal, the one place that
	 * knows both which widget took focus and which scrolling ancestors contain it -- a box cannot
	 * work that out alone, and polling HasFocusedDescendants would be a tick for an edge.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Scroll Box")
	FDreamScrollBoxFocusUpdatedEvent OnFocusUpdated;

	/**
	 * The `<->` convention: a two-way binding synthesizes its reverse route against this exact name.
	 * A scroll box's value is where it is scrolled to, so it carries one and fires it with OnScrolled.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Scroll Box")
	FDreamScrollBoxScrolledEvent OnValueChangedBP;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Scroll Box")
	TObjectPtr<UDreamWidget> FaceNode = nullptr;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Scroll Box")
	TObjectPtr<UDreamWidget> ViewportNode = nullptr;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Scroll Box")
	TObjectPtr<UDreamWidget> ContentNode = nullptr;

	virtual TArray<FName> GetNativeSlotNames() const override { return { ContentSlotName }; }
	virtual FName GetDefaultSlotName() const override { return ContentSlotName; }

	/** Named once: the declaration, the node's display name and the binding key are the same string. */
	static const FName ContentSlotName;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Scroll Box")
	TObjectPtr<UDreamScrollBar> ScrollBarNode = nullptr;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Scroll Box")
	TObjectPtr<UUIScrollView> ScrollView = nullptr;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Scroll Box")
	TObjectPtr<UDreamLayoutContainerStackBox> ContentStack = nullptr;

	/** By value, not by reference: a UFUNCTION return has to be a value, and a style is a small struct. */
	UFUNCTION(BlueprintPure, Category = "Scroll Box")
	FDreamScrollBoxStyle GetStyle() const { return Style; }

	/** Replace the whole look and re-push it. The one road a Blueprint write has to take. */
	UFUNCTION(BlueprintCallable, Category = "Scroll Box")
	void SetStyle(const FDreamScrollBoxStyle& InStyle);

	UFUNCTION(BlueprintCallable, Category = "Scroll Box")
	float GetScrollProgress() const;

	UFUNCTION(BlueprintCallable, Category = "Scroll Box")
	void SetScrollProgress(float InProgress);

	UFUNCTION(BlueprintCallable, Category = "Scroll Box")
	EDreamPanelOrientation GetOrientation() const { return Orientation; }

	/** Re-states the axis on the view, the stack and the bar, and re-cuts the gutter. */
	UFUNCTION(BlueprintCallable, Category = "Scroll Box")
	void SetOrientation(EDreamPanelOrientation InOrientation);

	UFUNCTION(BlueprintCallable, Category = "Scroll Box")
	bool GetShowScrollBar() const { return bShowScrollBar; }

	UFUNCTION(BlueprintCallable, Category = "Scroll Box")
	void SetShowScrollBar(bool bInShowScrollBar);

	UFUNCTION(BlueprintCallable, Category = "Scroll Box")
	EDreamScrollBoxScrollbarVisibility GetScrollBarVisibility() const { return ScrollBarVisibility; }

	UFUNCTION(BlueprintCallable, Category = "Scroll Box")
	void SetScrollBarVisibility(EDreamScrollBoxScrollbarVisibility InVisibility);

	UFUNCTION(BlueprintCallable, Category = "Scroll Box")
	float GetScrollSensitivity() const { return ScrollSensitivity; }

	UFUNCTION(BlueprintCallable, Category = "Scroll Box")
	void SetScrollSensitivity(float InSensitivity);

	UFUNCTION(BlueprintCallable, Category = "Scroll Box")
	float GetDecelerateRate() const { return DecelerateRate; }

	UFUNCTION(BlueprintCallable, Category = "Scroll Box")
	void SetDecelerateRate(float InRate);

	/**
	 * The four scroll commands UMG's box offers, forwarded to the behaviour.
	 *
	 * Forwarded rather than left to GetScrollView(): the behaviour has had all of them since it was
	 * written, and a consumer reaching through the accessor to call them is a consumer reaching past
	 * the control into a part the control owns -- which is the one thing this whole family exists to
	 * make unnecessary. GetScrollView() stays for everything genuinely beyond the control's surface.
	 */
	UFUNCTION(BlueprintCallable, Category = "Scroll Box")
	bool GetAnimateWheelScrolling() const { return bAnimateWheelScrolling; }

	UFUNCTION(BlueprintCallable, Category = "Scroll Box")
	void SetAnimateWheelScrolling(bool bInAnimate);

	UFUNCTION(BlueprintCallable, Category = "Scroll Box")
	EDreamUIScrollDestination GetNavigationDestination() const { return NavigationDestination; }

	UFUNCTION(BlueprintCallable, Category = "Scroll Box")
	void SetNavigationDestination(EDreamUIScrollDestination InDestination);

	UFUNCTION(BlueprintCallable, Category = "Scroll Box")
	float GetNavigationScrollPadding() const { return NavigationScrollPadding; }

	UFUNCTION(BlueprintCallable, Category = "Scroll Box")
	void SetNavigationScrollPadding(float InPadding);

	UFUNCTION(BlueprintCallable, Category = "Scroll Box")
	void ScrollToStart();

	UFUNCTION(BlueprintCallable, Category = "Scroll Box")
	void ScrollToEnd();

	/**
	 * Scroll the least distance that brings InWidget fully into view; nothing when it already is.
	 *
	 * @param InDestination Where it ends up. Configured (the default) leaves it to NavigationDestination,
	 *                      which is what this call meant before the parameter existed.
	 * @param InPadding     How much of the window to keep clear around it. Negative leaves it to
	 *                      NavigationScrollPadding, for the same reason.
	 */
	UFUNCTION(BlueprintCallable, Category = "Scroll Box")
	bool ScrollWidgetIntoView(UDreamWidget* InWidget, bool bInAnimate = true,
		EDreamUIScrollDestination InDestination = EDreamUIScrollDestination::Configured, float InPadding = -1.0f);

	/** Drop the fling, keeping the position -- UMG's EndInertialScrolling. */
	UFUNCTION(BlueprintCallable, Category = "Scroll Box")
	void EndInertialScrolling();

	/** True while momentum or a spring-back still has the content moving. */
	UFUNCTION(BlueprintPure, Category = "Scroll Box")
	bool GetIsScrolling() const;

	/** The offset at which the END of the content is in view: the whole scrollable extent. */
	UFUNCTION(BlueprintPure, Category = "Scroll Box")
	float GetScrollOffsetOfEnd() const;

	/** Fraction of the content currently visible, 0..1. One when everything fits. */
	UFUNCTION(BlueprintPure, Category = "Scroll Box")
	float GetViewFraction() const;

	/** How far through the scrollable range the window sits, 0..1. Zero when nothing can scroll. */
	UFUNCTION(BlueprintPure, Category = "Scroll Box")
	float GetViewOffsetFraction() const;

	/** Signed distance past an end, in local units; zero in range and zero while overscroll is off. */
	UFUNCTION(BlueprintPure, Category = "Scroll Box")
	float GetOverscrollOffset() const;

	/** The same distance as a percentage of the window -- UMG's GetOverscrollPercentage. */
	UFUNCTION(BlueprintPure, Category = "Scroll Box")
	float GetOverscrollPercentage() const;

	/**
	 * The bar's thickness, in local units. A READER of the style rather than a second copy: the
	 * number lives in FDreamScrollBarStyle::Thickness, because it is what the bar is drawn from, and
	 * a control-level field beside it would be the same measurement written twice.
	 *
	 * UMG's is an FVector2D; a bar here has one thickness, across its own axis, because its length is
	 * whatever it is scrolling.
	 */
	UFUNCTION(BlueprintPure, Category = "Scroll Box")
	float GetScrollbarThickness() const;

	/** Writes FDreamScrollBarStyle::Thickness and re-cuts the gutter, which is what it decides. */
	UFUNCTION(BlueprintCallable, Category = "Scroll Box")
	void SetScrollbarThickness(float InThickness);

	/** The margin around the bar. The same arrangement as the thickness: the style holds it. */
	UFUNCTION(BlueprintPure, Category = "Scroll Box")
	FMargin GetScrollbarPadding() const;

	UFUNCTION(BlueprintCallable, Category = "Scroll Box")
	void SetScrollbarPadding(FMargin InPadding);

	/**
	 * Whether the bar stays put with nothing to scroll -- UMG's AlwaysShowScrollbar.
	 *
	 * A reader of ScrollBarVisibility rather than a flag beside it: Permanent IS "always show", and
	 * two properties that both answered would be two answers to keep in step.
	 */
	UFUNCTION(BlueprintPure, Category = "Scroll Box")
	bool IsAlwaysShowScrollbar() const { return ScrollBarVisibility == EDreamScrollBoxScrollbarVisibility::Permanent; }

	UFUNCTION(BlueprintCallable, Category = "Scroll Box")
	void SetAlwaysShowScrollbar(bool bInAlwaysShow);

	UFUNCTION(BlueprintPure, Category = "Scroll Box")
	bool IsAlwaysShowScrollbarTrack() const { return bAlwaysShowScrollbarTrack; }

	UFUNCTION(BlueprintCallable, Category = "Scroll Box")
	void SetAlwaysShowScrollbarTrack(bool bInAlwaysShow);

	UFUNCTION(BlueprintPure, Category = "Scroll Box")
	float GetWheelScrollAnimationDuration() const { return WheelScrollAnimationDuration; }

	UFUNCTION(BlueprintCallable, Category = "Scroll Box")
	void SetWheelScrollAnimationDuration(float InDuration);

	UFUNCTION(BlueprintPure, Category = "Scroll Box")
	float GetWheelScrollMultiplier() const { return WheelScrollMultiplier; }

	UFUNCTION(BlueprintCallable, Category = "Scroll Box")
	void SetWheelScrollMultiplier(float InMultiplier);

	UFUNCTION(BlueprintPure, Category = "Scroll Box")
	EDreamScrollBoxConsumeMouseWheel GetConsumeMouseWheel() const { return ConsumeMouseWheel; }

	UFUNCTION(BlueprintCallable, Category = "Scroll Box")
	void SetConsumeMouseWheel(EDreamScrollBoxConsumeMouseWheel InConsume);

	UFUNCTION(BlueprintPure, Category = "Scroll Box")
	bool GetAllowOverscroll() const { return bAllowOverscroll; }

	UFUNCTION(BlueprintCallable, Category = "Scroll Box")
	void SetAllowOverscroll(bool bInAllow);

	UFUNCTION(BlueprintPure, Category = "Scroll Box")
	bool GetBackPadScrolling() const { return bBackPadScrolling; }

	/** Changes how far there is to go, so the range is re-stated and the bar re-measured. */
	UFUNCTION(BlueprintCallable, Category = "Scroll Box")
	void SetBackPadScrolling(bool bInPad);

	UFUNCTION(BlueprintPure, Category = "Scroll Box")
	bool GetFrontPadScrolling() const { return bFrontPadScrolling; }

	UFUNCTION(BlueprintCallable, Category = "Scroll Box")
	void SetFrontPadScrolling(bool bInPad);

	UFUNCTION(BlueprintPure, Category = "Scroll Box")
	bool GetEnableTouchScrolling() const { return bEnableTouchScrolling; }

	UFUNCTION(BlueprintCallable, Category = "Scroll Box")
	void SetEnableTouchScrolling(bool bInEnable);

	UFUNCTION(BlueprintPure, Category = "Scroll Box")
	bool GetAllowRightClickDragScrolling() const { return bAllowRightClickDragScrolling; }

	UFUNCTION(BlueprintCallable, Category = "Scroll Box")
	void SetAllowRightClickDragScrolling(bool bInAllow);

	UFUNCTION(BlueprintPure, Category = "Scroll Box")
	bool GetConsumePointerInput() const { return bConsumePointerInput; }

	UFUNCTION(BlueprintCallable, Category = "Scroll Box")
	void SetConsumePointerInput(bool bInConsume);

	UFUNCTION(BlueprintPure, Category = "Scroll Box")
	FKey GetAnalogMouseWheelKey() const { return AnalogMouseWheelKey; }

	UFUNCTION(BlueprintCallable, Category = "Scroll Box")
	void SetAnalogMouseWheelKey(FKey InKey);

	UFUNCTION(BlueprintPure, Category = "Scroll Box")
	EDreamUIScrollWhenFocusChanges GetScrollWhenFocusChanges() const { return ScrollWhenFocusChanges; }

	UFUNCTION(BlueprintCallable, Category = "Scroll Box")
	void SetScrollWhenFocusChanges(EDreamUIScrollWhenFocusChanges InRule);

	/** How far the window has travelled from the content's start edge, in local units. */
	UFUNCTION(BlueprintPure, Category = "Scroll Box")
	float GetScrollOffset() const;

	/** Clamped to how far there is to go. The absolute counterpart of SetScrollProgress. */
	UFUNCTION(BlueprintCallable, Category = "Scroll Box")
	void SetScrollOffset(float InOffset);

	/** Where things go. Parent into this, or use AddContent, and the stack piles them up in order. */
	UFUNCTION(BlueprintCallable, Category = "Scroll Box")
	UDreamWidget* GetContentNode() const;

	/** The behaviour, for anything this control does not wrap -- ScrollTo, inertia, a second bar. */
	UFUNCTION(BlueprintCallable, Category = "Scroll Box")
	UUIScrollView* GetScrollView() const;

	/** Put a widget in the content stack and re-measure. */
	UFUNCTION(BlueprintCallable, Category = "Scroll Box")
	bool AddContent(UDreamWidget* InWidget);

	/**
	 * Re-take the content's extent from the stack and tell the behaviour its range moved -- and, when
	 * that changed whether anything overflows, bring an auto-hiding bar out or put it away.
	 *
	 * The counterpart of UUIScrollView::RectRangeChanged, and it exists for the same reason: nothing
	 * re-measures a scrolled column on its own, because the column's size is not layout output -- it
	 * is the control's statement of how far there is to scroll.
	 */
	UFUNCTION(BlueprintCallable, Category = "Scroll Box")
	void RefreshContentExtent();

	virtual void ApplyStyle() override;

	/** Re-measure the content and re-state the scroll range after this control is resized. */
	void HandleDimensionsChanged(bool bPivotChanged, bool bWidthChanged, bool bHeightChanged);

protected:
	virtual void CollectParts(TArray<FDreamControlPart>& OutParts) override;
	virtual void RealizeBuiltIn() override;
	virtual void WireParts() override;
	/**
	 * Says out loud what the ScrollSensitivity semantic change left silent -- see
	 * UUIScrollView::PostLoad, which says it for the behaviour the same way and for the same reason.
	 * This knob is the one an author actually edits, and it is pushed into the behaviour AFTER load,
	 * so the behaviour's own check never sees the number a scroll box carries.
	 */
	virtual void PostLoad() override;

private:
	void HandleScrollViewChanged(FVector2D InProgress);
	void PushScrollProgress();

	/** Re-state every behaviour knob on the view. One list, called from the style push and the setters. */
	void PushScrollBehaviourSettings();

	/**
	 * Make the box a focus target, or stop it being one, to match the base's bIsFocusable.
	 *
	 * A UUISelectable on the FACE, added on demand and slept rather than destroyed: the selectable is
	 * what the event system hands focus to, and it is also what already distinguishes a resting
	 * pointer from focus -- so subscribing to its state is the whole of received and lost.
	 */
	void RefreshFocusTarget();
	void HandleFaceSelectionStateChanged(EUISelectableSelectionState InState, bool bInImmediate);
	void HandleContentFocusMoved(UDreamWidget* InFocusedWidget);

	/** The face's focus behaviour, made the first time bIsFocusable is on. Null until then. */
	UPROPERTY(Transient)
	TObjectPtr<UUISelectable> FocusSelectable = nullptr;

	/** Whether the face was focused last time its state moved, so an edge can be told from a repaint. */
	bool bWasFocused = false;

	/** Kept so the content-focus subscription is made exactly once, however often ApplyStyle runs. */
	FDelegateHandle ContentFocusHandle;

public:
	/**
	 * Drive the focus edge directly, for a test.
	 *
	 * The selectable's state is moved by the event system, which needs a viewport, a player and a
	 * press -- none of which exist headless. What is worth pinning is this control's own rule (an
	 * edge, not a state, and only while focusable), and that rule lives here rather than in the
	 * event system.
	 */
	void HandleFaceSelectionStateChangedForTest(EUISelectableSelectionState InState)
	{
		HandleFaceSelectionStateChanged(InState, false);
	}

private:

	/** True while the bar has something to say: shown at all, and either permanent or overflowing. */
	bool ShouldShowScrollBar() const;

	bool IsHorizontal() const { return Orientation == EDreamPanelOrientation::Horizontal; }

	/**
	 * Set while this control is the one moving the content, so the change that comes back out of the
	 * behaviour is not reported as the player's.
	 *
	 * The behaviour broadcasts one event for both roads, and by the time it arrives there is nothing
	 * left in the value to say which it was -- so the only place the distinction exists is here, at
	 * the moment the push is made.
	 */
	bool bPushingScroll = false;

	/** Whether the bar was showing last time the style push decided, so a change can be announced. */
	bool bScrollBarWasVisible = false;

	/** Guard for the flag above: RAII, because the push paths have early returns in them. */
	struct FScopedProgrammaticScroll
	{
		explicit FScopedProgrammaticScroll(UDreamScrollBox& InBox)
			: Box(InBox), bPrevious(InBox.bPushingScroll)
		{
			Box.bPushingScroll = true;
		}
		~FScopedProgrammaticScroll() { Box.bPushingScroll = bPrevious; }
		FScopedProgrammaticScroll(const FScopedProgrammaticScroll&) = delete;
		FScopedProgrammaticScroll& operator=(const FScopedProgrammaticScroll&) = delete;
	private:
		UDreamScrollBox& Box;
		bool bPrevious;
	};

	/**
	 * Set for the length of a style push. RefreshContentExtent runs inside the push twice, and a style
	 * push is what RefreshContentExtent calls when the bar's answer has moved -- so while one push is
	 * under way the re-measure must not start another.
	 */
	bool bApplyingStyle = false;
};
