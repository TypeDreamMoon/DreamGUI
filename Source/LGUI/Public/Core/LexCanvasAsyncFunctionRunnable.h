#pragma once

class FLexCanvasAsyncFunctionRunnable : FRunnable
{
public:
	void Start()
	{
		bIsRunning = true;
		PreparedDrawCallDataQueueEvent = FPlatformProcess::GetSynchEventFromPool();
		Thread.Reset(FRunnableThread::Create(this, TEXT("FLexCanvasDrawCallProcessingRunnable"), 0, TPri_Normal));
	}
	void PushFunction(const TFunction<void()>& InFunction)
	{
		FunctionQueue.Enqueue(InFunction);
		PreparedDrawCallDataQueueEvent->Trigger();
		ItemCount++;
	}

	int NumItems()const
	{
		return ItemCount;
	}
	bool IsEmpty()const
	{
		return ItemCount == 0;
	}

	//~ Begin FRunnable Interface
	virtual uint32 Run() override
	{
		while (bIsRunning)
		{
			if (PreparedDrawCallDataQueueEvent->Wait())
			{
				while (FunctionQueue.Dequeue(TempFunction))
				{
					TempFunction();
					ItemCount--;
				}
			}
		}

		return 0;
	}
	virtual void Stop() override
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
	virtual void Exit() override
	{
		Stop();
	}
	//~ End FRunnable Interface

private:
	TFunction<void()> TempFunction = nullptr;
	TQueue<TFunction<void()>, EQueueMode::Mpsc> FunctionQueue;
	std::atomic<int> ItemCount = 0;
	FEvent* PreparedDrawCallDataQueueEvent = nullptr;
	
	std::atomic<bool> bIsRunning = false;
	TUniquePtr<FRunnableThread> Thread;
};
