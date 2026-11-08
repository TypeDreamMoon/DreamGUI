// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Math/Float16Color.h"
#include "RenderGraphFwd.h"
#include "RHIResources.h"
#include "DreamUIRender/DreamUIBlendMode.h"
#include "DreamUIRender/IDreamUIRendererPrimitive.h"

class FGlobalShaderMap;
class UTexture;

/*
 * The renderer's side of the effects that work on what is behind them, pixel for pixel: an adjustment layer, a stylize
 * effect, a progressive blur and glass. Each copies the part of the target its element covers (and the room its effect
 * reads beyond it), works on the copy, and draws its element back through the element's clips: what is behind, made over
 * where the element covers it.
 *
 * An adjustment's and a stylize effect's work is an image effect (FDreamUIImageEffect), which a group can also run on its
 * own picture before it is put down (FDreamUICompositeDesc::Effect): the same operations on a widget's own subtree.
 */

/** What an image effect is told about the texture it works on. */
struct FDreamUIImageEffectContext
{
	/** The texture holds linear light (a render target's), not encoded values (a viewport's): colours are encoded to be worked on. */
	bool bLinear = false;
	/** Its alpha says nothing -- a viewport's picture: every pixel counts as opaque. */
	bool bOpaque = false;
	/** The world's time, in seconds. */
	float TimeSeconds = 0.0f;
	/** The pixel of the target the texture's first texel is: what a pattern laid on the screen is anchored to. */
	FIntPoint Origin = FIntPoint::ZeroValue;
	/** The element's rect within the texture, in its texels: xy the first, zw the size. What an effect laid on the element measures. */
	FVector4f Frame = FVector4f(0.0f, 0.0f, 1.0f, 1.0f);
	/** Pixels of the target to a unit of the canvas: what the effect's sizes, given in units, are drawn at. */
	float PixelScale = 1.0f;
};

/**
 * Work on a picture, into a picture of the same size: an adjustment's operations, a stylize effect's layers. Made on the game
 * thread and never changed after; a change is a new one. Render thread from the moment it is handed over, through a render
 * command that calls ResolveTextures_RenderThread first.
 */
class DREAMGUIRENDERER_API FDreamUIImageEffect
{
public:
	virtual ~FDreamUIImageEffect() = default;
	/** InSource worked on, premultiplied in and out, the same size. */
	virtual FRDGTextureRef Apply_RenderThread(FRDGBuilder& GraphBuilder, FGlobalShaderMap* ShaderMap, FRDGTextureRef InSource, const FDreamUIImageEffectContext& InContext) const = 0;
	/** How far from a pixel the effect reads, in units of the canvas: the room a copy of what is behind needs round the element. */
	virtual float GetReach() const { return 0.0f; }
	/** Inside the render command that hands it over: the textures it was made with, as the render thread sees them. */
	virtual void ResolveTextures_RenderThread() const {}
};
using FDreamUIImageEffectRef = TSharedPtr<const FDreamUIImageEffect, ESPMode::ThreadSafe>;

/** The layout of an adjustment's operations, as DreamUIAdjust.ush reads them. */
namespace DreamUIAdjustLayout
{
	inline constexpr int32 MaxOps = 8;
	inline constexpr int32 RowsPerOp = 4;
	/** Texels in a row of the table texture: a curve's or a gradient map's samples, at 0, 1/255, ..., 1. */
	inline constexpr int32 TableWidth = 256;
}

/**
 * An adjustment, as the shader takes it: up to MaxOps operations of four rows each (FDreamUIAdjustMath::Pack), the table
 * texture's rows, the lookup table texture, and how the result is mixed with what it was made from.
 */
struct FDreamUIAdjustPacked
{
	int32 NumOps = 0;
	FVector4f Rows[DreamUIAdjustLayout::MaxOps * DreamUIAdjustLayout::RowsPerOp];
	/** TableWidth by MaxOps, a row for each operation that reads one, at its index. */
	TArray<FFloat16Color> Table;
	/** The lookup table operation's texture (the engine's colour grading layout), read on the render thread only. */
	const UTexture* LookupTable = nullptr;
	float Opacity = 1.0f;
	EDreamUIBlendMode BlendMode = EDreamUIBlendMode::Alpha;

	FDreamUIAdjustPacked()
	{
		FMemory::Memzero(Rows);
	}
};

/** The layout of a stylize effect's layers, as DreamUIStylize.ush reads them: each a pass of its own. */
namespace DreamUIStylizeLayout
{
	inline constexpr int32 MaxLayers = 8;
	inline constexpr int32 RowsPerLayer = 3;
}

struct FDreamUIStylizePacked
{
	int32 NumLayers = 0;
	FVector4f Rows[DreamUIStylizeLayout::MaxLayers * DreamUIStylizeLayout::RowsPerLayer];
	/** The furthest any layer reads from a pixel, in units of the canvas. */
	float Reach = 0.0f;
	float Opacity = 1.0f;
	EDreamUIBlendMode BlendMode = EDreamUIBlendMode::Alpha;

	FDreamUIStylizePacked()
	{
		FMemory::Memzero(Rows);
	}
};

/** How strong a progressive blur is across its element: what multiplies its strength at each point (UDreamBackgroundBlur). */
struct FDreamUIBlurStrengthMap
{
	/** 0 none (uniform), 1 a texture's channel, 2 linear, 3 radial, 4 a clip mask's procedural pattern. */
	int32 Kind = 0;
	/** Linear: the two ends, in the element's 0..1 (v down); radial: the centre in xy and the two radii in zw. */
	FVector4f Points = FVector4f(0.0f, 0.5f, 1.0f, 0.5f);
	/** The strength at the first end (inner radius) and at the second (outer). */
	FVector2f Strengths = FVector2f(0.0f, 1.0f);
	/** Texture: the channel (0 red .. 3 alpha, 4 luminance). Procedural: the pattern (EDreamClipMaskPattern), its frequency and seed. */
	int32 Channel = 3;
	int32 Pattern = 0;
	float Frequency = 4.0f;
	float Seed = 0.0f;
	const UTexture* Texture = nullptr;
	/** The most levels of the pyramid it reaches into: the strongest blur. */
	int32 MaxLevel = 5;
};

/** What glass draws (UDreamGlass), as the composite shader takes it. */
struct FDreamUIGlassParams
{
	/**
	 * The shape: 0 a rounded rect of the element's size. A shape another visual gives (the smooth union of several, later)
	 * is the next kind; the shader measures it in one place, DreamUIGlass_Distance.
	 */
	int32 ShapeKind = 0;
	/** The corners' radii in units, in a rect block's order (UDreamRectBlock::CornerRadius, as its unit mode resolves it). */
	FVector4f CornerRadii = FVector4f(16.0f, 16.0f, 16.0f, 16.0f);
	/** Refraction: the band along the edge that bends, how far it bends at the edge (units), the profile (0 squircle, 1 circle, 2 linear). */
	float EdgeWidth = 24.0f;
	float Refraction = 18.0f;
	int32 Profile = 0;
	/** The red and blue channels bend this much less and more than green. */
	float Dispersion = 0.0f;
	/** Frost, as a blur's strength: 0 clear, 1 the pyramid's strongest level. */
	float Frost = 0.0f;
	/** A tint over what is seen through (encoded, alpha its strength), a saturation factor and a brightness offset. */
	FVector4f Tint = FVector4f(1.0f, 1.0f, 1.0f, 0.0f);
	float Saturation = 1.0f;
	float Brightness = 0.0f;
	/** The edge's highlight: its colour (encoded, alpha its strength), width in units, and where the light comes from (degrees, 0 up, clockwise). */
	FVector4f HighlightColor = FVector4f(1.0f, 1.0f, 1.0f, 0.6f);
	float HighlightWidth = 3.0f;
	float LightAngle = 315.0f;
	/** The inner shadow: its width in units and strength. */
	float ShadowWidth = 10.0f;
	float ShadowStrength = 0.15f;
	float Opacity = 1.0f;
};

namespace DreamUIPostProcessEffects
{
	/**
	 * An adjustment, made on the game thread: InPacked's operations and their tables. The lookup table texture is read by the
	 * render command that hands the effect over (FDreamUIImageEffect::ResolveTextures_RenderThread).
	 */
	DREAMGUIRENDERER_API FDreamUIImageEffectRef MakeAdjustEffect(FDreamUIAdjustPacked&& InPacked);
	DREAMGUIRENDERER_API FDreamUIImageEffectRef MakeStylizeEffect(const FDreamUIStylizePacked& InPacked);

	// ---- an image effect on what is behind: adjustment layer, stylize effect

	DREAMGUIRENDERER_API FDreamVisualPostProcessRenderProxyPtr CreateBackdropEffectProxy();
	/** The effect the proxy draws, or none, which draws nothing. */
	DREAMGUIRENDERER_API void SetBackdropEffect_GameThread(const FDreamVisualPostProcessRenderProxyPtr& InProxy, const FDreamUIImageEffectRef& InEffect);

	// ---- glass

	DREAMGUIRENDERER_API FDreamVisualPostProcessRenderProxyPtr CreateGlassProxy();
	DREAMGUIRENDERER_API void SetGlass_GameThread(const FDreamVisualPostProcessRenderProxyPtr& InProxy, const FDreamUIGlassParams& InParams);

	// ---- the background blur's strength map: none draws the uniform blur it always drew

	DREAMGUIRENDERER_API void SetBackgroundBlurStrengthMap_GameThread(const FDreamVisualPostProcessRenderProxyPtr& InProxy, const FDreamUIBlurStrengthMap& InMap);

	/** Levels of the blur pyramid, the original included: the strongest blur a progressive blur or frosted glass reaches. */
	inline constexpr int32 BlurPyramidLevels = 6;
	/** How far, in pixels, the pyramid's level InLevel reads from a pixel: the room a copy needs round what it blurs. */
	DREAMGUIRENDERER_API int32 GetBlurPyramidReach(int32 InLevel);
}
