// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/LexCanvasDrawCallProcessingRunnable.h"

#include "Core/Components/LexCanvas.h"

void FLexCanvasDrawCallProcessingRunnable::Start()
{
	check (!bIsRunning);
	check (PreparedDrawCallDataQueue == nullptr);
	PreparedDrawCallDataQueue = MakeShared<TQueue<FLexCanvasPreparedDrawCallData>>();
	PreparedDrawCallDataQueueEvent = FPlatformProcess::GetSynchEventFromPool();
	PendingRebuildDrawCallQueue = MakeShared<TQueue<FLexCanvasPendingDrawCallData>>();
	bIsRunning = true;
	Thread.Reset(FRunnableThread::Create(this, TEXT("FLexCanvasDrawCallProcessingRunnable"), 0, TPri_Normal));
}

uint32 FLexCanvasDrawCallProcessingRunnable::Run()
{
	while (bIsRunning)
	{
		if (PreparedDrawCallDataQueueEvent->Wait())
		{
			if (!bIsRunning)break;
			bIsBatching = true;
			FLexCanvasPreparedDrawCallData PreparedDrawCallData;
			while (PreparedDrawCallDataQueue->Dequeue(PreparedDrawCallData)){}//discard old data and get the newest one
			FLexCanvasPendingDrawCallData PendingDrawCallData;
			PendingDrawCallData.FrameNumber = PreparedDrawCallData.FrameNumber;
			ULexCanvas::BatchDrawCallAsync(PreparedDrawCallData.LeftBottomPoint, PreparedDrawCallData.RightTopPoint, PreparedDrawCallData.DataArray, PendingDrawCallData.DrawCallArray);
			//push to main thread queue
			PendingRebuildDrawCallQueue->Enqueue(MoveTemp(PendingDrawCallData));
			bIsBatching = false;
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
	PreparedDrawCallDataQueueEvent->Trigger();
	FPlatformProcess::ReturnSynchEventToPool(PreparedDrawCallDataQueueEvent);
	Thread->WaitForCompletion();
}
void FLexCanvasDrawCallProcessingRunnable::Exit()
{
	Stop();
}

void FLexCanvasDrawCallProcessingRunnable::PushPreparedDrawCallData(FLexCanvasPreparedDrawCallData InData)
{
	PreparedDrawCallDataQueue->Enqueue(MoveTemp(InData));
	PreparedDrawCallDataQueueEvent->Trigger();
}

bool FLexCanvasDrawCallProcessingRunnable::TryGetDrawCallData(FLexCanvasPendingDrawCallData& OutData)
{
	if (!PendingRebuildDrawCallQueue->IsEmpty())
	{
		while (PendingRebuildDrawCallQueue->Dequeue(OutData))
		{
			return true;
		}
	}
	return false;
}
