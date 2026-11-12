// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "MCP/DreamGUIMCPResult.h"

#include "Text/DreamUIDiagnostics.h"
#include "Text/DreamUIPaths.h"

#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace DreamGUIMCP
{
	namespace
	{
		/** The published manual, where every code has its place on its thousand's page. */
		const TCHAR* const ManualSite = TEXT("https://gui.toolchain.64hz.cn/");

		FString NormalizeAbsolute(const FString& Path)
		{
			FString Absolute = FPaths::ConvertRelativePathToFull(Path);
			FPaths::NormalizeFilename(Absolute);
			FPaths::CollapseRelativeDirectories(Absolute);
			return Absolute;
		}
	}

	const TCHAR* LexOutcome(const EOutcome Outcome)
	{
		switch (Outcome)
		{
		case EOutcome::Ok:          return TEXT("ok");
		case EOutcome::SourceError: return TEXT("source-error");
		case EOutcome::Refused:     return TEXT("refused");
		case EOutcome::EngineError: return TEXT("engine-error");
		}
		return TEXT("engine-error");
	}

	FResult FResult::MakeOk(const FString& InSummary)
	{
		FResult Result;
		Result.Summary = InSummary;
		return Result;
	}

	FResult FResult::MakeRefused(const FString& InReason, const FString& InSummary, const FString& InNext)
	{
		FResult Result;
		Result.Outcome = EOutcome::Refused;
		Result.Reason = InReason;
		Result.Summary = InSummary;
		if (!InNext.IsEmpty())
		{
			Result.Next.Add(InNext);
		}
		return Result;
	}

	FResult FResult::MakeEngineError(const FString& InReason, const FString& InSummary, const FString& InNext)
	{
		FResult Result = MakeRefused(InReason, InSummary, InNext);
		Result.Outcome = EOutcome::EngineError;
		return Result;
	}

	TSharedRef<FJsonObject> FResult::ToJson() const
	{
		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetBoolField(TEXT("ok"), Outcome == EOutcome::Ok);
		Root->SetStringField(TEXT("outcome"), LexOutcome(Outcome));
		if (Reason.IsEmpty())
		{
			Root->SetField(TEXT("reason"), MakeShared<FJsonValueNull>());
		}
		else
		{
			Root->SetStringField(TEXT("reason"), Reason);
		}
		Root->SetStringField(TEXT("summary"), Summary);

		// Errors first, then warnings, then infos, each kind in the order it was found: what a summary leads with is what
		// the list leads with, and an info never sits above the error it is read next to.
		TArray<const FDiagnostic*> Ordered;
		for (const FDiagnostic& Diagnostic : Diagnostics)
		{
			Ordered.Add(&Diagnostic);
		}
		const auto Rank = [](const FDiagnostic& Diagnostic)
		{
			if (Diagnostic.Severity.Equals(TEXT("error"), ESearchCase::IgnoreCase)) { return 0; }
			if (Diagnostic.Severity.Equals(TEXT("warning"), ESearchCase::IgnoreCase)) { return 1; }
			return 2;
		};
		Ordered.StableSort([&Rank](const FDiagnostic& A, const FDiagnostic& B) { return Rank(A) < Rank(B); });

		TArray<TSharedPtr<FJsonValue>> DiagnosticValues;
		for (const FDiagnostic* DiagnosticPtr : Ordered)
		{
			const FDiagnostic& Diagnostic = *DiagnosticPtr;
			TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
			if (!Diagnostic.Code.IsEmpty())
			{
				Object->SetStringField(TEXT("code"), Diagnostic.Code);
			}
			Object->SetStringField(TEXT("severity"), Diagnostic.Severity);
			if (!Diagnostic.File.IsEmpty())
			{
				Object->SetStringField(TEXT("file"), Diagnostic.File);
			}
			if (Diagnostic.Line > 0)
			{
				Object->SetNumberField(TEXT("line"), Diagnostic.Line);
				Object->SetNumberField(TEXT("column"), FMath::Max(1, Diagnostic.Column));
			}
			Object->SetStringField(TEXT("message"), Diagnostic.Message);
			if (!Diagnostic.Hint.IsEmpty())
			{
				Object->SetStringField(TEXT("hint"), Diagnostic.Hint);
			}
			if (!Diagnostic.Node.IsEmpty())
			{
				Object->SetStringField(TEXT("node"), Diagnostic.Node);
			}
			if (!Diagnostic.Stage.IsEmpty())
			{
				Object->SetStringField(TEXT("stage"), Diagnostic.Stage);
			}
			if (!Diagnostic.Doc.IsEmpty())
			{
				Object->SetStringField(TEXT("doc"), Diagnostic.Doc);
			}
			DiagnosticValues.Add(MakeShared<FJsonValueObject>(Object));
		}
		Root->SetArrayField(TEXT("diagnostics"), DiagnosticValues);

		TArray<TSharedPtr<FJsonValue>> WriteValues;
		for (const FWrite& Write : Writes)
		{
			TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
			Object->SetStringField(TEXT("asset"), Write.Asset);
			Object->SetStringField(TEXT("state"), Write.State);
			WriteValues.Add(MakeShared<FJsonValueObject>(Object));
		}
		Root->SetArrayField(TEXT("writes"), WriteValues);

		TArray<TSharedPtr<FJsonValue>> ArtifactValues;
		for (const FArtifact& Artifact : Artifacts)
		{
			TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
			Object->SetStringField(TEXT("kind"), Artifact.Kind);
			Object->SetStringField(TEXT("path"), Artifact.Path);
			Object->SetStringField(TEXT("title"), Artifact.Title);
			ArtifactValues.Add(MakeShared<FJsonValueObject>(Object));
		}
		Root->SetArrayField(TEXT("artifacts"), ArtifactValues);

		Root->SetObjectField(TEXT("data"), Data.IsValid() ? Data : MakeShared<FJsonObject>());

		TArray<TSharedPtr<FJsonValue>> NextValues;
		for (const FString& Step : Next)
		{
			NextValues.Add(MakeShared<FJsonValueString>(Step));
		}
		Root->SetArrayField(TEXT("next"), NextValues);
		return Root;
	}

	FString SerializeJson(const TSharedRef<FJsonObject>& Object)
	{
		FString Text;
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text);
		FJsonSerializer::Serialize(Object, Writer);
		return Text;
	}

	TSharedPtr<FJsonObject> ParseJsonObject(const FString& Text)
	{
		TSharedPtr<FJsonObject> Object;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		if (!FJsonSerializer::Deserialize(Reader, Object) || !Object.IsValid())
		{
			return nullptr;
		}
		return Object;
	}

	FString MakeProjectRelativePath(const FString& Path)
	{
		if (Path.IsEmpty())
		{
			return FString();
		}
		const FString Absolute = NormalizeAbsolute(Path);
		FString ProjectDirectory = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
		FPaths::NormalizeDirectoryName(ProjectDirectory);
		FString Relative = Absolute;
		if (!FPaths::MakePathRelativeTo(Relative, *(ProjectDirectory + TEXT("/"))))
		{
			// A path on another drive has no relative spelling. Its file name is all that may leave the process.
			Relative = FPaths::GetCleanFilename(Absolute);
		}
		Relative.ReplaceInline(TEXT("\\"), TEXT("/"));
		return Relative;
	}

	FString ResolveProjectFileArgument(const FString& Argument)
	{
		FString Value = Argument.TrimStartAndEnd();
		Value.ReplaceInline(TEXT("\\"), TEXT("/"));
		if (Value.IsEmpty())
		{
			return FString();
		}
		if (!FPaths::IsRelative(Value))
		{
			return NormalizeAbsolute(Value);
		}
		const FString UnderProject = NormalizeAbsolute(FPaths::Combine(FPaths::ProjectDir(), Value));
		if (IFileManager::Get().FileExists(*UnderProject))
		{
			return UnderProject;
		}
		// The spellings a `use` line and a Blueprint's Source File take: relative to a DUI/ root, or Plugin.X:...
		const FString UnderSourceRoot = NormalizeAbsolute(DreamUIPaths::Resolve(Value));
		if (IFileManager::Get().FileExists(*UnderSourceRoot))
		{
			return UnderSourceRoot;
		}
		return UnderProject;
	}

	FString ScrubAbsolutePaths(const FString& Text)
	{
		FString ProjectDirectory = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
		FPaths::NormalizeDirectoryName(ProjectDirectory);
		FString Scrubbed = Text.Replace(*(ProjectDirectory + TEXT("/")), TEXT(""), ESearchCase::IgnoreCase);
		FString Backslashed = ProjectDirectory + TEXT("/");
		Backslashed.ReplaceInline(TEXT("/"), TEXT("\\"));
		return Scrubbed.Replace(*Backslashed, TEXT(""), ESearchCase::IgnoreCase);
	}

	FString FormatDiagnosticCode(const int32 Code)
	{
		return Code > 0 ? FDreamUIDiagnostic::CodeToString(static_cast<EDreamUIDiagnosticCode>(Code)) : FString();
	}

	FString DescribeDiagnosticStage(const int32 Code)
	{
		switch (Code / 1000)
		{
		case 1: return TEXT("lexer");
		case 2: return TEXT("parser");
		case 3: return TEXT("semantic");
		case 4: return TEXT("values");
		case 5: return TEXT("builder");
		case 6: return TEXT("compile");
		case 7: return TEXT("write-back");
		case 8: return TEXT("layout");
		default: return FString();
		}
	}

	FString MakeDiagnosticDocUrl(const int32 Code)
	{
		if (Code < 1000 || Code > 9999)
		{
			return FString();
		}
		// https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui3xxx#dui3001, as Docs/diagnostics/index.md links them.
		return FString::Printf(TEXT("%sen/docs/diagnostics/dui%dxxx#dui%04d"), ManualSite, Code / 1000, Code); // I18N-EXEMPT: a URL
	}

	FDiagnostic MakeDiagnostic(const FDreamUIDiagnostic& Diagnostic, const FString& SourceFile)
	{
		FDiagnostic Out;
		const int32 Code = static_cast<int32>(Diagnostic.Code);
		Out.Code = FormatDiagnosticCode(Code);
		Out.Severity = Diagnostic.IsError() ? TEXT("error") : TEXT("warning");
		// The diagnostic's own file when it names one by its path (an imported style library's line), else the file checked.
		const bool bOwnFile = !Diagnostic.SourceName.IsEmpty() && !FPaths::IsRelative(Diagnostic.SourceName);
		Out.File = MakeProjectRelativePath(bOwnFile ? Diagnostic.SourceName : SourceFile);
		Out.Line = Diagnostic.Location.Line;
		Out.Column = Diagnostic.Location.Column;
		// A message may quote a path the compiler resolved (an import, a font): it leaves as a project-relative one.
		Out.Message = ScrubAbsolutePaths(Diagnostic.Message);
		Out.Stage = DescribeDiagnosticStage(Code);
		Out.Doc = MakeDiagnosticDocUrl(Code);
		return Out;
	}

	void AppendDiagnostics(TArray<FDiagnostic>& Out, const FDreamUIDiagnosticBag& Bag, const FString& SourceFile)
	{
		for (const FDreamUIDiagnostic& Diagnostic : Bag.Diagnostics)
		{
			Out.Add(MakeDiagnostic(Diagnostic, SourceFile));
		}
	}

	int32 CountSeverity(const TArray<FDiagnostic>& Diagnostics, const TCHAR* Severity)
	{
		int32 Count = 0;
		for (const FDiagnostic& Diagnostic : Diagnostics)
		{
			Count += Diagnostic.Severity.Equals(Severity, ESearchCase::IgnoreCase) ? 1 : 0;
		}
		return Count;
	}
}
