// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "Engine/Texture.h"
#include "Core/LexUIMeshIndex.h"
#include "Core/LexUIQuadTree.h"

class ULexVisualPostProcess;
class FLexUIGeometry;
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
	TWeakPtr<FLexUIRenderSection> DrawCallRenderSection = nullptr;//section of mesh which render this draw-call

	bool bMaterialContainsLexUIParameter = false;//if Material contains LGUI's parameter, then a MaterialInstanceDynamic will be created and stored as RenderMaterial, otherwise RenderMaterial is same as Material
	bool bMaterialChanged = true;
	bool bMaterialNeedToReassign = true;//once a mesh section is recreated, and the material is still valid, then we need to re-assign the material to newly created mesh section
	bool bTextureChanged = true;

	bool bNeedToUpdateVertex = true;
	bool bVertexPositionChanged = true;//if vertex position changed? use for update bounds

	TWeakObjectPtr<ULexVisualPostProcess> PostProcessVisualObject;//post process object

	TWeakObjectPtr<ULexVisualDirectMesh> DirectMeshVisualObject;

	TArray<TWeakObjectPtr<ULexVisualBatchMesh>> BatchMeshVisualObjectList;//BatchMesh object collections belong to this draw-call, must be sorted on hierarchy-index
	bool bNeedToSortBatchMeshVisualObjectList = false;//need to sort BatchMeshRenderObjectList?
	TUniquePtr<LexUIQuadTree::Node> BatchMeshTreeNode = nullptr;
	int32 VerticesCount = 0;//vertices count of all BatchMeshRenderObjectList
	int32 IndicesCount = 0;//triangle indices count of all BatchMeshRenderObjectList

	bool bIs2DSpace = false;//transform relative to canvas is 2d or not? only 2d draw-call can batch

	TWeakObjectPtr<class ULexCanvas> ChildCanvas;//insert point to sort child canvas
public:
	void GetCombined(TArray<FLexUIMeshVertex>& vertices, TArray<FLexUIMeshIndexBufferType>& triangles)const;
	void CopyUpdateState(FLexUIDrawCall* Target);
	bool CanConsumeUIGeometryForBatchMesh(FLexUIGeometry* geo, int32 itemVertCount);
};
