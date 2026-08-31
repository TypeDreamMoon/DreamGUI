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
 * UUIScrollbar with the three things a code-built bar needs and the shipped component does not hand out.
 *
 * Handle and DirectionType are protected UPROPERTYs with getters and no setters -- the component was
 * written to be filled in by the details panel of a Blueprint prefab, and the only other writer it
 * admits is its editor customization, declared a friend. A control that builds its own hierarchy has
 * no details panel to be filled in from, so the two writes have to come from inside the class.
 *
 * The third is the reason this is a class rather than two one-line setters: ApplyValueToVisual places
 * the handle by writing RATIO anchors on it, and this library has paid four separate defects for that
 * shape (see UDreamScrollBar::ApplyHandleGeometry). It is private and non-virtual, so it cannot be
 * replaced -- but every path that reaches it either broadcasts afterwards or goes through a virtual
 * this class overrides, so the owning control can always get the last word. These overrides are that
 * last word for the three paths nobody else sees: enable, dimensions changed, and Start.
 */
UCLASS(ClassGroup = (DreamGUI), NotBlueprintable, HideDropdown, DisplayName = "Dream Scroll Bar Behaviour")
class DREAMGUI_API UDreamScrollBarBehaviour : public UUIScrollbar
{
	GENERATED_BODY()

public:
	/** The widget the value moves. Its PARENT becomes the area the value is measured against. */
	void SetHandleWidget(UDreamWidget* InHandle);

	/** Which end of the track means zero. The bar's whole horizontal/vertical split lives here. */
	void SetBarDirection(EUIScrollbarDirectionType InDirection);

	/**
	 * The base class just wrote the handle's anchors and nothing broadcast it.
	 *
	 * Fires from the three lifecycle paths that reach ApplyValueToVisual without passing through the
	 * value event -- so the owner re-asserts its own geometry after them instead of discovering a
	 * ratio-anchored handle on the frame the bar happens to be resized.
	 */
	FSimpleMulticastDelegate& GetOnHandleVisualDirtyEvent() { return OnHandleVisualDirtyCPP; }

	virtual void Start() override;

protected:
	virtual void OnEnable() override;
	virtual void OnDimensionsChanged(bool PivotChanged, bool WidthChanged, bool HeightChanged) override;

private:
	FSimpleMulticastDelegate OnHandleVisualDirtyCPP;
};

/**
 * A scroll bar whose hierarchy is code, not an asset.
 *
 * Two nodes: a track, and a handle inside it. BP_HorizontalScrollbar and BP_VerticalScrollbar are two
 * assets because an asset cannot branch on a property; this is one control because code can, and
 * Direction is the only thing they disagreed about. The handle's parent IS the track, which is what
 * the behaviour measures the value against -- a scroll bar handle rides the whole track (unlike a
 * slider's, which is inset by its own size), because its LENGTH already shrinks the travel.
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
	 * Floor on the handle's drawn length, in local units. Applied by raising the FRACTION rather than
	 * by clamping the drawn rect: the behaviour derives its drag scale from Size, so a handle drawn
	 * longer than Size claims would drag at the wrong rate for the whole of a long list.
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
	TObjectPtr<UDreamScrollBarBehaviour> BarBehaviour = nullptr;

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

	/**
	 * Place the handle from the value the behaviour holds. Public because the geometry has to be
	 * re-asserted after anything that writes the handle's anchors behind the control's back.
	 */
	UFUNCTION(BlueprintCallable, Category = "Scroll Bar")
	void ApplyHandleGeometry();

	virtual void ApplyStyle() override;

protected:
	virtual void NativeOnInitialized() override;

private:
	void HandleValueChanged(float InValue);
	void HandleScrollViewProgress(FVector2D InProgress);

	/** The one writer of Value/HandleSize and the behaviour's copy of them. */
	void PushValueAndSize(float InValue, float InFraction, bool bInBroadcast);

	/** HandleSize raised to whatever MinHandleLength asks for on the current track. */
	float ResolveEffectiveSize() const;

	UPROPERTY(Transient)
	TWeakObjectPtr<UUIScrollView> ScrollView;

	FDelegateHandle ScrollViewDelegateHandle;
};
