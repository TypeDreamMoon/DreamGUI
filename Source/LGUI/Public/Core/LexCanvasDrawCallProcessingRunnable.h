// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

struct FLexCanvasPreparedDrawCallData;
struct FLexCanvasPendingDrawCallData;

class FLexCanvasDrawCallProcessingRunnable : public FRunnable
{
public:
	void Start(const TSharedPtr<TQueue<FLexCanvasPreparedDrawCallData>>& InDrawCallDataQueue, const TSharedPtr<TQueue<FLexCanvasPendingDrawCallData>>& InRebuildDrawCallQueue);

	//~ Begin FRunnable Interface
	virtual uint32 Run() override;
	virtual void Stop() override;
	virtual void Exit() override;
	//~ End FRunnable Interface
	bool IsBatching()const { return bIsBatching; }

private:
	TSharedPtr<TQueue<FLexCanvasPreparedDrawCallData>> PreparedDrawCallDataQueue;
	TSharedPtr<TQueue<FLexCanvasPendingDrawCallData>> RebuildDrawCallQueue;
	
	std::atomic<bool> bIsRunning = false;
	std::atomic<bool> bIsBatching = false;
	TUniquePtr<FRunnableThread> Thread;
};
