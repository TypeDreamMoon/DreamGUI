// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/LexUIDrawCall.h"
#include "Core/LexUIGeometry.h"
#include "LGUI.h"
#include "DynamicMeshBuilder.h"
#include "LGUI/Public/Core/Components/LGUICanvas.h"
#include "LGUI/Public/Core/Components/UIPostProcessRenderable.h"
#include "LGUI/Public/Core/Components/UIBatchMeshRenderable.h"
#include "LGUI/Public/Core/Components/UIDirectMeshRenderable.h"
#include "Core/LGUISettings.h"

void FLexUIDrawCall::GetCombined(TArray<FLexUIMeshVertex>& vertices, TArray<FLexUIMeshIndexBufferType>& triangles)const
{
	int count = RenderObjectList.Num();
	if (count == 1)
	{
		auto uiGeo = RenderObjectList[0]->GetGeometry();
		vertices = uiGeo->Vertices;
		triangles = uiGeo->Triangles;
	}
	else
	{
		int prevVertexCount = 0;
		int triangleIndicesIndex = 0;
		vertices.Reserve(this->VerticesCount);
		triangles.SetNumUninitialized(this->IndicesCount);
		for (int geoIndex = 0; geoIndex < count; geoIndex++)
		{
			auto uiGeo = RenderObjectList[geoIndex]->GetGeometry();
			auto& geomTriangles = uiGeo->Triangles;
			int triangleCount = geomTriangles.Num();
			if (triangleCount <= 0)continue;
			vertices.Append(uiGeo->Vertices);
			for (int geomTriangleIndicesIndex = 0; geomTriangleIndicesIndex < triangleCount; geomTriangleIndicesIndex++)
			{
				auto triangleIndex = geomTriangles[geomTriangleIndicesIndex] + prevVertexCount;
				triangles[triangleIndicesIndex++] = triangleIndex;
			}

			prevVertexCount += uiGeo->Vertices.Num();
		}
	}
}

void FLexUIDrawCall::CopyUpdateState(FLexUIDrawCall* Target)
{
	if (bMaterialChanged)Target->bMaterialChanged = true;
	if (bTextureChanged)Target->bTextureChanged = true;
	if (bNeedToUpdateVertex)Target->bNeedToUpdateVertex = true;
	if (bVertexPositionChanged)Target->bVertexPositionChanged = true;
}

bool FLexUIDrawCall::CanConsumeUIBatchMeshRenderable(FLexUIGeometry* geo, int32 itemVertCount)
{
	return this->Type == ELexUIDrawCallType::BatchGeometry
		&& this->Material == geo->Material
		&& this->Texture == geo->Texture
		&& this->VerticesCount + itemVertCount < LEXUI_MAX_VERTEX_COUNT;
}
