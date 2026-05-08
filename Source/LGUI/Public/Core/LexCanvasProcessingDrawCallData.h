// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once
#include "LexUIDrawCall.h"

struct FLexCanvasPreparedDrawCallData
{
	TArray<FLexUIRenderData> DataArray;
	FVector2D LeftBottomPoint;
	FVector2D RightTopPoint;
	uint64 FrameNumber = 0;
};
struct FLexCanvasPendingDrawCallData
{
	TArray<FLexUIDrawCall> DrawCallArray;
	uint64 FrameNumber = 0;//the frame number when pushing this draw-call
};