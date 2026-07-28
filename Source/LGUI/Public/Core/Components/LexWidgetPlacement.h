// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "Core/Components/LexPanelSlot.h"
#include "Layout/Margin.h"

#include "LexWidgetPlacement.generated.h"

class ULexWidget;

/**
 * Everything about where a widget sits that is destroyed by reparenting it, captured so it can be
 * put back.
 *
 * This exists because of one detail of how parenting works here: detaching a widget DESTROYS its
 * panel slot, and re-attaching creates a fresh default one. So a widget lifted out to a drag layer
 * and dropped back does not merely need its parent and sibling index restored -- it needs every
 * authored slot property restored too, or padding, alignment, fill weight, grid placement and
 * auto-size all silently reset to defaults. That is the failure that looks like "my layout broke
 * itself when I dragged something", and it is not obvious from the API that it can happen.
 *
 * Anything that temporarily moves a widget needs this: drag and drop, a reorder preview, a
 * full-screen zoom of a card, an item flying to an inventory slot. It is deliberately not part of
 * the drag system, because the need is "move this somewhere and put it back" and not "drag".
 */
USTRUCT(BlueprintType)
struct LGUI_API FLexWidgetPlacement
{
	GENERATED_BODY()

public:
	/** Record where the widget currently sits. Safe to call on a widget with no parent or no slot. */
	void Capture(const ULexWidget* InWidget);

	/**
	 * Put the widget back where Capture found it, including its slot properties.
	 *
	 * Returns false if it could not be reparented -- the original parent may have been destroyed
	 * while the widget was away, which is a real case for a card whose owner left the table. The
	 * caller decides what that means; this will not invent a placement.
	 */
	bool Restore(ULexWidget* InWidget) const;

	/** Whether Capture has been called and the record still names a live parent. */
	bool IsValid()const;

	void Reset();

private:
	UPROPERTY()
	TWeakObjectPtr<ULexWidget> Parent = nullptr;
	UPROPERTY()
	int32 SiblingIndex = INDEX_NONE;

	UPROPERTY()
	FVector2D Pivot = FVector2D(0.5, 0.5);
	UPROPERTY()
	FVector2D AnchorMin = FVector2D(0.5, 0.5);
	UPROPERTY()
	FVector2D AnchorMax = FVector2D(0.5, 0.5);
	UPROPERTY()
	FVector2D AnchoredPosition = FVector2D::ZeroVector;
	UPROPERTY()
	FVector2D SizeDelta = FVector2D::ZeroVector;
	UPROPERTY()
	bool bIgnoreLayout = false;

	/** False when the widget had no panel slot, so Restore does not fabricate one. */
	UPROPERTY()
	bool bHadSlot = false;
	UPROPERTY()
	FMargin SlotPadding = FMargin(0.0f);
	UPROPERTY()
	ELexPanelHorizontalAlignment SlotHorizontalAlignment = ELexPanelHorizontalAlignment::Fill;
	UPROPERTY()
	ELexPanelVerticalAlignment SlotVerticalAlignment = ELexPanelVerticalAlignment::Fill;
	UPROPERTY()
	ELexPanelSizeRule SlotSizeRule = ELexPanelSizeRule::Auto;
	UPROPERTY()
	float SlotFillWeight = 1.0f;
	UPROPERTY()
	int32 SlotRow = 0;
	UPROPERTY()
	int32 SlotColumn = 0;
	UPROPERTY()
	int32 SlotRowSpan = 1;
	UPROPERTY()
	int32 SlotColumnSpan = 1;
	UPROPERTY()
	bool bSlotAutoSize = false;
	UPROPERTY()
	int32 SlotZOrder = 0;
};
