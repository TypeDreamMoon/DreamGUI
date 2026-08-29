// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Text/DreamUIDiagnostics.h"

/*
 * The one place a DUInnnn code turns into text, and the one place that decides what a diagnostic
 * looks like on a line.
 *
 * The layout is MSVC's, character for character: "File(Line,Col): error DUI3001: text". That is not
 * nostalgia -- it is the format Rider, Visual Studio, the UE message log and every editor's problem
 * matcher already know how to turn into a clickable jump. Inventing a prettier one would cost the
 * single most useful property these messages have, which is that double-clicking one lands the
 * caret on the offending column.
 *
 * Nothing here logs. The bag is a value that gets carried out to whoever asked for the parse, and
 * a front end that also wrote to LogDreamGUI would be a front end that cannot be run twice in a
 * test without the second run's expected errors swallowing the first run's real ones -- the shape
 * DreamShader's automation hit when assertion text collided with the expected-error filter.
 */

FString FDreamUIDiagnostic::CodeToString(EDreamUIDiagnosticCode InCode)
{
	// Four digits, zero padded, so DUI1001 and DUI7002 sort and align the same way. The table is
	// deliberately banded by stage (see the header), and fixed width is what makes the band readable
	// at a glance in a wall of messages.
	return FString::Printf(TEXT("DUI%04d"), static_cast<int32>(InCode));
}

FString FDreamUIDiagnostic::ToString() const
{
	const TCHAR* SeverityText = IsError() ? TEXT("error") : TEXT("warning");

	// Both halves of the prefix are optional and degrade independently. A diagnostic raised before
	// the bag knew its file name, or one about the file as a whole rather than a place in it, still
	// has to print something a human can read -- silently emitting "(0,0)" would be worse than
	// having no position at all, because it looks like a position.
	FString Prefix;
	if (!SourceName.IsEmpty())
	{
		Prefix = SourceName;
	}
	if (Location.IsValid())
	{
		Prefix += FString::Printf(TEXT("(%d,%d)"), Location.Line, Location.Column);
	}

	if (Prefix.IsEmpty())
	{
		return FString::Printf(TEXT("%s %s: %s"), SeverityText, *CodeToString(Code), *Message);
	}
	return FString::Printf(TEXT("%s: %s %s: %s"), *Prefix, SeverityText, *CodeToString(Code), *Message);
}

void FDreamUIDiagnosticBag::Add(FDreamUIDiagnostic InDiagnostic)
{
	// Stamped here rather than read from the bag at print time, because a bag can outlive the parse
	// that filled it: the compiler collects several files into one message log, and a diagnostic that
	// asked its bag for the file name would then name whichever file happened to be parsed last.
	if (InDiagnostic.SourceName.IsEmpty())
	{
		InDiagnostic.SourceName = SourceName;
	}
	Diagnostics.Add(MoveTemp(InDiagnostic));
}

void FDreamUIDiagnosticBag::AddError(EDreamUIDiagnosticCode InCode, const FDreamUISourceLocation& InLocation, FString InMessage)
{
	Add(FDreamUIDiagnostic(InCode, InLocation, MoveTemp(InMessage), EDreamUISeverity::Error));
}

void FDreamUIDiagnosticBag::AddWarning(EDreamUIDiagnosticCode InCode, const FDreamUISourceLocation& InLocation, FString InMessage)
{
	Add(FDreamUIDiagnostic(InCode, InLocation, MoveTemp(InMessage), EDreamUISeverity::Warning));
}

bool FDreamUIDiagnosticBag::HasErrors() const
{
	for (const FDreamUIDiagnostic& Diagnostic : Diagnostics)
	{
		if (Diagnostic.IsError())
		{
			return true;
		}
	}
	return false;
}

int32 FDreamUIDiagnosticBag::NumErrors() const
{
	int32 Count = 0;
	for (const FDreamUIDiagnostic& Diagnostic : Diagnostics)
	{
		Count += Diagnostic.IsError() ? 1 : 0;
	}
	return Count;
}

FString FDreamUIDiagnosticBag::ToString() const
{
	// Raise order, not sorted by position: a reader fixing a file works down it, and the parser
	// already walks the file in order. Sorting would only shuffle the recovery diagnostics that
	// follow a real one away from the thing that caused them.
	FString Result;
	for (const FDreamUIDiagnostic& Diagnostic : Diagnostics)
	{
		if (!Result.IsEmpty())
		{
			Result += LINE_TERMINATOR;
		}
		Result += Diagnostic.ToString();
	}
	return Result;
}
