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
#include "SceneTextures.h"
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

void FDreamPixelSortRenderProxy::OnRenderPostProcess_RenderThread(
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
)
{
	SCOPE_CYCLE_COUNTER(STAT_PixelSort);
	if (!CanRender())return;

	auto& RHICmdList = GraphBuilder.RHICmdList;

	// Each pooled target is handed to the graph with GraphBuilder.RegisterExternalTexture as soon as it
	// is taken, as blur does, which parks a strong reference on it until the graph has executed. This
	// function only RECORDS passes: releasing the targets at its end, as it used to, put them back in
	// GRenderTargetPool before any pass had run, and a later effect in the same frame could be handed
	// the same memory with RDG seeing no dependency between the two.
	TRefCountPtr<IPooledRenderTarget> ScreenResolvedTexture;
	TRefCountPtr<IPooledRenderTarget> SortTargetA;
	TRefCountPtr<IPooledRenderTarget> SortTargetB;
	TRefCountPtr<IPooledRenderTarget> IndexTarget;

	const uint8 NumSamples = ScreenTargetTexture->GetNumSamples();
	const auto ScreenSize = ScreenTargetTexture->GetSizeXY();
	if (NumSamples > 1)
	{
		// A multisampled screen texture cannot be sampled directly; resolve first. This MUST be
		// AddResolvePass and not CopyRenderTarget: the copy shader declares its source as a plain
		// Texture2D, and binding an MSAA texture to a Texture2D slot is a dimension mismatch that
		// reads as zero on D3D12 -- the resolve silently comes back black and every grab downstream
		// is a grab of nothing. The resolve shader is the one place in the plugin that declares
		// Texture2DMS and loads each sample. Getting this wrong cost a day; see blur and pixelate,
		// which have always done it this way.
		FPooledRenderTargetDesc ResolveDesc(FPooledRenderTargetDesc::Create2DDesc(ScreenSize, ScreenTargetTexture->GetFormat(),
			FClearValueBinding::Black, TexCreate_None, TexCreate_RenderTargetable, false));
		GRenderTargetPool.FindFreeElement(RHICmdList, ResolveDesc, ScreenResolvedTexture, TEXT("DreamUIPixelSortResolveTarget"));
		if (!ScreenResolvedTexture.IsValid())
		{
			return;
		}
		auto ResolveSrc = RegisterExternalTexture(GraphBuilder, ScreenTargetTexture, TEXT("DreamUIPixelSortResolveSource"));
		auto ResolveDst = GraphBuilder.RegisterExternalTexture(ScreenResolvedTexture, TEXT("DreamUIPixelSortResolveTarget"));
		Renderer->AddResolvePass(GraphBuilder, FRDGTextureMSAA(ResolveSrc, ResolveDst), FIntRect(0, 0, ScreenSize.X, ScreenSize.Y), NumSamples, GlobalShaderMap);
	}

	// Read the FLAG, not a coincidence of sizes. bUseFullSize makes RectSize the root canvas's
	// authored resolution, which almost never equals the screen -- so testing sizes takes the
	// widget-rect path while the author has asked for the screen, and the sort then runs in a buffer
	// whose texels are not pixels and whose edges sample past what is on screen.
	const bool bFullScreen = bUseFullSize;
	const FIntPoint RegionSize = DreamUIPostProcessEffects::ResolvePixelSortRegionSize(bUseFullSize, RectSize, ScreenSize);
	// Still no in-place shortcut, unlike blur: blur can write straight into the backbuffer because
	// its own passes ping-pong internally, but a sort pass reading and writing one texture produces
	// per-tile garbage that varies by GPU. Full screen here means a screen-sized SCRATCH buffer.

	{
		FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(RegionSize, ScreenTargetTexture->GetFormat(),
			FClearValueBinding::Black, TexCreate_None, TexCreate_RenderTargetable, false));
		GRenderTargetPool.FindFreeElement(RHICmdList, Desc, SortTargetA, TEXT("DreamUIPixelSortTargetA"));
		GRenderTargetPool.FindFreeElement(RHICmdList, Desc, SortTargetB, TEXT("DreamUIPixelSortTargetB"));
		// One float channel for the destination index. R32F holds every integer up to 2^24 exactly,
		// far past any line length -- packing an index into 8-bit RGB, as the reference shader has to
		// on Shadertoy, is unnecessary here and would only add rounding to something that must be
		// compared for equality.
		FPooledRenderTargetDesc IndexDesc(FPooledRenderTargetDesc::Create2DDesc(RegionSize, PF_R32_FLOAT,
			FClearValueBinding::Black, TexCreate_None, TexCreate_RenderTargetable | TexCreate_ShaderResource, false));
		GRenderTargetPool.FindFreeElement(RHICmdList, IndexDesc, IndexTarget, TEXT("DreamUIPixelSortIndexTarget"));
		if (!SortTargetA.IsValid() || !SortTargetB.IsValid() || !IndexTarget.IsValid())
		{
			return;
		}
		GraphBuilder.RegisterExternalTexture(SortTargetA, TEXT("DreamUIPixelSortTargetA"));
		GraphBuilder.RegisterExternalTexture(SortTargetB, TEXT("DreamUIPixelSortTargetB"));
		GraphBuilder.RegisterExternalTexture(IndexTarget, TEXT("DreamUIPixelSortIndexTarget"));
	}
	auto SortTextureA = SortTargetA->GetRHI();
	auto SortTextureB = SortTargetB->GetRHI();
	auto IndexTexture = IndexTarget->GetRHI();

	// Grab the widget's region out of the screen.
	const auto ModelViewProjectionMatrix = ObjectToWorldMatrix * ViewProjectionMatrix;
	auto SourceScreenTexture = NumSamples > 1 ? ScreenResolvedTexture->GetRHI() : ScreenTargetTexture.GetReference();
	if (!bFullScreen)
	{
		Renderer->CopyRenderTargetOnMeshRegion(GraphBuilder
			, RegisterExternalTexture(GraphBuilder, SortTextureA, TEXT("DreamUIPixelSortRegionGrab"))
			, SourceScreenTexture
			, GlobalShaderMap
			, RenderScreenToMeshRegionVertexArray
			, ModelViewProjectionMatrix
			, bIsRenderTarget
			, FIntRect(0, 0, RegionSize.X, RegionSize.Y)
			, ViewTextureScaleOffset
		);
	}
	else
	{
		Renderer->CopyRenderTarget(GraphBuilder, GlobalShaderMap, SourceScreenTexture, SortTextureA);
	}

	// Registered once per pooled target. Registering the same RHI texture twice gives RDG two
	// handles onto one resource, so it cannot see the dependency between passes and they race.
	FRDGTextureRef SourceTexture = RegisterExternalTexture(GraphBuilder, SortTextureA, TEXT("DreamUIPixelSortSource"));
	FRDGTextureRef DestinationTexture = RegisterExternalTexture(GraphBuilder, IndexTexture, TEXT("DreamUIPixelSortDestinations"));
	FRDGTextureRef ResultRDGTexture = RegisterExternalTexture(GraphBuilder, SortTextureB, TEXT("DreamUIPixelSortResult"));

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
			[VertexShader, RankShader, Renderer, SourceTexture, DestinationTexture, SortSampler,
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
				VertexShader->SetParameters(RHICmdList);
				RHICmdList.SetViewport(0, 0, 0.0f, DestinationTexture->Desc.Extent.X, DestinationTexture->Desc.Extent.Y, 1.0f);
				RankShader->SetParameters(RHICmdList, SourceTexture->GetRHI(), SortSampler,
					RegionSizeFloat, Band, AxisFlag, KeyFlag, DescendingFlag, RadiusFloat, IntervalParams);
				Renderer->DrawFullScreenQuad(RHICmdList);
			});
	}

	// Pass 2: invert that into who comes here, because a pixel shader can only gather.
	{
		auto* PassParameters = GraphBuilder.AllocParameters<FDreamUIPixelSortGatherParameters>();
		PassParameters->SourceTexture = SourceTexture;
		PassParameters->DestinationTexture = DestinationTexture;
		PassParameters->RenderTargets[0] = FRenderTargetBinding(ResultRDGTexture, ERenderTargetLoadAction::ENoAction);
		GraphBuilder.AddPass(RDG_EVENT_NAME("DreamUIPixelSort_Gather"), PassParameters, ERDGPassFlags::Raster,
			[VertexShader, GatherShader, Renderer, SourceTexture, DestinationTexture, ResultRDGTexture,
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
				VertexShader->SetParameters(RHICmdList);
				RHICmdList.SetViewport(0, 0, 0.0f, ResultRDGTexture->Desc.Extent.X, ResultRDGTexture->Desc.Extent.Y, 1.0f);
				GatherShader->SetParameters(RHICmdList, SourceTexture->GetRHI(), SortSampler,
					DestinationTexture->GetRHI(), RegionSizeFloat, AxisFlag, RadiusFloat);
				Renderer->DrawFullScreenQuad(RHICmdList);
			});
	}

	auto ResultTexture = SortTextureB;

	const auto PointSampler = SortSampler;
	if (!OutputTargetTexture.IsValid())
	{
		if (!bFullScreen)
		{
			RenderMeshOnScreen_RenderThread(GraphBuilder, SceneTextures, ScreenTargetTexture, GlobalShaderMap, ResultTexture,
				ModelViewProjectionMatrix, ObjectToWorldMatrix, bIsWorldSpace, BlendDepthForWorld, DepthFadeForWorld,
				DepthTextureScaleOffset, ViewRect, PointSampler);
		}
		else
		{
			Renderer->CopyRenderTarget(GraphBuilder, GlobalShaderMap, ResultTexture, ScreenTargetTexture, PointSampler);
		}
	}
	else
	{
		// Pixelate omits this branch from its GetRenderProxy push chain and the RenderTarget output
		// mode quietly does nothing as a result. Blur's version is the complete one.
		Renderer->CopyRenderTarget_ColorCorrect(GraphBuilder, GlobalShaderMap, ResultTexture,
			OutputTargetTexture, PointSampler);
	}
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
