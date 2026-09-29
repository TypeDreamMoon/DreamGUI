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
	 */
	void WaitForAllFunctions()
	{
		for (;;)
		{
			UE::Tasks::FTask TaskToWait;
			{
				FScopeLock Lock(&DrainTaskLock);
				TaskToWait = DrainTask;
			}
			if (TaskToWait.IsValid() && !TaskToWait.IsCompleted())
			{
				TaskToWait.Wait();
				continue;//it may have launched the next drain on its way out
			}
			if (!bIsDraining.load(std::memory_order_acquire))
			{
				return;
			}
			//a drain has taken the slot and is about to record its task; it will not be long
			FPlatformProcess::YieldThread();
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
		bIsDraining.store(false, std::memory_order_release);
		//a push between the drain above and releasing the slot launched nothing of its own
		if (bIsRunning && !FunctionQueue.IsEmpty())
		{
			LaunchDrainTaskIfIdle();
		}
	}

	TQueue<TFunction<void()>, EQueueMode::Mpsc> FunctionQueue;
	std::atomic<int> ItemCount = 0;

	/** Guards DrainTask only; never held across a wait. */
	FCriticalSection DrainTaskLock;
	UE::Tasks::FTask DrainTask;

	std::atomic<bool> bIsRunning = false;
	std::atomic<bool> bIsDraining = false;
};
