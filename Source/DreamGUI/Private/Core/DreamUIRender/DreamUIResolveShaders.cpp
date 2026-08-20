// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/DreamUIRender/DreamUIResolveShaders.h"
#include "Materials/Material.h"

IMPLEMENT_SHADER_TYPE(, FDreamUIResolveShaderVS, TEXT("/Plugin/DreamGUI/Private/DreamUIResolveShader.usf"), TEXT("DreamUIResolveVS"), SF_Vertex)
IMPLEMENT_SHADER_TYPE(, FDreamUIResolveShader2xPS, TEXT("/Plugin/DreamGUI/Private/DreamUIResolveShader.usf"), TEXT("DreamUIResolve2xPS"), SF_Pixel)
IMPLEMENT_SHADER_TYPE(, FDreamUIResolveShader4xPS, TEXT("/Plugin/DreamGUI/Private/DreamUIResolveShader.usf"), TEXT("DreamUIResolve4xPS"), SF_Pixel)
IMPLEMENT_SHADER_TYPE(, FDreamUIResolveShader8xPS, TEXT("/Plugin/DreamGUI/Private/DreamUIResolveShader.usf"), TEXT("DreamUIResolve8xPS"), SF_Pixel)
