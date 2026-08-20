// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once
#include "DreamUIDrawCall.h"

struct FDreamCanvasPreparedDrawCallData
{
	TArray<FDreamUIRenderData> DataArray;
	FVector2D LeftBottomPoint;
	FVector2D RightTopPoint;
	uint64 FrameNumber = 0;
};
struct FDreamCanvasPendingDrawCallData
{
	TArray<FDreamUIDrawCall> DrawCallArray;
	uint64 FrameNumber = 0;//the frame number when pushing this draw-call
};