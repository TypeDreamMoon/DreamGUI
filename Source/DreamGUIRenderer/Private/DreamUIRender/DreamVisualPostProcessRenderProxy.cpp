// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "DreamUIRender/DreamVisualPostProcessRenderProxy.h"
#include "DreamUIRender/DreamUIPostProcessShaders.h"
#include "Rendering/Texture2DResource.h"
#include "DreamUIRender/DreamUIRenderer.h"
#include "Engine/Texture.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GlobalRenderResources.h"
#include "RHIResourceUtils.h"
#include "TextureResource.h"

BEGIN_SHADER_PARAMETER_STRUCT(FDreamUIPostProcessRenderMeshParameters, )
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneDepthTex)
	RDG_TEXTURE_ACCESS(MeshRegionTexture, ERHIAccess::SRVGraphics)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

FDreamVisualPostProcessRenderProxy::FDreamVisualPostProcessRenderProxy()
{
	
}

void FDreamVisualPostProcessRenderProxy::ReleaseOnRenderThread(FDreamVisualPostProcessRenderProxyPtr&& InProxy)
{
	if (!InProxy.IsValid())
	{
		return;
	}
	// The mesh section and any update command still in flight hold their own references, so the proxy dies
	// when the last of them lets go -- and because every one of those is released on the render thread, the
	// destructor (which touches render resources) always runs there.
	ENQUEUE_RENDER_COMMAND(FDreamPostProcess_ReleaseRenderProxy)
		([ReleasedProxy = MoveTemp(InProxy)](FRHICommandListImmediate& RHICmdList) mutable
			{
				ReleasedProxy.Reset();
			});
}

void FDreamVisualPostProcessRenderProxy::SetCommonParams_GameThread(const FDreamVisualPostProcessRenderProxyPtr& InProxy, FDreamUIPostProcessCommonParams&& InParams)
{
	if (!InProxy.IsValid())
	{
		return;
	}
	ENQUEUE_RENDER_COMMAND(FDreamPostProcess_UpdateData)
		([TempRenderProxy = InProxy, Params = MoveTemp(InParams)](FRHICommandListImmediate& RHICmdList) mutable
			{
				TempRenderProxy->RenderScreenToMeshRegionVertexArray = MoveTemp(Params.ScreenToMeshRegionVertices);
				TempRenderProxy->RenderMeshRegionToScreenVertexArray = MoveTemp(Params.MeshRegionToScreenVertices);
				TempRenderProxy->RectSize = Params.RectSize;
				TempRenderProxy->ObjectToWorldMatrix = Params.ObjectToWorldMatrix;
				// The texture was alive when this was enqueued, and anything that releases it is enqueued after.
				TempRenderProxy->ClipDataTextureRHI = Params.ClipDataTexture != nullptr ? Params.ClipDataTexture->TextureReference.TextureReferenceRHI : FTextureReferenceRHIRef();
				TempRenderProxy->bUseFullSize = Params.bUseFullSize;
				TempRenderProxy->BoundingBox = Params.BoundingBox;
				TempRenderProxy->TintColor = Params.TintColor;
				TempRenderProxy->TintMode = Params.TintMode;
			});
}

void FDreamVisualPostProcessRenderProxy::SetMaskTexture_GameThread(const FDreamVisualPostProcessRenderProxyPtr& InProxy, FTexture2DResource* InMaskTextureResource)
{
	if (!InProxy.IsValid())
	{
		return;
	}
	ENQUEUE_RENDER_COMMAND(FDreamPostProcess_UpdateMaskTexture)
		([TempRenderProxy = InProxy, MaskTextureResource = InMaskTextureResource](FRHICommandListImmediate& RHICmdList)
			{
				// Read the resource here, on the render thread, and keep only ref-counted handles: the resource
				// itself is deleted whenever the texture's resource is rebuilt, with no notification to this
				// proxy. Dereferencing it now is safe because the pointer was taken from the texture on the game
				// thread just before this command was enqueued, and the delete for it can only be enqueued after.
				if (MaskTextureResource != nullptr)
				{
					TempRenderProxy->MaskTextureRHI = MaskTextureResource->TextureRHI;
					TempRenderProxy->MaskTextureSamplerState = MaskTextureResource->SamplerStateRHI;
				}
				else
				{
					TempRenderProxy->MaskTextureRHI = nullptr;
					TempRenderProxy->MaskTextureSamplerState = nullptr;
				}
			});
}

void FDreamVisualPostProcessRenderProxy::SetRenderTarget_GameThread(const FDreamVisualPostProcessRenderProxyPtr& InProxy, UTextureRenderTarget2D* InRenderTarget)
{
	if (!InProxy.IsValid())
	{
		return;
	}
	ENQUEUE_RENDER_COMMAND(FDreamPostProcess_UpdateRenderTarget)
		([TempRenderProxy = InProxy, RenderTarget = InRenderTarget](FRHICommandListImmediate& RHICmdList)
			{
				// The target's resource as the render thread sees it now, read by the command the game thread enqueued
				// while the target was alive; only the texture is kept, and it keeps itself alive.
				FTextureRHIRef Texture;
				if (RenderTarget != nullptr)
				{
					if (FTextureRenderTargetResource* Resource = RenderTarget->GetRenderTargetResource())
					{
						Texture = Resource->GetRenderTargetTexture();
					}
				}
				TempRenderProxy->OutputTargetTexture = Texture;
			});
}

FDreamVisualPostProcessRenderProxy::FScreenRead FDreamVisualPostProcessRenderProxy::ReadScreen_RenderThread(FRDGBuilder& GraphBuilder
	, FDreamUIRenderer* Renderer, FGlobalShaderMap* GlobalShaderMap, FTextureRHIRef ScreenTargetTexture)
{
	FScreenRead Screen;
	Screen.Target = RegisterExternalTexture(GraphBuilder, ScreenTargetTexture, TEXT("DreamUIPostProcessScreen"));
	Screen.NumSamples = Screen.Target->Desc.NumSamples;
	Screen.Size = Screen.Target->Desc.Extent;
	Screen.Readable = Screen.Target;
	if (Screen.NumSamples > 1)
	{
		Screen.Readable = CreateWorkTexture(GraphBuilder, Screen, Screen.Size, TEXT("DreamUIPostProcessScreenResolved"));
		Renderer->AddResolvePass(GraphBuilder, FRDGTextureMSAA(Screen.Target, Screen.Readable), FIntRect(FIntPoint::ZeroValue, Screen.Size), Screen.NumSamples, GlobalShaderMap);
	}
	return Screen;
}

FRDGTextureRef FDreamVisualPostProcessRenderProxy::CreateWorkTexture(FRDGBuilder& GraphBuilder, const FScreenRead& InScreen, FIntPoint InSize
	, const TCHAR* InName, EPixelFormat InFormat)
{
	const FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(FIntPoint(FMath::Max(InSize.X, 1), FMath::Max(InSize.Y, 1))
		, InFormat == PF_Unknown ? InScreen.Target->Desc.Format : InFormat, FClearValueBinding::Black
		, TexCreate_RenderTargetable | TexCreate_ShaderResource);
	return GraphBuilder.CreateTexture(Desc, InName);
}

void FDreamVisualPostProcessRenderProxy::GrabRegion_RenderThread(FRDGBuilder& GraphBuilder, FDreamUIRenderer* Renderer, FGlobalShaderMap* GlobalShaderMap
	, const FScreenRead& InScreen, FRDGTextureRef InWork, bool bInWholeScreen, const FMatrix44f& ModelViewProjectionMatrix, bool bIsRenderTarget
	, const FVector4f& ViewTextureScaleOffset) const
{
	if (bInWholeScreen)
	{
		Renderer->CopyRenderTarget(GraphBuilder, GlobalShaderMap, InScreen.Readable, InWork);
	}
	else
	{
		Renderer->CopyRenderTargetOnMeshRegion(GraphBuilder, InWork, InScreen.Readable, GlobalShaderMap, RenderScreenToMeshRegionVertexArray
			, ModelViewProjectionMatrix, bIsRenderTarget, FIntRect(FIntPoint::ZeroValue, InWork->Desc.Extent), ViewTextureScaleOffset);
	}
}

void FDreamVisualPostProcessRenderProxy::WriteBack_RenderThread(FRDGBuilder& GraphBuilder, FDreamUIRenderer* Renderer, FGlobalShaderMap* GlobalShaderMap
	, FRDGTextureRef SceneDepth, const FScreenRead& InScreen, FRDGTextureRef InResult, bool bInWholeScreen, const FMatrix44f& ModelViewProjectionMatrix
	, bool bIsWorldSpace, float BlendDepthForWorld, int DepthFadeForWorld, const FVector4f& DepthTextureScaleOffset, const FIntRect& ViewRect
	, FRHISamplerState* InSampler)
{
	if (OutputTargetTexture.IsValid())
	{
		Renderer->CopyRenderTarget_ColorCorrect(GraphBuilder, GlobalShaderMap, InResult
			, RegisterExternalTexture(GraphBuilder, OutputTargetTexture, TEXT("DreamUIPostProcessOutputTarget")), InSampler);
	}
	else if (!bInWholeScreen)
	{
		RenderMeshOnScreen_RenderThread(GraphBuilder, SceneDepth, InScreen.Target, GlobalShaderMap, InResult, ModelViewProjectionMatrix, ObjectToWorldMatrix
			, bIsWorldSpace, BlendDepthForWorld, DepthFadeForWorld, DepthTextureScaleOffset, ViewRect, InSampler);
	}
	else if (InResult != InScreen.Target)
	{
		// The whole screen, worked on in a texture of its own -- or in place in a multisampled target's resolved copy, which has
		// to go back into the target: the resolve that ends the UI's recording writes the target over the picture.
		Renderer->CopyRenderTarget(GraphBuilder, GlobalShaderMap, InResult, InScreen.Target, InSampler);
	}
}

void FDreamVisualPostProcessRenderProxy::RenderMeshOnScreen_RenderThread(
	FRDGBuilder& GraphBuilder
	, FRDGTextureRef SceneDepth
	, FRDGTextureRef ScreenTarget
	, FGlobalShaderMap* GlobalShaderMap
	, FRDGTextureRef MeshRegionTexture
	, const FMatrix44f& ModelViewProjectionMatrix
	, const FMatrix44f& ModelMatrix
	, bool IsWorldSpace
	, float BlendDepthForWorld
	, int DepthFadeForWorld
	, const FVector4f& DepthTextureScaleOffset
	, const FIntRect& ViewRect
	, FRHISamplerState* ResultTextureSamplerState
)
{
	uint8 NumSamples = ScreenTarget->Desc.NumSamples;
	auto MeshRegionRDGTexture = MeshRegionTexture;
	auto PSShaderParameters = GraphBuilder.AllocParameters<FDreamUIPostProcessRenderMeshParameters>();
	PSShaderParameters->SceneDepthTex = SceneDepth;
	PSShaderParameters->MeshRegionTexture = MeshRegionRDGTexture;
	PSShaderParameters->RenderTargets[0] = FRenderTargetBinding(ScreenTarget, ERenderTargetLoadAction::ELoad);

	// The proxy is read when the pass runs, and lives until then: on the immediate list the pass runs inline, inside the
	// graph's Execute, which the render command that recorded it reaches before it returns -- and the mesh section that
	// holds the proxy can only let go of it in a later render command.
	GraphBuilder.AddPass(
		RDG_EVENT_NAME("UIPostProcess_RenderMeshToScreen"),
		PSShaderParameters,
		ERDGPassFlags::Raster,
		[this, PSShaderParameters, GlobalShaderMap, MeshRegionRDGTexture, ModelViewProjectionMatrix, ModelMatrix, IsWorldSpace, BlendDepthForWorld, DepthFadeForWorld, DepthTextureScaleOffset, ViewRect, ResultTextureSamplerState, NumSamples](FRHICommandListImmediate& RHICmdList)
		{
			MeshRegionRDGTexture->MarkResourceAsUsed();
			auto MeshRegionTextureRHI = MeshRegionRDGTexture->GetRHI();
			RHICmdList.SetViewport(ViewRect.Min.X, ViewRect.Min.Y, 0.0f, ViewRect.Max.X, ViewRect.Max.Y, 1.0f);

			// One shader for every case: the mask, the blend against the scene's depth for a world-space canvas, and that
			// blend's fade are its permutations.
			const bool bMask = MaskTextureRHI.IsValid();
			FDreamUIRenderMeshVS::FPermutationDomain VertexPermutation;
			VertexPermutation.Set<FDreamUIRenderMeshVS::FBlendDepth>(IsWorldSpace);
			TShaderMapRef<FDreamUIRenderMeshVS> VertexShader(GlobalShaderMap, VertexPermutation);
			FDreamUIRenderMeshPS::FPermutationDomain PixelPermutation;
			PixelPermutation.Set<FDreamUIRenderMeshPS::FMask>(bMask);
			PixelPermutation.Set<FDreamUIRenderMeshPS::FBlendDepth>(IsWorldSpace);
			PixelPermutation.Set<FDreamUIRenderMeshPS::FDepthFade>(IsWorldSpace && DepthFadeForWorld > 0);
			TShaderMapRef<FDreamUIRenderMeshPS> PixelShader(GlobalShaderMap, PixelPermutation);

			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, ECompareFunction::CF_Always>::GetRHI();
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
			GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_InverseDestAlpha, BF_One>::GetRHI();
			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GetDreamUIPostProcessVertexDeclaration();
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
			GraphicsPSOInit.PrimitiveType = EPrimitiveType::PT_TriangleList;
			GraphicsPSOInit.NumSamples = NumSamples;
			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0, EApplyRendertargetOption::ForceApply);

			FDreamUIRenderMeshVS::FParameters VertexParameters;
			VertexParameters.LocalToClip = ModelViewProjectionMatrix;
			VertexParameters.LocalToWorld = ModelMatrix;
			SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), VertexParameters);

			// Every texture a permutation reads is bound: one that is not there is a fallback that changes nothing.
			const auto BilinearSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
			FDreamUIRenderMeshPS::FParameters PixelParameters;
			PixelParameters.MainTex = MeshRegionTextureRHI;
			PixelParameters.MainTexSampler = ResultTextureSamplerState != nullptr ? ResultTextureSamplerState : BilinearSampler;
			PixelParameters.TintColor = TintColor;
			PixelParameters.TintMode = TintMode;
			PixelParameters.MaskTex = bMask ? MaskTextureRHI.GetReference() : GWhiteTexture->TextureRHI.GetReference();
			PixelParameters.MaskTexSampler = MaskTextureSamplerState.IsValid() ? MaskTextureSamplerState.GetReference() : BilinearSampler;
			PixelParameters.ClipDataTex = ClipDataTextureRHI.IsValid() ? static_cast<FRHITexture*>(ClipDataTextureRHI.GetReference()) : GBlackTexture->TextureRHI.GetReference();
			PixelParameters.SceneDepthTex = IsWorldSpace ? PSShaderParameters->SceneDepthTex->GetRHI() : GBlackTexture->TextureRHI.GetReference();
			PixelParameters.SceneDepthTexSampler = BilinearSampler;
			PixelParameters.SceneDepthTextureScaleOffset = DepthTextureScaleOffset;
			PixelParameters.SceneDepthBlend = BlendDepthForWorld;
			PixelParameters.SceneDepthFade = DepthFadeForWorld;
			PixelParameters.ViewSizeInv = FVector2f(1.0f / FMath::Max(ViewRect.Width(), 1), 1.0f / FMath::Max(ViewRect.Height(), 1));
			SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), PixelParameters);

			FBufferRHIRef VertexBufferRHI = UE::RHIResourceUtils::CreateVertexBufferFromArray(
				RHICmdList, TEXT("RenderMeshOnScreen"), EBufferUsageFlags::Volatile, MakeConstArrayView(RenderMeshRegionToScreenVertexArray)
			);
			RHICmdList.SetStreamSource(0, VertexBufferRHI, 0);
			RHICmdList.DrawIndexedPrimitive(GDreamUIFullScreenQuadIndexBuffer.IndexBufferRHI, 0, 0, RenderMeshRegionToScreenVertexArray.Num(), 0, 2, 1);
			VertexBufferRHI.SafeRelease();
		});
}
