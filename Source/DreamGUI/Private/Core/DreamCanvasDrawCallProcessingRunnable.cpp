// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/DreamCanvasDrawCallProcessingRunnable.h"

#include "Core/Components/DreamCanvas.h"

void FDreamCanvasDrawCallProcessingRunnable::Start()
{
	check (!bIsRunning);
	check (PreparedDrawCallDataQueue == nullptr);
	check (PreparedDrawCallDataQueueEvent == nullptr);
	PreparedDrawCallDataQueue = MakeShared<TQueue<FDreamCanvasPreparedDrawCallData>>();
	PreparedDrawCallDataQueueEvent = FPlatformProcess::GetSynchEventFromPool();
	PendingRebuildDrawCallQueue = MakeShared<TQueue<FDreamCanvasPendingDrawCallData>>();
	bIsRunning = true;
	bIsBatching = false;
	Thread.Reset(FRunnableThread::Create(this, TEXT("FDreamCanvasDrawCallProcessingRunnable"), 0, TPri_Normal));
}

uint32 FDreamCanvasDrawCallProcessingRunnable::Run()
{
	while (bIsRunning)
	{
		auto QueueEvent = PreparedDrawCallDataQueueEvent;
		if (QueueEvent == nullptr || !PreparedDrawCallDataQueue.IsValid() || !PendingRebuildDrawCallQueue.IsValid())
		{
			break;
		}

		if (QueueEvent->Wait(500))
		{
			if (!bIsRunning)break;
			bIsBatching = true;
			FDreamCanvasPreparedDrawCallData PreparedDrawCallData;
			while (PreparedDrawCallDataQueue->Dequeue(PreparedDrawCallData)){}//discard old data and get the newest one
			FDreamCanvasPendingDrawCallData PendingDrawCallData;
			PendingDrawCallData.FrameNumber = PreparedDrawCallData.FrameNumber;
			UDreamCanvas::BatchDrawCallAsync(PreparedDrawCallData.LeftBottomPoint, PreparedDrawCallData.RightTopPoint, PreparedDrawCallData.DataArray, PendingDrawCallData.DrawCallArray);
			//push to main thread queue
			PendingRebuildDrawCallQueue->Enqueue(MoveTemp(PendingDrawCallData));
			bIsBatching = false;
		}
	}

	return 0;
}
void FDreamCanvasDrawCallProcessingRunnable::Stop()
{
	if (!bIsRunning)
	{
		return;
	}

	bIsRunning = false;
	if (PreparedDrawCallDataQueueEvent != nullptr)
	{
		PreparedDrawCallDataQueueEvent->Trigger();
	}
	if (Thread.IsValid())
	{
		Thread->WaitForCompletion();
		Thread.Reset();
	}
	if (PreparedDrawCallDataQueueEvent != nullptr)
	{
		FPlatformProcess::ReturnSynchEventToPool(PreparedDrawCallDataQueueEvent);
		PreparedDrawCallDataQueueEvent = nullptr;
	}
	PreparedDrawCallDataQueue.Reset();
	PendingRebuildDrawCallQueue.Reset();
	bIsBatching = false;
}
void FDreamCanvasDrawCallProcessingRunnable::Exit()
{
	Stop();
}

void FDreamCanvasDrawCallProcessingRunnable::PushPreparedDrawCallData(FDreamCanvasPreparedDrawCallData InData)
{
	if (!bIsRunning || !PreparedDrawCallDataQueue.IsValid() || PreparedDrawCallDataQueueEvent == nullptr)
	{
		return;
	}

	PreparedDrawCallDataQueue->Enqueue(MoveTemp(InData));
	PreparedDrawCallDataQueueEvent->Trigger();
}

bool FDreamCanvasDrawCallProcessingRunnable::TryGetDrawCallData(FDreamCanvasPendingDrawCallData& OutData)
{
	if (!PendingRebuildDrawCallQueue.IsValid())
	{
		return false;
	}

	if (!PendingRebuildDrawCallQueue->IsEmpty())
	{
		while (PendingRebuildDrawCallQueue->Dequeue(OutData)){}//only use the newest one
		return true;
	}
	return false;
}
