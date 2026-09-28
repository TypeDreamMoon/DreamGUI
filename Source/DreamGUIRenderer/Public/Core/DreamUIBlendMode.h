// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamUIBlendMode.generated.h"

/**
 * How an element's colour is combined with what is already in the render target.
 *
 * Applies to elements drawn by DreamGUI's built-in shader -- that is, elements with no material of
 * their own. An element that HAS a material takes its blend mode from the material, as it always
 * has; this enum still takes part in the batching decision for those, because two elements that
 * composite differently can never share a draw-call whatever the reason.
 */
UENUM(BlueprintType, Category = DreamGUI)
enum class EDreamUIBlendMode : uint8
{
	/** Premultiplied alpha composite. The default, and what every element did before this existed. */
	Alpha		UMETA(DisplayName = "Alpha"),
	/** Added to the target, so the element can only brighten it: glows, sparks, highlights. */
	Additive	UMETA(DisplayName = "Additive"),
	/** Multiplies the target, so the element can only darken it: shadows, tints, grading overlays. */
	Multiply	UMETA(DisplayName = "Multiply"),
};
