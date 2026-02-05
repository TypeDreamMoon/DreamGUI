#pragma once

class FLexCanvasAsyncFunctionRunnable : FRunnable
{
public:
	void Start()
	{
		bIsRunning = true;
		Thread.Reset(FRunnableThread::Create(this, TEXT("FLexCanvasDrawCallProcessingRunnable"), 0, TPri_Normal));
	}
	void PushFunction(const TFunction<void()>& InFunction)
	{
		FunctionQueue.Enqueue(InFunction);
		ItemCount++;
	}

	int NumItems()const
	{
		return ItemCount;
	}
	bool IsEmpty()const
	{
		return FunctionQueue.IsEmpty();
	}

	//~ Begin FRunnable Interface
	virtual uint32 Run() override
	{
		while (bIsRunning)
		{
			if (!FunctionQueue.IsEmpty())
			{
				while (!FunctionQueue.IsEmpty())
				{
					FunctionQueue.Dequeue(TempFunction);
					TempFunction();
					ItemCount--;
				}
			}
			else
			{
				FPlatformProcess::Sleep(0.001f);
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

		Thread->WaitForCompletion();
	}
	virtual void Exit() override
	{
		if (!bIsRunning)
		{
			return;
		}

		bIsRunning = false;

		Thread->WaitForCompletion();
	}
	//~ End FRunnable Interface

private:
	TFunction<void()> TempFunction = nullptr;
	TQueue<TFunction<void()>, EQueueMode::Mpsc> FunctionQueue;
	std::atomic<int> ItemCount = 0;
	
	std::atomic<bool> bIsRunning = false;
	TUniquePtr<FRunnableThread> Thread;
};
