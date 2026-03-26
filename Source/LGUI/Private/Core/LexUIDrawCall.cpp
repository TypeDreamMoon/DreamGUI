// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/LexUIDrawCall.h"
#include "Core/LexUIGeometry.h"

void FLexUIDrawCall::CopyBatchMeshGeometry()
{
	int PrevVertCount = 0;
	for (int geoIndex = 0; geoIndex < BatchMeshGeometryArray.Num(); geoIndex++)
	{
		auto uiGeo = BatchMeshVisualArray[geoIndex]->GetGeometry();
		FMemory::Memcpy(CombinedBatchMeshGeometryVertices.GetData() + PrevVertCount, uiGeo->Vertices.GetData(), uiGeo->Vertices.Num() * sizeof(FLexUIMeshVertex));
		PrevVertCount += uiGeo->Vertices.Num();
	}
}

void FLexUIDrawCall::ApplyBatchMeshGeometryToCombined()
{
	CombinedBatchMeshGeometryVertices.Reset();
	CombinedBatchMeshGeometryTriangles.Reset();
	CombinedBounds.Init();
	
	if (BatchMeshGeometryArray.Num() == 1)
	{
		auto& uiGeo = BatchMeshGeometryArray[0];
		CombinedBatchMeshGeometryVertices.SetNumUninitialized(uiGeo.Vertices.Num());
		FMemory::Memcpy(CombinedBatchMeshGeometryVertices.GetData(), uiGeo.Vertices.GetData(), uiGeo.Vertices.Num() * sizeof(FLexUIMeshVertex));
		CombinedBatchMeshGeometryTriangles.SetNumUninitialized(uiGeo.Triangles.Num());
		FMemory::Memcpy(CombinedBatchMeshGeometryTriangles.GetData(), uiGeo.Triangles.GetData(), uiGeo.Triangles.Num() * sizeof(FLexUIMeshIndex));
		CombinedBounds += FVector(0.1f, uiGeo.BoundsMin2DInCanvasSpace.X, uiGeo.BoundsMin2DInCanvasSpace.Y);
		CombinedBounds += FVector(0.1f, uiGeo.BoundsMax2DInCanvasSpace.X, uiGeo.BoundsMax2DInCanvasSpace.Y);
	}
	else
	{
		int prevVertexCount = 0;
		int triangleIndicesIndex = 0;
		CombinedBatchMeshGeometryVertices.Reserve(this->VerticesCount);
		CombinedBatchMeshGeometryTriangles.SetNumUninitialized(this->IndicesCount);
		auto CombinedTriangleData = CombinedBatchMeshGeometryTriangles.GetData();
		for (int geoIndex = 0; geoIndex < BatchMeshGeometryArray.Num(); geoIndex++)
		{
			auto& uiGeo = BatchMeshGeometryArray[geoIndex];
			int triangleCount = uiGeo.Triangles.Num();
			if (triangleCount <= 0)continue;
			
			CombinedBatchMeshGeometryVertices.AddUninitialized(uiGeo.Vertices.Num());
			FMemory::Memcpy(CombinedBatchMeshGeometryVertices.GetData() + prevVertexCount, uiGeo.Vertices.GetData(), uiGeo.Vertices.Num() * sizeof(FLexUIMeshVertex));

			auto TriangleData = uiGeo.Triangles.GetData();
			for (int geomTriangleIndicesIndex = 0; geomTriangleIndicesIndex < triangleCount; geomTriangleIndicesIndex++)
			{
				auto triangleIndex = TriangleData[geomTriangleIndicesIndex] + prevVertexCount;
				CombinedTriangleData[triangleIndicesIndex++] = triangleIndex;
			}

			CombinedBounds += FVector(0.1f, uiGeo.BoundsMin2DInCanvasSpace.X, uiGeo.BoundsMin2DInCanvasSpace.Y);
			CombinedBounds += FVector(0.1f, uiGeo.BoundsMax2DInCanvasSpace.X, uiGeo.BoundsMax2DInCanvasSpace.Y);
			
			prevVertexCount += uiGeo.Vertices.Num();
		}
	}
}

bool FLexUIDrawCall::CanConsumeUIGeometryForBatchMesh(const FLexUIGeometry& geo)const
{
	if (this->Type != ELexUIDrawCallType::BatchMesh)return false;
	if (this->Material != geo.Material)return false;
	if (geo.bIsFont)
	{
		if (this->FontTexture != nullptr && this->FontTexture != geo.Texture)//draw-call also contains font but different of geo's
			return false;
	}
	else
	{
		if (this->Texture != nullptr && this->Texture != geo.Texture)//draw-call also contains non-font but difference of geo's
			return false;
	}
	if (this->VerticesCount + geo.Vertices.Num() >= LEXUI_MAX_VERTEX_COUNT)return false;
	return true;
}
