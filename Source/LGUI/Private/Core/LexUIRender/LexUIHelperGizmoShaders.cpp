// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/LexUIRender/LexUIHelperGizmoShaders.h"

IMPLEMENT_SHADER_TYPE(, FLexUIHelperGizmoShaderVS, TEXT("/Plugin/LGUI/Private/LexUIHelperGizmoShader.usf"), TEXT("MainVS"), SF_Vertex)
IMPLEMENT_SHADER_TYPE(, FLexUIHelperGizmoShaderPS, TEXT("/Plugin/LGUI/Private/LexUIHelperGizmoShader.usf"), TEXT("MainPS"), SF_Pixel)
