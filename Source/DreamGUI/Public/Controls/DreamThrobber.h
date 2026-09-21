// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Controls/DreamUIControl.h"
#include "DreamThrobber.generated.h"

class UDreamWidget;

/**
 * A throbber whose hierarchy is code, not an asset: N pieces that pulse, saying only "something is
 * happening".
 *
 * ONE CONTROL FOR UMG'S TWO, which is the call this library has already made three times: the
 * progress bar has Bar and Radial rather than two classes, the slider has a Direction rather than
 * two Blueprint presets, the scroll box has an Orientation. The pieces, the period, the colours and
 * the fade are identical between a Throbber and a CircularThrobber; the only thing that differs is
 * where the pieces are put, so that is one enum and not one class. The palette still offers both
 * rows -- they differ by a single property write, exactly like the two sliders.
 *
 * WHY IT TICKS, WHEN NOTHING ELSE IN THE LIBRARY DOES
 * ---------------------------------------------------
 * Every other animated thing here is a TWEEN: it has a start, an end and a duration, and the tween
 * manager owns the clock. A throbber has none of those -- it runs until somebody takes it off screen
 * -- so a tween would have to be re-started forever, and a looping tween per piece would be N
 * objects the manager walks every frame to do arithmetic this control can do in one pass. So it opts
 * into NativeOnTick (off by default for every widget, see UDreamUserWidget::SetWantsTick) and writes
 * the opacities itself.
 *
 * That also makes it correct where the tween manager does not exist: the manager is a world
 * subsystem and hands back null in a headless test and in the designer's preview, where a tween-based
 * throbber would simply stand still. This one animates wherever something calls its tick, and holds
 * its first frame where nothing does -- which is a throbber that LOOKS like a throbber in the
 * designer rather than an empty box.
 *
 * NO PLAY / STOP, for UMG's reason: a throbber is not a timeline. Take it off screen to stop it.
 */
UCLASS(BlueprintType, Blueprintable, DisplayName = "Dream Throbber")
class DREAMGUI_API UDreamThrobber : public UDreamUIControl
{
	GENERATED_BODY()

public:
	/**
	 * This instance's own look. The project sheet wins while StyleSource says so AND a sheet
	 * actually exists; with no sheet in the project this IS the look in effect.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetStyle", BlueprintSetter = "SetStyle", Category = "Throbber")
	FDreamThrobberStyle Style;

	UFUNCTION(BlueprintPure, Category = "Throbber")
	FDreamThrobberStyle GetStyle() const { return Style; }

	/**
	 * This instance's whole look, replaced and pushed -- which for a throbber means the piece POOL is
	 * rebuilt if the count changed. See UDreamButton::SetStyle for the caveat about the sheet.
	 */
	UFUNCTION(BlueprintCallable, Category = "Throbber")
	void SetStyle(const FDreamThrobberStyle& InStyle);

	/** A row of pieces, or pieces around a circle. UMG's Throbber and CircularThrobber. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetShape", BlueprintSetter = "SetShape", Category = "Throbber")
	EDreamThrobberShape Shape = EDreamThrobberShape::Linear;

	/** Off holds the pieces at the phase they were on. The widget stops ticking with it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetAnimate", BlueprintSetter = "SetAnimate", Category = "Throbber")
	bool bAnimate = true;

	/**
	 * Whether a piece also PULSES on that axis -- UMG's bAnimateHorizontally / bAnimateVertically,
	 * and Slate's rule for them: the piece is scaled on the ticked axis by the same wave that drives
	 * its opacity, so it swells as it brightens and shrinks as it fades.
	 *
	 * Both off, which is what this throbber has always drawn (opacity only). Both on is UMG's own
	 * default look; one on is the squash a loading strip wants.
	 *
	 * On the CONTROL rather than in the style because it decides what the animation DOES, the way
	 * bAnimate and Shape beside it do, rather than what a piece looks like.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetAnimateHorizontally", BlueprintSetter = "SetAnimateHorizontally", Category = "Throbber")
	bool bAnimateHorizontally = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetAnimateVertically", BlueprintSetter = "SetAnimateVertically", Category = "Throbber")
	bool bAnimateVertically = false;

	/**
	 * What the pieces hang under, and the control's one built-in node.
	 *
	 * A root rather than hanging the pieces off the control directly, for two reasons that are both
	 * about the rest of the library rather than about throbbers: a control with no built-in tree never
	 * gets a UDreamWidgetTree at all (DreamUI::Realize's owner overload is what makes one), so there
	 * would be nothing for the pieces to be built INTO; and a named root is what gives the template
	 * road something to supply, the way every other control's Face does.
	 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Throbber")
	TObjectPtr<UDreamWidget> PieceRootNode = nullptr;

	/** The pieces, in order. Public because a decorator may want to skin one. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Throbber")
	TArray<TObjectPtr<UDreamWidget>> PieceNodes;

	UFUNCTION(BlueprintPure, Category = "Throbber")
	EDreamThrobberShape GetShape() const { return Shape; }

	/** Re-places every piece: the two shapes put them in different places, nothing else differs. */
	UFUNCTION(BlueprintCallable, Category = "Throbber")
	void SetShape(EDreamThrobberShape InShape);

	UFUNCTION(BlueprintPure, Category = "Throbber")
	bool GetAnimate() const { return bAnimate; }

	UFUNCTION(BlueprintCallable, Category = "Throbber")
	void SetAnimate(bool bInAnimate);

	UFUNCTION(BlueprintPure, Category = "Throbber")
	bool GetAnimateHorizontally() const { return bAnimateHorizontally; }

	/**
	 * Turning it OFF re-places every piece at once, because a piece frozen mid-pulse would otherwise
	 * keep the width the last tick gave it -- a switch that leaves the thing it switched off in a
	 * random state is a switch that looks broken.
	 */
	UFUNCTION(BlueprintCallable, Category = "Throbber")
	void SetAnimateHorizontally(bool bInAnimateHorizontally);

	UFUNCTION(BlueprintPure, Category = "Throbber")
	bool GetAnimateVertically() const { return bAnimateVertically; }

	UFUNCTION(BlueprintCallable, Category = "Throbber")
	void SetAnimateVertically(bool bInAnimateVertically);

	/**
	 * The four numbers UMG's two throbbers offer one at a time.
	 *
	 * Each getter answers the style in EFFECT, and each setter edits THIS INSTANCE'S style and
	 * re-pushes -- so they show up when this instance's style is what is in effect, and under a
	 * project sheet they are writes to a value the sheet is overruling. Same bargain, and same
	 * wording, as UDreamBorder::SetPadding.
	 */
	UFUNCTION(BlueprintPure, Category = "Throbber")
	int32 GetNumberOfPieces() const;

	/** Rebuilds the piece pool, which is what a count IS here. */
	UFUNCTION(BlueprintCallable, Category = "Throbber")
	void SetNumberOfPieces(int32 InNumberOfPieces);

	UFUNCTION(BlueprintPure, Category = "Throbber")
	float GetPeriod() const;

	/** Seconds for one full cycle. Drives the linear shape too, where UMG offers it only on the ring. */
	UFUNCTION(BlueprintCallable, Category = "Throbber")
	void SetPeriod(float InPeriod);

	UFUNCTION(BlueprintPure, Category = "Throbber")
	float GetRadius() const;

	/** How far out the pieces ride. Circular only, as in UMG; the linear shape ignores it. */
	UFUNCTION(BlueprintCallable, Category = "Throbber")
	void SetRadius(float InRadius);

	/**
	 * Whether the pieces fade at all -- UMG's bAnimateOpacity, read off how faint they are allowed
	 * to get: a floor of 1 keeps every piece solid, which is the flag being off.
	 */
	UFUNCTION(BlueprintPure, Category = "Throbber")
	bool GetAnimateOpacity() const;

	/**
	 * Off pins the floor at 1. On restores the style's shipped floor, but only when the floor is
	 * currently 1 -- so switching off and on again is not a way to lose a hand-tuned value, and a
	 * throbber already fading keeps fading exactly as far as it did.
	 */
	UFUNCTION(BlueprintCallable, Category = "Throbber")
	void SetAnimateOpacity(bool bInAnimateOpacity);

	/** How far through one cycle the animation is, 0 to 1. Authored in; readable out. */
	UFUNCTION(BlueprintPure, Category = "Throbber")
	float GetPhase() const;

	virtual void ApplyStyle() override;

	/**
	 * Public, where the base declares it protected, for one reason: a headless test has no tick.
	 * Driving one frame by hand is how the piece opacities get asserted at a phase the test chose,
	 * and a protected override would leave the only interesting behaviour in this class untestable.
	 */
	virtual void NativeOnTick(float DeltaTime) override;

protected:
	virtual void CollectParts(TArray<FDreamControlPart>& OutParts) override;
	virtual void RealizeBuiltIn() override;
	virtual void WireParts() override;

private:
	/** Grow or shrink the piece pool to exactly this many widgets, keeping the ones that survive. */
	void ResizePieces(int32 InCount, const FDreamThrobberStyle& InStyle);

	/** Where piece N sits and how big it is, for the current shape. */
	void PlacePiece(int32 InIndex, int32 InCount, const FDreamThrobberStyle& InStyle);

	/** Write every piece's opacity for the current phase. The whole of what the tick does. */
	void ApplyPhase(const FDreamThrobberStyle& InStyle);

	/** Seconds into the current cycle. Wrapped rather than accumulated, so it cannot lose precision. */
	UPROPERTY(Transient)
	float ElapsedInCycle = 0.0f;
};
