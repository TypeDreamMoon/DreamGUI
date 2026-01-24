// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "LexUIGeometry.h"
#include "Engine/Texture.h"
#include "Core/LexUIMeshIndex.h"
#include "Core/LexUIQuadTree.h"

class ULexVisualPostProcess;
struct FLexUIMeshVertex;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class ULexWidget;
class ULexVisualBatchMesh;
class ULexVisualDirectMesh;
class ULexUIMeshComponent;
struct FLexUIRenderSection;

enum class ELexUIDrawCallType :uint8
{
	BatchMesh = 1,
	PostProcess,
	DirectMesh,
	ChildCanvas,
};

class FLexUIRenderData
{
public:
	virtual ~FLexUIRenderData() {}
	ELexUIDrawCallType Type = ELexUIDrawCallType::BatchMesh;
};
class FLexUIRenderData_BatchMesh: public FLexUIRenderData
{
public:
	FLexUIRenderData_BatchMesh(){Type = ELexUIDrawCallType::BatchMesh;}
	FLexUIGeometry BatchMeshGeometry;
	TWeakObjectPtr<ULexVisualBatchMesh> BatchMeshVisualObject;
};
class FLexUIRenderData_PostProcess: public FLexUIRenderData
{
public:
	FLexUIRenderData_PostProcess(){Type = ELexUIDrawCallType::PostProcess;}
	TWeakObjectPtr<ULexVisualPostProcess> PostProcessVisualObject;//post process object
};
class FLexUIRenderData_DirectMesh: public FLexUIRenderData
{
public:
	FLexUIRenderData_DirectMesh(){Type = ELexUIDrawCallType::DirectMesh;}
	TWeakObjectPtr<ULexVisualDirectMesh> DirectMeshVisualObject;
};
class FLexUIRenderData_ChildCanvas: public FLexUIRenderData
{
public:
	FLexUIRenderData_ChildCanvas(){Type = ELexUIDrawCallType::ChildCanvas;}
	TWeakObjectPtr<class ULexCanvas> ChildCanvas;
};

class LGUI_API FLexUIDrawCall
{
public:
	FLexUIDrawCall(ELexUIDrawCallType InType)
	{
		Type = InType;
	}
	FLexUIDrawCall(LexUIQuadTree::Rectangle InCanvasRect)
	{
		Type = ELexUIDrawCallType::BatchMesh;
		BatchMeshTreeNode = MakeUnique<LexUIQuadTree::Node>(InCanvasRect);
	}
	~FLexUIDrawCall()
	{
		
	}
	ELexUIDrawCallType Type = ELexUIDrawCallType::BatchMesh;

	TWeakObjectPtr<UTexture> Texture = nullptr;//draw-call use this texture to render
	TWeakObjectPtr<UTexture> FontTexture = nullptr;//draw-call use this texture to render font
	TWeakObjectPtr<UMaterialInterface> Material = nullptr;//draw-call use this material to render, can be null to use default material
	TWeakObjectPtr<UMaterialInterface> RenderMaterial = nullptr;//actual material that render this draw-call
	TWeakObjectPtr<ULexUIMeshComponent> DrawCallMesh = nullptr;//mesh for render this draw-call

	TWeakObjectPtr<ULexVisualPostProcess> PostProcessVisualObject;//post process object

	TWeakObjectPtr<ULexVisualDirectMesh> DirectMeshVisualObject;

	TArray<TWeakObjectPtr<ULexVisualBatchMesh>> BatchMeshVisualArray;
	TArray<FLexUIGeometry> BatchMeshGeometryArray;//BatchMesh's geometry collections belong to this draw-call, must be sorted on hierarchy-index
	TArray<FLexUIMeshVertex> CombinedBatchMeshGeometryVertices;
	TArray<FLexUIMeshIndexBufferType> CombinedBatchMeshGeometryTriangles;
	FBox CombinedBounds;
	bool bNeedToSortBatchMeshVisualObjectList = false;//need to sort BatchMeshRenderObjectList?
	TUniquePtr<LexUIQuadTree::Node> BatchMeshTreeNode = nullptr;
	int32 VerticesCount = 0;//vertices count of all BatchMeshRenderObjectList
	int32 IndicesCount = 0;//triangle indices count of all BatchMeshRenderObjectList

	bool bIs2DSpace = false;//transform relative to canvas is 2d or not? only 2d draw-call can batch

	TWeakObjectPtr<class ULexCanvas> ChildCanvas;//insert point to sort child canvas
public:
	void CopyBatchMeshGeometry();
	void ApplyCombinedBatchMeshGeometry();
	bool CanConsumeUIGeometryForBatchMesh(const FLexUIGeometry* geo)const;
};
