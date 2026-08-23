// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DreamUIFontData_FreeTypeRender.h"
#include "Core/Text/DreamGlyphSdf.h"

struct FT_LibraryRec_;
struct FT_FaceRec_;

/**
 * Off-thread glyph rasterization for one font. The worker owns its own FreeType library and
 * faces, opened over shared copies of the font files, so the game thread's faces are never touched
 * from another thread (a FreeType face is not thread-safe, and HarfBuzz reads tables through it).
 * Jobs are drained by one task at a time; results wait in a list until the game thread collects
 * them. The object is shared with the running task, so a font may drop it at any moment.
 */
class FDreamGlyphRasterizer : public TSharedFromThis<FDreamGlyphRasterizer, ESPMode::ThreadSafe>
{
public:
	struct FJob
	{
		FDreamUIGlyphKey Key;
		float CharSize = 0.0f;
		bool bBold = false;
		/** What the generator needs: the atlas sample size, spread and synthetic bold, in pixels. */
		float PixelsPerEm = 0.0f;
		float SpreadPixels = 0.0f;
		float BoldPixels = 0.0f;
	};
	struct FResult
	{
		FJob Job;
		FDreamGlyphSdfResult Sdf;
		bool bSucceeded = false;
	};

	FDreamGlyphRasterizer();
	~FDreamGlyphRasterizer();

	/** Register the bytes behind a face index; the worker opens its own FT_Face over them on first use. */
	void SetFaceSource(int32 FaceIndex, const TSharedRef<const TArray<uint8>, ESPMode::ThreadSafe>& Bytes, int32 FaceIndexInFile);

	/** Queue a job and make sure a task is draining the queue. */
	void Enqueue(const FJob& Job);
	/** Collect finished results (game thread). */
	void Drain(TArray<FResult>& OutResults);
	/** Block until every queued job has been rasterized. For teardown and tests. */
	void WaitForAll();
	int32 NumQueued() const;
	bool HasResults() const;

private:
	struct FFaceSource
	{
		TSharedPtr<const TArray<uint8>, ESPMode::ThreadSafe> Bytes;
		int32 FaceIndexInFile = 0;
	};
	mutable FCriticalSection Lock;
	TArray<FJob> Queue;
	TArray<FResult> Results;
	TMap<int32, FFaceSource> FaceSources;
	bool bTaskRunning = false;

	// Worker-only state (one task at a time touches it).
	FT_LibraryRec_* Library = nullptr;
	TMap<int32, FT_FaceRec_*> Faces;

	void RunWorker();
	FT_FaceRec_* GetWorkerFace(int32 FaceIndex);
};
