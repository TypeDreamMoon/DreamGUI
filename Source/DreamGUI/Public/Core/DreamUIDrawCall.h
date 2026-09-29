// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once
#include "CoreMinimal.h"
#include "DreamUIGeometry.h"
#include "Engine/Texture.h"
#include "DreamUIRender/DreamUIMeshIndex.h"
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

	/**
	 * The batch mesh's geometry as the batching is to see it: the visual's copy of its geometry, made again only when
	 * the geometry changed since the last copy, and never written once made (UDreamVisualBatchMesh::
	 * GetGeometryForBatching). The batching reads it on a worker thread and the draw call keeps it, so a canvas where
	 * one widget moved copies that widget's geometry and hands every other one over as it was.
	 */
	TSharedPtr<const FDreamUIGeometry> BatchMeshGeometry;
	TWeakObjectPtr<UDreamVisualBatchMesh> BatchMeshVisualObject;

	TWeakObjectPtr<UDreamVisualPostProcess> PostProcessVisualObject;//post process object
	/**
	 * The post-process element's canvas-space bounds, read off its geometry on the game thread while this
	 * render-data is prepared. Batching runs on a worker thread and must not dereference the weak pointer
	 * above, so the only thing it is allowed to know about a post-process draw-call is these two numbers.
	 */
	FVector2D PostProcessBoundsMin2DInCanvasSpace = FVector2D::ZeroVector;
	FVector2D PostProcessBoundsMax2DInCanvasSpace = FVector2D::ZeroVector;

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

	TWeakObjectPtr<UDreamVisualPostProcess> PostProcessVisualObject;//post process object
	/** Canvas-space bounds carried over from FDreamUIRenderData so the batching worker thread never has to touch the visual. */
	FVector2D PostProcessBoundsMin2DInCanvasSpace = FVector2D::ZeroVector;
	FVector2D PostProcessBoundsMax2DInCanvasSpace = FVector2D::ZeroVector;

	TWeakObjectPtr<UDreamVisualDirectMesh> DirectMeshVisualObject;

	/**
	 * The render section this draw-call produced in UpdateDrawCallMesh, or null when it was skipped
	 * (invalid object, WorldSpace post process). Draw-call index and section index diverge whenever
	 * anything is skipped, so priority/geometry updates must address the section through this handle,
	 * never by the draw-call's position.
	 */
	TSharedPtr<struct FDreamUIRenderSection> RenderSection;
	/**
	 * A pooled mesh section built from exactly this draw call's geometries, claimed for it before any section is set
	 * up (UDreamUIMeshComponent::ClaimPooledMeshSections), so that setting up an earlier draw call cannot take it from
	 * the pool first. Set up, it becomes RenderSection.
	 */
	TSharedPtr<struct FDreamUIRenderSection> ClaimedMeshSection;
	/**
	 * ClaimedMeshSection was built from geometries laid out as this draw call's are (GeometryListsShareLayout), not all
	 * of them the same copies: the vertices of those that differ are written into it in place, and the rest of it stays
	 * as it is, here and on the GPU (UDreamUIMeshComponent::PatchMeshSection).
	 */
	bool bPatchClaimedMeshSection = false;

	TArray<TWeakObjectPtr<UDreamVisualBatchMesh>> BatchMeshVisualArray;
	/**
	 * The geometries this draw call was built from, in hierarchy order: the visuals' copies, shared and never written.
	 * Two draw calls built from the same list hold the same vertices, which is how a mesh section built from one is
	 * known to need nothing uploaded for the other (UDreamUIMeshComponent::SetupRenderSection).
	 */
	TArray<TSharedPtr<const FDreamUIGeometry>> BatchMeshGeometryArray;
	TArray<FDreamUIMeshVertex> CombinedBatchMeshGeometryVertices;
	TArray<FDreamUIMeshIndex> CombinedBatchMeshGeometryTriangles;
	FBox CombinedBounds;
	TSharedPtr<DreamUIQuadTree::Node> BatchMeshTreeNode = nullptr;
	int32 VerticesCount = 0;//vertices count of all BatchMeshRenderObjectList
	int32 IndicesCount = 0;//triangle indices count of all BatchMeshRenderObjectList

	bool bIs2DSpace = false;//transform relative to canvas is 2d or not? only 2d draw-call can batch
	/** The blend mode every element in this draw-call shares; the blend state is set once per draw-call. */
	EDreamUIBlendMode BlendMode = EDreamUIBlendMode::Alpha;

	TWeakObjectPtr<class UDreamCanvas> ChildCanvas;//insert point to sort child canvas
public:
	/**
	 * The cheap refresh, for a draw call whose layout still holds: each visual's copy of its geometry taken again
	 * (UDreamVisualBatchMesh::GetGeometryForBatching) and its vertices written into the combined buffer where the batch
	 * put them -- or, when the batching left that buffer to be made (bCombinePending), only taken: the buffer is made
	 * from them should anything read it. False, and nothing taken, when no visual changed -- the draw call then has
	 * nothing to upload -- or when the layout no longer holds, which the coming rebuild fixes.
	 */
	bool CopyBatchMeshGeometry();
	/** Whether every copy this draw call now holds has the triangles of the copy it was built from: see CopyBatchMeshGeometry. */
	bool bTrianglesAsBuilt = true;
	/**
	 * Set by the batching when it left the combined buffers empty, because the canvas has a section built from these
	 * very geometries, or from geometries laid out as they are, and will take it back as it is or with the vertices that
	 * differ written in place. Everything that reads the buffers asks CombineIfPending first.
	 */
	bool bCombinePending = false;
	/** The combined buffers, made now if the batching left them. */
	void CombineIfPending();
	/** The bounds ApplyBatchMeshGeometryToCombined works out, alone: for a draw call whose buffers are left. */
	void ApplyBatchMeshBoundsToCombined();
	void ApplyBatchMeshGeometryToCombined();
	bool CanConsumeUIGeometryForBatchMesh(const FDreamUIGeometry& geo)const;
	/**
	 * Whether a mesh built from A holds B's triangles and a place for each of B's vertices: as many geometries, each with
	 * the vertex count and the triangles of the one in its place. Only vertices can differ, and B's can be written over
	 * A's where they are.
	 */
	static bool GeometryListsShareLayout(const TArray<TSharedPtr<const FDreamUIGeometry>>& A, const TArray<TSharedPtr<const FDreamUIGeometry>>& B);
};
