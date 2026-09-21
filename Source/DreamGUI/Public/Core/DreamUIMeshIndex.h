// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "DynamicMeshBuilder.h"
#include <type_traits>

/**
 * Index width for every DreamGUI draw-call mesh.
 *
 * 16 bits by default, which is what UI wants: half the index memory and bandwidth, and 65535 vertices
 * is ~16k quads in a single draw-call. Define LEXUI_USE_32BIT_INDEXBUFFER (PublicDefinitions in a
 * Build.cs, or before including this header) for projects that genuinely need more in ONE element --
 * a single text block or tiled image that cannot be split. Both widths are supported configurations:
 * everything downstream is written against FDreamUIMeshIndex / FDreamUIMeshIndexBuffer, and the
 * static_asserts below are what keep it that way.
 */
#ifdef LEXUI_USE_32BIT_INDEXBUFFER
typedef uint32 FDreamUIMeshIndex;
typedef FDynamicMeshIndexBuffer32 FDreamUIMeshIndexBuffer;
/**
 * Not MAX_uint32, and not even MAX_int32.
 *
 * Two things bound this. Vertex counts are compared against, and added to, TArray::Num(), which is a
 * signed int32 -- so the budget must be an int32. And FDreamUIDrawCall::CanConsumeUIGeometryForBatchMesh
 * evaluates `ThisDrawCall.VerticesCount + Geometry.Vertices.Num()` BEFORE testing it against the
 * budget, so two values that are each just under the budget have to be addable without overflowing.
 * Half of MAX_int32 satisfies both, and is about 32000x what the 16-bit build allows.
 */
const int LEXUI_MAX_VERTEX_COUNT = MAX_int32 / 2;
#else
typedef uint16 FDreamUIMeshIndex;
typedef FDynamicMeshIndexBuffer16 FDreamUIMeshIndexBuffer;
/** The largest ordinal a uint16 index can name. See the 32-bit branch for why the sum must not overflow. */
const int LEXUI_MAX_VERTEX_COUNT = 65535;
#endif

/** Every vertex the budget allows has to be nameable by one index. */
static_assert(LEXUI_MAX_VERTEX_COUNT <= (int64)TNumericLimits<FDreamUIMeshIndex>::Max(),
	"LEXUI_MAX_VERTEX_COUNT is larger than FDreamUIMeshIndex can address; a draw-call would reference vertices it cannot index.");
/** Two draw-calls' counts are summed before the budget is checked, so the sum must stay in int32. */
static_assert((int64)LEXUI_MAX_VERTEX_COUNT * 2 <= (int64)MAX_int32,
	"LEXUI_MAX_VERTEX_COUNT is large enough that VerticesCount + Vertices.Num() can overflow int32 in CanConsumeUIGeometryForBatchMesh.");
/** The index buffer typedef has to be the one that stores FDreamUIMeshIndex, or every memcpy is wrong. */
static_assert(std::is_same_v<decltype(FDreamUIMeshIndexBuffer::Indices)::ElementType, FDreamUIMeshIndex>,
	"FDreamUIMeshIndexBuffer stores a different index type than FDreamUIMeshIndex.");
