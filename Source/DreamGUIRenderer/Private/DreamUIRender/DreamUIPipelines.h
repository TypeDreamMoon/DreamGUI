// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GlobalShader.h"
#include "PipelineStateCache.h"
#include "PixelShaderUtils.h"
#include "PSOPrecache.h"
#include "RenderGraphBuilder.h"
#include "RHIResources.h"
#include "Shader.h"
#include "ShaderParameterStruct.h"

enum class EDreamUIBlendMode : uint8;

/**
 * Every graphics pipeline DreamGUI's own shaders draw with, built in one place for two callers: the draw, which sets
 * the targets it has bound (ApplyCachedRenderTargets) and then calls the Init function of its pipeline, and the precache
 * (DreamUIPipelinePrecache.cpp), which calls the same Init function for each combination a draw could ask for and adds
 * the targets it could be drawn into. A pipeline the precache built differently from the draw is one the draw compiles
 * the first time it needs it, which on a phone's GPU driver can take seconds; building both from one function is what
 * keeps them the same. A material's pipelines are not here: they are the material's, and so is precaching them.
 */
namespace DreamUIPipelines
{
	/** A target a pipeline can be drawn into: what of a texture a pipeline is compiled for. */
	struct FTarget
	{
		EPixelFormat Format = PF_Unknown;
		/** Only SRGB counts (FGraphicsPipelineStateInitializer::RelevantRenderTargetFlagMask). */
		ETextureCreateFlags Flags = ETextureCreateFlags::None;
		uint16 NumSamples = 1;

		bool operator==(const FTarget& Other) const
		{
			return Format == Other.Format && Flags == Other.Flags && NumSamples == Other.NumSamples;
		}
	};

	/** The depth a screen-space layer with depth test binds under its canvas (FDreamUIRenderer::RenderDreamUI_RenderThread). */
	struct FDepthTarget
	{
		EPixelFormat Format = PF_Unknown;
		ETextureCreateFlags Flags = ETextureCreateFlags::None;
	};

	/** What the precache gathers, and the targets it gathers them for. Filled by DreamUIPipelines::Collect. */
	class FCollector
	{
	public:
		FCollector(FGlobalShaderMap* InShaderMap, ERHIFeatureLevel::Type InFeatureLevel)
			: ShaderMap(InShaderMap), FeatureLevel(InFeatureLevel)
		{
		}

		FGlobalShaderMap* const ShaderMap;
		const ERHIFeatureLevel::Type FeatureLevel;
		/** What a canvas draws into directly: the scene colour at its sample count, a render target, the view after tonemapping. */
		TArray<FTarget> CanvasTargets;
		/** Every format of CanvasTargets, single-sampled: what a group, an effect, a copy is drawn into. */
		TArray<FTarget> OffscreenTargets;
		/** CanvasTargets and OffscreenTargets: for a draw that can go into either. */
		TArray<FTarget> AnyTargets;
		/** Each of CanvasTargets that a screen-space layer with depth test can draw into, with that depth under it. */
		TArray<FTarget> DepthTestedTargets;
		FDepthTarget DepthTarget;

		/** The shader, or a null one where this platform does not have that permutation. */
		template<typename ShaderType>
		TShaderRef<ShaderType> Find(const typename ShaderType::FPermutationDomain& Permutation) const
		{
			return TShaderRef<ShaderType>::Cast(ShaderMap->GetShader(&ShaderType::GetStaticType(), Permutation.ToDimensionValueId()));
		}
		template<typename ShaderType>
		TShaderRef<ShaderType> Find() const
		{
			return TShaderRef<ShaderType>::Cast(ShaderMap->GetShader(&ShaderType::GetStaticType(), 0));
		}

		/**
		 * The pipeline of Init (which Init functions filled) once for each of InTargets, InNumTargets of it bound at once,
		 * and InDepth under it where given. A pipeline with a shader missing is skipped.
		 */
		void Add(const FGraphicsPipelineStateInitializer& Init, TConstArrayView<FTarget> InTargets, EPSOPrecachePriority Priority
			, int32 InNumTargets = 1, const FDepthTarget* InDepth = nullptr);
		void Add(const FGraphicsPipelineStateInitializer& Init, const FTarget& InTarget, EPSOPrecachePriority Priority, int32 InNumTargets = 1)
		{
			Add(Init, MakeArrayView(&InTarget, 1), Priority, InNumTargets);
		}

		const TArray<FPSOPrecacheData>& GetInitializers() const { return Initializers; }

	private:
		TArray<FPSOPrecacheData> Initializers;
		TSet<uint64> Seen;
	};

	/**
	 * FPixelShaderUtils::InitFullscreenPipelineState without the targets: the engine's full-screen triangle, no blend,
	 * no depth, and InPixelShader.
	 */
	void InitFullscreen(FGraphicsPipelineStateInitializer& Init, const FGlobalShaderMap* ShaderMap, const TShaderRef<FShader>& InPixelShader);

	/**
	 * FPixelShaderUtils::AddFullscreenPass, its pipeline built by InitFullscreen, so that every pipeline DreamGUI's own
	 * shaders draw with is one this file can precache.
	 */
	template<typename TShaderClass>
	void AddFullscreenPass(FRDGBuilder& GraphBuilder, const FGlobalShaderMap* ShaderMap, FRDGEventName&& PassName, const TShaderRef<TShaderClass>& PixelShader
		, typename TShaderClass::FParameters* Parameters, const FIntRect& Viewport);

	// The Init functions, each defined beside the draw that uses it. Each fills every part of the pipeline but the targets.

	/** FDreamUIRenderer::AddCopyTargetPass's. */
	void InitCopyTarget(FGraphicsPipelineStateInitializer& Init, FGlobalShaderMap* ShaderMap, bool bColorCorrect, bool bBlendAlpha, bool bPremultipliedAlpha);
	/** FDreamUIRenderer::CopyRenderTargetOnMeshRegion's. */
	void InitCopyMeshRegion(FGraphicsPipelineStateInitializer& Init, FGlobalShaderMap* ShaderMap, bool bColorCorrect);
	/** The built-in shader's (FDreamUIRenderer::DrawBuiltInBatch); its shaders are the draw's, which caches them. */
	void InitBuiltIn(FGraphicsPipelineStateInitializer& Init, ERHIFeatureLevel::Type FeatureLevel, FRHIVertexShader* InVertexShader, FRHIPixelShader* InPixelShader
		, EDreamUIBlendMode BlendMode, bool bIsDepthValid, bool bReverseCulling);
	/** The MSAA resolve's (FDreamUIRenderer's DreamUIResolveColor pass): InNumSamples is the source's, 2, 4 or 8. */
	void InitResolve(FGraphicsPipelineStateInitializer& Init, FGlobalShaderMap* ShaderMap, uint32 InNumSamples);
	/** A group composited onto what is under it (DreamUICompositor.cpp). */
	void InitComposite(FGraphicsPipelineStateInitializer& Init, FGlobalShaderMap* ShaderMap, bool bBackdrop, bool bMask, bool bExternal, EDreamUIBlendMode BlendMode);
	/** A visual post process's result drawn onto its canvas (DreamVisualPostProcessRenderProxy.cpp). */
	void InitRenderMesh(FGraphicsPipelineStateInitializer& Init, FGlobalShaderMap* ShaderMap, bool bMask, bool bWorldSpace, bool bDepthFade);
	/** A backdrop effect composited over what is behind it (DreamUIBackdropEffects.cpp). */
	void InitBackdropComposite(FGraphicsPipelineStateInitializer& Init, FGlobalShaderMap* ShaderMap, int32 Effect, bool bWorldSpace, bool bMask, bool bGlassShape);
	/**
	 * A full-screen pass of DreamGUI's post-process quad (FDreamUISimplePostProcessVS) with InPixelShader, no blend: the
	 * background blur's two passes (DreamUIBackgroundBlurRenderProxy.cpp) and the pixel sort's (DreamUIPixelSortRenderProxy.cpp).
	 */
	void InitPostProcessQuad(FGraphicsPipelineStateInitializer& Init, FRHIVertexShader* InVertexShader, FRHIPixelShader* InPixelShader);
#if WITH_EDITOR
	/** The editor's helper geometry (FDreamUIRenderer::RenderGizmoMesh_RenderThread). */
	void InitGizmo(FGraphicsPipelineStateInitializer& Init, ERHIFeatureLevel::Type FeatureLevel, FGlobalShaderMap* ShaderMap, EPrimitiveType PrimitiveType);
#endif

	/**
	 * Precaches every pipeline the Collect functions gather for a canvas whose target is of Format, Flags and NumSamples,
	 * the first time it is called for that target (dreamgui.PSOPrecache). Render thread; cheap after the first call.
	 */
	void PrecacheForCanvas(ERHIFeatureLevel::Type FeatureLevel, EPixelFormat Format, ETextureCreateFlags Flags, uint32 NumSamples, bool bHDROutput);

	/** The format a group's texture is kept in over a target of InTarget (DreamUICompositor.cpp). */
	EPixelFormat GroupFormatOf(EPixelFormat InTarget);

	// The Collect functions, each beside the Init functions it calls: every combination its draws can ask for.

	void CollectRenderer(FCollector& Collector);
	void CollectCompositor(FCollector& Collector);
	void CollectVisualPostProcess(FCollector& Collector);
	void CollectBackdropEffects(FCollector& Collector);
	void CollectBackgroundBlur(FCollector& Collector);
	void CollectPixelSort(FCollector& Collector);
	void CollectLayerStyle(FCollector& Collector);
	void CollectHDR(FCollector& Collector);
}

template<typename TShaderClass>
void DreamUIPipelines::AddFullscreenPass(FRDGBuilder& GraphBuilder, const FGlobalShaderMap* ShaderMap, FRDGEventName&& PassName, const TShaderRef<TShaderClass>& PixelShader
	, typename TShaderClass::FParameters* Parameters, const FIntRect& Viewport)
{
	check(PixelShader.IsValid());
	ClearUnusedGraphResources(PixelShader, Parameters);
	GraphBuilder.AddPass(
		Forward<FRDGEventName>(PassName),
		Parameters,
		ERDGPassFlags::Raster,
		[Parameters, ShaderMap, PixelShader, Viewport](FRDGAsyncTask, FRHICommandList& RHICmdList)
		{
			RHICmdList.SetViewport(static_cast<float>(Viewport.Min.X), static_cast<float>(Viewport.Min.Y), 0.0f
				, static_cast<float>(Viewport.Max.X), static_cast<float>(Viewport.Max.Y), 1.0f);
			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
			InitFullscreen(GraphicsPSOInit, ShaderMap, PixelShader);
			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0);
			SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), *Parameters);
			FPixelShaderUtils::DrawFullscreenTriangle(RHICmdList);
		});
}
