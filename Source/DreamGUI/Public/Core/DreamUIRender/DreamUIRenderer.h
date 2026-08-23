// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SceneViewExtension.h"
#include "RendererInterface.h"
#include "RenderGraphUtils.h"
#include "RenderResource.h"
#include "Core/DreamUIRender/IDreamUIRendererPrimitive.h"

class FDreamUIGizmoMesh;
class UDreamCanvas;
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

class DREAMGUI_API FDreamUIRenderer : public FSceneViewExtensionBase
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
	void AddWorldSpacePrimitive_RenderThread(void* InCanvasPtr, float InBlendDepth, int InDepthFade, IDreamUIRendererPrimitive* InPrimitive);
	void RemoveWorldSpacePrimitive_RenderThread(IDreamUIRendererPrimitive* InPrimitive);

	void AddScreenSpacePrimitive_RenderThread(IDreamUIRendererPrimitive* InPrimitive);
	void RemoveScreenSpacePrimitive_RenderThread(IDreamUIRendererPrimitive* InPrimitive);

	void MarkNeedToSortScreenSpacePrimitiveRenderPriority();
	void MarkNeedToSortWorldSpacePrimitiveRenderPriority();
	void SetRenderCanvasDepthParameter(UDreamCanvas* InRenderCanvas, float InBlendDepth, int InDepthFade);

	void SetScreenSpaceRootCanvas(UDreamCanvas* InCanvas);
	void ClearScreenSpaceRootCanvas();

	void UpdateRenderTargetRenderer(class UTextureRenderTarget2D* InRenderTarget, FColor InClearColor);

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
	static void DrawBuiltInBatch(FRHICommandListImmediate& RHICmdList, FGraphicsPipelineStateInitializer& GraphicsPSOInit
		, const FSceneView& View, const FIntRect& ViewRect, const struct FDreamUIMeshBatchContainer& Batch
		, uint8 NumSamples, float GammaValue, bool bIsDepthValid
		, bool bBlendDepth, float BlendDepth, int DepthFade, const FVector4f& SceneDepthTexST, FRHITexture* SceneDepthTexture
	);
	struct FWorldSpaceRenderParameter
	{
		/*
		 * CAUTION! use this uobject pointer only in game-thread!
		 * I use it in render-thread just as a pointer or a key, so it is safe here.
		 */
		void* RenderCanvasPtr = nullptr;
		//blend depth, 0-occlude by depth, 1-all visible
		float BlendDepth = 0.0f;
		//depth fade effect
		int DepthFade = 0;

		IDreamUIRendererPrimitive* Primitive = nullptr;
	};
	struct FScreenSpaceRenderParameter
	{
		FVector ViewOrigin = FVector::ZeroVector;
		FMatrix ViewRotationMatrix = FMatrix::Identity;
		FMatrix ProjectionMatrix = FMatrix::Identity;
		FMatrix44f ViewProjectionMatrix = FMatrix44f::Identity;
		bool bEnableDepthTest = false;
		bool bNeedSortRenderPriority = true;

		TWeakObjectPtr<UDreamCanvas> RootCanvas = nullptr;
		TArray<IDreamUIRendererPrimitive*> PrimitiveArray;
	};
	TArray<FWorldSpaceRenderParameter> WorldSpaceRenderCanvasParameterArray;
	TMap<UDreamCanvas*, bool> WorldSpaceCanvasVisibilityMap;
	bool bNeedSortWorldSpaceRenderCanvas = true;
	bool bFrustumCulling = true;
	FScreenSpaceRenderParameter ScreenSpaceRenderParameter;
	TWeakObjectPtr<UWorld> World;
	TArray<FDreamUIMeshBatchContainer> MeshBatchArray;
	//if 'bIsRenderToRenderTarget' is true then we need a render target
	class FTextureRenderTargetResource* RenderTargetResource = nullptr;
	FColor RenderTargetClearColor = FColor::Transparent;
	void SortScreenSpacePrimitiveRenderPriority_RenderThread();
	void SetRenderCanvasDepthFade_RenderThread(UDreamCanvas* InRenderCanvas, float InBlendDepth, int InDepthFade);
	//render thread sample count for MSAA
	uint8 NumSamples_MSAA = 1;
	EDreamUIRendererType RendererType = EDreamUIRendererType::ScreenSpace_and_WorldSpace;

	void RenderDreamUI_RenderThread(
		FRDGBuilder& GraphBuilder
		, FSceneView& InView);
#if WITH_EDITORONLY_DATA
private:
	bool bIsEditorPreview = false;
	mutable bool bCanRenderScreenSpace = true;
	mutable bool bIsPlaying = false;
#endif
#if WITH_EDITOR
private:
	TArray<TSharedPtr<FDreamUIGizmoMesh>> ScreenSpaceGizmoMeshArray;
	TArray<TSharedPtr<FDreamUIGizmoMesh>> WorldSpaceGizmoMeshArray;
	void RenderGizmoMesh_RenderThread(TArray<TSharedPtr<FDreamUIGizmoMesh>>& HelperGizmoDataMap
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

class DREAMGUI_API FDreamUIFullScreenQuadVertexBuffer :public FVertexBuffer
{
public:
	void InitRHI(FRHICommandListBase& RHICmdList)override;
};
class DREAMGUI_API FDreamUIFullScreenQuadIndexBuffer :public FIndexBuffer
{
public:
	void InitRHI(FRHICommandListBase& RHICmdList)override;
};
class DREAMGUI_API FDreamUIFullScreenSlicedQuadIndexBuffer :public FIndexBuffer
{
public:
	void InitRHI(FRHICommandListBase& RHICmdList)override;
};
static TGlobalResource<FDreamUIFullScreenQuadVertexBuffer> GDreamUIFullScreenQuadVertexBuffer;
static TGlobalResource<FDreamUIFullScreenQuadIndexBuffer> GDreamUIFullScreenQuadIndexBuffer;
static TGlobalResource<FDreamUIFullScreenSlicedQuadIndexBuffer> GDreamUIFullScreenSlicedQuadIndexBuffer;
BEGIN_SHADER_PARAMETER_STRUCT(FDreamUIWorldRenderPSParameter, )
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneDepthTex)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()
