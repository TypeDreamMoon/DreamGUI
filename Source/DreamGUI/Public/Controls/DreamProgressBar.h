// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Controls/DreamUIControl.h"
#include "DreamProgressBar.generated.h"

class UDreamWidget;

/**
 * Which edge a Bar's fill grows from -- UMG's EProgressBarFillType, its four DIRECTIONAL cases.
 *
 * Four rather than one because the slider next door has had Direction since it was written and a
 * progress bar that could only run left-to-right was the family's one asymmetry: a vertical charge
 * meter and a right-to-left bar (which is what a mirrored-for-RTL layout needs) were not expressible
 * at all. UMG's two FillFromCenter cases are deliberately absent -- they are a different SHAPE of
 * fill rather than a direction, and this control's Radial already covers the "grows from a middle"
 * want with something a bar cannot draw.
 */
UENUM(BlueprintType)
enum class EDreamProgressFillType : uint8
{
	LeftToRight,
	RightToLeft,
	BottomToTop,
	TopToBottom,
};

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
	 *
	 * BlueprintSetter, not a bare writable field: nothing re-derives a control from a property that
	 * changed, so a runtime `Set Percent` straight onto the variable used to move the number and
	 * leave the fill where it was -- visible to nobody until something else happened to restyle.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetPercent", BlueprintSetter = "SetPercent", Category = "Progress Bar", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Percent = 0.0f;

	/**
	 * Which silhouette the percent is spent on: a bar's width, or a ring's swept angle. A property,
	 * not a subclass -- see the class comment, and EDreamProgressShape.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetShape", BlueprintSetter = "SetShape", Category = "Progress Bar")
	EDreamProgressShape Shape = EDreamProgressShape::Bar;

	/**
	 * Which edge the fill grows from. Bar only -- a ring's direction is its RadialStartAngle, which
	 * the style already states.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetFillType", BlueprintSetter = "SetFillType", Category = "Progress Bar", meta = (EditCondition = "Shape == EDreamProgressShape::Bar"))
	EDreamProgressFillType FillType = EDreamProgressFillType::LeftToRight;

	/**
	 * The INDETERMINATE bar -- UMG's bIsMarquee: a short fill sweeping the track forever, for work
	 * whose length nobody knows.
	 *
	 * Percent is ignored while this is on, which is the whole meaning of indeterminate; the fill's
	 * own length becomes MarqueeFraction of the track. It is the one thing on this control that costs
	 * a per-frame tick, so the tick is opted in and out with the flag rather than left running: a
	 * determinate bar pays nothing.
	 *
	 * Bar only. The class comment offers the ring's own answer for a spinner -- a Radial at a fixed
	 * Percent whose rotation an animation drives -- and that one needs no tick from this control.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetIsMarquee", BlueprintSetter = "SetIsMarquee", Category = "Progress Bar", meta = (EditCondition = "Shape == EDreamProgressShape::Bar"))
	bool bIsMarquee = false;

	/**
	 * How much of the track the sweeping fill covers, 0..1. A fraction rather than a pixel count for
	 * the reason every other length on this control is one: a bar is stretched by whoever placed it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Progress Bar", meta = (ClampMin = "0.01", ClampMax = "1.0", EditCondition = "bIsMarquee"))
	float MarqueeFraction = 0.25f;

	/** Seconds for one sweep, entrance to exit. Zero or less parks the sweep at the start edge. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Progress Bar", meta = (ClampMin = "0.0", EditCondition = "bIsMarquee"))
	float MarqueeDuration = 1.5f;

	UFUNCTION(BlueprintCallable, Category = "Progress Bar")
	float GetPercent() const;

	UFUNCTION(BlueprintCallable, Category = "Progress Bar")
	void SetPercent(float InPercent);

	UFUNCTION(BlueprintCallable, Category = "Progress Bar")
	EDreamProgressShape GetShape() const;

	/** Re-pushes the whole style, because the shape decides what every other knob means. */
	UFUNCTION(BlueprintCallable, Category = "Progress Bar")
	void SetShape(EDreamProgressShape InShape);

	UFUNCTION(BlueprintCallable, Category = "Progress Bar")
	EDreamProgressFillType GetFillType() const;

	/** Re-places the fill: the direction decides which edge it is anchored to and which axis it spends. */
	UFUNCTION(BlueprintCallable, Category = "Progress Bar")
	void SetFillType(EDreamProgressFillType InFillType);

	UFUNCTION(BlueprintCallable, Category = "Progress Bar")
	bool GetIsMarquee() const;

	/** Turning it on starts the sweep AND the per-frame tick it needs; turning it off stops both. */
	UFUNCTION(BlueprintCallable, Category = "Progress Bar")
	void SetIsMarquee(bool bInIsMarquee);

	virtual void ApplyStyle() override;

	/** Drives the marquee's sweep, and is opted into only while one is running. */
	virtual void NativeOnTick(float DeltaTime) override;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Progress Bar")
	TObjectPtr<UDreamWidget> TrackNode = nullptr;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Progress Bar")
	TObjectPtr<UDreamWidget> FillNode = nullptr;

protected:
	virtual void CollectParts(TArray<FDreamControlPart>& OutParts) override;
	virtual void RealizeBuiltIn() override;

	/**
	 * No behaviour to wire -- this control writes its own geometry -- but the track's size has to be
	 * subscribed to, because that geometry is absolute numbers read off it. See the definition.
	 */
	virtual void WireParts() override;

public:
	/** Re-spend the percent against the track's new rect. Bound to the track's dimension event. */
	void HandleTrackDimensionsChanged(bool bPivotChanged, bool bWidthChanged, bool bHeightChanged);

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

	/**
	 * How far the marquee has swept, in seconds, wrapped into one cycle. Transient because a sweep is
	 * a live gesture and not something a package should remember.
	 */
	UPROPERTY(Transient)
	float MarqueeTime = 0.0f;
};
