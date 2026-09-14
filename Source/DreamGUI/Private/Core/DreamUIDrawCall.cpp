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
	//the multi-geometry branch of ApplyBatchMeshGeometryToCombined leaves out anything with no
	//triangles, so a refresh that walked every geometry wrote each later one at the wrong offset
	const bool bSkipTrianglelessGeometry = BatchMeshGeometryArray.Num() != 1;
	int PrevVertCount = 0;
	for (int geoIndex = 0; geoIndex < BatchMeshGeometryArray.Num(); geoIndex++)
	{
		const auto& BuiltGeo = BatchMeshGeometryArray[geoIndex];
		if (bSkipTrianglelessGeometry && BuiltGeo.Triangles.Num() <= 0)continue;
		if (!BatchMeshVisualArray.IsValidIndex(geoIndex))return;
		auto BatchMeshVisual = BatchMeshVisualArray[geoIndex].Get();
		if (BatchMeshVisual == nullptr)return;
		auto uiGeo = BatchMeshVisual->GetGeometry();
		if (uiGeo == nullptr)return;
		//the destination slot is the size the batch was built with, so that -- not the live count --
		//is what the layout says; a count that no longer matches means the layout itself is stale
		const int32 VertexCount = BuiltGeo.Vertices.Num();
		if (!ensureMsgf(uiGeo->Vertices.Num() == VertexCount && PrevVertCount + VertexCount <= CombinedVertexCount
			, TEXT("[FDreamUIDrawCall::CopyBatchMeshGeometry] Geometry changed since the draw-call was built (%d vertices now, %d then; %d + %d > %d), skipping the refresh; the draw-call rebuild will pick it up.")
			, uiGeo->Vertices.Num(), VertexCount, PrevVertCount, VertexCount, CombinedVertexCount))
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
				//in int32 first, then narrowed once, on purpose: the operands promote differently in the
				//16-bit build (int) and the 32-bit one (uint32), and the guards that keep the result in
				//range are all written in int32 (LEXUI_MAX_VERTEX_COUNT, TArray::Num)
				const int32 triangleIndex = (int32)TriangleData[geomTriangleIndicesIndex] + prevVertexCount;
				checkSlow(triangleIndex >= 0 && triangleIndex < LEXUI_MAX_VERTEX_COUNT);
				CombinedTriangleData[triangleIndicesIndex++] = (FDreamUIMeshIndex)triangleIndex;
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
	//the blend state is chosen once for the whole draw-call, so elements that composite differently
	//cannot share one however identical everything else is
	if (this->BlendMode != geo.BlendMode)return false;
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
