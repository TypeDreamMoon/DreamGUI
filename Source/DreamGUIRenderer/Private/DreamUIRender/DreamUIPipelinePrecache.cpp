// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "DreamUIRender/DreamUIPipelines.h"
#include "CommonRenderResources.h"
#include "DreamUIRender/DreamUIRendererLogging.h"
#include "DynamicRHI.h"
#include "MeshPassProcessor.h"

/**
 * DreamGUI's own pipelines, compiled on the PSO precache's worker threads instead of on the frame that first draws with
 * them: all of them for a target, the first time DreamGUI draws into a target of that format and sample count.
 *
 * The engine precaches the pipelines of global shaders only through the collectors registered with
 * FGlobalPSOCollectorManager, and only where FPSOPrecacheSettings::GlobalShaderMode is not 0 -- which BaseEngine.ini
 * sets for Vulkan and OpenGL ES on Android and Metal on iOS: on a phone a registered collector is never called. So
 * DreamGUI gathers its own (DreamUIPipelines::Collect*) and hands them to RequestPrecachePSOs itself, on every platform.
 * It does so when it first draws into a target rather than at startup because what it draws into is only known then: on
 * a phone without mobile HDR the view is the back buffer's format, multisampled, which nothing at startup says
 * (FSceneTexturesConfig there gives the HDR scene colour, single-sampled).
 *
 * What it buys on a phone: the pipeline the archive screen of the demo first drew with took 3.2 s of a Vulkan driver's
 * compile on the frame after the screen appeared, every launch, because the engine keeps a pipeline it compiled at
 * draw time only once enough of them fill a chunk of its cache (r.Vulkan.ChunkedPSOCache.MaxSingleCachePSOCount).
 * Precached from the title screen on, the archive finds it compiled, and the precache's pipelines fill those chunks, so
 * the next launch reads them from disk instead of compiling them again.
 */
static TAutoConsoleVariable<int32> CVarDreamGUIPSOPrecache(
	TEXT("dreamgui.PSOPrecache"), 1,
	TEXT("Compile DreamGUI's own graphics pipelines on the PSO precache's worker threads, where the RHI precaches PSOs: all of them\n")
	TEXT("for a target the first time DreamGUI draws into one of its format and sample count.\n")
	TEXT("0: off, each pipeline is compiled by the first frame that draws with it. 1: on (default). 2: on, and log what it compiles for."),
	ECVF_RenderThreadSafe);

void DreamUIPipelines::FCollector::Add(const FGraphicsPipelineStateInitializer& Init, TConstArrayView<FTarget> InTargets, EPSOPrecachePriority Priority
	, int32 InNumTargets, const FDepthTarget* InDepth)
{
	if (Init.BoundShaderState.GetVertexShader() == nullptr || Init.BoundShaderState.GetPixelShader() == nullptr)
	{
		return;
	}
	check(InNumTargets >= 1 && InNumTargets <= MaxSimultaneousRenderTargets);
	for (const FTarget& Target : InTargets)
	{
		FGraphicsPipelineStateInitializer Pipeline = Init;
		FGraphicsPipelineRenderTargetsInfo Targets;
		for (int32 Index = 0; Index < InNumTargets; ++Index)
		{
			AddRenderTargetInfo(Target.Format, Target.Flags, Targets);
		}
		Targets.NumSamples = Target.NumSamples;
		if (InDepth != nullptr)
		{
			SetupDepthStencilInfo(InDepth->Format, InDepth->Flags, ERenderTargetLoadAction::ELoad, ERenderTargetLoadAction::ELoad
				, FExclusiveDepthStencil::DepthWrite_StencilWrite, Targets);
		}
		// The state's hash first, without the targets, as the engine's own collectors do (see SlateGlobalPSOCollector).
		Pipeline.StatePrecachePSOHash = RHIComputeStatePrecachePSOHash(Pipeline);
		ApplyTargetsInfo(Pipeline, Targets);

		bool bAlready = false;
		Seen.Add(RHIComputePrecachePSOHash(Pipeline), &bAlready);
		if (bAlready)
		{
			continue;
		}
		FPSOPrecacheData& Data = Initializers.AddDefaulted_GetRef();
		Data.Type = FPSOPrecacheData::EType::Graphics;
		Data.bRequired = true;
		Data.Priority = Priority;
		Data.GraphicsPSOInitializer = Pipeline;
#if PSO_PRECACHING_VALIDATE
		Data.ResourceName = TEXT("DreamGUI");
#endif
	}
}

void DreamUIPipelines::InitFullscreen(FGraphicsPipelineStateInitializer& Init, const FGlobalShaderMap* ShaderMap, const TShaderRef<FShader>& InPixelShader)
{
	TShaderMapRef<FScreenVertexShaderVS> VertexShader(ShaderMap);
	Init.BlendState = TStaticBlendState<>::GetRHI();
	Init.RasterizerState = TStaticRasterizerState<>::GetRHI();
	Init.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
	Init.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
	Init.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
	Init.BoundShaderState.PixelShaderRHI = InPixelShader.GetPixelShader();
	Init.PrimitiveType = PT_TriangleList;
}

namespace DreamUIPipelinePrecacheLocal
{
	void AddUnique(TArray<DreamUIPipelines::FTarget>& InOutTargets, EPixelFormat Format, ETextureCreateFlags Flags, uint32 NumSamples)
	{
		if (Format == PF_Unknown)
		{
			return;
		}
		DreamUIPipelines::FTarget Target;
		Target.Format = Format;
		Target.Flags = Flags & FGraphicsPipelineStateInitializer::RelevantRenderTargetFlagMask;
		Target.NumSamples = static_cast<uint16>(FMath::Max(NumSamples, 1u));
		InOutTargets.AddUnique(Target);
	}

	/**
	 * What DreamGUI draws into when its canvas's target is InCanvas: the target itself, at its sample count, and, single-
	 * sampled, its format, which is what the resolve, a copy and an effect's work textures are in, and the format a group
	 * over it is kept in (DreamUIPipelines::GroupFormatOf), each with the target's sRGB flag.
	 */
	void FillTargets(DreamUIPipelines::FCollector& Collector, const DreamUIPipelines::FTarget& InCanvas)
	{
		TArray<DreamUIPipelines::FTarget>& Canvas = Collector.CanvasTargets;
		AddUnique(Canvas, InCanvas.Format, InCanvas.Flags, InCanvas.NumSamples);
		AddUnique(Collector.OffscreenTargets, InCanvas.Format, InCanvas.Flags, 1);
		AddUnique(Collector.OffscreenTargets, DreamUIPipelines::GroupFormatOf(InCanvas.Format), InCanvas.Flags, 1);
		Collector.AnyTargets = Canvas;
		for (const DreamUIPipelines::FTarget& Target : Collector.OffscreenTargets)
		{
			Collector.AnyTargets.AddUnique(Target);
		}
		// What FDreamUIRenderer::RenderDreamUI_RenderThread creates for a layer with depth test, at the canvas's sample count.
		Collector.DepthTarget.Format = PF_DepthStencil;
		Collector.DepthTarget.Flags = TexCreate_DepthStencilTargetable | TexCreate_Memoryless | (GRHISupportsDepthUAV ? TexCreate_UAV : TexCreate_None);
		Collector.DepthTestedTargets = Canvas;
	}
}

void DreamUIPipelines::PrecacheForCanvas(ERHIFeatureLevel::Type FeatureLevel, EPixelFormat Format, ETextureCreateFlags Flags, uint32 NumSamples, bool bHDROutput)
{
	check(IsInRenderingThread());
	const int32 Mode = CVarDreamGUIPSOPrecache.GetValueOnRenderThread();
	if (Mode <= 0 || Format == PF_Unknown || !GRHISupportsPSOPrecaching || !PipelineStateCache::IsPSOPrecachingEnabled())
	{
		return;
	}
	FTarget Canvas;
	Canvas.Format = Format;
	Canvas.Flags = Flags & FGraphicsPipelineStateInitializer::RelevantRenderTargetFlagMask;
	Canvas.NumSamples = static_cast<uint16>(FMath::Max(NumSamples, 1u));
	// Once for each target DreamGUI draws into, for as long as the process runs: what was asked for is the precache's.
	static TSet<uint64> Done;
	const uint64 Key = static_cast<uint64>(Canvas.Format) | (static_cast<uint64>(Canvas.Flags) << 8) | (static_cast<uint64>(Canvas.NumSamples) << 40)
		| (static_cast<uint64>(bHDROutput ? 1 : 0) << 56) | (static_cast<uint64>(FeatureLevel) << 57);
	bool bAlready = false;
	Done.Add(Key, &bAlready);
	if (bAlready)
	{
		return;
	}
	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(FeatureLevel);
	if (ShaderMap == nullptr)
	{
		return;
	}
	const double Start = FPlatformTime::Seconds();
	FCollector Collector(ShaderMap, FeatureLevel);
	DreamUIPipelinePrecacheLocal::FillTargets(Collector, Canvas);

	CollectRenderer(Collector);
	CollectCompositor(Collector);
	CollectVisualPostProcess(Collector);
	CollectBackdropEffects(Collector);
	CollectBackgroundBlur(Collector);
	CollectPixelSort(Collector);
	CollectLayerStyle(Collector);
	if (bHDROutput)
	{
		CollectHDR(Collector);
	}

	RequestPrecachePSOs(EPSOPrecacheType::Global, Collector.GetInitializers());

	int32 NumHigh = 0;
	for (const FPSOPrecacheData& Data : Collector.GetInitializers())
	{
		NumHigh += Data.Priority >= EPSOPrecachePriority::High ? 1 : 0;
	}
	UE_LOG(LogDreamGUIRenderer, Log, TEXT("Precaching %d DreamGUI graphics pipelines (%d first) for a target of %s%s, %d sample(s), gathered in %.2f ms."),
		Collector.GetInitializers().Num(), NumHigh, GetPixelFormatString(Canvas.Format), EnumHasAnyFlags(Canvas.Flags, ETextureCreateFlags::SRGB) ? TEXT(" sRGB") : TEXT("")
		, Canvas.NumSamples, (FPlatformTime::Seconds() - Start) * 1000.0);
	if (Mode >= 2)
	{
		for (const FTarget& Target : Collector.AnyTargets)
		{
			UE_LOG(LogDreamGUIRenderer, Log, TEXT("  into %s%s, %d sample(s)"), GetPixelFormatString(Target.Format)
				, EnumHasAnyFlags(Target.Flags, ETextureCreateFlags::SRGB) ? TEXT(" sRGB") : TEXT(""), Target.NumSamples);
		}
	}
}
