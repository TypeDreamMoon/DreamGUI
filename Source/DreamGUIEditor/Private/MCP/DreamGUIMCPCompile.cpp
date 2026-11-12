// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "MCP/DreamGUIMCPCompile.h"

#include "MCP/DreamGUIMCPLog.h"

#include "Core/DreamTextUserWidget.h"
#include "Core/DreamWidgetEachBinding.h"
#include "Core/DreamWidgetPropertyBinding.h"
#include "Core/DreamWidgetTree.h"
#include "DataFactory/DreamWidgetBlueprintFactory.h"
#include "Designer/DreamUITextAuthoringGate.h"
#include "DreamWidgetBlueprint.h"
#include "Text/DreamUIAst.h"
#include "Text/DreamUIDiagnostics.h"
#include "Text/DreamUIDiagnosticsMailbox.h"
#include "Text/DreamUIPaths.h"
#include "Text/DreamUISourceFile.h"
#include "Text/DreamUISourceWatcher.h"
#include "Text/DreamUITextBuilder.h"

#include "AssetToolsModule.h"
#include "FileHelpers.h"
#include "HAL/FileManager.h"
#include "IAssetTools.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Logging/TokenizedMessage.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

namespace DreamGUIMCP
{
	namespace
	{
		/** Asset path -> what its source and imports were, the last time this process compiled it. */
		TMap<FString, FString> GLastCompiledHash;

		FString NormalizeFile(const FString& InPath)
		{
			FString Path = FPaths::ConvertRelativePathToFull(InPath);
			FPaths::NormalizeFilename(Path);
			FPaths::CollapseRelativeDirectories(Path);
			return Path;
		}

		bool ReadSource(const FString& InAbsoluteFile, FString& OutText, FString& OutReason, FString& OutSummary)
		{
			const FString Relative = MakeProjectRelativePath(InAbsoluteFile);
			if (!InAbsoluteFile.EndsWith(DreamUIPaths::SourceExtension, ESearchCase::IgnoreCase))
			{
				OutReason = TEXT("not-a-dui-file");
				OutSummary = FString::Printf(TEXT("%s is not a .dui file."), *Relative); // I18N-EXEMPT: wire text
				return false;
			}
			if (!IFileManager::Get().FileExists(*InAbsoluteFile))
			{
				OutReason = TEXT("not-found");
				OutSummary = FString::Printf(TEXT("There is no file %s."), *Relative); // I18N-EXEMPT: wire text
				return false;
			}
			if (!FFileHelper::LoadFileToString(OutText, *InAbsoluteFile))
			{
				OutReason = TEXT("unreadable");
				OutSummary = FString::Printf(TEXT("%s could not be read."), *Relative); // I18N-EXEMPT: wire text
				return false;
			}
			return true;
		}

		/** The text of the file and of every file it imports, hashed: what decides whether a compile is due. */
		FString HashSources(const FString& InText, const FDreamUIAst& InAst)
		{
			FSHA1 Sha;
			const auto Add = [&Sha](const FString& Text)
			{
				const FTCHARToUTF8 Utf8(*Text);
				Sha.Update(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
			};
			Add(InText);
			for (const FString& Import : InAst.Imports)
			{
				FString ImportText;
				FFileHelper::LoadFileToString(ImportText, *Import);
				Add(Import);
				Add(ImportText);
			}
			Sha.Final();
			uint8 Digest[FSHA1::DigestSize];
			Sha.GetHash(Digest);
			return BytesToHex(Digest, FSHA1::DigestSize);
		}

		/** `/Game/UI/WBP_X.WBP_X` or `/Game/UI/WBP_X` -> `/Game/UI/WBP_X`. */
		FString PackageOf(const FString& InClassPath)
		{
			FString Package = InClassPath.TrimStartAndEnd();
			int32 Dot = INDEX_NONE;
			if (Package.FindLastChar(TEXT('.'), Dot))
			{
				Package.LeftInline(Dot);
			}
			return Package;
		}

		void AppendBag(TArray<FDiagnostic>& Out, const FDreamUIDiagnosticBag& InBag, const FString& InFile)
		{
			AppendDiagnostics(Out, InBag, InFile);
		}

		/** The Kismet compiler's own errors -- not the .dui's, which the mailbox carries -- as diagnostics with no code. */
		void AppendKismetErrors(TArray<FDiagnostic>& Out, const FCompilerResultsLog& InResults, const FString& InFile, const TArray<FDiagnostic>& InTextDiagnostics)
		{
			for (const TSharedRef<FTokenizedMessage>& Message : InResults.Messages)
			{
				if (Message->GetSeverity() != EMessageSeverity::Error)
				{
					continue;
				}
				const FString Text = ScrubAbsolutePaths(Message->ToText().ToString());
				// A text diagnostic is reported into the log as well (ReportTextDiagnostics); it is already listed.
				const bool bAlreadyListed = InTextDiagnostics.ContainsByPredicate([&Text](const FDiagnostic& Known)
				{
					return !Known.Message.IsEmpty() && Text.Contains(Known.Message);
				});
				if (bAlreadyListed)
				{
					continue;
				}
				FDiagnostic Diagnostic;
				Diagnostic.Severity = TEXT("error");
				Diagnostic.File = MakeProjectRelativePath(InFile);
				Diagnostic.Message = Text;
				Diagnostic.Stage = TEXT("compile");
				Out.Add(MoveTemp(Diagnostic));
			}
		}

		/** The component sources a file places, resolved: its own `use "X.dui" as X` lines and those its libraries bring. */
		void CollectComponentSources(const FDreamUIAst& InAst, TArray<FString>& OutSources)
		{
			const auto Collect = [&OutSources](const TArray<FDreamUIComponentAlias>& InAliases)
			{
				for (const FDreamUIComponentAlias& Alias : InAliases)
				{
					if (!Alias.SourcePath.IsEmpty())
					{
						OutSources.AddUnique(NormalizeFile(Alias.SourcePath));
					}
				}
			};
			Collect(InAst.ComponentAliases);
			Collect(InAst.ImportedComponentAliases);
		}

		UDreamWidgetBlueprint* FindBlueprintOfSource(const FString& InAbsoluteFile)
		{
			if (UClass* Class = FDreamUISourceWatcher::FindClassForSource(InAbsoluteFile))
			{
				return Cast<UDreamWidgetBlueprint>(Class->ClassGeneratedBy);
			}
			return nullptr;
		}

		struct FCompileContext
		{
			bool bSave = false;
			bool bForce = false;
			TArray<FString> Stack;
			TSet<FString> Done;
			FCompileOutcome* Outcome = nullptr;
		};

		/** One file, its components first. False when it, or a component it needs, did not compile. */
		bool CompileOne(const FString& InFile, FCompileContext& Context, UDreamWidgetBlueprint** OutBlueprint)
		{
			FCompileOutcome& Outcome = *Context.Outcome;
			const FString Relative = MakeProjectRelativePath(InFile);

			FString Text;
			FString Reason;
			FString Summary;
			if (!ReadSource(InFile, Text, Reason, Summary))
			{
				if (Context.Stack.Num() == 0)
				{
					Outcome.RefusalReason = Reason;
					Outcome.RefusalSummary = Summary;
					Outcome.RefusalNext = TEXT("Give file as a path relative to the project root, for example DUI/Menu.dui.");
				}
				else
				{
					FDiagnostic Missing;
					Missing.Severity = TEXT("error");
					Missing.File = MakeProjectRelativePath(Context.Stack.Last());
					Missing.Message = FString::Printf(TEXT("the component %s cannot be compiled: %s"), *Relative, *Summary); // I18N-EXEMPT: wire text
					Missing.Stage = TEXT("compile");
					Outcome.Diagnostics.Add(MoveTemp(Missing));
				}
				return false;
			}

			FDreamUIAst Ast;
			FDreamUIDiagnosticBag ParseBag;
			ParseBag.SourceName = InFile;
			const bool bParsed = FDreamUISourceFile::Parse(Text, InFile, Ast, ParseBag, FDreamUISourceFile::MakeFileImportReader());
			if (!bParsed)
			{
				// What the compiler would say first, without touching the Blueprint: a file that does not parse builds nothing.
				AppendBag(Outcome.Diagnostics, ParseBag, InFile);
				FCompiledAsset& Failed = Outcome.Assets.AddDefaulted_GetRef();
				Failed.SourceFile = Relative;
				Failed.AssetPath = PackageOf(Ast.ClassPath);
				Failed.bFailed = true;
				return false;
			}

			// The components this file places, each before it: a screen compiled ahead of its row finds no row class.
			Context.Stack.Push(InFile);
			TArray<FString> Components;
			CollectComponentSources(Ast, Components);
			bool bComponentsOk = true;
			for (const FString& Component : Components)
			{
				if (Context.Done.Contains(Component))
				{
					continue;
				}
				if (Context.Stack.Contains(Component))
				{
					continue; // the parser reports the cycle itself (ImportFailed); nothing to order
				}
				Context.Done.Add(Component);
				bComponentsOk &= CompileOne(Component, Context, nullptr);
			}
			Context.Stack.Pop();

			// The asset: the `class` line's, else whichever Blueprint already names this file.
			UDreamWidgetBlueprint* Blueprint = nullptr;
			FString AssetPath = PackageOf(Ast.ClassPath);
			bool bCreated = false;
			if (AssetPath.IsEmpty())
			{
				Blueprint = FindBlueprintOfSource(InFile);
				if (Blueprint == nullptr)
				{
					if (Context.Stack.Num() == 0)
					{
						Outcome.RefusalReason = TEXT("no-class-line");
						Outcome.RefusalSummary = FString::Printf(TEXT("%s has no class line, so it does not say which widget Blueprint to build."), *Relative); // I18N-EXEMPT: wire text
						Outcome.RefusalNext = TEXT("Add a first line such as: class /Game/UI/WBP_Menu");
					}
					return false;
				}
				AssetPath = Blueprint->GetOutermost()->GetName();
			}
			else if (AssetPath.StartsWith(TEXT("/Script/")))
			{
				if (Context.Stack.Num() == 0)
				{
					Outcome.RefusalReason = TEXT("native-class");
					Outcome.RefusalSummary = FString::Printf(TEXT("%s is the source of the C++ class %s; only a widget Blueprint can be built from a file here."), *Relative, *Ast.ClassPath); // I18N-EXEMPT: wire text
					Outcome.RefusalNext = TEXT("Write class /Game/<Folder>/WBP_<Name> for a Blueprint of your own.");
				}
				return false;
			}
			else
			{
				FText InvalidReason;
				if (!FPackageName::IsValidLongPackageName(AssetPath, /*bIncludeReadOnlyRoots*/ false, &InvalidReason))
				{
					if (Context.Stack.Num() == 0)
					{
						Outcome.RefusalReason = TEXT("bad-class-path");
						Outcome.RefusalSummary = FString::Printf(TEXT("The class line of %s names %s, which is not an asset path: %s"), *Relative, *Ast.ClassPath, *InvalidReason.ToString()); // I18N-EXEMPT: wire text
						Outcome.RefusalNext = TEXT("Write it as class /Game/<Folder>/WBP_<Name>.");
					}
					return false;
				}
				const FString AssetName = FPackageName::GetLongPackageAssetName(AssetPath);
				if (FPackageName::DoesPackageExist(AssetPath) || FindPackage(nullptr, *AssetPath) != nullptr)
				{
					UObject* Existing = StaticLoadObject(UObject::StaticClass(), nullptr, *(AssetPath + TEXT(".") + AssetName));
					Blueprint = Cast<UDreamWidgetBlueprint>(Existing);
					if (Blueprint == nullptr)
					{
						if (Context.Stack.Num() == 0)
						{
							Outcome.RefusalReason = TEXT("not-a-dream-widget-blueprint");
							Outcome.RefusalSummary = FString::Printf(TEXT("%s already exists and is not a DreamGUI widget Blueprint (%s)."), *AssetPath, *GetNameSafe(Existing ? Existing->GetClass() : nullptr)); // I18N-EXEMPT: wire text
							Outcome.RefusalNext = TEXT("Pick another path in the class line.");
						}
						return false;
					}
				}
				else
				{
					UDreamWidgetBlueprintFactory* Factory = NewObject<UDreamWidgetBlueprintFactory>();
					Factory->ParentClass = UDreamTextUserWidget::StaticClass();
					IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
					Blueprint = Cast<UDreamWidgetBlueprint>(AssetTools.CreateAsset(AssetName, FPackageName::GetLongPackagePath(AssetPath),
						UDreamWidgetBlueprint::StaticClass(), Factory));
					bCreated = Blueprint != nullptr;
					if (Blueprint == nullptr)
					{
						if (Context.Stack.Num() == 0)
						{
							Outcome.RefusalReason = TEXT("create-failed");
							Outcome.RefusalSummary = FString::Printf(TEXT("The widget Blueprint %s could not be created."), *AssetPath); // I18N-EXEMPT: wire text
						}
						return false;
					}
				}
			}

			if (!DreamUITextAuthoring::CanAuthorFromText(Blueprint))
			{
				if (Context.Stack.Num() == 0)
				{
					Outcome.RefusalReason = TEXT("not-text-authorable");
					Outcome.RefusalSummary = FString::Printf(TEXT("%s derives from %s, which cannot be built from a .dui (it needs UDreamTextUserWidget as a parent)."), // I18N-EXEMPT: wire text
						*AssetPath, *GetNameSafe(Blueprint->ParentClass));
					Outcome.RefusalNext = TEXT("Point the class line at a new asset path; compile creates the Blueprint with the right parent.");
				}
				return false;
			}

			// The file the Blueprint reads, spelled the way a Blueprint stores it (relative to a DUI/ root).
			const FString Portable = DreamUIPaths::MakePortablePath(InFile);
			if (!FPaths::IsSamePath(DreamUIPaths::Resolve(DreamUITextAuthoring::GetAuthoredSourcePath(Blueprint)), InFile))
			{
				DreamUITextAuthoring::SetAuthoredSourcePath(Blueprint, Portable);
			}

			FCompiledAsset& Asset = Outcome.Assets.AddDefaulted_GetRef();
			const int32 AssetIndex = Outcome.Assets.Num() - 1;
			Asset.SourceFile = Relative;
			Asset.AssetPath = AssetPath;
			Asset.bCreated = bCreated;

			const FString Hash = HashSources(Text, Ast);
			const FString* LastHash = GLastCompiledHash.Find(AssetPath);
			const bool bUpToDate = !Context.bForce && !bCreated && LastHash != nullptr && *LastHash == Hash
				&& Blueprint->Status == BS_UpToDate && Blueprint->GeneratedClass != nullptr;

			TArray<FDiagnostic> FileDiagnostics;
			bool bOk = true;
			if (bUpToDate)
			{
				Outcome.Assets[AssetIndex].bUnchanged = true;
			}
			else
			{
				FCompilerResultsLog Results;
				Results.bSilentMode = true;
				FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection, &Results);
				bOk = Results.NumErrors == 0 && Blueprint->Status != BS_Error;
				FDreamUIDiagnosticsMailboxEntry Entry;
				if (FDreamUIDiagnosticsMailbox::FindEntry(InFile, Entry))
				{
					for (const FDreamUIDiagnostic& Diagnostic : Entry.Diagnostics)
					{
						FileDiagnostics.Add(MakeDiagnostic(Diagnostic, InFile));
					}
				}
				AppendKismetErrors(FileDiagnostics, Results, InFile, FileDiagnostics);
				if (bOk)
				{
					GLastCompiledHash.Add(AssetPath, Hash);
				}
				else
				{
					GLastCompiledHash.Remove(AssetPath);
				}
			}
			if (bUpToDate)
			{
				// The diagnostics of the compile it is still the result of.
				FDreamUIDiagnosticsMailboxEntry Entry;
				if (FDreamUIDiagnosticsMailbox::FindEntry(InFile, Entry))
				{
					for (const FDreamUIDiagnostic& Diagnostic : Entry.Diagnostics)
					{
						FileDiagnostics.Add(MakeDiagnostic(Diagnostic, InFile));
					}
				}
			}

			if (bOk && Context.bSave && !bUpToDate)
			{
				if (UEditorLoadingAndSavingUtils::SavePackages({ Blueprint->GetOutermost() }, /*bOnlyDirty*/ false))
				{
					Outcome.Assets[AssetIndex].bSaved = true;
				}
				else
				{
					FDiagnostic NotSaved;
					NotSaved.Severity = TEXT("error");
					NotSaved.File = Relative;
					NotSaved.Message = FString::Printf(TEXT("%s compiled, but its package could not be saved"), *AssetPath); // I18N-EXEMPT: wire text
					NotSaved.Stage = TEXT("compile");
					FileDiagnostics.Add(MoveTemp(NotSaved));
					bOk = false;
				}
			}
			Outcome.Assets[AssetIndex].bFailed = !bOk;
			Outcome.Diagnostics.Append(FileDiagnostics);
			if (OutBlueprint != nullptr)
			{
				*OutBlueprint = Blueprint;
			}
			UE_LOG(LogDreamGUIMCP, Verbose, TEXT("DreamMCP compile: %s -> %s %s"), *Relative, *AssetPath,
				bUpToDate ? TEXT("(unchanged)") : (bOk ? TEXT("compiled") : TEXT("FAILED")));
			return bOk && bComponentsOk;
		}
	}

	FCheckOutcome CheckSource(const FString& InAbsoluteFile)
	{
		FCheckOutcome Outcome;
		FString Text;
		if (!ReadSource(InAbsoluteFile, Text, Outcome.RefusalReason, Outcome.RefusalSummary))
		{
			return Outcome;
		}
		Outcome.bRead = true;

		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Bag;
		Bag.SourceName = InAbsoluteFile;
		// The reader the compiler parses with: a `use` of a file reads it, as a compile would.
		const bool bParsed = FDreamUISourceFile::Parse(Text, InAbsoluteFile, Ast, Bag, FDreamUISourceFile::MakeFileImportReader());
		Outcome.AssetPath = PackageOf(Ast.ClassPath);
		if (bParsed)
		{
			// Built as the write-back builds its reference tree: into the transient package, with the loop sink the real
			// compile gives (a loop body is built and checked like any other node), the bindings dropped.
			TArray<FDreamWidgetPropertyBinding> Bindings;
			TArray<FDreamWidgetEventBinding> EventBindings;
			TArray<FDreamWidgetEachBinding> EachBindings;
			TStrongObjectPtr<UDreamWidgetTree> Tree(FDreamUITextBuilder::Build(Ast, GetTransientPackage(), Bag, Bindings, &EventBindings, &EachBindings));
			if (Tree.IsValid())
			{
				Tree->ForEachWidget([&Outcome](UDreamWidget*) { ++Outcome.Nodes; });
				Tree->MarkAsGarbage();
			}
		}
		AppendDiagnostics(Outcome.Diagnostics, Bag, InAbsoluteFile);
		Outcome.bClean = !Bag.HasErrors();
		return Outcome;
	}

	FCompileOutcome CompileSource(const FString& InAbsoluteFile, const bool bSave, const bool bForce)
	{
		FCompileOutcome Outcome;
		FCompileContext Context;
		Context.bSave = bSave;
		Context.bForce = bForce;
		Context.Outcome = &Outcome;
		const FString File = NormalizeFile(InAbsoluteFile);
		Context.Done.Add(File);
		UDreamWidgetBlueprint* Blueprint = nullptr;
		const bool bOk = CompileOne(File, Context, &Blueprint);
		Outcome.Blueprint = Blueprint;
		Outcome.bFailed = !bOk;
		return Outcome;
	}

	void ResetCompileState()
	{
		GLastCompiledHash.Reset();
	}

	void FindAllSources(TArray<FString>& OutFiles)
	{
		DreamUIPaths::FindSourceFiles(OutFiles);
	}
}
