// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "DreamUIDocument.generated.h"

/** Why Content changed. The host regenerates the same way for all of them; this is for logging and for gating. */
UENUM()
enum class EDreamUIDocumentChangeReason : uint8
{
	/** SetContent: an editor action wrote new text. Recorded in the caller's transaction. */
	Edited,
	/**
	 * The transaction system put an older (or newer) Content back.
	 *
	 * Undo and redo are one value on purpose -- see the PostEditUndo comment: the engine does not
	 * tell us which one this was, and the document's response is identical either way.
	 */
	UndoRedo,
	/** LoadFromFile: the bytes on disk are now the truth, because someone outside the editor wrote them. */
	Reloaded,
};

DECLARE_MULTICAST_DELEGATE_OneParam(FDreamUIDocumentTextChanged, EDreamUIDocumentChangeReason /*Reason*/);

/** What FlushToDisk should do when it believes the file already holds Content. */
enum class EDreamUIDocumentFlush : uint8
{
	/**
	 * Skip the write when the content hash matches what we last put on (or read from) disk.
	 *
	 * This is what makes undo cheap AND makes the read-only story behave: undoing back to the text
	 * the file already has must not fail just because the file is checked in.
	 */
	OnlyIfDiskDisagrees,
	/** Write regardless. For the case where a foreign edit landed and the in-memory text is meant to win. */
	Always,
};

/**
 * The full text of one `.dui` file, held in a UObject so that UE's transaction system can undo it.
 *
 * This class exists for exactly one reason, and it is not "somewhere to keep a string".
 *
 * Under the `.dui` direction the text is the only truth and the designer is a graphical front end
 * for it: a property edit becomes `write the preview object -> patch the .dui -> regenerate the
 * tree`. UE's undo restores OBJECTS. So with the text left outside the transaction, Ctrl+Z puts
 * the widget back, the line in the file still says the new value, and the next regeneration --
 * a recompile, a reopen, the next unrelated edit -- brings the change back. The undo appears to
 * work and then silently undoes itself. Putting Content inside a transaction is what buys the
 * whole undo stack for free, and it is why this cannot be deferred until after write-back exists:
 * write-back built on "edit the object and also write the file" has to be torn out to add it.
 *
 * Deliberately knows nothing about `.dui` syntax. It moves bytes and it participates in undo; the
 * parser, the builder and the patcher live elsewhere and are reached through OnTextChanged. That
 * keeps this testable and landable while the front end is still being written, and it means a
 * malformed file is still a document you can edit your way out of rather than a document that
 * refuses to exist.
 *
 * Two contracts a caller has to know, both chosen rather than fallen into -- the reasoning is on
 * SetContent and on PostEditUndo respectively:
 *
 *   1. THE CALLER OWNS THE TRANSACTION. SetContent only calls Modify(); it never opens one.
 *   2. MEMORY IS THE TRUTH, DISK IS A PROJECTION THAT MAY LAG. A write that fails (read-only file,
 *      not checked out) keeps the edit, says so, and is retried on the next flush.
 */
UCLASS(Transient)
class DREAMGUIEDITOR_API UDreamUIDocument : public UObject
{
	GENERATED_BODY()

public:
	UDreamUIDocument();

	// ---------------------------------------------------------------------------------------------
	// Loading
	// ---------------------------------------------------------------------------------------------

	/**
	 * Read InAbsoluteFilePath and hand back a document for it, or null with OutError saying why.
	 *
	 * InOuter may be null, in which case the transient package owns it. The outer decides lifetime
	 * and nothing else here: UCLASS(Transient) forces RF_Transient onto every instance
	 * (UObjectGlobals.cpp:3740), and MarkPackageDirty walks out of a transient outer chain without
	 * dirtying anything -- so a text edit never marks the owning asset dirty, which is what we want,
	 * because the file is the truth and the asset's tree is regenerated from it.
	 *
	 * RF_Transient does NOT keep the document out of the undo stack, which is the reasonable thing
	 * to fear here: SaveToTransactionBuffer looks at RF_Transactional and the script-package flag,
	 * and at nothing else (UObjectGlobals.cpp:3215).
	 */
	static UDreamUIDocument* CreateFromFile(UObject* InOuter, const FString& InAbsoluteFilePath, FString& OutError);

	/**
	 * (Re)read the file this document is bound to, replacing Content and resetting the disk belief.
	 *
	 * NOT transacted, on purpose. A reload is how a change made OUTSIDE the editor arrives -- an AI
	 * writing the file, a merge, a hand edit. Recording that on the undo stack would mean a Ctrl+Z
	 * silently overwrites someone else's file with text they never wrote, which is worse than a
	 * foreign change simply not being undoable.
	 *
	 * KNOWN HOLE, and it belongs to the host rather than to this class: entries already on the undo
	 * stack describe text from BEFORE the reload. Undoing past a reload therefore writes pre-reload
	 * text over the foreign edit. Nothing here can see that -- the transaction system hands us a
	 * restored string with no history attached. The host's move when it accepts a foreign change is
	 * GEditor->Trans->SetUndoBarrier(), so undo stops at the reload instead of stepping over it.
	 */
	bool LoadFromFile(FString& OutError);

	/** Point this document at a file without reading it. For a document that is about to be created. */
	void SetFilePath(const FString& InAbsoluteFilePath);

	/** Absolute path of the `.dui` this document is the text of. Empty until bound. */
	const FString& GetFilePath() const { return FilePath; }

	/** The full text. This, not the file, is what the tree is regenerated from. */
	const FString& GetContent() const { return Content; }

	// ---------------------------------------------------------------------------------------------
	// Editing
	// ---------------------------------------------------------------------------------------------

	/**
	 * Record the current text on the undo stack, replace it with InContent, write the file, broadcast.
	 *
	 * THE CALLER MUST ALREADY BE INSIDE A TRANSACTION. This calls Modify() and nothing else; it will
	 * not open one for you, and it complains loudly (rather than silently producing a non-undoable
	 * edit) when there is no transaction to join.
	 *
	 * Why not open one when there isn't one, which is the obvious convenience:
	 *
	 *   - For the path that matters it buys nothing. The details panel is already inside a
	 *     transaction by the time it reaches us -- the NotifyPreChange pass exists to snapshot for
	 *     undo -- and UTransBuffer::Begin folds a nested Begin into the outer transaction by a
	 *     counter (`if (ActiveCount++ == 0)`, TransBuffer.h). The extra Begin/End pair would be two
	 *     no-ops and a Description nobody ever sees.
	 *   - For the path that doesn't have one it is actively harmful. A tool-menu caller that edits
	 *     objects AND text would get the text change fenced into a transaction of its own, so the
	 *     two halves of one user action land on different undo entries. One Ctrl+Z then leaves the
	 *     tree regenerated from old text while the objects still hold the new values -- a state no
	 *     amount of further undoing produces again, and one nobody would think to look for.
	 *
	 * A half-undone state is worse than an error, so this reports instead of guessing. Callers with
	 * no transaction of their own want FScopedTransaction around their whole action -- which is what
	 * they should have had anyway, for their object edits.
	 *
	 * Returns false when the FILE could not be written; the edit is applied and recorded regardless
	 * and HasUnflushedWrite() then reports true. OutError says which of the two went wrong.
	 */
	bool SetContent(const FString& InContent, FString& OutError);

	// ---------------------------------------------------------------------------------------------
	// Disk
	// ---------------------------------------------------------------------------------------------

	/**
	 * Put Content on disk. Idempotent, and by default a no-op when the file is believed to agree.
	 *
	 * Never clears the read-only bit. On a source-controlled project read-only means "not checked
	 * out", and a text editor that quietly makes files writable behind Perforce's back produces the
	 * worst kind of merge: a change nobody has a changelist for.
	 */
	bool FlushToDisk(FString& OutError, EDreamUIDocumentFlush InMode = EDreamUIDocumentFlush::OnlyIfDiskDisagrees);

	/**
	 * True when Content has not reached the file -- the write was refused and is still owed.
	 *
	 * The host should treat this as "do not trust the file": anything that reads the `.dui` from
	 * disk (a compile, a reopen) is reading text one edit behind, and the fix is for the user to
	 * check the file out, not for us to force it.
	 */
	bool HasUnflushedWrite() const { return bHasUnflushedWrite; }

	/** True when the file exists and the OS says it cannot be written. Cheap; hits the filesystem. */
	bool IsFileReadOnly() const;

	// ---------------------------------------------------------------------------------------------
	// Watcher self-excitation (plan section 4.3)
	// ---------------------------------------------------------------------------------------------

	/**
	 * Hash of a piece of `.dui` text. Both sides of the watcher must use THIS, not their own.
	 *
	 * Hashes the UTF-8 bytes of the string. Not FMD5::HashAnsiString, which routes through
	 * TCHAR_TO_ANSI and turns every non-ASCII character into '?' -- two files differing only in
	 * their Chinese labels would hash identically, and the suppression below would then swallow a
	 * real foreign edit. Text with CJK in it is the normal case here, not the exotic one.
	 */
	static FString ComputeContentHash(const FString& InContent);

	/** Hash of the text this document last successfully wrote to, or read from, the file. */
	const FString& GetLastWrittenHash() const { return LastWrittenHash; }

	/**
	 * "Is this file content just my own write coming back at me?"
	 *
	 * DirectoryWatcher reports the write we made ourselves, and reacting to it means reload ->
	 * regenerate -> write -> watcher -> forever. The host asks this before reloading and skips on
	 * true. Wiring the watcher is the host's job; the hash is ours so there is one definition of it.
	 */
	bool IsOwnWrite(const FString& InFileContent) const;

	// ---------------------------------------------------------------------------------------------
	// Notification
	// ---------------------------------------------------------------------------------------------

	/**
	 * Fires whenever Content changed and the tree needs regenerating -- edits included, not just undo.
	 *
	 * One notification for every cause on purpose. The alternative (the caller regenerates after its
	 * own SetContent, and only undo goes through the delegate) is two regeneration paths that have
	 * to stay in step, which is the same shape as the one-builder-two-endpoints rule the text
	 * pipeline is built on. Handlers must not write back into the document while handling; a
	 * re-entrant change is refused with a warning rather than allowed to recurse.
	 */
	FDreamUIDocumentTextChanged& OnTextChanged() { return TextChangedDelegate; }

	// ---------------------------------------------------------------------------------------------
	// UObject
	// ---------------------------------------------------------------------------------------------

	/**
	 * Content has just been restored by undo OR redo: put the restored text back on the file and
	 * tell the host to regenerate.
	 *
	 * Two engine facts this is shaped around, both checked in 5.8 rather than assumed:
	 *
	 *   - THERE IS NO UObject::PostEditRedo. PostEditUndo is called from FTransaction::Apply, which
	 *     is what both Undo() and Redo() run (EditorTransaction.cpp:957), so this one override
	 *     covers both directions and cannot tell them apart. Distinguishing them needs
	 *     FEditorUndoClient::PostUndo/PostRedo, an editor-side interface with a registration
	 *     lifetime -- not worth it while the response is byte-identical either way.
	 *   - UObject::PostEditUndo(TSharedPtr<ITransactionObjectAnnotation>) calls
	 *     `UObject::PostEditUndo()` by QUALIFIED name, not virtually (Obj.cpp:893-896). Overriding
	 *     only the no-argument version means the override is skipped for any object the transaction
	 *     restored with an annotation. We have no annotation today, so both overloads are here for
	 *     the day something gives us one -- a silent skip on the undo path is the exact failure this
	 *     class exists to prevent.
	 *
	 * Writing the file from here means Ctrl+Z touches the filesystem, and a read-only file makes
	 * that write fail. THE CHOSEN SEMANTICS: the undo always applies in memory (there is no way to
	 * refuse one at this point -- by the time PostEditUndo runs the restore has happened), the
	 * failure is reported, and the write is remembered as owed and retried at the next flush.
	 * See HasUnflushedWrite. The alternative -- try once and drop it -- reintroduces exactly the
	 * divergence this class exists to close, just one step further along.
	 */
#if WITH_EDITOR
	virtual void PostEditUndo() override;
	virtual void PostEditUndo(TSharedPtr<ITransactionObjectAnnotation> InTransactionAnnotation) override;
#endif

private:
	/**
	 * The whole `.dui` file.
	 *
	 * A plain UPROPERTY, and it must stay one: this being in the transaction record is the entire
	 * point of the class. Marking it Transient would be harmless (a transacting archive is not
	 * persistent, so transient properties are still recorded) but marking it NonTransactional would
	 * silently remove it from undo and leave every test that checks the object passing.
	 */
	UPROPERTY()
	FString Content;

	/** Absolute path. Not a UPROPERTY: rebinding a document to a different file is not an undoable edit. */
	FString FilePath;

	/** Hash of what we believe the file holds. Not transacted; it describes the disk, not the edit. */
	FString LastWrittenHash;

	/** Content has not reached the file. See HasUnflushedWrite. */
	bool bHasUnflushedWrite = false;

	/** Guards against a handler writing back into the document from inside the broadcast. */
	bool bIsBroadcasting = false;

	FDreamUIDocumentTextChanged TextChangedDelegate;

	/** Fire OnTextChanged once, refusing to nest. Every path that changes Content ends here. */
	void Broadcast(EDreamUIDocumentChangeReason InReason);
};
