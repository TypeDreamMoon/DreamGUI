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
};
struct FDreamCanvasPendingDrawCallData
{
	TArray<FDreamUIDrawCall> DrawCallArray;
	uint64 FrameNumber = 0;//the frame number when pushing this draw-call
};