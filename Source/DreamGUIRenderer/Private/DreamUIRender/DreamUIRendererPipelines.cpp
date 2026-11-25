// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "DreamUIRender/DreamUIPipelines.h"
#include "DreamUIRender/DreamUIBaseShaders.h"
#include "DreamUIRender/DreamUIBlendMode.h"
#include "DreamUIRender/DreamUIMeshVertex.h"
#include "DreamUIRender/DreamUIPostProcessShaders.h"
#include "DreamUIRender/DreamUIPostProcessVertex.h"
#include "DreamUIRender/DreamUIRenderer.h"
#include "DreamUIRender/DreamUIResolveShaders.h"
#include "RenderUtils.h"

// The pipelines of FDreamUIRenderer's own draws (DreamUIRenderer.cpp), and every combination of them it can draw with.

void DreamUIPipelines::InitCopyTarget(FGraphicsPipelineStateInitializer& Init, FGlobalShaderMap* ShaderMap, bool bColorCorrect, bool bBlendAlpha, bool bPremultipliedAlpha)
{
	TShaderMapRef<FDreamUISimplePostProcessVS> VertexShader(ShaderMap);
	FDreamUISimpleCopyTargetPS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FDreamUISimpleCopyTargetPS::FColorCorrect>(bColorCorrect);
	PermutationVector.Set<FDreamUISimpleCopyTargetPS::FBlendAlpha>(bBlendAlpha);
	TShaderMapRef<FDreamUISimpleCopyTargetPS> PixelShader(ShaderMap, PermutationVector);
	Init.DepthStencilState = TStaticDepthStencilState<false, ECompareFunction::CF_Always>::GetRHI();
	Init.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
	Init.BlendState = bBlendAlpha
		? (bPremultipliedAlpha
			? TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_InverseSourceAlpha, BO_Add, BF_One, BF_InverseSourceAlpha>::GetRHI()
			: TStaticBlendState<CW_RGBA, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_InverseDestAlpha, BF_One>::GetRHI())
		: TStaticBlendState<>::GetRHI();
	Init.PrimitiveType = EPrimitiveType::PT_TriangleList;
	Init.BoundShaderState.VertexDeclarationRHI = GetDreamUIPostProcessVertexDeclaration();
	Init.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
	Init.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
}

void DreamUIPipelines::InitCopyMeshRegion(FGraphicsPipelineStateInitializer& Init, FGlobalShaderMap* ShaderMap, bool bColorCorrect)
{
	TShaderMapRef<FDreamUICopyMeshRegionVS> VertexShader(ShaderMap);
	FDreamUICopyMeshRegionPS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FDreamUICopyMeshRegionPS::FColorCorrect>(bColorCorrect);
	TShaderMapRef<FDreamUICopyMeshRegionPS> PixelShader(ShaderMap, PermutationVector);
	Init.DepthStencilState = TStaticDepthStencilState<false, ECompareFunction::CF_Always>::GetRHI();
	Init.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
	Init.BlendState = TStaticBlendState<>::GetRHI();
	Init.BoundShaderState.VertexDeclarationRHI = GetDreamUIPostProcessCopyMeshRegionVertexDeclaration();
	Init.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
	Init.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
	Init.PrimitiveType = EPrimitiveType::PT_TriangleList;
}

/**
 * The shader outputs premultiplied colour; two-sided like the UI materials it replaces.
 *
 * Two-step on purpose. The depth/stencil/rasterizer half comes from the shared helper with
 * BLEND_AlphaComposite, which is what every built-in draw used to be nailed to; then the blend
 * state alone is replaced by the one this draw-call asked for. Per-element blend modes are safe
 * to set here because the batcher never puts two different ones in one draw-call -- see
 * FDreamUIDrawCall::CanConsumeUIGeometryForBatchMesh.
 */
void DreamUIPipelines::InitBuiltIn(FGraphicsPipelineStateInitializer& Init, ERHIFeatureLevel::Type FeatureLevel, FRHIVertexShader* InVertexShader, FRHIPixelShader* InPixelShader
	, EDreamUIBlendMode BlendMode, bool bIsDepthValid, bool bReverseCulling)
{
	FDreamUIRenderer::SetGraphicPipelineState_BlendDepthStencilRasterize(FeatureLevel, Init, BLEND_AlphaComposite
		, false, true, false, bIsDepthValid, bReverseCulling
	);
	Init.BlendState = FDreamUIRenderer::GetBuiltInBlendState(BlendMode);
	Init.BoundShaderState.VertexDeclarationRHI = GetDreamUIMeshVertexDeclaration();
	Init.BoundShaderState.VertexShaderRHI = InVertexShader;
	Init.BoundShaderState.PixelShaderRHI = InPixelShader;
	Init.PrimitiveType = EPrimitiveType::PT_TriangleList;
}

void DreamUIPipelines::InitResolve(FGraphicsPipelineStateInitializer& Init, FGlobalShaderMap* ShaderMap, uint32 InNumSamples)
{
	TShaderMapRef<FDreamUIResolveShaderVS> VertexShader(ShaderMap);
	FDreamUIResolveShaderPS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FDreamUIResolveShaderPS::FSampleCount>(InNumSamples);
	TShaderMapRef<FDreamUIResolveShaderPS> PixelShader(ShaderMap, PermutationVector);
	Init.BlendState = TStaticBlendState<>::GetRHI();
	Init.RasterizerState = TStaticRasterizerState<>::GetRHI();
	Init.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
	Init.BoundShaderState.VertexDeclarationRHI = GetVertexDeclarationFVector4();
	Init.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
	Init.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
	Init.PrimitiveType = PT_TriangleList;
}

void DreamUIPipelines::InitPostProcessQuad(FGraphicsPipelineStateInitializer& Init, FRHIVertexShader* InVertexShader, FRHIPixelShader* InPixelShader)
{
	Init.DepthStencilState = TStaticDepthStencilState<false, ECompareFunction::CF_Always>::GetRHI();
	Init.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
	Init.BlendState = TStaticBlendState<>::GetRHI();
	Init.BoundShaderState.VertexDeclarationRHI = GetDreamUIPostProcessVertexDeclaration();
	Init.BoundShaderState.VertexShaderRHI = InVertexShader;
	Init.BoundShaderState.PixelShaderRHI = InPixelShader;
	Init.PrimitiveType = EPrimitiveType::PT_TriangleList;
}

void DreamUIPipelines::CollectBackgroundBlur(FCollector& Collector)
{
	// Both passes of the separable blur: into its intermediate, then back into the copy it blurs; both single-sampled.
	const TShaderRef<FDreamUISimplePostProcessVS> VertexShader = Collector.Find<FDreamUISimplePostProcessVS>();
	const TShaderRef<FDreamUIPostProcessGaussianBlurPS> PixelShader = Collector.Find<FDreamUIPostProcessGaussianBlurPS>();
	if (VertexShader.IsValid() && PixelShader.IsValid())
	{
		FGraphicsPipelineStateInitializer Init;
		InitPostProcessQuad(Init, VertexShader.GetVertexShader(), PixelShader.GetPixelShader());
		Collector.Add(Init, Collector.OffscreenTargets, EPSOPrecachePriority::Medium);
	}
}

void DreamUIPipelines::CollectPixelSort(FCollector& Collector)
{
	const TShaderRef<FDreamUISimplePostProcessVS> VertexShader = Collector.Find<FDreamUISimplePostProcessVS>();
	const TShaderRef<FDreamUIPostProcessPixelSortRankPS> RankShader = Collector.Find<FDreamUIPostProcessPixelSortRankPS>();
	const TShaderRef<FDreamUIPostProcessPixelSortGatherPS> GatherShader = Collector.Find<FDreamUIPostProcessPixelSortGatherPS>();
	if (!VertexShader.IsValid())
	{
		return;
	}
	// The rank pass writes each texel's destination into one float channel; the gather pass, the sorted colour into a
	// copy of the region.
	if (RankShader.IsValid())
	{
		FTarget Destinations;
		Destinations.Format = PF_R32_FLOAT;
		FGraphicsPipelineStateInitializer Init;
		InitPostProcessQuad(Init, VertexShader.GetVertexShader(), RankShader.GetPixelShader());
		Collector.Add(Init, Destinations, EPSOPrecachePriority::Low);
	}
	if (GatherShader.IsValid())
	{
		FGraphicsPipelineStateInitializer Init;
		InitPostProcessQuad(Init, VertexShader.GetVertexShader(), GatherShader.GetPixelShader());
		Collector.Add(Init, Collector.OffscreenTargets, EPSOPrecachePriority::Low);
	}
}

#if WITH_EDITOR
void DreamUIPipelines::InitGizmo(FGraphicsPipelineStateInitializer& Init, ERHIFeatureLevel::Type FeatureLevel, FGlobalShaderMap* ShaderMap, EPrimitiveType PrimitiveType)
{
	TShaderMapRef<FDreamUIBaseVS> VertexShader(ShaderMap);
	FDreamUIBasePS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FDreamUIBasePS::FPlainColor>(true);
	TShaderMapRef<FDreamUIBasePS> PixelShader(ShaderMap, PermutationVector);
	// Straight alpha over the scene, two-sided, no depth: helper geometry is an overlay.
	FDreamUIRenderer::SetGraphicPipelineState_BlendDepthStencilRasterize(FeatureLevel, Init, BLEND_AlphaComposite
		, false, true, true, false, false);
	Init.BoundShaderState.VertexDeclarationRHI = GetDreamUIMeshVertexDeclaration();
	Init.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
	Init.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
	Init.PrimitiveType = PrimitiveType;
}
#endif

void DreamUIPipelines::CollectRenderer(FCollector& Collector)
{
	FGlobalShaderMap* ShaderMap = Collector.ShaderMap;

	// The copies: over the whole target (FDreamUIRenderer::CopyRenderTarget and its siblings) and onto a mesh's region.
	for (int32 Variant = 0; Variant < 4; ++Variant)
	{
		const bool bColorCorrect = Variant == 1;
		const bool bBlendAlpha = Variant >= 2;
		const bool bPremultipliedAlpha = Variant == 3;
		FDreamUISimpleCopyTargetPS::FPermutationDomain Permutation;
		Permutation.Set<FDreamUISimpleCopyTargetPS::FColorCorrect>(bColorCorrect);
		Permutation.Set<FDreamUISimpleCopyTargetPS::FBlendAlpha>(bBlendAlpha);
		if (!Collector.Find<FDreamUISimplePostProcessVS>().IsValid() || !Collector.Find<FDreamUISimpleCopyTargetPS>(Permutation).IsValid())
		{
			continue;
		}
		FGraphicsPipelineStateInitializer Init;
		InitCopyTarget(Init, ShaderMap, bColorCorrect, bBlendAlpha, bPremultipliedAlpha);
		Collector.Add(Init, Collector.AnyTargets, EPSOPrecachePriority::Medium);
	}
	for (const bool bColorCorrect : { false, true })
	{
		FDreamUICopyMeshRegionPS::FPermutationDomain Permutation;
		Permutation.Set<FDreamUICopyMeshRegionPS::FColorCorrect>(bColorCorrect);
		if (!Collector.Find<FDreamUICopyMeshRegionVS>().IsValid() || !Collector.Find<FDreamUICopyMeshRegionPS>(Permutation).IsValid())
		{
			continue;
		}
		FGraphicsPipelineStateInitializer Init;
		InitCopyMeshRegion(Init, ShaderMap, bColorCorrect);
		Collector.Add(Init, Collector.AnyTargets, EPSOPrecachePriority::Medium);
	}

	// The built-in shader (FDreamUIRenderer::DrawBuiltInBatch): each permutation it draws with, each blend mode as the
	// draw resolves it, with and without a layer's depth test. Helper geometry's plain-colour permutation is the gizmo's.
	const TShaderRef<FDreamUIBaseVS> BaseVertexShader = Collector.Find<FDreamUIBaseVS>();
	for (int32 Bits = 0; Bits < 8 && BaseVertexShader.IsValid(); ++Bits)
	{
		const bool bBlendDepth = (Bits & 1) != 0;
		const bool bDepthFade = (Bits & 2) != 0;
		const bool bBackdrop = (Bits & 4) != 0;
		FDreamUIBasePS::FPermutationDomain Permutation;
		Permutation.Set<FDreamUIBasePS::FBlendDepth>(bBlendDepth);
		Permutation.Set<FDreamUIBasePS::FDepthFade>(bDepthFade);
		Permutation.Set<FDreamUIBasePS::FBackdrop>(bBackdrop);
		const TShaderRef<FDreamUIBasePS> PixelShader = Collector.Find<FDreamUIBasePS>(Permutation);
		if (!PixelShader.IsValid())
		{
			continue;
		}
		for (int32 Mode = 0; Mode < DreamUIBlend::NumModes; ++Mode)
		{
			const EDreamUIBlendMode Asked = static_cast<EDreamUIBlendMode>(Mode);
			// A backdrop is given only to a mode that reads one; such a mode given none draws as Alpha.
			if (bBackdrop && !DreamUIBlend::ReadsBackdrop(Asked))
			{
				continue;
			}
			const EDreamUIBlendMode Drawn = DreamUIBlend::ReadsBackdrop(Asked) && !bBackdrop ? EDreamUIBlendMode::Alpha : Asked;
			const EPSOPrecachePriority Priority = Bits == 0 && Drawn == EDreamUIBlendMode::Alpha ? EPSOPrecachePriority::High : EPSOPrecachePriority::Low;
			for (const bool bIsDepthValid : { false, true })
			{
				FGraphicsPipelineStateInitializer Init;
				InitBuiltIn(Init, Collector.FeatureLevel, BaseVertexShader.GetVertexShader(), PixelShader.GetPixelShader(), Drawn, bIsDepthValid, false);
				if (bIsDepthValid)
				{
					Collector.Add(Init, Collector.DepthTestedTargets, EPSOPrecachePriority::Low, 1, &Collector.DepthTarget);
				}
				else
				{
					Collector.Add(Init, Collector.AnyTargets, Priority);
				}
			}
		}
	}

	// The MSAA resolve, from each sample count it has a shader for into the single-sampled copy of the target.
	for (const uint32 Samples : { 2u, 4u, 8u })
	{
		FDreamUIResolveShaderPS::FPermutationDomain Permutation;
		Permutation.Set<FDreamUIResolveShaderPS::FSampleCount>(Samples);
		if (!Collector.Find<FDreamUIResolveShaderVS>().IsValid() || !Collector.Find<FDreamUIResolveShaderPS>(Permutation).IsValid())
		{
			continue;
		}
		FGraphicsPipelineStateInitializer Init;
		InitResolve(Init, ShaderMap, Samples);
		Collector.Add(Init, Collector.OffscreenTargets, EPSOPrecachePriority::Medium);
	}

#if WITH_EDITOR
	FDreamUIBasePS::FPermutationDomain PlainColor;
	PlainColor.Set<FDreamUIBasePS::FPlainColor>(true);
	if (BaseVertexShader.IsValid() && Collector.Find<FDreamUIBasePS>(PlainColor).IsValid())
	{
		for (const EPrimitiveType Primitive : { PT_LineList, PT_TriangleList })
		{
			FGraphicsPipelineStateInitializer Init;
			InitGizmo(Init, Collector.FeatureLevel, ShaderMap, Primitive);
			Collector.Add(Init, Collector.CanvasTargets, EPSOPrecachePriority::Low);
		}
	}
#endif
}
