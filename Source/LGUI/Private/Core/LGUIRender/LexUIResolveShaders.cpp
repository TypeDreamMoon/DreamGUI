// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/LGUIRender/LexUIResolveShaders.h"
#include "LGUI.h"
#include "Materials/Material.h"

IMPLEMENT_SHADER_TYPE(, FLexUIResolveShaderVS, TEXT("/Plugin/LGUI/Private/LexUIResolveShader.usf"), TEXT("LexUIResolveVS"), SF_Vertex)
IMPLEMENT_SHADER_TYPE(, FLexUIResolveShader2xPS, TEXT("/Plugin/LGUI/Private/LexUIResolveShader.usf"), TEXT("LexUIResolve2xPS"), SF_Pixel)
IMPLEMENT_SHADER_TYPE(, FLexUIResolveShader4xPS, TEXT("/Plugin/LGUI/Private/LexUIResolveShader.usf"), TEXT("LexUIResolve4xPS"), SF_Pixel)
IMPLEMENT_SHADER_TYPE(, FLexUIResolveShader8xPS, TEXT("/Plugin/LGUI/Private/LexUIResolveShader.usf"), TEXT("LexUIResolve8xPS"), SF_Pixel)
