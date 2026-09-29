// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SceneViewExtension.h"
#include "RendererInterface.h"
#include "RenderGraphUtils.h"
#include "RenderResource.h"
#include "UObject/ObjectKey.h"
#include "DreamUIRender/DreamUIBlendMode.h"
#include "DreamUIRender/IDreamUIRendererPrimitive.h"
#include "DreamUIRender/IDreamUIRendererViewSource.h"

class FDreamUIGizmoMesh;
struct FDreamUIPostProcessVertex;
struct FDreamUIPostProcessCopyMeshRegionVertex;
class FGlobalShaderMap;

class FDreamUIMeshElementCollector : public FMeshElementCollector//why use a custom collector? because default FMeshElementCollector have no public constructor
{
public:
	FDreamUIMeshElementCollector(ERHIFeatureLevel::Type InFeatureLevel, FSceneRenderingBulkObjectAllocator& Allocator, FRHICommandList& InRHICmdList)
		:FMeshElementCollector(InFeatureLevel, Allocator)
	{
		RHICmdList = &InRHICmdList;
	}
};

enum class EDreamUIRendererType :uint8
{
	ScreenSpace_and_WorldSpace,
	RenderTarget,
};

class DREAMGUIRENDERER_API FDreamUIRenderer : public FSceneViewExtensionBase
{
public:
	FDreamUIRenderer(const FAutoRegister&, UWorld* InWorld, EDreamUIRendererType InRendererType);
	virtual ~FDreamUIRenderer()override;

	//begin ISceneViewExtension interfaces
	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily)override {};
	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView)override;
	virtual void SetupViewPoint(APlayerController* Player, FMinimalViewInfo& InViewInfo)override;
	virtual void SetupViewProjectionMatrix(FSceneViewProjectionData& InOutProjectionData)override;
	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily)override;

	virtual void PreRenderViewFamily_RenderThread(FRDGBuilder& GraphBuilder, FSceneViewFamily& InViewFamily)override {};
	virtual void PreRenderView_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& InView)override;

	virtual void PostRenderBasePassDeferred_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& InView, const FRenderTargetBindingSlots& RenderTargets, TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTextures)override;
	virtual void PrePostProcessPass_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessingInputs& Inputs)override {};
	virtual void SubscribeToPostProcessingPass(EPostProcessingPass Pass, const FSceneView& InView, FAfterPassCallbackDelegateArray& InOutPassCallbacks, bool bIsPassEnabled)override {};

	virtual void PostRenderView_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& InView)override;
	virtual void PostRenderViewFamily_RenderThread(FRDGBuilder& GraphBuilder, FSceneViewFamily& InViewFamily)override {};

	virtual int32 GetPriority() const override;
	virtual bool IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const override;
	//end ISceneViewExtension interfaces

	//
	void AddWorldSpacePrimitive_RenderThread(FObjectKey InCanvasKey, float InBlendDepth, int InDepthFade, IDreamUIRendererPrimitive* InPrimitive);
	void RemoveWorldSpacePrimitive_RenderThread(IDreamUIRendererPrimitive* InPrimitive);

	void AddScreenSpacePrimitive_RenderThread(IDreamUIRendererPrimitive* InPrimitive);
	void RemoveScreenSpacePrimitive_RenderThread(IDreamUIRendererPrimitive* InPrimitive);

	void MarkNeedToSortScreenSpacePrimitiveRenderPriority();
	//there is deliberately no world-space counterpart: that sequence is rebuilt and resorted every
	//frame because its ordering depends on distance to this frame's camera. See RenderDreamUI_RenderThread.
	/** InRenderCanvas is only an identity here: the render thread matches primitives by it and never dereferences it. */
	void SetRenderCanvasDepthParameter(const UObject* InRenderCanvas, float InBlendDepth, int InDepthFade);

	/**
	 * Register a root canvas that renders screen-space into this extension. InViewSource is the same object seen
	 * as what the view is set up from; it is only asked while InCanvas is alive.
	 */
	void SetScreenSpaceRootCanvas(UObject* InCanvas, const IDreamUIRendererViewSource* InViewSource);
	/** Removes only InCanvas; the other registered root canvases keep rendering. */
	void ClearScreenSpaceRootCanvas(const UObject* InCanvas);

	/**
	 * The size a screen-space pass renders at for InRequestedScale of InViewportSize, and the scale that size
	 * really is once rounded to pixels (OutAppliedScale).
	 *
	 * Separate from the RDG work on purpose: this is the whole decision, it is pure arithmetic, and it is
	 * where the rules live -- never larger than the viewport, never smaller than one pixel on either axis,
	 * and a scale of exactly 1 must give back the viewport size unchanged so that the ordinary case cannot
	 * drift by a rounding error.
	 */
	static FIntPoint CalculateRenderScaledSize(const FIntPoint& InViewportSize, float InRequestedScale, float& OutAppliedScale);
#if WITH_EDITOR
	/**
	 * How the renderer learns that the editor is simulating (SIE), when screen-space UI must not draw. The
	 * editor engine is not something the renderer links; whoever does sets this.
	 */
	static void SetSimulatingInEditorQuery(TFunction<bool()> InQuery);
#endif

	void UpdateRenderTargetRenderer(class UTextureRenderTarget2D* InRenderTarget, FColor InClearColor);

	/**
	 * Whether a render-target canvas is drawn by a render command and a graph of its own (r.DreamUI.RTDrawer, on by
	 * default) rather than inside the render of one of its world's views. Game thread.
	 */
	static bool IsRenderTargetDrawerEnabled();
	/**
	 * Render-target mode with the drawer on: draws the canvas into InRenderTarget with a render command and a graph of
	 * its own, enqueued now -- after this frame's changes to the canvas's sections -- whether or not anything renders
	 * the canvas's world. Game thread.
	 */
	void DrawRenderTarget_GameThread(class UTextureRenderTarget2D* InRenderTarget, FColor InClearColor);

	TWeakObjectPtr<UWorld> GetWorld() { return World; }

	void CopyRenderTarget(
		FRDGBuilder& GraphBuilder,
		FGlobalShaderMap* GlobalShaderMap,
		FTextureRHIRef Src, FTextureRHIRef Dst,
		FRHISamplerState* SrcTextureSamplerState = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI()
	);
	void CopyRenderTarget_ColorCorrect(
		FRDGBuilder& GraphBuilder,
		FGlobalShaderMap* GlobalShaderMap,
		FTextureRHIRef Src, FTextureRHIRef Dst,
		FRHISamplerState* SrcTextureSamplerState = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI()
	);
	void CopyRenderTarget_BlendAlpha(
		FRDGBuilder& GraphBuilder,
		FGlobalShaderMap* GlobalShaderMap,
		FTextureRHIRef Src, FTextureRHIRef Dst,
		float BlendAlpha,
		FRHISamplerState* SrcTextureSamplerState = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI()
		);
	void CopyRenderTargetOnMeshRegion(
		FRDGBuilder& GraphBuilder,
		FRDGTextureRef Dst,
		FTextureRHIRef Src,
		FGlobalShaderMap* GlobalShaderMap,
		const TArray<FDreamUIPostProcessCopyMeshRegionVertex>& RegionVertexData,
		const FMatrix44f& MVP,
		bool bIsRenderTarget,
		const FIntRect& ViewRect,
		const FVector4f& SrcTextureScaleOffset,
		bool ColorCorrect = false
	);
	/**
	 * The same four on textures of the graph. The effects' intermediates are the graph's own (GraphBuilder.CreateTexture),
	 * which have no RHI texture until the graph runs.
	 */
	void CopyRenderTarget(FRDGBuilder& GraphBuilder, FGlobalShaderMap* GlobalShaderMap, FRDGTextureRef Src, FRDGTextureRef Dst,
		FRHISamplerState* SrcTextureSamplerState = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI());
	void CopyRenderTarget_ColorCorrect(FRDGBuilder& GraphBuilder, FGlobalShaderMap* GlobalShaderMap, FRDGTextureRef Src, FRDGTextureRef Dst,
		FRHISamplerState* SrcTextureSamplerState = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI());
	void CopyRenderTarget_BlendAlpha(FRDGBuilder& GraphBuilder, FGlobalShaderMap* GlobalShaderMap, FRDGTextureRef Src, FRDGTextureRef Dst, float BlendAlpha,
		FRHISamplerState* SrcTextureSamplerState = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI());
	void CopyRenderTargetOnMeshRegion(FRDGBuilder& GraphBuilder, FRDGTextureRef Dst, FRDGTextureRef Src, FGlobalShaderMap* GlobalShaderMap,
		const TArray<FDreamUIPostProcessCopyMeshRegionVertex>& RegionVertexData, const FMatrix44f& MVP, bool bIsRenderTarget,
		const FIntRect& ViewRect, const FVector4f& SrcTextureScaleOffset, bool ColorCorrect = false);
	void DrawFullScreenQuad(
		FRHICommandListImmediate& RHICmdList
	);
	void AddResolvePass(
		FRDGBuilder& GraphBuilder
		, FRDGTextureMSAA SceneColor
		, const FIntRect& ViewRect
		, uint8 NumSamples
		, FGlobalShaderMap* GlobalShaderMap
	);
private:
	static void SetGraphicPipelineState_BlendDepthStencilRasterize(ERHIFeatureLevel::Type FeatureLevel, FGraphicsPipelineStateInitializer& GraphicsPSOInit, EBlendMode BlendMode
		, bool bIsWireFrame, bool bIsTwoSided, bool bDisableDepthTestForTransparent, bool bIsDepthValid, bool bReverseCulling
	);
	/**
	 * Draw one batch with the built-in UI shader (no material). Used for both screen-space and world-space
	 * canvases; bBlendDepth selects the world-space depth blend / fade permutation.
	 */
	/** The blend state one of DreamGUI's own blend modes maps to, for the built-in (material-less) path. */
	static FRHIBlendState* GetBuiltInBlendState(EDreamUIBlendMode InBlendMode);
	static void DrawBuiltInBatch(FRHICommandList& RHICmdList, FGraphicsPipelineStateInitializer& GraphicsPSOInit
		, const FSceneView& View, const FIntRect& ViewRect, const struct FDreamUIMeshBatchContainer& Batch
		, uint8 NumSamples, float GammaValue, bool bIsDepthValid
		, bool bBlendDepth, float BlendDepth, int DepthFade, const FVector4f& SceneDepthTexST, FRHITexture* SceneDepthTexture
	);
	struct FWorldSpaceRenderParameter
	{
		/*
		 * Which canvas registered this primitive, as an identity the render thread can compare without
		 * ever dereferencing it. It used to be the raw UDreamCanvas* -- and a raw address is not an
		 * identity: once a canvas is destroyed, the next UObject allocated at that address answers to
		 * the same key, so SetRenderCanvasDepthFade_RenderThread would apply one canvas's blend depth
		 * and depth fade to another's primitives. FObjectKey carries the serial number that tells the
		 * two apart.
		 */
		FObjectKey RenderCanvasKey;
		//blend depth, 0-occlude by depth, 1-all visible
		float BlendDepth = 0.0f;
		//depth fade effect
		int DepthFade = 0;

		IDreamUIRendererPrimitive* Primitive = nullptr;
	};
	/**
	 * What the game thread works out about the view in SetupView and the render thread draws with.
	 *
	 * One struct, copied across in a render command, because the render thread used to read these
	 * fields straight out of the object the game thread was writing them into.
	 */
	struct FScreenSpaceViewParameter
	{
		FVector ViewOrigin = FVector::ZeroVector;
		FMatrix ViewRotationMatrix = FMatrix::Identity;
		FMatrix ProjectionMatrix = FMatrix::Identity;
		FMatrix44f ViewProjectionMatrix = FMatrix44f::Identity;
		bool bEnableDepthTest = false;
		bool bFrustumCulling = true;
		//sample count for MSAA
		uint8 NumSamples_MSAA = 1;
		/** Fraction of the viewport the screen-space UI is drawn at; 1 is full resolution. */
		float ScreenSpaceRenderScale = 1.0f;
#if WITH_EDITORONLY_DATA
		/** Whether screen-space UI draws in this frame's views at all, and whether the world is playing: see IsActiveThisFrame_Internal. */
		bool bCanRenderScreenSpace = true;
		bool bIsPlaying = false;
#endif
	};
	/** A registered screen-space root: the canvas, for liveness and identity, and what it answers as a view. */
	struct FScreenSpaceRoot
	{
		TWeakObjectPtr<UObject> Canvas;
		const IDreamUIRendererViewSource* ViewSource = nullptr;
	};
	struct FScreenSpaceRenderParameter
	{
		bool bNeedSortRenderPriority = true;

		/**
		 * Every root canvas currently rendering into this view extension, in registration order.
		 *
		 * There used to be a single slot here. A second ScreenSpaceOverlay root canvas in the same
		 * world silently took it over, and -- worse -- whichever of them unregistered first cleared it,
		 * leaving the survivor drawing with no view parameters at all. The view can still only be set
		 * up from one canvas (see GetScreenSpaceViewCanvas), but which one is now stable and
		 * unregistering one no longer breaks the others.
		 */
		TArray<FScreenSpaceRoot> RootCanvasArray;
		TArray<IDreamUIRendererPrimitive*> PrimitiveArray;
	};
	/** The registered root the screen-space view parameters are taken from, or null. */
	const FScreenSpaceRoot* GetScreenSpaceViewRoot()const;
	/** The view parameters, from the registered root and the project's settings, worked out and sent to the render thread. */
	void UpdateViewParameter_GameThread();
	/** The drawer's command: a view of the target alone, and a graph that draws the canvas into it. */
	void DrawRenderTarget_RenderThread(FRHICommandListImmediate& RHICmdList, class UTextureRenderTarget2D* InRenderTarget, const struct FGameTime& InTime);
	TArray<FWorldSpaceRenderParameter> WorldSpaceRenderCanvasParameterArray;
	FScreenSpaceRenderParameter ScreenSpaceRenderParameter;
	/** Written on the game thread by UpdateViewParameter_GameThread, for SetupView or the drawer; never read there. */
	FScreenSpaceViewParameter GameThreadViewParameter;
	/** The render thread's own copy, replaced by the command UpdateViewParameter_GameThread enqueues. */
	FScreenSpaceViewParameter RenderThreadViewParameter;
	TWeakObjectPtr<UWorld> World;
	//no MeshBatchArray member: mesh batches are collected into a pass-local array inside each RDG
	//pass. A shared one was only safe because every pass here takes FRHICommandListImmediate& and is
	//therefore serialised, which is also what stops these passes being recorded in parallel.
	/**
	 * A render-target canvas's target for its next draw, taken on the render thread from the target's resource by the
	 * command UpdateRenderTargetRenderer enqueues, and let go of once drawn into. Held as the texture, which keeps
	 * itself alive: the resource pointer kept here before could be freed with its target between the command and the
	 * draw.
	 */
	FTextureRHIRef CanvasTargetTexture;
	FColor RenderTargetClearColor = FColor::Transparent;
	void SortScreenSpacePrimitiveRenderPriority_RenderThread();
	void SetRenderCanvasDepthFade_RenderThread(FObjectKey InRenderCanvasKey, float InBlendDepth, int InDepthFade);
	EDreamUIRendererType RendererType = EDreamUIRendererType::ScreenSpace_and_WorldSpace;

	void RenderDreamUI_RenderThread(
		FRDGBuilder& GraphBuilder
		, FSceneView& InView);
	/** What one recording of the UI into a view shares between its stages. */
	struct FRecordTargets
	{
		bool bIsMainViewport = true;
		bool bRenderWireframe = false;
		bool bRenderLit = true;
		FMaterialRenderProxy* WireframeMaterialInstance = nullptr;
		/** The target the UI ends up in, when ScreenColorRenderTargetTexture is a multisampled one resolved into it. */
		FTextureRHIRef OrignScreenColorRenderTargetTexture;
		FTextureRHIRef ScreenColorRenderTargetTexture;
		TRefCountPtr<IPooledRenderTarget> MSAARenderTarget;
		uint8 NumSamples = 1;
		FIntRect ViewRect;
		FVector4f DepthTextureScaleOffset = FVector4f(1, 1, 0, 0);
		FVector4f ColorTextureScaleOffset = FVector4f(1, 1, 0, 0);
		FRDGTextureRef SceneDepth = nullptr;
		FRDGTextureRef RenderTargetTexture = nullptr;
		float GammaValue = 1.0f;
	};
	/** The targets: a render-target canvas's own, the view's otherwise, multisampled when asked. False when there is none to draw into. */
	bool PrepareTargets_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& InView, FRecordTargets& Targets);
	/** The world-space canvases, sorted by priority and then distance, each drawn against the scene's depth. */
	void RecordWorldSpace_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& InView, FRecordTargets& Targets);
	/** The screen-space canvases, through the canvas's own view, scaled down first when asked. */
	void RecordScreenSpace_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& InView, FRecordTargets& Targets);
	/** The multisampled target resolved into the one the UI ends up in. */
	void Resolve_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& InView, const FRecordTargets& Targets);
#if WITH_EDITORONLY_DATA
private:
	bool bIsEditorPreview = false;
	/**
	 * Decided on the game thread each frame by IsActiveThisFrame_Internal and read there only: SetupView
	 * copies them into the view parameters, and the render thread reads the copies. It read these
	 * members directly while the game thread was writing the next frame's values.
	 */
	mutable bool bCanRenderScreenSpace = true;
	mutable bool bIsPlaying = false;
#endif
#if WITH_EDITOR
private:
	/**
	 * Render thread: the gizmos of the frame last sent (SubmitGizmoMeshes), drawn by every view of every family until the
	 * next frame's replace them. A pass takes its own copy of the list, so what it draws lives until it has drawn it.
	 */
	TArray<TSharedPtr<FDreamUIGizmoMesh>> ScreenSpaceGizmoMeshArray;
	TArray<TSharedPtr<FDreamUIGizmoMesh>> WorldSpaceGizmoMeshArray;
	/** Game thread: the gizmos added in the frame PendingGizmoFrame, not sent yet. */
	TArray<TSharedPtr<FDreamUIGizmoMesh>> PendingScreenSpaceGizmoMeshes;
	TArray<TSharedPtr<FDreamUIGizmoMesh>> PendingWorldSpaceGizmoMeshes;
	uint64 PendingGizmoFrame = 0;
	uint64 SubmittedGizmoFrame = 0;
	/** Game thread: the pending gizmos become this frame's, dropping a frame's that were never sent. */
	void BeginGizmoFrame();
	/** Game thread, as a frame's first view family begins: this frame's gizmos replace the render thread's. */
	void SubmitGizmoMeshes();
	void RenderGizmoMesh_RenderThread(const TArray<TSharedPtr<FDreamUIGizmoMesh>>& HelperGizmoDataMap
	, FRDGBuilder& GraphBuilder
	, FSceneView* RenderView
	, const FIntRect& ViewRect
	, uint8 NumSamples
	, FRDGTextureRef RenderTargetTexture
	);
public:
	void AddScreenSpaceGizmoMesh(TSharedPtr<FDreamUIGizmoMesh> InMesh);
	void AddWorldSpaceGizmoMesh(TSharedPtr<FDreamUIGizmoMesh> InMesh);
#endif
};

class DREAMGUIRENDERER_API FDreamUIFullScreenQuadVertexBuffer :public FVertexBuffer
{
public:
	void InitRHI(FRHICommandListBase& RHICmdList)override;
};
class DREAMGUIRENDERER_API FDreamUIFullScreenQuadIndexBuffer :public FIndexBuffer
{
public:
	void InitRHI(FRHICommandListBase& RHICmdList)override;
};
class DREAMGUIRENDERER_API FDreamUIFullScreenSlicedQuadIndexBuffer :public FIndexBuffer
{
public:
	void InitRHI(FRHICommandListBase& RHICmdList)override;
};
// One quad, not one per translation unit: `static` in a header gave every .cpp that included it its
// own copy, each registering and holding its own GPU buffers. Defined in DreamUIRenderer.cpp.
extern DREAMGUIRENDERER_API TGlobalResource<FDreamUIFullScreenQuadVertexBuffer> GDreamUIFullScreenQuadVertexBuffer;
extern DREAMGUIRENDERER_API TGlobalResource<FDreamUIFullScreenQuadIndexBuffer> GDreamUIFullScreenQuadIndexBuffer;
extern DREAMGUIRENDERER_API TGlobalResource<FDreamUIFullScreenSlicedQuadIndexBuffer> GDreamUIFullScreenSlicedQuadIndexBuffer;
BEGIN_SHADER_PARAMETER_STRUCT(FDreamUIWorldRenderPSParameter, )
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneDepthTex)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()
