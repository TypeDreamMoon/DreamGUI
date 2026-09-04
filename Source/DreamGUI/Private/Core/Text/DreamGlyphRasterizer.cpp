// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/Text/DreamGlyphRasterizer.h"
#include "Async/Async.h"
#include "HAL/PlatformProcess.h"

#if WITH_FREETYPE
THIRD_PARTY_INCLUDES_START
#include <ft2build.h>
#include FT_FREETYPE_H
THIRD_PARTY_INCLUDES_END
#endif

FDreamGlyphRasterizer::FDreamGlyphRasterizer()
{
}

FDreamGlyphRasterizer::~FDreamGlyphRasterizer()
{
#if WITH_FREETYPE
	// Runs on whichever thread drops the last reference; by then no task is using the faces.
	for (auto& Pair : Faces)
	{
		if (Pair.Value != nullptr)
		{
			FT_Done_Face(Pair.Value);
		}
	}
	Faces.Empty();
	if (Library != nullptr)
	{
		FT_Done_FreeType(Library);
		Library = nullptr;
	}
#endif
}

void FDreamGlyphRasterizer::SetFaceSource(int32 FaceIndex, const TSharedRef<const TArray<uint8>, ESPMode::ThreadSafe>& Bytes, int32 FaceIndexInFile)
{
	FScopeLock ScopeLock(&Lock);
	FFaceSource& Source = FaceSources.FindOrAdd(FaceIndex);
	Source.Bytes = Bytes;
	Source.FaceIndexInFile = FaceIndexInFile;
}

bool FDreamGlyphRasterizer::HasFaceSource(int32 FaceIndex) const
{
	FScopeLock ScopeLock(&Lock);
	const FFaceSource* Source = FaceSources.Find(FaceIndex);
	return Source != nullptr && Source->Bytes.IsValid() && Source->Bytes->Num() > 0;
}

void FDreamGlyphRasterizer::Enqueue(const FJob& Job)
{
	bool bLaunch = false;
	{
		FScopeLock ScopeLock(&Lock);
		Queue.Add(Job);
		if (!bTaskRunning)
		{
			bTaskRunning = true;
			bLaunch = true;
		}
	}
	if (bLaunch)
	{
		TSharedRef<FDreamGlyphRasterizer, ESPMode::ThreadSafe> Self = AsShared();
		Async(EAsyncExecution::ThreadPool, [Self]()
		{
			Self->RunWorker();
		});
	}
}

void FDreamGlyphRasterizer::Drain(TArray<FResult>& OutResults)
{
	FScopeLock ScopeLock(&Lock);
	OutResults.Append(MoveTemp(Results));
	Results.Reset();
}

void FDreamGlyphRasterizer::WaitForAll()
{
	for (;;)
	{
		{
			FScopeLock ScopeLock(&Lock);
			if (Queue.Num() == 0 && !bTaskRunning)
			{
				return;
			}
		}
		FPlatformProcess::Sleep(0.001f);
	}
}

int32 FDreamGlyphRasterizer::NumQueued() const
{
	FScopeLock ScopeLock(&Lock);
	return Queue.Num();
}

bool FDreamGlyphRasterizer::HasResults() const
{
	FScopeLock ScopeLock(&Lock);
	return Results.Num() > 0;
}

FT_FaceRec_* FDreamGlyphRasterizer::GetWorkerFace(int32 FaceIndex)
{
#if WITH_FREETYPE
	if (FT_FaceRec_** Found = Faces.Find(FaceIndex))
	{
		return *Found;
	}
	FFaceSource Source;
	{
		FScopeLock ScopeLock(&Lock);
		if (const FFaceSource* Found = FaceSources.Find(FaceIndex))
		{
			Source = *Found;
		}
	}
	FT_FaceRec_* Face = nullptr;
	if (Source.Bytes.IsValid() && Source.Bytes->Num() > 0)
	{
		if (Library == nullptr)
		{
			if (FT_Init_FreeType(&Library) != 0)
			{
				Library = nullptr;
			}
		}
		if (Library != nullptr)
		{
			if (FT_New_Memory_Face(Library, Source.Bytes->GetData(), Source.Bytes->Num(), Source.FaceIndexInFile, &Face) != 0)
			{
				Face = nullptr;
			}
		}
	}
	// Remember failures too, so a missing face is not retried per glyph. The bytes stay referenced
	// by FaceSources for as long as the face may read them.
	Faces.Add(FaceIndex, Face);
	return Face;
#else
	return nullptr;
#endif
}

void FDreamGlyphRasterizer::RunWorker()
{
	for (;;)
	{
		FJob Job;
		{
			FScopeLock ScopeLock(&Lock);
			if (Queue.Num() == 0)
			{
				bTaskRunning = false;
				return;
			}
			Job = Queue[0];
			Queue.RemoveAt(0, 1, EAllowShrinking::No);
		}
		FResult Result;
		Result.Job = Job;
#if WITH_FREETYPE
		if (FT_FaceRec_* Face = GetWorkerFace(Job.Key.FaceIndex))
		{
			Result.bSucceeded = FDreamGlyphSdf::GenerateMTSDF(Face, Job.Key.GlyphIndex, Job.PixelsPerEm, Job.SpreadPixels, Job.BoldPixels, Result.Sdf);
		}
#endif
		{
			FScopeLock ScopeLock(&Lock);
			Results.Add(MoveTemp(Result));
		}
	}
}
