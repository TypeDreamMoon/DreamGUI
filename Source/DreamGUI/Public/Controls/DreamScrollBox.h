// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Controls/DreamScrollBar.h"
#include "Controls/DreamUIControl.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Interaction/UIScrollView.h"
#include "DreamScrollBox.generated.h"

class UDreamWidget;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDreamScrollBoxScrolledEvent, float, Progress);

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
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scroll Box")
	FDreamScrollBoxStyle Style;

	/** Which way it scrolls, and which way its content stacks. One property instead of two Blueprint assets. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scroll Box")
	EDreamPanelOrientation Orientation = EDreamPanelOrientation::Vertical;

	/** Off means no bar at all, and the viewport keeps the gutter it would have cost. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scroll Box")
	bool bShowScrollBar = true;

	/** Whether the bar stays put or disappears while the content already fits. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scroll Box", meta = (EditCondition = "bShowScrollBar"))
	EDreamScrollBoxScrollbarVisibility ScrollBarVisibility = EDreamScrollBoxScrollbarVisibility::AutoHide;

	/** Multiplier on mouse-wheel input. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scroll Box", meta = (ClampMin = "0.0"))
	float ScrollSensitivity = 1.0f;

	/** How quickly a flick stops. Zero never slows down; larger stops sooner. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scroll Box", meta = (ClampMin = "0.0"))
	float DecelerateRate = 0.135f;

	/**
	 * Position along the scrolling axis, 0 to 1. Authored in; mirror of the behaviour's out, so a
	 * `.dui` binding and the designer can both see it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scroll Box", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ScrollProgress = 0.0f;

	/** Re-broadcast from the scroll behaviour, so a consumer binds to the control, not to a part of it. */
	UPROPERTY(BlueprintAssignable, Category = "Scroll Box")
	FDreamScrollBoxScrolledEvent OnScrolled;

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

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Scroll Box")
	TObjectPtr<UDreamScrollBar> ScrollBarNode = nullptr;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Scroll Box")
	TObjectPtr<UUIScrollView> ScrollView = nullptr;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Scroll Box")
	TObjectPtr<UDreamLayoutContainerStackBox> ContentStack = nullptr;

	UFUNCTION(BlueprintCallable, Category = "Scroll Box")
	float GetScrollProgress() const;

	UFUNCTION(BlueprintCallable, Category = "Scroll Box")
	void SetScrollProgress(float InProgress);

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
	 * Re-take the content's extent from the stack and tell the behaviour its range moved.
	 *
	 * The counterpart of UUIScrollView::RectRangeChanged, and it exists for the same reason: nothing
	 * re-measures a scrolled column on its own, because the column's size is not layout output -- it
	 * is the control's statement of how far there is to scroll.
	 */
	UFUNCTION(BlueprintCallable, Category = "Scroll Box")
	void RefreshContentExtent();

	virtual void ApplyStyle() override;

	/** Re-resolve the stretched viewport (and its content) after this control is resized. */
	void HandleDimensionsChanged(bool bPivotChanged, bool bWidthChanged, bool bHeightChanged);

	/** The same watch UDreamListViewBase keeps, for the same reason -- see there. */
	virtual void NativeOnTick(float InDeltaTime) override;

	/** The face size the inner stretched nodes were last made to agree with. */
	UPROPERTY(Transient)
	FVector2D LastSettledFaceSize = FVector2D(-1.0, -1.0);

protected:
	virtual void NativeOnInitialized() override;

private:
	void HandleScrollViewChanged(FVector2D InProgress);
	void PushScrollProgress();

	/** True while the bar has something to say: shown at all, and either permanent or overflowing. */
	bool ShouldShowScrollBar() const;

	bool IsHorizontal() const { return Orientation == EDreamPanelOrientation::Horizontal; }
};
