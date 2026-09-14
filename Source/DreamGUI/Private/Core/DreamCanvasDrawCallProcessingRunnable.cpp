// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/DreamCanvasDrawCallProcessingRunnable.h"

#include "Misc/ScopeLock.h"
#include "Core/Components/DreamCanvas.h"

void FDreamCanvasDrawCallProcessingRunnable::Start()
{
	check (!bIsRunning);
	check (PreparedDrawCallDataQueue == nullptr);
	PreparedDrawCallDataQueue = MakeShared<TQueue<FDreamCanvasPreparedDrawCallData>>();
	PendingRebuildDrawCallQueue = MakeShared<TQueue<FDreamCanvasPendingDrawCallData>>();
	bIsRunning = true;
	bIsBatching = false;
}

UE::Tasks::FTask FDreamCanvasDrawCallProcessingRunnable::GetBatchingTask()const
{
	FScopeLock Lock(&BatchingTaskLock);
	return BatchingTask;
}

void FDreamCanvasDrawCallProcessingRunnable::LaunchBatchingTaskIfIdle()
{
	bool bExpected = false;
	if (!bIsBatching.compare_exchange_strong(bExpected, true))
	{
		return;//a batch already holds the slot, and it re-checks the queue before letting go
	}
	FScopeLock Lock(&BatchingTaskLock);
	BatchingTask = UE::Tasks::Launch(TEXT("DreamCanvasDrawCallBatching"), [this]()
		{
			ProcessPreparedDrawCallData();
		});
}

void FDreamCanvasDrawCallProcessingRunnable::ProcessPreparedDrawCallData()
{
	//local copies: Stop() waits for this task before dropping the queues, but reading the members
	//once is cheaper than repeatedly, and makes the lifetime contract explicit
	auto PreparedQueue = PreparedDrawCallDataQueue;
	auto PendingQueue = PendingRebuildDrawCallQueue;
	if (PreparedQueue.IsValid() && PendingQueue.IsValid())
	{
		FDreamCanvasPreparedDrawCallData PreparedDrawCallData;
		bool bGotData = false;
		while (PreparedQueue->Dequeue(PreparedDrawCallData)){bGotData = true;}//discard old data and get the newest one
		/**
		 * Nothing to take is a real outcome, not a bug to paper over: the task claims the slot before
		 * it runs, so a push that arrives in between finds the slot taken and leaves its data for this
		 * drain, and the re-check at the bottom can find the queue already emptied. Batching the
		 * default-constructed data instead produced an empty draw-call list stamped with FrameNumber 0
		 * -- the canvas flashed empty for a frame, and that zero also broke the cheap refresh path,
		 * whose whole test is CurrentDrawCallData.FrameNumber == NewestDrawCallFrameNumber.
		 */
		if (bGotData)
		{
			FDreamCanvasPendingDrawCallData PendingDrawCallData;
			PendingDrawCallData.FrameNumber = PreparedDrawCallData.FrameNumber;
			UDreamCanvas::BatchDrawCallAsync(PreparedDrawCallData.LeftBottomPoint, PreparedDrawCallData.RightTopPoint, PreparedDrawCallData.DataArray, PendingDrawCallData.DrawCallArray
				, PreparedDrawCallData.bCullElementsOutsideCanvasRect);
			//push to main thread queue
			PendingQueue->Enqueue(MoveTemp(PendingDrawCallData));
		}
	}

	bIsBatching.store(false, std::memory_order_release);

	//a push between the drain above and releasing the slot saw a batch in flight and launched nothing
	//of its own; pick that data up now rather than leaving it until the next push
	if (bIsRunning && PreparedQueue.IsValid() && !PreparedQueue->IsEmpty())
	{
		LaunchBatchingTaskIfIdle();
	}
}

void FDreamCanvasDrawCallProcessingRunnable::WaitForBatchingToFinish()
{
	//the loop is for the re-launch above, not a spin: Wait() blocks (or retracts the task and runs it
	//here), and a batch only re-launches while there is queued data, which the caller is not adding to
	while (bIsBatching.load(std::memory_order_acquire))
	{
		UE::Tasks::FTask TaskToWait = GetBatchingTask();
		if (!TaskToWait.IsValid())
		{
			break;
		}
		TaskToWait.Wait();
	}
}

void FDreamCanvasDrawCallProcessingRunnable::Stop()
{
	if (!bIsRunning)
	{
		return;
	}

	bIsRunning = false;
	//nothing may still be reading the queues when they go
	WaitForBatchingToFinish();
	{
		FScopeLock Lock(&BatchingTaskLock);
		BatchingTask = UE::Tasks::FTask();
	}
	PreparedDrawCallDataQueue.Reset();
	PendingRebuildDrawCallQueue.Reset();
	bIsBatching = false;
}

void FDreamCanvasDrawCallProcessingRunnable::PushPreparedDrawCallData(FDreamCanvasPreparedDrawCallData InData)
{
	if (!bIsRunning || !PreparedDrawCallDataQueue.IsValid())
	{
		return;
	}

	PreparedDrawCallDataQueue->Enqueue(MoveTemp(InData));
	LaunchBatchingTaskIfIdle();
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
