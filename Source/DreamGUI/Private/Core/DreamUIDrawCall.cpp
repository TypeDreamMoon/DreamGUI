// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/DreamUIDrawCall.h"
#include "Core/DreamUIGeometry.h"
#include "Core/DreamUIRenderLayerTable.h"
#include "Core/Components/DreamVisualBatchMesh.h"

namespace DreamUIDrawCallLocal
{
	/**
	 * InGeometry's box, into InOutCombined when it is in no render layer and into its layer's entry of InOutLayerBounds when
	 * it is in one: its 2D bounds are the layer's then, which only the layer's row puts on the canvas.
	 */
	void AddGeometryBounds(const FDreamUIGeometry& InGeometry, FBox& InOutCombined, TArray<FDreamUIDrawCall::FLayerBounds>& InOutLayerBounds)
	{
		const FVector Min(0.1f, InGeometry.BoundsMin2DInCanvasSpace.X, InGeometry.BoundsMin2DInCanvasSpace.Y);
		const FVector Max(0.1f, InGeometry.BoundsMax2DInCanvasSpace.X, InGeometry.BoundsMax2DInCanvasSpace.Y);
		if (!InGeometry.IsInRenderLayer())
		{
			InOutCombined += Min;
			InOutCombined += Max;
			return;
		}
		if (InOutLayerBounds.Num() == 0 || InOutLayerBounds.Last().Row != InGeometry.RenderLayerRow)
		{
			InOutLayerBounds.AddDefaulted_GetRef().Row = InGeometry.RenderLayerRow;
		}
		InOutLayerBounds.Last().Bounds += Min;
		InOutLayerBounds.Last().Bounds += Max;
	}

	/**
	 * The room a draw call of render layer elements is boxed with beyond what its layers need now: an eighth of its largest
	 * side, and no less than a unit, all round. A layer turning about the middle of what it holds boxes the same every
	 * frame only on paper -- rounding moves the box a hair each frame, and a layer turning about a point off the middle of
	 * its elements carries their box round in a small circle -- and a box given out again each time is the section's and
	 * the mesh's, and a render command, every frame, for every such canvas.
	 */
	double GetLayerBoundsRoom(const FBox& InPlaced)
	{
		return FMath::Max(InPlaced.GetSize().GetMax() * 0.125, 1.0);
	}

	/**
	 * Whether InKept, the box a draw call was given before, still does for InPlaced, where its layers stand now: it holds
	 * it, and is not more than InPlaced with twice the room all round -- so a box that grew for a layer that has since
	 * shrunk or stopped is given back its size.
	 */
	bool StillHolds(const FBox& InKept, const FBox& InPlaced)
	{
		if (!InKept.IsValid || !InPlaced.IsValid || !InKept.IsInsideOrOn(InPlaced))
		{
			return false;
		}
		const FVector Slack = InKept.GetSize() - InPlaced.GetSize();
		return Slack.GetMax() <= 4.0 * GetLayerBoundsRoom(InPlaced);
	}
}

bool FDreamUIDrawCall::CopyBatchMeshGeometry()
{
	/**
	 * This is the cheap refresh path: the draw-call layout is left alone and only the vertices are taken again, from
	 * each visual's copy of its geometry. A visual whose geometry did not change hands over the very copy this draw
	 * call holds, so a draw call none of whose visuals changed is left as it is and has nothing to upload -- it used
	 * to be copied and uploaded again with every other one whenever anything in the canvas changed.
	 *
	 * The destination was sized by ApplyBatchMeshGeometryToCombined from the copies the batch was built from, so two
	 * things are checked before anything is written: that each visual is still alive (a widget can be destroyed between
	 * the batch and this refresh) and that it still has the vertex count its slot was sized for. Either way the answer is
	 * to stop and leave the buffer as it is; a rebuild is what fixes it.
	 */
	const int32 CombinedVertexCount = CombinedBatchMeshGeometryVertices.Num();
	//the multi-geometry branch of ApplyBatchMeshGeometryToCombined leaves out anything with no
	//triangles, so a refresh that walked every geometry wrote each later one at the wrong offset
	const bool bSkipTrianglelessGeometry = BatchMeshGeometryArray.Num() != 1;
	TArray<TSharedPtr<const FDreamUIGeometry>, TInlineAllocator<16>> Latest;
	Latest.Reserve(BatchMeshGeometryArray.Num());
	bool bAnyChanged = false;
	int32 PrevVertCount = 0;
	for (int geoIndex = 0; geoIndex < BatchMeshGeometryArray.Num(); geoIndex++)
	{
		const TSharedPtr<const FDreamUIGeometry>& Built = BatchMeshGeometryArray[geoIndex];
		if (!Built.IsValid() || !BatchMeshVisualArray.IsValidIndex(geoIndex))return false;
		UDreamVisualBatchMesh* BatchMeshVisual = BatchMeshVisualArray[geoIndex].Get();
		if (BatchMeshVisual == nullptr)return false;
		TSharedPtr<const FDreamUIGeometry> Now = BatchMeshVisual->GetGeometryForBatching();
		if (!Now.IsValid())return false;
		// Kept in another space than it was batched in -- a render layer came or went -- which only the rebuild that asked
		// for it can put right.
		if (Now->RenderLayer != Built->RenderLayer || Now->RenderLayerRow != Built->RenderLayerRow)return false;
		//the slot is the size the batch was built with, so that -- not the live count -- is what the layout says; a
		//count that no longer matches means the layout itself is stale
		const int32 VertexCount = Built->Vertices.Num();
		const bool bInBuffer = !(bSkipTrianglelessGeometry && Built->Triangles.Num() <= 0);
		if (!ensureMsgf(Now->Vertices.Num() == VertexCount && (!bInBuffer || bCombinePending || PrevVertCount + VertexCount <= CombinedVertexCount)
			, TEXT("[FDreamUIDrawCall::CopyBatchMeshGeometry] Geometry changed since the draw-call was built (%d vertices now, %d then; %d + %d > %d), skipping the refresh; the draw-call rebuild will pick it up.")
			, Now->Vertices.Num(), VertexCount, PrevVertCount, VertexCount, CombinedVertexCount))
		{
			return false;
		}
		if (bInBuffer)
		{
			PrevVertCount += VertexCount;
		}
		bAnyChanged |= Now != Built;
		Latest.Add(MoveTemp(Now));
	}
	if (!bAnyChanged)
	{
		return false;
	}
	auto SameTriangles = [](const FDreamUIGeometry& InA, const FDreamUIGeometry& InB)
	{
		return InA.Triangles.Num() == InB.Triangles.Num()
			&& (InA.Triangles.Num() == 0 || FMemory::Memcmp(InA.Triangles.GetData(), InB.Triangles.GetData(), InA.Triangles.Num() * sizeof(FDreamUIMeshIndex)) == 0);
	};
	// Left to be made, the buffer is made from the copies taken here whenever something reads it -- while their triangles
	// are the ones built. A copy with other triangles has its vertices written into the buffer as built, as below.
	if (bCombinePending)
	{
		for (int geoIndex = 0; geoIndex < BatchMeshGeometryArray.Num(); geoIndex++)
		{
			if (!SameTriangles(*Latest[geoIndex], *BatchMeshGeometryArray[geoIndex]))
			{
				CombineIfPending();
				break;
			}
		}
	}
	// Only vertices go into the buffer; the indices stay as the batch laid them out. The section can stand for these
	// copies only while their triangles are the ones it holds.
	FDreamUIMeshVertex* CombinedVertexData = bCombinePending ? nullptr : CombinedBatchMeshGeometryVertices.GetData();
	PrevVertCount = 0;
	for (int geoIndex = 0; geoIndex < BatchMeshGeometryArray.Num(); geoIndex++)
	{
		const FDreamUIGeometry& Built = *BatchMeshGeometryArray[geoIndex];
		const FDreamUIGeometry& Now = *Latest[geoIndex];
		if (CombinedVertexData != nullptr && !(bSkipTrianglelessGeometry && Built.Triangles.Num() <= 0))
		{
			// A copy that did not change is in the buffer already, as the batch or an earlier refresh wrote it.
			if (Latest[geoIndex] != BatchMeshGeometryArray[geoIndex])
			{
				FMemory::Memcpy(CombinedVertexData + PrevVertCount, Now.Vertices.GetData(), Now.Vertices.Num() * sizeof(FDreamUIMeshVertex));
			}
			PrevVertCount += Now.Vertices.Num();
		}
		bTrianglesAsBuilt = bTrianglesAsBuilt && SameTriangles(Now, Built);
		BatchMeshGeometryArray[geoIndex] = Latest[geoIndex];
	}
	return true;
}

void FDreamUIDrawCall::CombineIfPending()
{
	if (bCombinePending)
	{
		bCombinePending = false;
		ApplyBatchMeshGeometryToCombined();
	}
}

void FDreamUIDrawCall::ApplyBatchMeshBoundsToCombined()
{
	// What ApplyBatchMeshGeometryToCombined adds to the bounds, and nothing else: every geometry when there is one, else
	// only those with triangles.
	CombinedBounds.Init();
	LayerBounds.Reset();
	for (int geoIndex = 0; geoIndex < BatchMeshGeometryArray.Num(); geoIndex++)
	{
		const FDreamUIGeometry& uiGeo = *BatchMeshGeometryArray[geoIndex];
		if (BatchMeshGeometryArray.Num() != 1 && uiGeo.Triangles.Num() <= 0)continue;
		DreamUIDrawCallLocal::AddGeometryBounds(uiGeo, CombinedBounds, LayerBounds);
	}
	CanvasBounds = CombinedBounds;
}

void FDreamUIDrawCall::PlaceBounds(const UDreamUIRenderLayerTable* InTable)
{
	FBox Placed = CombinedBounds;
	for (const FLayerBounds& Entry : LayerBounds)
	{
		if (!Entry.Bounds.IsValid)
		{
			continue;
		}
		/**
		 * The sphere around the run's box, where the layer's row takes its centre, and as big as the layer's largest scale
		 * makes it: the box of that on the canvas. A layer turning about the middle of what it holds -- a button spinning
		 * about its pivot, as animations turn them -- boxes the same every frame, so the section's box, and the mesh's
		 * with it, are left as they are rather than sent again each frame. A little larger than the turned box itself,
		 * which only ever makes the mesh less likely to be culled. A table that is gone places nothing: the layer's
		 * elements are boxed where they would stand in no layer.
		 */
		const FMatrix LayerToCanvas(InTable != nullptr ? InTable->ReadRow(Entry.Row) : FMatrix44f::Identity);
		const FVector Center = LayerToCanvas.TransformPosition(Entry.Bounds.GetCenter());
		const double Radius = Entry.Bounds.GetExtent().Size() * LayerToCanvas.GetMaximumAxisScale();
		Placed += FBox(Center - FVector(Radius), Center + FVector(Radius));
	}
	if (LayerBounds.Num() == 0)
	{
		CanvasBounds = Placed;
		return;
	}
	// A box given out before is kept while it still holds the layers where they stand: see GetLayerBoundsRoom.
	if (!DreamUIDrawCallLocal::StillHolds(CanvasBounds, Placed))
	{
		CanvasBounds = Placed.ExpandBy(DreamUIDrawCallLocal::GetLayerBoundsRoom(Placed));
	}
}

void FDreamUIDrawCall::ApplyBatchMeshGeometryToCombined()
{
	CombinedBatchMeshGeometryVertices.Reset();
	CombinedBatchMeshGeometryTriangles.Reset();
	CombinedBounds.Init();
	LayerBounds.Reset();
	
	if (BatchMeshGeometryArray.Num() == 1)
	{
		const FDreamUIGeometry& uiGeo = *BatchMeshGeometryArray[0];
		CombinedBatchMeshGeometryVertices.SetNumUninitialized(uiGeo.Vertices.Num());
		FMemory::Memcpy(CombinedBatchMeshGeometryVertices.GetData(), uiGeo.Vertices.GetData(), uiGeo.Vertices.Num() * sizeof(FDreamUIMeshVertex));
		CombinedBatchMeshGeometryTriangles.SetNumUninitialized(uiGeo.Triangles.Num());
		FMemory::Memcpy(CombinedBatchMeshGeometryTriangles.GetData(), uiGeo.Triangles.GetData(), uiGeo.Triangles.Num() * sizeof(FDreamUIMeshIndex));
		DreamUIDrawCallLocal::AddGeometryBounds(uiGeo, CombinedBounds, LayerBounds);
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
			const FDreamUIGeometry& uiGeo = *BatchMeshGeometryArray[geoIndex];
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

			DreamUIDrawCallLocal::AddGeometryBounds(uiGeo, CombinedBounds, LayerBounds);
			
			prevVertexCount += uiGeo.Vertices.Num();
		}
	}
	CanvasBounds = CombinedBounds;
}

bool FDreamUIDrawCall::GeometryListsShareLayout(const TArray<TSharedPtr<const FDreamUIGeometry>>& A, const TArray<TSharedPtr<const FDreamUIGeometry>>& B)
{
	if (A.Num() != B.Num() || A.Num() == 0)
	{
		return false;
	}
	for (int32 Index = 0; Index < A.Num(); ++Index)
	{
		const FDreamUIGeometry* GeometryA = A[Index].Get();
		const FDreamUIGeometry* GeometryB = B[Index].Get();
		if (GeometryA == nullptr || GeometryB == nullptr)
		{
			return false;
		}
		if (GeometryA == GeometryB)
		{
			continue;
		}
		if (GeometryA->Vertices.Num() != GeometryB->Vertices.Num() || GeometryA->Triangles.Num() != GeometryB->Triangles.Num()
			|| (GeometryA->Triangles.Num() > 0
				&& FMemory::Memcmp(GeometryA->Triangles.GetData(), GeometryB->Triangles.GetData(), GeometryA->Triangles.Num() * sizeof(FDreamUIMeshIndex)) != 0))
		{
			return false;
		}
	}
	return true;
}

bool FDreamUIDrawCall::CanConsumeUIGeometryForBatchMesh(const FDreamUIGeometry& geo)const
{
	if (this->Type != EDreamUIDrawCallType::BatchMesh)return false;
	// Whatever render layers its elements are in: each vertex is placed through its own layer's row, on the GPU.
	// Compared as the keys they are, never resolved: this runs on the batching thread, and == and != resolve both weak
	// pointers whenever they differ, reading the object array while a collection may be under way on the game thread. Two
	// pointers set to the same object that has gone are still the same key; one set to nothing is not.
	if (!this->Material.HasSameIndexAndSerialNumber(geo.Material))return false;
	//the blend state is chosen once for the whole draw-call, so elements that composite differently
	//cannot share one however identical everything else is
	if (this->BlendMode != geo.BlendMode)return false;
	if (geo.bIsFont)
	{
		if (!this->FontTexture.IsExplicitlyNull() && !this->FontTexture.HasSameIndexAndSerialNumber(geo.Texture))//draw-call also contains font but different of geo's
			return false;
	}
	else
	{
		if (!this->Texture.IsExplicitlyNull() && !this->Texture.HasSameIndexAndSerialNumber(geo.Texture))//draw-call also contains non-font but difference of geo's
			return false;
	}
	if (this->VerticesCount + geo.Vertices.Num() >= LEXUI_MAX_VERTEX_COUNT)return false;
	return true;
}
