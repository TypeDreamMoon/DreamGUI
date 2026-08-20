// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/DreamUIRender/DreamUIPostProcessShaders.h"
#include "Materials/Material.h"

// Implement uniform buffer structs for Metal compatibility
IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FDreamUIPostProcessMainTexUB, "DreamUIPostProcessMainTexUB");
IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FDreamUIRenderMeshMainTexUB, "DreamUIRenderMeshMainTexUB");
IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FDreamUIRenderMeshMaskTexUB, "DreamUIRenderMeshMaskTexUB");
IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FDreamUIRenderMeshClipDataTexUB, "DreamUIRenderMeshClipDataTexUB");
IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FDreamUIRenderMeshDepthTexUB, "DreamUIRenderMeshDepthTexUB");

IMPLEMENT_SHADER_TYPE(, FDreamUISimplePostProcessVS, TEXT("/Plugin/DreamGUI/Private/PostProcess/DreamUIPostProcessVertexShader.usf"), TEXT("SimplePostProcessVS"), SF_Vertex)
IMPLEMENT_SHADER_TYPE(, FDreamUIPostProcessGaussianBlurPS, TEXT("/Plugin/DreamGUI/Private/PostProcess/DreamUIPostProcessGaussianBlur.usf"), TEXT("GaussianBlurPS"), SF_Pixel)
IMPLEMENT_SHADER_TYPE(, FDreamUIPostProcessPixelSortRankPS, TEXT("/Plugin/DreamGUI/Private/PostProcess/DreamUIPostProcessPixelSort.usf"), TEXT("RankPS"), SF_Pixel)
IMPLEMENT_SHADER_TYPE(, FDreamUIPostProcessPixelSortGatherPS, TEXT("/Plugin/DreamGUI/Private/PostProcess/DreamUIPostProcessPixelSort.usf"), TEXT("GatherPS"), SF_Pixel)
IMPLEMENT_SHADER_TYPE(, FDreamUISimpleCopyTargetPS, TEXT("/Plugin/DreamGUI/Private/PostProcess/DreamUIPostProcessPixelShader.usf"), TEXT("SimpleCopyTargetPS"), SF_Pixel)
IMPLEMENT_SHADER_TYPE(, FDreamUISimpleCopyTargetPS_ColorCorrect, TEXT("/Plugin/DreamGUI/Private/PostProcess/DreamUIPostProcessPixelShader.usf"), TEXT("SimpleCopyTargetPS"), SF_Pixel)
IMPLEMENT_SHADER_TYPE(, FDreamUISimpleCopyTargetPS_BlendAlpha, TEXT("/Plugin/DreamGUI/Private/PostProcess/DreamUIPostProcessPixelShader.usf"), TEXT("SimpleCopyTargetPS"), SF_Pixel)



IMPLEMENT_SHADER_TYPE(, FDreamUICopyMeshRegionVS, TEXT("/Plugin/DreamGUI/Private/PostProcess/DreamUIPostProcessVertexShader.usf"), TEXT("CopyMeshRegionVS"), SF_Vertex)
IMPLEMENT_SHADER_TYPE(, FDreamUICopyMeshRegionPS, TEXT("/Plugin/DreamGUI/Private/PostProcess/DreamUIPostProcessPixelShader.usf"), TEXT("CopyMeshRegionPS"), SF_Pixel)
IMPLEMENT_SHADER_TYPE(, FDreamUICopyMeshRegionPS_ColorCorrect, TEXT("/Plugin/DreamGUI/Private/PostProcess/DreamUIPostProcessPixelShader.usf"), TEXT("CopyMeshRegionPS"), SF_Pixel)



IMPLEMENT_SHADER_TYPE(, FDreamUIRenderMeshVS, TEXT("/Plugin/DreamGUI/Private/PostProcess/DreamUIRenderMeshVertexShader.usf"), TEXT("RenderMeshVS"), SF_Vertex)

IMPLEMENT_SHADER_TYPE(, FDreamUIRenderMeshPS, TEXT("/Plugin/DreamGUI/Private/PostProcess/DreamUIRenderMeshPixelShader.usf"), TEXT("RenderMeshPS"), SF_Pixel)
IMPLEMENT_SHADER_TYPE(, FDreamUIRenderMeshWithMaskPS, TEXT("/Plugin/DreamGUI/Private/PostProcess/DreamUIRenderMeshPixelShader.usf"), TEXT("RenderMeshPS"), SF_Pixel)

IMPLEMENT_SHADER_TYPE(, FDreamUIRenderMeshPS_Clip, TEXT("/Plugin/DreamGUI/Private/PostProcess/DreamUIRenderMeshPixelShader.usf"), TEXT("RenderMeshPS"), SF_Pixel)
IMPLEMENT_SHADER_TYPE(, FDreamUIRenderMeshWithMaskPS_Clip, TEXT("/Plugin/DreamGUI/Private/PostProcess/DreamUIRenderMeshPixelShader.usf"), TEXT("RenderMeshPS"), SF_Pixel)



IMPLEMENT_SHADER_TYPE(, FDreamUIRenderMeshWorldVS, TEXT("/Plugin/DreamGUI/Private/PostProcess/DreamUIRenderMeshVertexShader.usf"), TEXT("RenderMeshVS"), SF_Vertex)

IMPLEMENT_SHADER_TYPE(, FDreamUIRenderMeshWorldPS_Clip, TEXT("/Plugin/DreamGUI/Private/PostProcess/DreamUIRenderMeshPixelShader.usf"), TEXT("RenderMeshPS"), SF_Pixel)
IMPLEMENT_SHADER_TYPE(, FDreamUIRenderMeshWorldDepthFadePS_Clip, TEXT("/Plugin/DreamGUI/Private/PostProcess/DreamUIRenderMeshPixelShader.usf"), TEXT("RenderMeshPS"), SF_Pixel)
IMPLEMENT_SHADER_TYPE(, FDreamUIRenderMeshWithMaskWorldPS_Clip, TEXT("/Plugin/DreamGUI/Private/PostProcess/DreamUIRenderMeshPixelShader.usf"), TEXT("RenderMeshPS"), SF_Pixel)
IMPLEMENT_SHADER_TYPE(, FDreamUIRenderMeshWithMaskWorldDepthFadePS_Clip, TEXT("/Plugin/DreamGUI/Private/PostProcess/DreamUIRenderMeshPixelShader.usf"), TEXT("RenderMeshPS"), SF_Pixel)
	 