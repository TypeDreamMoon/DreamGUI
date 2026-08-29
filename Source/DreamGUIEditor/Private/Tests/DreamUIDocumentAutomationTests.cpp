// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Text/DreamUIDocument.h"

#include "Editor.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/StrongObjectPtr.h"

/**
 * The undo carrier for `.dui` text.
 *
 * The load-bearing test here is UndoRestoresTheTextNotJustTheObject, and specifically its second
 * half. UE's transaction system restores OBJECTS, so a designer that edits the object and writes
 * the file as a side effect passes every in-memory undo assertion anyone would think to write --
 * and still loses the undo, because the line in the file is unchanged and the next regeneration
 * brings the edit back. An undo test that only checks the object cannot see that. So every case
 * below asserts the bytes on disk as well.
 *
 * These use ordinary files under Saved/, NOT DreamOnDiskFixture: that fixture is about a package
 * arriving through the serializer, which is a different hazard. What it and this share is the
 * reason both exist -- a test that never touches the filesystem is structurally unable to see the
 * failure the feature is about.
 */
namespace DreamUIDocumentTestLocal
{
	/** A real `.dui` under Saved/, removed -- read-only bit and all -- when the test leaves. */
	struct FScopedDuiFile
	{
		FString Path;

		FScopedDuiFile(const TCHAR* InFileName, const FString& InInitialText)
		{
			Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("DreamGUITests") / InFileName);
			// The same encoding the document writes with, so a test that never edits still starts
			// from a file the document itself could have produced.
			FFileHelper::SaveStringToFile(InInitialText, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		}

		~FScopedDuiFile()
		{
			// EvenReadOnly, because one of the tests below deliberately makes it read-only, and a
			// leftover would poison the NEXT run rather than fail this one.
			SetReadOnly(false);
			IFileManager::Get().Delete(*Path, /*RequireExists*/false, /*EvenReadOnly*/true, /*Quiet*/true);
		}

		FScopedDuiFile(const FScopedDuiFile&) = delete;
		FScopedDuiFile& operator=(const FScopedDuiFile&) = delete;

		/** What the file actually says right now. The only honest answer to "did the undo land". */
		FString ReadBack() const
		{
			FString Text;
			if (!FFileHelper::LoadFileToString(Text, *Path))
			{
				return TEXT("<the file could not be read>");
			}
			return Text;
		}

		/** Write the file behind the document's back, the way an external editor or an AI would. */
		void WriteBehindTheDocument(const FString& InText) const
		{
			FFileHelper::SaveStringToFile(InText, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		}

		void SetReadOnly(bool bInReadOnly) const
		{
			FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*Path, bInReadOnly);
		}
	};

	/** Records what OnTextChanged said, so a test can assert the host would have been told. */
	struct FTextChangeRecorder
	{
		TArray<EDreamUIDocumentChangeReason> Seen;
		void OnChanged(EDreamUIDocumentChangeReason InReason) { Seen.Add(InReason); }
	};

	const TCHAR* const TextA = TEXT("Panel Root {\n  AnchorData.SizeDelta = (400, 240)\n}\n");
	const TCHAR* const TextB = TEXT("Panel Root {\n  AnchorData.SizeDelta = (800, 480)\n}\n");

	/** Every case needs GEditor and a transaction buffer; without them nothing below means anything. */
	bool HasTransactionBuffer(FAutomationTestBase& InTest)
	{
		if (GEditor == nullptr || GEditor->Trans == nullptr)
		{
			InTest.AddError(TEXT("no transaction buffer; this test cannot say anything"));
			return false;
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIDocumentEditReachesDiskTest,
	"DreamGUI.Designer.ADocumentEditReachesTheFileOnDisk",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIDocumentEditReachesDiskTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIDocumentTestLocal;
	if (!HasTransactionBuffer(*this)) return false;

	FScopedDuiFile File(TEXT("EditReachesDisk.dui"), TextA);

	FString Error;
	TStrongObjectPtr<UDreamUIDocument> Document(UDreamUIDocument::CreateFromFile(nullptr, File.Path, Error));
	if (!TestNotNull(TEXT("the document loaded"), (UObject*)Document.Get())) return false;
	TestEqualSensitive(TEXT("and holds what the file said"), Document->GetContent(), FString(TextA));
	TestFalse(TEXT("with nothing owed to the file"), Document->HasUnflushedWrite());

	FTextChangeRecorder Recorder;
	Document->OnTextChanged().AddRaw(&Recorder, &FTextChangeRecorder::OnChanged);

	GEditor->BeginTransaction(FText::FromString(TEXT("Test Edit")));
	const bool bSet = Document->SetContent(TextB, Error);
	GEditor->EndTransaction();

	TestTrue(TEXT("the edit went through"), bSet);
	TestEqualSensitive(TEXT("the document holds the new text"), Document->GetContent(), FString(TextB));
	TestEqualSensitive(TEXT("and so does the file"), File.ReadBack(), FString(TextB));
	TestFalse(TEXT("nothing is owed"), Document->HasUnflushedWrite());

	if (TestEqual(TEXT("the host was told once"), Recorder.Seen.Num(), 1))
	{
		TestTrue(TEXT("and told it was an edit"), Recorder.Seen[0] == EDreamUIDocumentChangeReason::Edited);
	}

	// Writing the same text again is not an edit. An undo entry that describes no change is worse
	// than no entry: Ctrl+Z appears to do nothing, so the user presses it again and loses the edit
	// before it -- the shape already pinned in DreamUIEditorUndoAutomationTests.
	GEditor->BeginTransaction(FText::FromString(TEXT("Test No-op Edit")));
	Document->SetContent(TextB, Error);
	GEditor->EndTransaction();
	TestEqual(TEXT("an identical write tells nobody"), Recorder.Seen.Num(), 1);

	Document->OnTextChanged().RemoveAll(&Recorder);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIDocumentUndoRestoresTextTest,
	"DreamGUI.Designer.UndoRestoresTheTextNotJustTheObject",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIDocumentUndoRestoresTextTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIDocumentTestLocal;
	if (!HasTransactionBuffer(*this)) return false;

	FScopedDuiFile File(TEXT("UndoRestoresText.dui"), TextA);

	FString Error;
	TStrongObjectPtr<UDreamUIDocument> Document(UDreamUIDocument::CreateFromFile(nullptr, File.Path, Error));
	if (!TestNotNull(TEXT("the document loaded"), (UObject*)Document.Get())) return false;

	FTextChangeRecorder Recorder;
	Document->OnTextChanged().AddRaw(&Recorder, &FTextChangeRecorder::OnChanged);

	GEditor->BeginTransaction(FText::FromString(TEXT("Test Edit")));
	Document->SetContent(TextB, Error);
	GEditor->EndTransaction();
	TestEqualSensitive(TEXT("the edit reached the file first"), File.ReadBack(), FString(TextB));

	GEditor->UndoTransaction();

	TestEqualSensitive(TEXT("undo restores the text in memory"), Document->GetContent(), FString(TextA));
	// THE HALF THAT MATTERS. Without it this test passes for a design that writes the file outside
	// the transaction, and the next regeneration silently brings the undone edit back.
	TestEqualSensitive(TEXT("and the file on disk goes back with it"), File.ReadBack(), FString(TextA));
	TestFalse(TEXT("with nothing left owed"), Document->HasUnflushedWrite());

	if (TestEqual(TEXT("the host heard about the edit and then the undo"), Recorder.Seen.Num(), 2))
	{
		TestTrue(TEXT("and the second one was the undo"), Recorder.Seen[1] == EDreamUIDocumentChangeReason::UndoRedo);
	}

	Document->OnTextChanged().RemoveAll(&Recorder);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIDocumentRedoRestoresTextTest,
	"DreamGUI.Designer.RedoPutsTheEditBackIntoTheFileToo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIDocumentRedoRestoresTextTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIDocumentTestLocal;
	if (!HasTransactionBuffer(*this)) return false;

	FScopedDuiFile File(TEXT("RedoRestoresText.dui"), TextA);

	FString Error;
	TStrongObjectPtr<UDreamUIDocument> Document(UDreamUIDocument::CreateFromFile(nullptr, File.Path, Error));
	if (!TestNotNull(TEXT("the document loaded"), (UObject*)Document.Get())) return false;

	GEditor->BeginTransaction(FText::FromString(TEXT("Test Edit")));
	Document->SetContent(TextB, Error);
	GEditor->EndTransaction();

	GEditor->UndoTransaction();
	if (!TestEqualSensitive(TEXT("undo took the file back"), File.ReadBack(), FString(TextA))) return false;

	GEditor->RedoTransaction();

	TestEqualSensitive(TEXT("redo restores the text in memory"), Document->GetContent(), FString(TextB));
	// Redo arrives through the same PostEditUndo -- UObject has no PostEditRedo, and FTransaction::Apply
	// serves both directions -- so this is the assertion that says that one override really covers both.
	TestEqualSensitive(TEXT("and puts it back on disk"), File.ReadBack(), FString(TextB));
	TestFalse(TEXT("with nothing left owed"), Document->HasUnflushedWrite());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIDocumentOwnWriteTest,
	"DreamGUI.Designer.ADocumentTellsItsOwnWriteFromAForeignOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIDocumentOwnWriteTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIDocumentTestLocal;
	if (!HasTransactionBuffer(*this)) return false;

	FScopedDuiFile File(TEXT("OwnWrite.dui"), TextA);

	FString Error;
	TStrongObjectPtr<UDreamUIDocument> Document(UDreamUIDocument::CreateFromFile(nullptr, File.Path, Error));
	if (!TestNotNull(TEXT("the document loaded"), (UObject*)Document.Get())) return false;

	const FString HashAtLoad = Document->GetLastWrittenHash();
	TestFalse(TEXT("loading gives the document something to compare against"), HashAtLoad.IsEmpty());
	TestEqual(TEXT("and it is the hash of what it read"), HashAtLoad, UDreamUIDocument::ComputeContentHash(TextA));

	GEditor->BeginTransaction(FText::FromString(TEXT("Test Edit")));
	Document->SetContent(TextB, Error);
	GEditor->EndTransaction();

	TestNotEqual(TEXT("writing moves the hash on"), Document->GetLastWrittenHash(), HashAtLoad);
	// The actual watcher question: the notification arrives, the host reads the file and asks
	// whether this is its own write coming back. Without the skip it reloads, regenerates, writes,
	// and is notified again -- forever.
	TestTrue(TEXT("the file the watcher would report is recognised as our own write"), Document->IsOwnWrite(File.ReadBack()));

	File.WriteBehindTheDocument(TEXT("Panel Root { AnchorData.SizeDelta = (100, 100) }\n"));
	TestFalse(TEXT("but somebody else's write is not"), Document->IsOwnWrite(File.ReadBack()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIDocumentNonAsciiTest,
	"DreamGUI.Designer.ANonAsciiDocumentSurvivesTheRoundTripAndHashesApart",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIDocumentNonAsciiTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIDocumentTestLocal;
	if (!HasTransactionBuffer(*this)) return false;

	// Labels are the normal case in a UI file, and in this project they are Chinese. Written as
	// escapes so this file stays pure ASCII and no toolchain's idea of a source charset can change
	// what is being tested. U+5B58 U+6863 is a save-slot label; U+8BFB U+6863 is a load-slot one.
	const FString SaveLabel = TEXT("Text Title { text: \"\u5B58\u6863\" }\n");
	const FString LoadLabel = TEXT("Text Title { text: \"\u8BFB\u6863\" }\n");
	// Two files differing ONLY in those characters must not hash the same. FMD5::HashAnsiString
	// would collapse both to '?' and the watcher suppression above would then swallow a real
	// foreign edit -- a skipped reload nobody can see, on the files most likely to be hand-edited.
	TestNotEqual(TEXT("two labels that differ only in CJK hash apart"),
		UDreamUIDocument::ComputeContentHash(SaveLabel), UDreamUIDocument::ComputeContentHash(LoadLabel));

	FScopedDuiFile File(TEXT("NonAscii.dui"), TextA);

	FString Error;
	TStrongObjectPtr<UDreamUIDocument> Document(UDreamUIDocument::CreateFromFile(nullptr, File.Path, Error));
	if (!TestNotNull(TEXT("the document loaded"), (UObject*)Document.Get())) return false;

	GEditor->BeginTransaction(FText::FromString(TEXT("Test Edit")));
	Document->SetContent(SaveLabel, Error);
	GEditor->EndTransaction();

	TestEqualSensitive(TEXT("the characters come back off disk unchanged"), File.ReadBack(), SaveLabel);
	TestTrue(TEXT("and the round trip is recognised as our own write"), Document->IsOwnWrite(File.ReadBack()));

	// The encoding is pinned, not merely "whatever survived". SaveStringToFile's AutoDetect default
	// writes ANSI while the text happens to be pure ASCII and switches to UTF-16-with-BOM at the
	// first CJK character, so a .dui would change encoding the day somebody typed a label.
	TArray<uint8> Bytes;
	if (TestTrue(TEXT("the raw file can be read"), FFileHelper::LoadFileToArray(Bytes, *File.Path)) && Bytes.Num() >= 3)
	{
		const bool bHasUtf16Bom = (Bytes[0] == 0xFF && Bytes[1] == 0xFE) || (Bytes[0] == 0xFE && Bytes[1] == 0xFF);
		const bool bHasUtf8Bom = (Bytes[0] == 0xEF && Bytes[1] == 0xBB && Bytes[2] == 0xBF);
		TestFalse(TEXT("the file did not turn into UTF-16"), bHasUtf16Bom);
		TestFalse(TEXT("and carries no byte order mark"), bHasUtf8Bom);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIDocumentReadOnlyTest,
	"DreamGUI.Designer.AReadOnlyFileKeepsTheEditAndStillOwesTheWrite",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIDocumentReadOnlyTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIDocumentTestLocal;
	if (!HasTransactionBuffer(*this)) return false;

	FScopedDuiFile File(TEXT("ReadOnly.dui"), TextA);

	FString Error;
	TStrongObjectPtr<UDreamUIDocument> Document(UDreamUIDocument::CreateFromFile(nullptr, File.Path, Error));
	if (!TestNotNull(TEXT("the document loaded"), (UObject*)Document.Get())) return false;

	// What a source-controlled project looks like before the file is checked out.
	File.SetReadOnly(true);
	if (!TestTrue(TEXT("the file really is read-only"), Document->IsFileReadOnly())) return false;

	GEditor->BeginTransaction(FText::FromString(TEXT("Test Edit")));
	const bool bSet = Document->SetContent(TextB, Error);
	GEditor->EndTransaction();

	// The chosen semantics: the edit is kept and the write is owed, never dropped. Dropping it would
	// put the document back into exactly the divergence this class exists to close -- memory saying
	// one thing, the file another, and nothing to say so.
	TestFalse(TEXT("the write is reported as having failed"), bSet);
	TestFalse(TEXT("and says why"), Error.IsEmpty());
	TestEqualSensitive(TEXT("but the edit is kept"), Document->GetContent(), FString(TextB));
	TestTrue(TEXT("and remembered as owed"), Document->HasUnflushedWrite());
	TestEqualSensitive(TEXT("while the file still holds the old text"), File.ReadBack(), FString(TextA));

	// Checking the file out is the user's move, not ours: nothing in the document clears the
	// read-only bit, because on a Perforce project that bit is what "not checked out" means.
	File.SetReadOnly(false);
	TestTrue(TEXT("the next flush pays the debt"), Document->FlushToDisk(Error));
	TestEqualSensitive(TEXT("the file catches up"), File.ReadBack(), FString(TextB));
	TestFalse(TEXT("and nothing is owed any more"), Document->HasUnflushedWrite());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIDocumentUndoCancelsOwedWriteTest,
	"DreamGUI.Designer.UndoingBackToTheFilesTextCancelsTheOwedWrite",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIDocumentUndoCancelsOwedWriteTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIDocumentTestLocal;
	if (!HasTransactionBuffer(*this)) return false;

	FScopedDuiFile File(TEXT("UndoCancelsOwedWrite.dui"), TextA);

	FString Error;
	TStrongObjectPtr<UDreamUIDocument> Document(UDreamUIDocument::CreateFromFile(nullptr, File.Path, Error));
	if (!TestNotNull(TEXT("the document loaded"), (UObject*)Document.Get())) return false;

	File.SetReadOnly(true);

	GEditor->BeginTransaction(FText::FromString(TEXT("Test Edit")));
	Document->SetContent(TextB, Error);
	GEditor->EndTransaction();
	if (!TestTrue(TEXT("the refused write is owed"), Document->HasUnflushedWrite())) return false;

	// Undo takes the text back to what the file already holds. There is then nothing to write, so
	// the undo must NOT fail on a still-read-only file and must not keep nagging about a debt that
	// no longer exists. This is why the flush compares hashes instead of always writing -- it is
	// also the one path on which a read-only file resolves itself.
	GEditor->UndoTransaction();

	TestEqualSensitive(TEXT("the text is back"), Document->GetContent(), FString(TextA));
	TestEqualSensitive(TEXT("the file was already right"), File.ReadBack(), FString(TextA));
	TestFalse(TEXT("and the owed write is cancelled rather than retried forever"), Document->HasUnflushedWrite());

	File.SetReadOnly(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIDocumentNoTransactionTest,
	"DreamGUI.Designer.AnEditOutsideATransactionSaysSoInsteadOfLosingTheUndo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIDocumentNoTransactionTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIDocumentTestLocal;
	if (!HasTransactionBuffer(*this)) return false;

	FScopedDuiFile File(TEXT("NoTransaction.dui"), TextA);

	FString Error;
	TStrongObjectPtr<UDreamUIDocument> Document(UDreamUIDocument::CreateFromFile(nullptr, File.Path, Error));
	if (!TestNotNull(TEXT("the document loaded"), (UObject*)Document.Get())) return false;

	// If something upstream leaked an open transaction, Modify() would succeed and the expected
	// error below would never fire -- which would read as a failure of this test rather than of
	// whatever leaked it.
	if (!TestFalse(TEXT("no transaction is open when this case starts"), GEditor->IsTransactionActive()))
	{
		return false;
	}

	// The document does not open a transaction of its own: doing that would fence the text change
	// into its own undo entry, away from whatever object edits the caller made in the same action,
	// and leave a half-undone state nobody can get back out of. It complains instead, and the
	// complaint is the thing under test -- an edit that quietly cannot be undone is how undo bugs
	// stay hidden. Narrow phrase on purpose: an expected-error pattern swallows everything it
	// matches, including assertion messages.
	AddExpectedError(TEXT("cannot be undone"), EAutomationExpectedErrorFlags::Contains, 1);

	const bool bSet = Document->SetContent(TextB, Error);

	TestTrue(TEXT("the edit is still applied rather than thrown away"), bSet);
	TestEqualSensitive(TEXT("the document holds the new text"), Document->GetContent(), FString(TextB));
	TestEqualSensitive(TEXT("and the file has it"), File.ReadBack(), FString(TextB));
	return true;
}

#endif
