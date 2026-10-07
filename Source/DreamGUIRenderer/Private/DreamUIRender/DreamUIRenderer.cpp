// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "DreamUIRender/DreamUIRenderer.h"
#include "DreamUIRender/DreamUIShaders.h"
#include "DreamUIRender/DreamUIBaseShaders.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialRenderProxy.h"
#include "DreamUIRender/DreamUIPostProcessShaders.h"
#include "DreamUIRender/DreamUIResolveShaders.h"
#include "DreamUIRender/DreamUIRendererLogging.h"
#include "DreamUIRender/DreamUIRenderStats.h"
#include "SceneView.h"
#include "PipelineStateCache.h"
#include "RenderTargetPool.h"//UE5.8: GRenderTargetPool no longer transitively included
#include "DreamUIRender/IDreamUIRendererPrimitive.h"
#include "TextureResource.h"
#include "Engine/TextureRenderTarget2D.h"
#include "DreamUIRender/DreamVisualPostProcessRenderProxy.h"
#include "FXRenderingUtils.h"
#include "SceneRenderTargetParameters.h"
#include "SystemTextures.h"
#if WITH_EDITOR
#include "Engine/Engine.h"
#include "ScreenPass.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#endif
#include "DreamUIRender/DreamUIRendererSettings.h"
#include "ClearQuad.h"
#include "DataDrivenShaderPlatformInfo.h"//RHISupportsMSAA, for the platform that cannot honour the setting

static TAutoConsoleVariable<int32> CVarDreamGUIDumpMaterialDraws(
	TEXT("dreamgui.DumpMaterialDraws"), 0,
	TEXT("1: log one line per screen-space material draw attempt (which branch/exit it took). Stays on until set back to 0."),
	ECVF_RenderThreadSafe);
#include "RHIResourceUtils.h"
#include "DreamUIRender/DreamUIMeshVertex.h"
#include "DreamUIRender/DreamUIGizmoMesh.h"
#include "DreamUIRender/DreamUIPostProcessVertex.h"

DEFINE_LOG_CATEGORY(LogDreamGUIRenderer);

namespace DreamUIRendererLocal
{
	/**
	 * The renderer, for a render command to hold until it has run. NewExtension makes a renderer with MakeShareable, so
	 * the last strong reference deletes it there and then, and the game thread lets go of a world's renderer when the
	 * world is torn down -- a test world is built, drawn into and torn down within one frame. A command holding a bare
	 * pointer then ran on the render thread against the freed renderer, writing into whatever had been allocated there
	 * since.
	 */
	TSharedRef<FDreamUIRenderer, ESPMode::ThreadSafe> HoldForRenderThread(FDreamUIRenderer& InRenderer)
	{
		return StaticCastSharedRef<FDreamUIRenderer>(InRenderer.AsShared());
	}
}

BEGIN_SHADER_PARAMETER_STRUCT(FDreamUITextureReadRenderTargetParameters, )
	RDG_TEXTURE_ACCESS(SourceTexture, ERHIAccess::SRVGraphics)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()


#if WITH_EDITORONLY_DATA
#endif
FDreamUIRenderer::FDreamUIRenderer(const FAutoRegister& AutoRegister, UWorld* InWorld, EDreamUIRendererType InRendererType)
	:FSceneViewExtensionBase(AutoRegister)
{
	World = InWorld;
	RendererType = InRendererType;

#if WITH_EDITORONLY_DATA
	bIsEditorPreview = !World->IsGameWorld();
	bIsLevelEditorWorld = World->WorldType == EWorldType::Editor;
#endif
}
FDreamUIRenderer::~FDreamUIRenderer()
{
	
}

FIntPoint FDreamUIRenderer::CalculateRenderScaledSize(const FIntPoint& InViewportSize, float InRequestedScale, float& OutAppliedScale)
{
	const FIntPoint ClampedViewport(FMath::Max(InViewportSize.X, 1), FMath::Max(InViewportSize.Y, 1));
	//1 means "leave it alone", and it has to mean that exactly: rounding a full-size pass through the
	//arithmetic below could come back one pixel short and quietly make every UI a rescale
	const float RequestedScale = FMath::Clamp(InRequestedScale, 0.1f, 1.0f);
	if (RequestedScale >= 1.0f)
	{
		OutAppliedScale = 1.0f;
		return ClampedViewport;
	}
	const FIntPoint ScaledSize(
		FMath::Max(FMath::RoundToInt(ClampedViewport.X * RequestedScale), 1),
		FMath::Max(FMath::RoundToInt(ClampedViewport.Y * RequestedScale), 1));
	//report what was actually rendered at, not what was asked for: the pixel rounding and the
	//one-pixel floor both move it, and the upscale has to use the size that exists
	OutAppliedScale = (float)ScaledSize.X / (float)ClampedViewport.X;
	return ScaledSize;
}

void FDreamUIRenderer::SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView)
{
	if (!World.IsValid())return;
	if (World.Get() != InView.Family->Scene->GetWorld())return;
	UpdateViewParameter_GameThread();
}

void FDreamUIRenderer::MakeLayerView(const IDreamUIRendererViewSource& InSource, FScreenSpaceLayerView& OutView)
{
	const FVector ViewLocation = InSource.GetRendererViewLocation();
	const FMatrix ViewRotationMatrix = FInverseRotationMatrix(InSource.GetRendererViewRotator()) * FMatrix(
		FPlane(0, 0, 1, 0),
		FPlane(1, 0, 0, 0),
		FPlane(0, 1, 0, 0),
		FPlane(0, 0, 0, 1));
	const FMatrix ProjectionMatrix = InSource.GetRendererProjectionMatrix();
	OutView.ViewOrigin = ViewLocation;
	OutView.ViewRotationMatrix = ViewRotationMatrix;
	OutView.ProjectionMatrix = ProjectionMatrix;
	OutView.ViewProjectionMatrix = FMatrix44f(FTranslationMatrix(-ViewLocation) * ViewRotationMatrix * ProjectionMatrix);
	OutView.bEnableDepthTest = InSource.GetRendererEnableDepthTest();
	//read here with the rest of the view state, from the same canvas, so the render thread never
	//asks the canvas anything
	OutView.ScreenSpaceRenderScale = InSource.GetRendererScreenSpaceRenderScale();
}

void FDreamUIRenderer::UpdateViewParameter_GameThread()
{
	if (const FScreenSpaceRoot* ViewRoot = GetScreenSpaceViewRoot())
	{
		FScreenSpaceLayerView SharedView;
		MakeLayerView(*ViewRoot->ViewSource, SharedView);
		GameThreadViewParameter.ViewOrigin = SharedView.ViewOrigin;
		GameThreadViewParameter.ViewRotationMatrix = SharedView.ViewRotationMatrix;
		GameThreadViewParameter.ProjectionMatrix = SharedView.ProjectionMatrix;
		GameThreadViewParameter.ViewProjectionMatrix = SharedView.ViewProjectionMatrix;
		GameThreadViewParameter.bEnableDepthTest = SharedView.bEnableDepthTest;
		GameThreadViewParameter.ScreenSpaceRenderScale = SharedView.ScreenSpaceRenderScale;
	}
	// Every root that fills a player's part of a split screen, with a view of its own: a part is its own size, so its
	// projection is not the shared one, and it is drawn in its player's view only. None in a game that is not split.
	GameThreadViewParameter.PlayerParts.Reset();
	for (const FScreenSpaceRoot& Root : ScreenSpaceRenderParameter.RootCanvasArray)
	{
		if (!Root.Canvas.IsValid() || Root.ViewSource == nullptr)
		{
			continue;
		}
		const int32 ViewPlayerIndex = Root.ViewSource->GetRendererViewPlayerIndex();
		if (ViewPlayerIndex == INDEX_NONE)
		{
			continue;
		}
		FScreenSpacePlayerPartView& Part = GameThreadViewParameter.PlayerParts.AddDefaulted_GetRef();
		Part.RootKey = FObjectKey(Root.Canvas.Get());
		Part.ViewPlayerIndex = ViewPlayerIndex;
		MakeLayerView(*Root.ViewSource, Part.View);
	}

	// The project's settings as the core provides them (DreamUIRendererSettings): read here, once per view,
	// as they always were, so an edit to them takes effect on the next frame.
	if (DreamUIRendererSettings::HasProvider())
	{
		const FDreamUIRendererSettings RendererSettings = DreamUIRendererSettings::Get();
		uint8 RequestedSamples = RendererSettings.MSAASampleCount;
		// The setting's own documentation says MSAA is "not valid on Android (gles)", and until now
		// nothing enforced that: it is a plain config value, so a project that turns MSAA on globally
		// (which is the ordinary thing to do) carried it onto a platform that cannot honour it and got
		// whatever the unsupported sample count did, with nothing said. Asking the RHI instead of
		// naming a platform covers every platform that answers no, and the fallback is the same path
		// the None setting takes. Said once, because this runs per frame.
		if (RequestedSamples > 1
			&& GMaxRHIShaderPlatform < EShaderPlatform::SP_NumPlatforms
			&& FDataDrivenShaderPlatformInfo::IsValid(GMaxRHIShaderPlatform)
			&& !RHISupportsMSAA(GMaxRHIShaderPlatform))
		{
			static bool bWarnedAboutUnsupportedMSAA = false;
			if (!bWarnedAboutUnsupportedMSAA)
			{
				bWarnedAboutUnsupportedMSAA = true;
				UE_LOG(LogDreamGUIRenderer, Warning, TEXT("[%s].%d DreamUI MSAA x%d was requested, but this shader platform does not support MSAA; falling back to no anti-aliasing. Override AntiAliasingMethod for this platform to make the choice explicit."),
					ANSI_TO_TCHAR(__FUNCTION__), __LINE__, RequestedSamples);
			}
			RequestedSamples = 1;
		}
		GameThreadViewParameter.NumSamples_MSAA = RequestedSamples;
		GameThreadViewParameter.bFrustumCulling = RendererSettings.bFrustumCulling;
	}
	else
	{
		GameThreadViewParameter.NumSamples_MSAA = 1;
	}

#if WITH_EDITOR
	GameThreadViewParameter.bCanRenderScreenSpace = bCanRenderScreenSpace;
	GameThreadViewParameter.bIsPlaying = bIsPlaying;
#endif

	// Hand the whole thing over by value. The render thread reads these while this function is writing
	// them, which is what the @todo that used to sit above asked for.
	ENQUEUE_RENDER_COMMAND(FDreamUIRender_SetScreenSpaceViewParameter)(
		[ViewExtension = DreamUIRendererLocal::HoldForRenderThread(*this), Parameter = GameThreadViewParameter](FRHICommandListImmediate& RHICmdList)
		{
			ViewExtension->RenderThreadViewParameter = Parameter;
		}
	);
}
void FDreamUIRenderer::SetupViewPoint(APlayerController* Player, FMinimalViewInfo& InViewInfo)
{

}
void FDreamUIRenderer::SetupViewProjectionMatrix(FSceneViewProjectionData& InOutProjectionData)
{
	
}
void FDreamUIRenderer::BeginRenderViewFamily(FSceneViewFamily& InViewFamily)
{
#if WITH_EDITOR
	SubmitGizmoMeshes();
#endif
}
void FDreamUIRenderer::PreRenderViewFamily_RenderThread(FRDGBuilder& GraphBuilder, FSceneViewFamily& InViewFamily)
{
#if WITH_EDITOR
	ViewsDrawnAfterTonemap.Reset();
#endif
}
void FDreamUIRenderer::PostRenderView_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& InView)
{
#if WITH_EDITOR
	if (ViewsDrawnAfterTonemap.RemoveSwap(&InView) > 0)
	{
		return;
	}
#endif
	RenderDreamUI_RenderThread(GraphBuilder, InView);
}
void FDreamUIRenderer::SubscribeToPostProcessingPass(EPostProcessingPass Pass, const FSceneView& InView, FAfterPassCallbackDelegateArray& InOutPassCallbacks, bool bIsPassEnabled)
{
#if WITH_EDITOR
	if (Pass == EPostProcessingPass::Tonemap && bIsPassEnabled && ShouldDrawBeforeEditorPrimitives_RenderThread(InView))
	{
		InOutPassCallbacks.Add(FAfterPassCallbackDelegate::CreateRaw(this, &FDreamUIRenderer::RenderAfterTonemap_RenderThread));
	}
#endif
}
#if WITH_EDITOR
bool FDreamUIRenderer::ShouldDrawBeforeEditorPrimitives_RenderThread(const FSceneView& InView) const
{
	// The level editor's views: the editor composites its gizmos and selection outline over the scene in the
	// post-process chain, after tonemapping, and a UI drawn once the view is finished went over them -- the
	// transform gizmo of a world-space panel's own actor disappeared behind the panel. Drawn straight after
	// tonemapping instead, the UI is part of the scene the gizmos are composited over.
	//
	// Nowhere else: a game view has no gizmos and keeps drawing last, at the output resolution. The level
	// editor never draws screen-space UI (it only does while playing), whose depth target is sized from the
	// family's render target and would not fit the texture drawn into here. And not for a world with nothing to
	// draw, which would pay for the two copies and draw nothing between them.
	return bIsLevelEditorWorld
		&& (WorldSpaceRenderCanvasParameterArray.Num() > 0 || WorldSpaceGizmoMeshArray.Num() > 0)
		&& RendererType == EDreamUIRendererType::ScreenSpace_and_WorldSpace
		&& InView.bIsViewInfo
		&& InView.Family->EngineShowFlags.CompositeEditorPrimitives
		&& InView.StereoPass == EStereoscopicPass::eSSP_FULL
		&& !(InView.bIsSceneCapture || InView.bIsReflectionCapture || InView.bIsPlanarReflection || InView.bIsVirtualTexture);
}

FScreenPassTexture FDreamUIRenderer::RenderAfterTonemap_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& InView, const FPostProcessMaterialInputs& Inputs)
{
	FScreenPassTexture SceneColor = FScreenPassTexture::CopyFromSlice(GraphBuilder, Inputs.GetInput(EPostProcessMaterialInput::SceneColor));
	const FIntPoint Size = SceneColor.ViewRect.Size();
	if (SceneColor.IsValid() && Size.X > 0 && Size.Y > 0)
	{
		// Drawn the way it is drawn after the view, into a texture that is the whole of the view: a pooled one of
		// the view's exact size, copied out of the scene colour and back. The post-process chain pools its
		// textures at sizes larger than the view, and a screen-reading effect (blur, pixelate) takes the whole
		// of the texture it is given for the view.
		TRefCountPtr<IPooledRenderTarget> Staging;
		const FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(Size, SceneColor.Texture->Desc.Format, FClearValueBinding::Black
			, TexCreate_None, TexCreate_RenderTargetable | TexCreate_ShaderResource, false));
		GRenderTargetPool.FindFreeElement(GraphBuilder.RHICmdList, Desc, Staging, TEXT("DreamUI_AfterTonemap"));
		if (Staging.IsValid())
		{
			// The pool element, registered, so the graph holds it until its passes have run (see PrepareTargets).
			const FRDGTextureRef StagingTexture = GraphBuilder.RegisterExternalTexture(Staging, TEXT("DreamUI_AfterTonemap"));
			AddCopyTexturePass(GraphBuilder, SceneColor.Texture, StagingTexture, SceneColor.ViewRect.Min, FIntPoint::ZeroValue, Size);
			// Only read through: every stage copies the view before changing anything of it.
			RenderDreamUI_RenderThread(GraphBuilder, const_cast<FSceneView&>(InView), Staging->GetRHI());
			AddCopyTexturePass(GraphBuilder, StagingTexture, SceneColor.Texture, FIntPoint::ZeroValue, SceneColor.ViewRect.Min, Size);
			ViewsDrawnAfterTonemap.AddUnique(&InView);
		}
	}
	if (Inputs.OverrideOutput.IsValid())
	{
		AddDrawTexturePass(GraphBuilder, InView, SceneColor, Inputs.OverrideOutput);
		return Inputs.OverrideOutput;
	}
	return SceneColor;
}
#endif
int32 FDreamUIRenderer::GetPriority() const
{
#if WITH_EDITOR
	auto Priority = DreamUIRendererSettings::Get().ViewExtensionPriority;
#else
	static auto Priority = DreamUIRendererSettings::Get().ViewExtensionPriority;
#endif
	return Priority;
}
#if WITH_EDITOR
namespace DreamUIRendererLocal
{
	TFunction<bool()>& SimulatingInEditorQuery()
	{
		static TFunction<bool()> Query;
		return Query;
	}
}

void FDreamUIRenderer::SetSimulatingInEditorQuery(TFunction<bool()> InQuery)
{
	check(IsInGameThread());
	DreamUIRendererLocal::SimulatingInEditorQuery() = MoveTemp(InQuery);
}
#endif

bool FDreamUIRenderer::IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const
{
	if (!World.IsValid())return false;
#if WITH_EDITOR
	if (GEngine == nullptr) return false;
	bCanRenderScreenSpace = true;
	bIsPlaying = World.Get()->IsGameWorld();
	//check if simulation: the editor engine is not the renderer's to link, so whoever does answers this
	if (DreamUIRendererLocal::SimulatingInEditorQuery() && DreamUIRendererLocal::SimulatingInEditorQuery()())
	{
		bCanRenderScreenSpace = false;
	}

	if (bIsPlaying == bIsEditorPreview)bCanRenderScreenSpace = false;
#endif

	if (World.Get() != Context.GetWorld())return false;//only render self world
	// A render-target canvas is drawn by DrawRenderTarget_GameThread, with a render command and a graph of its own, and
	// never inside its world's views.
	if (RendererType == EDreamUIRendererType::RenderTarget)return false;
	return true;
}

void FDreamUIRenderer::DrawRenderTarget_GameThread(UTextureRenderTarget2D* InRenderTarget, FColor InClearColor)
{
	check(IsInGameThread());
	if (RendererType != EDreamUIRendererType::RenderTarget || !World.IsValid()
		|| InRenderTarget == nullptr || InRenderTarget->GameThread_GetRenderTargetResource() == nullptr)
	{
		return;
	}
	// In order on the render thread: the view, the target, then the draw. The canvas's sections for this frame were
	// sent before this was called, so the draw sees them.
	UpdateViewParameter_GameThread();
	UpdateRenderTargetRenderer(InRenderTarget, InClearColor);
	const FGameTime Time = World->GetTime();
	ENQUEUE_RENDER_COMMAND(FDreamUIRender_DrawRenderTarget)(
		[Self = DreamUIRendererLocal::HoldForRenderThread(*this), InRenderTarget, Time](FRHICommandListImmediate& RHICmdList)
		{
			Self->DrawRenderTarget_RenderThread(RHICmdList, InRenderTarget, Time);
		});
}

void FDreamUIRenderer::DrawRenderTarget_RenderThread(FRHICommandListImmediate& RHICmdList, UTextureRenderTarget2D* InRenderTarget, const FGameTime& InTime)
{
	// The target is read here, by the command the game thread enqueued while it was alive, exactly as the command
	// before this one read it; nothing of it is kept past this call.
	FTextureRenderTargetResource* TargetResource = InRenderTarget->GetRenderTargetResource();
	if (TargetResource == nullptr || !CanvasTargetTexture.IsValid())
	{
		return;
	}
	const FIntPoint Size = TargetResource->GetSizeXY();
	if (Size.X <= 0 || Size.Y <= 0)
	{
		return;
	}
	// No scene: a view of the target alone, the way the engine's canvas draws its tiles, with the view the canvas
	// answers for. The scene renderer is not asked for anything, so a world nothing renders draws all the same.
	// A material's cached uniform expressions are brought up to date as a scene render starts, and this is none: those
	// asked for since -- a canvas's material proxy given new parameters -- are made here.
	if (FMaterialRenderProxy::HasDeferredUniformExpressionCacheRequests())
	{
		FMaterialRenderProxy::UpdateDeferredCachedUniformExpressions(RHICmdList);
	}
	FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("DreamUI_RenderTargetCanvas"));
	FSceneViewFamily* ViewFamily = GraphBuilder.AllocObject<FSceneViewFamily>(FSceneViewFamily::ConstructionValues(
		TargetResource, nullptr, FEngineShowFlags(ESFIM_Game)).SetTime(InTime));
	FSceneViewInitOptions ViewInitOptions;
	ViewInitOptions.ViewFamily = ViewFamily;
	ViewInitOptions.SetViewRectangle(FIntRect(FIntPoint::ZeroValue, Size));
	ViewInitOptions.ViewOrigin = RenderThreadViewParameter.ViewOrigin;
	ViewInitOptions.ViewRotationMatrix = RenderThreadViewParameter.ViewRotationMatrix;
	ViewInitOptions.ProjectionMatrix = RenderThreadViewParameter.ProjectionMatrix;
	ViewInitOptions.BackgroundColor = FLinearColor::Transparent;
	FSceneView* View = GraphBuilder.AllocObject<FSceneView>(ViewInitOptions);
	RenderDreamUI_RenderThread(GraphBuilder, *View);
	{
		// The canvas's draws: the passes recorded above run here, in its own graph. A canvas drawn into a view runs them
		// in the scene renderer's graph instead, where they are that graph's passes.
		TRACE_CPUPROFILER_EVENT_SCOPE(DreamUI_RenderTargetExecute);
		GraphBuilder.Execute();
	}
}
void FDreamUIRenderer::PreRenderView_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& InView)
{
	
}
void FDreamUIRenderer::PostRenderBasePassDeferred_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& InView, const FRenderTargetBindingSlots& RenderTargets, TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTextures)
{

}

namespace DreamUIRendererLocal
{
	/**
	 * The pass of the three CopyRenderTarget functions: Src over the whole of Dst, the colour linearized or the alpha
	 * scaled by BlendAlpha and blended over what Dst holds. Render scale supplies premultiplied RGB; other callers
	 * supply straight RGB. PassName names the pass in the graph.
	 */
	static void AddCopyTargetPass(FRDGBuilder& GraphBuilder, FGlobalShaderMap* GlobalShaderMap
		, FRDGTextureRef SourceTexture, FRDGTextureRef DestinationTexture, FRHISamplerState* SrcTextureSamplerState
		, bool bColorCorrect, bool bBlendAlpha, float BlendAlpha, const TCHAR* PassName, bool bPremultipliedAlpha = false)
	{
		auto* PassParameters = GraphBuilder.AllocParameters<FDreamUITextureReadRenderTargetParameters>();
		PassParameters->SourceTexture = SourceTexture;
		// A blended copy goes over what the target holds; any other covers all of it.
		PassParameters->RenderTargets[0] = FRenderTargetBinding(DestinationTexture, bBlendAlpha ? ERenderTargetLoadAction::ELoad : ERenderTargetLoadAction::ENoAction);
		GraphBuilder.AddPass(
			RDG_EVENT_NAME("%s", PassName),
			PassParameters,
			ERDGPassFlags::Raster,
			[GlobalShaderMap, SourceTexture, DestinationTexture, SrcTextureSamplerState, bColorCorrect, bBlendAlpha, BlendAlpha, bPremultipliedAlpha](FRHICommandListImmediate& RHICmdList)
			{
				SourceTexture->MarkResourceAsUsed();
				const FIntPoint DestinationExtent = DestinationTexture->Desc.Extent;
				RHICmdList.SetViewport(0, 0, 0, DestinationExtent.X, DestinationExtent.Y, 1.0f);

				TShaderMapRef<FDreamUISimplePostProcessVS> VertexShader(GlobalShaderMap);
				FDreamUISimpleCopyTargetPS::FPermutationDomain PermutationVector;
				PermutationVector.Set<FDreamUISimpleCopyTargetPS::FColorCorrect>(bColorCorrect);
				PermutationVector.Set<FDreamUISimpleCopyTargetPS::FBlendAlpha>(bBlendAlpha);
				TShaderMapRef<FDreamUISimpleCopyTargetPS> PixelShader(GlobalShaderMap, PermutationVector);
				FGraphicsPipelineStateInitializer GraphicsPSOInit;
				RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
				GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, ECompareFunction::CF_Always>::GetRHI();
				GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
				GraphicsPSOInit.BlendState = bBlendAlpha
					? (bPremultipliedAlpha
						? TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_InverseSourceAlpha, BO_Add, BF_One, BF_InverseSourceAlpha>::GetRHI()
						: TStaticBlendState<CW_RGBA, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_InverseDestAlpha, BF_One>::GetRHI())
					: TStaticBlendState<>::GetRHI();
				GraphicsPSOInit.PrimitiveType = EPrimitiveType::PT_TriangleList;
				GraphicsPSOInit.NumSamples = DestinationTexture->Desc.NumSamples;
				GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GetDreamUIPostProcessVertexDeclaration();
				GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
				SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0, EApplyRendertargetOption::CheckApply);

				FDreamUISimpleCopyTargetPS::FParameters Parameters;
				Parameters.MainTex = SourceTexture->GetRHI();
				Parameters.MainTexSampler = SrcTextureSamplerState != nullptr ? SrcTextureSamplerState : TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
				Parameters.BlendAlpha = BlendAlpha;
				SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), Parameters);

				FDreamUIRenderer::DrawFullScreenQuad(RHICmdList);
			});
	}
}

void FDreamUIRenderer::CopyRenderTarget(FRDGBuilder& GraphBuilder, FGlobalShaderMap* GlobalShaderMap, FTextureRHIRef Src, FTextureRHIRef Dst
	, FRHISamplerState* SrcTextureSamplerState
)
{
	CopyRenderTarget(GraphBuilder, GlobalShaderMap, RegisterExternalTexture(GraphBuilder, Src, TEXT("DreamUICopyRenderTargetSource"))
		, RegisterExternalTexture(GraphBuilder, Dst, TEXT("DreamUICopyRenderTarget")), SrcTextureSamplerState);
}

void FDreamUIRenderer::CopyRenderTarget(FRDGBuilder& GraphBuilder, FGlobalShaderMap* GlobalShaderMap, FRDGTextureRef Src, FRDGTextureRef Dst
	, FRHISamplerState* SrcTextureSamplerState)
{
	DreamUIRendererLocal::AddCopyTargetPass(GraphBuilder, GlobalShaderMap, Src, Dst, SrcTextureSamplerState, false, false, 1.0f
		, TEXT("DreamUICopyRenderTarget"));
}

void FDreamUIRenderer::CopyRenderTarget_ColorCorrect(FRDGBuilder& GraphBuilder, FGlobalShaderMap* GlobalShaderMap,
	FTextureRHIRef Src, FTextureRHIRef Dst, FRHISamplerState* SrcTextureSamplerState)
{
	CopyRenderTarget_ColorCorrect(GraphBuilder, GlobalShaderMap, RegisterExternalTexture(GraphBuilder, Src, TEXT("DreamUICopyRenderTarget_ColorCorrectSource"))
		, RegisterExternalTexture(GraphBuilder, Dst, TEXT("DreamUICopyRenderTarget_ColorCorrect")), SrcTextureSamplerState);
}

void FDreamUIRenderer::CopyRenderTarget_ColorCorrect(FRDGBuilder& GraphBuilder, FGlobalShaderMap* GlobalShaderMap, FRDGTextureRef Src, FRDGTextureRef Dst
	, FRHISamplerState* SrcTextureSamplerState)
{
	DreamUIRendererLocal::AddCopyTargetPass(GraphBuilder, GlobalShaderMap, Src, Dst, SrcTextureSamplerState, true, false, 1.0f
		, TEXT("DreamUICopyRenderTarget_ColorCorrect"));
}

void FDreamUIRenderer::CopyRenderTarget_BlendAlpha(FRDGBuilder& GraphBuilder, FGlobalShaderMap* GlobalShaderMap,
                                                 FTextureRHIRef Src, FTextureRHIRef Dst, float BlendAlpha, FRHISamplerState* SrcTextureSamplerState)
{
	CopyRenderTarget_BlendAlpha(GraphBuilder, GlobalShaderMap, RegisterExternalTexture(GraphBuilder, Src, TEXT("DreamUICopyRenderTarget_BlendAlphaSource"))
		, RegisterExternalTexture(GraphBuilder, Dst, TEXT("DreamUICopyRenderTarget_BlendAlpha")), BlendAlpha, SrcTextureSamplerState);
}

void FDreamUIRenderer::CopyRenderTarget_BlendAlpha(FRDGBuilder& GraphBuilder, FGlobalShaderMap* GlobalShaderMap, FRDGTextureRef Src, FRDGTextureRef Dst
	, float BlendAlpha, FRHISamplerState* SrcTextureSamplerState)
{
	DreamUIRendererLocal::AddCopyTargetPass(GraphBuilder, GlobalShaderMap, Src, Dst, SrcTextureSamplerState, false, true, BlendAlpha
		, TEXT("DreamUICopyRenderTarget_BlendAlpha"));
}

void FDreamUIRenderer::CopyRenderTargetOnMeshRegion(
	FRDGBuilder& GraphBuilder
	, FRDGTextureRef Dst
	, FTextureRHIRef Src
	, FGlobalShaderMap* GlobalShaderMap
	, const TArray<FDreamUIPostProcessCopyMeshRegionVertex>& RegionVertexData
	, const FMatrix44f& MVP
	, bool bIsRenderTarget
	, const FIntRect& ViewRect
	, const FVector4f& SrcTextureScaleOffset
	, bool ColorCorrect
)
{
	CopyRenderTargetOnMeshRegion(GraphBuilder, Dst, RegisterExternalTexture(GraphBuilder, Src, TEXT("DreamUICopyRenderTargetOnMeshRegionSource"))
		, GlobalShaderMap, RegionVertexData, MVP, bIsRenderTarget, ViewRect, SrcTextureScaleOffset, ColorCorrect);
}

void FDreamUIRenderer::CopyRenderTargetOnMeshRegion(
	FRDGBuilder& GraphBuilder
	, FRDGTextureRef Dst
	, FRDGTextureRef SourceTexture
	, FGlobalShaderMap* GlobalShaderMap
	, const TArray<FDreamUIPostProcessCopyMeshRegionVertex>& RegionVertexData
	, const FMatrix44f& MVP
	, bool bIsRenderTarget
	, const FIntRect& ViewRect
	, const FVector4f& SrcTextureScaleOffset
	, bool ColorCorrect
)
{
	auto* PassParameters = GraphBuilder.AllocParameters<FDreamUITextureReadRenderTargetParameters>();
	PassParameters->SourceTexture = SourceTexture;
	PassParameters->RenderTargets[0] = FRenderTargetBinding(Dst, ERenderTargetLoadAction::EClear);
	auto NumSamples = Dst->Desc.NumSamples;

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("DreamUICopyRenderTargetOnMeshRegion"),
		PassParameters,
		ERDGPassFlags::Raster,
		[SourceTexture, GlobalShaderMap, RegionVertexData, MVP, bIsRenderTarget, ViewRect, SrcTextureScaleOffset, NumSamples, ColorCorrect](FRHICommandListImmediate& RHICmdList)
		{
			SourceTexture->MarkResourceAsUsed();
			auto SourceRHI = SourceTexture->GetRHI();
			RHICmdList.SetViewport(ViewRect.Min.X, ViewRect.Min.Y, 0.0f, ViewRect.Max.X, ViewRect.Max.Y, 1.0f);

			TShaderMapRef<FDreamUICopyMeshRegionVS> VertexShader(GlobalShaderMap);
			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, ECompareFunction::CF_Always>::GetRHI();
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
			GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GetDreamUIPostProcessCopyMeshRegionVertexDeclaration();
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
			GraphicsPSOInit.PrimitiveType = EPrimitiveType::PT_TriangleList;
			GraphicsPSOInit.NumSamples = NumSamples;
			FDreamUICopyMeshRegionPS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FDreamUICopyMeshRegionPS::FColorCorrect>(ColorCorrect);
			TShaderMapRef<FDreamUICopyMeshRegionPS> PixelShader(GlobalShaderMap, PermutationVector);
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0, EApplyRendertargetOption::CheckApply);

			FDreamUICopyMeshRegionPS::FParameters Parameters;
			Parameters.MainTex = SourceRHI;
			Parameters.MainTexSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
			Parameters.MainTextureScaleOffset = SrcTextureScaleOffset;
			Parameters.LocalToClip = MVP;
			Parameters.IsRenderTarget = bIsRenderTarget ? 1.0f : 0.0f;
			SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), Parameters);
			
			FBufferRHIRef VertexBufferRHI = UE::RHIResourceUtils::CreateVertexBufferFromArray(
			RHICmdList, TEXT("CopyRenderTargetOnMeshRegion"), EBufferUsageFlags::Volatile, MakeConstArrayView(RegionVertexData)
			);

			RHICmdList.SetStreamSource(0, VertexBufferRHI, 0);
			RHICmdList.DrawIndexedPrimitive(GDreamUIFullScreenQuadIndexBuffer.IndexBufferRHI, 0, 0, 4, 0, 2, 1);

			VertexBufferRHI.SafeRelease();
		});
}

/**
 * The blend state behind each EDreamUIBlendMode, for DreamGUI's built-in shader.
 *
 * The shader emits PREMULTIPLIED colour (rgb already scaled by alpha), which is what makes these
 * three one-liners rather than three shader permutations:
 *   Alpha     src + dst*(1-srcA)   -- the ordinary composite, unchanged from before this existed
 *   Additive  src + dst            -- premultiplied source, so a transparent pixel adds nothing
 *   Multiply  src*dst              -- destination colour scaled by the element
 * Alpha is written to keep the destination alpha sensible (src.a + dst.a*(1-src.a)) so a UI drawn
 * into a transparent render target still composites correctly afterwards; Additive and Multiply
 * leave the destination alpha alone, which is what their scene-material equivalents do.
 */
FRHIBlendState* FDreamUIRenderer::GetBuiltInBlendState(EDreamUIBlendMode InBlendMode)
{
	switch (InBlendMode)
	{
	case EDreamUIBlendMode::Additive:
		return TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_One, BO_Add, BF_Zero, BF_One>::GetRHI();
	case EDreamUIBlendMode::Multiply:
		return TStaticBlendState<CW_RGBA, BO_Add, BF_DestColor, BF_Zero, BO_Add, BF_Zero, BF_One>::GetRHI();
	default:
	case EDreamUIBlendMode::Alpha:
		return TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_InverseSourceAlpha, BO_Add, BF_One, BF_InverseSourceAlpha>::GetRHI();
	}
}

void FDreamUIRenderer::DrawFullScreenQuad(FRHICommandListImmediate& RHICmdList)
{
	RHICmdList.SetStreamSource(0, GDreamUIFullScreenQuadVertexBuffer.VertexBufferRHI, 0);
	RHICmdList.DrawIndexedPrimitive(GDreamUIFullScreenQuadIndexBuffer.IndexBufferRHI, 0, 0, 4, 0, 2, 1);
}
namespace DreamUIRendererLocal
{
	/**
	 * The mesh batches of one primitive, collected on the render thread when its pass is recorded, and
	 * the allocator their uniform buffers live in. Allocated by the graph, which destroys it after every
	 * pass that reads it has run.
	 */
	struct FCollectedMeshBatches
	{
		FSceneRenderingBulkObjectAllocator Allocator;
		TArray<FDreamUIMeshBatchContainer> Batches;
	};

	/**
	 * What the draws of one pass through a material looked up last: the shaders the material has for the world renderer's
	 * shader types. A wall of world-space panels draws one material thousands of times a frame, panel after panel, and each
	 * draw looked the shaders up in the material's shader map again.
	 */
	template<typename PixelShaderType>
	struct TMaterialShadersCache
	{
		const FMaterial* Material = nullptr;
		bool bFound = false;
		TShaderRef<FDreamUIScreenRenderVS> VertexShader;
		TShaderRef<PixelShaderType> PixelShader;

		bool Find(const FMaterial& InMaterial)
		{
			if (Material != &InMaterial)
			{
				Material = &InMaterial;
				FMaterialShaderTypes ShaderTypes;
				ShaderTypes.AddShaderType<FDreamUIScreenRenderVS>();
				ShaderTypes.AddShaderType<PixelShaderType>();
				FMaterialShaders Shaders;
				bFound = InMaterial.TryGetShaders(ShaderTypes, nullptr, Shaders);
				VertexShader = TShaderRef<FDreamUIScreenRenderVS>();
				PixelShader = TShaderRef<PixelShaderType>();
				if (bFound)
				{
					Shaders.TryGetVertexShader(VertexShader);
					Shaders.TryGetPixelShader(PixelShader);
				}
			}
			return bFound;
		}
	};

	/**
	 * The pipeline state a pass's last draw through a material set, by what it was made from: a draw that would make the
	 * same one does not look it up in the cache and set it again. Forgotten by any draw that sets another -- a built-in one.
	 */
	struct FMaterialPipelineKey
	{
		const FMaterial* Material = nullptr;
		bool bWireframe = false;
		bool bReverseCulling = false;
		bool bDepthFade = false;
		bool bSet = false;

		bool Matches(const FMaterial* InMaterial, bool bInWireframe, bool bInReverseCulling, bool bInDepthFade) const
		{
			return bSet && Material == InMaterial && bWireframe == bInWireframe && bReverseCulling == bInReverseCulling && bDepthFade == bInDepthFade;
		}
		void Remember(const FMaterial* InMaterial, bool bInWireframe, bool bInReverseCulling, bool bInDepthFade)
		{
			Material = InMaterial;
			bWireframe = bInWireframe;
			bReverseCulling = bInReverseCulling;
			bDepthFade = bInDepthFade;
			bSet = true;
		}
	};

	/** What one collection is about to draw -- its batches from InFirst on -- into the frame's counters. */
	void CountCollected(const FCollectedMeshBatches& InCollected, int32 InFirst = 0)
	{
		int64 Vertices = 0;
		for (int32 Index = InFirst; Index < InCollected.Batches.Num(); ++Index)
		{
			Vertices += InCollected.Batches[Index].NumVerts;
		}
		DreamUIRenderStats::AddCount(DreamUIRenderStats::ECounter::BatchesRecorded, InCollected.Batches.Num() - InFirst);
		DreamUIRenderStats::AddCount(DreamUIRenderStats::ECounter::VerticesRecorded, Vertices);
	}

	/**
	 * Whether a batch collected -- from InFirst on -- is drawn through a material, its own or the wireframe's, which reads
	 * the view's uniform buffer.
	 */
	bool DrawsThroughAMaterial(const FCollectedMeshBatches& InCollected, bool bRenderWireframe, int32 InFirst = 0)
	{
		if (bRenderWireframe)
		{
			return true;
		}
		for (int32 Index = InFirst; Index < InCollected.Batches.Num(); ++Index)
		{
			if (!InCollected.Batches[Index].bBuiltIn)
			{
				return true;
			}
		}
		return false;
	}

	/** Blends whose destination colour or alpha cannot be recovered by compositing a transparent UI target. */
	bool UsesOriginalDestination(const FCollectedMeshBatches& InCollected, ERHIFeatureLevel::Type InFeatureLevel)
	{
		for (const FDreamUIMeshBatchContainer& Batch : InCollected.Batches)
		{
			if (Batch.bBuiltIn)
			{
				if (Batch.GetBuiltIn().BlendMode == EDreamUIBlendMode::Multiply)
				{
					return true;
				}
			}
			else if (const FMaterialRenderProxy* Proxy = Batch.Mesh.MaterialRenderProxy)
			{
				if (const FMaterial* Material = Proxy->GetMaterialNoFallback(InFeatureLevel))
				{
					const EBlendMode Blend = Material->GetBlendMode();
					if (Blend == BLEND_Modulate || Blend == BLEND_AlphaHoldout || Blend == BLEND_Additive)
					{
						return true;
					}
				}
			}
		}
		return false;
	}

	/**
	 * The view's resolved scene depth, through the renderer's public scene-texture uniform buffers -- the deferred one
	 * or the mobile one, whichever path drew the view -- and the dummy depth where there is none to read: a view the
	 * scene renderer did not make, or a mobile depth that never leaves tile memory. It was read before from the view
	 * family cast to the renderer's private class, which only a private include path could see.
	 *
	 * The deferred buffer is read as the scene renderer left it, not made again: by the time a view is done, it holds
	 * every scene texture. Making one is a whole struct of textures to set up for every renderer and every view, every
	 * frame, and the render thread paid for it on each of them. The mobile renderer sets its buffer up pass by pass and
	 * may have left the depth out, so for it one is made that asks for the depth alone.
	 */
	FRDGTextureRef SceneDepthOf(FRDGBuilder& GraphBuilder, const FSceneView& InView)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(DreamUI_SceneDepth);
		FRDGTextureRef Depth = nullptr;
		if (InView.bIsViewInfo)
		{
			const FSceneTextureShaderParameters Parameters = GetSceneTextureShaderParameters(InView);
			if (Parameters.SceneTextures)
			{
				Depth = Parameters.SceneTextures->GetContents()->SceneDepthTexture;
			}
			else if (Parameters.MobileSceneTextures)
			{
				if (const TRDGUniformBufferRef<FMobileSceneTextureUniformParameters> Mobile
					= CreateMobileSceneTextureUniformBuffer(GraphBuilder, InView, EMobileSceneTextureSetupMode::SceneDepth))
				{
					Depth = Mobile->GetContents()->SceneDepthTexture;
				}
			}
		}
		return Depth != nullptr ? Depth : GSystemTextures.GetDepthDummy(GraphBuilder);
	}

	/**
	 * The multisampled target the UI is drawn into before the resolve writes it into InTarget: InTarget's size and format,
	 * InNumSamples samples, sRGB when InTarget is, and a shader resource for the resolve to read. The sRGB flag is the
	 * point. Without it the target of a canvas drawn into an sRGB texture (a render-target canvas) stored the linear values
	 * the UI blends in 8 bits, and only the resolve encoded them: every dark level fell on one of a dozen steps -- the gallery
	 * backdrop (28,30,38) came out (28,28,38) -- and the clear colour was rounded with them. With it the target encodes as it
	 * is written and decodes as the resolve reads it, like the single-sampled path. One description for both branches, the
	 * render-target canvas's and the view's.
	 */
	FPooledRenderTargetDesc MakeMultisampledTargetDesc(const FRHITexture& InTarget, uint8 InNumSamples)
	{
		const ETextureCreateFlags SRGBFlag = EnumHasAnyFlags(InTarget.GetFlags(), TexCreate_SRGB) ? TexCreate_SRGB : TexCreate_None;
		FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(InTarget.GetSizeXY(), InTarget.GetFormat(), FClearValueBinding::Black
			, SRGBFlag, TexCreate_RenderTargetable | TexCreate_ShaderResource, false));
		Desc.NumSamples = InNumSamples;
		return Desc;
	}
}

/**
 * What the built-in draws of one pass looked up and set last: the vertex shader and each permutation's pixel shader, and the
 * pipeline state by what it was made from. A wall of world-space panels draws with the built-in shader thousands of times
 * a frame, one panel after another, and each draw looked both shaders up in the global shader map and made and set its
 * pipeline state again. A draw that sets another pipeline -- through a material -- forgets it (Pipeline.bSet).
 */
struct FDreamUIBuiltInDrawCache
{
	TShaderRef<FDreamUIBaseVS> VertexShader;
	/** By permutation: 1 blends with the scene's depth, 2 fades by it. */
	TShaderRef<FDreamUIBasePS> PixelShaders[4];
	struct FPipeline
	{
		bool bSet = false;
		EDreamUIBlendMode BlendMode = EDreamUIBlendMode::Alpha;
		bool bIsDepthValid = false;
		bool bReverseCulling = false;
		int32 Permutation = 0;
		uint8 NumSamples = 0;

		bool Matches(EDreamUIBlendMode InBlendMode, bool bInIsDepthValid, bool bInReverseCulling, int32 InPermutation, uint8 InNumSamples) const
		{
			return bSet && BlendMode == InBlendMode && bIsDepthValid == bInIsDepthValid && bReverseCulling == bInReverseCulling
				&& Permutation == InPermutation && NumSamples == InNumSamples;
		}
		void Remember(EDreamUIBlendMode InBlendMode, bool bInIsDepthValid, bool bInReverseCulling, int32 InPermutation, uint8 InNumSamples)
		{
			bSet = true;
			BlendMode = InBlendMode;
			bIsDepthValid = bInIsDepthValid;
			bReverseCulling = bInReverseCulling;
			Permutation = InPermutation;
			NumSamples = InNumSamples;
		}
	};
	FPipeline Pipeline;
};

/**
 * Takes FRHICommandList&, not FRHICommandListImmediate&, and so does every mesh pass that calls it:
 * RDG reads the lambda's command-list type and only records a pass on a task when it is the
 * non-immediate one (see TRDGLambdaPass: an immediate list forces ERDGPassTaskMode::Inline). Every
 * call below -- ApplyCachedRenderTargets, SetGraphicsPipelineState, the shader parameter setters,
 * SetStreamSource, DrawIndexedPrimitive -- is declared on FRHICommandList, so nothing here actually
 * needed the immediate list; it only cost the UI passes their parallelism.
 */
void FDreamUIRenderer::DrawBuiltInBatch(FRHICommandList& RHICmdList, FGraphicsPipelineStateInitializer& GraphicsPSOInit
	, const FSceneView& View, const FIntRect& ViewRect, const FDreamUIMeshBatchContainer& Batch
	, uint8 NumSamples, float GammaValue, bool bIsDepthValid
	, bool bBlendDepth, float BlendDepth, int DepthFade, const FVector4f& SceneDepthTexST, FRHITexture* SceneDepthTexture
	, FDreamUIBuiltInDrawCache* InOutCache
)
{
	const FDreamUIBuiltInDrawParams& Params = Batch.GetBuiltIn();
	const FMeshBatch& Mesh = Batch.Mesh;
	const ERHIFeatureLevel::Type FeatureLevel = View.GetFeatureLevel();

	const bool bDepthFade = bBlendDepth && DepthFade > 0;
	const int32 Permutation = (bBlendDepth ? 1 : 0) | (bDepthFade ? 2 : 0);
	TShaderRef<FDreamUIBaseVS> VertexShader;
	TShaderRef<FDreamUIBasePS> PixelShader;
	// The pass's cache has them after its first draw of each permutation (FDreamUIBuiltInDrawCache).
	if (InOutCache != nullptr && InOutCache->PixelShaders[Permutation].IsValid())
	{
		VertexShader = InOutCache->VertexShader;
		PixelShader = InOutCache->PixelShaders[Permutation];
	}
	else
	{
		auto GlobalShaderMap = GetGlobalShaderMap(FeatureLevel);
		VertexShader = TShaderMapRef<FDreamUIBaseVS>(GlobalShaderMap);
		FDreamUIBasePS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FDreamUIBasePS::FBlendDepth>(bBlendDepth);
		PermutationVector.Set<FDreamUIBasePS::FDepthFade>(bDepthFade);
		PixelShader = TShaderMapRef<FDreamUIBasePS>(GlobalShaderMap, PermutationVector);
		if (InOutCache != nullptr)
		{
			InOutCache->VertexShader = VertexShader;
			InOutCache->PixelShaders[Permutation] = PixelShader;
		}
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
	// Set only when it differs from the one the pass's last built-in draw set: nothing else of it changes between draws.
	if (InOutCache == nullptr || !InOutCache->Pipeline.Matches(Params.BlendMode, bIsDepthValid, Mesh.ReverseCulling, Permutation, NumSamples))
	{
		SetGraphicPipelineState_BlendDepthStencilRasterize(FeatureLevel, GraphicsPSOInit, BLEND_AlphaComposite
			, false, true, false, bIsDepthValid, Mesh.ReverseCulling
		);
		GraphicsPSOInit.BlendState = GetBuiltInBlendState(Params.BlendMode);
		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GetDreamUIMeshVertexDeclaration();
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
		GraphicsPSOInit.PrimitiveType = EPrimitiveType::PT_TriangleList;
		GraphicsPSOInit.NumSamples = NumSamples;
		SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0, EApplyRendertargetOption::CheckApply);
		if (InOutCache != nullptr)
		{
			InOutCache->Pipeline.Remember(Params.BlendMode, bIsDepthValid, Mesh.ReverseCulling, Permutation, NumSamples);
		}
	}

	// Each texture's reference, which is whatever texture its UTexture has now -- or a black one, once the UTexture is
	// gone -- and each texture's own sampler, as the material path uses it.
	auto TextureOrFallback = [](FRHITextureReference* InReference, FTexture* Fallback) -> FRHITexture*
	{
		return InReference != nullptr ? static_cast<FRHITexture*>(InReference) : Fallback->TextureRHI.GetReference();
	};

	// Model-view-projection in double precision, demoted once.
	const FMatrix ViewProjection = View.ViewMatrices.GetWorldToClip();
	FDreamUIBaseVS::FParameters VSParameters;
	VSParameters.DreamUI_MVP = FMatrix44f(Batch.LocalToWorld * ViewProjection);
	VSParameters.DreamUI_RenderLayerTable = TextureOrFallback(Params.RenderLayerTableRHI.GetReference(), GBlackTexture);
	VSParameters.DreamUI_RenderLayerWidgetData = TextureOrFallback(Params.WidgetDataTextureRHI.GetReference(), GBlackTexture);
	SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), VSParameters);
	auto SamplerOrBilinear = [](FRHISamplerState* InSampler) -> FRHISamplerState*
	{
		return InSampler != nullptr ? InSampler : TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	};
	FDreamUIBasePS::FParameters PSParameters;
	PSParameters.DreamUI_GammaValues = FVector4f(2.2f / GammaValue, 1.0f / GammaValue, 0.0f, 0.0f);
	PSParameters.DreamUI_FontAtlasInfo = FVector4f(Params.FontAtlasSize.X, Params.FontAtlasSize.Y, Params.FontFieldRangeTexels, Params.FontEmTexels);
	PSParameters.DreamUI_MainTex = TextureOrFallback(Params.MainTextureRHI.GetReference(), GWhiteTexture);
	PSParameters.DreamUI_MainTexSampler = SamplerOrBilinear(Params.MainSamplerRHI.GetReference());
	PSParameters.DreamUI_FontTex = TextureOrFallback(Params.FontTextureRHI.GetReference(), GBlackArrayTexture);
	PSParameters.DreamUI_FontTexSampler = SamplerOrBilinear(Params.FontSamplerRHI.GetReference());
	PSParameters.DreamUI_WidgetDataTex = TextureOrFallback(Params.WidgetDataTextureRHI.GetReference(), GBlackTexture);
	PSParameters.DreamUI_ClipDataTex = TextureOrFallback(Params.ClipDataTextureRHI.GetReference(), GBlackTexture);
	PSParameters.DreamUI_RectBlockDataTex = TextureOrFallback(Params.RectBlockDataRHI.GetReference(), GBlackTexture);
	// Black where the world has no paint rows: it reads gradient type 0, None, so a painted quad drawn without them is solid.
	PSParameters.DreamUI_PaintDataTex = TextureOrFallback(Params.PaintDataRHI.GetReference(), GBlackTexture);
	PSParameters.DreamUI_SceneDepthTex = SceneDepthTexture ? SceneDepthTexture : GBlackTexture->TextureRHI.GetReference();
	PSParameters.DreamUI_SceneDepthTexSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	PSParameters.DreamUI_SceneDepthTextureScaleOffset = SceneDepthTexST;
	PSParameters.DreamUI_SceneDepthBlend = BlendDepth;
	PSParameters.DreamUI_SceneDepthFade = DepthFade;
	const FIntPoint ViewSize = ViewRect.Size();
	PSParameters.DreamUI_ViewSizeInv = FVector2f(1.0f / FMath::Max(ViewSize.X, 1), 1.0f / FMath::Max(ViewSize.Y, 1));
	SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), PSParameters);

	RHICmdList.SetStreamSource(0, Batch.GetVertexBuffer(), 0);
	RHICmdList.DrawIndexedPrimitive(Batch.GetIndexBuffer(), 0, 0, Batch.NumVerts, 0, Mesh.Elements[0].NumPrimitives, Mesh.Elements[0].NumInstances);
}

void FDreamUIRenderer::SetGraphicPipelineState_BlendDepthStencilRasterize(ERHIFeatureLevel::Type FeatureLevel, FGraphicsPipelineStateInitializer& GraphicsPSOInit, EBlendMode BlendMode
	, bool bIsWireFrame, bool bIsTwoSided, bool bDisableDepthTestForTransparent, bool bIsDepthValid, bool bReverseCulling
) 
{
	switch (BlendMode)
	{
	default:
	case BLEND_Opaque:
		GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
		break;
	case BLEND_Masked:
		GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
		break;
	case BLEND_Translucent:
		GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_InverseDestAlpha, BF_One>::GetRHI();
		break;
	case BLEND_Additive:
		// Add to the existing scene color
		GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_SourceAlpha, BF_One, BO_Add, BF_Zero, BF_InverseSourceAlpha>::GetRHI();
		//GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_SourceAlpha, BF_One, BO_Add, BF_One, BF_One>::GetRHI();
		break;
	case BLEND_Modulate:
		// Modulate with the existing scene color
		GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGB, BO_Add, BF_Zero, BF_SourceColor>::GetRHI();
		break;
	case BLEND_AlphaComposite:
		// Blend with existing scene color. New color is already pre-multiplied by alpha.
		GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_InverseSourceAlpha, BO_Add, BF_One, BF_InverseSourceAlpha>::GetRHI();
		break;
	case BLEND_AlphaHoldout:
		// Blend by holding out the matte shape of the source alpha
		GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_Zero, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_InverseSourceAlpha>::GetRHI();
		break;
	};

	if (bIsDepthValid)
	{
		if (BlendMode == BLEND_Opaque || BlendMode == BLEND_Masked)
		{
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<true, ECompareFunction::CF_GreaterEqual>::GetRHI();
		}
		else
		{
			if (bDisableDepthTestForTransparent)
			{
				GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, ECompareFunction::CF_Always>::GetRHI();
			}
			else
			{
				GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, ECompareFunction::CF_GreaterEqual>::GetRHI();
			}
		}
	}
	else
	{
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, ECompareFunction::CF_Always>::GetRHI();
	}
	
#if PLATFORM_ANDROID
	auto ShaderPlatform = GShaderPlatformForFeatureLevel[FeatureLevel];
	if (ShaderPlatform == EShaderPlatform::SP_OPENGL_ES3_1_ANDROID)
	{
		bReverseCulling = !bReverseCulling;//android gles is flipped
	}
#endif
	if (!bIsWireFrame)
	{
		if (bIsTwoSided)
		{
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
		}
		else
		{
			if (bReverseCulling)
			{
				GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_CCW>::GetRHI();
			}
			else
			{
				GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_CW>::GetRHI();
			}
		}
	}
	else
	{
		if (bIsTwoSided)
		{
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Wireframe, CM_None>::GetRHI();
		}
		else
		{
			if (bReverseCulling)
			{
				GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Wireframe, CM_CCW>::GetRHI();
			}
			else
			{
				GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Wireframe, CM_CW>::GetRHI();
			}
		}
	}
}


DECLARE_CYCLE_STAT(TEXT("DreamUI RHIRenderMesh"), STAT_DreamGUI_RHIRenderMesh, STATGROUP_DreamGUI);
DECLARE_CYCLE_STAT(TEXT("DreamUI RHIRenderPostProcess"), STAT_DreamGUI_RHIRenderPostProcess, STATGROUP_DreamGUI);
void FDreamUIRenderer::RenderDreamUI_RenderThread(
	FRDGBuilder& GraphBuilder
	, FSceneView& InView
	, FTextureRHIRef InTargetOverride)
{
	if (ScreenSpaceRenderParameter.PrimitiveArray.Num() <= 0 && WorldSpaceRenderCanvasParameterArray.Num() <= 0
#if WITH_EDITOR
		&& ScreenSpaceGizmoMeshArray.Num() <= 0
		&& WorldSpaceGizmoMeshArray.Num() <= 0
#endif
		)return;//nothing to render
	DREAMUI_STAGE_SCOPE(RenderRecord);
	// In stages, which share what the first one makes: the targets, then the world-space canvases, then the screen-space
	// ones, then the resolve of the multisampled target.
	FRecordTargets Targets;
	if (!PrepareTargets_RenderThread(GraphBuilder, InView, Targets, InTargetOverride))
	{
		return;
	}
	RecordWorldSpace_RenderThread(GraphBuilder, InView, Targets);
	RecordScreenSpace_RenderThread(GraphBuilder, InView, Targets);
	Resolve_RenderThread(GraphBuilder, InView, Targets);

	//Targets.MSAARenderTarget goes out of scope here. It is deliberately NOT released early: the graph holds
	//a reference of its own until it has executed the passes recorded above.
}

bool FDreamUIRenderer::PrepareTargets_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& InView, FRecordTargets& Targets, FTextureRHIRef InTargetOverride)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(DreamUI_PrepareTargets);
	bool bIsMainViewport = !(InView.bIsSceneCapture || InView.bIsReflectionCapture || InView.bIsPlanarReflection || InView.bIsVirtualTexture);
	
	// Lit wireframe is the lit view mode with mesh edges shown now.
	bool bRenderWireframe = InView.Family->ViewMode == VMI_Wireframe || InView.Family->EngineShowFlags.MeshEdges;
	bool bRenderLit = InView.Family->ViewMode != VMI_Wireframe;
	FMaterialRenderProxy* WireframeMaterialInstance = NULL;
	if (bRenderWireframe)
	{
		WireframeMaterialInstance = GEngine->WireframeMaterial->GetRenderProxy();
	}

	FTextureRHIRef OrignScreenColorRenderTargetTexture = nullptr;
	FTextureRHIRef ScreenColorRenderTargetTexture = nullptr;
	//msaa render target
	TRefCountPtr<IPooledRenderTarget> MSAARenderTarget = nullptr;

	uint8 NumSamples = RenderThreadViewParameter.NumSamples_MSAA;
	FIntRect ViewRect;
	FRHICommandListImmediate& RHICmdList = GraphBuilder.RHICmdList;
	FVector4f DepthTextureScaleOffset;
	FVector4f ColorTextureScaleOffset;
	// Once for the view: taken from the view the scene renderer made. The copies of it made below for the world- and
	// screen-space passes are plain FSceneViews and cannot be asked. A render-target canvas draws into a target of its
	// own and tests nothing against the scene, so it binds the dummy and does not ask at all.
	const FRDGTextureRef SceneDepth = RendererType == EDreamUIRendererType::RenderTarget
		? GSystemTextures.GetDepthDummy(GraphBuilder)
		: DreamUIRendererLocal::SceneDepthOf(GraphBuilder, InView);
	if (RendererType == EDreamUIRendererType::RenderTarget)//render-target mode
	{
		if (!bIsMainViewport)//render to scene capture (or other capture)
		{
			return false;
		}
		if (CanvasTargetTexture.IsValid())
		{
			ScreenColorRenderTargetTexture = CanvasTargetTexture;
			if (ScreenColorRenderTargetTexture == nullptr)return false;//invalid render target

			if (NumSamples > 1)
			{
				//get msaa render target, sRGB as the canvas's own target is (MakeMultisampledTargetDesc)
				const FPooledRenderTargetDesc desc = DreamUIRendererLocal::MakeMultisampledTargetDesc(*ScreenColorRenderTargetTexture, NumSamples);
				GRenderTargetPool.FindFreeElement(RHICmdList, desc, MSAARenderTarget, TEXT("DreamUI_MSAA_RenderTarget"));
				if (!MSAARenderTarget.IsValid())
					return false;

				/**
				 * Hand the POOLED target to the graph, not just its RHI texture. Registering the raw
				 * texture wraps it in a throw-away FPooledRenderTarget that keeps the texture alive but
				 * not the POOL ELEMENT, so the element went back to GRenderTargetPool the moment this
				 * function returned -- which is before GraphBuilder.Execute() runs a single one of the
				 * passes recorded below. The next FindFreeElement of the same description in the same
				 * frame (the second eye, the second split-screen view) then got the very same texture.
				 * This registration parks a strong reference on the graph for its whole lifetime, and
				 * every later RegisterExternalTexture of the same RHI texture resolves to it.
				 */
				GraphBuilder.RegisterExternalTexture(MSAARenderTarget, TEXT("DreamUI_MSAA_RenderTarget"));

				OrignScreenColorRenderTargetTexture = ScreenColorRenderTargetTexture;
				ScreenColorRenderTargetTexture = MSAARenderTarget->GetRHI();
			}

			//clear render target
			{
				auto Parameters = GraphBuilder.AllocParameters<FRenderTargetParameters>();
				Parameters->RenderTargets[0] = FRenderTargetBinding(RegisterExternalTexture(GraphBuilder, ScreenColorRenderTargetTexture, TEXT("DreamUIRender_ClearRenderTarget")), ERenderTargetLoadAction::ENoAction);
				GraphBuilder.AddPass(
					RDG_EVENT_NAME("DreamUIRender_ClearRenderTarget"),
					Parameters,
					ERDGPassFlags::Raster,
					[ClearColor = RenderTargetClearColor](FRHICommandListImmediate& RHICmdList)
					{
						DrawClearQuad(RHICmdList, FLinearColor(ClearColor));
					}
				);
			}
			CanvasTargetTexture = nullptr;

			ViewRect = FIntRect(0, 0, ScreenColorRenderTargetTexture->GetSizeXYZ().X, ScreenColorRenderTargetTexture->GetSizeXYZ().Y);
		}
		else
		{
			return false;
		}

		ColorTextureScaleOffset = DepthTextureScaleOffset = FVector4f(1, 1, 0, 0);
	}
	else//world space or screen space mode
	{
		ScreenColorRenderTargetTexture = InTargetOverride.IsValid() ? InTargetOverride : InView.Family->RenderTarget->GetRenderTargetTexture();
		if (ScreenColorRenderTargetTexture == nullptr)return false;//invalid render target

		if (NumSamples > 1)
		{
			//get msaa render target, sRGB when the view's target is (MakeMultisampledTargetDesc), which it normally is not
			const FPooledRenderTargetDesc desc = DreamUIRendererLocal::MakeMultisampledTargetDesc(*ScreenColorRenderTargetTexture, NumSamples);
			GRenderTargetPool.FindFreeElement(RHICmdList, desc, MSAARenderTarget, TEXT("DreamUI_MSAA_RenderTarget"));
			if (!MSAARenderTarget.IsValid())
				return false;

			//keep the pool element itself referenced for the graph's lifetime -- see the long note on
			//the render-target-mode branch above
			GraphBuilder.RegisterExternalTexture(MSAARenderTarget, TEXT("DreamUI_MSAA_RenderTarget"));

			CopyRenderTarget(GraphBuilder, GetGlobalShaderMap(InView.GetFeatureLevel()), ScreenColorRenderTargetTexture, MSAARenderTarget->GetRHI());
			OrignScreenColorRenderTargetTexture = ScreenColorRenderTargetTexture;
			ScreenColorRenderTargetTexture = MSAARenderTarget->GetRHI();
		}

		ViewRect = InTargetOverride.IsValid() ? FIntRect(FIntPoint::ZeroValue, InTargetOverride->GetSizeXY()) : InView.UnscaledViewRect;
		float ScreenPercentage = 1.0f;//this can affect scale on depth texture
		if (InView.bIsViewInfo)
		{
			ScreenPercentage = (float)UE::FXRenderingUtils::GetRawViewRectUnsafe(InView).Width() / ViewRect.Width();
		}
		else
		{
			ScreenPercentage = 1.0f;
		}
		const FIntVector SceneDepthSize = SceneDepth->Desc.GetSize();
		switch (InView.StereoPass)
		{
		case EStereoscopicPass::eSSP_FULL:
		if (InTargetOverride.IsValid())
		{
			// The override is the whole view at whatever resolution the post-process chain has reached, which
			// is not always the output's; the depth is the primary view's. The shaders take a position across
			// the view, so it maps straight onto the depth texture's view rect.
			const FIntRect DepthRect = InView.bIsViewInfo ? UE::FXRenderingUtils::GetRawViewRectUnsafe(InView) : ViewRect;
			DepthTextureScaleOffset = FVector4f(
				(float)DepthRect.Width() / SceneDepthSize.X,
				(float)DepthRect.Height() / SceneDepthSize.Y,
				(float)DepthRect.Min.X / SceneDepthSize.X,
				(float)DepthRect.Min.Y / SceneDepthSize.Y
			);
			ColorTextureScaleOffset = FVector4f(1, 1, 0, 0);
		}
		else
		{
			DepthTextureScaleOffset = FVector4f(
				(float)ScreenColorRenderTargetTexture->GetSizeXYZ().X / SceneDepthSize.X,
				(float)ScreenColorRenderTargetTexture->GetSizeXYZ().Y / SceneDepthSize.Y,
				0, 0
			);
			DepthTextureScaleOffset = DepthTextureScaleOffset * ScreenPercentage;
			ColorTextureScaleOffset = FVector4f(1, 1, 0, 0);
		}
		break;
		case EStereoscopicPass::eSSP_PRIMARY:
		{
			DepthTextureScaleOffset = FVector4f(
				(float)ViewRect.Width() / SceneDepthSize.X,//normally ViewRect.Width is half of screen size
				(float)ViewRect.Height() / SceneDepthSize.Y,
				0, 0
			);
			DepthTextureScaleOffset = DepthTextureScaleOffset * ScreenPercentage;
			ColorTextureScaleOffset = FVector4f(0.5f, 1, 0, 0);
		}
		break;
		case EStereoscopicPass::eSSP_SECONDARY:
		{
			DepthTextureScaleOffset = FVector4f(
				(float)ViewRect.Width() / SceneDepthSize.X,
				(float)ViewRect.Height() / SceneDepthSize.Y,
				0, 0
			);
			DepthTextureScaleOffset = DepthTextureScaleOffset * ScreenPercentage;
			DepthTextureScaleOffset.Z = 0.5f;//right eye offset 0.5
			ColorTextureScaleOffset = FVector4f(0.5f, 1, 0.5f, 0);
		}
		break;
		}
	}

	FRDGTextureRef RenderTargetTexture = RegisterExternalTexture(GraphBuilder, ScreenColorRenderTargetTexture, TEXT("DreamUIRendererTargetTexture"));
	const float EngineGamma = GEngine ? GEngine->GetDisplayGamma() : 2.2f;
	float GammaValue =
		(RendererType == EDreamUIRendererType::RenderTarget || !bIsMainViewport) ? 1.0f : EngineGamma;

	Targets.bIsMainViewport = bIsMainViewport;
	Targets.bRenderWireframe = bRenderWireframe;
	Targets.bRenderLit = bRenderLit;
	Targets.WireframeMaterialInstance = WireframeMaterialInstance;
	Targets.OrignScreenColorRenderTargetTexture = OrignScreenColorRenderTargetTexture;
	Targets.ScreenColorRenderTargetTexture = ScreenColorRenderTargetTexture;
	Targets.MSAARenderTarget = MSAARenderTarget;
	Targets.NumSamples = NumSamples;
	Targets.ViewRect = ViewRect;
	Targets.DepthTextureScaleOffset = DepthTextureScaleOffset;
	Targets.ColorTextureScaleOffset = ColorTextureScaleOffset;
	Targets.SceneDepth = SceneDepth;
	Targets.RenderTargetTexture = RenderTargetTexture;
	Targets.GammaValue = GammaValue;
	return true;
}

void FDreamUIRenderer::RecordWorldSpace_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& InView, FRecordTargets& Targets)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(DreamUI_RecordWorldSpace);
	// The recording's targets, by the names the stages shared when they were one function.
	bool& bRenderWireframe = Targets.bRenderWireframe;
	bool& bRenderLit = Targets.bRenderLit;
	FMaterialRenderProxy*& WireframeMaterialInstance = Targets.WireframeMaterialInstance;
	FTextureRHIRef& ScreenColorRenderTargetTexture = Targets.ScreenColorRenderTargetTexture;
	uint8& NumSamples = Targets.NumSamples;
	FIntRect& ViewRect = Targets.ViewRect;
	FVector4f& DepthTextureScaleOffset = Targets.DepthTextureScaleOffset;
	FVector4f& ColorTextureScaleOffset = Targets.ColorTextureScaleOffset;
	const FRDGTextureRef SceneDepth = Targets.SceneDepth;
	const FRDGTextureRef RenderTargetTexture = Targets.RenderTargetTexture;
	const float GammaValue = Targets.GammaValue;

	//Render world space
	if (WorldSpaceRenderCanvasParameterArray.Num() > 0
#if WITH_EDITOR
		|| WorldSpaceGizmoMeshArray.Num() > 0
#endif
		)
	{
		//collect render primitive to a sequence
		struct FWorldSpaceRenderParameterSequence
		{
			FDreamUIPrimitiveDataArray RenderDataArray;
			//blend depth, 0-occlude by depth, 1-all visible
			float BlendDepth = 0.0f;
			//depth fade effect
			int DepthFade = 0;

			//for sort translucent
			FVector3f WorldPosition;
			//distance to camera (square)
			float DistToCamera = 0;
			int RenderPriority = 0;
		};
		TArray<FWorldSpaceRenderParameterSequence> RenderSequenceArray;
		RenderSequenceArray.Reserve(WorldSpaceRenderCanvasParameterArray.Num());
		for (auto& WorldRenderParameter : WorldSpaceRenderCanvasParameterArray)
		{
			if (WorldRenderParameter.Primitive->DreamUI_CanRender())
			{
				bool bIsPrimitiveVisible = false;//default is not visible
				if (InView.ShowOnlyPrimitives.IsSet())
				{
					bIsPrimitiveVisible = InView.ShowOnlyPrimitives.GetValue().Contains(WorldRenderParameter.Primitive->DreamUI_GetPrimitiveComponentId());
				}
				else
				{
					bIsPrimitiveVisible = !InView.HiddenPrimitives.Contains(WorldRenderParameter.Primitive->DreamUI_GetPrimitiveComponentId());
				}
				if (bIsPrimitiveVisible)
				{
					auto WorldBounds = WorldRenderParameter.Primitive->DreamUI_GetWorldBounds();
					if (!RenderThreadViewParameter.bFrustumCulling 
						|| (RenderThreadViewParameter.bFrustumCulling && InView.GetCullingFrustum().IntersectBox(WorldBounds.Origin, WorldBounds.BoxExtent))//simple View Frustum Culling
						)
					{
						FWorldSpaceRenderParameterSequence Item;
						WorldRenderParameter.Primitive->DreamUI_CollectRenderData(Item.RenderDataArray);
						if (Item.RenderDataArray.Num() > 0)
						{
							Item.BlendDepth = WorldRenderParameter.BlendDepth;
							Item.DepthFade = WorldRenderParameter.DepthFade;
							Item.WorldPosition = WorldRenderParameter.Primitive->DreamUI_GetWorldPositionForSortTranslucent();
							Item.RenderPriority = WorldRenderParameter.Primitive->DreamUI_GetRenderPriority();
							// Moved: its render data is an array of arrays, which a copy made again for each of a world's panels.
							RenderSequenceArray.Add(MoveTemp(Item));
						}
					}
				}
			}
		}
		if (RenderSequenceArray.Num() > 0
#if WITH_EDITOR
			|| WorldSpaceGizmoMeshArray.Num() > 0
#endif
			)
		{
			//use a copied view. 
			//NOTE!!! world-space and screen-space must use different 'RenderView' (actually different ViewUniformBuffer), because RDG is async. 
			//if use same one, after world-space when modify 'RenderView' for screen-space, the screen-space ViewUniformBuffer will be applyed to world-space
			//
			// Owned by the GRAPH, not by a pass. The draw passes below take FRHICommandList&, so the graph is
			// free to record them on task threads -- and it does, asynchronously, after the passes that need
			// the immediate list have already run on the render thread. A closing pass that deleted this view
			// therefore ran BEFORE the draws that read it: they bound a View uniform buffer that had just been
			// released to null, and the RHI thread dereferenced it a moment later. An object the graph
			// allocated is destroyed by the graph's own deleter, whose prerequisites are those very tasks.
			FSceneView* RenderView = GraphBuilder.AllocObject<FSceneView>(InView);
			auto GlobalShaderMap = GetGlobalShaderMap(RenderView->GetFeatureLevel());

			RenderView->ViewMatrices = InView.ViewMatrices;
			RenderView->ViewMatrices.HackRemoveTemporalAAProjectionJitter();
			auto ViewProjectionMatrix = FMatrix44f(RenderView->ViewMatrices.GetWorldToClip());

			// The view's uniform buffer is what a material reads; the built-in shader reads none of it. It is made once a batch
			// that can be drawn through a material has been collected, and a stage that collects none makes none. The copy
			// starts without the view's own.
			RenderView->ViewUniformBuffer.SafeRelease();
			auto MakeViewUniformBuffer = [RenderView, &ViewRect]()
			{
				if (RenderView->ViewUniformBuffer.IsValid())
				{
					return;
				}
				TRACE_CPUPROFILER_EVENT_SCOPE(DreamUI_ViewUniformBuffer);
				FViewUniformShaderParameters ViewUniformShaderParameters;
				RenderView->SetupCommonViewUniformBufferParameters(
					ViewUniformShaderParameters,
					ViewRect.Size(),
					1,
					ViewRect,
					RenderView->ViewMatrices,
					FViewMatrices()
				);
				RenderView->ViewUniformBuffer = TUniformBufferRef<FViewUniformShaderParameters>::CreateUniformBufferImmediate(ViewUniformShaderParameters, UniformBuffer_SingleFrame);
			};

			/**
			 * Unconditional on purpose, and no dirty flag can change that: RenderSequenceArray is built
			 * from scratch a few lines above, so it arrives unsorted every frame, and the distance term
			 * is measured against this frame's camera. The commented-out `if (bNeedSortWorldSpaceRenderCanvas)`
			 * that used to stand here would have rendered world-space canvases in registration order.
			 */
			/**
			 * Sorted as keys -- priority, distance, and the place in the array, which makes it the order a stable sort gives:
			 * equal (priority, distance) keeps registration order, so ties cannot flip between frames when the array is
			 * rebuilt or resorted. Moving the records themselves, each with an array of its own, was a twelfth of the render
			 * thread's frame with a world of panels.
			 */
			struct FSortKey
			{
				int32 RenderPriority = 0;
				float DistToCamera = 0.0f;
				int32 Index = 0;
			};
			TArray<FSortKey> SortedSequence;
			{
				auto InViewPosition = FVector3f(RenderView->ViewMatrices.GetViewOrigin());
				SortedSequence.SetNumUninitialized(RenderSequenceArray.Num());
				for (int32 Index = 0; Index < RenderSequenceArray.Num(); ++Index)
				{
					FWorldSpaceRenderParameterSequence& Item = RenderSequenceArray[Index];
					Item.DistToCamera = FVector3f::DistSquared(InViewPosition, Item.WorldPosition);
					SortedSequence[Index] = FSortKey{ Item.RenderPriority, Item.DistToCamera, Index };
				}
				SortedSequence.Sort([](const FSortKey& A, const FSortKey& B)
				{
					if (A.RenderPriority != B.RenderPriority)
					{
						return A.RenderPriority < B.RenderPriority;
					}
					if (A.DistToCamera != B.DistToCamera)
					{
						return A.DistToCamera > B.DistToCamera;
					}
					return A.Index < B.Index;
				});
			}

			/**
			 * Consecutive mesh primitives are drawn in one pass. A pass each put a render pass, its barriers and a
			 * command list around every canvas, and with a thousand world-space panels that was most of what DreamUI
			 * cost the render thread and the GPU. A post process ends the run, because it reads the target these draw
			 * into; and a run is cut every MaxPrimitivesPerPass primitives, so RDG can still record runs in parallel.
			 * Each primitive's draws are replayed in order, with its own blend depth and depth fade, exactly as its
			 * own pass drew them.
			 *
			 * A pass's primitives collect into one array, with one allocator for their uniform buffers, each primitive's run
			 * of batches ending where FMeshDrawRun says: an array, an allocator and an object of the graph's for every
			 * primitive were made and let go of again for every panel of a world of them, every frame.
			 */
			struct FMeshDrawRun
			{
				int32 EndBatch = 0;
				float BlendDepth = 0.0f;
				int DepthFade = 0;
			};
			struct FPassDraws
			{
				DreamUIRendererLocal::FCollectedMeshBatches Collected;
				TArray<FMeshDrawRun, TInlineAllocator<64>> Runs;
			};
			constexpr int32 MaxPrimitivesPerPass = 64;
			FPassDraws* PendingDraws = nullptr;
			auto FlushPendingDraws = [&]()
			{
				if (PendingDraws == nullptr || PendingDraws->Runs.Num() == 0)
				{
					return;
				}
				auto* PassParameters = GraphBuilder.AllocParameters<FDreamUIWorldRenderPSParameter>();
				PassParameters->SceneDepthTex = SceneDepth;
				PassParameters->RenderTargets[0] = FRenderTargetBinding(RenderTargetTexture, ERenderTargetLoadAction::ELoad);
				GraphBuilder.AddPass(
					RDG_EVENT_NAME("DreamUIRender_WorldSpace"),
					PassParameters,
					ERDGPassFlags::Raster,
					//FRHICommandList&, so RDG may record this pass on a task rather than inline on
					//the render thread -- the more UI draw-calls there are, the longer that
					//serial stretch used to be. Everything below is FRHICommandList API, and it
					//reads only what was collected when the pass was recorded: it calls the
					//renderer's static helpers alone, and holds no pointer to the renderer.
					[Draws = PendingDraws, RenderView, ViewRect, PassParameters
						, SceneDepthTexST = DepthTextureScaleOffset, NumSamples, GammaValue
						, bRenderWireframe, bRenderLit, WireframeMaterialInstance](FRHICommandList& RHICmdList)
					{
						SCOPE_CYCLE_COUNTER(STAT_DreamGUI_RHIRenderMesh);
						FGraphicsPipelineStateInitializer GraphicsPSOInit;
						RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
						RHICmdList.SetViewport(ViewRect.Min.X, ViewRect.Min.Y, 0.0f, ViewRect.Max.X, ViewRect.Max.Y, 1.0f);
						// See TMaterialShadersCache, FMaterialPipelineKey and FDreamUIBuiltInDrawCache: kept across this pass's draws.
						DreamUIRendererLocal::TMaterialShadersCache<FDreamUIWorldRenderPS> WorldShaders;
						DreamUIRendererLocal::TMaterialShadersCache<FDreamUIWorldRenderDepthFadePS> DepthFadeShaders;
						DreamUIRendererLocal::FMaterialPipelineKey PipelineKey;
						FDreamUIBuiltInDrawCache BuiltInCache;
						// The scene depth the pass's draws through a material blend against, and its sampler: one uniform buffer for
						// the pass, made at the first of them (FDreamUIWorldRenderPS::SetDepthBlendParameter).
						TUniformBufferRef<FDreamUIWorldRenderDepthTexUB> DepthTextureBuffer;

						const TArray<FDreamUIMeshBatchContainer>& MeshBatchArray = Draws->Collected.Batches;
						int32 FirstOfRun = 0;
						for (const FMeshDrawRun& Run : Draws->Runs)
						{
							const float BlendDepth = Run.BlendDepth;
							const int DepthFade = Run.DepthFade;
							const int32 EndOfRun = Run.EndBatch;
							for (int MeshIndex = FirstOfRun; MeshIndex < EndOfRun; MeshIndex++)
							{
								auto& MeshBatchContainer = MeshBatchArray[MeshIndex];
								const FMeshBatch& Mesh = MeshBatchContainer.Mesh;

								auto DoRender = [&](bool bWireframe)
								{
									if (!bWireframe && MeshBatchContainer.bBuiltIn)
									{
										DrawBuiltInBatch(RHICmdList, GraphicsPSOInit, *RenderView, ViewRect, MeshBatchContainer
											, NumSamples, GammaValue, false
											, true, BlendDepth, DepthFade, SceneDepthTexST, PassParameters->SceneDepthTex->GetRHI(), &BuiltInCache);
										// It set a pipeline of its own.
										PipelineKey.bSet = false;
										return;
									}
									auto MaterialRenderProxy = (bWireframe ? WireframeMaterialInstance : Mesh.MaterialRenderProxy);
									if (!MaterialRenderProxy)return;
									auto Material = MaterialRenderProxy->GetMaterialNoFallback(RenderView->GetFeatureLevel());//why not use "GetIncompleteMaterialWithFallback" here? because fallback material can't render with DreamUIRenderer
									if (!Material)return;
									// Collected with a primitive uniform buffer, and the view's made, whenever it can be drawn through a material.
									if (!ensure(Mesh.Elements[0].PrimitiveUniformBufferResource != nullptr && RenderView->ViewUniformBuffer.IsValid()))
									{
										return;
									}

									if (DepthFade <= 0)
									{
										if (WorldShaders.Find(*Material))
										{
											const TShaderRef<FDreamUIScreenRenderVS>& VertexShader = WorldShaders.VertexShader;
											const TShaderRef<FDreamUIWorldRenderPS>& PixelShader = WorldShaders.PixelShader;

											if (!PipelineKey.Matches(Material, bWireframe, Mesh.ReverseCulling, false))
											{
												FDreamUIRenderer::SetGraphicPipelineState_BlendDepthStencilRasterize(RenderView->GetFeatureLevel(), GraphicsPSOInit, Material->GetBlendMode()
												, Material->IsWireframe() || bWireframe, Material->IsTwoSided(), Material->ShouldDisableDepthTest(), false, Mesh.ReverseCulling
												);
												GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GetDreamUIMeshVertexDeclaration();
												GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
												GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
												GraphicsPSOInit.PrimitiveType = EPrimitiveType::PT_TriangleList;
												GraphicsPSOInit.NumSamples = NumSamples;
												SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0, EApplyRendertargetOption::CheckApply);
												PipelineKey.Remember(Material, bWireframe, Mesh.ReverseCulling, false);
												BuiltInCache.Pipeline.bSet = false;
											}

											VertexShader->SetMaterialShaderParameters(RHICmdList, *RenderView, MaterialRenderProxy, Material, Mesh.Elements[0].PrimitiveUniformBufferResource, MeshBatchContainer.GetBuiltIn().RenderLayerTableRHI.GetReference(), MeshBatchContainer.GetBuiltIn().WidgetDataTextureRHI.GetReference());
											PixelShader->SetMaterialShaderParameters(RHICmdList, *RenderView, MaterialRenderProxy, Material, Mesh.Elements[0].PrimitiveUniformBufferResource);
											PixelShader->SetDepthBlendParameter(RHICmdList, BlendDepth, SceneDepthTexST, PassParameters->SceneDepthTex->GetRHI(), DepthTextureBuffer);
											PixelShader->SetGammaValue(RHICmdList, GammaValue);

											RHICmdList.SetStreamSource(0, MeshBatchContainer.GetVertexBuffer(), 0);
											RHICmdList.DrawIndexedPrimitive(MeshBatchContainer.GetIndexBuffer(), 0, 0, MeshBatchContainer.NumVerts, 0, Mesh.GetNumPrimitives(), 1);
										}
									}
									else
									{
										if (DepthFadeShaders.Find(*Material))
										{
											const TShaderRef<FDreamUIScreenRenderVS>& VertexShader = DepthFadeShaders.VertexShader;
											const TShaderRef<FDreamUIWorldRenderDepthFadePS>& PixelShader = DepthFadeShaders.PixelShader;

											if (!PipelineKey.Matches(Material, bWireframe, Mesh.ReverseCulling, true))
											{
												FDreamUIRenderer::SetGraphicPipelineState_BlendDepthStencilRasterize(RenderView->GetFeatureLevel(), GraphicsPSOInit, Material->GetBlendMode()
												, Material->IsWireframe() || bWireframe, Material->IsTwoSided(), Material->ShouldDisableDepthTest(), false, Mesh.ReverseCulling
												);
												GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GetDreamUIMeshVertexDeclaration();
												GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
												GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
												GraphicsPSOInit.PrimitiveType = EPrimitiveType::PT_TriangleList;
												GraphicsPSOInit.NumSamples = NumSamples;
												SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0, EApplyRendertargetOption::CheckApply);
												PipelineKey.Remember(Material, bWireframe, Mesh.ReverseCulling, true);
												BuiltInCache.Pipeline.bSet = false;
											}

											VertexShader->SetMaterialShaderParameters(RHICmdList, *RenderView, MaterialRenderProxy, Material, Mesh.Elements[0].PrimitiveUniformBufferResource, MeshBatchContainer.GetBuiltIn().RenderLayerTableRHI.GetReference(), MeshBatchContainer.GetBuiltIn().WidgetDataTextureRHI.GetReference());
											PixelShader->SetMaterialShaderParameters(RHICmdList, *RenderView, MaterialRenderProxy, Material, Mesh.Elements[0].PrimitiveUniformBufferResource);
											PixelShader->SetDepthBlendParameter(RHICmdList, BlendDepth, SceneDepthTexST, PassParameters->SceneDepthTex->GetRHI(), DepthTextureBuffer);
											PixelShader->SetDepthFadeParameter(RHICmdList, DepthFade);
											PixelShader->SetGammaValue(RHICmdList, GammaValue);

											RHICmdList.SetStreamSource(0, MeshBatchContainer.GetVertexBuffer(), 0);
											RHICmdList.DrawIndexedPrimitive(MeshBatchContainer.GetIndexBuffer(), 0, 0, MeshBatchContainer.NumVerts, 0, Mesh.GetNumPrimitives(), 1);
										}
									}
								};
								if (bRenderLit)
								{
									DoRender(false);
								}
								if (bRenderWireframe)
								{
									DoRender(true);
								}
							}
							FirstOfRun = EndOfRun;
						}
					});
				PendingDraws = nullptr;
			};

			for (const FSortKey& SortKey : SortedSequence)
			{
				auto& RenderSequenceItem = RenderSequenceArray[SortKey.Index];
				for (auto& RenderPrimitiveItem : RenderSequenceItem.RenderDataArray)
				{
					switch (RenderPrimitiveItem.Type)
					{
					case EDreamUIRendererPrimitiveType::PostProcess://render post process
						{
							// What is drawn so far goes in first: the post process reads it.
							FlushPendingDraws();
							for (int i = 0; i < RenderPrimitiveItem.Sections.Num(); i++)
							{
								// A reference, held for the call: the passes added below capture the proxy raw and
								// only run at GraphBuilder.Execute(), so it has to outlive this loop. The section's
								// own reference covers that -- nothing can drop it until this render command ends.
								if (auto Primitive = RenderPrimitiveItem.Primitive->DreamUI_GetPostProcessElement(RenderPrimitiveItem.Sections[i].SectionPointer))
								{
									SCOPE_CYCLE_COUNTER(STAT_DreamGUI_RHIRenderPostProcess);
									TRACE_CPUPROFILER_EVENT_SCOPE(DreamUI_RecordPostProcess);
									Primitive->OnRenderPostProcess_RenderThread(
										GraphBuilder,
										SceneDepth,
										this,
										ScreenColorRenderTargetTexture,
										GlobalShaderMap,
										ViewProjectionMatrix,
										true,
										false,
										RenderSequenceItem.BlendDepth,
										RenderSequenceItem.DepthFade,
										ViewRect,
										DepthTextureScaleOffset,
										ColorTextureScaleOffset
									);
								}
							}
						}
						break;
					case EDreamUIRendererPrimitiveType::Mesh://render mesh
						{
							// Collected now, on the render thread while the pass is recorded, and not when it
							// runs: the pass may run on a task after this function has returned, and the
							// primitive -- a scene proxy -- may be gone by then, and every virtual call into
							// it with it. The graph owns what is collected, uniform buffers and all, until
							// its passes have run.
							if (PendingDraws == nullptr)
							{
								PendingDraws = GraphBuilder.AllocObject<FPassDraws>();
								// Mostly a section or two a primitive: room for the pass, grown by doubling past it.
								PendingDraws->Collected.Batches.Reserve(MaxPrimitivesPerPass * 2);
							}
							DreamUIRendererLocal::FCollectedMeshBatches& Collected = PendingDraws->Collected;
							const int32 FirstBatch = Collected.Batches.Num();
							{
								TRACE_CPUPROFILER_EVENT_SCOPE(DreamUI_CollectMeshBatches);
								FDreamUIMeshElementCollector MeshCollector(RenderView->GetFeatureLevel(), Collected.Allocator, GraphBuilder.RHICmdList);
								RenderPrimitiveItem.Primitive->DreamUI_GetMeshElements(*RenderView->Family, MeshCollector, RenderPrimitiveItem, Collected.Batches);
							}
							DreamUIRendererLocal::CountCollected(Collected, FirstBatch);
							if (DreamUIRendererLocal::DrawsThroughAMaterial(Collected, bRenderWireframe, FirstBatch))
							{
								MakeViewUniformBuffer();
							}
							PendingDraws->Runs.Add(FMeshDrawRun{ Collected.Batches.Num(), RenderSequenceItem.BlendDepth, RenderSequenceItem.DepthFade });
							if (PendingDraws->Runs.Num() >= MaxPrimitivesPerPass)
							{
								FlushPendingDraws();
							}
						}break;
					}
				}
			}
			FlushPendingDraws();
#if WITH_EDITOR
			RenderGizmoMesh_RenderThread(WorldSpaceGizmoMeshArray, GraphBuilder, RenderView, ViewRect, NumSamples, RenderTargetTexture);
#endif
		}
	}
}

void FDreamUIRenderer::RecordScreenSpace_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& InView, FRecordTargets& Targets)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(DreamUI_RecordScreenSpace);
	//Render screen space
	if (!((ScreenSpaceRenderParameter.PrimitiveArray.Num() > 0
#if WITH_EDITOR
		|| ScreenSpaceGizmoMeshArray.Num() > 0
#endif
		)
		&& Targets.bIsMainViewport))
	{
		return;
	}
	if (ScreenSpaceRenderParameter.bNeedSortRenderPriority)
	{
		ScreenSpaceRenderParameter.bNeedSortRenderPriority = false;
		SortScreenSpacePrimitiveRenderPriority_RenderThread();
	}
#if WITH_EDITOR
	if (RendererType == EDreamUIRendererType::RenderTarget)
	{

	}
	else
	{
		if (!RenderThreadViewParameter.bCanRenderScreenSpace)
		{
			return;
		}
		if (RenderThreadViewParameter.bIsPlaying)
		{
			if (!InView.bIsGameView)
			{
				return;
			}
		}
		else
		{
			return;
		}
	}
#endif
	FScreenSpaceLayerView SharedView;
	SharedView.ViewOrigin = RenderThreadViewParameter.ViewOrigin;
	SharedView.ViewRotationMatrix = RenderThreadViewParameter.ViewRotationMatrix;
	SharedView.ProjectionMatrix = RenderThreadViewParameter.ProjectionMatrix;
	SharedView.ViewProjectionMatrix = RenderThreadViewParameter.ViewProjectionMatrix;
	SharedView.bEnableDepthTest = RenderThreadViewParameter.bEnableDepthTest;
	SharedView.ScreenSpaceRenderScale = RenderThreadViewParameter.ScreenSpaceRenderScale;

	const TArray<FScreenSpacePlayerPartView>& PlayerParts = RenderThreadViewParameter.PlayerParts;
	// PostRenderView is called in family order after the scene has finished every view. Draw the shared layer
	// after the last player's layer, once across the full viewport, as its layout and hit testing already do.
	// Stereo retains its per-eye projection and rect; render-target canvases have their own single view.
	const bool bSharedAcrossSplitScreen = RendererType != EDreamUIRendererType::RenderTarget
		&& InView.Family->Views.Num() > 1
		&& !InView.Family->Views.ContainsByPredicate([](const FSceneView* View)
		{
			return View->StereoPass != EStereoscopicPass::eSSP_FULL;
		});
	const bool bDrawSharedLayer = !bSharedAcrossSplitScreen || InView.Family->Views.Last() == &InView;
	FRecordTargets SharedTargets = Targets;
	if (bSharedAcrossSplitScreen)
	{
		// eSSP_FULL colour/depth sampling already maps the whole family target. Keep that target for MSAA
		// and render scale as well; only the shared drawing's viewport changes.
		SharedTargets.ViewRect = FIntRect(FIntPoint::ZeroValue, Targets.ScreenColorRenderTargetTexture->GetSizeXY());
	}
	if (PlayerParts.Num() == 0)
	{
		if (bDrawSharedLayer)
		{
			RecordScreenSpaceLayer_RenderThread(GraphBuilder, InView, SharedTargets, SharedView
				, [](IDreamUIRendererPrimitive*) { return true; }, /*bInDrawGizmos*/true, /*bInAllowRenderScale*/true);
			// The final MSAA resolve must include every pixel the shared layer just drew.
			Targets.ViewRect = SharedTargets.ViewRect;
		}
		return;
	}

	/**
	 * A split screen, as UMG draws one: each player's own layer in that player's view only, then the shared layer over
	 * the whole viewport once all players have been drawn (SGameLayerManager keeps a layer per local player, laid out
	 * over the player's part of the viewport and
	 * clipped to it, in EGameLayerOrder::Player, under the shared EGameLayerOrder::Viewport). A view here is one player's
	 * -- ULocalPlayer::CalcSceneView names it by the player's controller id in FSceneView::PlayerIndex, which is what a
	 * part says it is drawn in -- and its rect is that player's part, so a part's own view, made from a canvas the size of
	 * the part, maps the canvas onto exactly that rect. A part is drawn at full resolution: the render-scale composite
	 * covers the whole target, not a player's rect of it.
	 */
	TArray<FObjectKey, TInlineAllocator<4>> PartRoots;
	for (const FScreenSpacePlayerPartView& Part : PlayerParts)
	{
		PartRoots.Add(Part.RootKey);
	}
	const TMap<IDreamUIRendererPrimitive*, FObjectKey>& RootKeys = ScreenSpaceRenderParameter.PrimitiveRootKeys;
	for (const FScreenSpacePlayerPartView& Part : PlayerParts)
	{
		if (Part.ViewPlayerIndex != InView.PlayerIndex)
		{
			continue;
		}
		RecordScreenSpaceLayer_RenderThread(GraphBuilder, InView, Targets, Part.View
			, [&RootKeys, &Part](IDreamUIRendererPrimitive* InPrimitive)
			{
				const FObjectKey* RootKey = RootKeys.Find(InPrimitive);
				return RootKey != nullptr && *RootKey == Part.RootKey;
			}
			, /*bInDrawGizmos*/false, /*bInAllowRenderScale*/false);
	}
	if (bDrawSharedLayer)
	{
		RecordScreenSpaceLayer_RenderThread(GraphBuilder, InView, SharedTargets, SharedView
			, [&RootKeys, &PartRoots](IDreamUIRendererPrimitive* InPrimitive)
			{
				const FObjectKey* RootKey = RootKeys.Find(InPrimitive);
				return RootKey == nullptr || !PartRoots.Contains(*RootKey);
			}
			, /*bInDrawGizmos*/true, /*bInAllowRenderScale*/true);
		// Player layers used their own rects; the final MSAA resolve also includes the shared full-viewport draw.
		Targets.ViewRect = SharedTargets.ViewRect;
	}
}

void FDreamUIRenderer::RecordScreenSpaceLayer_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& InView, FRecordTargets& Targets
	, const FScreenSpaceLayerView& InLayer, TFunctionRef<bool(IDreamUIRendererPrimitive*)> InIsInLayer
	, bool bInDrawGizmos, bool bInAllowRenderScale)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(DreamUI_RecordScreenSpaceLayer);
	// The recording's targets, by the names the stages shared when they were one function.
	FRHICommandListImmediate& RHICmdList = GraphBuilder.RHICmdList;
	bool& bRenderWireframe = Targets.bRenderWireframe;
	bool& bRenderLit = Targets.bRenderLit;
	FMaterialRenderProxy*& WireframeMaterialInstance = Targets.WireframeMaterialInstance;
	FTextureRHIRef& ScreenColorRenderTargetTexture = Targets.ScreenColorRenderTargetTexture;
	uint8& NumSamples = Targets.NumSamples;
	FIntRect& ViewRect = Targets.ViewRect;
	FVector4f& DepthTextureScaleOffset = Targets.DepthTextureScaleOffset;
	FVector4f& ColorTextureScaleOffset = Targets.ColorTextureScaleOffset;
	const FRDGTextureRef SceneDepth = Targets.SceneDepth;
	const FRDGTextureRef RenderTargetTexture = Targets.RenderTargetTexture;
	const float GammaValue = Targets.GammaValue;

	TRefCountPtr<IPooledRenderTarget> DreamUIScreenSpaceDepthTexture = nullptr;
	FRDGTextureRef DreamUIScreenSpaceDepthRDGTexture = nullptr;
	if (InLayer.bEnableDepthTest)
	{
		// Allow UAV depth?
		const ETextureCreateFlags textureUAVCreateFlags = GRHISupportsDepthUAV ? TexCreate_UAV : TexCreate_None;

		// Create a texture to store the resolved scene depth, and a render-targetable surface to hold the unresolved scene depth.
		FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(
			InView.Family->RenderTarget->GetRenderTargetTexture()->GetSizeXY()
			, EPixelFormat::PF_DepthStencil
			, FClearValueBinding::DepthFar
			, TexCreate_None, TexCreate_DepthStencilTargetable | textureUAVCreateFlags
			, false
		));
		Desc.NumSamples = NumSamples;
		Desc.ArraySize = 1;
		Desc.Flags |= TexCreate_Memoryless;

		GRenderTargetPool.FindFreeElement(RHICmdList, Desc, DreamUIScreenSpaceDepthTexture, TEXT("DreamUIScreenSpaceDepthTexture"));
		//register the pool element, not its RHI texture: that is what keeps GRenderTargetPool from
		//handing this depth buffer to another view before the passes recorded here have executed
		DreamUIScreenSpaceDepthRDGTexture = GraphBuilder.RegisterExternalTexture(DreamUIScreenSpaceDepthTexture, TEXT("DreamUIScreenSpaceDepthTexture"));
	}

	//use a copied view. 
	//NOTE!!! world-space and screen-space must use different 'RenderView' (actually different ViewUniformBuffer), because RDG is not immediately execute. 
	//if use same one, after world-space when modify 'RenderView' for screen-space, the screen-space ViewUniformBuffer will be applyed to world-space
	//owned by the graph for the same reason as the world-space copy above: a pass must not free what a parallel pass still reads
	FSceneView* RenderView = GraphBuilder.AllocObject<FSceneView>(InView);
	auto GlobalShaderMap = GetGlobalShaderMap(RenderView->GetFeatureLevel());

	RenderView->SceneViewInitOptions.ViewOrigin = InLayer.ViewOrigin;
	RenderView->SceneViewInitOptions.ViewRotationMatrix = InLayer.ViewRotationMatrix;
	RenderView->SceneViewInitOptions.ProjectionMatrix = InLayer.ProjectionMatrix;
	RenderView->ViewMatrices = FViewMatrices(RenderView->SceneViewInitOptions);
	if (RenderThreadViewParameter.bFrustumCulling)
	{
		RenderView->UpdateProjectionMatrix(InLayer.ProjectionMatrix);//this is mainly for ViewFrustum
	}

	//collect render primitive to a sequence.
	//NOTE: this happens BEFORE the view uniform buffer is built, because the render-scale decision
	//below depends on what is in the sequence and the uniform buffer depends on the decision.
	//Collection itself only needs the matrices, which are already set.
	FDreamUIPrimitiveDataArray RenderSequenceArray;
	for (auto Primitive : ScreenSpaceRenderParameter.PrimitiveArray)
	{
		if (Primitive->DreamUI_CanRender() && InIsInLayer(Primitive))
		{
			auto WorldBounds = Primitive->DreamUI_GetWorldBounds();
			if (!RenderThreadViewParameter.bFrustumCulling
				|| (RenderThreadViewParameter.bFrustumCulling && RenderView->GetCullingFrustum().IntersectBox(WorldBounds.Origin, WorldBounds.BoxExtent))//simple View Frustum Culling
				)
			{
				Primitive->DreamUI_CollectRenderData(RenderSequenceArray);
			}
		}
	}

	/**
	 * Render scale: draw the screen-space UI into a smaller, transparent target and composite that
	 * up over the real one. The point is fill rate -- on a phone or a low-end console profile a
	 * full-screen UI pass is expensive out of proportion to how much detail it needs.
	 *
	 * These cases switch it off:
	 *
	 * - MSAA. The multisampled path already redirects the whole UI through its own target and
	 *   resolves at the end; two redirections would need the resolve and the upscale ordered
	 *   against each other, and picking one anti-aliasing scheme is the honest answer anyway.
	 * - Depth testing. The depth target is sized from the view family's render target and shared
	 *   with the colour binding; RDG wants a raster pass's attachments to agree, so a scaled colour
	 *   target would need a scaled depth target and a rescaled depth comparison with it.
	 * - Any screen-space post process. A post process READS the scene colour behind it. Scaled, the
	 *   UI it should be reading is in the small target while the scene is in the big one, so it
	 *   would sample the wrong image and land in the wrong place in the z-order.
	 * - A blend that needs the original destination. Multiply/Modulate and AlphaHoldout cannot affect
	 *   the scene through a transparent target; a material's Additive also attenuates destination alpha.
	 *
	 * Scale 1 (the default) skips all of this and leaves the path exactly as it was.
	 */
	TRefCountPtr<IPooledRenderTarget> RenderScaleTarget;
	FRDGTextureRef ScreenSpaceRenderTargetTexture = RenderTargetTexture;
	const FIntRect UnscaledScreenSpaceViewRect = ViewRect;
	TArray<DreamUIRendererLocal::FCollectedMeshBatches*, TInlineAllocator<1>> CollectedForScale;
	if (bInAllowRenderScale
		&& InLayer.ScreenSpaceRenderScale < 1.0f
		&& NumSamples <= 1
		&& !InLayer.bEnableDepthTest
		&& ScreenColorRenderTargetTexture != nullptr)
	{
		bool bAnyPostProcess = false;
		for (const auto& RenderSequenceItem : RenderSequenceArray)
		{
			if (RenderSequenceItem.Type == EDreamUIRendererPrimitiveType::PostProcess)
			{
				bAnyPostProcess = true;
				break;
			}
		}
		bool bNeedsOriginalDestination = false;
		if (!bAnyPostProcess)
		{
			// Inspect the batches that will actually draw, including a material's current blend override. Keep
			// the collected state for the pass below: a scale decision must not collect the same mesh twice.
			CollectedForScale.SetNumZeroed(RenderSequenceArray.Num());
			for (int32 Index = 0; Index < RenderSequenceArray.Num(); ++Index)
			{
				const FDreamUIPrimitiveDataContainer& Item = RenderSequenceArray[Index];
				auto* Collected = GraphBuilder.AllocObject<DreamUIRendererLocal::FCollectedMeshBatches>();
				CollectedForScale[Index] = Collected;
				{
					TRACE_CPUPROFILER_EVENT_SCOPE(DreamUI_CollectMeshBatches);
					FDreamUIMeshElementCollector MeshCollector(RenderView->GetFeatureLevel(), Collected->Allocator, GraphBuilder.RHICmdList);
					Item.Primitive->DreamUI_GetMeshElements(*RenderView->Family, MeshCollector, Item, Collected->Batches);
				}
				if (bRenderLit && DreamUIRendererLocal::UsesOriginalDestination(*Collected, RenderView->GetFeatureLevel()))
				{
					bNeedsOriginalDestination = true;
					break;
				}
			}
		}
		if (!bAnyPostProcess && !bNeedsOriginalDestination)
		{
			float AppliedScale = 1.0f;
			const FIntPoint ScaledSize = CalculateRenderScaledSize(ViewRect.Size(), InLayer.ScreenSpaceRenderScale, AppliedScale);
			if (ScaledSize != ViewRect.Size())
			{
				FPooledRenderTargetDesc ScaleDesc(FPooledRenderTargetDesc::Create2DDesc(
					ScaledSize, ScreenColorRenderTargetTexture->GetFormat(), FClearValueBinding::Transparent
					, TexCreate_None, TexCreate_RenderTargetable | TexCreate_ShaderResource, false));
				GRenderTargetPool.FindFreeElement(RHICmdList, ScaleDesc, RenderScaleTarget, TEXT("DreamUI_ScreenSpaceRenderScale"));
				if (RenderScaleTarget.IsValid())
				{
					//register the pool element so the graph holds it past this function, as everywhere else here
					ScreenSpaceRenderTargetTexture = GraphBuilder.RegisterExternalTexture(RenderScaleTarget, TEXT("DreamUI_ScreenSpaceRenderScale"));
					//transparent to start with: only UI goes in here, and the composite at the end
					//is what puts it over the scene
					AddClearRenderTargetPass(GraphBuilder, ScreenSpaceRenderTargetTexture, FLinearColor::Transparent);
					//everything below draws into the small target, so the view is the small target
					ViewRect = FIntRect(FIntPoint::ZeroValue, ScaledSize);
				}
			}
		}
	}

	// The view's uniform buffer is what a material reads; the built-in shader reads none of it. It is made once a batch
	// that can be drawn through a material has been collected, and a stage that collects none makes none. The copy
	// starts without the view's own.
	RenderView->ViewUniformBuffer.SafeRelease();
	auto MakeViewUniformBuffer = [RenderView, &ViewRect]()
	{
		if (RenderView->ViewUniformBuffer.IsValid())
		{
			return;
		}
		TRACE_CPUPROFILER_EVENT_SCOPE(DreamUI_ViewUniformBuffer);
		FViewUniformShaderParameters ViewUniformShaderParameters;
		RenderView->SetupCommonViewUniformBufferParameters(
			ViewUniformShaderParameters,
			ViewRect.Size(),
			1,
			ViewRect,
			RenderView->ViewMatrices,
			FViewMatrices()
		);
		RenderView->ViewUniformBuffer = TUniformBufferRef<FViewUniformShaderParameters>::CreateUniformBufferImmediate(ViewUniformShaderParameters, UniformBuffer_SingleFrame);
	};

	bool bIsDepthStencilCleared = false;
	bool bIsRenderTarget = RendererType == EDreamUIRendererType::RenderTarget;
	for (int32 SequenceIndex = 0; SequenceIndex < RenderSequenceArray.Num(); ++SequenceIndex)
	{
		auto& RenderSequenceItem = RenderSequenceArray[SequenceIndex];
		switch (RenderSequenceItem.Type)
		{
		case EDreamUIRendererPrimitiveType::PostProcess://render post process
		{
			for (int i = 0; i < RenderSequenceItem.Sections.Num(); i++)
			{
				// See the world-space path above: the reference is what keeps the proxy alive until the
				// passes this adds have actually executed.
				if (auto Primitive = RenderSequenceItem.Primitive->DreamUI_GetPostProcessElement(RenderSequenceItem.Sections[i].SectionPointer))
				{
					SCOPE_CYCLE_COUNTER(STAT_DreamGUI_RHIRenderPostProcess);
					TRACE_CPUPROFILER_EVENT_SCOPE(DreamUI_RecordPostProcess);
					Primitive->OnRenderPostProcess_RenderThread(
						GraphBuilder,
						SceneDepth,
						this,
						ScreenColorRenderTargetTexture,
						GlobalShaderMap,
						InLayer.ViewProjectionMatrix,
						/*IsWorldSpace*/false,
						/*IsRenderToRenderTarget*/bIsRenderTarget,
						/*BlendDepthForWorld*/0.0f,//actually this value will not work because 'IsWorldSpace' is false
						/*BlendDepthForWorld*/0.0f,//actually this value will not work because 'IsWorldSpace' is false
						ViewRect,
						DepthTextureScaleOffset,
						ColorTextureScaleOffset
					);
				}
			}
		}
		break;
		case EDreamUIRendererPrimitiveType::Mesh:
		{
			auto* PassParameters = GraphBuilder.AllocParameters<FRenderTargetParameters>();
			//the scaled target when render scale is on, the real one otherwise
			PassParameters->RenderTargets[0] = FRenderTargetBinding(ScreenSpaceRenderTargetTexture, ERenderTargetLoadAction::ELoad);
			if (DreamUIScreenSpaceDepthRDGTexture != nullptr)
			{
				if (bIsDepthStencilCleared)
				{
					PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(DreamUIScreenSpaceDepthRDGTexture, ERenderTargetLoadAction::ENoAction, ERenderTargetLoadAction::ENoAction, FExclusiveDepthStencil::DepthWrite_StencilWrite);
				}
				else
				{
					bIsDepthStencilCleared = true;//only clear depth stencil when first use depth texture
					PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(DreamUIScreenSpaceDepthRDGTexture, ERenderTargetLoadAction::EClear, ERenderTargetLoadAction::EClear, FExclusiveDepthStencil::DepthWrite_StencilWrite);
				}
			}
			// Collected while the pass is recorded, not when it runs: see the world-space mesh pass above.
			auto* Collected = CollectedForScale.IsValidIndex(SequenceIndex) ? CollectedForScale[SequenceIndex] : nullptr;
			if (Collected == nullptr)
			{
				Collected = GraphBuilder.AllocObject<DreamUIRendererLocal::FCollectedMeshBatches>();
				TRACE_CPUPROFILER_EVENT_SCOPE(DreamUI_CollectMeshBatches);
				FDreamUIMeshElementCollector MeshCollector(RenderView->GetFeatureLevel(), Collected->Allocator, GraphBuilder.RHICmdList);
				RenderSequenceItem.Primitive->DreamUI_GetMeshElements(*RenderView->Family, MeshCollector, RenderSequenceItem, Collected->Batches);
			}
			DreamUIRendererLocal::CountCollected(*Collected);
			if (DreamUIRendererLocal::DrawsThroughAMaterial(*Collected, bRenderWireframe))
			{
				MakeViewUniformBuffer();
			}
			GraphBuilder.AddPass(
				RDG_EVENT_NAME("DreamUIRender_ScreenSpace"),
				PassParameters,
				ERDGPassFlags::Raster,
				//FRHICommandList&: see the note on the world-space mesh pass above
				[Collected, RenderView, ViewRect, SceneDepthTexST = DepthTextureScaleOffset
					, NumSamples, ValidDepth = DreamUIScreenSpaceDepthRDGTexture != nullptr, GammaValue
					, bRenderLit, bRenderWireframe, WireframeMaterialInstance](FRHICommandList& RHICmdList)
				{
					SCOPE_CYCLE_COUNTER(STAT_DreamGUI_RHIRenderMesh);
					FGraphicsPipelineStateInitializer GraphicsPSOInit;
					RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
					RHICmdList.SetViewport(ViewRect.Min.X, ViewRect.Min.Y, 0.0f, ViewRect.Max.X, ViewRect.Max.Y, 1.0f);
					const TArray<FDreamUIMeshBatchContainer>& MeshBatchArray = Collected->Batches;
					// See FDreamUIBuiltInDrawCache: kept across this pass's draws.
					FDreamUIBuiltInDrawCache BuiltInCache;

					for (int MeshIndex = 0; MeshIndex < MeshBatchArray.Num(); MeshIndex++)
					{
						auto& MeshBatchContainer = MeshBatchArray[MeshIndex];
						const FMeshBatch& Mesh = MeshBatchContainer.Mesh;

						auto DoRender = [&](bool bWireframe)
						{
							const bool bDump = CVarDreamGUIDumpMaterialDraws.GetValueOnRenderThread() != 0;
							if (!bWireframe && MeshBatchContainer.bBuiltIn)
							{
								if (bDump)
								{
									UE_LOG(LogDreamGUIRenderer, Display, TEXT("[DumpMaterialDraws] built-in batch, %d verts"), MeshBatchContainer.NumVerts);
								}
								DrawBuiltInBatch(RHICmdList, GraphicsPSOInit, *RenderView, ViewRect, MeshBatchContainer
									, NumSamples, GammaValue, ValidDepth
									, false, 0.0f, 0, SceneDepthTexST, nullptr, &BuiltInCache);
								return;
							}
							auto MaterialRenderProxy = (bWireframe ? WireframeMaterialInstance : Mesh.MaterialRenderProxy);
							if (!MaterialRenderProxy)
							{
								if (bDump) { UE_LOG(LogDreamGUIRenderer, Display, TEXT("[DumpMaterialDraws] EXIT no proxy")); }
								return;
							}
							auto Material = MaterialRenderProxy->GetMaterialNoFallback(RenderView->GetFeatureLevel());//why not use "GetIncompleteMaterialWithFallback" here? because fallback material cann't render with DreamUIRenderer
							if (!Material)
							{
								if (bDump)
								{
									UE_LOG(LogDreamGUIRenderer, Display, TEXT("[DumpMaterialDraws] EXIT no material (shader map not ready?) proxy=%s"),
										*MaterialRenderProxy->GetMaterialName());
								}
								return;
							}
							
							// Collected with a primitive uniform buffer, and the view's made, whenever it can be drawn through a material.
							if (!ensure(Mesh.Elements[0].PrimitiveUniformBufferResource != nullptr && RenderView->ViewUniformBuffer.IsValid()))
							{
								return;
							}
							FMaterialShaderTypes ShaderTypes;
							ShaderTypes.AddShaderType<FDreamUIScreenRenderVS>();
							ShaderTypes.AddShaderType<FDreamUIScreenRenderPS>();
							FMaterialShaders Shaders;
							const bool bGotShaders = Material->TryGetShaders(ShaderTypes, nullptr, Shaders);
							if (bDump)
							{
								UE_LOG(LogDreamGUIRenderer, Display, TEXT("[DumpMaterialDraws] material=%s verts=%d prims=%d shaders=%s"),
									*Material->GetFriendlyName(), MeshBatchContainer.NumVerts,
									Mesh.Elements.Num() > 0 ? Mesh.Elements[0].NumPrimitives : -1,
									bGotShaders ? TEXT("OK") : TEXT("MISSING"));
							}
							if (bGotShaders)
							{
								TShaderRef<FDreamUIScreenRenderVS> VertexShader;
								TShaderRef<FDreamUIScreenRenderPS> PixelShader;
								Shaders.TryGetVertexShader(VertexShader);
								Shaders.TryGetPixelShader(PixelShader);

								FDreamUIRenderer::SetGraphicPipelineState_BlendDepthStencilRasterize(RenderView->GetFeatureLevel(), GraphicsPSOInit, Material->GetBlendMode()
									, Material->IsWireframe() || bWireframe, Material->IsTwoSided(), Material->ShouldDisableDepthTest(), ValidDepth, Mesh.ReverseCulling
								);

								GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GetDreamUIMeshVertexDeclaration();
								GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
								GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
								GraphicsPSOInit.PrimitiveType = EPrimitiveType::PT_TriangleList;
								GraphicsPSOInit.NumSamples = NumSamples;
								SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0, EApplyRendertargetOption::CheckApply);
								// The built-in draws' pipeline is no longer the one bound, as in the world-space pass: the next of them
								// sets its own again. Kept, the cache let it draw through this material's shaders -- a text after a
								// material-drawn image went blank or boxed, and the RHI ensured on parameters set for an unbound shader.
								BuiltInCache.Pipeline.bSet = false;

								VertexShader->SetMaterialShaderParameters(RHICmdList, *RenderView, MaterialRenderProxy, Material, Mesh.Elements[0].PrimitiveUniformBufferResource, MeshBatchContainer.GetBuiltIn().RenderLayerTableRHI.GetReference(), MeshBatchContainer.GetBuiltIn().WidgetDataTextureRHI.GetReference());
								PixelShader->SetMaterialShaderParameters(RHICmdList, *RenderView, MaterialRenderProxy, Material, Mesh.Elements[0].PrimitiveUniformBufferResource);
								PixelShader->SetGammaValue(RHICmdList, GammaValue);

								RHICmdList.SetStreamSource(0, MeshBatchContainer.GetVertexBuffer(), 0);
								RHICmdList.DrawIndexedPrimitive(MeshBatchContainer.GetIndexBuffer(), 0, 0, MeshBatchContainer.NumVerts, 0, Mesh.Elements[0].NumPrimitives, Mesh.Elements[0].NumInstances);
							}
						};
						if (bRenderLit)
						{
							DoRender(false);
						}
						if (bRenderWireframe)
						{
							DoRender(true);
						}
					}
				});
		}break;
		}
	}

#if WITH_EDITOR
	if (bInDrawGizmos)
	{
		RenderGizmoMesh_RenderThread(ScreenSpaceGizmoMeshArray, GraphBuilder, RenderView, ViewRect, NumSamples, ScreenSpaceRenderTargetTexture);
	}
#endif

	if (RenderScaleTarget.IsValid())
	{
		//composite the scaled UI up over the real target. Bilinear, and premultiplied-over with a
		//blend alpha of 1, which is the same composite the UI would have done straight onto the
		//target -- only once, at the end, from a smaller image.
		// The small target already contains premultiplied RGB (including additive RGB with zero alpha).
		DreamUIRendererLocal::AddCopyTargetPass(GraphBuilder, GlobalShaderMap, ScreenSpaceRenderTargetTexture, RenderTargetTexture
			, nullptr, false, true, 1.0f, TEXT("DreamUIRenderScaleComposite"), /*bPremultipliedAlpha*/true);
		//restore for anything after this block (the MSAA resolve reads it, and it must be the full rect)
		ViewRect = UnscaledScreenSpaceViewRect;
	}

	//no SafeRelease here any more: the graph holds its own reference (see where it is registered),
	//and dropping ours mid-recording is what let another view claim the same pool element
}

void FDreamUIRenderer::Resolve_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& InView, const FRecordTargets& Targets)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(DreamUI_Resolve);
	if (Targets.NumSamples > 1)
	{
		auto Src = RegisterExternalTexture(GraphBuilder, Targets.ScreenColorRenderTargetTexture, TEXT("DreamUIResolveSrc"));
		auto Dst = RegisterExternalTexture(GraphBuilder, Targets.OrignScreenColorRenderTargetTexture, TEXT("DreamUIResolveDst"));

		AddResolvePass(GraphBuilder, FRDGTextureMSAA(Src, Dst), Targets.ViewRect, Targets.NumSamples, GetGlobalShaderMap(InView.GetFeatureLevel()));
	}
}


class FDreamUIDummySceneColorResolveBuffer : public FVertexBuffer
{
public:
	virtual void InitRHI(FRHICommandListBase& RHICmdList) override
	{
		const int32 NumDummyVerts = 3;
		const uint32 Size = sizeof(FVector4f) * NumDummyVerts;
		const FRHIBufferCreateDesc CreateDesc =
			FRHIBufferCreateDesc::CreateVertex(TEXT("FDreamUIDummySceneColorResolveBuffer"), Size)
			.AddUsage(EBufferUsageFlags::Static)
			.DetermineInitialState();

		VertexBufferRHI = RHICmdList.CreateBuffer(CreateDesc);
	}
};

TGlobalResource<FDreamUIDummySceneColorResolveBuffer> GDreamUIResolveDummyVertexBuffer;

BEGIN_SHADER_PARAMETER_STRUCT(FDreamUIResolveParameters, )
RDG_TEXTURE_ACCESS(MainTex, ERHIAccess::SRVGraphics)
RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

//reference from SceneRendering.cpp::AddResolveSceneColorPass
void FDreamUIRenderer::AddResolvePass(
	FRDGBuilder& GraphBuilder
	, FRDGTextureMSAA SceneColor
	, const FIntRect& ViewRect
	, uint8 NumSamples
	, FGlobalShaderMap* GlobalShaderMap
)
{
	FDreamUIResolveParameters* PassParameters = GraphBuilder.AllocParameters<FDreamUIResolveParameters>();
	PassParameters->MainTex = SceneColor.Target;
	PassParameters->RenderTargets[0] = FRenderTargetBinding(SceneColor.Resolve, SceneColor.Resolve->HasBeenProduced() ? ERenderTargetLoadAction::ELoad : ERenderTargetLoadAction::ENoAction);

	FRDGTextureRef SceneColorTargetable = SceneColor.Target;

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("DreamUIResolveColor"),
		PassParameters,
		ERDGPassFlags::Raster,
		[ViewRect, SceneColorTargetable, NumSamples, GlobalShaderMap](FRHICommandList& RHICmdList)
		{
			// 2, 4 and 8 samples resolve; a target with any other count has no shader to resolve it.
			if (NumSamples != 2 && NumSamples != 4 && NumSamples != 8)
			{
				return;
			}
			FRHITexture* SceneColorTargetableRHI = SceneColorTargetable->GetRHI();

			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

			GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

			const FIntPoint SceneColorExtent = SceneColorTargetable->Desc.Extent;

			// Resolve views individually. In the case of adaptive resolution, the view family will be much larger than the views individually.
			RHICmdList.SetViewport(0.0f, 0.0f, 0.0f, SceneColorExtent.X, SceneColorExtent.Y, 1.0f);
			RHICmdList.SetScissorRect(true, ViewRect.Min.X, ViewRect.Min.Y, ViewRect.Max.X, ViewRect.Max.Y);

			TShaderMapRef<FDreamUIResolveShaderVS> VertexShader(GlobalShaderMap);
			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GetVertexDeclarationFVector4();
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
			GraphicsPSOInit.PrimitiveType = PT_TriangleList;
			FDreamUIResolveShaderPS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FDreamUIResolveShaderPS::FSampleCount>(NumSamples);
			TShaderMapRef<FDreamUIResolveShaderPS> PixelShader(GlobalShaderMap, PermutationVector);
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0);
			FDreamUIResolveShaderPS::FParameters Parameters;
			Parameters.Tex = SceneColorTargetableRHI;
			SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), Parameters);

			RHICmdList.SetStreamSource(0, GDreamUIResolveDummyVertexBuffer.VertexBufferRHI, 0);
			RHICmdList.DrawPrimitive(0, 1, 1);
			RHICmdList.SetScissorRect(false, 0, 0, 0, 0);
		}
	);
}

void FDreamUIRenderer::AddWorldSpacePrimitive_RenderThread(FObjectKey InCanvasKey, float InBlendDepth, int InDepthFade, IDreamUIRendererPrimitive* InPrimitive)
{
	if (InPrimitive != nullptr)
	{
		FWorldSpaceRenderParameter RenderParameter;
		RenderParameter.BlendDepth = InBlendDepth;
		RenderParameter.DepthFade = InDepthFade;
		RenderParameter.RenderCanvasKey = InCanvasKey;
		RenderParameter.Primitive = InPrimitive;

		WorldSpaceRenderCanvasParameterArray.Add(RenderParameter);
	}
	else
	{
		UE_LOG(LogDreamGUIRenderer, Warning, TEXT("[%s].%d Add nullptr as IDreamUIRendererPrimitive!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
	}
}
void FDreamUIRenderer::RemoveWorldSpacePrimitive_RenderThread(IDreamUIRendererPrimitive* InPrimitive)
{
	if (InPrimitive != nullptr)
	{
		int existIndex = WorldSpaceRenderCanvasParameterArray.IndexOfByPredicate([InPrimitive](const FWorldSpaceRenderParameter& item) {
			return item.Primitive == InPrimitive;
			});
		if (existIndex == INDEX_NONE)
		{
			UE_LOG(LogDreamGUIRenderer, Log, TEXT("[%s].%d Canvas already removed."), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		}
		else
		{
			WorldSpaceRenderCanvasParameterArray.RemoveAt(existIndex);
		}
	}
	else
	{
		UE_LOG(LogDreamGUIRenderer, Warning, TEXT("[%s].%d Remove nullptr as IDreamUIRendererPrimitive!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
	}
}
void FDreamUIRenderer::AddScreenSpacePrimitive_RenderThread(IDreamUIRendererPrimitive* InPrimitive)
{
	AddScreenSpacePrimitive_RenderThread(InPrimitive, FObjectKey());
}
void FDreamUIRenderer::AddScreenSpacePrimitive_RenderThread(IDreamUIRendererPrimitive* InPrimitive, FObjectKey InRootCanvasKey)
{
	if (InPrimitive != nullptr)
	{
		ScreenSpaceRenderParameter.PrimitiveArray.AddUnique(InPrimitive);
		ScreenSpaceRenderParameter.bNeedSortRenderPriority = true;
		if (InRootCanvasKey != FObjectKey())
		{
			ScreenSpaceRenderParameter.PrimitiveRootKeys.Add(InPrimitive, InRootCanvasKey);
		}
		else
		{
			ScreenSpaceRenderParameter.PrimitiveRootKeys.Remove(InPrimitive);
		}
	}
	else
	{
		UE_LOG(LogDreamGUIRenderer, Warning, TEXT("[%s].%d Add nullptr as IDreamUIRendererPrimitive!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
	}
}
void FDreamUIRenderer::RemoveScreenSpacePrimitive_RenderThread(IDreamUIRendererPrimitive* InPrimitive)
{
	if (InPrimitive != nullptr)
	{
		ScreenSpaceRenderParameter.PrimitiveArray.RemoveSingle(InPrimitive);
		ScreenSpaceRenderParameter.PrimitiveRootKeys.Remove(InPrimitive);
	}
	else
	{
		UE_LOG(LogDreamGUIRenderer, Warning, TEXT("[%s].%d Remove nullptr as IDreamUIRendererPrimitive!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
	}
}
void FDreamUIRenderer::SortScreenSpacePrimitiveRenderPriority_RenderThread()
{
	//StableSort: multiple root canvases share SortOrder 0 by default; an unstable sort could flip
	//their relative order (and thus visual z-order) on any resort, e.g. after a proxy re-registers.
	ScreenSpaceRenderParameter.PrimitiveArray.StableSort([](IDreamUIRendererPrimitive& A, IDreamUIRendererPrimitive& B)
		{
			return A.DreamUI_GetRenderPriority() < B.DreamUI_GetRenderPriority();
		});
}

void FDreamUIRenderer::MarkNeedToSortScreenSpacePrimitiveRenderPriority()
{
	ENQUEUE_RENDER_COMMAND(FDreamUIRender_SortRenderPriority)(
		[ViewExtension = DreamUIRendererLocal::HoldForRenderThread(*this)](FRHICommandListImmediate& RHICmdList)
		{
			ViewExtension->ScreenSpaceRenderParameter.bNeedSortRenderPriority = true;
		}
	);
}
void FDreamUIRenderer::SetRenderCanvasDepthParameter(const UObject* InRenderCanvas, float InBlendDepth, int InDepthFade)
{
	//take the identity here, on the game thread, so the canvas pointer never crosses to the render thread
	const FObjectKey RenderCanvasKey(InRenderCanvas);
	ENQUEUE_RENDER_COMMAND(FDreamUIRender_SortRenderPriority)(
		[viewExtension = DreamUIRendererLocal::HoldForRenderThread(*this), RenderCanvasKey, InBlendDepth, InDepthFade](FRHICommandListImmediate& RHICmdList)
		{
			viewExtension->SetRenderCanvasDepthFade_RenderThread(RenderCanvasKey, InBlendDepth, InDepthFade);
		}
	);
}

void FDreamUIRenderer::SetRenderCanvasDepthFade_RenderThread(FObjectKey InRenderCanvasKey, float InBlendDepth, int InDepthFade)
{
	for (auto& RenderParameter : WorldSpaceRenderCanvasParameterArray)
	{
		if (RenderParameter.RenderCanvasKey == InRenderCanvasKey)
		{
			RenderParameter.BlendDepth = InBlendDepth;
			RenderParameter.DepthFade = InDepthFade;
		}
	}
}

const FDreamUIRenderer::FScreenSpaceRoot* FDreamUIRenderer::GetScreenSpaceViewRoot()const
{
	//first still-alive registration wins, so the answer does not change when a later canvas appears -- among the roots
	//that fill the whole viewport, which is all of them unless a split screen gives some a player's part: a part has a
	//view of its own (UpdateViewParameter_GameThread), and the shared view is for what every view draws
	const FScreenSpaceRoot* FirstAlive = nullptr;
	for (const FScreenSpaceRoot& Root : ScreenSpaceRenderParameter.RootCanvasArray)
	{
		if (Root.Canvas.IsValid())
		{
			if (Root.ViewSource != nullptr && Root.ViewSource->GetRendererViewPlayerIndex() == INDEX_NONE)
			{
				return &Root;
			}
			if (FirstAlive == nullptr)
			{
				FirstAlive = &Root;
			}
		}
	}
	return FirstAlive;
}

void FDreamUIRenderer::SetScreenSpaceRootCanvas(UObject* InCanvas, const IDreamUIRendererViewSource* InViewSource)
{
	if (InCanvas == nullptr || InViewSource == nullptr)return;
	//drop registrations whose canvas has been collected, so a dead one cannot keep owning the view
	ScreenSpaceRenderParameter.RootCanvasArray.RemoveAll([](const FScreenSpaceRoot& Item) { return !Item.Canvas.IsValid(); });
	if (!ScreenSpaceRenderParameter.RootCanvasArray.ContainsByPredicate([InCanvas](const FScreenSpaceRoot& Item) { return Item.Canvas.Get() == InCanvas; }))
	{
		ScreenSpaceRenderParameter.RootCanvasArray.Add(FScreenSpaceRoot{ InCanvas, InViewSource });
	}
	// Only the roots that share the view: one that fills a player's part of a split screen is drawn through its own.
	int32 SharedRootCount = 0;
	for (const FScreenSpaceRoot& Root : ScreenSpaceRenderParameter.RootCanvasArray)
	{
		if (Root.Canvas.IsValid() && Root.ViewSource != nullptr && Root.ViewSource->GetRendererViewPlayerIndex() == INDEX_NONE)
		{
			++SharedRootCount;
		}
	}
	if (SharedRootCount > 1)
	{
		UE_LOG(LogDreamGUIRenderer, Warning, TEXT("[%s].%d %d root canvases are rendering screen-space into the same view; the view location, rotation, projection and depth test are taken from '%s', the first one registered. Give the others their own render mode (RenderTarget or WorldSpace) if they need their own projection.")
			, ANSI_TO_TCHAR(__FUNCTION__), __LINE__
			, SharedRootCount
			, *GetNameSafe(GetScreenSpaceViewRoot() != nullptr ? GetScreenSpaceViewRoot()->Canvas.Get() : nullptr));
	}
}
void FDreamUIRenderer::ClearScreenSpaceRootCanvas(const UObject* InCanvas)
{
	//only this canvas: clearing the whole thing is what used to blank the view parameters for every
	//other root canvas as soon as any one of them unregistered
	ScreenSpaceRenderParameter.RootCanvasArray.RemoveAll([InCanvas](const FScreenSpaceRoot& Item)
		{
			return !Item.Canvas.IsValid() || Item.Canvas.Get() == InCanvas;
		});
}

void FDreamUIRenderer::UpdateRenderTargetRenderer(UTextureRenderTarget2D* InRenderTarget, FColor InClearColor)
{
	if (InRenderTarget == nullptr || InRenderTarget->GameThread_GetRenderTargetResource() == nullptr)
	{
		return;
	}
	ENQUEUE_RENDER_COMMAND(FDreamUIRender_UpdateRenderTargetRenderer)(
		[ViewExtension = DreamUIRendererLocal::HoldForRenderThread(*this), InRenderTarget, InClearColor](FRHICommandListImmediate& RHICmdList)
		{
			// The target's resource as the render thread sees it, read by the command the game thread enqueued while
			// the target was alive: what it releases is enqueued after this. Only the texture is kept.
			FTextureRHIRef Texture;
			if (FTextureRenderTargetResource* Resource = InRenderTarget->GetRenderTargetResource())
			{
				Texture = Resource->GetRenderTargetTexture();
			}
			ViewExtension->CanvasTargetTexture = Texture;
			ViewExtension->RenderTargetClearColor = InClearColor;
		}
	);
}

#if WITH_EDITOR
void FDreamUIRenderer::RenderGizmoMesh_RenderThread(const TArray<TSharedPtr<FDreamUIGizmoMesh>>& HelperGizmoDataMap,
	FRDGBuilder& GraphBuilder, FSceneView* RenderView, const FIntRect& ViewRect, uint8 NumSamples,
	FRDGTextureRef RenderTargetTexture)
{
	if (HelperGizmoDataMap.Num() <= 0)return;
	auto* PassParameters = GraphBuilder.AllocParameters<FRenderTargetParameters>();
	PassParameters->RenderTargets[0] = FRenderTargetBinding(RenderTargetTexture, ERenderTargetLoadAction::ELoad);
	GraphBuilder.AddPass(
		RDG_EVENT_NAME("DreamUI_RenderHelperLine"),
		PassParameters,
		ERDGPassFlags::Raster,
		// What it reads of the renderer, by value: the pass holds no pointer to it.
		[Gizmos = TArray<TSharedPtr<FDreamUIGizmoMesh>>(HelperGizmoDataMap), RenderView, ViewRect, NumSamples
			, bFrustumCulling = RenderThreadViewParameter.bFrustumCulling](FRHICommandListImmediate& RHICmdList)
		{
			RHICmdList.SetViewport(ViewRect.Min.X, ViewRect.Min.Y, 0.0f, ViewRect.Max.X, ViewRect.Max.Y, 1.0f);

			auto GlobalShaderMap = GetGlobalShaderMap(RenderView->GetFeatureLevel());
			TShaderMapRef<FDreamUIBaseVS> VertexShader(GlobalShaderMap);
			FDreamUIBasePS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FDreamUIBasePS::FPlainColor>(true);
			TShaderMapRef<FDreamUIBasePS> PixelShader(GlobalShaderMap, PermutationVector);
			const FMatrix ViewProjection = RenderView->ViewMatrices.GetWorldToClip();

			for (const TSharedPtr<FDreamUIGizmoMesh>& RenderParameter : Gizmos)
			{
				auto& LocalBounds = RenderParameter->LocalBounds;
				auto& LocalToWorldMatrix = RenderParameter->LocalToWorldMatrix;
				auto WorldBounds = LocalBounds.TransformBy(LocalToWorldMatrix);
				if (bFrustumCulling)
				{
					if (!RenderView->GetCullingFrustum().IntersectBox(WorldBounds.Origin, WorldBounds.BoxExtent))continue;
				}

				uint32 NumPrimitives = 0;
				EPrimitiveType PrimitiveType = EPrimitiveType::PT_TriangleList;
				switch (RenderParameter->GetPrimitiveType())
				{
				case EDreamUIGizmoMeshPrimitiveType::Line:
					{
						NumPrimitives = RenderParameter->GetIndexBuffer().Indices.Num() / 2;
						PrimitiveType = EPrimitiveType::PT_LineList;
					}
					break;
				case EDreamUIGizmoMeshPrimitiveType::Triangle:
					{
						NumPrimitives = RenderParameter->GetIndexBuffer().Indices.Num() / 3;
						PrimitiveType = EPrimitiveType::PT_TriangleList;
					}
					break;
				}
				if (NumPrimitives == 0)continue;

				FGraphicsPipelineStateInitializer GraphicsPSOInit;
				RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
				// Straight alpha over the scene, two-sided, no depth: helper geometry is an overlay.
				FDreamUIRenderer::SetGraphicPipelineState_BlendDepthStencilRasterize(RenderView->GetFeatureLevel(), GraphicsPSOInit, BLEND_AlphaComposite
					, false, true, true, false, false);
				GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GetDreamUIMeshVertexDeclaration();
				GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
				GraphicsPSOInit.NumSamples = NumSamples;
				GraphicsPSOInit.PrimitiveType = PrimitiveType;
				SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0, EApplyRendertargetOption::CheckApply);

				FDreamUIBaseVS::FParameters VSParameters;
				VSParameters.DreamUI_MVP = FMatrix44f(LocalToWorldMatrix * ViewProjection);
				// A gizmo's vertices are in its own space already, which LocalToWorldMatrix places: black widget data names
				// row 0 of a black table, no layer.
				VSParameters.DreamUI_RenderLayerTable = GBlackTexture->TextureRHI.GetReference();
				VSParameters.DreamUI_RenderLayerWidgetData = GBlackTexture->TextureRHI.GetReference();
				SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), VSParameters);

				// The plain-colour permutation reads none of the textures, but the parameter struct is
				// shared, so they still need something bound.
				FDreamUIBasePS::FParameters PSParameters;
				PSParameters.DreamUI_GammaValues = FVector4f(2.2f, 1.0f, 0.0f, 0.0f);
				PSParameters.DreamUI_FontAtlasInfo = FVector4f(1.0f, 1.0f, 0.0f, 0.0f);
				PSParameters.DreamUI_MainTex = GWhiteTexture->TextureRHI;
				PSParameters.DreamUI_MainTexSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
				PSParameters.DreamUI_FontTex = GBlackArrayTexture->TextureRHI;
				PSParameters.DreamUI_FontTexSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
				PSParameters.DreamUI_WidgetDataTex = GBlackTexture->TextureRHI;
				PSParameters.DreamUI_ClipDataTex = GBlackTexture->TextureRHI;
				PSParameters.DreamUI_RectBlockDataTex = GBlackTexture->TextureRHI;
				PSParameters.DreamUI_PaintDataTex = GBlackTexture->TextureRHI;
				PSParameters.DreamUI_SceneDepthTex = GBlackTexture->TextureRHI;
				PSParameters.DreamUI_SceneDepthTexSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
				PSParameters.DreamUI_SceneDepthTextureScaleOffset = FVector4f(1.0f, 1.0f, 0.0f, 0.0f);
				PSParameters.DreamUI_SceneDepthBlend = 0.0f;
				PSParameters.DreamUI_SceneDepthFade = 0;
				PSParameters.DreamUI_ViewSizeInv = FVector2f(1.0f / FMath::Max(ViewRect.Size().X, 1), 1.0f / FMath::Max(ViewRect.Size().Y, 1));
				SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), PSParameters);

				RHICmdList.SetStreamSource(0, RenderParameter->GetVertexBuffer().GetRHI(), 0);
				RHICmdList.DrawIndexedPrimitive(RenderParameter->GetIndexBuffer().GetRHI(), 0, 0, RenderParameter->GetNumVertices(), 0, NumPrimitives, 1);
			}
		});
}

void FDreamUIRenderer::BeginGizmoFrame()
{
	check(IsInGameThread());
	if (PendingGizmoFrame == GFrameCounter)
	{
		return;
	}
	PendingGizmoFrame = GFrameCounter;
	if (PendingScreenSpaceGizmoMeshes.Num() > 0 || PendingWorldSpaceGizmoMeshes.Num() > 0)
	{
		// A frame no view family began in: its gizmos were never sent, and are not drawn late. Let go of on the render thread,
		// where a mesh's buffers are released without a flush.
		ENQUEUE_RENDER_COMMAND(FDreamUIRender_DropGizmoMeshes)(
			[ScreenMeshes = MoveTemp(PendingScreenSpaceGizmoMeshes), WorldMeshes = MoveTemp(PendingWorldSpaceGizmoMeshes)](FRHICommandListImmediate& RHICmdList)
			{
			});
		PendingScreenSpaceGizmoMeshes.Reset();
		PendingWorldSpaceGizmoMeshes.Reset();
	}
}

void FDreamUIRenderer::SubmitGizmoMeshes()
{
	check(IsInGameThread());
	if (SubmittedGizmoFrame == GFrameCounter)
	{
		return;
	}
	SubmittedGizmoFrame = GFrameCounter;
	BeginGizmoFrame();
	// Every view of every family this frame draws the same gizmos: the list is replaced once a frame, not emptied by the
	// first view that draws it -- a second viewport saw none. The meshes it replaces are let go of on the render thread.
	ENQUEUE_RENDER_COMMAND(FDreamUIRender_SetGizmoMeshes)(
		[Renderer = DreamUIRendererLocal::HoldForRenderThread(*this), ScreenMeshes = MoveTemp(PendingScreenSpaceGizmoMeshes), WorldMeshes = MoveTemp(PendingWorldSpaceGizmoMeshes)](FRHICommandListImmediate& RHICmdList) mutable
		{
			Renderer->ScreenSpaceGizmoMeshArray = MoveTemp(ScreenMeshes);
			Renderer->WorldSpaceGizmoMeshArray = MoveTemp(WorldMeshes);
		});
	PendingScreenSpaceGizmoMeshes.Reset();
	PendingWorldSpaceGizmoMeshes.Reset();
}

void FDreamUIRenderer::AddScreenSpaceGizmoMesh(TSharedPtr<FDreamUIGizmoMesh> InMesh)
{
	BeginGizmoFrame();
	PendingScreenSpaceGizmoMeshes.Add(MoveTemp(InMesh));
}

void FDreamUIRenderer::AddWorldSpaceGizmoMesh(TSharedPtr<FDreamUIGizmoMesh> InMesh)
{
	BeginGizmoFrame();
	PendingWorldSpaceGizmoMeshes.Add(MoveTemp(InMesh));
}
#endif

// The single definitions behind the extern declarations in the header.
TGlobalResource<FDreamUIFullScreenQuadVertexBuffer> GDreamUIFullScreenQuadVertexBuffer;
TGlobalResource<FDreamUIFullScreenQuadIndexBuffer> GDreamUIFullScreenQuadIndexBuffer;
TGlobalResource<FDreamUIFullScreenSlicedQuadIndexBuffer> GDreamUIFullScreenSlicedQuadIndexBuffer;

void FDreamUIFullScreenQuadVertexBuffer::InitRHI(FRHICommandListBase& RHICmdList)
{
	TArray<FDreamUIPostProcessVertex> Vertices;
	Vertices.SetNumUninitialized(4);

	Vertices[0] = FDreamUIPostProcessVertex(FVector3f(-1, -1, 0), FVector2f(0.0f, 1.0f));
	Vertices[1] = FDreamUIPostProcessVertex(FVector3f(1, -1, 0), FVector2f(1.0f, 1.0f));
	Vertices[2] = FDreamUIPostProcessVertex(FVector3f(-1, 1, 0), FVector2f(0.0f, 0.0f));
	Vertices[3] = FDreamUIPostProcessVertex(FVector3f(1, 1, 0), FVector2f(1.0f, 0.0f));

	VertexBufferRHI = UE::RHIResourceUtils::CreateVertexBufferFromArray(
		RHICmdList, TEXT("DreamUIFullScreenQuadVertexBuffer"), EBufferUsageFlags::Static, MakeConstArrayView(Vertices)
	);
}
void FDreamUIFullScreenQuadIndexBuffer::InitRHI(FRHICommandListBase& RHICmdList)
{
	const uint16 Indices[] =
	{
		0, 2, 3,
		0, 3, 1
	};
	
	IndexBufferRHI = UE::RHIResourceUtils::CreateIndexBufferFromArray(
		RHICmdList, TEXT("DreamUIFullScreenQuadIndexBuffer"), EBufferUsageFlags::Static, MakeConstArrayView(Indices)
	);
}
void FDreamUIFullScreenSlicedQuadIndexBuffer::InitRHI(FRHICommandListBase& RHICmdList)
{
	uint16 Indices[54];
	int wSeg = 3, hSeg = 3;
	int vStartIndex = 0;
	int triangleArrayIndex = 0;
	for (int h = 0; h < hSeg; h++)
	{
		for (int w = 0; w < wSeg; w++)
		{
			int vIndex = vStartIndex + w;
			Indices[triangleArrayIndex++] = vIndex;
			Indices[triangleArrayIndex++] = vIndex + wSeg + 2;
			Indices[triangleArrayIndex++] = vIndex + wSeg + 1;

			Indices[triangleArrayIndex++] = vIndex;
			Indices[triangleArrayIndex++] = vIndex + 1;
			Indices[triangleArrayIndex++] = vIndex + wSeg + 2;
		}
		vStartIndex += wSeg + 1;
	}
	
	IndexBufferRHI = UE::RHIResourceUtils::CreateIndexBufferFromArray(
		RHICmdList, TEXT("DreamUIFullScreenSlicedQuadIndexBuffer"), EBufferUsageFlags::Static, MakeConstArrayView(Indices)
	);
}


