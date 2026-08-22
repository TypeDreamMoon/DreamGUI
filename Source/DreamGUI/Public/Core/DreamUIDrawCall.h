// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once
#include "CoreMinimal.h"
#include "DreamUIGeometry.h"
#include "Engine/Texture.h"
#include "Core/DreamUIMeshIndex.h"
#include "Core/DreamUIQuadTree.h"

class UDreamVisualPostProcess;
class UDreamUIFontData_BaseObject;
struct FDreamUIMeshVertex;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UDreamWidget;
class UDreamVisualBatchMesh;
class UDreamVisualDirectMesh;
class UDreamUIMeshComponent;
struct FDreamUIRenderSection;

enum class EDreamUIDrawCallType :uint8
{
	BatchMesh = 1,
	PostProcess,
	DirectMesh,
	ChildCanvas,
};

class FDreamUIRenderData
{
public:
	FDreamUIRenderData(EDreamUIDrawCallType InType)
	{
		Type = InType;
	}
	EDreamUIDrawCallType Type = EDreamUIDrawCallType::BatchMesh;

	FDreamUIGeometry BatchMeshGeometry;
	TWeakObjectPtr<UDreamVisualBatchMesh> BatchMeshVisualObject;

	TWeakObjectPtr<UDreamVisualPostProcess> PostProcessVisualObject;//post process object

	TWeakObjectPtr<UDreamVisualDirectMesh> DirectMeshVisualObject;

	TWeakObjectPtr<UDreamCanvas> ChildCanvas;
};

class DREAMGUI_API FDreamUIDrawCall
{
public:
	FDreamUIDrawCall(EDreamUIDrawCallType InType)
	{
		Type = InType;
	}
	FDreamUIDrawCall(DreamUIQuadTree::Rectangle InCanvasRect)
	{
		Type = EDreamUIDrawCallType::BatchMesh;
		BatchMeshTreeNode = MakeShared<DreamUIQuadTree::Node>(InCanvasRect);
	}
	~FDreamUIDrawCall()
	{
		
	}
	EDreamUIDrawCallType Type = EDreamUIDrawCallType::BatchMesh;

	TWeakObjectPtr<UTexture> Texture = nullptr;//draw-call use this texture to render
	TWeakObjectPtr<UTexture> FontTexture = nullptr;//draw-call use this texture to render font
	TWeakObjectPtr<UDreamUIFontData_BaseObject> Font = nullptr;//the font FontTexture belongs to
	TWeakObjectPtr<UMaterialInterface> Material = nullptr;//draw-call use this material to render, can be null to use default material
	TWeakObjectPtr<UMaterialInterface> RenderMaterial = nullptr;//actual material that render this draw-call

	TWeakObjectPtr<UDreamVisualPostProcess> PostProcessVisualObject;//post process object

	TWeakObjectPtr<UDreamVisualDirectMesh> DirectMeshVisualObject;

	/**
	 * The render section this draw-call produced in UpdateDrawCallMesh, or null when it was skipped
	 * (invalid object, WorldSpace post process). Draw-call index and section index diverge whenever
	 * anything is skipped, so priority/geometry updates must address the section through this handle,
	 * never by the draw-call's position.
	 */
	TSharedPtr<struct FDreamUIRenderSection> RenderSection;

	TArray<TWeakObjectPtr<UDreamVisualBatchMesh>> BatchMeshVisualArray;
	TArray<FDreamUIGeometry> BatchMeshGeometryArray;//BatchMesh's geometry collections belong to this draw-call, must be sorted on hierarchy-index
	TArray<FDreamUIMeshVertex> CombinedBatchMeshGeometryVertices;
	TArray<FDreamUIMeshIndex> CombinedBatchMeshGeometryTriangles;
	FBox CombinedBounds;
	bool bNeedToSortBatchMeshVisualObjectList = false;//need to sort BatchMeshRenderObjectList?
	TSharedPtr<DreamUIQuadTree::Node> BatchMeshTreeNode = nullptr;
	int32 VerticesCount = 0;//vertices count of all BatchMeshRenderObjectList
	int32 IndicesCount = 0;//triangle indices count of all BatchMeshRenderObjectList

	bool bIs2DSpace = false;//transform relative to canvas is 2d or not? only 2d draw-call can batch

	TWeakObjectPtr<class UDreamCanvas> ChildCanvas;//insert point to sort child canvas
public:
	void CopyBatchMeshGeometry();
	void ApplyBatchMeshGeometryToCombined();
	bool CanConsumeUIGeometryForBatchMesh(const FDreamUIGeometry& geo)const;
};
