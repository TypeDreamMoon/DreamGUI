// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/DreamTextUserWidget.h"

#include "Misc/Paths.h"

FString UDreamTextUserWidget::ResolveDuiFilePath(const FString& InPath)
{
	// Trimmed first because this string is typed by hand as often as it is picked, and a trailing
	// space turns "the file is right there" into DUI6001 with a message that looks identical to a
	// path that is genuinely wrong.
	FString ResolvedPath = InPath.TrimStartAndEnd();
	if (ResolvedPath.IsEmpty())
	{
		// Empty is the ordinary state of every widget blueprint that is not text-backed, not a
		// mistake: the caller reads it as "this class has no .dui" and leaves the hierarchy alone.
		return FString();
	}

	if (FPaths::IsRelative(ResolvedPath))
	{
		ResolvedPath = FPaths::Combine(FPaths::ProjectContentDir(), ResolvedPath);
	}
	// Absolute and normalised, because this string is about to be both opened AND printed: it is what
	// a diagnostic names when the file will not read, and a message log line only becomes something a
	// reader can act on -- or click -- when the path in it is the one on disk.
	ResolvedPath = FPaths::ConvertRelativePathToFull(ResolvedPath);
	FPaths::NormalizeFilename(ResolvedPath);
	return ResolvedPath;
}
