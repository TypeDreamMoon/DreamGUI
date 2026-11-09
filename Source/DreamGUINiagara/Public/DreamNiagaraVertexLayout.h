// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * How a particle's vertex is laid out, the same as Shaders/Private/DreamUIParticle.ush reads it (a test holds the two to
 * each other):
 *   UV0    the sprite's frame, or a ribbon's first UV set
 *   UV1.x  the widget property record, marked ParticleMark; UV1.y the dynamic parameter's x
 *   UV2    the frame blended towards, or a ribbon's second UV set
 *   UV3.x  the colour's scale and the frame blend, packed (Pack): the colour is written divided by its scale, so that one
 *          brighter than 1 survives an 8-bit vertex colour
 *   UV3.y  the dynamic parameter's y; UV4 its z and w
 * A converted material reads the same channels (DreamGUINiagaraEditor's material conversion).
 */
namespace DreamNiagaraVertexLayout
{
	/** DREAMUI_PARTICLE_MARK: the widget record's mark, which the built-in shader shades a particle by. */
	inline constexpr uint8 ParticleMark = 5;
	/** DREAMUI_PARTICLE_SCALE_STEPS: the colour's scale is kept in steps of one over this. */
	inline constexpr float ScaleSteps = 16.0f;
	/** DREAMUI_PARTICLE_BLEND_RANGE: the share of the fraction the blend takes, so that a blend of 1 never carries. */
	inline constexpr float BlendRange = 0.99609375f;
	/** The brightest a colour is kept: past it, it is clamped. */
	inline constexpr float MaxScale = 255.0f;

	/** UV3.x, from a scale of at least 1 (a whole number of steps, see ScaleFor) and a blend of 0 to 1. */
	DREAMGUINIAGARA_API float Pack(float InScale, float InBlend);
	/** UV3.x taken apart, as DreamUIParticle_Unpack does it. */
	DREAMGUINIAGARA_API void Unpack(float InPacked, float& OutScale, float& OutBlend);
	/** The smallest scale, in whole steps, that brings every channel of InLinear to 1 or below. */
	DREAMGUINIAGARA_API float ScaleFor(const FLinearColor& InLinear);
	/** InLinear divided by InScale, as the 8-bit sRGB vertex colour every DreamGUI vertex carries. */
	DREAMGUINIAGARA_API FColor EncodeColor(const FLinearColor& InLinear, float InScale);
}
