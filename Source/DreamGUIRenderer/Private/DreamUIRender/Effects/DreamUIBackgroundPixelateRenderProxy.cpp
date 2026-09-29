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

		auto& RHICmdList = GraphBuilder.RHICmdList;

		/**
		 * Each pooled render target is handed to the graph with GraphBuilder.RegisterExternalTexture as
		 * soon as it is taken, as blur does: that parks a strong reference on it until the graph has
		 * executed. This function only RECORDS passes. Releasing the targets at its end, as it used to,
		 * put them back in GRenderTargetPool before any pass had run, so a later effect in the same
		 * frame -- another pixelate, a blur with the same descriptor, the other eye -- could be handed
		 * the same memory, with RDG seeing no dependency between the two.
		 */
		TRefCountPtr<IPooledRenderTarget> ScreenResolvedTexture;
		TRefCountPtr<IPooledRenderTarget> PixelateEffectRenderTarget;

		uint8 NumSamples = ScreenTargetTexture->GetNumSamples();
		auto ScreenSize = ScreenTargetTexture->GetSizeXY();
		if (NumSamples > 1)
		{
			FPooledRenderTargetDesc desc(FPooledRenderTargetDesc::Create2DDesc(ScreenSize, ScreenTargetTexture->GetFormat(), FClearValueBinding::Black, TexCreate_None, TexCreate_RenderTargetable, false));
			GRenderTargetPool.FindFreeElement(RHICmdList, desc, ScreenResolvedTexture, TEXT("DreamUIPixelateEffectResolveTarget"));
			if (!ScreenResolvedTexture.IsValid())
				return;
			auto ResolveSrc = RegisterExternalTexture(GraphBuilder, ScreenTargetTexture, TEXT("DreamUIPixelateEffectResolveSource"));
			auto ResolveDst = GraphBuilder.RegisterExternalTexture(ScreenResolvedTexture, TEXT("DreamUIPixelateEffectResolveTarget"));
			Renderer->AddResolvePass(GraphBuilder, FRDGTextureMSAA(ResolveSrc, ResolveDst), FIntRect(0, 0, ScreenSize.X, ScreenSize.Y), NumSamples, GlobalShaderMap);
		}

		float calculatedStrength = FMath::Pow(PixelateStrength * INV_MAX_PixelateStrength, 2) * MAX_PixelateStrength;//this can make the pixelate effect transition feel more linear
		calculatedStrength = FMath::Clamp(calculatedStrength, 0.0f, 100.0f);
		calculatedStrength += 1;

		auto width = (int)(RectSize.X / calculatedStrength);
		auto height = (int)(RectSize.Y / calculatedStrength);
		width = FMath::Clamp(width, 1, (int)RectSize.X);
		height = FMath::Clamp(height, 1, (int)RectSize.Y);
		auto TextureSize = FIntPoint(width, height);
		bool bFullScreen = TextureSize == ScreenSize;

		//get render target
		{
			FPooledRenderTargetDesc desc(FPooledRenderTargetDesc::Create2DDesc(FIntPoint(width, height), ScreenTargetTexture->GetFormat(), FClearValueBinding::Black, TexCreate_None, TexCreate_RenderTargetable, false));
			GRenderTargetPool.FindFreeElement(RHICmdList, desc, PixelateEffectRenderTarget, TEXT("DreamUIPixelateEffectRenderTarget"));
			if (!PixelateEffectRenderTarget.IsValid())
			{
				return;
			}
			GraphBuilder.RegisterExternalTexture(PixelateEffectRenderTarget, TEXT("DreamUIPixelateEffectRenderTarget"));
		}
		auto PixelateEffectRenderTargetTexture = PixelateEffectRenderTarget->GetRHI();

		//copy rect area from screen image to a render target, so we can just process this area
		auto ModelViewProjectionMatrix = ObjectToWorldMatrix * ViewProjectionMatrix;
		if (!bFullScreen)
		{
			Renderer->CopyRenderTargetOnMeshRegion(GraphBuilder
				, RegisterExternalTexture(GraphBuilder, PixelateEffectRenderTargetTexture, TEXT("DreamUI_PixelateEffectRenderTargetTexture"))
				, NumSamples > 1 ? ScreenResolvedTexture->GetRHI() : ScreenTargetTexture.GetReference()
				, GlobalShaderMap
				, RenderScreenToMeshRegionVertexArray
				, ModelViewProjectionMatrix
				, bIsRenderTarget
				, FIntRect(0, 0, PixelateEffectRenderTargetTexture->GetSizeXYZ().X, PixelateEffectRenderTargetTexture->GetSizeXYZ().Y)
				, ViewTextureScaleOffset
			);
		}
		else
		{
			Renderer->CopyRenderTarget(GraphBuilder, GlobalShaderMap, NumSamples > 1 ? ScreenResolvedTexture->GetRHI() : ScreenTargetTexture.GetReference()
				, PixelateEffectRenderTargetTexture);
		}

		if (!OutputTargetTexture.IsValid())
		{
			//after pixelate process, copy the area back to screen image
			if (!bFullScreen)
			{
				RenderMeshOnScreen_RenderThread(GraphBuilder, SceneDepth, ScreenTargetTexture, GlobalShaderMap, PixelateEffectRenderTargetTexture, ModelViewProjectionMatrix, ObjectToWorldMatrix, bIsWorldSpace, BlendDepthForWorld, DepthFadeForWorld, DepthTextureScaleOffset, ViewRect
					, TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI());
			}
			else
			{
				Renderer->CopyRenderTarget(GraphBuilder, GlobalShaderMap, PixelateEffectRenderTargetTexture, ScreenTargetTexture
					, TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI());
			}
		}
		else
		{
			Renderer->CopyRenderTarget_ColorCorrect(GraphBuilder, GlobalShaderMap, PixelateEffectRenderTargetTexture, OutputTargetTexture
					, TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI());
		}
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
