// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/LexUIRender/LexUIHelperLineShaders.h"
#include "Materials/Material.h"

IMPLEMENT_SHADER_TYPE(, FLexUIHelperLineShaderVS, TEXT("/Plugin/LGUI/Private/LexUIHelperLineShader.usf"), TEXT("MainVS"), SF_Vertex)
IMPLEMENT_SHADER_TYPE(, FLexUIHelperLineShaderPS, TEXT("/Plugin/LGUI/Private/LexUIHelperLineShader.usf"), TEXT("MainPS"), SF_Pixel)
