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
#include "ScreenRendering.h"

DECLARE_CYCLE_STAT(TEXT("PostProcess PixelSort"), STAT_PixelSort, STATGROUP_DreamGUI);

BEGIN_SHADER_PARAMETER_STRUCT(FDreamUIPixelSortPassParameters, )
	RDG_TEXTURE_ACCESS(SourceTexture, ERHIAccess::SRVGraphics)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FDreamUIPixelSortGatherParameters, )
	RDG_TEXTURE_ACCESS(SourceTexture, ERHIAccess::SRVGraphics)
	RDG_TEXTURE_ACCESS(DestinationTexture, ERHIAccess::SRVGraphics)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

/**
 * The render-thread half of the pixel sort visual (UDreamPixelSort, an extension). Its arithmetic is mirrored
 * by DreamUIPostProcessPixelSort.usf and by the CPU reference in DreamPixelSort.cpp -- keep the three in step.
 */
class FDreamPixelSortRenderProxy : public FDreamVisualPostProcessRenderProxy
{
public:
	/** How far a texel may look along its run, and therefore how far it may travel. */
	int32 SearchRadius = 0;
	/** Threshold band, already ordered low-then-high. */
	FVector2f Band = FVector2f(0.25f, 0.8f);
	DreamUIPostProcessEffects::EPixelSortAxis SortAxis = DreamUIPostProcessEffects::EPixelSortAxis::Vertical;
	DreamUIPostProcessEffects::EPixelSortKey SortKey = DreamUIPostProcessEffects::EPixelSortKey::Luminance;
	DreamUIPostProcessEffects::EPixelSortInterval IntervalMode = DreamUIPostProcessEffects::EPixelSortInterval::Threshold;
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
	)override;
};

void FDreamPixelSortRenderProxy::OnRenderPostProcess_RenderThread(
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
)
{
	SCOPE_CYCLE_COUNTER(STAT_PixelSort);
	if (!CanRender())return;

	const FScreenRead Screen = ReadScreen_RenderThread(GraphBuilder, Renderer, GlobalShaderMap, ScreenTargetTexture);
	const bool bFullScreen = bUseFullSize;
	const FIntPoint RegionSize = DreamUIPostProcessEffects::ResolvePixelSortRegionSize(bUseFullSize, RectSize, Screen.Size);
	FRDGTextureRef SourceTexture = CreateWorkTexture(GraphBuilder, Screen, RegionSize, TEXT("DreamUIPixelSortSource"));
	// One float channel for the destination index. R32F holds every integer up to 2^24 exactly,
	// far past any line length -- packing an index into 8-bit RGB, as the reference shader has to
	// on Shadertoy, is unnecessary here and would only add rounding to something that must be
	// compared for equality.
	FRDGTextureRef DestinationTexture = CreateWorkTexture(GraphBuilder, Screen, RegionSize, TEXT("DreamUIPixelSortDestinations"), PF_R32_FLOAT);
	FRDGTextureRef ResultRDGTexture = CreateWorkTexture(GraphBuilder, Screen, RegionSize, TEXT("DreamUIPixelSortResult"));

	// Grab the widget's region out of the screen.
	const auto ModelViewProjectionMatrix = ObjectToWorldMatrix * ViewProjectionMatrix;
	GrabRegion_RenderThread(GraphBuilder, Renderer, GlobalShaderMap, Screen, SourceTexture, bFullScreen, ModelViewProjectionMatrix, bIsRenderTarget, ViewTextureScaleOffset);

	TShaderMapRef<FDreamUISimplePostProcessVS> VertexShader(GlobalShaderMap);
	TShaderMapRef<FDreamUIPostProcessPixelSortRankPS> RankShader(GlobalShaderMap);
	TShaderMapRef<FDreamUIPostProcessPixelSortGatherPS> GatherShader(GlobalShaderMap);

	const FVector2f RegionSizeFloat((float)RegionSize.X, (float)RegionSize.Y);
	const float AxisFlag = SortAxis == DreamUIPostProcessEffects::EPixelSortAxis::Horizontal ? 0.0f : 1.0f;
	const float KeyFlag = (float)(uint8)SortKey;
	const float DescendingFlag = bDescending ? 1.0f : 0.0f;
	const float RadiusFloat = (float)SearchRadius;
	const FVector4f IntervalParams((float)(uint8)IntervalMode, (float)IntervalLength, Randomness, 0.0f);
	// POINT sampling is mandatory, not a preference: the rank counts exact texels, and a blended
	// sample belongs to no texel at all -- it would make neighbouring invocations disagree about
	// which of them comes first and two texels would claim one destination.
	const auto SortSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

	// Pass 1: every texel scans its own run and writes where it is going.
	{
		auto* PassParameters = GraphBuilder.AllocParameters<FDreamUIPixelSortPassParameters>();
		PassParameters->SourceTexture = SourceTexture;
		PassParameters->RenderTargets[0] = FRenderTargetBinding(DestinationTexture, ERenderTargetLoadAction::ENoAction);
		GraphBuilder.AddPass(RDG_EVENT_NAME("DreamUIPixelSort_Rank"), PassParameters, ERDGPassFlags::Raster,
			[VertexShader, RankShader, SourceTexture, DestinationTexture, SortSampler,
			RegionSizeFloat, Band = this->Band, AxisFlag, KeyFlag, DescendingFlag, RadiusFloat, IntervalParams]
			(FRHICommandListImmediate& RHICmdList)
			{
				SourceTexture->MarkResourceAsUsed();
				FGraphicsPipelineStateInitializer PSOInit;
				RHICmdList.ApplyCachedRenderTargets(PSOInit);
				PSOInit.DepthStencilState = TStaticDepthStencilState<false, ECompareFunction::CF_Always>::GetRHI();
				PSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
				PSOInit.BlendState = TStaticBlendState<>::GetRHI();
				PSOInit.BoundShaderState.VertexDeclarationRHI = GetDreamUIPostProcessVertexDeclaration();
				PSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
				PSOInit.BoundShaderState.PixelShaderRHI = RankShader.GetPixelShader();
				PSOInit.PrimitiveType = EPrimitiveType::PT_TriangleList;
				SetGraphicsPipelineState(RHICmdList, PSOInit, 0, EApplyRendertargetOption::CheckApply);
				RHICmdList.SetViewport(0, 0, 0.0f, DestinationTexture->Desc.Extent.X, DestinationTexture->Desc.Extent.Y, 1.0f);
				FDreamUIPostProcessPixelSortRankPS::FParameters Parameters;
				Parameters.MainTex = SourceTexture->GetRHI();
				Parameters.MainTexSampler = SortSampler;
				Parameters.RegionSize = RegionSizeFloat;
				Parameters.Band = Band;
				Parameters.SortAxis = AxisFlag;
				Parameters.SortKey = KeyFlag;
				Parameters.Descending = DescendingFlag;
				Parameters.SearchRadius = RadiusFloat;
				Parameters.IntervalParams = IntervalParams;
				SetShaderParameters(RHICmdList, RankShader, RankShader.GetPixelShader(), Parameters);
				FDreamUIRenderer::DrawFullScreenQuad(RHICmdList);
			});
	}

	// Pass 2: invert that into who comes here, because a pixel shader can only gather.
	{
		auto* PassParameters = GraphBuilder.AllocParameters<FDreamUIPixelSortGatherParameters>();
		PassParameters->SourceTexture = SourceTexture;
		PassParameters->DestinationTexture = DestinationTexture;
		PassParameters->RenderTargets[0] = FRenderTargetBinding(ResultRDGTexture, ERenderTargetLoadAction::ENoAction);
		GraphBuilder.AddPass(RDG_EVENT_NAME("DreamUIPixelSort_Gather"), PassParameters, ERDGPassFlags::Raster,
			[VertexShader, GatherShader, SourceTexture, DestinationTexture, ResultRDGTexture,
			SortSampler, RegionSizeFloat, AxisFlag, RadiusFloat](FRHICommandListImmediate& RHICmdList)
			{
				SourceTexture->MarkResourceAsUsed();
				DestinationTexture->MarkResourceAsUsed();
				FGraphicsPipelineStateInitializer PSOInit;
				RHICmdList.ApplyCachedRenderTargets(PSOInit);
				PSOInit.DepthStencilState = TStaticDepthStencilState<false, ECompareFunction::CF_Always>::GetRHI();
				PSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
				PSOInit.BlendState = TStaticBlendState<>::GetRHI();
				PSOInit.BoundShaderState.VertexDeclarationRHI = GetDreamUIPostProcessVertexDeclaration();
				PSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
				PSOInit.BoundShaderState.PixelShaderRHI = GatherShader.GetPixelShader();
				PSOInit.PrimitiveType = EPrimitiveType::PT_TriangleList;
				SetGraphicsPipelineState(RHICmdList, PSOInit, 0, EApplyRendertargetOption::CheckApply);
				RHICmdList.SetViewport(0, 0, 0.0f, ResultRDGTexture->Desc.Extent.X, ResultRDGTexture->Desc.Extent.Y, 1.0f);
				FDreamUIPostProcessPixelSortGatherPS::FParameters Parameters;
				Parameters.MainTex = SourceTexture->GetRHI();
				Parameters.MainTexSampler = SortSampler;
				Parameters.DestinationTex = DestinationTexture->GetRHI();
				Parameters.DestinationTexSampler = SortSampler;
				Parameters.RegionSize = RegionSizeFloat;
				Parameters.SortAxis = AxisFlag;
				Parameters.SearchRadius = RadiusFloat;
				SetShaderParameters(RHICmdList, GatherShader, GatherShader.GetPixelShader(), Parameters);
				FDreamUIRenderer::DrawFullScreenQuad(RHICmdList);
			});
	}

	WriteBack_RenderThread(GraphBuilder, Renderer, GlobalShaderMap, SceneDepth, Screen, ResultRDGTexture, bFullScreen, ModelViewProjectionMatrix
		, bIsWorldSpace, BlendDepthForWorld, DepthFadeForWorld, DepthTextureScaleOffset, ViewRect, SortSampler);
}

FIntPoint DreamUIPostProcessEffects::ResolvePixelSortRegionSize(bool bInUseFullSize, const FVector2f& InRectSize, const FIntPoint& InScreenSize)
{
	if (bInUseFullSize)
	{
		return InScreenSize;
	}
	return FIntPoint(FMath::Max((int32)InRectSize.X, 1), FMath::Max((int32)InRectSize.Y, 1));
}

FDreamVisualPostProcessRenderProxyPtr DreamUIPostProcessEffects::CreatePixelSortProxy()
{
	return MakeShared<FDreamPixelSortRenderProxy, ESPMode::ThreadSafe>();
}

void DreamUIPostProcessEffects::SetPixelSort_GameThread(const FDreamVisualPostProcessRenderProxyPtr& InProxy, const FPixelSortParams& InParams)
{
	if (!InProxy.IsValid())
	{
		return;
	}
	auto TempRenderProxy = StaticCastSharedPtr<FDreamPixelSortRenderProxy>(InProxy);
	// Resolved on the game thread and shipped by value, so the render thread never reads a UPROPERTY.
	ENQUEUE_RENDER_COMMAND(FDreamPixelSort_UpdateData)
		([TempRenderProxy, InParams](FRHICommandListImmediate& RHICmdList)
			{
				TempRenderProxy->SearchRadius = InParams.SearchRadius;
				TempRenderProxy->Band = InParams.Band;
				TempRenderProxy->SortAxis = InParams.SortAxis;
				TempRenderProxy->SortKey = InParams.SortKey;
				TempRenderProxy->IntervalMode = InParams.IntervalMode;
				TempRenderProxy->IntervalLength = InParams.IntervalLength;
				TempRenderProxy->Randomness = InParams.Randomness;
				TempRenderProxy->bDescending = InParams.bDescending;
			});
}
