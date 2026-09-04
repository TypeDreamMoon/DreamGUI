// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/DreamUIDrawCall.h"
#include "Core/DreamUIGeometry.h"

void FDreamUIDrawCall::CopyBatchMeshGeometry()
{
	/**
	 * This is the cheap refresh path: the draw-call layout is left alone and only the vertices are
	 * re-read from the visuals. The destination was sized by ApplyBatchMeshGeometryToCombined from the
	 * geometry as it stood when the batch was built, so two things have to be checked before each copy
	 * rather than assumed -- that the visual is still alive (a widget can be destroyed between the batch
	 * being built and this refresh) and that it still has the vertex count the buffer was sized for.
	 * Either way the answer is to stop and leave the buffer as it is; a rebuild is what fixes it.
	 */
	const int32 CombinedVertexCount = CombinedBatchMeshGeometryVertices.Num();
	auto CombinedVertexData = CombinedBatchMeshGeometryVertices.GetData();
	int PrevVertCount = 0;
	for (int geoIndex = 0; geoIndex < BatchMeshGeometryArray.Num(); geoIndex++)
	{
		if (!BatchMeshVisualArray.IsValidIndex(geoIndex))return;
		auto BatchMeshVisual = BatchMeshVisualArray[geoIndex].Get();
		if (BatchMeshVisual == nullptr)return;
		auto uiGeo = BatchMeshVisual->GetGeometry();
		if (uiGeo == nullptr)return;
		const int32 VertexCount = uiGeo->Vertices.Num();
		if (!ensureMsgf(PrevVertCount + VertexCount <= CombinedVertexCount
			, TEXT("[FDreamUIDrawCall::CopyBatchMeshGeometry] Geometry grew since the draw-call was built (%d + %d > %d), skipping the refresh; the draw-call rebuild will pick it up.")
			, PrevVertCount, VertexCount, CombinedVertexCount))
		{
			return;
		}
		FMemory::Memcpy(CombinedVertexData + PrevVertCount, uiGeo->Vertices.GetData(), VertexCount * sizeof(FDreamUIMeshVertex));
		PrevVertCount += VertexCount;
	}
}

void FDreamUIDrawCall::ApplyBatchMeshGeometryToCombined()
{
	CombinedBatchMeshGeometryVertices.Reset();
	CombinedBatchMeshGeometryTriangles.Reset();
	CombinedBounds.Init();
	
	if (BatchMeshGeometryArray.Num() == 1)
	{
		auto& uiGeo = BatchMeshGeometryArray[0];
		CombinedBatchMeshGeometryVertices.SetNumUninitialized(uiGeo.Vertices.Num());
		FMemory::Memcpy(CombinedBatchMeshGeometryVertices.GetData(), uiGeo.Vertices.GetData(), uiGeo.Vertices.Num() * sizeof(FDreamUIMeshVertex));
		CombinedBatchMeshGeometryTriangles.SetNumUninitialized(uiGeo.Triangles.Num());
		FMemory::Memcpy(CombinedBatchMeshGeometryTriangles.GetData(), uiGeo.Triangles.GetData(), uiGeo.Triangles.Num() * sizeof(FDreamUIMeshIndex));
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
			FMemory::Memcpy(CombinedBatchMeshGeometryVertices.GetData() + prevVertexCount, uiGeo.Vertices.GetData(), uiGeo.Vertices.Num() * sizeof(FDreamUIMeshVertex));

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

bool FDreamUIDrawCall::CanConsumeUIGeometryForBatchMesh(const FDreamUIGeometry& geo)const
{
	if (this->Type != EDreamUIDrawCallType::BatchMesh)return false;
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
