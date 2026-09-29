// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "DreamUIRender/DreamUIPostProcessEffects.h"
#include "DreamUIRender/DreamUIPostProcessShaders.h"
#include "DreamUIRender/DreamUIRenderer.h"
#include "DreamUIRender/DreamUIRendererLogging.h"
#include "DreamUIRender/DreamVisualPostProcessRenderProxy.h"
#include "PipelineStateCache.h"
#include "RenderGraphUtils.h"
#include "RenderTargetPool.h"
#include "RHIStaticStates.h"

#define MAX_PixelateStrength 100.0f
#define INV_MAX_PixelateStrength 0.01f

DECLARE_CYCLE_STAT(TEXT("PostProcess_BackgroundPixelate"), STAT_BackgroundPixelate, STATGROUP_DreamGUI);
class FUIBackgroundPixelateRenderProxy :public FDreamVisualPostProcessRenderProxy
{
public:
	float PixelateStrength = 0.0f;
public:
	FUIBackgroundPixelateRenderProxy()
		:FDreamVisualPostProcessRenderProxy()
	{

	}
	virtual bool CanRender()const override
	{
		return PixelateStrength > 0.0f;
	}
	virtual void OnRenderPostProcess_RenderThread(
		FRDGBuilder& GraphBuilder,
		FRDGTextureRef SceneDepth,
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
	)override
	{
		SCOPE_CYCLE_COUNTER(STAT_BackgroundPixelate);
		if (PixelateStrength <= 0.0f)return;

		const FScreenRead Screen = ReadScreen_RenderThread(GraphBuilder, Renderer, GlobalShaderMap, ScreenTargetTexture);

		float calculatedStrength = FMath::Pow(PixelateStrength * INV_MAX_PixelateStrength, 2) * MAX_PixelateStrength;//this can make the pixelate effect transition feel more linear
		calculatedStrength = FMath::Clamp(calculatedStrength, 0.0f, 100.0f);
		calculatedStrength += 1;

		auto width = (int)(RectSize.X / calculatedStrength);
		auto height = (int)(RectSize.Y / calculatedStrength);
		width = FMath::Clamp(width, 1, (int)RectSize.X);
		height = FMath::Clamp(height, 1, (int)RectSize.Y);
		auto TextureSize = FIntPoint(width, height);
		bool bFullScreen = TextureSize == Screen.Size;

		//copy rect area from screen image to a texture of the pixelated size, which is the effect: the copy back is point sampled
		FRDGTextureRef PixelateTexture = CreateWorkTexture(GraphBuilder, Screen, TextureSize, TEXT("DreamUIPixelateEffectRenderTarget"));
		auto ModelViewProjectionMatrix = ObjectToWorldMatrix * ViewProjectionMatrix;
		GrabRegion_RenderThread(GraphBuilder, Renderer, GlobalShaderMap, Screen, PixelateTexture, bFullScreen, ModelViewProjectionMatrix, bIsRenderTarget, ViewTextureScaleOffset);
		WriteBack_RenderThread(GraphBuilder, Renderer, GlobalShaderMap, SceneDepth, Screen, PixelateTexture, bFullScreen, ModelViewProjectionMatrix
			, bIsWorldSpace, BlendDepthForWorld, DepthFadeForWorld, DepthTextureScaleOffset, ViewRect, TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI());
	}
};

FDreamVisualPostProcessRenderProxyPtr DreamUIPostProcessEffects::CreateBackgroundPixelateProxy()
{
	return MakeShared<FUIBackgroundPixelateRenderProxy, ESPMode::ThreadSafe>();
}

void DreamUIPostProcessEffects::SetBackgroundPixelateStrength_GameThread(const FDreamVisualPostProcessRenderProxyPtr& InProxy, float InPixelateStrength)
{
	if (!InProxy.IsValid())
	{
		return;
	}
	auto TempRenderProxy = StaticCastSharedPtr<FUIBackgroundPixelateRenderProxy>(InProxy);
	ENQUEUE_RENDER_COMMAND(FDreamBackgroundPixelate_UpdateData)
		([TempRenderProxy, InPixelateStrength](FRHICommandListImmediate& RHICmdList)
			{
				TempRenderProxy->PixelateStrength = InPixelateStrength;
			});
}
