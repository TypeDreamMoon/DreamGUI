// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once

#include "DreamUIRender/IDreamUIRendererPrimitive.h"
#include "DreamUIRender/DreamUIPostProcessVertex.h"
#include "RenderGraphFwd.h"
#include "RHIStaticStates.h"
#include "TextureResource.h"

class FTexture2DResource;
class UTexture;
class UTextureRenderTarget2D;

/**
 * What a post-process visual sends its proxy whenever its region, transform or tint changes: worked out on the
 * game thread and handed over whole, so the render thread never reads the visual.
 */
struct FDreamUIPostProcessCommonParams
{
	TArray<FDreamUIPostProcessCopyMeshRegionVertex> ScreenToMeshRegionVertices;
	TArray<FDreamUIPostProcessVertex> MeshRegionToScreenVertices;
	FVector2f RectSize = FVector2f::ZeroVector;
	FMatrix44f ObjectToWorldMatrix = FMatrix44f::Identity;
	/**
	 * The canvas's clip data texture. Read on the render thread by the command that carries these, and only there: the
	 * proxy keeps the texture's reference, which follows it as it grows (see FDreamVisualPostProcessRenderProxy).
	 */
	const UTexture* ClipDataTexture = nullptr;
	bool bUseFullSize = false;
	FBox BoundingBox = FBox(EForceInit::ForceInit);
	/** See FDreamVisualPostProcessRenderProxy::TintColor. */
	FVector4f TintColor = FVector4f(1, 1, 1, 1);
	int32 TintMode = 0;
};

/**
 * DreamVisualPostProcessRenderProxy is a render-agent for DreamVisualPostProcess in render thread, just like a SceneProxy for PrimitiveComponent.
 *
 * Owned through FDreamVisualPostProcessRenderProxyPtr by three parties at once -- the visual, the mesh
 * section, and any render command in flight -- and never deleted directly. Every one of those releases
 * its reference on the render thread (the visual hands its own to a render command in BeginDestroy), so
 * whichever is last, the destructor runs there. Everything below is therefore free to be render-thread
 * state; subclasses may keep RHI references without arranging a deferred release of their own.
 */
class DREAMGUIRENDERER_API FDreamVisualPostProcessRenderProxy
{
public:
	FDreamVisualPostProcessRenderProxy();
	virtual~FDreamVisualPostProcessRenderProxy()
	{
		
	}

	/*
	 * The game thread's side. Each call enqueues one render command, in the order the calls are made, and the
	 * command keeps its own reference to the proxy, so the caller may let go of it straight after.
	 */
	/** Give up InProxy on the render thread, where the last reference must go because the destructor runs there. */
	static void ReleaseOnRenderThread(FDreamVisualPostProcessRenderProxyPtr&& InProxy);
	static void SetCommonParams_GameThread(const FDreamVisualPostProcessRenderProxyPtr& InProxy, FDreamUIPostProcessCommonParams&& InParams);
	/** InMaskTextureResource is the mask texture's resource as the game thread sees it now, or null for no mask. */
	static void SetMaskTexture_GameThread(const FDreamVisualPostProcessRenderProxyPtr& InProxy, FTexture2DResource* InMaskTextureResource);
	/**
	 * Null draws the effect to the screen; otherwise it is drawn into this render target. The command takes the
	 * target's texture on the render thread and keeps it, so a target collected afterwards cannot leave the proxy
	 * drawing into freed memory; a target that is resized is sent again by the visual.
	 */
	static void SetRenderTarget_GameThread(const FDreamVisualPostProcessRenderProxyPtr& InProxy, UTextureRenderTarget2D* InRenderTarget);
private:
	TWeakPtr<FDreamUIRenderer, ESPMode::ThreadSafe> DreamRenderer;
	bool bIsWorld = false;//is world space or screen space
public:
	/**
	 * Multiplied onto the captured background when the effect is composited back onto the screen.
	 * Comes from the visual's Color (RGB only — alpha keeps whatever meaning the effect gives it, e.g. background
	 * blur uses it for blur strength). White is the default and leaves the background untouched.
	 */
	FVector4f TintColor = FVector4f(1, 1, 1, 1);
	/** EDreamPostProcessTintMode as an int, to keep this render-thread struct free of UObject headers. */
	int32 TintMode = 0;
	virtual bool CanRender() const = 0;
	/**
	 * render thread function that will do the post process draw
	 * @param	SceneDepth						The view's resolved scene depth, which a world-space effect is tested against
	 * @param	ScreenTargetTexture				The full screen render target
	 * @param	ViewProjectionMatrix			For vertex shader to convert vertex to screen space. vertex position is already transformed to world space, so we dont need model matrix
	 */
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
	) = 0;
public:
	/**
	 * The clip data texture's reference, which is whatever texture the canvas's clip data lives in now -- it grows in
	 * place -- and outlives it. The resource pointer it replaces was sent once and read every frame after, and the
	 * texture it pointed into could be rebuilt or collected in between.
	 */
	FTextureReferenceRHIRef ClipDataTextureRHI;
	
	FMatrix44f ObjectToWorldMatrix = FMatrix44f::Identity;
	TArray<FDreamUIPostProcessCopyMeshRegionVertex> RenderScreenToMeshRegionVertexArray;
	TArray<FDreamUIPostProcessVertex> RenderMeshRegionToScreenVertexArray;
	FVector2f RectSize;
	/**
	 * The mask, held as RHI handles rather than as the FTexture2DResource* it came from.
	 *
	 * That resource belongs to the UTexture2D and is deleted on the render thread whenever the
	 * texture's resource is rebuilt -- a re-import, a compression or size change, any UpdateResource,
	 * a streaming rebuild -- and nothing informs this proxy, which then draws through a freed
	 * pointer. These two are ref-counted, so the worst case is drawing with the mask as it was when
	 * it was last sent instead of a use-after-free; the visual re-sends on every change it knows about.
	 */
	FTextureRHIRef MaskTextureRHI;
	FSamplerStateRHIRef MaskTextureSamplerState;
	bool bUseFullSize = false;
	FBox BoundingBox;
	/** The render target the effect draws into, or null to draw it to the screen. See SetRenderTarget_GameThread. */
	FTextureRHIRef OutputTargetTexture;

	/**
	 * Use a mesh to render the MeshRegionTexture to ScreenTargetTexture
	 */
	void RenderMeshOnScreen_RenderThread(
		FRDGBuilder& GraphBuilder
		, FRDGTextureRef SceneDepth
		, FTextureRHIRef ScreenTargetTexture
		, FGlobalShaderMap* GlobalShaderMap
		, FTextureRHIRef MeshRegionTexture
		, const FMatrix44f & ModelViewProjectionMatrix
		, const FMatrix44f & ModelMatrix
		, bool IsWorldSpace
		, float BlendDepthForWorld
		, int DepthFadeForWorld
		, const FVector4f& DepthTextureScaleOffset
		, const FIntRect& ViewRect
		, FRHISamplerState* ResultTextureSamplerState = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI()
	);
};
