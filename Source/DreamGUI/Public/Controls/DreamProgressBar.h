// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Controls/DreamUIControl.h"
#include "DreamProgressBar.generated.h"

class UDreamWidget;

/**
 * A progress bar whose hierarchy is code, not an asset.
 *
 * Two nodes and no behaviour: a track that IS the root -- the button's face-is-root argument, a bar
 * is one rectangle -- and a fill anchored inside it whose horizontal anchors run (0,0)-(Percent,1).
 * UUIProgressBar exists and could drive the same anchors, but a behaviour earns its place by owning
 * interaction or per-frame work (marquee), and a plain bar has neither; the control writing one
 * anchor itself is smaller than handing a part to a component whose other features it hides.
 *
 * UMG parity is UProgressBar's core: Percent in 0..1, SetPercent/GetPercent, no events -- progress
 * is written by code, so there is nobody to notify.
 *
 *     /Script/DreamGUI.DreamProgressBar LoadProgress {
 *         Percent = 0.35
 *     }
 */
UCLASS(BlueprintType, Blueprintable, DisplayName = "Dream Progress Bar")
class DREAMGUI_API UDreamProgressBar : public UDreamUIControl
{
	GENERATED_BODY()

public:
	/**
	 * This instance's own look. The project sheet wins while StyleSource says so AND a sheet
	 * actually exists; with no sheet in the project this IS the look in effect -- which is why
	 * it stays editable instead of being gated on the enum: the old edit condition greyed the
	 * exact values that were driving the control.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Progress Bar")
	FDreamProgressBarStyle Style;

	/**
	 * How much of the track is filled, 0..1. A property so .dui and bindings can see it; the value
	 * is stored as authored and clamped only where it becomes geometry, which is UMG's arrangement.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Progress Bar", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Percent = 0.0f;

	UFUNCTION(BlueprintCallable, Category = "Progress Bar")
	float GetPercent() const;

	UFUNCTION(BlueprintCallable, Category = "Progress Bar")
	void SetPercent(float InPercent);

	virtual void ApplyStyle() override;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Progress Bar")
	TObjectPtr<UDreamWidget> TrackNode = nullptr;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Progress Bar")
	TObjectPtr<UDreamWidget> FillNode = nullptr;

protected:
	virtual void NativeOnInitialized() override;

private:
	/** Percent, made geometry: the fill's horizontal AnchorMax.X. The only writer of that anchor. */
	void ApplyPercent();
};
