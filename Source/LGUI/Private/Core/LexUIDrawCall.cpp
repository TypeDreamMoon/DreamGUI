// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/LexUIDrawCall.h"
#include "Core/LexUIGeometry.h"

void FLexUIDrawCall::CopyBatchMeshGeometry()
{
#if 1
	int VertOffset = 0;
	const int VertDataLength = sizeof(FLexUIMeshVertex);
	for (int i = 0; i < BatchMeshVisualArray.Num(); i++)
	{
		int VerticesDataNum = BatchMeshVisualArray[i]->GetGeometry()->Vertices.Num() * VertDataLength;
		FMemory::Memcpy((uint8*)CombinedBatchMeshGeometryVertices.GetData() + VertOffset, (uint8*)BatchMeshVisualArray[i]->GetGeometry()->Vertices.GetData(), VerticesDataNum);
		VertOffset+=VerticesDataNum;
	}
#else
	for (int i = 0; i < BatchMeshVisualArray.Num(); i++)
	{
		BatchMeshGeometryArray[i] = *BatchMeshVisualArray[i]->GetGeometry();
	}
#endif
}

void FLexUIDrawCall::ApplyCombinedBatchMeshGeometry()
{
	CombinedBatchMeshGeometryVertices.Reset();
	CombinedBatchMeshGeometryTriangles.Reset();
	CombinedBounds.Init();
	int count = BatchMeshGeometryArray.Num();
	if (count == 1)
	{
		auto& uiGeo = BatchMeshGeometryArray[0];
		CombinedBatchMeshGeometryVertices = uiGeo.Vertices;
		CombinedBatchMeshGeometryTriangles = uiGeo.Triangles;
		CombinedBounds += FVector(0.1f, uiGeo.BoundsMin2DInCanvasSpace.X, uiGeo.BoundsMin2DInCanvasSpace.Y);
		CombinedBounds += FVector(0.1f, uiGeo.BoundsMax2DInCanvasSpace.X, uiGeo.BoundsMax2DInCanvasSpace.Y);
	}
	else
	{
		int prevVertexCount = 0;
		int triangleIndicesIndex = 0;
		CombinedBatchMeshGeometryVertices.Reserve(this->VerticesCount);
		CombinedBatchMeshGeometryTriangles.SetNumUninitialized(this->IndicesCount);
		for (int geoIndex = 0; geoIndex < count; geoIndex++)
		{
			auto& uiGeo = BatchMeshGeometryArray[geoIndex];
			auto& geomTriangles = uiGeo.Triangles;
			int triangleCount = geomTriangles.Num();
			if (triangleCount <= 0)continue;
			CombinedBounds += FVector(0.1f, uiGeo.BoundsMin2DInCanvasSpace.X, uiGeo.BoundsMin2DInCanvasSpace.Y);
			CombinedBounds += FVector(0.1f, uiGeo.BoundsMax2DInCanvasSpace.X, uiGeo.BoundsMax2DInCanvasSpace.Y);
			CombinedBatchMeshGeometryVertices.Append(uiGeo.Vertices);
			for (int geomTriangleIndicesIndex = 0; geomTriangleIndicesIndex < triangleCount; geomTriangleIndicesIndex++)
			{
				auto triangleIndex = geomTriangles[geomTriangleIndicesIndex] + prevVertexCount;
				CombinedBatchMeshGeometryTriangles[triangleIndicesIndex++] = triangleIndex;
			}

			prevVertexCount += uiGeo.Vertices.Num();
		}
	}
}

bool FLexUIDrawCall::CanConsumeUIGeometryForBatchMesh(const FLexUIGeometry* geo)const
{
	if (this->Type != ELexUIDrawCallType::BatchMesh)return false;
	if (this->Material != geo->Material)return false;
	if (geo->bIsFont)
	{
		if (this->FontTexture != nullptr && this->FontTexture != geo->Texture)//draw-call also contains font but different of geo's
			return false;
	}
	else
	{
		if (this->Texture != nullptr && this->Texture != geo->Texture)//draw-call also contains non-font but difference of geo's
			return false;
	}
	if (this->VerticesCount + geo->Vertices.Num() >= LEXUI_MAX_VERTEX_COUNT)return false;
	return true;
}
