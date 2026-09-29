// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DreamUIRender/DreamUIPostProcessShaders.h"

IMPLEMENT_GLOBAL_SHADER(FDreamUISimplePostProcessVS, "/Plugin/DreamGUI/Private/PostProcess/DreamUIPostProcessVertexShader.usf", "SimplePostProcessVS", SF_Vertex);
IMPLEMENT_GLOBAL_SHADER(FDreamUISimpleCopyTargetPS, "/Plugin/DreamGUI/Private/PostProcess/DreamUIPostProcessPixelShader.usf", "SimpleCopyTargetPS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FDreamUIPostProcessGaussianBlurPS, "/Plugin/DreamGUI/Private/PostProcess/DreamUIPostProcessGaussianBlur.usf", "GaussianBlurPS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FDreamUIPostProcessPixelSortRankPS, "/Plugin/DreamGUI/Private/PostProcess/DreamUIPostProcessPixelSort.usf", "RankPS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FDreamUIPostProcessPixelSortGatherPS, "/Plugin/DreamGUI/Private/PostProcess/DreamUIPostProcessPixelSort.usf", "GatherPS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FDreamUICopyMeshRegionVS, "/Plugin/DreamGUI/Private/PostProcess/DreamUIPostProcessVertexShader.usf", "CopyMeshRegionVS", SF_Vertex);
IMPLEMENT_GLOBAL_SHADER(FDreamUICopyMeshRegionPS, "/Plugin/DreamGUI/Private/PostProcess/DreamUIPostProcessPixelShader.usf", "CopyMeshRegionPS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FDreamUIRenderMeshVS, "/Plugin/DreamGUI/Private/PostProcess/DreamUIRenderMeshVertexShader.usf", "RenderMeshVS", SF_Vertex);
IMPLEMENT_GLOBAL_SHADER(FDreamUIRenderMeshPS, "/Plugin/DreamGUI/Private/PostProcess/DreamUIRenderMeshPixelShader.usf", "RenderMeshPS", SF_Pixel);
