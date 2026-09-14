// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once

#include "DreamUIRender/IDreamUIRendererPrimitive.h"
#include "DreamUIRender/DreamUIPostProcessVertex.h"
#include "RHIStaticStates.h"
#include "SceneTextures.h"
#include "TextureResource.h"

class UDreamCanvas;
class UDreamVisualPostProcess;

/**
 * DreamVisualPostProcessRenderProxy is a render-agent for DreamVisualPostProcess in render thread, just like a SceneProxy for PrimitiveComponent.
 *
 * Owned through FDreamVisualPostProcessRenderProxyPtr by three parties at once -- the visual, the mesh
 * section, and any render command in flight -- and never deleted directly. Every one of those releases
 * its reference on the render thread (the visual hands its own to a render command in BeginDestroy), so
 * whichever is last, the destructor runs there. Everything below is therefore free to be render-thread
 * state; subclasses may keep RHI references without arranging a deferred release of their own.
 */
class DREAMGUI_API FDreamVisualPostProcessRenderProxy
{
public:
	FDreamVisualPostProcessRenderProxy();
	virtual~FDreamVisualPostProcessRenderProxy()
	{
		
	}
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
	 * @param	ScreenTargetTexture				The full screen render target
	 * @param	ViewProjectionMatrix			For vertex shader to convert vertex to screen space. vertex position is already transformed to world space, so we dont need model matrix
	 */
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
	) = 0;
public:
	FTexture2DDynamicResource* ClipDataTexture = nullptr;
	
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
	//output target
	FTextureRenderTargetResource* RenderTargetResource = nullptr;

	/**
	 * Use a mesh to render the MeshRegionTexture to ScreenTargetTexture
	 */
	void RenderMeshOnScreen_RenderThread(
		FRDGBuilder& GraphBuilder
		, const FMinimalSceneTextures& SceneTextures
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
