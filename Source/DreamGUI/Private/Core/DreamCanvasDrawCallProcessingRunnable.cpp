// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/DreamCanvasDrawCallProcessingRunnable.h"

#include "HAL/PlatformProcess.h"
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
		while (PreparedQueue->Dequeue(PreparedDrawCallData))//discard old data and get the newest one
		{
			bGotData = true;
			--NumPreparedQueued;
		}
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
			//the prepared data is this task's own and is not looked at again, so the batch may use it up
			UDreamCanvas::BatchDrawCallAsync(PreparedDrawCallData.LeftBottomPoint, PreparedDrawCallData.RightTopPoint, MoveTemp(PreparedDrawCallData.DataArray), PendingDrawCallData.DrawCallArray
				, PreparedDrawCallData.bCullElementsOutsideCanvasRect, &PreparedDrawCallData.GeometryListsOnSections);
			//push to main thread queue
			PendingQueue->Enqueue(MoveTemp(PendingDrawCallData));
		}
	}

	/**
	 * A push between the drain above and releasing the slot saw a batch in flight and launched nothing
	 * of its own; pick that data up now rather than leaving it until the next push.
	 *
	 * The release and the re-check are sequentially consistent, and the re-check reads the count rather
	 * than the queue, for the reason FDreamCanvasAsyncFunctionRunnable::ReleaseSlot gives: with a release
	 * store and a plain read of the queue, this could see the queue empty while the push saw the slot
	 * still taken, and the data sat unbatched -- for good, on a canvas nothing pushes to again.
	 */
	bIsBatching.store(false);
	if (bIsRunning && NumPreparedQueued.load() > 0)
	{
		LaunchBatchingTaskIfIdle();
	}
}

void FDreamCanvasDrawCallProcessingRunnable::WaitForBatchingToFinish()
{
	//until no batch holds the slot AND the last one launched has returned. A batch gives the slot up
	//before its re-check of the queue and is still reading this object for that re-check, and may
	//re-launch from there; returning on the slot alone let Stop()'s caller delete this under the batch's
	//tail, which then wrote into freed memory -- heap damage that surfaced wherever it landed.
	//
	//The loop is for that re-launch, not a spin: Wait() blocks (or retracts the task and runs it here),
	//and a batch only re-launches while there is queued data, which the caller is not adding to.
	//
	//An idle slot with data still queued is what a lost hand-off leaves behind; that data is batched
	//here, since the caller is waiting precisely for this frame's batch.
	for (;;)
	{
		UE::Tasks::FTask TaskToWait = GetBatchingTask();
		if (!TaskToWait.IsCompleted())
		{
			TaskToWait.Wait();
			continue;
		}
		if (bIsBatching.load())
		{
			//a batch has taken the slot and is about to record its task; it will not be long
			FPlatformProcess::YieldThread();
			continue;
		}
		if (NumPreparedQueued.load() == 0)
		{
			return;
		}
		bool bExpected = false;
		if (bIsBatching.compare_exchange_strong(bExpected, true))
		{
			ProcessPreparedDrawCallData();
		}
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
	NumPreparedQueued = 0;
	bIsBatching = false;
}

void FDreamCanvasDrawCallProcessingRunnable::PushPreparedDrawCallData(FDreamCanvasPreparedDrawCallData InData)
{
	if (!bIsRunning || !PreparedDrawCallDataQueue.IsValid())
	{
		return;
	}

	++NumPreparedQueued;
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
