// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "DreamUIRender/DreamUIRenderStats.h"

#include "CoreGlobals.h"
#include "HAL/IConsoleManager.h"
#include "Misc/OutputDevice.h"

#include <atomic>

namespace DreamUIRenderStats
{
	namespace
	{
		// Relaxed throughout: each total is only ever added to, and a snapshot is a report, not a synchronisation point.
		std::atomic<uint64> StageCycles[StageCount];
		std::atomic<uint64> StageEntries[StageCount];
		std::atomic<int64> CounterValues[CounterCount];
		std::atomic<uint64> FrameAtReset{ 0 };

		const TCHAR* const StageNames[StageCount] =
		{
			TEXT("ManagerTick"),
			TEXT("CanvasUpdate"),
			TEXT("Batching"),
			TEXT("DrawCallSubmit"),
			TEXT("RenderRecord"),
		};
		const TCHAR* const CounterNames[CounterCount] =
		{
			TEXT("BatchesRecorded"),
			TEXT("VerticesRecorded"),
			TEXT("SectionUploads"),
			TEXT("UploadedBytes"),
			TEXT("DataTextureUpdates"),
			TEXT("GeometryCopies"),
			TEXT("SectionReuses"),
			TEXT("WidgetsUpdated"),
			TEXT("SectionPatches"),
			TEXT("DrawCallRebuilds"),
			TEXT("InPlaceRefreshes"),
		};
	}

	void AddTime(EStage InStage, uint64 InCycles)
	{
		const int32 Index = static_cast<int32>(InStage);
		if (Index < 0 || Index >= StageCount)
		{
			return;
		}
		StageCycles[Index].fetch_add(InCycles, std::memory_order_relaxed);
		StageEntries[Index].fetch_add(1, std::memory_order_relaxed);
	}

	void AddCount(ECounter InCounter, int64 InAmount)
	{
		const int32 Index = static_cast<int32>(InCounter);
		if (Index < 0 || Index >= CounterCount)
		{
			return;
		}
		CounterValues[Index].fetch_add(InAmount, std::memory_order_relaxed);
	}

	FSnapshot TakeSnapshot(bool bInReset)
	{
		FSnapshot Result;
		const uint64 Now = GFrameCounter;
		const uint64 Since = bInReset ? FrameAtReset.exchange(Now, std::memory_order_relaxed) : FrameAtReset.load(std::memory_order_relaxed);
		Result.Frames = Now > Since ? Now - Since : 0;
		for (int32 Index = 0; Index < StageCount; ++Index)
		{
			const uint64 Cycles = bInReset ? StageCycles[Index].exchange(0, std::memory_order_relaxed) : StageCycles[Index].load(std::memory_order_relaxed);
			Result.Seconds[Index] = FPlatformTime::ToSeconds64(Cycles);
			Result.Entries[Index] = bInReset ? StageEntries[Index].exchange(0, std::memory_order_relaxed) : StageEntries[Index].load(std::memory_order_relaxed);
		}
		for (int32 Index = 0; Index < CounterCount; ++Index)
		{
			Result.Counters[Index] = bInReset ? CounterValues[Index].exchange(0, std::memory_order_relaxed) : CounterValues[Index].load(std::memory_order_relaxed);
		}
		return Result;
	}

	const TCHAR* GetStageName(EStage InStage)
	{
		const int32 Index = static_cast<int32>(InStage);
		return Index >= 0 && Index < StageCount ? StageNames[Index] : TEXT("?");
	}

	const TCHAR* GetCounterName(ECounter InCounter)
	{
		const int32 Index = static_cast<int32>(InCounter);
		return Index >= 0 && Index < CounterCount ? CounterNames[Index] : TEXT("?");
	}

	FString Describe(const FSnapshot& InSnapshot)
	{
		FString Text = FString::Printf(TEXT("DreamGUI over %llu frame(s):\n"), InSnapshot.Frames);
		for (int32 Index = 0; Index < StageCount; ++Index)
		{
			const EStage Stage = static_cast<EStage>(Index);
			Text += FString::Printf(TEXT("  %-16s %8.3f ms/frame  (%llu run(s))\n"),
				GetStageName(Stage), InSnapshot.GetMillisecondsPerFrame(Stage), InSnapshot.Entries[Index]);
		}
		for (int32 Index = 0; Index < CounterCount; ++Index)
		{
			const ECounter Counter = static_cast<ECounter>(Index);
			Text += FString::Printf(TEXT("  %-18s %12.1f /frame  (%lld in all)\n"),
				GetCounterName(Counter), InSnapshot.GetPerFrame(Counter), InSnapshot.Counters[Index]);
		}
		return Text;
	}
}

// What the frames since the last call cost, and counting afresh from here.
static FAutoConsoleCommandWithOutputDevice GDreamUIStatsCommand(
	TEXT("DreamUI.Stats"),
	TEXT("Prints what DreamGUI's frames cost since the last DreamUI.Stats -- milliseconds per frame for each stage, and what was drawn and uploaded -- and starts counting again."),
	FConsoleCommandWithOutputDeviceDelegate::CreateLambda([](FOutputDevice& Ar)
	{
		Ar.Log(DreamUIRenderStats::Describe(DreamUIRenderStats::TakeSnapshot(/*bInReset*/ true)));
	}));
