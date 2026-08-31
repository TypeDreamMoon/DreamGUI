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
 * is one rectangle -- and a fill inside it that the control drives. UUIProgressBar exists and could
 * drive the same geometry, but a behaviour earns its place by owning interaction or per-frame work
 * (marquee), and a plain bar has neither; the control writing the fill itself is smaller than
 * handing a part to a component whose other features it hides.
 *
 * TWO SHAPES, ONE CONTROL. Bar spends the percent as the fill's WIDTH; Radial spends it as a swept
 * ANGLE and the two rects become rings. That is one enum rather than two classes for the reason the
 * slider has one Direction rather than two Blueprint presets: the parts, the property and the style
 * are identical and only the drawing differs. Radial rides the fill rect's OWN RadialFill wedge --
 * no second visual, no mask, no material variant -- and the ring itself is the rect's border with
 * its body switched off, because a border is the only hole this primitive has.
 *
 * A Throbber falls out of that and needs no class of its own: a Radial bar at a fixed Percent whose
 * RadialStartAngle (or the widget's rotation) an animation spins is exactly the spinner UMG ships,
 * and it is styled from the same sheet as every other bar in the project.
 *
 * UMG parity is UProgressBar's core: Percent in 0..1, SetPercent/GetPercent, no events -- progress
 * is written by code, so there is nobody to notify.
 *
 *     /Script/DreamGUI.DreamProgressBar LoadProgress {
 *         Percent = 0.35
 *     }
 *
 *     /Script/DreamGUI.DreamProgressBar Spinner {
 *         Shape = Radial
 *         Percent = 0.25
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

	/**
	 * Which silhouette the percent is spent on: a bar's width, or a ring's swept angle. A property,
	 * not a subclass -- see the class comment, and EDreamProgressShape.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Progress Bar")
	EDreamProgressShape Shape = EDreamProgressShape::Bar;

	UFUNCTION(BlueprintCallable, Category = "Progress Bar")
	float GetPercent() const;

	UFUNCTION(BlueprintCallable, Category = "Progress Bar")
	void SetPercent(float InPercent);

	UFUNCTION(BlueprintCallable, Category = "Progress Bar")
	EDreamProgressShape GetShape() const;

	/** Re-pushes the whole style, because the shape decides what every other knob means. */
	UFUNCTION(BlueprintCallable, Category = "Progress Bar")
	void SetShape(EDreamProgressShape InShape);

	virtual void ApplyStyle() override;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Progress Bar")
	TObjectPtr<UDreamWidget> TrackNode = nullptr;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Progress Bar")
	TObjectPtr<UDreamWidget> FillNode = nullptr;

protected:
	virtual void NativeOnInitialized() override;

private:
	/**
	 * Percent, made geometry: the fill's WIDTH in Bar, the fill rect's swept RadialFillAngle in
	 * Radial. The only writer of either, and in both shapes it feeds absolute numbers read from the
	 * track's live size (see the comment on the definition for the flicker that rule came from).
	 */
	void ApplyPercent();

	/**
	 * The silhouette both rects wear -- rounded rect, or ring -- pushed in full every time, in both
	 * directions. Symmetry is the point: a control whose Shape changed at runtime must not keep a
	 * ring's body-off, border-on state while drawing a bar.
	 */
	void ApplyShape(const FDreamProgressBarStyle& InActive);
};
