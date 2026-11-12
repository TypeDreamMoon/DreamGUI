// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "MCP/DreamGUIMCPActions.h"

#include "MCP/DreamGUIMCPCompile.h"
#include "MCP/DreamGUIMCPEndpoint.h"
#include "MCP/DreamGUIMCPLayout.h"
#include "MCP/DreamGUIMCPLog.h"
#include "MCP/DreamGUIMCPPreviewStats.h"
#include "MCP/DreamGUIMCPService.h"
#include "MCP/DreamGUIMCPStage.h"

#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamTexture.h"
#include "Core/Components/DreamWidget.h"
#include "DreamWidgetBlueprint.h"
#include "Text/DreamUIAst.h"
#include "Text/DreamUIDiagnostics.h"
#include "Text/DreamUIDiagnosticsMailbox.h"
#include "Text/DreamUIPaths.h"
#include "Text/DreamUISourceFile.h"
#include "Text/DreamUISymbolExport.h"

#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Interfaces/IPluginManager.h"
#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"
#include "Misc/App.h"
#include "Misc/DateTime.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ShaderCompiler.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

namespace DreamGUIMCP
{
	namespace
	{
		// Every string below reaches the wire: invariant English, literal on purpose.

		/**
		 * What this endpoint last found in each file, absolute path -> the diagnostics: a check's, or a compile's (which a file
		 * that did not parse never deposits in the mailbox). What `diagnostics` has besides the mailbox.
		 */
		struct FCheckedFile
		{
			FString CheckedAt;
			FString From = TEXT("check");
			TArray<FDiagnostic> Diagnostics;
		};
		TMap<FString, FCheckedFile>& GetCheckedFiles()
		{
			static TMap<FString, FCheckedFile> Checked;
			return Checked;
		}

		FString Rel(const FString& Path)
		{
			return MakeProjectRelativePath(Path);
		}

		FString Plural(int32 Count, const TCHAR* One, const TCHAR* Many)
		{
			return FString::Printf(TEXT("%d %s"), Count, Count == 1 ? One : Many);
		}

		/** The `file` argument as an absolute path, or a refusal. */
		bool ResolveFile(const FActionCall& Call, FString& OutFile, FResult& OutRefusal)
		{
			const FString Argument = Call.GetString(TEXT("file"));
			if (Argument.TrimStartAndEnd().IsEmpty())
			{
				OutRefusal = FResult::MakeRefused(TEXT("bad-args"), FString::Printf(TEXT("%s needs file, a .dui path relative to the project root."), *Call.Action),
					TEXT("For example file=DUI/Menu.dui."));
				return false;
			}
			OutFile = ResolveProjectFileArgument(Argument);
			return true;
		}

		/** `1920x1080` -> (1920, 1080); false when it does not read or is out of range. */
		bool ParseSize(const FString& InText, FIntPoint& OutSize)
		{
			FString W;
			FString H;
			if (!InText.TrimStartAndEnd().Split(TEXT("x"), &W, &H, ESearchCase::IgnoreCase))
			{
				return false;
			}
			W.TrimStartAndEndInline();
			H.TrimStartAndEndInline();
			if (!W.IsNumeric() || !H.IsNumeric())
			{
				return false;
			}
			OutSize = FIntPoint(FCString::Atoi(*W), FCString::Atoi(*H));
			return OutSize.X >= 64 && OutSize.Y >= 64 && OutSize.X <= 7680 && OutSize.Y <= 4320;
		}

		FIntPoint GetViewport(const FActionCall& Call)
		{
			return FIntPoint(static_cast<int32>(Call.GetNumber(TEXT("width"), 1920.0)), static_cast<int32>(Call.GetNumber(TEXT("height"), 1080.0)));
		}

		void AddCompileWrites(FResult& Result, const FCompileOutcome& Outcome, const bool bHost)
		{
			for (const FCompiledAsset& Asset : Outcome.Assets)
			{
				if (Asset.bUnchanged || Asset.bFailed)
				{
					continue;
				}
				FWrite Write;
				Write.Asset = Asset.AssetPath;
				Write.State = !bHost ? TEXT("memory") : Asset.bCreated ? TEXT("created") : TEXT("saved");
				Result.Writes.Add(Write);
			}
		}

		TSharedRef<FJsonObject> CompileToJson(const FCompileOutcome& Outcome, const bool bHost)
		{
			TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
			TArray<TSharedPtr<FJsonValue>> Assets;
			for (const FCompiledAsset& Asset : Outcome.Assets)
			{
				TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetStringField(TEXT("file"), Asset.SourceFile);
				Entry->SetStringField(TEXT("asset"), Asset.AssetPath);
				Entry->SetStringField(TEXT("state"), Asset.bFailed ? TEXT("failed") : Asset.bUnchanged ? TEXT("unchanged")
					: Asset.bCreated ? TEXT("created") : TEXT("compiled"));
				if (!Asset.bFailed && !Asset.bUnchanged)
				{
					Entry->SetBoolField(TEXT("onDisk"), Asset.bSaved);
				}
				Assets.Add(MakeShared<FJsonValueObject>(Entry));
			}
			Data->SetArrayField(TEXT("assets"), Assets);
			Data->SetStringField(TEXT("note"), bHost
				? TEXT("Change the source, not the asset. In a host every Blueprint a compile writes is saved to disk.")
				: TEXT("Change the source, not the asset. In the user's editor the Blueprint stays in memory until the user saves it."));
			return Data;
		}

		/**
		 * The compile a layout or a preview needs first, as a refusal or a source error when it did not compile -- or null
		 * when the screen is there to look at. Its writes and warnings go on OutResult either way.
		 */
		/** Each file a compile read, with what it found there: the latest word on those files for `diagnostics`. */
		void RememberCompile(const FCompileOutcome& Outcome)
		{
			for (const FCompiledAsset& Asset : Outcome.Assets)
			{
				const FString File = ResolveProjectFileArgument(Asset.SourceFile);
				FCheckedFile& Remembered = GetCheckedFiles().FindOrAdd(File);
				Remembered.CheckedAt = FDateTime::UtcNow().ToIso8601();
				Remembered.From = TEXT("compile");
				Remembered.Diagnostics.Reset();
				for (const FDiagnostic& Diagnostic : Outcome.Diagnostics)
				{
					if (Diagnostic.File == Asset.SourceFile)
					{
						Remembered.Diagnostics.Add(Diagnostic);
					}
				}
			}
		}

		bool CompileForLooking(const FString& InFile, const FActionCall& Call, FResult& OutResult, UDreamWidgetBlueprint*& OutBlueprint, FDreamUIAst& OutAst)
		{
			const bool bHost = Call.Mode == EEndpointMode::Host;
			const FCompileOutcome Outcome = CompileSource(InFile, /*bSave*/ bHost, Call.GetBool(TEXT("force"), false));
			RememberCompile(Outcome);
			if (!Outcome.RefusalReason.IsEmpty())
			{
				OutResult = FResult::MakeRefused(Outcome.RefusalReason, Outcome.RefusalSummary, Outcome.RefusalNext);
				return false;
			}
			AddCompileWrites(OutResult, Outcome, bHost);
			OutResult.Diagnostics.Append(Outcome.Diagnostics);
			if (Outcome.bFailed || Outcome.Blueprint == nullptr || Outcome.Blueprint->GeneratedClass == nullptr)
			{
				OutResult.Outcome = EOutcome::SourceError;
				OutResult.Summary = FString::Printf(TEXT("%s did not compile (%s), so there is no screen to %s."), *Rel(InFile),
					*Plural(CountSeverity(OutResult.Diagnostics, TEXT("error")), TEXT("error"), TEXT("errors")), Call.Action == TEXT("preview") ? TEXT("draw") : TEXT("lay out"));
				OutResult.Next.Add(TEXT("Fix the errors at the lines given; check finds most of them without building anything."));
				return false;
			}
			OutBlueprint = Outcome.Blueprint;
			FString Text;
			FFileHelper::LoadFileToString(Text, *InFile);
			FDreamUIDiagnosticBag Ignored;
			FDreamUISourceFile::Parse(Text, InFile, OutAst, Ignored, FDreamUISourceFile::MakeFileImportReader());
			return true;
		}

		/** The layout passes a screen needs: until every glyph is in and two passes running agree, at most a dozen. */
		void SettleLayout(FScreenStage& Stage)
		{
			FString Previous;
			for (int32 Pass = 0; Pass < 12; ++Pass)
			{
				Stage.Pass(1.0f / 30.0f);
				const int32 Pending = Stage.CountPendingGlyphs(/*bFinish*/ true);
				FString Signature;
				TArray<UDreamWidget*> Widgets;
				UDreamWidget::CollectChildrenWidgets(Stage.GetRoot(), Widgets, true);
				for (const UDreamWidget* Widget : Widgets)
				{
					Signature += FString::Printf(TEXT("%.1f,%.1f;"), Widget->GetWidth(), Widget->GetHeight());
				}
				if (Pass >= 2 && Pending == 0 && Signature == Previous)
				{
					return;
				}
				Previous = MoveTemp(Signature);
			}
		}

		// ---------------------------------------------------------------------------------------------- check

		TUniquePtr<IJob> StartCheck(const FActionCall& Call)
		{
			TArray<FString> Files;
			const bool bAll = Call.GetBool(TEXT("all"), false);
			if (bAll)
			{
				FindAllSources(Files);
			}
			else
			{
				FString File;
				FResult Refusal;
				if (!ResolveFile(Call, File, Refusal))
				{
					return MakeUnique<FFunctionJob>([Refusal](FJobContext&, FResult& OutResult) { OutResult = Refusal; return true; });
				}
				Files.Add(File);
			}

			struct FState
			{
				TArray<FString> Files;
				int32 Next = 0;
				int32 Clean = 0;
				int32 Nodes = 0;
				FResult Result;
				TArray<TSharedPtr<FJsonValue>> PerFile;
			};
			TSharedRef<FState> State = MakeShared<FState>();
			State->Files = MoveTemp(Files);
			return MakeUnique<FFunctionJob>([State, bAll](FJobContext& Context, FResult& OutResult)
			{
				if (State->Files.Num() == 0)
				{
					OutResult = FResult::MakeOk(TEXT("There is no .dui file under the project's or the plugins' DUI/ folders."));
					OutResult.Next.Add(TEXT("Write screens under DUI/ in the project folder, for example DUI/Menu.dui."));
					return true;
				}
				if (Context.IsCancelRequested())
				{
					OutResult = FResult::MakeRefused(TEXT("cancelled"), FString::Printf(TEXT("Cancelled after %d of %d files."), State->Next, State->Files.Num()));
					return true;
				}
				const FString File = State->Files[State->Next++];
				Context.SetProgress(Rel(File), State->Next, State->Files.Num());
				const FCheckOutcome Check = CheckSource(File);
				if (!Check.bRead)
				{
					if (!bAll)
					{
						OutResult = FResult::MakeRefused(Check.RefusalReason, Check.RefusalSummary,
							TEXT("Give file as a path relative to the project root, for example DUI/Menu.dui."));
						return true;
					}
				}
				else
				{
					FCheckedFile& Remembered = GetCheckedFiles().FindOrAdd(File);
					Remembered.CheckedAt = FDateTime::UtcNow().ToIso8601();
					Remembered.Diagnostics = Check.Diagnostics;
					State->Result.Diagnostics.Append(Check.Diagnostics);
					State->Clean += Check.bClean ? 1 : 0;
					State->Nodes += Check.Nodes;
					TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
					Entry->SetStringField(TEXT("file"), Rel(File));
					Entry->SetStringField(TEXT("asset"), Check.AssetPath);
					Entry->SetNumberField(TEXT("nodes"), Check.Nodes);
					Entry->SetNumberField(TEXT("errors"), CountSeverity(Check.Diagnostics, TEXT("error")));
					Entry->SetNumberField(TEXT("warnings"), CountSeverity(Check.Diagnostics, TEXT("warning")));
					State->PerFile.Add(MakeShared<FJsonValueObject>(Entry));
				}
				if (State->Next < State->Files.Num())
				{
					return false;
				}

				FResult& Result = State->Result;
				const int32 Errors = CountSeverity(Result.Diagnostics, TEXT("error"));
				const int32 Warnings = CountSeverity(Result.Diagnostics, TEXT("warning"));
				Result.Outcome = Errors > 0 ? EOutcome::SourceError : EOutcome::Ok;
				Result.Data = MakeShared<FJsonObject>();
				Result.Data->SetNumberField(TEXT("checked"), State->PerFile.Num());
				Result.Data->SetNumberField(TEXT("errors"), Errors);
				Result.Data->SetNumberField(TEXT("warnings"), Warnings);
				Result.Data->SetArrayField(TEXT("files"), State->PerFile);
				if (State->PerFile.Num() == 1)
				{
					Result.Summary = FString::Printf(TEXT("Checked %s: %s, %s, %s. Nothing was built."), *Rel(State->Files[0]),
						*Plural(Errors, TEXT("error"), TEXT("errors")), *Plural(Warnings, TEXT("warning"), TEXT("warnings")),
						*Plural(State->Nodes, TEXT("node"), TEXT("nodes")));
				}
				else
				{
					Result.Summary = FString::Printf(TEXT("Checked %d files: %d clean, %s, %s. Nothing was built."), State->PerFile.Num(), State->Clean,
						*Plural(Errors, TEXT("error"), TEXT("errors")), *Plural(Warnings, TEXT("warning"), TEXT("warnings")));
				}
				if (Errors > 0)
				{
					Result.Next.Add(TEXT("Fix the errors at the lines given, then check again. Each code has its page at the doc link."));
				}
				else
				{
					Result.Next.Add(TEXT("layout compiles the screen and lays it out at a viewport size; it lists every node's rect and the problems it sees."));
					Result.Next.Add(TEXT("check reads the file as the compiler does but cannot see what only a compile sees (props, events, view models, expression bindings): compile or layout reports those."));
				}
				OutResult = Result;
				return true;
			});
		}

		// -------------------------------------------------------------------------------------------- compile

		TUniquePtr<IJob> StartCompile(const FActionCall& Call)
		{
			FString File;
			FResult Refusal;
			if (!ResolveFile(Call, File, Refusal))
			{
				return MakeUnique<FFunctionJob>([Refusal](FJobContext&, FResult& OutResult) { OutResult = Refusal; return true; });
			}
			const bool bHost = Call.Mode == EEndpointMode::Host;
			const bool bForce = Call.GetBool(TEXT("force"), false);
			return MakeUnique<FFunctionJob>([File, bHost, bForce](FJobContext& Context, FResult& OutResult)
			{
				Context.SetProgress(TEXT("compile"), 0, 0);
				const FCompileOutcome Outcome = CompileSource(File, /*bSave*/ bHost, bForce);
				RememberCompile(Outcome);
				if (!Outcome.RefusalReason.IsEmpty())
				{
					OutResult = FResult::MakeRefused(Outcome.RefusalReason, Outcome.RefusalSummary, Outcome.RefusalNext);
					return true;
				}
				FResult Result;
				AddCompileWrites(Result, Outcome, bHost);
				Result.Diagnostics = Outcome.Diagnostics;
				Result.Data = CompileToJson(Outcome, bHost);
				const int32 Errors = CountSeverity(Result.Diagnostics, TEXT("error"));
				const int32 Warnings = CountSeverity(Result.Diagnostics, TEXT("warning"));
				int32 Written = 0;
				int32 Unchanged = 0;
				for (const FCompiledAsset& Asset : Outcome.Assets)
				{
					Written += (!Asset.bFailed && !Asset.bUnchanged) ? 1 : 0;
					Unchanged += Asset.bUnchanged ? 1 : 0;
				}
				const FString Asset = Outcome.Assets.Num() > 0 ? Outcome.Assets.Last().AssetPath : FString();
				if (Outcome.bFailed)
				{
					Result.Outcome = EOutcome::SourceError;
					Result.Summary = FString::Printf(TEXT("%s did not compile into %s: %s, %s."), *Rel(File), *Asset,
						*Plural(Errors, TEXT("error"), TEXT("errors")), *Plural(Warnings, TEXT("warning"), TEXT("warnings")));
					Result.Next.Add(TEXT("Fix the errors at the lines given, then compile again."));
				}
				else
				{
					Result.Summary = FString::Printf(TEXT("Compiled %s into %s: %s written%s, %s."), *Rel(File), *Asset,
						*Plural(Written, TEXT("Blueprint"), TEXT("Blueprints")),
						Unchanged > 0 ? *FString::Printf(TEXT(", %d unchanged"), Unchanged) : TEXT(""),
						*Plural(Warnings, TEXT("warning"), TEXT("warnings")));
					Result.Next.Add(TEXT("layout lays the screen out and lists what is where; preview draws it."));
				}
				OutResult = Result;
				return true;
			});
		}

		// ---------------------------------------------------------------------------------------- diagnostics

		FResult RunDiagnostics(const FActionCall& Call)
		{
			struct FKnown
			{
				FString File;
				FString From;
				TArray<FDiagnostic> Diagnostics;
			};
			TMap<FString, FKnown> Known;
			TArray<FDreamUIDiagnosticsMailboxEntry> Entries;
			FDreamUIDiagnosticsMailbox::GetEntries(Entries);
			for (const FDreamUIDiagnosticsMailboxEntry& Entry : Entries)
			{
				FKnown& File = Known.FindOrAdd(FPaths::ConvertRelativePathToFull(Entry.SourceName));
				File.File = Entry.SourceName;
				File.From = TEXT("compile");
				File.Diagnostics.Reset();
				for (const FDreamUIDiagnostic& Diagnostic : Entry.Diagnostics)
				{
					File.Diagnostics.Add(MakeDiagnostic(Diagnostic, Entry.SourceName));
				}
			}
			// A check that ran after the last compile of a file is what is known of it now.
			for (const TPair<FString, FCheckedFile>& Pair : GetCheckedFiles())
			{
				const FString Key = FPaths::ConvertRelativePathToFull(Pair.Key);
				FKnown* Existing = Known.Find(Key);
				if (Existing == nullptr)
				{
					for (TPair<FString, FKnown>& Other : Known)
					{
						if (FPaths::IsSamePath(Other.Key, Key))
						{
							Existing = &Other.Value;
						}
					}
				}
				FKnown& File = Existing != nullptr ? *Existing : Known.Add(Key);
				File.File = Pair.Key;
				File.From = Pair.Value.From;
				File.Diagnostics = Pair.Value.Diagnostics;
			}

			FString Wanted;
			if (!Call.GetString(TEXT("file")).IsEmpty())
			{
				Wanted = ResolveProjectFileArgument(Call.GetString(TEXT("file")));
			}
			FResult Result;
			Result.Data = MakeShared<FJsonObject>();
			TArray<FKnown*> Files;
			for (TPair<FString, FKnown>& Pair : Known)
			{
				if (Wanted.IsEmpty() || FPaths::IsSamePath(Pair.Key, Wanted))
				{
					Files.Add(&Pair.Value);
				}
			}
			const auto ErrorsOf = [](const FKnown* InFile) { return CountSeverity(InFile->Diagnostics, TEXT("error")); };
			Files.StableSort([&ErrorsOf](const FKnown& A, const FKnown& B) { return ErrorsOf(&A) > ErrorsOf(&B); });

			if (!Wanted.IsEmpty() && Files.Num() == 0)
			{
				Result.Summary = FString::Printf(TEXT("This engine has nothing on %s: it was not checked or compiled since the engine started."), *Rel(Wanted));
				Result.Next.Add(FString::Printf(TEXT("check file=%s reads it now."), *Rel(Wanted)));
				Result.Data->SetArrayField(TEXT("files"), {});
				return Result;
			}
			TArray<TSharedPtr<FJsonValue>> FileValues;
			TArray<FString> Named;
			int32 Errors = 0;
			int32 WithAny = 0;
			for (const FKnown* File : Files)
			{
				const int32 FileErrors = CountSeverity(File->Diagnostics, TEXT("error"));
				const int32 FileWarnings = CountSeverity(File->Diagnostics, TEXT("warning"));
				Errors += FileErrors;
				Result.Diagnostics.Append(File->Diagnostics);
				TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetStringField(TEXT("file"), Rel(File->File));
				Entry->SetStringField(TEXT("from"), File->From == TEXT("check") ? TEXT("the last check") : TEXT("the last compile"));
				Entry->SetNumberField(TEXT("errors"), FileErrors);
				Entry->SetNumberField(TEXT("warnings"), FileWarnings);
				FileValues.Add(MakeShared<FJsonValueObject>(Entry));
				if (File->Diagnostics.Num() > 0)
				{
					++WithAny;
					if (Named.Num() < 10)
					{
						Named.Add(FString::Printf(TEXT("%s has %s, %s"), *Rel(File->File), *Plural(FileErrors, TEXT("error"), TEXT("errors")),
							*Plural(FileWarnings, TEXT("warning"), TEXT("warnings"))));
					}
				}
			}
			Result.Data->SetArrayField(TEXT("files"), FileValues);
			Result.Outcome = Errors > 0 ? EOutcome::SourceError : EOutcome::Ok;
			if (Files.Num() == 0)
			{
				Result.Summary = TEXT("This engine has checked or compiled no .dui since it started, so it holds no diagnostics.");
				Result.Next.Add(TEXT("check file=<path> reads a file now; check all=true reads every one."));
			}
			else if (WithAny == 0)
			{
				Result.Summary = FString::Printf(TEXT("No diagnostics: %s, all clean when last read."), *Plural(Files.Num(), TEXT("file"), TEXT("files")));
			}
			else
			{
				Result.Summary = FString::Printf(TEXT("%d of %d file(s) have diagnostics: %s%s."), WithAny, Files.Num(), *FString::Join(Named, TEXT("; ")),
					WithAny > Named.Num() ? *FString::Printf(TEXT("; %d more"), WithAny - Named.Num()) : TEXT(""));
				if (Errors > 0)
				{
					Result.Next.Add(TEXT("Fix the errors first, at the lines given."));
				}
			}
			return Result;
		}

		// --------------------------------------------------------------------------------------------- lookup

		/** The symbol table, built at most once every 30 s: walking every class is not free, and classes rarely appear. */
		TSharedRef<FJsonObject> GetSymbols()
		{
			static TSharedPtr<FJsonObject> Cached;
			static double BuiltAt = -1000.0;
			const double Now = FPlatformTime::Seconds();
			if (!Cached.IsValid() || Now - BuiltAt > 30.0)
			{
				Cached = FDreamUISymbolExport::BuildSymbols(/*bInvariantTooltips*/ true);
				BuiltAt = Now;
			}
			return Cached.ToSharedRef();
		}

		FString FirstSentence(const FString& InText)
		{
			FString Text = InText.Replace(TEXT("\r"), TEXT(" ")).Replace(TEXT("\n"), TEXT(" "));
			int32 Stop = INDEX_NONE;
			for (int32 Index = 0; Index + 1 < Text.Len(); ++Index)
			{
				if (Text[Index] == TEXT('.') && Text[Index + 1] == TEXT(' '))
				{
					Stop = Index + 1;
					break;
				}
			}
			if (Stop != INDEX_NONE)
			{
				Text.LeftInline(Stop);
			}
			Text.TrimStartAndEndInline();
			return Text.Len() > 160 ? Text.Left(157) + TEXT("...") : Text;
		}

		/** How many property lines one entry lists before it says how many more there are. */
		constexpr int32 MaxPropertyLines = 60;

		/** A tag's or component's entry, condensed: one line per property, tooltips cut to their first sentence. */
		TSharedRef<FJsonObject> CondenseEntry(const FString& InName, const FString& InKind, const TSharedPtr<FJsonObject>& InEntry, const FString& InFilter)
		{
			TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
			Out->SetStringField(TEXT("name"), InName);
			Out->SetStringField(TEXT("kind"), InKind);
			FString Class;
			if (InEntry->TryGetStringField(TEXT("class"), Class))
			{
				Out->SetStringField(TEXT("class"), Class);
			}
			FString Tooltip;
			if (InEntry->TryGetStringField(TEXT("tooltip"), Tooltip))
			{
				Out->SetStringField(TEXT("about"), FirstSentence(Tooltip));
			}
			const TArray<TSharedPtr<FJsonValue>>* Properties = nullptr;
			if (InEntry->TryGetArrayField(TEXT("properties"), Properties))
			{
				TArray<TSharedPtr<FJsonValue>> Lines;
				int32 Left = 0;
				for (const TSharedPtr<FJsonValue>& Value : *Properties)
				{
					const TSharedPtr<FJsonObject>* Property = nullptr;
					if (!Value->TryGetObject(Property))
					{
						continue;
					}
					FString Name, Type, Default, Enum, PropTooltip;
					(*Property)->TryGetStringField(TEXT("name"), Name);
					if (!InFilter.IsEmpty() && !Name.StartsWith(InFilter) && !Name.Contains(TEXT(".") + InFilter))
					{
						continue;
					}
					// A style's override switches follow from writing the value; listing them doubles the entry for nothing.
					if (Name.StartsWith(TEXT("bOverride_")) || Name.Contains(TEXT(".bOverride_")))
					{
						continue;
					}
					if (Lines.Num() >= MaxPropertyLines)
					{
						++Left;
						continue;
					}
					(*Property)->TryGetStringField(TEXT("type"), Type);
					(*Property)->TryGetStringField(TEXT("default"), Default);
					(*Property)->TryGetStringField(TEXT("enum"), Enum);
					(*Property)->TryGetStringField(TEXT("tooltip"), PropTooltip);
					FString Line = FString::Printf(TEXT("%s: %s"), *Name, *(Enum.IsEmpty() ? Type : Enum));
					if (!Default.IsEmpty())
					{
						Line += FString::Printf(TEXT(" = %s"), *Default);
					}
					if (!PropTooltip.IsEmpty())
					{
						Line += FString::Printf(TEXT(" -- %s"), *FirstSentence(PropTooltip));
					}
					Lines.Add(MakeShared<FJsonValueString>(Line));
				}
				Out->SetArrayField(TEXT("properties"), Lines);
				if (Left > 0)
				{
					Out->SetStringField(TEXT("more"), FString::Printf(TEXT("%d more properties; narrow with %s.<part of a name>, e.g. %s.Style"), Left, *InName, *InName));
				}
			}
			for (const TCHAR* Field : { TEXT("events"), TEXT("singleCastEvents") })
			{
				const TArray<TSharedPtr<FJsonValue>>* Events = nullptr;
				if (InEntry->TryGetArrayField(Field, Events) && Events->Num() > 0)
				{
					TArray<TSharedPtr<FJsonValue>> Names;
					for (const TSharedPtr<FJsonValue>& Event : *Events)
					{
						const TSharedPtr<FJsonObject>* EventObject = nullptr;
						FString EventName;
						if (Event->TryGetObject(EventObject))
						{
							(*EventObject)->TryGetStringField(TEXT("name"), EventName);
						}
						else
						{
							EventName = Event->AsString();
						}
						if (!EventName.IsEmpty())
						{
							Names.Add(MakeShared<FJsonValueString>(EventName));
						}
					}
					Out->SetArrayField(Field, Names);
				}
			}
			return Out;
		}

		FResult RunLookup(const FActionCall& Call)
		{
			const FString Query = Call.GetString(TEXT("name")).TrimStartAndEnd();
			const FString Kind = Call.GetString(TEXT("kind")).IsEmpty() ? FString(TEXT("any")) : Call.GetString(TEXT("kind"));
			const int32 Limit = FMath::Clamp(static_cast<int32>(Call.GetNumber(TEXT("limit"), 30.0)), 1, 200);
			const TSharedRef<FJsonObject> Symbols = GetSymbols();
			FResult Result;
			Result.Data = MakeShared<FJsonObject>();
			Result.Data->SetStringField(TEXT("query"), Query);

			// `Text.FontSize`, `Native.Button.Style`: the longest node type or component the query starts with, and the rest as
			// the start of a property path.
			FString TypePart = Query;
			FString PropertyPart;
			const auto WantKind = [&Kind](const TCHAR* InKind) { return Kind == TEXT("any") || Kind == InKind; };

			const TSharedPtr<FJsonObject>* Tags = nullptr;
			const TSharedPtr<FJsonObject>* Components = nullptr;
			const TSharedPtr<FJsonObject>* Enums = nullptr;
			const TSharedPtr<FJsonObject>* ViewModels = nullptr;
			Symbols->TryGetObjectField(TEXT("tags"), Tags);
			Symbols->TryGetObjectField(TEXT("components"), Components);
			Symbols->TryGetObjectField(TEXT("enums"), Enums);
			Symbols->TryGetObjectField(TEXT("viewModels"), ViewModels);
			for (const TSharedPtr<FJsonObject>* Section : { Tags, Components })
			{
				if (Section == nullptr || !Section->IsValid())
				{
					continue;
				}
				for (const auto& Pair : (*Section)->Values)
				{
					const FString Name(*Pair.Key);
					if (Query.Len() > Name.Len() + 1 && Query.StartsWith(Name + TEXT("."), ESearchCase::IgnoreCase) && Name.Len() > (PropertyPart.IsEmpty() ? 0 : TypePart.Len()))
					{
						TypePart = Query.Left(Name.Len());
						PropertyPart = Query.RightChop(Name.Len() + 1);
					}
				}
			}

			// The words every node and every @slot line can write, under the names an author asks for them by.
			if (WantKind(TEXT("node")) && (Query.Equals(TEXT("Widget"), ESearchCase::IgnoreCase) || Query.Equals(TEXT("slot"), ESearchCase::IgnoreCase) || Query.Equals(TEXT("@slot"), ESearchCase::IgnoreCase)))
			{
				const bool bSlot = !Query.Equals(TEXT("Widget"), ESearchCase::IgnoreCase);
				TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetArrayField(TEXT("properties"), Symbols->GetArrayField(bSlot ? TEXT("slotProperties") : TEXT("widgetProperties")));
				if (!bSlot)
				{
					Entry->SetArrayField(TEXT("events"), Symbols->GetArrayField(TEXT("widgetEvents")));
				}
				Result.Data->SetBoolField(TEXT("exact"), true);
				Result.Data->SetObjectField(TEXT("entry"), CondenseEntry(bSlot ? TEXT("@slot") : TEXT("Widget"), bSlot ? TEXT("slot") : TEXT("node"), Entry.ToSharedPtr(), FString()));
				Result.Summary = bSlot
					? TEXT("@slot: what a node asks of the layout container of its parent (alignment, size rule, padding).")
					: TEXT("Widget: the properties and events every node has, whatever its type (anchors, size, visibility...).");
				return Result;
			}

			// Exact: a node type, a component, an enum, a view model class -- in that order, case as written first.
			struct FSection { const TCHAR* Kind; const TCHAR* Label; const TSharedPtr<FJsonObject>* Object; };
			const FSection Sections[] = {
				{ TEXT("node"), TEXT("node type"), Tags },
				{ TEXT("component"), TEXT("component"), Components },
				{ TEXT("enum"), TEXT("enum"), Enums },
				{ TEXT("viewmodel"), TEXT("view model class"), ViewModels },
			};
			for (const ESearchCase::Type Case : { ESearchCase::CaseSensitive, ESearchCase::IgnoreCase })
			{
				for (const FSection& Section : Sections)
				{
					if (!WantKind(Section.Kind) || Section.Object == nullptr || !Section.Object->IsValid())
					{
						continue;
					}
					for (const auto& Pair : (*Section.Object)->Values)
					{
						const FString Name(*Pair.Key);
						const bool bWhole = Name.Equals(Query, Case);
						const bool bTypeWithProperty = !PropertyPart.IsEmpty() && Name.Equals(TypePart, Case);
						if (!bWhole && !bTypeWithProperty)
						{
							continue;
						}
						const TSharedPtr<FJsonObject>* EntryObject = nullptr;
						if (!Pair.Value->TryGetObject(EntryObject))
						{
							continue;
						}
						Result.Data->SetBoolField(TEXT("exact"), true);
						if (FCString::Strcmp(Section.Kind, TEXT("enum")) == 0)
						{
							TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
							Entry->SetStringField(TEXT("name"), Name);
							Entry->SetStringField(TEXT("kind"), TEXT("enum"));
							Entry->SetArrayField(TEXT("values"), (*EntryObject)->GetArrayField(TEXT("values")));
							Result.Data->SetObjectField(TEXT("entry"), Entry);
							Result.Summary = FString::Printf(TEXT("%s is an enum; a property of its type takes one of its values, written bare (no quotes)."), *Name);
							return Result;
						}
						if (FCString::Strcmp(Section.Kind, TEXT("viewmodel")) == 0)
						{
							Result.Data->SetObjectField(TEXT("entry"), *EntryObject);
							Result.Summary = FString::Printf(TEXT("%s is a view model class: a viewmodels line can name it, and bindings reach its members."), *Name);
							return Result;
						}
						const TSharedRef<FJsonObject> Condensed = CondenseEntry(Name, Section.Kind, *EntryObject, bTypeWithProperty ? PropertyPart : FString());
						Result.Data->SetObjectField(TEXT("entry"), Condensed);
						const int32 Count = Condensed->GetArrayField(TEXT("properties")).Num();
						Result.Summary = bTypeWithProperty
							? FString::Printf(TEXT("%s %s: %s matching %s."), Section.Label, *Name, *Plural(Count, TEXT("property"), TEXT("properties")), *PropertyPart)
							: FString::Printf(TEXT("%s %s: %s%s."), Section.Label, *Name, *Plural(Count, TEXT("property"), TEXT("properties")),
								FCString::Strcmp(Section.Kind, TEXT("node")) == 0 ? TEXT(", besides the ones every node has (lookup Widget) and @slot (lookup slot)") : TEXT(""));
						if (FCString::Strcmp(Section.Kind, TEXT("component")) == 0)
						{
							Result.Next.Add(FString::Printf(TEXT("Attach it inside a node with: + %s { ... }"), *Name));
						}
						return Result;
					}
				}
			}

			// Otherwise every name containing the text: node types, components, enums (and their values), view models,
			// and properties by the node types that have them.
			TArray<TSharedPtr<FJsonValue>> Matches;
			int32 Total = 0;
			const auto AddMatch = [&](const FString& InName, const TCHAR* InKind, const FString& InWhere)
			{
				++Total;
				if (Matches.Num() >= Limit)
				{
					return;
				}
				TSharedRef<FJsonObject> Match = MakeShared<FJsonObject>();
				Match->SetStringField(TEXT("name"), InName);
				Match->SetStringField(TEXT("kind"), InKind);
				if (!InWhere.IsEmpty())
				{
					Match->SetStringField(TEXT("on"), InWhere);
				}
				Matches.Add(MakeShared<FJsonValueObject>(Match));
			};
			for (const FSection& Section : Sections)
			{
				if (!WantKind(Section.Kind) || Section.Object == nullptr || !Section.Object->IsValid())
				{
					continue;
				}
				for (const auto& Pair : (*Section.Object)->Values)
				{
					const FString Name(*Pair.Key);
					if (Name.Contains(Query))
					{
						AddMatch(Name, Section.Kind, FString());
					}
					if (FCString::Strcmp(Section.Kind, TEXT("enum")) == 0)
					{
						const TSharedPtr<FJsonObject>* EnumObject = nullptr;
						const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
						if (Pair.Value->TryGetObject(EnumObject) && (*EnumObject)->TryGetArrayField(TEXT("values"), Values))
						{
							for (const TSharedPtr<FJsonValue>& Value : *Values)
							{
								if (Value->AsString().Equals(Query, ESearchCase::IgnoreCase))
								{
									AddMatch(Value->AsString(), TEXT("enum value"), Name);
								}
							}
						}
					}
				}
			}
			if (WantKind(TEXT("property")))
			{
				TMap<FString, TArray<FString>> Owners;
				const auto Scan = [&Owners, &Query](const FString& InOwner, const TArray<TSharedPtr<FJsonValue>>& InProperties)
				{
					for (const TSharedPtr<FJsonValue>& Value : InProperties)
					{
						const TSharedPtr<FJsonObject>* Property = nullptr;
						FString Name;
						if (Value->TryGetObject(Property) && (*Property)->TryGetStringField(TEXT("name"), Name) && Name.Contains(Query))
						{
							Owners.FindOrAdd(Name).AddUnique(InOwner);
						}
					}
				};
				Scan(TEXT("every node"), Symbols->GetArrayField(TEXT("widgetProperties")));
				Scan(TEXT("@slot"), Symbols->GetArrayField(TEXT("slotProperties")));
				if (Tags != nullptr && Tags->IsValid())
				{
					for (const auto& Pair : (*Tags)->Values)
					{
						const TSharedPtr<FJsonObject>* Entry = nullptr;
						const TArray<TSharedPtr<FJsonValue>>* Properties = nullptr;
						if (Pair.Value->TryGetObject(Entry) && (*Entry)->TryGetArrayField(TEXT("properties"), Properties))
						{
							Scan(FString(*Pair.Key), *Properties);
						}
					}
				}
				TArray<FString> Names;
				Owners.GetKeys(Names);
				Names.Sort();
				for (const FString& Name : Names)
				{
					TArray<FString>& On = Owners[Name];
					const FString Where = On.Num() > 6 ? FString::Join(TArrayView<FString>(On.GetData(), 6), TEXT(", ")) + FString::Printf(TEXT(" and %d more"), On.Num() - 6)
						: FString::Join(On, TEXT(", "));
					AddMatch(Name, TEXT("property"), Where);
				}
			}
			Result.Data->SetBoolField(TEXT("exact"), false);
			Result.Data->SetNumberField(TEXT("total"), Total);
			Result.Data->SetArrayField(TEXT("matches"), Matches);
			if (Total == 0)
			{
				Result.Summary = FString::Printf(TEXT("Nothing the compiler accepts is named like '%s'."), *Query);
				Result.Next.Add(TEXT("Try part of the name (Button, Font, Anchor), or kind=node to list node types only."));
			}
			else
			{
				Result.Summary = FString::Printf(TEXT("%d name(s) contain '%s'%s."), Total, *Query, Total > Matches.Num() ? *FString::Printf(TEXT("; the first %d are listed"), Matches.Num()) : TEXT(""));
				Result.Next.Add(TEXT("lookup an exact node type or component name for its properties with their types and defaults; Type.Property narrows to one."));
			}
			return Result;
		}

		// --------------------------------------------------------------------------------------------- layout

		/** The issues of one snapshot as diagnostics, after the ones found at every size are told from those found at some. */
		void ComposeLayoutIssues(const TArray<FLayoutSnapshot>& Snapshots, const TArray<TArray<FLayoutIssue>>& Issues, FResult& Result, TArray<FLayoutIssue>& OutBase)
		{
			OutBase = Issues[0];
			TSet<FString> BaseKeys;
			for (const FLayoutIssue& Issue : Issues[0])
			{
				BaseKeys.Add(Issue.Key);
			}
			for (const FLayoutIssue& Issue : Issues[0])
			{
				Result.Diagnostics.Add(MakeLayoutDiagnostic(Snapshots[0], Issue));
			}
			// DUI8006: what breaks at another size and not at the first.
			for (int32 Size = 1; Size < Snapshots.Num(); ++Size)
			{
				for (const FLayoutIssue& Issue : Issues[Size])
				{
					if (Issue.Code == static_cast<int32>(EDreamUIDiagnosticCode::LayoutNearlyAligned) || BaseKeys.Contains(Issue.Key))
					{
						continue;
					}
					FLayoutIssue Adaptive = Issue;
					Adaptive.Code = static_cast<int32>(EDreamUIDiagnosticCode::LayoutBreaksAtSize);
					Adaptive.Severity = TEXT("warning");
					Adaptive.Message = FString::Printf(TEXT("at %dx%d only: %s"), Snapshots[Size].Viewport.X, Snapshots[Size].Viewport.Y, *Issue.Message);
					Adaptive.Hint = FString::Printf(TEXT("It holds at %dx%d and breaks at %dx%d. Anchor to the edges it belongs to instead of fixed positions, and let boxes size it (Fill, Auto) instead of fixed sizes. %s"),
						Snapshots[0].Viewport.X, Snapshots[0].Viewport.Y, Snapshots[Size].Viewport.X, Snapshots[Size].Viewport.Y, *Issue.Hint);
					Adaptive.Weight = Issue.Weight + 0.1;
					Result.Diagnostics.Add(MakeLayoutDiagnostic(Snapshots[Size], Adaptive));
					OutBase.Add(Adaptive);
				}
			}
		}

		const FLayoutIssue* MostSerious(const TArray<FLayoutIssue>& Issues)
		{
			const FLayoutIssue* Best = nullptr;
			for (const FLayoutIssue& Issue : Issues)
			{
				const double Rank = (Issue.Severity == TEXT("warning") ? 10.0 : 0.0) + Issue.Weight;
				const double BestRank = Best == nullptr ? -1.0 : (Best->Severity == TEXT("warning") ? 10.0 : 0.0) + Best->Weight;
				if (Rank > BestRank)
				{
					Best = &Issue;
				}
			}
			return Best;
		}

		TUniquePtr<IJob> StartLayout(const FActionCall& Call)
		{
			FString File;
			FResult Refusal;
			if (!ResolveFile(Call, File, Refusal))
			{
				return MakeUnique<FFunctionJob>([Refusal](FJobContext&, FResult& OutResult) { OutResult = Refusal; return true; });
			}
			TArray<FIntPoint> Sizes;
			Sizes.Add(GetViewport(Call));
			const FString SizesText = Call.GetString(TEXT("sizes"));
			if (!SizesText.IsEmpty())
			{
				TArray<FString> Parts;
				SizesText.ParseIntoArray(Parts, TEXT(","), true);
				for (const FString& Part : Parts)
				{
					FIntPoint Size;
					if (!ParseSize(Part, Size))
					{
						const FResult Bad = FResult::MakeRefused(TEXT("bad-args"), FString::Printf(TEXT("layout: '%s' in sizes is not a size like 1280x720 (64 to 7680 by 64 to 4320)."), *Part.TrimStartAndEnd()),
							TEXT("Write sizes as a comma-separated list: sizes=\"1280x720, 2560x1440, 1080x1920\"."));
						return MakeUnique<FFunctionJob>([Bad](FJobContext&, FResult& OutResult) { OutResult = Bad; return true; });
					}
					Sizes.AddUnique(Size);
				}
			}
			if (Sizes.Num() > 6)
			{
				const FResult Bad = FResult::MakeRefused(TEXT("bad-args"), TEXT("layout takes at most 5 sizes besides width and height."));
				return MakeUnique<FFunctionJob>([Bad](FJobContext&, FResult& OutResult) { OutResult = Bad; return true; });
			}

			struct FState
			{
				int32 Step = 0;
				FActionCall Call;
				FString File;
				TArray<FIntPoint> Sizes;
				UDreamWidgetBlueprint* Blueprint = nullptr;
				FDreamUIAst Ast;
				FResult Result;
				TArray<FLayoutSnapshot> Snapshots;
				TArray<TArray<FLayoutIssue>> Issues;
				double StartedAt = 0.0;
			};
			TSharedRef<FState> State = MakeShared<FState>();
			State->Call = Call;
			State->File = File;
			State->Sizes = MoveTemp(Sizes);
			State->StartedAt = FPlatformTime::Seconds();
			return MakeUnique<FFunctionJob>([State](FJobContext& Context, FResult& OutResult)
			{
				if (Context.IsCancelRequested())
				{
					FScreenStage::Get().Close();
					OutResult = FResult::MakeRefused(TEXT("cancelled"), TEXT("Cancelled."));
					return true;
				}
				if (State->Step == 0)
				{
					Context.SetProgress(TEXT("compile"), 0, State->Sizes.Num() + 1);
					++State->Step;
					if (!CompileForLooking(State->File, State->Call, State->Result, State->Blueprint, State->Ast))
					{
						OutResult = State->Result;
						return true;
					}
					return false;
				}

				// One viewport size a step: the endpoint answers hello between them.
				const int32 SizeIndex = State->Step - 1;
				if (SizeIndex < State->Sizes.Num())
				{
					Context.SetProgress(FString::Printf(TEXT("layout %dx%d"), State->Sizes[SizeIndex].X, State->Sizes[SizeIndex].Y), State->Step, State->Sizes.Num() + 1);
					++State->Step;
					FScreenStage& Stage = FScreenStage::Get();
					FStageOptions Options;
					Options.Viewport = State->Sizes[SizeIndex];
					FString Error;
					if (!Stage.Open(State->Blueprint, Options, Error))
					{
						OutResult = FResult::MakeEngineError(TEXT("stage-failed"), FString::Printf(TEXT("The screen could not be put on the stage: %s."), *Error));
						return true;
					}
					SettleLayout(Stage);
					FLayoutSnapshot& Snapshot = State->Snapshots.AddDefaulted_GetRef();
					CaptureLayout(Stage, &State->Ast, State->File, Snapshot);
					Stage.Close();
					TArray<FLayoutIssue>& Issues = State->Issues.AddDefaulted_GetRef();
					LintLayout(Snapshot, Issues);
					return false;
				}

				// Everything laid out: the words.
				FResult& Result = State->Result;
				FLayoutSnapshot& Base = State->Snapshots[0];
				const FString RootArg = State->Call.GetString(TEXT("root"));
				if (!RootArg.IsEmpty())
				{
					const int32 Focus = Base.FindByPath(RootArg);
					if (Focus == INDEX_NONE)
					{
						OutResult = FResult::MakeRefused(TEXT("not-found"), FString::Printf(TEXT("No node of the screen is named %s."), *RootArg),
							FString::Printf(TEXT("Call layout without root to see every node's path (the root is %s)."), Base.Nodes.Num() > 0 ? *Base.Nodes[0].Path : TEXT("empty")));
						return true;
					}
					Base.Focus = Focus;
				}
				TArray<FLayoutIssue> AllIssues;
				ComposeLayoutIssues(State->Snapshots, State->Issues, Result, AllIssues);
				const int32 Depth = FMath::Clamp(static_cast<int32>(State->Call.GetNumber(TEXT("depth"), 6.0)), 1, 64);
				int32 Shown = 0;
				int32 Hidden = 0;
				const FString Tree = FormatLayoutTree(Base, Depth, 250, Shown, Hidden);

				int32 Warnings = 0;
				int32 Infos = 0;
				for (const FLayoutIssue& Issue : AllIssues)
				{
					(Issue.Severity == TEXT("warning") ? Warnings : Infos) += 1;
				}
				const int32 CompileWarnings = CountSeverity(Result.Diagnostics, TEXT("warning")) - Warnings;
				const FLayoutIssue* Worst = MostSerious(AllIssues);
				FString SizesNote;
				if (State->Snapshots.Num() > 1)
				{
					TArray<FString> Others;
					for (int32 Index = 1; Index < State->Snapshots.Num(); ++Index)
					{
						Others.Add(FString::Printf(TEXT("%dx%d"), State->Snapshots[Index].Viewport.X, State->Snapshots[Index].Viewport.Y));
					}
					SizesNote = FString::Printf(TEXT(" (also at %s)"), *FString::Join(Others, TEXT(", ")));
				}
				FString Headline = FString::Printf(TEXT("Laid out %s at %dx%d%s: %s, "), *Rel(State->File), Base.Viewport.X, Base.Viewport.Y, *SizesNote,
					*Plural(Base.Nodes.Num(), TEXT("node"), TEXT("nodes")));
				if (Worst == nullptr)
				{
					Headline += TEXT("no layout problems found.");
				}
				else
				{
					Headline += FString::Printf(TEXT("%s (%d warning(s), %d info); the most serious: %s."), *Plural(AllIssues.Num(), TEXT("problem"), TEXT("problems")),
						Warnings, Infos, *Worst->Message);
				}
				if (CompileWarnings > 0)
				{
					Headline += FString::Printf(TEXT(" The compile also warned %d time(s)."), CompileWarnings);
				}
				Result.Summary = Headline + TEXT("\nThe screen, one node a line (x, y from the top left of the viewport, in pixels):\n") + Tree;

				Result.Data = MakeShared<FJsonObject>();
				Result.Data->SetStringField(TEXT("file"), Rel(State->File));
				Result.Data->SetStringField(TEXT("asset"), State->Blueprint != nullptr ? State->Blueprint->GetOutermost()->GetName() : FString());
				TSharedRef<FJsonObject> Viewport = MakeShared<FJsonObject>();
				Viewport->SetNumberField(TEXT("width"), Base.Viewport.X);
				Viewport->SetNumberField(TEXT("height"), Base.Viewport.Y);
				Result.Data->SetObjectField(TEXT("viewport"), Viewport);
				Result.Data->SetNumberField(TEXT("nodes"), Base.Nodes.Num());
				Result.Data->SetNumberField(TEXT("warnings"), Warnings);
				Result.Data->SetNumberField(TEXT("infos"), Infos);
				Result.Data->SetObjectField(TEXT("tree"), LayoutTreeToJson(Base, Depth));
				if (State->Snapshots.Num() > 1)
				{
					TArray<TSharedPtr<FJsonValue>> SizeValues;
					for (int32 Index = 0; Index < State->Snapshots.Num(); ++Index)
					{
						TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
						Entry->SetStringField(TEXT("size"), FString::Printf(TEXT("%dx%d"), State->Snapshots[Index].Viewport.X, State->Snapshots[Index].Viewport.Y));
						Entry->SetNumberField(TEXT("problems"), State->Issues[Index].Num());
						SizeValues.Add(MakeShared<FJsonValueObject>(Entry));
					}
					Result.Data->SetArrayField(TEXT("sizes"), SizeValues);
				}
				Result.Data->SetNumberField(TEXT("seconds"), FMath::RoundToDouble((FPlatformTime::Seconds() - State->StartedAt) * 100.0) / 100.0);
				if (Hidden > 0)
				{
					Result.Next.Add(FString::Printf(TEXT("%d node(s) deeper than depth %d are not listed: call layout with depth=%d, or root=<path> to look inside one."), Hidden, Depth, Depth + 4));
				}
				if (Warnings > 0)
				{
					Result.Next.Add(TEXT("Fix the warnings at the lines given, then run layout again."));
				}
				if (State->Snapshots.Num() == 1)
				{
					Result.Next.Add(TEXT("sizes=\"1280x720, 2560x1440\" lays it out at more viewports and reports what breaks at only some of them."));
				}
				Result.Next.Add(TEXT("preview draws the screen as a picture and checks the contrast of its texts."));
				OutResult = Result;
				return true;
			});
		}

		// -------------------------------------------------------------------------------------------- preview

		/** `#RRGGBB` / `#RRGGBBAA` -> the colour; false when it is not one. */
		bool ParseHexColor(const FString& InText, FColor& OutColor)
		{
			FString Hex = InText.TrimStartAndEnd();
			if (!Hex.RemoveFromStart(TEXT("#")) || (Hex.Len() != 6 && Hex.Len() != 8))
			{
				return false;
			}
			for (const TCHAR Char : Hex)
			{
				if (!FChar::IsHexDigit(Char))
				{
					return false;
				}
			}
			OutColor = FColor::FromHex(Hex);
			OutColor.A = 255;
			return true;
		}

		/** A checkerboard the size of the viewport, for a backdrop that shows what is see-through. */
		UTexture2D* MakeCheckerTexture(FIntPoint InSize)
		{
			UTexture2D* Texture = UTexture2D::CreateTransient(InSize.X, InSize.Y, PF_B8G8R8A8);
			if (Texture == nullptr || Texture->GetPlatformData() == nullptr || Texture->GetPlatformData()->Mips.Num() == 0)
			{
				return nullptr;
			}
			Texture->Filter = TF_Nearest;
			Texture->SRGB = true;
			Texture->LODGroup = TEXTUREGROUP_UI;
			FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
			FColor* Pixels = static_cast<FColor*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
			for (int32 Y = 0; Y < InSize.Y; ++Y)
			{
				for (int32 X = 0; X < InSize.X; ++X)
				{
					Pixels[Y * InSize.X + X] = ((X / 16) + (Y / 16)) % 2 == 0 ? FColor(54, 54, 58, 255) : FColor(38, 38, 42, 255);
				}
			}
			Mip.BulkData.Unlock();
			Texture->UpdateResource();
			return Texture;
		}

		TUniquePtr<IJob> StartPreview(const FActionCall& Call)
		{
			FString File;
			FResult Refusal;
			if (!ResolveFile(Call, File, Refusal))
			{
				return MakeUnique<FFunctionJob>([Refusal](FJobContext&, FResult& OutResult) { OutResult = Refusal; return true; });
			}
			const FString BackgroundText = Call.GetString(TEXT("background")).IsEmpty() ? FString(TEXT("#1C1E26")) : Call.GetString(TEXT("background"));
			FColor Background(28, 30, 38, 255);
			const bool bChecker = BackgroundText.Equals(TEXT("checker"), ESearchCase::IgnoreCase);
			if (!bChecker && !ParseHexColor(BackgroundText, Background))
			{
				const FResult Bad = FResult::MakeRefused(TEXT("bad-args"), FString::Printf(TEXT("preview: background '%s' is not #RRGGBB or checker."), *BackgroundText));
				return MakeUnique<FFunctionJob>([Bad](FJobContext&, FResult& OutResult) { OutResult = Bad; return true; });
			}
			const FIntPoint Viewport = GetViewport(Call);
			const int32 Frames = FMath::Clamp(static_cast<int32>(Call.GetNumber(TEXT("frames"), 1.0)), 1, 16);
			if (Frames > 1 && Viewport.X * Frames > 8192)
			{
				const FResult Bad = FResult::MakeRefused(TEXT("strip-too-wide"), FString::Printf(TEXT("preview: %d frames of %d px side by side would be %d px wide; the most is 8192."), Frames, Viewport.X, Viewport.X * Frames),
					TEXT("Lower width (a strip of 8 frames fits at width 1024), or ask for fewer frames."));
				return MakeUnique<FFunctionJob>([Bad](FJobContext&, FResult& OutResult) { OutResult = Bad; return true; });
			}

			struct FState
			{
				enum class EPhase : uint8 { Compile, Open, Warm, Frames, Done };
				EPhase Phase = EPhase::Compile;
				FActionCall Call;
				FString File;
				FIntPoint Viewport;
				FColor Background;
				bool bChecker = false;
				int32 Frames = 1;
				double Time = 0.0;
				double Duration = 1.0;
				UDreamWidgetBlueprint* Blueprint = nullptr;
				FDreamUIAst Ast;
				FResult Result;
				TStrongObjectPtr<UTexture2D> Checker;
				UDreamWidget* Backdrop = nullptr;
				double WarmStartedAt = 0.0;
				int32 StableFrames = 0;
				TArray<FColor> LastPicture;
				int32 Frame = 0;
				TArray<TArray<FColor>> Pictures;
				TArray<FPreviewStats> PerFrame;
				TArray<double> Times;
				FLayoutSnapshot Snapshot;
				bool bWarmTimedOut = false;
				double StartedAt = 0.0;
			};
			TSharedRef<FState> State = MakeShared<FState>();
			State->Call = Call;
			State->File = File;
			State->Viewport = FIntPoint(FMath::Clamp(Viewport.X, 64, 4096), FMath::Clamp(Viewport.Y, 64, 4096));
			State->Background = Background;
			State->bChecker = bChecker;
			State->Frames = Frames;
			State->Time = FMath::Max(0.0, Call.GetNumber(TEXT("time"), 0.0));
			State->Duration = FMath::Clamp(Call.GetNumber(TEXT("duration"), 1.0), 0.01, 60.0);
			State->StartedAt = FPlatformTime::Seconds();

			return MakeUnique<FFunctionJob>([State](FJobContext& Context, FResult& OutResult)
			{
				FScreenStage& Stage = FScreenStage::Get();
				if (Context.IsCancelRequested())
				{
					Stage.Close();
					OutResult = FResult::MakeRefused(TEXT("cancelled"), TEXT("Cancelled."));
					return true;
				}
				const auto Read = [&Stage](TArray<FColor>& OutPixels)
				{
					FIntPoint Size;
					return Stage.ReadPixels(OutPixels, Size);
				};
				const auto DrawOver = [&Stage, &State](const FColor& InClear, bool bInShowBackdrop, TArray<FColor>& OutPixels)
				{
					Stage.GetCanvas()->SetRenderTargetClearColor(InClear);
					if (State->Backdrop != nullptr)
					{
						State->Backdrop->SetShown(bInShowBackdrop);
					}
					Stage.Pass(0.0f);
					FIntPoint Size;
					return Stage.ReadPixels(OutPixels, Size);
				};

				switch (State->Phase)
				{
				case FState::EPhase::Compile:
				{
					Context.SetProgress(TEXT("compile"), 0, 0);
					if (!CompileForLooking(State->File, State->Call, State->Result, State->Blueprint, State->Ast))
					{
						OutResult = State->Result;
						return true;
					}
					State->Phase = FState::EPhase::Open;
					return false;
				}
				case FState::EPhase::Open:
				{
					Context.SetProgress(TEXT("draw"), 0, 0);
					FStageOptions Options;
					Options.Viewport = State->Viewport;
					Options.bDraw = true;
					Options.Backdrop = State->bChecker ? FColor(46, 46, 50, 255) : State->Background;
					FString Error;
					if (!Stage.Open(State->Blueprint, Options, Error))
					{
						OutResult = FResult::MakeEngineError(TEXT("stage-failed"), FString::Printf(TEXT("The screen could not be put on the stage: %s."), *Error));
						return true;
					}
					if (State->bChecker)
					{
						// Behind the screen: the first child of the stage's root, which the screen draws over.
						State->Checker.Reset(MakeCheckerTexture(State->Viewport));
						UDreamWidget* Backdrop = NewObject<UDreamWidget>(Stage.GetWorld(), NAME_None, RF_Transient);
						Backdrop->SetDisplayName(TEXT("DreamMCPBackdrop"));
						Backdrop->SetWidth(static_cast<float>(State->Viewport.X));
						Backdrop->SetHeight(static_cast<float>(State->Viewport.Y));
						Backdrop->OnRegister();
						Backdrop->TrySetParent(Stage.GetRoot(), false);
						Backdrop->SetAsFirstSibling();
						Backdrop->SetAnchoredPosition(FVector2D::ZeroVector);
						if (UDreamTexture* Visual = Backdrop->CreateNewVisual<UDreamTexture>())
						{
							Visual->SetTexture(State->Checker.Get());
						}
						State->Backdrop = Backdrop;
					}
					SettleLayout(Stage);
					State->WarmStartedAt = FPlatformTime::Seconds();
					State->Phase = FState::EPhase::Warm;
					return false;
				}
				case FState::EPhase::Warm:
				{
					// Until the picture holds still: a material's shaders compile the first time anything draws with it, and
					// a glyph rasterises on a worker. Three equal pictures running, with nothing compiling, is still.
					Stage.Pass(0.0f);
					TArray<FColor> Picture;
					if (!Read(Picture))
					{
						Stage.Close();
						OutResult = FResult::MakeEngineError(TEXT("readback-failed"), TEXT("The picture could not be read back from the render target."));
						return true;
					}
					const bool bCompiling = GShaderCompilingManager != nullptr && GShaderCompilingManager->IsCompiling();
					const bool bSame = State->LastPicture.Num() == Picture.Num() && MeanFrameDifference(State->LastPicture, Picture) <= 0.0;
					State->StableFrames = (bSame && !bCompiling && Stage.CountPendingGlyphs(false) == 0) ? State->StableFrames + 1 : 0;
					State->LastPicture = MoveTemp(Picture);
					const double Waited = FPlatformTime::Seconds() - State->WarmStartedAt;
					if (State->StableFrames < 3 && Waited < 120.0)
					{
						Context.SetProgress(bCompiling ? TEXT("waiting for shaders") : TEXT("waiting for the picture to settle"), 0, 0);
						Context.SetWaiting();
						return false;
					}
					State->bWarmTimedOut = State->StableFrames < 3;
					CaptureLayout(Stage, &State->Ast, State->File, State->Snapshot);
					// The screen's own time, up to `time`: whatever animates by itself moves meanwhile.
					for (double Elapsed = 0.0; Elapsed + 1e-6 < State->Time;)
					{
						const double Step = FMath::Min(1.0 / 30.0, State->Time - Elapsed);
						Stage.Pass(static_cast<float>(Step));
						Elapsed += Step;
					}
					State->Phase = FState::EPhase::Frames;
					return false;
				}
				case FState::EPhase::Frames:
				{
					Context.SetProgress(TEXT("frames"), State->Frame, State->Frames);
					if (State->Frame > 0)
					{
						const double Target = State->Duration * State->Frame / State->Frames;
						for (double Elapsed = State->Duration * (State->Frame - 1) / State->Frames; Elapsed + 1e-6 < Target;)
						{
							const double Step = FMath::Min(1.0 / 30.0, Target - Elapsed);
							Stage.Pass(static_cast<float>(Step));
							Elapsed += Step;
						}
					}
					const FColor Clear = State->bChecker ? FColor(46, 46, 50, 255) : State->Background;
					TArray<FColor> Picture;
					TArray<FColor> OverBlack;
					TArray<FColor> OverWhite;
					if (!DrawOver(Clear, true, Picture) || !DrawOver(FColor::Black, false, OverBlack) || !DrawOver(FColor::White, false, OverWhite))
					{
						Stage.Close();
						OutResult = FResult::MakeEngineError(TEXT("readback-failed"), TEXT("The picture could not be read back from the render target."));
						return true;
					}
					// The backdrop back for the next frame; it is drawn when the next frame is.
					Stage.GetCanvas()->SetRenderTargetClearColor(Clear);
					if (State->Backdrop != nullptr)
					{
						State->Backdrop->SetShown(true);
					}
					FPreviewStats& Stats = State->PerFrame.AddDefaulted_GetRef();
					Stats.Add(Picture, OverBlack, OverWhite);
					for (FColor& Pixel : Picture)
					{
						Pixel.A = 255;
					}
					State->Pictures.Add(MoveTemp(Picture));
					State->Times.Add(State->Time + State->Duration * State->Frame / State->Frames);
					if (++State->Frame < State->Frames)
					{
						return false;
					}
					Stage.Close();
					State->Phase = FState::EPhase::Done;
					return false;
				}
				case FState::EPhase::Done:
				default:
					break;
				}

				// The picture: one frame, or the frames side by side.
				FResult& Result = State->Result;
				const int32 W = State->Viewport.X;
				const int32 H = State->Viewport.Y;
				const int32 N = State->Pictures.Num();
				TArray<FColor> Image;
				Image.SetNumUninitialized(W * N * H);
				for (int32 Index = 0; Index < N; ++Index)
				{
					for (int32 Y = 0; Y < H; ++Y)
					{
						FMemory::Memcpy(&Image[Y * W * N + Index * W], &State->Pictures[Index][Y * W], W * sizeof(FColor));
					}
				}
				TArray64<uint8> Png;
				FImageUtils::PNGCompressImageArray(W * N, H, TArrayView64<const FColor>(Image.GetData(), Image.Num()), Png);
				const FString Directory = FEndpoint::GetArtifactDirectory();
				IFileManager::Get().MakeDirectory(*Directory, true);
				const FString ImagePath = FPaths::Combine(Directory, Context.GetJobId() + TEXT(".png"));
				if (!FFileHelper::SaveArrayToFile(Png, *ImagePath))
				{
					OutResult = FResult::MakeEngineError(TEXT("image-not-written"), FString::Printf(TEXT("The picture could not be written to %s."), *Rel(ImagePath)));
					return true;
				}

				// Contrast, on the first frame: each text against the pixels around it.
				TArray<FLayoutIssue> Contrast;
				LintContrast(State->Snapshot, State->Pictures[0], State->Viewport, Contrast);
				for (const FLayoutIssue& Issue : Contrast)
				{
					Result.Diagnostics.Add(MakeLayoutDiagnostic(State->Snapshot, Issue));
				}

				FPreviewStats Overall;
				for (int32 Index = 0; Index < N; ++Index)
				{
					// Recomputed over the frames together: the sums of each are what the overall figures are made of.
					Overall.Pixels += State->PerFrame[Index].Pixels;
					Overall.CoveredPixels += State->PerFrame[Index].CoveredPixels;
					Overall.PartialPixels += State->PerFrame[Index].PartialPixels;
					Overall.NearWhitePixels += State->PerFrame[Index].NearWhitePixels;
					Overall.NearBlackPixels += State->PerFrame[Index].NearBlackPixels;
					for (int32 Channel = 0; Channel < 3; ++Channel)
					{
						Overall.Sum[Channel] += State->PerFrame[Index].Sum[Channel];
						Overall.SumSquares[Channel] += State->PerFrame[Index].SumSquares[Channel];
					}
					Overall.LumaSum += State->PerFrame[Index].LumaSum;
					Overall.LumaSumSquares += State->PerFrame[Index].LumaSumSquares;
					Overall.LumaMin = FMath::Min(Overall.LumaMin, State->PerFrame[Index].LumaMin);
					Overall.LumaMax = FMath::Max(Overall.LumaMax, State->PerFrame[Index].LumaMax);
				}
				TArray<FString> Next;
				FString Sentence;
				TSharedPtr<FJsonObject> Animation;
				if (N > 1)
				{
					const FPreviewAnimation Analysis = AnalysePreviewAnimation(State->Pictures, State->PerFrame);
					Sentence = DescribePreview(State->PerFrame[0], Next) + TEXT(". ") + DescribePreviewAnimation(Analysis, State->PerFrame, State->Times, State->Duration, Next);
					Animation = PreviewAnimationToJson(Analysis, State->PerFrame, State->Times, State->Duration);
				}
				else
				{
					Sentence = DescribePreview(Overall, Next);
				}
				if (Contrast.Num() > 0)
				{
					Sentence += FString::Printf(TEXT(". %s hard to read against what is behind %s"),
						*Plural(Contrast.Num(), TEXT("text is"), TEXT("texts are")), Contrast.Num() == 1 ? TEXT("it") : TEXT("them"));
				}
				Result.Summary = FString::Printf(TEXT("%s. Image: %s (%dx%d%s)."), *Sentence, *Rel(ImagePath), W, H,
					N > 1 ? *FString::Printf(TEXT(", %d frames side by side"), N) : TEXT(""));
				FArtifact Artifact;
				Artifact.Kind = TEXT("image");
				Artifact.Path = Rel(ImagePath);
				Artifact.Title = FString::Printf(TEXT("%s at %dx%d"), *Rel(State->File), W, H);
				Result.Artifacts.Add(Artifact);
				Result.Next.Append(Next);
				if (State->bWarmTimedOut)
				{
					Result.Next.Add(TEXT("The picture kept changing for 2 minutes (shaders still compiling, or something animates by itself); run preview again to see it settled."));
				}
				// The layout rules on the same screen, counted: preview reports contrast, and points at layout for the rest.
				TArray<FLayoutIssue> LayoutIssues;
				LintLayout(State->Snapshot, LayoutIssues);
				int32 LayoutWarnings = 0;
				for (const FLayoutIssue& Issue : LayoutIssues)
				{
					LayoutWarnings += Issue.Severity == TEXT("warning") ? 1 : 0;
				}
				Result.Next.Add(LayoutWarnings > 0
					? FString::Printf(TEXT("layout finds %s on this screen at %dx%d (off the screen, cut text, small or stacked controls): run it for each one's node and line."),
						*Plural(LayoutWarnings, TEXT("problem"), TEXT("problems")), W, H)
					: FString(TEXT("layout lists where every node is and what does not fit; it reads the same screen without drawing it.")));
				Result.Data = MakeShared<FJsonObject>();
				Result.Data->SetStringField(TEXT("file"), Rel(State->File));
				Result.Data->SetNumberField(TEXT("layoutWarnings"), LayoutWarnings);
				Result.Data->SetNumberField(TEXT("width"), W);
				Result.Data->SetNumberField(TEXT("height"), H);
				Result.Data->SetStringField(TEXT("background"), State->bChecker ? FString(TEXT("checker")) : FormatHexColor(FLinearColor(State->Background.R / 255.0f, State->Background.G / 255.0f, State->Background.B / 255.0f)));
				Result.Data->SetObjectField(TEXT("stats"), (N > 1 ? State->PerFrame[0] : Overall).ToJson());
				if (Animation.IsValid())
				{
					Result.Data->SetObjectField(TEXT("animation"), Animation);
				}
				Result.Data->SetNumberField(TEXT("lowContrastTexts"), Contrast.Num());
				Result.Data->SetNumberField(TEXT("seconds"), FMath::RoundToDouble((FPlatformTime::Seconds() - State->StartedAt) * 100.0) / 100.0);
				OutResult = Result;
				return true;
			});
		}

		// ----------------------------------------------------------------------------------------------- info

		/** The plugin's commit, read the way git stores it; `unknown` when it is not a checkout. */
		FString ReadPluginCommit(const FString& InPluginDir)
		{
			FString GitDir = FPaths::Combine(InPluginDir, TEXT(".git"));
			FString Pointer;
			if (FFileHelper::LoadFileToString(Pointer, *GitDir) && Pointer.StartsWith(TEXT("gitdir:")))
			{
				GitDir = Pointer.RightChop(7).TrimStartAndEnd();
				if (FPaths::IsRelative(GitDir))
				{
					GitDir = FPaths::Combine(InPluginDir, GitDir);
				}
			}
			FString Head;
			if (!FFileHelper::LoadFileToString(Head, *FPaths::Combine(GitDir, TEXT("HEAD"))))
			{
				return TEXT("unknown");
			}
			Head.TrimStartAndEndInline();
			if (!Head.StartsWith(TEXT("ref:")))
			{
				return Head.Left(9);
			}
			const FString Ref = Head.RightChop(4).TrimStartAndEnd();
			FString CommonDir = GitDir;
			FString CommonPointer;
			if (FFileHelper::LoadFileToString(CommonPointer, *FPaths::Combine(GitDir, TEXT("commondir"))))
			{
				CommonPointer.TrimStartAndEndInline();
				CommonDir = FPaths::IsRelative(CommonPointer) ? FPaths::Combine(GitDir, CommonPointer) : CommonPointer;
			}
			FString Commit;
			if (FFileHelper::LoadFileToString(Commit, *FPaths::Combine(CommonDir, Ref)) || FFileHelper::LoadFileToString(Commit, *FPaths::Combine(GitDir, Ref)))
			{
				return Commit.TrimStartAndEnd().Left(9);
			}
			FString Packed;
			if (FFileHelper::LoadFileToString(Packed, *FPaths::Combine(CommonDir, TEXT("packed-refs"))))
			{
				TArray<FString> Lines;
				Packed.ParseIntoArrayLines(Lines);
				for (const FString& Line : Lines)
				{
					if (Line.EndsWith(TEXT(" ") + Ref))
					{
						return Line.Left(9);
					}
				}
			}
			return TEXT("unknown");
		}

		FResult RunInfo(const FActionCall& Call)
		{
			FResult Result;
			Result.Data = MakeShared<FJsonObject>();
			FString Version = TEXT("unknown");
			FString Commit = TEXT("unknown");
			if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("DreamGUI")))
			{
				Version = Plugin->GetDescriptor().VersionName;
				Commit = ReadPluginCommit(Plugin->GetBaseDir());
			}
			const FString Engine = FEngineVersion::Current().ToString(EVersionComponent::Patch);
			const FString EngineKind = FApp::IsEngineInstalled() ? TEXT("launcher (installed)") : TEXT("source build");
			const FString Os = FPlatformMisc::GetOSVersion();
			const FString Locale = FInternationalization::Get().GetCurrentCulture()->GetName();
			const bool bHost = Call.Mode == EEndpointMode::Host;
			Result.Data->SetStringField(TEXT("pluginVersion"), Version);
			Result.Data->SetStringField(TEXT("pluginCommit"), Commit);
			Result.Data->SetStringField(TEXT("engineVersion"), Engine);
			Result.Data->SetStringField(TEXT("engineKind"), EngineKind);
			Result.Data->SetStringField(TEXT("os"), Os);
			Result.Data->SetStringField(TEXT("locale"), Locale);
			Result.Data->SetStringField(TEXT("mode"), bHost ? TEXT("host") : TEXT("editor"));
			Result.Data->SetBoolField(TEXT("render"), Call.bCanRender);
			Result.Data->SetBoolField(TEXT("commandlet"), IsRunningCommandlet());
			Result.Data->SetBoolField(TEXT("layoutWithoutRender"), true);
			Result.Data->SetBoolField(TEXT("editorEndpointEnabled"), IsDreamGUIMCPEndpointEnabled());
			TArray<TSharedPtr<FJsonValue>> Roots;
			for (const FDreamUISourceRoot& Root : DreamUIPaths::GetSourceRoots())
			{
				TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetStringField(TEXT("folder"), Rel(Root.Directory));
				if (!Root.RootToken.IsEmpty())
				{
					Entry->SetStringField(TEXT("spelling"), Root.RootToken + TEXT(":"));
				}
				Roots.Add(MakeShared<FJsonValueObject>(Entry));
			}
			Result.Data->SetArrayField(TEXT("sourceRoots"), Roots);
			Result.Data->SetStringField(TEXT("widgets"), bHost
				? TEXT("A compile saves every widget Blueprint it writes to disk.")
				: TEXT("A compile leaves the widget Blueprints in memory in the user's editor, for the user to save."));
			Result.Summary = FString::Printf(TEXT("DreamGUI %s (%s) on Unreal Engine %s (%s), %s, in %s%s."), *Version, *Commit, *Engine, *EngineKind, *Os,
				bHost ? TEXT("a host DreamMCP started") : TEXT("the user's editor"), Call.bCanRender ? TEXT(" that can draw previews") : TEXT(" that cannot draw (layout still works)"));
			return Result;
		}
	}

	void RegisterDreamGUIMCPActions(FEndpoint& Endpoint)
	{
		FActionHandler Check;
		Check.Start = &StartCheck;
		Endpoint.RegisterAction(TEXT("check"), MoveTemp(Check));

		FActionHandler Compile;
		Compile.Start = &StartCompile;
		Endpoint.RegisterAction(TEXT("compile"), MoveTemp(Compile));

		FActionHandler Diagnostics;
		Diagnostics.Run = &RunDiagnostics;
		Endpoint.RegisterAction(TEXT("diagnostics"), MoveTemp(Diagnostics));

		FActionHandler Lookup;
		Lookup.Run = &RunLookup;
		Endpoint.RegisterAction(TEXT("lookup"), MoveTemp(Lookup));

		FActionHandler Layout;
		Layout.Start = &StartLayout;
		Endpoint.RegisterAction(TEXT("layout"), MoveTemp(Layout));

		FActionHandler Preview;
		Preview.Start = &StartPreview;
		Endpoint.RegisterAction(TEXT("preview"), MoveTemp(Preview));

		FActionHandler Info;
		Info.Run = &RunInfo;
		Endpoint.RegisterAction(TEXT("info"), MoveTemp(Info));
	}

	void ResetDreamGUIMCPActionState()
	{
		GetCheckedFiles().Reset();
		ResetCompileState();
		FScreenStage::Shutdown();
	}
}
