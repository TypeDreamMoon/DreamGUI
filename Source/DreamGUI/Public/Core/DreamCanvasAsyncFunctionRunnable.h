#pragma once

#include "CoreMinimal.h"
#include "Containers/Queue.h"
#include "HAL/PlatformProcess.h"
#include "Misc/ScopeLock.h"
#include "Tasks/Task.h"

/**
 * Runs the canvas's vertex-transform work off the game thread.
 *
 * This was the second dedicated OS thread every UDreamCanvas created in OnRegister -- the pair cost
 * sixty permanently resident threads in a thirty-canvas hierarchy. It now runs the queue on a task,
 * so the thread count comes from the engine's worker pool and does not grow with the canvas count.
 * At most one drain task is in flight, so the queued functions still run one at a time, in order.
 */
class FDreamCanvasAsyncFunctionRunnable
{
public:
	/** Stop() is what waits for the in-flight drain; a queued function captures data this object owns. */
	~FDreamCanvasAsyncFunctionRunnable()
	{
		Stop();
	}
	void Start()
	{
		bIsRunning = true;
	}
	void PushFunction(TFunction<void()> InFunction)
	{
		if (!bIsRunning)
		{
			return;
		}

		++ItemCount;
		FunctionQueue.Enqueue(MoveTemp(InFunction));
		LaunchDrainTaskIfIdle();
	}
	bool IsRunning()const
	{
		return bIsRunning;
	}

	int NumItems()const
	{
		return ItemCount;
	}
	bool IsEmpty()const
	{
		return ItemCount == 0;
	}

	/**
	 * Block until everything pushed so far has run, and the drain that ran it has returned. Prefer this
	 * to sleeping on a per-item flag: the wait can retract a drain that has not started and run it on the
	 * calling thread.
	 *
	 * The slot alone is not enough to wait on. A drain gives it up before its last look at the queue --
	 * so that a push in between is not lost -- and is still reading this object for that look, and may
	 * launch the next drain from there. Returning on the slot let an owner destroy this under the drain's
	 * tail, which then wrote into freed memory: heap damage that surfaced wherever it happened to land.
	 *
	 * Nor is an idle slot proof that the queue is empty, which is why the count is asked last. Queued work
	 * with no drain to run it is exactly the state a lost hand-off leaves (see ReleaseSlot), and returning
	 * on it froze the editor: the canvas went on to read a geometry whose transform was still queued, and
	 * spun on its bIsCalculating for ever, with nothing left to run it. Such work is run here instead.
	 */
	void WaitForAllFunctions()
	{
		for (;;)
		{
			const UE::Tasks::FTask TaskToWait = GetDrainTask();
			if (!TaskToWait.IsCompleted())
			{
				TaskToWait.Wait();
				continue;//it may have launched the next drain on its way out
			}
			if (bIsDraining.load())
			{
				//a drain has taken the slot and is about to record its task; it will not be long
				FPlatformProcess::YieldThread();
				continue;
			}
			if (ItemCount.load() == 0)
			{
				return;
			}
			//pushed, and no drain coming for it: take the slot and run it on this thread
			bool bExpected = false;
			if (bIsDraining.compare_exchange_strong(bExpected, true))
			{
				DrainQueue();
			}
		}
	}

	void Stop()
	{
		if (!bIsRunning)
		{
			return;
		}

		bIsRunning = false;
		//the queued functions capture canvas-owned data, so none may still be running when this returns
		WaitForAllFunctions();
		{
			FScopeLock Lock(&DrainTaskLock);
			DrainTask = UE::Tasks::FTask();
		}
		FunctionQueue.Empty();
		ItemCount = 0;
	}

private:
	UE::Tasks::FTask GetDrainTask()
	{
		FScopeLock Lock(&DrainTaskLock);
		return DrainTask;
	}
	void LaunchDrainTaskIfIdle()
	{
		bool bExpected = false;
		if (!bIsDraining.compare_exchange_strong(bExpected, true))
		{
			return;//a drain already holds the slot, and it re-checks the queue before letting go
		}
		FScopeLock Lock(&DrainTaskLock);
		DrainTask = UE::Tasks::Launch(TEXT("DreamCanvasAsyncFunction"), [this]()
			{
				DrainQueue();
			});
	}
	void DrainQueue()
	{
		TFunction<void()> Function;
		while (FunctionQueue.Dequeue(Function))
		{
			Function();
			--ItemCount;
		}
		ReleaseSlot();
	}
	/**
	 * Gives up the slot, then relaunches if a push came in meanwhile: that push found the slot taken
	 * and launched nothing of its own.
	 *
	 * This is one half of a hand-off whose other half is PushFunction (count, enqueue, try the slot).
	 * The protocol only holds if at least one side sees the other, and that takes a total order over
	 * both sides' accesses. A release store, then a plain read of the queue, does not give one: x86 lets
	 * the read overtake the buffered store, so the drain could see the queue empty while the push saw
	 * the slot still taken, and neither ran the function. It sat queued with its geometry flagged
	 * bIsCalculating, and the next prepare of the canvas spun on the flag for ever. Both sides therefore
	 * use sequentially consistent atomics, and the re-check reads the count, which is an atomic, instead
	 * of the queue, whose emptiness test is a plain read.
	 */
	void ReleaseSlot()
	{
		bIsDraining.store(false);
		if (bIsRunning && ItemCount.load() > 0)
		{
			LaunchDrainTaskIfIdle();
		}
	}

	TQueue<TFunction<void()>, EQueueMode::Mpsc> FunctionQueue;
	/** Pushed and not yet run. Counted before the enqueue, so it is never behind the queue. */
	std::atomic<int> ItemCount = 0;

	/** Guards DrainTask only; never held across a wait. */
	FCriticalSection DrainTaskLock;
	UE::Tasks::FTask DrainTask;

	std::atomic<bool> bIsRunning = false;
	std::atomic<bool> bIsDraining = false;
};
