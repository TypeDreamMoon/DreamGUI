// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "Core/DreamVisualPostProcessRenderProxy.h"
#include "Core/DreamUIRender/DreamUIPostProcessShaders.h"
#include "Rendering/Texture2DResource.h"
#include "Core/DreamUIRender/DreamUIRenderer.h"
#include "RHIResourceUtils.h"
#include "SceneTextures.h"

BEGIN_SHADER_PARAMETER_STRUCT(FDreamUIPostProcessRenderMeshParameters, )
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneDepthTex)
	RDG_TEXTURE_ACCESS(MeshRegionTexture, ERHIAccess::SRVGraphics)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

FDreamVisualPostProcessRenderProxy::FDreamVisualPostProcessRenderProxy()
{
	
}

#define SET_PIPELINE_STATE_FOR_CLIP()\
FGraphicsPipelineStateInitializer GraphicsPSOInit;\
RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);\
GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, ECompareFunction::CF_Always>::GetRHI();\
GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();\
GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_InverseDestAlpha, BF_One>::GetRHI();\
GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GetDreamUIPostProcessVertexDeclaration();\
GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();\
GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();\
GraphicsPSOInit.PrimitiveType = EPrimitiveType::PT_TriangleList;\
GraphicsPSOInit.NumSamples = NumSamples;\
SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0, EApplyRendertargetOption::ForceApply);

void FDreamVisualPostProcessRenderProxy::RenderMeshOnScreen_RenderThread(
	FRDGBuilder& GraphBuilder
	, const FMinimalSceneTextures& SceneTextures
	, FTextureRHIRef ScreenTargetTexture
	, FGlobalShaderMap* GlobalShaderMap
	, FTextureRHIRef MeshRegionTexture
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
	uint8 NumSamples = ScreenTargetTexture->GetNumSamples();
	auto MeshRegionRDGTexture = RegisterExternalTexture(GraphBuilder, MeshRegionTexture, TEXT("DreamUIPostProcessMeshRegionTexture"));
	auto PSShaderParameters = GraphBuilder.AllocParameters<FDreamUIPostProcessRenderMeshParameters>();
	PSShaderParameters->SceneDepthTex = SceneTextures.Depth.Resolve;
	PSShaderParameters->MeshRegionTexture = MeshRegionRDGTexture;
	PSShaderParameters->RenderTargets[0] = FRenderTargetBinding(RegisterExternalTexture(GraphBuilder, ScreenTargetTexture, TEXT("DreamUIRendererTargetTexture")), ERenderTargetLoadAction::ELoad);

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("UIPostProcess_RenderMeshToScreen"),
		PSShaderParameters,
		ERDGPassFlags::Raster,
		[this, PSShaderParameters, GlobalShaderMap, MeshRegionRDGTexture, ModelViewProjectionMatrix, ModelMatrix, IsWorldSpace, BlendDepthForWorld, DepthFadeForWorld, DepthTextureScaleOffset, ViewRect, ResultTextureSamplerState, NumSamples](FRHICommandListImmediate& RHICmdList)
		{
			MeshRegionRDGTexture->MarkResourceAsUsed();
			auto MeshRegionTextureRHI = MeshRegionRDGTexture->GetRHI();
			RHICmdList.SetViewport(ViewRect.Min.X, ViewRect.Min.Y, 0.0f, ViewRect.Max.X, ViewRect.Max.Y, 1.0f);

			FBufferRHIRef IndexBuffer = nullptr;
			int32 TriangleCount = 2;
			if (MaskTexture != nullptr)
			{
				if (IsWorldSpace)
				{
					if (DepthFadeForWorld <= 0.0f)
					{
						TShaderMapRef<FDreamUIRenderMeshWorldVS> VertexShader(GlobalShaderMap);
						TShaderMapRef<FDreamUIRenderMeshWithMaskWorldPS_Clip> PixelShader(GlobalShaderMap);
						SET_PIPELINE_STATE_FOR_CLIP();
						VertexShader->SetParameters(RHICmdList, ModelViewProjectionMatrix, ModelMatrix);
						PixelShader->SetParameters(RHICmdList, MeshRegionTextureRHI, MaskTexture->TextureRHI
							, ResultTextureSamplerState
							, MaskTexture->SamplerStateRHI
, TintColor, TintMode
						);
						if (ClipDataTexture != nullptr)
						{
							PixelShader->SetClipParameters(RHICmdList, ModelMatrix.Inverse(), ClipDataTexture->TextureRHI, ClipDataTexture->SamplerStateRHI);
						}
						PixelShader->SetDepthBlendParameter(RHICmdList, BlendDepthForWorld, DepthTextureScaleOffset, PSShaderParameters->SceneDepthTex->GetRHI());
					}
					else
					{
						TShaderMapRef<FDreamUIRenderMeshWorldVS> VertexShader(GlobalShaderMap);
						TShaderMapRef<FDreamUIRenderMeshWithMaskWorldDepthFadePS_Clip> PixelShader(GlobalShaderMap);
						SET_PIPELINE_STATE_FOR_CLIP();
						VertexShader->SetParameters(RHICmdList, ModelViewProjectionMatrix, ModelMatrix);
						PixelShader->SetParameters(RHICmdList, MeshRegionTextureRHI, MaskTexture->TextureRHI
							, ResultTextureSamplerState
							, MaskTexture->SamplerStateRHI
, TintColor, TintMode
						);
						if (ClipDataTexture != nullptr)
						{
							PixelShader->SetClipParameters(RHICmdList, ModelMatrix.Inverse(), ClipDataTexture->TextureRHI, ClipDataTexture->SamplerStateRHI);
						}
						PixelShader->SetDepthBlendParameter(RHICmdList, BlendDepthForWorld, DepthTextureScaleOffset, PSShaderParameters->SceneDepthTex->GetRHI());
						PixelShader->SetDepthFadeParameter(RHICmdList, DepthFadeForWorld, FVector2f(1.0f / ViewRect.Width(), 1.0f / ViewRect.Height()));
					}
				}
				else
				{
					TShaderMapRef<FDreamUIRenderMeshVS> VertexShader(GlobalShaderMap);
					TShaderMapRef<FDreamUIRenderMeshWithMaskPS_Clip> PixelShader(GlobalShaderMap);
					SET_PIPELINE_STATE_FOR_CLIP();
					VertexShader->SetParameters(RHICmdList, ModelViewProjectionMatrix, ModelMatrix);
					PixelShader->SetParameters(RHICmdList, MeshRegionTextureRHI, MaskTexture->TextureRHI
						, ResultTextureSamplerState
						, MaskTexture->SamplerStateRHI
, TintColor, TintMode
					);
					if (ClipDataTexture != nullptr)
					{
						PixelShader->SetClipParameters(RHICmdList, ModelMatrix.Inverse(), ClipDataTexture->TextureRHI, ClipDataTexture->SamplerStateRHI);
					}
				}
				IndexBuffer = GDreamUIFullScreenQuadIndexBuffer.IndexBufferRHI;
			}
			else
			{
				if (IsWorldSpace)
				{
					if (DepthFadeForWorld <= 0.0f)
					{
						TShaderMapRef<FDreamUIRenderMeshWorldVS> VertexShader(GlobalShaderMap);
						TShaderMapRef<FDreamUIRenderMeshWorldPS_Clip> PixelShader(GlobalShaderMap);
						SET_PIPELINE_STATE_FOR_CLIP();
						VertexShader->SetParameters(RHICmdList, ModelViewProjectionMatrix, ModelMatrix);
						PixelShader->SetParameters(RHICmdList, MeshRegionTextureRHI, ResultTextureSamplerState, TintColor, TintMode);
						if (ClipDataTexture != nullptr)
						{
							PixelShader->SetClipParameters(RHICmdList, ModelMatrix.Inverse(), ClipDataTexture->TextureRHI);
						}
						PixelShader->SetDepthBlendParameter(RHICmdList, BlendDepthForWorld, DepthTextureScaleOffset, PSShaderParameters->SceneDepthTex->GetRHI());
					}
					else
					{
						TShaderMapRef<FDreamUIRenderMeshWorldVS> VertexShader(GlobalShaderMap);
						TShaderMapRef<FDreamUIRenderMeshWorldDepthFadePS_Clip> PixelShader(GlobalShaderMap);
						SET_PIPELINE_STATE_FOR_CLIP();
						VertexShader->SetParameters(RHICmdList, ModelViewProjectionMatrix, ModelMatrix);
						PixelShader->SetParameters(RHICmdList, MeshRegionTextureRHI, ResultTextureSamplerState, TintColor, TintMode);
						if (ClipDataTexture != nullptr)
						{
							PixelShader->SetClipParameters(RHICmdList, ModelMatrix.Inverse(), ClipDataTexture->TextureRHI);
						}
						PixelShader->SetDepthBlendParameter(RHICmdList, BlendDepthForWorld, DepthTextureScaleOffset, PSShaderParameters->SceneDepthTex->GetRHI());
						PixelShader->SetDepthFadeParameter(RHICmdList, DepthFadeForWorld, FVector2f(1.0f / ViewRect.Width(), 1.0f / ViewRect.Height()));
					}
				}
				else
				{
					TShaderMapRef<FDreamUIRenderMeshVS> VertexShader(GlobalShaderMap);
					TShaderMapRef<FDreamUIRenderMeshPS_Clip> PixelShader(GlobalShaderMap);
					SET_PIPELINE_STATE_FOR_CLIP();
					VertexShader->SetParameters(RHICmdList, ModelViewProjectionMatrix, ModelMatrix);
					PixelShader->SetParameters(RHICmdList, MeshRegionTextureRHI, ResultTextureSamplerState, TintColor, TintMode);
					if (ClipDataTexture != nullptr)
					{
						PixelShader->SetClipParameters(RHICmdList, ModelMatrix.Inverse(), ClipDataTexture->TextureRHI);
					}
				}
				IndexBuffer = GDreamUIFullScreenQuadIndexBuffer.IndexBufferRHI;
			}
			
			FBufferRHIRef VertexBufferRHI = UE::RHIResourceUtils::CreateVertexBufferFromArray(
				RHICmdList, TEXT("RenderMeshOnScreen"), EBufferUsageFlags::Volatile, MakeConstArrayView(RenderMeshRegionToScreenVertexArray)
			);
			RHICmdList.SetStreamSource(0, VertexBufferRHI, 0);
			RHICmdList.DrawIndexedPrimitive(IndexBuffer, 0, 0, RenderMeshRegionToScreenVertexArray.Num(), 0, TriangleCount, 1);
			VertexBufferRHI.SafeRelease();
		});
}
