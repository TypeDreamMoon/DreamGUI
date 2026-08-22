// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "DreamCanvasProcessingDrawCallData.h"

class FDreamCanvasDrawCallProcessingRunnable : public FRunnable
{
public:
	void Start();

	//~ Begin FRunnable Interface
	virtual uint32 Run() override;
	virtual void Stop() override;
	virtual void Exit() override;
	//~ End FRunnable Interface
	bool IsBatching()const { return bIsBatching; }

	void PushPreparedDrawCallData(FDreamCanvasPreparedDrawCallData InData);
	bool TryGetDrawCallData(FDreamCanvasPendingDrawCallData& OutData);

private:
	TSharedPtr<TQueue<FDreamCanvasPreparedDrawCallData>> PreparedDrawCallDataQueue;
	TSharedPtr<TQueue<FDreamCanvasPendingDrawCallData>> PendingRebuildDrawCallQueue;
	FEvent* PreparedDrawCallDataQueueEvent = nullptr;
	
	std::atomic<bool> bIsRunning = false;
	std::atomic<bool> bIsBatching = false;
	TUniquePtr<FRunnableThread> Thread;
};
