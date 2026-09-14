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
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Throbber")
	FDreamThrobberStyle Style;

	/** A row of pieces, or pieces around a circle. UMG's Throbber and CircularThrobber. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetShape", BlueprintSetter = "SetShape", Category = "Throbber")
	EDreamThrobberShape Shape = EDreamThrobberShape::Linear;

	/** Off holds the pieces at the phase they were on. The widget stops ticking with it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetAnimate", BlueprintSetter = "SetAnimate", Category = "Throbber")
	bool bAnimate = true;

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
