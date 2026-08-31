// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Controls/DreamUIControl.h"
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
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scroll Bar")
	FDreamScrollBarStyle Style;

	/** Which way it runs, and which end is zero. One property instead of two Blueprint assets. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scroll Bar")
	EUIScrollbarDirectionType Direction = EUIScrollbarDirectionType::TopToBottom;

	/** Authored position in; mirror of the behaviour's out. A property so .dui and bindings can see it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scroll Bar", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Value = 0.0f;

	/**
	 * How much of the track the handle covers, 0 to 1 -- the visible fraction of whatever is being
	 * scrolled. A bar attached to a scroll view has this rewritten from the view on every push; a
	 * quarter is what a bare bar shows so it reads as a bar and not as a filled rail.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scroll Bar", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float HandleSize = 0.25f;

	/**
	 * Floor on the handle's drawn length, in local units. Pushed into the behaviour, which applies it
	 * to the drawn length and to the drag scale together -- a handle drawn longer than the fraction
	 * it drags with would run at the wrong rate for the whole of a long list.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scroll Bar", meta = (ClampMin = "0.0"))
	float MinHandleLength = 24.0f;

	/** How far one navigation press moves the value. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scroll Bar", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float NavigationChangeInterval = 0.1f;

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
	virtual void NativeOnInitialized() override;

private:
	void HandleValueChanged(float InValue);
	void HandleScrollViewProgress(FVector2D InProgress);

	/** The one writer of Value/HandleSize and the behaviour's copy of them. */
	void PushValueAndSize(float InValue, float InFraction, bool bInBroadcast);

	UPROPERTY(Transient)
	TWeakObjectPtr<UUIScrollView> ScrollView;

	FDelegateHandle ScrollViewDelegateHandle;
};
