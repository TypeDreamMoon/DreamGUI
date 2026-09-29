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

BEGIN_SHADER_PARAMETER_STRUCT(FDreamUIBackgroundBlurPassParameters, )
	RDG_TEXTURE_ACCESS(SourceTexture, ERHIAccess::SRVGraphics)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

DECLARE_CYCLE_STAT(TEXT("PostProcess_BackgroundBlur"), STAT_BackgroundBlur, STATGROUP_DreamGUI);
class FUIBackgroundBlurRenderProxy : public FDreamVisualPostProcessRenderProxy
{
public:
	int MaxDownSampleLevel = 0;
	float BlurStrength = 0.0f;
public:
	FUIBackgroundBlurRenderProxy()
		:FDreamVisualPostProcessRenderProxy()
	{

	}
	virtual bool CanRender()const override
	{
		return BlurStrength > 0.0f;
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
	) override
	{
		SCOPE_CYCLE_COUNTER(STAT_BackgroundBlur);
		if (BlurStrength <= 0.0f && !OutputTargetTexture.IsValid())return;

		const FScreenRead Screen = ReadScreen_RenderThread(GraphBuilder, Renderer, GlobalShaderMap, ScreenTargetTexture);
		const auto ModelViewProjectionMatrix = ObjectToWorldMatrix * ViewProjectionMatrix;
		// Full size and to the screen, the blur is done in the screen itself -- in its resolved copy when it is multisampled.
		// Otherwise in a texture of the region's size, which holds the region, or the whole screen for an output target.
		FRDGTextureRef BlurTexture = Screen.Readable;
		if (!bUseFullSize || OutputTargetTexture.IsValid())
		{
			const FIntPoint WorkSize(static_cast<int32>(FMath::Max(RectSize.X, 1.0f)), static_cast<int32>(FMath::Max(RectSize.Y, 1.0f)));
			BlurTexture = CreateWorkTexture(GraphBuilder, Screen, WorkSize, TEXT("DreamUIBlurEffectRenderTarget"));
			GrabRegion_RenderThread(GraphBuilder, Renderer, GlobalShaderMap, Screen, BlurTexture, bUseFullSize, ModelViewProjectionMatrix, bIsRenderTarget, ViewTextureScaleOffset);
		}

		float MagicNumber = 1.0f / 2.2f;//this is a magic number which can make blur transition feel smooth
		uint32 SourceWidth = BlurTexture->Desc.Extent.X;
		uint32 SourceHeight = BlurTexture->Desc.Extent.Y;
		constexpr uint32 MinBlurDimension = 8;
		const uint32 MinSourceDimension = FMath::Min(SourceWidth, SourceHeight);
		const int32 DimensionLimitedDownSampleCount = static_cast<int32>(FMath::FloorLog2(FMath::Max(MinSourceDimension / MinBlurDimension, 1u)));
		const int32 MaxDownSampleCount = FMath::Min(FMath::Max(MaxDownSampleLevel, 0), DimensionLimitedDownSampleCount);
		float FilteredBlurStrength = FMath::Pow(BlurStrength, MagicNumber) * MaxDownSampleCount;//convert BlurStrength from 0~1 to 0~Count, with adjusted curvature
		FRDGTextureRef PrevTexture = BlurTexture;
		TArray<FRDGTextureRef, TInlineAllocator<8>> DownSampleTextures;//from big to small
		for (int i = MaxDownSampleCount; i >= 1; i--)
		{
			if (FilteredBlurStrength >= i)
			{
				SourceWidth >>= 1;
				SourceHeight >>= 1;
				FRDGTextureRef DownSampleTexture = CreateWorkTexture(GraphBuilder, Screen, FIntPoint(static_cast<int32>(SourceWidth), static_cast<int32>(SourceHeight))
					, TEXT("DreamUI_DownsampleRT"), BlurTexture->Desc.Format);
				Renderer->CopyRenderTarget(GraphBuilder, GlobalShaderMap, PrevTexture, DownSampleTexture);
				DownSampleTextures.Add(DownSampleTexture);
				PrevTexture = DownSampleTexture;
			}
		}
		for (int i = MaxDownSampleCount; i >= 1; i--)
		{
			if (FilteredBlurStrength >= i)
			{
				FRDGTextureRef DownSampleTexture = DownSampleTextures[i - 1];
				DoBlur(DownSampleTexture, FilteredBlurStrength - i, MagicNumber, GraphBuilder, Renderer, GlobalShaderMap);
				FRDGTextureRef NextTexture = i == 1 ? BlurTexture : DownSampleTextures[i - 2];
				if (FilteredBlurStrength >= i + 1)
				{
					Renderer->CopyRenderTarget(GraphBuilder, GlobalShaderMap, DownSampleTexture, NextTexture);
				}
				else
				{
					auto BlendValue = FMath::Clamp(FilteredBlurStrength - i, 0.0f, 1.0f);
					BlendValue = FMath::Pow(BlendValue, MagicNumber);
					Renderer->CopyRenderTarget_BlendAlpha(GraphBuilder, GlobalShaderMap, DownSampleTexture, NextTexture, BlendValue);
				}
			}
		}
		DoBlur(BlurTexture, FilteredBlurStrength, MagicNumber, GraphBuilder, Renderer, GlobalShaderMap);

		WriteBack_RenderThread(GraphBuilder, Renderer, GlobalShaderMap, SceneDepth, Screen, BlurTexture, bUseFullSize, ModelViewProjectionMatrix
			, bIsWorldSpace, BlendDepthForWorld, DepthFadeForWorld, DepthTextureScaleOffset, ViewRect, TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI());
	}
	/** Blurs SourceTexture in place, through a texture of the same size. */
	void DoBlur(FRDGTextureRef SourceTexture
		, float BlurAmount
		, float MagicNumber
		, FRDGBuilder& GraphBuilder
		, FDreamUIRenderer* Renderer
		, FGlobalShaderMap* GlobalShaderMap
		)
	{
		FRDGTextureRef BlurTexture = GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(SourceTexture->Desc.Extent, SourceTexture->Desc.Format
			, FClearValueBinding::Black, TexCreate_RenderTargetable | TexCreate_ShaderResource), TEXT("DreamUIBackgroundBlurIntermediate"));

		TShaderMapRef<FDreamUISimplePostProcessVS> VertexShader(GlobalShaderMap);
		TShaderMapRef<FDreamUIPostProcessGaussianBlurPS> PixelShader(GlobalShaderMap);
		auto SamplerState = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

		BlurAmount = FMath::Clamp(BlurAmount, 0.0f, 1.0f);
		BlurAmount = FMath::Pow(BlurAmount, MagicNumber);
				
		auto* VerticalPassParameters = GraphBuilder.AllocParameters<FDreamUIBackgroundBlurPassParameters>();
		VerticalPassParameters->SourceTexture = SourceTexture;
		VerticalPassParameters->RenderTargets[0] = FRenderTargetBinding(BlurTexture, ERenderTargetLoadAction::ENoAction);
		GraphBuilder.AddPass(
			RDG_EVENT_NAME("DreamUIBackgroundBlur_Pass_Horizontal"),
			VerticalPassParameters,
			ERDGPassFlags::Raster,
			[this, VertexShader, PixelShader, Renderer, SourceTexture, BlurTexture, SamplerState, BlurAmount](FRHICommandListImmediate& RHICmdList)
			{
				SourceTexture->MarkResourceAsUsed();
				FGraphicsPipelineStateInitializer GraphicsPSOInit;
				RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
				GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, ECompareFunction::CF_Always>::GetRHI();
				GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
				GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
				GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GetDreamUIPostProcessVertexDeclaration();
				GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
				GraphicsPSOInit.PrimitiveType = EPrimitiveType::PT_TriangleList;
				SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0, EApplyRendertargetOption::CheckApply);
				//render vertical
				RHICmdList.SetViewport(0, 0, 0.0f, BlurTexture->Desc.Extent.X, BlurTexture->Desc.Extent.Y, 1.0f);
				FDreamUIPostProcessGaussianBlurPS::FParameters Parameters;
				Parameters.MainTex = SourceTexture->GetRHI();
				Parameters.MainTexSampler = SamplerState;
				Parameters.BlurStrength = FVector2f(1.0f / SourceTexture->Desc.Extent.X * BlurAmount, 0);
				SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), Parameters);
				Renderer->DrawFullScreenQuad(RHICmdList);
			});

		auto* HorizontalPassParameters = GraphBuilder.AllocParameters<FDreamUIBackgroundBlurPassParameters>();
		HorizontalPassParameters->SourceTexture = BlurTexture;
		HorizontalPassParameters->RenderTargets[0] = FRenderTargetBinding(SourceTexture, ERenderTargetLoadAction::ENoAction);
		GraphBuilder.AddPass(
			RDG_EVENT_NAME("DreamUIBackgroundBlur_Pass_Vertical"),
			HorizontalPassParameters,
			ERDGPassFlags::Raster,
			[this, VertexShader, PixelShader, Renderer, SourceTexture, BlurTexture, SamplerState, BlurAmount](FRHICommandListImmediate& RHICmdList)
			{
				BlurTexture->MarkResourceAsUsed();
				FGraphicsPipelineStateInitializer GraphicsPSOInit;
				RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
				GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, ECompareFunction::CF_Always>::GetRHI();
				GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
				GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
				GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GetDreamUIPostProcessVertexDeclaration();
				GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
				GraphicsPSOInit.PrimitiveType = EPrimitiveType::PT_TriangleList;
				SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0, EApplyRendertargetOption::CheckApply);
				//render horizontal
				RHICmdList.SetViewport(0, 0, 0.0f, SourceTexture->Desc.Extent.X, SourceTexture->Desc.Extent.Y, 1.0f);
				FDreamUIPostProcessGaussianBlurPS::FParameters Parameters;
				Parameters.MainTex = BlurTexture->GetRHI();
				Parameters.MainTexSampler = SamplerState;
				Parameters.BlurStrength = FVector2f(0, 1.0f / BlurTexture->Desc.Extent.Y * BlurAmount);
				SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), Parameters);
				Renderer->DrawFullScreenQuad(RHICmdList);
			});

	}
};

FDreamVisualPostProcessRenderProxyPtr DreamUIPostProcessEffects::CreateBackgroundBlurProxy()
{
	return MakeShared<FUIBackgroundBlurRenderProxy, ESPMode::ThreadSafe>();
}

void DreamUIPostProcessEffects::SetBackgroundBlur_GameThread(const FDreamVisualPostProcessRenderProxyPtr& InProxy, float InBlurStrength, int32 InMaxDownSampleLevel)
{
	if (!InProxy.IsValid())
	{
		return;
	}
	auto BackgroundBlurRenderProxy = StaticCastSharedPtr<FUIBackgroundBlurRenderProxy>(InProxy);
	ENQUEUE_RENDER_COMMAND(FDreamBackgroundBlur_UpdateData)
		([BackgroundBlurRenderProxy, InBlurStrength, InMaxDownSampleLevel](FRHICommandListImmediate& RHICmdList)
		{
			BackgroundBlurRenderProxy->MaxDownSampleLevel = InMaxDownSampleLevel;
			BackgroundBlurRenderProxy->BlurStrength = InBlurStrength;
		});
}

void DreamUIPostProcessEffects::SetBackgroundBlurStrength_GameThread(const FDreamVisualPostProcessRenderProxyPtr& InProxy, float InBlurStrength)
{
	if (!InProxy.IsValid())
	{
		return;
	}
	auto BackgroundBlurRenderProxy = StaticCastSharedPtr<FUIBackgroundBlurRenderProxy>(InProxy);
	ENQUEUE_RENDER_COMMAND(FDreamBackgroundBlur_UpdateData)
		([BackgroundBlurRenderProxy, InBlurStrength](FRHICommandListImmediate& RHICmdList)
			{
				BackgroundBlurRenderProxy->BlurStrength = InBlurStrength;
			});
}
