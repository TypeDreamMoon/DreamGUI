// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RenderGraphFwd.h"
#include "DreamUIRender/DreamUIBackdropEffects.h"
#include "DreamUIRender/DreamVisualPostProcessRenderProxy.h"

/*
 * The stages an effect on what is behind it goes through, shared by the adjustment layer, the stylize effect, the progressive
 * blur and glass: the copy of what the element covers, the blur pyramid over it, and the element drawn back.
 */
namespace DreamUIBackdropPasses
{
	/** The copy of what is behind an element, and where it is. */
	struct FRegion
	{
		FRDGTextureRef Copy = nullptr;
		/** The current target's pixel the copy's first texel is. */
		FIntPoint Origin = FIntPoint::ZeroValue;
		/** The bottom target's pixel it is: what a pattern laid on the screen is anchored to. */
		FIntPoint TargetOrigin = FIntPoint::ZeroValue;
		/** The element's rect within the copy, in its texels. */
		FVector4f Frame = FVector4f(0.0f, 0.0f, 1.0f, 1.0f);
		/** Pixels of the target to a unit of the canvas, at the element. */
		float PixelScale = 1.0f;

		FDreamUIImageEffectContext MakeEffectContext(const FDreamUIPostProcessContext& InContext) const;
	};

	/**
	 * What the proxy's element covers of the current target, InReachUnits of the canvas and InReachPixels more all round, within
	 * the view: copied. False where nothing of it is in view, or there is no stack of targets to copy from.
	 */
	bool CopyRegion(const FDreamUIPostProcessContext& InContext, const FDreamVisualPostProcessRenderProxy& InProxy, float InReachUnits, int32 InReachPixels, FRegion& OutRegion);

	/**
	 * The blur pyramid over InSource: OutLevels[0] InSource itself, OutLevels[k] blurred k levels (the dual filter down k times
	 * and up again to half InSource's size), up to InMaxLevel; the levels past it repeat the last.
	 */
	void BuildPyramid(FRDGBuilder& GraphBuilder, FGlobalShaderMap* ShaderMap, FRDGTextureRef InSource, int32 InMaxLevel, FRDGTextureRef (&OutLevels)[DreamUIPostProcessEffects::BlurPyramidLevels]);

	/** How the element is drawn back: what DreamUIBackdropComposite.usf's permutation reads. */
	struct FComposite
	{
		/** DREAMUI_BACKDROP_PLAIN, _PROGRESSIVE_BLUR or _GLASS. */
		int32 Effect = 0;
		/** The effect's picture (plain), or the pyramid's levels. */
		FRDGTextureRef Levels[DreamUIPostProcessEffects::BlurPyramidLevels] = {};
		/** Progressive blur: the strength, the map and its texture as the render thread sees it. */
		float Strength = 1.0f;
		FDreamUIBlurStrengthMap Map;
		FTextureReferenceRHIRef MapTexture;
		FSamplerStateRHIRef MapSampler;
		FDreamUIGlassParams Glass;
	};

	/** The proxy's element drawn back over the current target, through its clips, as InComposite says. */
	void DrawComposite(const FDreamUIPostProcessContext& InContext, const FDreamVisualPostProcessRenderProxy& InProxy, const FRegion& InRegion, const FComposite& InComposite);
}
