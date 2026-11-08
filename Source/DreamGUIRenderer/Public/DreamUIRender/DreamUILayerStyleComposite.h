// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RenderGraphFwd.h"
#include "RHIResources.h"
#include "DreamUIRender/DreamUIBlendMode.h"
#include "DreamUIRender/DreamUILayerStyleRecord.h"

class FGlobalShaderMap;

/** What a group's layer style is told about the group's texture. */
struct FDreamUILayerStyleContext
{
	/** The texture holds linear light (a render target's), not encoded values (a viewport's). */
	bool bLinear = false;
	/** Pixels of the target to a unit of the canvas: the style's sizes are in units. */
	float PixelScale = 1.0f;
	/**
	 * The layer's rect in the texture's texels: xy its centre, zw its half size. What a gradient or a pattern is laid across;
	 * the whole texture where the layer's rect is not known.
	 */
	FVector4f LayerRect = FVector4f(0.0f, 0.0f, 1.0f, 1.0f);
};

/** One of the layers a styled group is put down as, from the bottom up: each by its own mode, at the group's opacity. */
struct FDreamUILayerStyleLayer
{
	FRDGTextureRef Texture = nullptr;
	EDreamUIBlendMode BlendMode = EDreamUIBlendMode::Alpha;
	/** The group's own picture with every effect on it (the last layer): put down by the group's mode, not its own. */
	bool bContent = false;
};

/**
 * A group's layer style on the render thread: the group's picture measured (a jump flood of its alpha gives the distance to its
 * edge at every texel), then styled into the layers it is put down as -- its outer effects' slots, each by its own mode, and the
 * picture with everything on it. Made on the game thread from a packed style and never changed after; a change is a new one.
 * Render thread from the moment it is handed over, through a render command that calls ResolveTextures_RenderThread first.
 */
class DREAMGUIRENDERER_API FDreamUILayerStyleComposite
{
public:
	explicit FDreamUILayerStyleComposite(FDreamUILayerStyleRecord&& InRecord);

	const FDreamUILayerStyleRecord& GetRecord() const { return Record; }
	/** How far past the group's edge its effects reach, in units: the room the group's texture needs round what it draws. */
	float GetReach() const { return Record.OuterReach; }
	/** Inside the render command that hands it over: its patterns, as the render thread sees them. */
	void ResolveTextures_RenderThread() const;
	/**
	 * InGroup (premultiplied, the target's space) styled: OutLayers from the bottom up, every texture the size of InGroup and on
	 * its grid. The last is the group's picture with its effects (bContent).
	 */
	void Apply_RenderThread(FRDGBuilder& GraphBuilder, FGlobalShaderMap* ShaderMap, FRDGTextureRef InGroup, const FDreamUILayerStyleContext& InContext,
		TArray<FDreamUILayerStyleLayer, TInlineAllocator<4>>& OutLayers) const;

	/**
	 * The jump flood alone, for a test: for every texel of InGroup, the nearest texel inside its shape (alpha 0.5 and over) in xy
	 * and the nearest outside it in zw, in texels (-1 where there is none). Float32 RGBA.
	 */
	static FRDGTextureRef JumpFlood(FRDGBuilder& GraphBuilder, FGlobalShaderMap* ShaderMap, FRDGTextureRef InGroup);

	/**
	 * What a group's style is told, from where its layer is: InStyleRect the layer's rect on the canvas (xy its centre, zw its half
	 * size; a zero size is the whole texture), InCanvasToWorld and InWorldToClip how the canvas reaches the view, InViewRect where
	 * the view is on the bottom target, InTextureOrigin the bottom target's pixel at the group texture's first texel.
	 */
	static FDreamUILayerStyleContext MakeContext(const FVector4f& InStyleRect, const FMatrix& InCanvasToWorld, const FMatrix& InWorldToClip,
		const FIntRect& InViewRect, FIntPoint InTextureOrigin, bool bInLinear, FIntPoint InTextureSize = FIntPoint::ZeroValue);

private:
	FDreamUILayerStyleRecord Record;
	mutable FTextureRHIRef PatternRHI[DreamUILayerStyleLayout::MaxPatterns];
	/** The record as a texture, made the first time it is drawn. */
	mutable FTextureRHIRef RecordRHI;
};

using FDreamUILayerStyleRef = TSharedPtr<const FDreamUILayerStyleComposite, ESPMode::ThreadSafe>;
