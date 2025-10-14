// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/LexUIRender/LexUIPostProcessShaders.h"
#include "Materials/Material.h"

IMPLEMENT_SHADER_TYPE(, FLexUISimplePostProcessVS, TEXT("/Plugin/LGUI/Private/PostProcess/LexUIPostProcessVertexShader.usf"), TEXT("SimplePostProcessVS"), SF_Vertex)
IMPLEMENT_SHADER_TYPE(, FLexUIPostProcessGaussianBlurPS, TEXT("/Plugin/LGUI/Private/PostProcess/LexUIPostProcessGaussianBlur.usf"), TEXT("GaussianBlurPS"), SF_Pixel)
IMPLEMENT_SHADER_TYPE(, FLexUISimpleCopyTargetPS, TEXT("/Plugin/LGUI/Private/PostProcess/LexUIPostProcessPixelShader.usf"), TEXT("SimpleCopyTargetPS"), SF_Pixel)
IMPLEMENT_SHADER_TYPE(, FLexUISimpleCopyTargetPS_ColorCorrect, TEXT("/Plugin/LGUI/Private/PostProcess/LexUIPostProcessPixelShader.usf"), TEXT("SimpleCopyTargetPS"), SF_Pixel)



IMPLEMENT_SHADER_TYPE(, FLexUICopyMeshRegionVS, TEXT("/Plugin/LGUI/Private/PostProcess/LexUIPostProcessVertexShader.usf"), TEXT("CopyMeshRegionVS"), SF_Vertex)
IMPLEMENT_SHADER_TYPE(, FLexUICopyMeshRegionPS, TEXT("/Plugin/LGUI/Private/PostProcess/LexUIPostProcessPixelShader.usf"), TEXT("CopyMeshRegionPS"), SF_Pixel)
IMPLEMENT_SHADER_TYPE(, FLexUICopyMeshRegionPS_ColorCorrect, TEXT("/Plugin/LGUI/Private/PostProcess/LexUIPostProcessPixelShader.usf"), TEXT("CopyMeshRegionPS"), SF_Pixel)



IMPLEMENT_SHADER_TYPE(, FLexUIRenderMeshVS, TEXT("/Plugin/LGUI/Private/PostProcess/LexUIRenderMeshVertexShader.usf"), TEXT("RenderMeshVS"), SF_Vertex)

IMPLEMENT_SHADER_TYPE(, FLexUIRenderMeshPS, TEXT("/Plugin/LGUI/Private/PostProcess/LexUIRenderMeshPixelShader.usf"), TEXT("RenderMeshPS"), SF_Pixel)
IMPLEMENT_SHADER_TYPE(, FLexUIRenderMeshWithMaskPS, TEXT("/Plugin/LGUI/Private/PostProcess/LexUIRenderMeshPixelShader.usf"), TEXT("RenderMeshPS"), SF_Pixel)

IMPLEMENT_SHADER_TYPE(, FLexUIRenderMeshPS_Clip, TEXT("/Plugin/LGUI/Private/PostProcess/LexUIRenderMeshPixelShader.usf"), TEXT("RenderMeshPS"), SF_Pixel)
IMPLEMENT_SHADER_TYPE(, FLexUIRenderMeshWithMaskPS_Clip, TEXT("/Plugin/LGUI/Private/PostProcess/LexUIRenderMeshPixelShader.usf"), TEXT("RenderMeshPS"), SF_Pixel)



IMPLEMENT_SHADER_TYPE(, FLexUIRenderMeshWorldVS, TEXT("/Plugin/LGUI/Private/PostProcess/LexUIRenderMeshVertexShader.usf"), TEXT("RenderMeshVS"), SF_Vertex)

IMPLEMENT_SHADER_TYPE(, FLexUIRenderMeshWorldPS_Clip, TEXT("/Plugin/LGUI/Private/PostProcess/LexUIRenderMeshPixelShader.usf"), TEXT("RenderMeshPS"), SF_Pixel)
IMPLEMENT_SHADER_TYPE(, FLexUIRenderMeshWorldDepthFadePS_Clip, TEXT("/Plugin/LGUI/Private/PostProcess/LexUIRenderMeshPixelShader.usf"), TEXT("RenderMeshPS"), SF_Pixel)
IMPLEMENT_SHADER_TYPE(, FLexUIRenderMeshWithMaskWorldPS_Clip, TEXT("/Plugin/LGUI/Private/PostProcess/LexUIRenderMeshPixelShader.usf"), TEXT("RenderMeshPS"), SF_Pixel)
IMPLEMENT_SHADER_TYPE(, FLexUIRenderMeshWithMaskWorldDepthFadePS_Clip, TEXT("/Plugin/LGUI/Private/PostProcess/LexUIRenderMeshPixelShader.usf"), TEXT("RenderMeshPS"), SF_Pixel)
	 