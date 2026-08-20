// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "Core/Components/DreamPixelSort.h"
#include "Core/DreamVisualPostProcessRenderProxy.h"

/**
 * The render-thread half of UDreamPixelSort.
 *
 * Blur and pixelate declare their proxies inside their .cpp files. This one lives in a private
 * header instead, for one reason: it lets the tests include it and assert that a details-panel edit
 * actually lands on the proxy. That failure -- the panel shows the new value, the GPU keeps the old
 * one -- has already happened once in this plugin, is invisible to every setter-based test, and
 * cannot be caught at compile time. The header stays PRIVATE, so nothing about the module's
 * engine-internal include situation changes.
 */
class FDreamPixelSortRenderProxy : public FDreamVisualPostProcessRenderProxy
{
public:
	/** How far a texel may look along its run, and therefore how far it may travel. */
	int32 SearchRadius = 0;
	/** Threshold band, already ordered low-then-high by DreamPixelSort::ResolveBand. */
	FVector2f Band = FVector2f(0.25f, 0.8f);
	EDreamPixelSortAxis SortAxis = EDreamPixelSortAxis::Vertical;
	EDreamPixelSortKey SortKey = EDreamPixelSortKey::Luminance;
	EDreamPixelSortInterval IntervalMode = EDreamPixelSortInterval::Threshold;
	int32 IntervalLength = 32;
	float Randomness = 0.0f;
	bool bDescending = false;

public:
	FDreamPixelSortRenderProxy()
		:FDreamVisualPostProcessRenderProxy()
	{
	}

	virtual bool CanRender()const override
	{
		// An empty band selects nothing and no passes move nothing, so both are a guaranteed no-op.
		// Returning true for them would still cost a full region grab and composite every frame, for
		// a result identical to the untouched background -- and it would not show up as anything
		// suspicious in a profile, just twenty widgets that are each slightly too expensive.
		return SearchRadius > 0 && Band.X < Band.Y;
	}

	virtual void OnRenderPostProcess_RenderThread(
		FRDGBuilder& GraphBuilder,
		const FMinimalSceneTextures& SceneTextures,
		FDreamUIRenderer* Renderer,
		FTextureRHIRef ScreenTargetTexture,
		FGlobalShaderMap* GlobalShaderMap,
		const FMatrix44f& ViewProjectionMatrix,
		bool bIsWorldSpace,
		bool bIsRenderTarget,
		float BlendDepthForWorld,
		int DepthFadeForWorld,
		const FIntRect& ViewRect,
		const FVector4f& DepthTextureScaleOffset,
		const FVector4f& ViewTextureScaleOffset
	)override;
};
