// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "Tasks/Task.h"
#include "DreamCanvasProcessingDrawCallData.h"

/**
 * Off-thread draw-call batching for one canvas.
 *
 * This used to own a dedicated OS thread, created in UDreamCanvas::OnRegister, so a scene with thirty
 * nested canvases carried thirty permanently blocked threads that were busy for a fraction of a frame
 * each. It now launches a task on the engine's worker pool instead: the number of threads is whatever
 * the pool has, no matter how many canvases exist, and the game thread's "I cannot drop this frame"
 * wait can retract an unstarted batch and run it in place rather than sleeping until it is picked up.
 *
 * Exactly one batch is ever in flight per canvas (bIsBatching is the claim on it), which keeps the
 * pending queue in frame order.
 */
class DREAMGUI_API FDreamCanvasDrawCallProcessingRunnable
{
public:
	/** Stop() is what waits for the in-flight batch; the task reads queues this object owns. */
	~FDreamCanvasDrawCallProcessingRunnable()
	{
		Stop();
	}
	void Start();
	void Stop();
	bool IsBatching()const { return bIsBatching.load(std::memory_order_acquire); }
	/**
	 * Block until nothing is being batched. Prefer this to spinning on IsBatching(): the wait can
	 * retract a batch that has not started yet and run it on the calling thread.
	 */
	void WaitForBatchingToFinish();

	void PushPreparedDrawCallData(FDreamCanvasPreparedDrawCallData InData);
	bool TryGetDrawCallData(FDreamCanvasPendingDrawCallData& OutData);

private:
	/** Claim the in-flight slot and launch a batching task, or do nothing if a batch already holds it. */
	void LaunchBatchingTaskIfIdle();
	/** The task body: take the newest prepared data, batch it, publish it. */
	void ProcessPreparedDrawCallData();
	UE::Tasks::FTask GetBatchingTask()const;

	TSharedPtr<TQueue<FDreamCanvasPreparedDrawCallData>> PreparedDrawCallDataQueue;
	TSharedPtr<TQueue<FDreamCanvasPendingDrawCallData>> PendingRebuildDrawCallQueue;

	/** Guards BatchingTask only; never held across a wait. */
	mutable FCriticalSection BatchingTaskLock;
	UE::Tasks::FTask BatchingTask;

	std::atomic<bool> bIsRunning = false;
	std::atomic<bool> bIsBatching = false;
};
