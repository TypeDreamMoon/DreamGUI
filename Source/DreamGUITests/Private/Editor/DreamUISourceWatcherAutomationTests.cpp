// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "DreamWidgetBlueprint.h"
#include "Core/DreamTextUserWidget.h"
#include "Core/DreamWidgetGeneratedClass.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamWidget.h"
#include "Designer/DreamUITextAuthoringGate.h"
#include "Text/DreamUISourceWatcher.h"
#include "Text/DreamUIDocument.h"
#include "Editor.h"
#include "Editor/Transactor.h"//UTransactor, whose undo barrier the reload tests read
#include "DreamGUIEditorSubsystem.h"
#include "Text/DreamUITextWriteBack.h"
#include "Text/DreamUIBridgeService.h"
#include "Text/DreamUIPaths.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ScopedTransaction.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"

/*
 * Save a .dui, and the classes built from it recompile.
 *
 * Driven through QueueFile/FlushPending rather than by touching a file and waiting for the OS to
 * notice. That is deliberate and it is not a shortcut: what these assert is the DRAIN -- which files
 * map to which classes, what gets suppressed, what gets compiled -- and every one of those answers
 * is the same whether the queue was filled by a directory event or by a menu command. Waiting on
 * DirectoryWatcher would add the one part of this that cannot be made deterministic, in exchange for
 * testing a callback that does nothing but filter on an extension.
 *
 * The file lives in Saved/ rather than a DUI root, so a test run cannot leave a source behind for
 * the real watcher to react to, and cannot have the real watcher react to the test.
 */

namespace DreamUISourceWatcherTestLocal
{
	/** A .dui that has really been to disk, and is gone again when the test returns. */
	struct FScopedDuiFile
	{
		explicit FScopedDuiFile(const TCHAR* InFileName)
		{
			FilePath = FPaths::ConvertRelativePathToFull(
				FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("DreamGUITests"), InFileName));
			FPaths::NormalizeFilename(FilePath);
		}

		~FScopedDuiFile()
		{
			IFileManager::Get().Delete(*FilePath, /*RequireExists*/false, /*EvenReadOnly*/true, /*Quiet*/true);
		}

		FScopedDuiFile(const FScopedDuiFile&) = delete;
		FScopedDuiFile& operator=(const FScopedDuiFile&) = delete;

		/** One widget under the root, named InWidgetName. Enough to tell one build from another. */
		bool WriteWith(const TCHAR* InWidgetName) const
		{
			const FString Content = FString::Printf(
				TEXT("Widget Root {\n  + CanvasPanel { }\n  Text %s { }\n}\n"), InWidgetName);
			return FFileHelper::SaveStringToFile(Content, *FilePath);
		}

		FString FilePath;
	};

	struct FScopedTextBlueprint
	{
		UPackage* Package = nullptr;
		UDreamWidgetBlueprint* Blueprint = nullptr;

		explicit FScopedTextBlueprint(const TCHAR* InName)
		{
			Package = CreatePackage(*FString::Printf(TEXT("/Temp/DreamGUITests/%s"), InName));
			Package->AddToRoot();
			Blueprint = Cast<UDreamWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
				UDreamTextUserWidget::StaticClass(), Package, FName(InName), BPTYPE_Normal,
				UDreamWidgetBlueprint::StaticClass(), UDreamWidgetGeneratedClass::StaticClass()));
		}

		~FScopedTextBlueprint()
		{
			if (Package != nullptr)
			{
				Package->RemoveFromRoot();
			}
		}

		FScopedTextBlueprint(const FScopedTextBlueprint&) = delete;
		FScopedTextBlueprint& operator=(const FScopedTextBlueprint&) = delete;

		UDreamWidget* FindTemplate(const TCHAR* InDisplayName) const
		{
			UDreamWidget* Found = nullptr;
			if (Blueprint != nullptr && IsValid(Blueprint->WidgetTree))
			{
				const FString Wanted(InDisplayName);
				Blueprint->WidgetTree->ForEachWidget([&Found, &Wanted](UDreamWidget* Widget)
				{
					if (Found == nullptr && Widget->GetDisplayName() == Wanted)
					{
						Found = Widget;
					}
				});
			}
			return Found;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIWatcherSavedSourceRebuildsItsClassTest,
	"DreamGUI.Text.ASavedSourceRebuildsItsClass",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIWatcherSavedSourceRebuildsItsClassTest::RunTest(const FString&)
{
	using namespace DreamUISourceWatcherTestLocal;

	FScopedDuiFile File(TEXT("WatcherRebuild.dui"));
	if (!TestTrue(TEXT("the .dui was written"), File.WriteWith(TEXT("Title"))))
	{
		return false;
	}

	FScopedTextBlueprint Fixture(TEXT("WatcherRebuild"));
	if (!TestNotNull(TEXT("the Blueprint was created"), Fixture.Blueprint)
		|| !TestTrue(TEXT("and points at the file"),
			DreamUITextAuthoring::SetAuthoredSourcePath(Fixture.Blueprint, File.FilePath)))
	{
		return false;
	}
	if (!TestNotNull(TEXT("which built the file's hierarchy"), Fixture.FindTemplate(TEXT("Title"))))
	{
		return false;
	}

	// The class is found from the file, and the two are spelled differently: the Blueprint stores
	// whatever MakePortablePath made of the picked path, and the watcher only ever has the absolute
	// one. Comparing the spellings instead of the resolved paths would make a rebuild depend on how
	// the class happened to name its file.
	TArray<UDreamWidgetBlueprint*> Found;
	FDreamUISourceWatcher::FindBlueprintsForSource(File.FilePath, Found);
	TestTrue(TEXT("the file finds its class"), Found.Contains(Fixture.Blueprint));

	// The save.
	if (!TestTrue(TEXT("the file was rewritten"), File.WriteWith(TEXT("Renamed"))))
	{
		return false;
	}
	FDreamUISourceWatcher::QueueFile(File.FilePath);
	FDreamUISourceWatcher::FlushPending();

	TestNotNull(TEXT("the class rebuilt from the new text"), Fixture.FindTemplate(TEXT("Renamed")));
	TestNull(TEXT("and the widget the old text named is gone"), Fixture.FindTemplate(TEXT("Title")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIWatcherOwnWriteDoesNotRebuildTest,
	"DreamGUI.Text.TheDesignersOwnWriteDoesNotRebuild",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The loop this closes is not hypothetical: the designer writes the file, the watcher sees a change,
 * the class rebuilds, the rebuild replaces the tree the designer is mirroring, and the next flush
 * writes again. It would run for as long as the editor was open.
 *
 * Asserted by leaving a mark that only a rebuild could erase. A test that checked "the file was not
 * re-read" would pass on a version that re-read it and compiled anyway.
 */
bool FDreamUIWatcherOwnWriteDoesNotRebuildTest::RunTest(const FString&)
{
	using namespace DreamUISourceWatcherTestLocal;

	FScopedDuiFile File(TEXT("WatcherOwnWrite.dui"));
	if (!TestTrue(TEXT("the .dui was written"), File.WriteWith(TEXT("Title"))))
	{
		return false;
	}

	FScopedTextBlueprint Fixture(TEXT("WatcherOwnWrite"));
	if (!TestNotNull(TEXT("the Blueprint was created"), Fixture.Blueprint)
		|| !TestTrue(TEXT("and points at the file"),
			DreamUITextAuthoring::SetAuthoredSourcePath(Fixture.Blueprint, File.FilePath)))
	{
		return false;
	}

	// A document open on the file is what the designer has, and its hash is what makes the
	// suppression answerable: with no document there is nothing of ours for a foreign edit to be
	// mistaken for, which is exactly the case the test above covers.
	FString OpenError;
	FDreamUIDocumentHandle Document = FDreamUIDocumentHandle::Open(File.FilePath, OpenError);
	if (!TestTrue(TEXT("a document opened on the file"), Document.IsValid()))
	{
		return false;
	}

	// The mark. Renaming the template is not something a rebuild would preserve -- the tree is
	// rebuilt from the file, and the file still says Title.
	UDreamWidget* Title = Fixture.FindTemplate(TEXT("Title"));
	if (!TestNotNull(TEXT("the built hierarchy is there to mark"), Title))
	{
		return false;
	}
	Title->SetDisplayName(TEXT("Sentinel"));

	FDreamUISourceWatcher::QueueFile(File.FilePath);
	FDreamUISourceWatcher::FlushPending();

	TestNotNull(TEXT("the class was left alone"), Fixture.FindTemplate(TEXT("Sentinel")));
	TestNull(TEXT("so no rebuild put the file's name back"), Fixture.FindTemplate(TEXT("Title")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIWatcherDeletedSourceIsReportedTest,
	"DreamGUI.Text.ADeletedSourceSaysWhichClassesItLeftBuiltFromNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The one change to a .dui that the class can never find out about on its own.
 *
 * A delete (and a rename, which arrives as one) leaves nothing to rebuild from, so the class keeps
 * the tree it last compiled and is wrong from then on -- silently, until somebody presses Compile by
 * hand and meets DUI6001 with no memory of having moved the file. The watcher used to drop
 * FCA_Removed where the event arrived, so there was not even a log line.
 *
 * Asserted on the warning, because the warning IS the fix: nothing is rebuilt and nothing can be.
 */
bool FDreamUIWatcherDeletedSourceIsReportedTest::RunTest(const FString&)
{
	using namespace DreamUISourceWatcherTestLocal;

	FScopedDuiFile File(TEXT("WatcherRemoved.dui"));
	if (!TestTrue(TEXT("the .dui was written"), File.WriteWith(TEXT("Title"))))
	{
		return false;
	}

	FScopedTextBlueprint Fixture(TEXT("WatcherRemoved"));
	if (!TestNotNull(TEXT("the Blueprint was created"), Fixture.Blueprint)
		|| !TestTrue(TEXT("and points at the file"),
			DreamUITextAuthoring::SetAuthoredSourcePath(Fixture.Blueprint, File.FilePath)))
	{
		return false;
	}
	if (!TestNotNull(TEXT("which built the file's hierarchy"), Fixture.FindTemplate(TEXT("Title"))))
	{
		return false;
	}

	// Gone, the way a delete or a rename leaves it. The queue is filled directly for the same reason
	// the rebuild tests fill it directly: what is under test is the drain, not DirectoryWatcher.
	IFileManager::Get().Delete(*File.FilePath, /*RequireExists*/false, /*EvenReadOnly*/true, /*Quiet*/true);
	if (!TestFalse(TEXT("the file is really gone"), FPaths::FileExists(File.FilePath)))
	{
		return false;
	}

	AddExpectedMessagePlain(TEXT("is gone from disk"), ELogVerbosity::Warning,
		EAutomationExpectedMessageFlags::Contains, 1);

	FDreamUISourceWatcher::QueueRemoval(File.FilePath);
	FDreamUISourceWatcher::FlushPending();

	// Nothing was rebuilt, because nothing could be: the hierarchy the class last compiled is still
	// there. The point of the pass is that the author has been told, not that anything changed.
	TestNotNull(TEXT("the class keeps the tree it last built"), Fixture.FindTemplate(TEXT("Title")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIWatcherWaitsForAnEditInProgressTest,
	"DreamGUI.Text.ASaveDuringAnEditInProgressWaitsForTheEditToEnd",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A save that lands while the author is in the middle of something -- dragging a widget in the designer's
 * hierarchy, dragging a value in a details panel -- would recompile the class under it: the compile rebuilds
 * the preview the drag holds widgets of, and an open transaction would take the compile's changes into the
 * edit it is recording, for the next undo to take back. The watcher waits both out, and compiles once the
 * edit is over. Driven through a transaction, because a drag in flight is Slate's to report and a test cannot
 * hold one open; the watcher asks the one question for both.
 */
bool FDreamUIWatcherWaitsForAnEditInProgressTest::RunTest(const FString&)
{
	using namespace DreamUISourceWatcherTestLocal;

	FScopedDuiFile File(TEXT("WatcherWaitsForEdit.dui"));
	if (!TestTrue(TEXT("the .dui was written"), File.WriteWith(TEXT("Title"))))
	{
		return false;
	}
	FScopedTextBlueprint Fixture(TEXT("WatcherWaitsForEdit"));
	if (!TestNotNull(TEXT("the Blueprint was created"), Fixture.Blueprint)
		|| !TestTrue(TEXT("and points at the file"),
			DreamUITextAuthoring::SetAuthoredSourcePath(Fixture.Blueprint, File.FilePath))
		|| !TestNotNull(TEXT("which built the file's hierarchy"), Fixture.FindTemplate(TEXT("Title"))))
	{
		return false;
	}
	if (!TestTrue(TEXT("the file was rewritten"), File.WriteWith(TEXT("Renamed"))))
	{
		return false;
	}

	GEditor->BeginTransaction(FText::FromString(TEXT("An edit in progress")));
	FDreamUISourceWatcher::QueueFile(File.FilePath);
	FDreamUISourceWatcher::FlushPending();
	TestNotNull(TEXT("while the edit is open, the class is left as it was"), Fixture.FindTemplate(TEXT("Title")));
	TestNull(TEXT("and the new text is not built yet"), Fixture.FindTemplate(TEXT("Renamed")));
	GEditor->EndTransaction();

	FDreamUISourceWatcher::FlushPending();
	TestNotNull(TEXT("once it is over, the queued save rebuilds the class"), Fixture.FindTemplate(TEXT("Renamed")));
	TestNull(TEXT("from the text as it is now"), Fixture.FindTemplate(TEXT("Title")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIWatcherBelongsToTheSessionTest,
	"DreamGUI.Text.TheWatchersQueueBelongsToTheEditorSession",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The watcher's queue, its watches and its dependency table are the editor session's, held by the editor's
 * DreamGUI subsystem from the session's start to its end, not globals that outlive it.
 */
bool FDreamUIWatcherBelongsToTheSessionTest::RunTest(const FString&)
{
	UDreamGUIEditorSubsystem* EditorSubsystem = UDreamGUIEditorSubsystem::Get();
	if (!TestNotNull(TEXT("the editor's DreamGUI subsystem exists"), EditorSubsystem))
	{
		return false;
	}
	FDreamUISourceWatcherState& State = EditorSubsystem->GetSourceWatcher();
	TestTrue(TEXT("the session's watcher is draining its queue"), State.TickerHandle.IsValid());

	const FString Missing = FDreamUIDocumentRegistry::NormalizePath(FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("DreamGUITests"), TEXT("WatcherSessionNeverWritten.dui"))));
	FDreamUISourceWatcher::QueueFile(Missing);
	TestTrue(TEXT("a queued file waits in the session's queue"), State.PendingFiles.Contains(Missing));
	FDreamUISourceWatcher::FlushPending();
	TestFalse(TEXT("and a flush drains it"), State.PendingFiles.Contains(Missing));
	return true;
}

namespace DreamUISourceWatcherTestLocal
{
	/**
	 * Point a Blueprint at a file the way Set Source File does, minus its transaction.
	 *
	 * The command opens one, and it records the Blueprint's own objects -- which is exactly what the undo-history
	 * test must not already have in the queue.
	 */
	bool AssignSourceWithoutTransaction(UDreamWidgetBlueprint* InBlueprint, const FString& InPath)
	{
		UDreamTextUserWidget* Defaults = InBlueprint != nullptr && InBlueprint->GeneratedClass != nullptr
			? Cast<UDreamTextUserWidget>(InBlueprint->GeneratedClass->GetDefaultObject(/*bCreateIfNeeded*/true))
			: nullptr;
		if (Defaults == nullptr)
		{
			return false;
		}
		Defaults->SourceFile.FilePath = DreamUIPaths::MakePortablePath(InPath);
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(InBlueprint);
		FKismetEditorUtilities::CompileBlueprint(InBlueprint, EBlueprintCompileOptions::SkipGarbageCollection);
		return true;
	}

	/** One request through a scratch bridge root, so the test never touches the live bridge. */
	TSharedPtr<FJsonObject> AskBridge(const FString& InRequestJson)
	{
		const FString Root = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(),
			TEXT("DreamGUITests"), TEXT("WatcherBridge"), FGuid::NewGuid().ToString(EGuidFormats::Short)));
		IFileManager::Get().MakeDirectory(*(Root / TEXT("Requests")), /*Tree*/true);
		IFileManager::Get().MakeDirectory(*(Root / TEXT("Responses")), /*Tree*/true);
		FFileHelper::SaveStringToFile(InRequestJson, *(Root / TEXT("Requests") / TEXT("001.request.json")),
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		FDreamUIBridgeService::ProcessPendingNow(Root);

		TSharedPtr<FJsonObject> Response;
		FString Serialized;
		if (FFileHelper::LoadFileToString(Serialized, *(Root / TEXT("Responses") / TEXT("001.response.json"))))
		{
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Serialized);
			FJsonSerializer::Deserialize(Reader, Response);
		}
		IFileManager::Get().DeleteDirectory(*Root, /*RequireExists*/false, /*Tree*/true);
		return Response;
	}

	/** The key an import edge is filed under, as the watcher spells it. */
	FString EdgeKey(const FString& InPath)
	{
		FString Key = InPath;
		FPaths::NormalizeFilename(Key);
		return Key.ToLower();
	}

	bool HasImporter(const FDreamUISourceWatcherState& InState, const FString& InImport, const FString& InImporter)
	{
		TArray<FString> Importers;
		InState.ImportEdges.MultiFind(EdgeKey(InImport), Importers);
		return Importers.Contains(InImporter);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIWatcherSkipsTextItAlreadyBuiltTest,
	"DreamGUI.Text.ASaveOfTheTextAlreadyBuiltRebuildsNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A touch, a save with no edit, a tool rewriting a file with the bytes it had: each is a file event, and each used to be
 * a full Blueprint compile per class to arrive at what was already there. The watcher remembers the text its last
 * successful rebuild read, and for which classes, and leaves an event for the same text alone.
 *
 * Asserted by a mark that only a rebuild would erase, as the own-write test does. The three ways the memory must NOT
 * be believed are asserted too, since a skip that is wrong is a class that silently stays stale:
 *   - an explicit command asks for a rebuild, and gets one;
 *   - somebody else compiled the class in between (the designer, the Compile button), from text the watcher
 *     cannot vouch for;
 *   - the text really changed.
 */
bool FDreamUIWatcherSkipsTextItAlreadyBuiltTest::RunTest(const FString&)
{
	using namespace DreamUISourceWatcherTestLocal;

	FScopedDuiFile File(TEXT("WatcherUnchanged.dui"));
	if (!TestTrue(TEXT("the .dui was written"), File.WriteWith(TEXT("Title"))))
	{
		return false;
	}
	FScopedTextBlueprint Fixture(TEXT("WatcherUnchanged"));
	if (!TestNotNull(TEXT("the Blueprint was created"), Fixture.Blueprint)
		|| !TestTrue(TEXT("and points at the file"),
			DreamUITextAuthoring::SetAuthoredSourcePath(Fixture.Blueprint, File.FilePath))
		|| !TestNotNull(TEXT("which built the file's hierarchy"), Fixture.FindTemplate(TEXT("Title"))))
	{
		return false;
	}

	// A save that changes the text: rebuilt, and remembered.
	if (!TestTrue(TEXT("the file was rewritten"), File.WriteWith(TEXT("Renamed"))))
	{
		return false;
	}
	FDreamUISourceWatcher::QueueFile(File.FilePath);
	FDreamUISourceWatcher::FlushPending();
	UDreamWidget* Renamed = Fixture.FindTemplate(TEXT("Renamed"));
	if (!TestNotNull(TEXT("a changed text rebuilds the class"), Renamed))
	{
		return false;
	}

	// The same text again: left alone.
	Renamed->SetDisplayName(TEXT("Sentinel"));
	if (!TestTrue(TEXT("the file was written again, with the same text"), File.WriteWith(TEXT("Renamed"))))
	{
		return false;
	}
	FDreamUISourceWatcher::QueueFile(File.FilePath);
	FDreamUISourceWatcher::FlushPending();
	TestNotNull(TEXT("an event for the same text rebuilds nothing"), Fixture.FindTemplate(TEXT("Sentinel")));

	// An explicit command asks for a rebuild and gets one, whatever the text.
	FDreamUISourceWatcher::QueueFile(File.FilePath, /*bAnnounceSuccess*/true);
	FDreamUISourceWatcher::FlushPending();
	TestNull(TEXT("an explicit rebuild ignores the memory"), Fixture.FindTemplate(TEXT("Sentinel")));
	UDreamWidget* Rebuilt = Fixture.FindTemplate(TEXT("Renamed"));
	if (!TestNotNull(TEXT("and builds the file's text"), Rebuilt))
	{
		return false;
	}

	// Somebody else compiled the class: what the watcher remembered no longer says what the class was built from.
	FKismetEditorUtilities::CompileBlueprint(Fixture.Blueprint, EBlueprintCompileOptions::SkipGarbageCollection);
	UDreamWidget* Recompiled = Fixture.FindTemplate(TEXT("Renamed"));
	if (!TestNotNull(TEXT("a manual compile rebuilt the hierarchy"), Recompiled))
	{
		return false;
	}
	Recompiled->SetDisplayName(TEXT("Sentinel"));
	FDreamUISourceWatcher::QueueFile(File.FilePath);
	FDreamUISourceWatcher::FlushPending();
	TestNull(TEXT("after somebody else's compile the same text is rebuilt"), Fixture.FindTemplate(TEXT("Sentinel")));
	TestNotNull(TEXT("from the file"), Fixture.FindTemplate(TEXT("Renamed")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIWatcherCutsUndoOnlyWhereItReachesTheDocumentTest,
	"DreamGUI.Text.AnOutsideSaveCutsTheUndoHistoryOnlyWhenUndoCouldReachTheDocument",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A reload is not transacted, so undoing an entry from before it would write the older text back over the outside edit;
 * the undo barrier stops that. It is also global -- level edits included -- so it is set when there is an entry to
 * protect and not otherwise.
 *
 * The queue position is moved on first by an unrelated transaction, so a barrier set at "now" cannot be mistaken for one
 * that was already standing at the same place.
 */
bool FDreamUIWatcherCutsUndoOnlyWhereItReachesTheDocumentTest::RunTest(const FString&)
{
	using namespace DreamUISourceWatcherTestLocal;

	if (!TestNotNull(TEXT("the editor has a transaction buffer"), GEditor != nullptr ? GEditor->Trans.Get() : nullptr))
	{
		return false;
	}
	const FString Suffix = FGuid::NewGuid().ToString(EGuidFormats::Digits);
	const FString Name = TEXT("WatcherUndoCut_") + Suffix;
	FScopedDuiFile File(*(Name + TEXT(".dui")));
	if (!TestTrue(TEXT("the .dui was written"), File.WriteWith(TEXT("Title"))))
	{
		return false;
	}
	FScopedTextBlueprint Fixture(*Name);
	if (!TestNotNull(TEXT("the Blueprint was created"), Fixture.Blueprint)
		|| !TestTrue(TEXT("and built from the file"), AssignSourceWithoutTransaction(Fixture.Blueprint, File.FilePath))
		|| !TestNotNull(TEXT("with its hierarchy"), Fixture.FindTemplate(TEXT("Title"))))
	{
		return false;
	}
	FString OpenError;
	FDreamUIDocumentHandle Document = FDreamUIDocumentHandle::Open(File.FilePath, OpenError);
	if (!TestTrue(TEXT("a document opened on the file"), Document.IsValid()))
	{
		return false;
	}

	// Something unrelated on the undo stack, to move its end past wherever a barrier may already stand.
	{
		const FScopedTransaction Unrelated(FText::FromString(TEXT("Something else")));
		UDreamUIDocument* Other = NewObject<UDreamUIDocument>(GetTransientPackage(), NAME_None, RF_Transactional);
		Other->Modify();
	}
	const int32 Before = GEditor->Trans->GetCurrentUndoBarrier();

	// An outside save while nothing on the stack touches the document or the Blueprint: no barrier.
	if (!TestTrue(TEXT("the file was changed from outside"), File.WriteWith(TEXT("Second"))))
	{
		return false;
	}
	FDreamUISourceWatcher::QueueFile(File.FilePath);
	FDreamUISourceWatcher::FlushPending();
	TestNotNull(TEXT("the class was rebuilt from the outside text"), Fixture.FindTemplate(TEXT("Second")));
	TestEqual(TEXT("and the undo history was left alone"), GEditor->Trans->GetCurrentUndoBarrier(), Before);

	// An undoable edit of the document, and then an outside save: now there is an entry undo could replay.
	{
		const FScopedTransaction Edit(FText::FromString(TEXT("Edit the document")));
		FString WriteError;
		TestTrue(TEXT("the document took the designer's edit"), Document.Get()->SetContent(
			TEXT("Widget Root {\n  + CanvasPanel { }\n  Text Designer { }\n}\n"), WriteError));
	}
	if (!TestTrue(TEXT("the file was changed from outside again"), File.WriteWith(TEXT("Third"))))
	{
		return false;
	}
	FDreamUISourceWatcher::QueueFile(File.FilePath);
	FDreamUISourceWatcher::FlushPending();
	TestNotNull(TEXT("the class was rebuilt again"), Fixture.FindTemplate(TEXT("Third")));
	const int32 After = GEditor->Trans->GetCurrentUndoBarrier();
	TestNotEqual(TEXT("and the undo history was cut"), After, Before);
	TestEqual(TEXT("at the end of the stack"), After,
		GEditor->Trans->GetQueueLength() - GEditor->Trans->GetUndoCount());

	// The barrier is the editor's, not this test's to keep.
	GEditor->Trans->RemoveUndoBarrier();
	TestEqual(TEXT("and taken back out"), GEditor->Trans->GetCurrentUndoBarrier(), Before);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIWatcherKeepsDesignerEditsAnOutsideSaveReplacesTest,
	"DreamGUI.Text.DesignerEditsAnOutsideSaveReplacesAreKeptAside",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The designer's edit was refused because the file had changed under it, and the edit stayed owed in the document. The
 * reload that brings the outside text in is where it is lost -- and used to be lost without a word. It is kept in a file
 * under Saved/ now, and the reload says so.
 */
bool FDreamUIWatcherKeepsDesignerEditsAnOutsideSaveReplacesTest::RunTest(const FString&)
{
	using namespace DreamUISourceWatcherTestLocal;

	const FString Suffix = FGuid::NewGuid().ToString(EGuidFormats::Digits);
	const FString Name = TEXT("WatcherDiscard_") + Suffix;
	FScopedDuiFile File(*(Name + TEXT(".dui")));
	if (!TestTrue(TEXT("the .dui was written"), File.WriteWith(TEXT("Title"))))
	{
		return false;
	}
	FScopedTextBlueprint Fixture(*Name);
	if (!TestNotNull(TEXT("the Blueprint was created"), Fixture.Blueprint)
		|| !TestTrue(TEXT("and built from the file"), AssignSourceWithoutTransaction(Fixture.Blueprint, File.FilePath)))
	{
		return false;
	}
	FString OpenError;
	FDreamUIDocumentHandle Document = FDreamUIDocumentHandle::Open(File.FilePath, OpenError);
	if (!TestTrue(TEXT("a document opened on the file"), Document.IsValid()))
	{
		return false;
	}

	// Somebody else saves the file; the designer edits before the watcher has brought that in.
	if (!TestTrue(TEXT("the file was changed from outside"), File.WriteWith(TEXT("Outside"))))
	{
		return false;
	}
	const FString DesignerText(TEXT("Widget Root {\n  + CanvasPanel { }\n  Text FromTheDesigner { }\n}\n"));
	{
		const FScopedTransaction Edit(FText::FromString(TEXT("Edit the document")));
		FString WriteError;
		TestFalse(TEXT("the designer's write is refused"), Document.Get()->SetContent(DesignerText, WriteError));
	}
	if (!TestTrue(TEXT("and stays owed"), Document.Get()->HasUnflushedWrite()))
	{
		return false;
	}

	const FString Directory = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("DreamGUI"), TEXT("Discarded")));
	TArray<FString> Before;
	IFileManager::Get().FindFiles(Before, *(Directory / (Name + TEXT(".*.dui"))), /*Files*/true, /*Directories*/false);

	AddExpectedMessagePlain(TEXT("while the designer held edits that had not reached it"), ELogVerbosity::Warning,
		EAutomationExpectedMessageFlags::Contains, 1);
	FDreamUISourceWatcher::QueueFile(File.FilePath);
	FDreamUISourceWatcher::FlushPending();

	TestFalse(TEXT("the document is no longer owed a write"), Document.Get()->HasUnflushedWrite());
	TestTrue(TEXT("it holds the outside text"), Document.Get()->GetContent().Contains(TEXT("Outside")));
	TestNotNull(TEXT("and the class was rebuilt from it"), Fixture.FindTemplate(TEXT("Outside")));

	TArray<FString> After;
	IFileManager::Get().FindFiles(After, *(Directory / (Name + TEXT(".*.dui"))), /*Files*/true, /*Directories*/false);
	if (TestEqual(TEXT("exactly one copy was kept"), After.Num(), Before.Num() + 1) && After.Num() > 0)
	{
		const FString Copy = Directory / After.Last();
		FString Kept;
		TestTrue(TEXT("the copy can be read"), FFileHelper::LoadFileToString(Kept, *Copy));
		TestTrue(TEXT("and is the text the designer held"), Kept.Equals(DesignerText, ESearchCase::CaseSensitive));
		for (const FString& Name2 : After)
		{
			IFileManager::Get().Delete(*(Directory / Name2), /*RequireExists*/false, /*EvenReadOnly*/true, /*Quiet*/true);
		}
	}

	// And the helper on its own: a path under Saved/, holding exactly what it was given.
	const FString Direct = FDreamUISourceWatcher::SaveDiscardedDesignerText(File.FilePath, TEXT("keep 中文"));
	if (TestFalse(TEXT("the helper reports where it wrote"), Direct.IsEmpty()))
	{
		FString Kept;
		TestTrue(TEXT("the file is there"), FFileHelper::LoadFileToString(Kept, *Direct));
		TestTrue(TEXT("with the text, non-ASCII included"), Kept.Equals(TEXT("keep 中文"), ESearchCase::CaseSensitive));
		IFileManager::Get().Delete(*Direct, /*RequireExists*/false, /*EvenReadOnly*/true, /*Quiet*/true);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIWatcherHoldsABackgroundEditorAtFullRateTest,
	"DreamGUI.Text.TheWatcherKeepsABackgroundEditorAtFullRateWhileASaveIsPending",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * An editor behind the author's text editor runs at 3 FPS, and every step between a save and the designer redrawing
 * advances one frame at a time. The editor lets systems opt out of that throttling; the watcher opts out while
 * something is pending that it could act on, and briefly after a .dui event.
 *
 * Asked of the watcher's own entry, found by its handle: the bridge adds one too, and either of them returning true
 * would hide the other's answer.
 */
bool FDreamUIWatcherHoldsABackgroundEditorAtFullRateTest::RunTest(const FString&)
{
	using namespace DreamUISourceWatcherTestLocal;

	UDreamGUIEditorSubsystem* EditorSubsystem = UDreamGUIEditorSubsystem::Get();
	if (!TestNotNull(TEXT("the editor's DreamGUI subsystem exists"), EditorSubsystem) || !TestNotNull(TEXT("and so does the editor"), GEditor))
	{
		return false;
	}
	FDreamUISourceWatcherState& State = EditorSubsystem->GetSourceWatcher();
	if (!TestTrue(TEXT("the session registered an exemption"), State.CpuThrottleHandle.IsValid()))
	{
		return false;
	}
	const UEditorEngine::FShouldDisableCPUThrottling* Exemption = GEditor->ShouldDisableCPUThrottlingDelegates.FindByPredicate(
		[&State](const UEditorEngine::FShouldDisableCPUThrottling& InDelegate)
		{
			return InDelegate.GetHandle() == State.CpuThrottleHandle;
		});
	if (!TestNotNull(TEXT("and it is in the editor's list"), Exemption))
	{
		return false;
	}

	FDreamUISourceWatcher::FlushPending();
	const double SavedActivity = State.LastActivityTime;

	State.LastActivityTime = -1000.0;
	TestFalse(TEXT("with nothing pending and nothing recent, the editor throttles as usual"), Exemption->Execute());

	const FString Missing = FDreamUIDocumentRegistry::NormalizePath(FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("DreamGUITests"), TEXT("WatcherThrottleNeverWritten.dui"))));
	FDreamUISourceWatcher::QueueFile(Missing);
	TestTrue(TEXT("a queued file is activity"), Exemption->Execute());

	State.LastActivityTime = -1000.0;
	TestTrue(TEXT("a pending file that could be compiled keeps full rate even after the activity is old"), Exemption->Execute());

	GEditor->BeginTransaction(FText::FromString(TEXT("An edit in progress")));
	TestFalse(TEXT("a pending file that cannot be compiled yet does not"), Exemption->Execute());
	GEditor->EndTransaction();

	FDreamUISourceWatcher::FlushPending();
	State.LastActivityTime = -1000.0;
	TestFalse(TEXT("and once drained the editor throttles again"), Exemption->Execute());
	State.LastActivityTime = SavedActivity;
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIWatcherRepublishingImportsReplacesTheOldTest,
	"DreamGUI.Text.RepublishingAFilesImportsReplacesExactlyItsOwnEdges",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The dependency table is written by the compiler after every parse and by the watcher's own sweep, both of which hand
 * over a file's WHOLE current list. The previous list's edges are found through a per-file record rather than by walking
 * the table (the first sweep was files times edges); what is asserted is that this finds exactly them -- not another
 * importer's edge to the same file, and not one too few.
 */
bool FDreamUIWatcherRepublishingImportsReplacesTheOldTest::RunTest(const FString&)
{
	using namespace DreamUISourceWatcherTestLocal;

	UDreamGUIEditorSubsystem* EditorSubsystem = UDreamGUIEditorSubsystem::Get();
	if (!TestNotNull(TEXT("the editor's DreamGUI subsystem exists"), EditorSubsystem))
	{
		return false;
	}
	const FDreamUISourceWatcherState& State = EditorSubsystem->GetSourceWatcher();

	const FString Suffix = FGuid::NewGuid().ToString(EGuidFormats::Digits);
	const FString A = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("DreamGUITests"), TEXT("EdgeA_") + Suffix + TEXT(".dui"));
	const FString B = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("DreamGUITests"), TEXT("EdgeB_") + Suffix + TEXT(".dui"));
	const FString C = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("DreamGUITests"), TEXT("EdgeC_") + Suffix + TEXT(".dui"));
	const FString D = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("DreamGUITests"), TEXT("EdgeD_") + Suffix + TEXT(".dui"));

	FDreamUISourceWatcher::NoteImports(A, { B, C });
	FDreamUISourceWatcher::NoteImports(D, { B });
	TestTrue(TEXT("A imports B"), HasImporter(State, B, A));
	TestTrue(TEXT("A imports C"), HasImporter(State, C, A));
	TestTrue(TEXT("D imports B"), HasImporter(State, B, D));

	// A drops B; D's own edge to B is not A's to remove.
	FDreamUISourceWatcher::NoteImports(A, { C });
	TestFalse(TEXT("A no longer imports B"), HasImporter(State, B, A));
	TestTrue(TEXT("A still imports C"), HasImporter(State, C, A));
	TestTrue(TEXT("and D still imports B"), HasImporter(State, B, D));

	// Nothing at all is a list too.
	FDreamUISourceWatcher::NoteImports(A, TArray<FString>());
	FDreamUISourceWatcher::NoteImports(D, TArray<FString>());
	TestFalse(TEXT("an empty list removes the last edge of A"), HasImporter(State, C, A));
	TestFalse(TEXT("and of D"), HasImporter(State, B, D));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIWatcherBridgeCompileLeavesAQueuedFileToTheQueueTest,
	"DreamGUI.Text.TheBridgesCompileLeavesAFileAlreadyQueuedToTheWatcher",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * VS Code saves a file and, a moment later, asks the editor to compile it. The watcher already has the save in its queue
 * and will compile it when the burst of events settles; compiling it from the request as well builds the same text twice.
 * The request is answered, says why it did not compile, and the queue still does.
 */
bool FDreamUIWatcherBridgeCompileLeavesAQueuedFileToTheQueueTest::RunTest(const FString&)
{
	using namespace DreamUISourceWatcherTestLocal;

	const FString Name = TEXT("WatcherBridgeCompile_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	FScopedDuiFile File(*(Name + TEXT(".dui")));
	if (!TestTrue(TEXT("the .dui was written"), File.WriteWith(TEXT("Title"))))
	{
		return false;
	}
	FScopedTextBlueprint Fixture(*Name);
	if (!TestNotNull(TEXT("the Blueprint was created"), Fixture.Blueprint)
		|| !TestTrue(TEXT("and built from the file"), AssignSourceWithoutTransaction(Fixture.Blueprint, File.FilePath))
		|| !TestNotNull(TEXT("with its hierarchy"), Fixture.FindTemplate(TEXT("Title"))))
	{
		return false;
	}
	const FString Request = FString::Printf(
		TEXT("{\"protocol\":1,\"requestId\":\"001\",\"action\":\"compile\",\"classPath\":\"%s\"}"),
		*Fixture.Blueprint->GetPathName());

	// Queued, not drained: the mark is still on the class after the request.
	Fixture.FindTemplate(TEXT("Title"))->SetDisplayName(TEXT("Sentinel"));
	FDreamUISourceWatcher::QueueFile(File.FilePath);
	TestTrue(TEXT("the file is waiting in the queue"), FDreamUISourceWatcher::IsQueued(File.FilePath));
	const TSharedPtr<FJsonObject> Queued = AskBridge(Request);
	if (TestTrue(TEXT("the request was answered"), Queued.IsValid()))
	{
		TestTrue(TEXT("as a success"), Queued->GetBoolField(TEXT("ok")));
		TestTrue(TEXT("saying the watcher has it"), Queued->GetStringField(TEXT("message")).Contains(TEXT("queued")));
	}
	TestNotNull(TEXT("and the class was not compiled by the request"), Fixture.FindTemplate(TEXT("Sentinel")));

	FDreamUISourceWatcher::FlushPending();
	TestFalse(TEXT("the queue then drained"), FDreamUISourceWatcher::IsQueued(File.FilePath));
	TestNull(TEXT("and compiled the file itself"), Fixture.FindTemplate(TEXT("Sentinel")));

	// Nothing queued: the request compiles.
	Fixture.FindTemplate(TEXT("Title"))->SetDisplayName(TEXT("Sentinel"));
	const TSharedPtr<FJsonObject> Direct = AskBridge(Request);
	if (TestTrue(TEXT("a request with nothing queued was answered"), Direct.IsValid()))
	{
		TestTrue(TEXT("as a success"), Direct->GetBoolField(TEXT("ok")));
		TestTrue(TEXT("saying it compiled"), Direct->GetStringField(TEXT("message")).Contains(TEXT("compiled")));
	}
	TestNull(TEXT("and the class was compiled by it"), Fixture.FindTemplate(TEXT("Sentinel")));
	return true;
}

#endif
