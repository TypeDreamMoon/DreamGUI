// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/PlatformTime.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

/**
 * What DreamGUI's frames cost, counted as they happen: the time each stage takes on the thread that runs it, and how
 * much was drawn and uploaded.
 *
 * Always on. A stage is one clock read at each end and an atomic add, a handful per root canvas per frame, so the
 * numbers a benchmark reads, the ones DreamUI.Stats prints and the ones Unreal Insights shows -- every stage is also a
 * CPU trace scope of the same name -- describe the same frames. The totals are the process's, not a canvas's or a
 * world's: that is what a frame costs.
 *
 * The stages nest the way the frame does. CanvasUpdate runs inside ManagerTick, so the two are not to be added up;
 * Batching and RenderRecord run on their own threads, beside the game thread rather than inside it.
 */
namespace DreamUIRenderStats
{
	/** Where the time goes, in the order a frame reaches them. */
	enum class EStage : uint8
	{
		/** Game thread: the UI manager's tick -- behaviours, layout, clips, and every root canvas's update below. */
		ManagerTick,
		/** Game thread, inside ManagerTick: a root canvas and its children bringing widgets, clips and geometry up to date. */
		CanvasUpdate,
		/** A worker thread: a canvas building its draw calls out of the geometry the game thread prepared. */
		Batching,
		/** Game thread: taking finished draw calls into mesh sections and materials, and sending them to the render thread. */
		DrawCallSubmit,
		/** Render thread: recording the UI's passes into the frame's graph. */
		RenderRecord,
		Num
	};

	/** What was done, counted. */
	enum class ECounter : uint8
	{
		/** Render thread: mesh batches recorded into the UI's passes. */
		BatchesRecorded,
		/** Render thread: the vertices those batches draw. */
		VerticesRecorded,
		/** Game thread: mesh sections whose vertices and indices were sent to the render thread. */
		SectionUploads,
		/** Game thread: bytes of vertices, indices and data-texture rows sent to the render thread. */
		UploadedBytes,
		/** Game thread: texture updates the data textures enqueued. */
		DataTextureUpdates,
		/** Game thread: element geometries copied for the batching. One that did not change is not copied again. */
		GeometryCopies,
		/** Game thread: mesh sections a rebuilt canvas took back as they were, their vertices already on the GPU. */
		SectionReuses,
		/** Game thread: widgets whose clip and geometry a canvas update looked at. */
		WidgetsUpdated,
		/** Game thread: mesh sections that took the vertices of only the elements that changed, the rest left as they were. */
		SectionPatches,
		/** Game thread: canvases that prepared their elements and had their draw calls batched again. */
		DrawCallRebuilds,
		/** Game thread: canvases whose elements only moved, refreshed in the draw calls they had instead of rebuilt. */
		InPlaceRefreshes,
		/** Game thread: render layers that moved, each only its sections' matrix and box sent anew -- nothing under it. */
		RenderLayerMoves,
		/** Game thread: widgets made render layers, each costing its canvas one rebuild. */
		RenderLayerPromotions,
		/** Game thread: render layers taken back, each costing its canvas one rebuild. */
		RenderLayerDemotions,
		Num
	};

	inline constexpr int32 StageCount = static_cast<int32>(EStage::Num);
	inline constexpr int32 CounterCount = static_cast<int32>(ECounter::Num);

	/** The totals over a stretch of frames. */
	struct FSnapshot
	{
		/** Game-thread frames the totals cover. */
		uint64 Frames = 0;
		double Seconds[StageCount] = {};
		/** How many times each stage ran: per canvas, per batch, per frame, whatever the stage is of. */
		uint64 Entries[StageCount] = {};
		int64 Counters[CounterCount] = {};

		double GetMillisecondsPerFrame(EStage InStage) const
		{
			return Frames > 0 ? Seconds[static_cast<int32>(InStage)] * 1000.0 / static_cast<double>(Frames) : 0.0;
		}
		double GetPerFrame(ECounter InCounter) const
		{
			return Frames > 0 ? static_cast<double>(Counters[static_cast<int32>(InCounter)]) / static_cast<double>(Frames) : 0.0;
		}
	};

	DREAMGUIRENDERER_API void AddTime(EStage InStage, uint64 InCycles);
	DREAMGUIRENDERER_API void AddCount(ECounter InCounter, int64 InAmount);
	/** The totals since the last reset. With bInReset, counting starts again from zero at this frame. */
	DREAMGUIRENDERER_API FSnapshot TakeSnapshot(bool bInReset);
	DREAMGUIRENDERER_API const TCHAR* GetStageName(EStage InStage);
	DREAMGUIRENDERER_API const TCHAR* GetCounterName(ECounter InCounter);
	/** A snapshot as lines a person reads: milliseconds per frame for each stage, then each counter per frame. */
	DREAMGUIRENDERER_API FString Describe(const FSnapshot& InSnapshot);

	/** Times the scope it is declared in as InStage. */
	class FScopedStage
	{
	public:
		explicit FScopedStage(EStage InStage)
			: Stage(InStage)
			, StartCycles(FPlatformTime::Cycles64())
		{
		}
		~FScopedStage()
		{
			AddTime(Stage, FPlatformTime::Cycles64() - StartCycles);
		}
		FScopedStage(const FScopedStage&) = delete;
		FScopedStage& operator=(const FScopedStage&) = delete;

	private:
		EStage Stage;
		uint64 StartCycles;
	};
}

/** Counts the enclosing scope as InStage, and marks it for Unreal Insights as DreamUI_<InStage>. */
#define DREAMUI_STAGE_SCOPE(InStage) \
	TRACE_CPUPROFILER_EVENT_SCOPE(DreamUI_##InStage); \
	const DreamUIRenderStats::FScopedStage UE_JOIN(DreamUIStageScope_, __LINE__)(DreamUIRenderStats::EStage::InStage)
