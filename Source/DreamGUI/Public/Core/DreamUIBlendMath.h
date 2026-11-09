// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamUIRender/DreamUIBlendMode.h"

/**
 * DreamUIBlend.ush on the CPU: every function there has its twin here, step for step, and a test holds each to the other
 * and both to the W3C Compositing and Blending definitions. Colours are straight (not premultiplied) unless a name says
 * otherwise; Cb is what is behind, Cs the element.
 */
struct DREAMGUI_API FDreamUIBlendMath
{
	/** The dissolve pattern's hash seed, DREAMUI_BLEND_DISSOLVE_SEED. */
	static constexpr uint32 DissolveSeed = 0x2545F491u;

	static float ToSrgbChannel(float X);
	static float ToLinearChannel(float X);
	static FVector3f ToSrgb(const FVector3f& C);
	static FVector3f ToLinear(const FVector3f& C);

	static float Lum(const FVector3f& C);
	static FVector3f ClipColor(FVector3f C);
	static FVector3f SetLum(const FVector3f& C, float L);
	static float Sat(const FVector3f& C);
	static FVector3f SetSat(const FVector3f& C, float S);

	/** One channel of a separable mode; a mode that is not separable answers S. */
	static float Separable(EDreamUIBlendMode Mode, float B, float S);
	/** What Mode makes of Cb and Cs, in the blend's (encoded) space. */
	static FVector3f Function(EDreamUIBlendMode Mode, const FVector3f& Cb, const FVector3f& Cs);

	/**
	 * DreamUIBlend_OverBackdrop: the element over BackdropPremultiplied (rgb premultiplied, alpha) for the One /
	 * InverseSourceAlpha state, as the shader gives it to the hardware: (Sa * Cs', Sa).
	 */
	static FVector4f OverBackdrop(EDreamUIBlendMode Mode, const FVector3f& Cs, float Sa, const FVector4f& BackdropPremultiplied, bool bLinearTarget, bool bOpaqueBackdrop);
	/** DreamUIBlend_FixedOutput: what the shader gives the blend hardware for a mode it does. */
	static FVector4f FixedOutput(EDreamUIBlendMode Mode, const FVector3f& Premultiplied, float Sa);
	/**
	 * The blend hardware of a mode it does (FDreamUIRenderer::GetBuiltInBlendState), on the shader's output Source and what
	 * the target holds, Destination (premultiplied): what the target holds after. A UNORM target clamps each channel.
	 */
	static FVector4f ApplyFixedBlendState(EDreamUIBlendMode Mode, const FVector4f& Source, const FVector4f& Destination);
	/** DreamUIBlend_DissolveAlpha. */
	static float DissolveAlpha(float Alpha, int32 CellX, int32 CellY);

	/**
	 * The W3C composite itself, written out as the specification has it, for the tests to hold the rest to: the element
	 * (Cs, Sa) over what is behind (Cb, Ab), both straight; premultiplied colour and alpha out. In the blend's space.
	 */
	static FVector4f ReferenceComposite(EDreamUIBlendMode Mode, const FVector3f& Cb, float Ab, const FVector3f& Cs, float Sa);
};
