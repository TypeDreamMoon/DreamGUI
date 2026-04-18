// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/LexCanvasDrawCallProcessingRunnable.h"

#include "Core/Components/LexCanvas.h"

void FLexCanvasDrawCallProcessingRunnable::Start(const TSharedPtr<TQueue<FLexCanvasPreparedDrawCallData>>& InDrawCallDataQueue, const TSharedPtr<TQueue<FLexCanvasPendingDrawCallData>>& InRebuildDrawCallQueue)
{
	check (!bIsRunning);
	check (PreparedDrawCallDataQueue == nullptr);
	PreparedDrawCallDataQueue = InDrawCallDataQueue;
	RebuildDrawCallQueue = InRebuildDrawCallQueue;
	bIsRunning = true;
	Thread.Reset(FRunnableThread::Create(this, TEXT("FLexCanvasDrawCallProcessingRunnable"), 0, TPri_Normal));
}

uint32 FLexCanvasDrawCallProcessingRunnable::Run()
{
	while (bIsRunning)
	{
		if (!PreparedDrawCallDataQueue->IsEmpty())
		{
			bIsBatching = true;
			FLexCanvasPreparedDrawCallData PreparedDrawCallData;
			while (!PreparedDrawCallDataQueue->IsEmpty())//discard old data and get the newest one
			{
				PreparedDrawCallDataQueue->Dequeue(PreparedDrawCallData);
			}
			FLexCanvasPendingDrawCallData PendingDrawCallData;
			PendingDrawCallData.FrameNumber = PreparedDrawCallData.FrameNumber;
			ULexCanvas::BatchDrawCallAsync(PreparedDrawCallData.LeftBottomPoint, PreparedDrawCallData.RightTopPoint, PreparedDrawCallData.DataArray, PendingDrawCallData.DrawCallArray);
			//push to main thread queue
			RebuildDrawCallQueue->Enqueue(MoveTemp(PendingDrawCallData));
			bIsBatching = false;
		}
		else
		{
			FPlatformProcess::Sleep(0.001f);
		}
	}

	return 0;
}
void FLexCanvasDrawCallProcessingRunnable::Stop()
{
	if (!bIsRunning)
	{
		return;
	}

	bIsRunning = false;

	Thread->WaitForCompletion();
}
void FLexCanvasDrawCallProcessingRunnable::Exit()
{
	if (!bIsRunning)
	{
		return;
	}

	bIsRunning = false;

	Thread->WaitForCompletion();
}
