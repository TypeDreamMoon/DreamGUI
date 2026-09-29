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
	 * of them takes that section back as it is (UDreamUIMeshComponent::ClaimPooledMeshSections), so the batching leaves
	 * its vertices uncombined; FDreamUIDrawCall::CombineIfPending combines them on the game thread should the section not
	 * be there after all.
	 */
	TArray<TArray<TSharedPtr<const FDreamUIGeometry>>> GeometryListsOnSections;
};
struct FDreamCanvasPendingDrawCallData
{
	TArray<FDreamUIDrawCall> DrawCallArray;
	uint64 FrameNumber = 0;//the frame number when pushing this draw-call
};