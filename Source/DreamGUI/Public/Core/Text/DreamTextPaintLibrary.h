// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DreamTweener.h"
#include "DreamTextPaintLibrary.generated.h"

class UDreamText;

/** One of a text's three paints (FDreamTextStyle::FacePaint, OutlinePaint, OverlayPaint), as a tween names it. */
UENUM(BlueprintType)
enum class EDreamTextPaintLayer : uint8
{
	Face,
	Outline,
	Overlay,
};

/**
 * Tweens that move a text's paints (FDreamTextPaint) without repainting it: each drives one of UDreamText's paint
 * animation setters, which write one pixel of the text's paint table a step -- no repaint, no layout. They pause with
 * the game and follow its time dilation as the text's other tweens do (UDreamVisual::ColorTo), and end with the text.
 */
UCLASS()
class DREAMGUI_API UDreamTextPaintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Move one paint's phase (UDreamText::SetFacePaintPhase and its siblings) from where it is to To: its gradient slides
	 * along its line, 1 being one whole gradient. Null when there is no text, or no tween manager to run it.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Paint", meta = (AdvancedDisplay = "Delay,Ease"))
	static UDreamTweener* PaintPhaseTo(UDreamText* Text, EDreamTextPaintLayer Layer, float To, float Duration = 0.5f, float Delay = 0.0f, EDreamTweenEase Ease = EDreamTweenEase::OutCubic);

	/** Turn every paint of the text (UDreamText::SetPaintAngleOffset) from its angle offset now to To, in degrees. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Paint", meta = (AdvancedDisplay = "Delay,Ease"))
	static UDreamTweener* PaintAngleTo(UDreamText* Text, float To, float Duration = 0.5f, float Delay = 0.0f, EDreamTweenEase Ease = EDreamTweenEase::OutCubic);

	/**
	 * Sweep the overlay paint across the text: its phase from -1 to 1 at an even speed, Duration seconds a pass, starting
	 * again at -1 after each, Loops passes (-1: until stopped, or until the text goes). The look is the overlay's: a band
	 * that is transparent at both ends -- linear-gradient(90deg, transparent 40%, white 50%, transparent 60%) -- with
	 * OverlayBlend Add, is a shimmer. The phase stays at -1 during the delay, which keeps such a band off the text.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Paint", meta = (AdvancedDisplay = "Delay"))
	static UDreamTweener* PlayShimmer(UDreamText* Text, float Duration = 1.2f, float Delay = 0.0f, int32 Loops = -1);
};
