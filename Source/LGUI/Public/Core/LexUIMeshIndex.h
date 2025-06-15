// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "DynamicMeshBuilder.h"

#ifdef LEXUI_USE_32BIT_INDEXBUFFER
typedef uint32 FLGUIMeshIndexBufferType;
const int LEXUI_MAX_VERTEX_COUNT = 2147483647;
typedef FDynamicMeshIndexBuffer32 FLGUIMeshIndexBuffer;
#else
typedef uint16 FLexUIMeshIndexBufferType;
const int LEXUI_MAX_VERTEX_COUNT = 65535;
typedef FDynamicMeshIndexBuffer16 FLexUIMeshIndexBuffer;
#endif
