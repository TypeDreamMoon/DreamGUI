// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DreamUIRender/DreamUIResolveShaders.h"

IMPLEMENT_GLOBAL_SHADER(FDreamUIResolveShaderVS, "/Plugin/DreamGUI/Private/DreamUIResolveShader.usf", "DreamUIResolveVS", SF_Vertex);
IMPLEMENT_GLOBAL_SHADER(FDreamUIResolveShaderPS, "/Plugin/DreamGUI/Private/DreamUIResolveShader.usf", "DreamUIResolvePS", SF_Pixel);
