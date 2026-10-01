// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once
#include "DreamUIDrawCall.h"

struct FDreamCanvasPreparedDrawCallData
{
	TArray<FDreamUIRenderData> DataArray;
	FVector2D LeftBottomPoint;
	FVector2D RightTopPoint;
	uint64 FrameNumber = 0;
	/** Decided on the game thread; see UDreamCanvas::BatchDrawCallAsync for when it may be true. */
	bool bCullElementsOutsideCanvasRect = false;
	/**
	 * The geometry lists the canvas's mesh sections were built from when this was prepared. A draw call built from one
	 * of them, or from geometries laid out as one of them is, takes that section back, as it is or with the vertices that
	 * differ written in place (UDreamUIMeshComponent::ClaimPooledMeshSections), so the batching leaves its vertices
	 * uncombined; FDreamUIDrawCall::CombineIfPending combines them on the game thread should the section not be there
	 * after all.
	 */
	TArray<TArray<TSharedPtr<const FDreamUIGeometry>>> GeometryListsOnSections;
};
/**
 * What a batch found about how its draw calls depend on where the elements are, for a canvas whose elements then only
 * move (UDreamCanvas::CanRefreshDrawCallsInPlace): if nothing it says can change, the draw calls a new batch would make
 * are the ones in hand, and only their vertices and bounds need to follow.
 */
struct FDreamUIBatchPlacement
{
	/**
	 * No element could have gone into a draw call before the last one made ahead of it. The batching looks back past the
	 * last draw call only as far as nothing in between overlaps the element, so where an earlier one could also take it,
	 * which one did depended on positions -- and a move could change it.
	 */
	bool bIndependentOfPositions = true;
	/** The flat elements left out because they lay wholly outside the canvas rect. */
	TArray<TWeakObjectPtr<UDreamVisualBatchMesh>> CulledVisuals;
};
struct FDreamCanvasPendingDrawCallData
{
	TArray<FDreamUIDrawCall> DrawCallArray;
	uint64 FrameNumber = 0;//the frame number when pushing this draw-call
	/** The canvas rect the batch culled against, and whether it did: the rect a moved element is measured against again. */
	FVector2D LeftBottomPoint = FVector2D::ZeroVector;
	FVector2D RightTopPoint = FVector2D::ZeroVector;
	bool bCullElementsOutsideCanvasRect = false;
	FDreamUIBatchPlacement Placement;
};