// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/DreamUIRender/DreamUIBaseShaders.h"

IMPLEMENT_GLOBAL_SHADER(FDreamUIBaseVS, "/Plugin/DreamGUI/Private/DreamUIBase.usf", "MainVS", SF_Vertex);
IMPLEMENT_GLOBAL_SHADER(FDreamUIBasePS, "/Plugin/DreamGUI/Private/DreamUIBase.usf", "MainPS", SF_Pixel);
