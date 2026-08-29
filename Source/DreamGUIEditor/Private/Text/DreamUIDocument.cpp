// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Text/DreamUIDocument.h"

#include "DreamGUIEditorModule.h"

#include "Containers/StringConv.h"
#include "Containers/UnrealString.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Templates/UnrealTemplate.h"
#include "UObject/UObjectGlobals.h"

UDreamUIDocument::UDreamUIDocument()
{
	// Not left to the caller's NewObject flags. Modify() is a silent no-op on an object without
	// RF_Transactional -- SaveToTransactionBuffer checks the flag and returns false
	// (UObjectGlobals.cpp:3215) -- so a document created without it edits, saves and reloads
	// perfectly while contributing nothing to the undo stack. That is the exact failure this class
	// was written to prevent, and it would be invisible until someone pressed Ctrl+Z.
	SetFlags(RF_Transactional);
}

UDreamUIDocument* UDreamUIDocument::CreateFromFile(UObject* InOuter, const FString& InAbsoluteFilePath, FString& OutError)
{
	OutError.Reset();
	if (InAbsoluteFilePath.IsEmpty())
	{
		OutError = TEXT("no file path was given");
		return nullptr;
	}

	UDreamUIDocument* Document = NewObject<UDreamUIDocument>(
		InOuter != nullptr ? InOuter : GetTransientPackageAsObject(), NAME_None, RF_Transactional);
	Document->SetFilePath(InAbsoluteFilePath);
	if (!Document->LoadFromFile(OutError))
	{
		return nullptr;
	}
	return Document;
}

void UDreamUIDocument::SetFilePath(const FString& InAbsoluteFilePath)
{
	FilePath = InAbsoluteFilePath;
	FPaths::NormalizeFilename(FilePath);
	// The old file's hash says nothing about the new file. Leaving it would let IsOwnWrite answer
	// "yes, mine" about a document we have never written, and the host would skip a real reload.
	LastWrittenHash.Reset();
	bHasUnflushedWrite = !Content.IsEmpty();
}

bool UDreamUIDocument::LoadFromFile(FString& OutError)
{
	OutError.Reset();
	if (FilePath.IsEmpty())
	{
		OutError = TEXT("this document is not bound to a file");
		return false;
	}

	FString Loaded;
	if (!FFileHelper::LoadFileToString(Loaded, *FilePath))
	{
		OutError = FString::Printf(TEXT("could not read '%s'"), *FilePath);
		return false;
	}

	// No Modify() here, and no transaction: see the header. A reload is how a change made outside
	// the editor arrives, and an undo that overwrites someone else's file is a worse outcome than a
	// foreign edit that is not undoable.
	Content = MoveTemp(Loaded);
	LastWrittenHash = ComputeContentHash(Content);
	bHasUnflushedWrite = false;

	Broadcast(EDreamUIDocumentChangeReason::Reloaded);
	return true;
}

bool UDreamUIDocument::SetContent(const FString& InContent, FString& OutError)
{
	OutError.Reset();

	if (bIsBroadcasting)
	{
		// A handler writing back into the document from inside OnTextChanged is a loop:
		// regenerate -> mirror -> SetContent -> regenerate. Refusing here turns an editor hang into
		// one line in the log naming the handler's own call.
		OutError = TEXT("the document is broadcasting a change; a handler must not write back into it");
		UE_LOG(DreamGUIEditor, Warning, TEXT("[%s].%d %s ('%s')"),
			ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *OutError, *FilePath);
		return false;
	}

	if (InContent.Equals(Content, ESearchCase::CaseSensitive))
	{
		// Nothing changed, so nothing goes on the undo stack. An entry that describes no change is
		// worse than no entry: Ctrl+Z appears to do nothing, and the user presses it again and loses
		// the edit before it -- the shape already pinned in DreamUIEditorUndoAutomationTests.
		// Still flush, because the previous write may be owed.
		return FlushToDisk(OutError);
	}

	// Modify() BEFORE the assignment. It snapshots the object as it is right now; called after, it
	// records the new text as the thing to restore and undo becomes a no-op that still reports
	// success everywhere.
	const bool bRecorded = Modify();
	if (!bRecorded)
	{
		// Not fatal -- the edit is still applied, because losing the user's edit to enforce a rule
		// about undo would be a strange trade -- but it is never silent. See SetContent's header
		// comment for why this class refuses to open a transaction of its own here.
		UE_LOG(DreamGUIEditor, Error,
			TEXT("[%s].%d '%s' was edited outside a transaction; this change cannot be undone. Callers must own the transaction (FScopedTransaction around the whole action)."),
			ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *FilePath);
	}

	Content = InContent;

	const bool bFlushed = FlushToDisk(OutError);
	Broadcast(EDreamUIDocumentChangeReason::Edited);
	return bFlushed;
}

bool UDreamUIDocument::FlushToDisk(FString& OutError, EDreamUIDocumentFlush InMode)
{
	OutError.Reset();
	if (FilePath.IsEmpty())
	{
		OutError = TEXT("this document is not bound to a file");
		bHasUnflushedWrite = true;
		return false;
	}

	const FString Hash = ComputeContentHash(Content);
	if (InMode == EDreamUIDocumentFlush::OnlyIfDiskDisagrees && Hash == LastWrittenHash)
	{
		// The file is believed to hold exactly this text already, so nothing is owed -- including
		// the case where an earlier write failed and undo has since taken the text back to what the
		// file has. Undo cancelling an owed write is the behaviour we want: it is the one path where
		// a read-only file resolves itself instead of nagging.
		bHasUnflushedWrite = false;
		return true;
	}

	if (IsFileReadOnly())
	{
		OutError = FString::Printf(
			TEXT("'%s' is read-only; check it out of source control. The edit is kept in memory and will be written on the next flush."),
			*FilePath);
		bHasUnflushedWrite = true;
		return false;
	}

	// ForceUTF8WithoutBOM, never the AutoDetect default. AutoDetect writes ANSI while the text
	// happens to be pure ASCII and switches to UTF-16-with-BOM the moment one CJK character appears
	// (FileHelper.cpp:787) -- so a .dui would silently change encoding mid-life, the first Chinese
	// label would turn the file into something git diffs as binary, and every external tool that
	// opened it would have to guess. One encoding, chosen once.
	// No FILEWRITE_EvenIfReadOnly: see the header. Read-only means "not checked out", not "locked
	// by accident".
	if (!FFileHelper::SaveStringToFile(Content, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		OutError = FString::Printf(TEXT("could not write '%s'"), *FilePath);
		bHasUnflushedWrite = true;
		return false;
	}

	LastWrittenHash = Hash;
	bHasUnflushedWrite = false;
	return true;
}

bool UDreamUIDocument::IsFileReadOnly() const
{
	if (FilePath.IsEmpty())
	{
		return false;
	}
	IFileManager& FileManager = IFileManager::Get();
	return FileManager.FileExists(*FilePath) && FileManager.IsReadOnly(*FilePath);
}

FString UDreamUIDocument::ComputeContentHash(const FString& InContent)
{
	// The UTF-8 bytes, which is what the file holds. FMD5::HashAnsiString would be one line shorter
	// and wrong: it goes through TCHAR_TO_ANSI, so every non-ASCII character becomes '?' and two
	// files that differ only in their Chinese labels hash the same. This hash decides whether the
	// watcher skips a reload, so a collision there means a real edit from outside is swallowed.
	const FTCHARToUTF8 Utf8(*InContent, InContent.Len());

	FMD5 Md5;
	if (Utf8.Length() > 0)
	{
		Md5.Update(reinterpret_cast<const uint8*>(Utf8.Get()), static_cast<uint64>(Utf8.Length()));
	}
	uint8 Digest[16];
	Md5.Final(Digest);
	return BytesToHex(Digest, static_cast<int32>(UE_ARRAY_COUNT(Digest)));
}

bool UDreamUIDocument::IsOwnWrite(const FString& InFileContent) const
{
	// An empty hash means we have never put anything on disk (or have just been pointed at a
	// different file), so nothing arriving can be our own write. Answering "yes" from an empty hash
	// would make the very first foreign change invisible.
	return !LastWrittenHash.IsEmpty() && ComputeContentHash(InFileContent) == LastWrittenHash;
}

#if WITH_EDITOR

void UDreamUIDocument::PostEditUndo()
{
	Super::PostEditUndo();

	// Disk first, then the broadcast. The host regenerates the tree from Content when it hears the
	// broadcast, and anything downstream of that regeneration which reads the .dui from the file --
	// a recompile, a nested document, a tool that re-parses -- must not see the pre-undo text.
	FString Error;
	if (!FlushToDisk(Error))
	{
		// Reported, not swallowed, and the write stays owed (HasUnflushedWrite). There is no way to
		// refuse an undo from here: by the time this runs the restore has already happened.
		UE_LOG(DreamGUIEditor, Error, TEXT("[%s].%d undo could not reach the file: %s"),
			ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *Error);
	}

	Broadcast(EDreamUIDocumentChangeReason::UndoRedo);
}

void UDreamUIDocument::PostEditUndo(TSharedPtr<ITransactionObjectAnnotation> InTransactionAnnotation)
{
	// Deliberately NOT Super::PostEditUndo(InTransactionAnnotation): the base implementation forwards
	// to `UObject::PostEditUndo()` by QUALIFIED name (Obj.cpp:895), so it runs the base version and
	// skips everything above. Forward virtually instead, so a subclass of this document is not
	// skipped the same way.
	PostEditUndo();
}

#endif // WITH_EDITOR

void UDreamUIDocument::Broadcast(EDreamUIDocumentChangeReason InReason)
{
	if (bIsBroadcasting)
	{
		UE_LOG(DreamGUIEditor, Warning,
			TEXT("[%s].%d '%s' changed again while its change was being broadcast; the nested notification is dropped. A handler of OnTextChanged is writing back into the document."),
			ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *FilePath);
		return;
	}

	TGuardValue<bool> BroadcastGuard(bIsBroadcasting, true);
	TextChangedDelegate.Broadcast(InReason);
}
