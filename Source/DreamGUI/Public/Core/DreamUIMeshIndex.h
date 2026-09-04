// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "DynamicMeshBuilder.h"

#ifdef LEXUI_USE_32BIT_INDEXBUFFER
typedef uint32 FDreamUIMeshIndex;
const int LEXUI_MAX_VERTEX_COUNT = 2147483647;
typedef FDynamicMeshIndexBuffer32 FDreamUIMeshIndexBuffer;
#else
typedef uint16 FDreamUIMeshIndex;
const int LEXUI_MAX_VERTEX_COUNT = 65535;
typedef FDynamicMeshIndexBuffer16 FDreamUIMeshIndexBuffer;
#endif
