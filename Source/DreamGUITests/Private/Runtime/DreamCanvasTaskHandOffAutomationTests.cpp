// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/DreamCanvasAsyncFunctionRunnable.h"
#include "Core/DreamCanvasDrawCallProcessingRunnable.h"
#include "HAL/PlatformTime.h"

/*
 * The hand-off between a canvas's game-thread pushes and the one task that drains them.
 *
 * Both of a canvas's worker queues -- vertex transforms and draw-call batching -- keep at most one task
 * in flight: a push claims the slot and launches a task, or finds it taken and leaves its work for the
 * task holding it, which looks at the queue once more after letting the slot go. That last look used to
 * be a plain read of the queue after a release store, and x86 lets such a read overtake the store: the
 * task saw the queue empty while the push saw the slot still taken, and the work sat there with nothing
 * to run it. The wait then returned on the idle slot. For the vertex queue that froze the editor -- a
 * stress scene of a thousand animated world-space canvases, after a minute or so of play -- because the
 * canvas went on to read a geometry whose transform was still queued and spun on its bIsCalculating for
 * ever. For the batching queue it left the canvas showing an older frame's draw calls.
 *
 * The window is a few nanoseconds wide, so these tests cannot open it on demand. They push, wait a
 * varied few microseconds so the task launched by the first push is often letting go of its slot just as
 * the second arrives, push again and wait, many thousands of times, and ask after every wait whether all
 * the work is done. On the old hand-off that failed only by chance; what they pin for certain is that the
 * wait never returns early, whichever way the race goes.
 */

namespace DreamCanvasTaskHandOffTestLocal
{
	constexpr int32 MaxRounds = 100000;
	constexpr double MaxSeconds = 1.5;

	/** A busy pause of up to roughly ten microseconds, varied from round to round. */
	void PauseFor(uint32& Seed)
	{
		Seed = Seed * 1664525u + 1013904223u;
		const int32 Spins = static_cast<int32>((Seed >> 8) % 12000u);
		for (volatile int32 Spin = 0; Spin < Spins; Spin = Spin + 1)
		{
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCanvasAsyncFunctionHandOffTest,
	"DreamGUI.Canvas.TaskHandOff.EveryPushedVertexTransformHasRunWhenTheWaitReturns",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamCanvasAsyncFunctionHandOffTest::RunTest(const FString& Parameters)
{
	using namespace DreamCanvasTaskHandOffTestLocal;

	FDreamCanvasAsyncFunctionRunnable Runnable;
	Runnable.Start();
	std::atomic<int32> NumRun = 0;
	int32 NumPushed = 0;
	uint32 Seed = 12345u;
	int32 Round = 0;
	const double Deadline = FPlatformTime::Seconds() + MaxSeconds;
	for (; Round < MaxRounds && FPlatformTime::Seconds() < Deadline; ++Round)
	{
		Runnable.PushFunction([&NumRun]() { ++NumRun; });
		++NumPushed;
		PauseFor(Seed);
		Runnable.PushFunction([&NumRun]() { ++NumRun; });
		++NumPushed;
		Runnable.WaitForAllFunctions();
		if (NumRun.load() != NumPushed)
		{
			AddError(FString::Printf(TEXT("Round %d: the wait returned with %d of %d pushed functions run."), Round, NumRun.load(), NumPushed));
			break;
		}
	}
	TestTrue(TEXT("The queue is empty once the wait returns"), Runnable.IsEmpty());
	Runnable.Stop();
	AddInfo(FString::Printf(TEXT("%d rounds."), Round));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCanvasBatchingHandOffTest,
	"DreamGUI.Canvas.TaskHandOff.TheWaitForBatchingLeavesTheNewestPushBatched",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamCanvasBatchingHandOffTest::RunTest(const FString& Parameters)
{
	using namespace DreamCanvasTaskHandOffTestLocal;

	FDreamCanvasDrawCallProcessingRunnable Runnable;
	Runnable.Start();
	uint64 FrameNumber = 0;
	uint32 Seed = 54321u;
	int32 Round = 0;
	// Nothing to draw: what is under test is which push got batched, and the frame number says that.
	auto Push = [&Runnable, &FrameNumber]()
	{
		FDreamCanvasPreparedDrawCallData Data;
		Data.LeftBottomPoint = FVector2D(-50.0, -50.0);
		Data.RightTopPoint = FVector2D(50.0, 50.0);
		Data.FrameNumber = ++FrameNumber;
		Runnable.PushPreparedDrawCallData(MoveTemp(Data));
	};
	const double Deadline = FPlatformTime::Seconds() + MaxSeconds;
	for (; Round < MaxRounds && FPlatformTime::Seconds() < Deadline; ++Round)
	{
		Push();
		PauseFor(Seed);
		Push();
		Runnable.WaitForBatchingToFinish();
		FDreamCanvasPendingDrawCallData Batched;
		if (!Runnable.TryGetDrawCallData(Batched) || Batched.FrameNumber != FrameNumber)
		{
			AddError(FString::Printf(TEXT("Round %d: after the wait the newest batch is frame %llu, not the last pushed, %llu."),
				Round, Batched.FrameNumber, FrameNumber));
			break;
		}
	}
	Runnable.Stop();
	AddInfo(FString::Printf(TEXT("%d rounds."), Round));
	return true;
}

#endif
