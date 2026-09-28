// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamUIRender/IDreamUIRendererPrimitive.h"

/*
 * The renderer's side of the post-process effects the extension visuals offer: background blur, background
 * pixelate and pixel sort. A visual creates its proxy here and sends it what changed through the functions
 * below; each call enqueues one render command, and the proxies themselves -- the render-thread passes --
 * are the renderer's own.
 */
namespace DreamUIPostProcessEffects
{
	// ---- background blur

	DREAMGUIRENDERER_API FDreamVisualPostProcessRenderProxyPtr CreateBackgroundBlurProxy();
	/** InBlurStrength already carries the visual's alpha, when the visual applies it. */
	DREAMGUIRENDERER_API void SetBackgroundBlur_GameThread(const FDreamVisualPostProcessRenderProxyPtr& InProxy, float InBlurStrength, int32 InMaxDownSampleLevel);
	DREAMGUIRENDERER_API void SetBackgroundBlurStrength_GameThread(const FDreamVisualPostProcessRenderProxyPtr& InProxy, float InBlurStrength);

	// ---- background pixelate

	DREAMGUIRENDERER_API FDreamVisualPostProcessRenderProxyPtr CreateBackgroundPixelateProxy();
	/** InPixelateStrength already carries the visual's alpha, when the visual applies it. */
	DREAMGUIRENDERER_API void SetBackgroundPixelateStrength_GameThread(const FDreamVisualPostProcessRenderProxyPtr& InProxy, float InPixelateStrength);

	// ---- pixel sort. The enums mirror the visual's, value for value; the visual converts.

	enum class EPixelSortAxis : uint8
	{
		Horizontal,
		Vertical,
	};
	enum class EPixelSortInterval : uint8
	{
		Threshold,
		Random,
		Waves,
		None,
	};
	enum class EPixelSortKey : uint8
	{
		Luminance,
		Brightness,
		Saturation,
		Hue,
		Intensity,
		Minimum,
		Alpha,
	};

	/** Everything the pixel sort passes read, resolved on the game thread. */
	struct FPixelSortParams
	{
		/** How far a texel may look along its run, and therefore how far it may travel. */
		int32 SearchRadius = 0;
		/** Threshold band, ordered low-then-high. */
		FVector2f Band = FVector2f(0.25f, 0.8f);
		EPixelSortAxis SortAxis = EPixelSortAxis::Vertical;
		EPixelSortKey SortKey = EPixelSortKey::Luminance;
		EPixelSortInterval IntervalMode = EPixelSortInterval::Threshold;
		int32 IntervalLength = 32;
		float Randomness = 0.0f;
		bool bDescending = false;
	};

	DREAMGUIRENDERER_API FDreamVisualPostProcessRenderProxyPtr CreatePixelSortProxy();
	DREAMGUIRENDERER_API void SetPixelSort_GameThread(const FDreamVisualPostProcessRenderProxyPtr& InProxy, const FPixelSortParams& InParams);
	/** The size of the region the pixel sort works in: the whole screen when bInUseFullSize, otherwise the rectangle, never under one pixel. */
	DREAMGUIRENDERER_API FIntPoint ResolvePixelSortRegionSize(bool bInUseFullSize, const FVector2f& InRectSize, const FIntPoint& InScreenSize);
}
