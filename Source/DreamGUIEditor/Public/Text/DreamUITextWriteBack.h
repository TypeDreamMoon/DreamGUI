// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Text/DreamUIDocument.h"
#include "Text/DreamUITextPatcher.h"
#include "UObject/StrongObjectPtr.h"

class FDreamWidgetPreviewHost;
class UDreamWidget;
class UDreamWidgetTree;

/**
 * One document per FILE, not one per editor.
 *
 * UDreamUIDocument's header names the hazard and leaves the fix to whoever owns the documents, so
 * this is that owner. Two toolkits opened on one `.dui` with a document each hold two Contents and
 * two disk hashes: A's undo writes the file, B's watcher sees text B has never written, B reloads
 * and pushes its own idea back, and alternating Ctrl+Z between the two windows never converges.
 * Nothing inside the document can see that -- from in there, both are behaving perfectly.
 *
 * The key is the normalised absolute path, because the same file reached as `I:/P/UI/Login.dui`
 * and `I:\P\UI\..\UI\Login.dui` is the same file and a map keyed on the spelling would not say so.
 *
 * THE MAP'S REFERENCE IS WEAK, and that is not an optimisation. A strong map would keep every
 * document ever opened alive for the life of the process, so a `.dui` edited outside the editor
 * after its window closed would come back to a document still holding the old text and still
 * believing the old hash -- the stale-editor failure the class model was built to end, rebuilt one
 * level up. Users hold the strong reference (FDreamUIDocumentHandle), the entry is dropped when the
 * last one goes, and the next open reads the file again.
 */
struct DREAMGUIEDITOR_API FDreamUIDocumentRegistry
{
	/** Absolute, forward slashes, collapsed relative segments. The key, and the only spelling of it. */
	static FString NormalizePath(const FString& InFilePath);

	/** The document for this file if one is open, without opening one. Null otherwise. */
	static UDreamUIDocument* Find(const FString& InFilePath);

	/** How many files currently have a document. Zero once every handle has gone. */
	static int32 NumTracked();

	/**
	 * Take out a reference, creating and loading the document when this is the first one.
	 *
	 * Prefer FDreamUIDocumentHandle, which pairs this with its Release. A caller that uses these
	 * directly owns the pairing, and an unpaired Acquire leaks an entry that later opens will keep
	 * handing out no matter what the file says.
	 */
	static UDreamUIDocument* Acquire(const FString& InFilePath, FString& OutError);

	/** Give a reference back. The entry disappears with the last one. */
	static void Release(const FString& InFilePath);
};

/**
 * A reference to the one document for a file, released when it goes out of scope.
 *
 * Strong on purpose: the registry's own reference is weak (see above), so this is what actually
 * keeps the document -- and everything the transaction buffer recorded about it -- alive for as
 * long as somebody is editing that file.
 */
class DREAMGUIEDITOR_API FDreamUIDocumentHandle
{
public:
	FDreamUIDocumentHandle() = default;
	~FDreamUIDocumentHandle();

	FDreamUIDocumentHandle(FDreamUIDocumentHandle&& InOther);
	FDreamUIDocumentHandle& operator=(FDreamUIDocumentHandle&& InOther);

	// Copying would double-release: the count is per handle, not per pointer.
	FDreamUIDocumentHandle(const FDreamUIDocumentHandle&) = delete;
	FDreamUIDocumentHandle& operator=(const FDreamUIDocumentHandle&) = delete;

	/** Open (or join) the document for this file. Invalid on failure, with OutError saying why. */
	static FDreamUIDocumentHandle Open(const FString& InFilePath, FString& OutError);

	UDreamUIDocument* Get() const { return Document.Get(); }
	bool IsValid() const { return Document.IsValid(); }

	/** The normalised path this handle was opened on. Empty when invalid. */
	const FString& GetFilePath() const { return FilePath; }

	void Reset();

private:
	FString FilePath;
	TStrongObjectPtr<UDreamUIDocument> Document;
};

/** The tree should be regenerated from the document's text, and this is why it changed. */
DECLARE_MULTICAST_DELEGATE_OneParam(FDreamUIWriteBackRebuildRequested, EDreamUIDocumentChangeReason /*Reason*/);

/**
 * The designer's edits, turned into lines of `.dui`, once per flush.
 *
 * This is the seam between the two halves of the text pipeline. Everything below it reads: the
 * parser makes an AST, the builder makes a tree, the compiler makes a class. This is the one thing
 * that goes the other way, and it is the reason `.dui` can be the single source of truth while the
 * designer stays a graphical front end for it rather than a second authoring surface that has to be
 * reconciled.
 *
 * ## The rule that shapes all of it: COMPARE VALUES, NEVER SOURCE TEXT
 *
 * FDreamUITextPatcher edits text slices, so handing it every property the designer might have
 * touched would rewrite lines whose value never changed: an author writes `(400,240)`, the printer
 * spells the identical value `(400, 240)`, and the line is replaced. Do that on the first flush of
 * every hand-written file -- which is what opening it in the designer is -- and the author's first
 * `.dui` diff is a page of noise they did not make and cannot review. After a week of that nobody
 * reads a `.dui` change at all, which costs more than the feature is worth.
 *
 * So a flush asks a different question than "what did the designer touch". It asks, for every
 * candidate property: WOULD A REBUILD FROM THE CURRENT TEXT PRODUCE THE VALUE THE TREE NOW HOLDS?
 * If yes, the file already says this and there is nothing to write, whatever the spelling.
 *
 * The baseline that question needs is not parsed here. It is a whole second tree, built from the
 * current text by the same FDreamUITextBuilder the compiler uses (see BuildReferenceTree). That
 * costs one tree per flush -- a gesture end, not a mouse move -- and buys the one thing a
 * hand-rolled comparison cannot: styles, class defaults, enum spellings, localisation and every
 * fallback in WriteValue are answered by the implementation that will actually build the file, not
 * by a second opinion that drifts from it. A `: Card` node whose FontSize comes from the style
 * compares against the STYLE's value, so opening the file writes nothing, and changing that font
 * size writes an override on the node rather than silently editing a style every other node shares.
 *
 * The two values are then compared as their CANONICAL PRINTED FORMS, not with FProperty::Identical.
 * Two reasons, both found the hard way:
 *
 *   - FTextProperty::Identical compares text identity, not the source string. Two FTexts built from
 *     one `.dui` line in two passes are not identical, so every localised property on every node
 *     would report a change forever.
 *   - FLinearColor's short form is quantised through sRGB (DreamUIValueFormat's header spells out
 *     the contract). A picked colour is not bit-equal to the colour its own hex code reads back as,
 *     so Identical would say "changed" on every flush for the rest of the file's life. Printing
 *     both sides folds that away exactly once, which is what `Print(Parse(Print(v))) == Print(v)`
 *     is for.
 *
 * A property whose live value has no `.dui` spelling at all -- an object reference, a struct with
 * no short form -- is skipped rather than guessed at. Writing a plausible wrong literal into the
 * author's file is the one outcome worth more than a missing feature.
 *
 * ## One flush is one undo step, or it is nothing
 *
 * Every edit of one flush is planned against ONE parse and applied in ONE FDreamUITextPatcher
 * batch, which is the patcher's own requirement: a source location describes the text as it was
 * parsed, and the first edit invalidates every location after it. One batch means one SetContent,
 * which means one entry on the undo stack for one gesture.
 *
 * And when nothing changed, there is no SetContent at all. An empty transaction is worse than no
 * transaction: Ctrl+Z appears to do nothing, so the user presses it again and loses the edit
 * before -- and it would also touch the file, so an idle editor would be a stream of writes for
 * the DirectoryWatcher to chew on.
 */
class DREAMGUIEDITOR_API FDreamUITextWriteBack : public TSharedFromThis<FDreamUITextWriteBack>
{
public:
	/**
	 * Bind a host and a file. Everything else is wiring the caller does not have to do.
	 *
	 * InHost may be null, and that is not a degenerate case worth refusing: the document half (the
	 * registry entry, the undo carrier, the deferred regeneration) is exactly as useful to a caller
	 * that supplies its own tree to FlushTree, which is how this is tested without standing up a
	 * preview world. Flush() with no host is a no-op that reports success, because "nothing to
	 * mirror" is not a failure.
	 */
	static TSharedPtr<FDreamUITextWriteBack> Create(const FString& InAbsoluteFilePath,
		const TSharedPtr<FDreamWidgetPreviewHost>& InHost, FString& OutError);

	~FDreamUITextWriteBack();

	UDreamUIDocument* GetDocument() const { return DocumentHandle.Get(); }
	const FString& GetFilePath() const { return DocumentHandle.GetFilePath(); }

	/**
	 * Mirror the host's authoring tree into the file. What OnTemplateChanged is wired to.
	 *
	 * Returns false only when something went wrong that the caller could act on -- the text does not
	 * parse, the tree does not build, the file could not be written. "Nothing had changed" returns
	 * true and writes nothing; so does having no host.
	 */
	bool Flush(FString& OutError);

	/** Flush an explicit tree. Flush() is this with the host's, and the two share every line below. */
	bool FlushTree(const UDreamWidgetTree* InLiveTree, FString& OutError);

	/**
	 * The document's text changed and the tree has to be rebuilt from it.
	 *
	 * Not fired for this object's own writes: a flush derives the text FROM the tree, so
	 * regenerating the tree from it would be a round trip whose only possible outcomes are "no
	 * change" and "a bug that eats the user's gesture halfway through it".
	 */
	FDreamUIWriteBackRebuildRequested& OnRebuildRequested() { return RebuildRequestedDelegate; }

	/**
	 * True when an undo left a regeneration owed to the next tick. See ProcessDeferredRebuild.
	 */
	bool IsRebuildPending() const { return bRebuildPending; }

	/**
	 * Run the regeneration an undo deferred, if there is one.
	 *
	 * UDreamUIDocument broadcasts from PostEditUndo, and PostEditUndo runs INSIDE the loop of
	 * FTransaction::Apply. The other objects of that same transaction -- the widgets whose values
	 * this text describes -- may not have been restored yet, so a tree rebuilt at that moment is
	 * built from the new text and then overwritten, property by property, by the rest of the
	 * restore. The result is a tree that matches neither state and no way to tell from inside the
	 * handler that it happened.
	 *
	 * So an undo only marks, and this runs on the next tick, when Apply has finished. It is public
	 * because a ticker is not something a test can wait for: the ticker calls exactly this.
	 */
	void ProcessDeferredRebuild();

	// -------------------------------------------------------------------------------------------
	// The pure half: text and trees in, text out. No document, no host, no editor.
	// -------------------------------------------------------------------------------------------

	/**
	 * The tree the current text describes, for the live tree to be compared against.
	 *
	 * Null when the text does not parse or does not build -- and then NOTHING is written, which is
	 * the only safe answer: without a baseline every property looks changed, and a flush would
	 * rewrite the whole file over an error the author is in the middle of fixing.
	 *
	 * The caller owns the result and must keep it alive (it is outered to the transient package)
	 * for as long as it compares against it.
	 */
	static UDreamWidgetTree* BuildReferenceTree(const FString& InText, FDreamUIAst& OutAst,
		FDreamUIDiagnosticBag& OutDiagnostics);

	/**
	 * Every property whose live value differs from what the text already says.
	 *
	 * InTextTree must be BuildReferenceTree(InText) and InAst the same parse, or the comparison is
	 * against something the file does not say. Nodes are paired between the two trees and the AST by
	 * the one name the language has -- UDreamWidget::GetDisplayName, which the builder sets from
	 * FDreamUINode::Id -- and the walk stops at each nested widget blueprint instance, whose
	 * contents belong to another file.
	 */
	static void CollectEdits(const FDreamUIAst& InAst, const UDreamWidgetTree* InLiveTree,
		const UDreamWidgetTree* InTextTree, TArray<FDreamUIPropertyEdit>& OutEdits,
		FDreamUIDiagnosticBag& OutDiagnostics);

	/**
	 * InText with the live tree's values in it. OutText equals InText when nothing had changed.
	 *
	 * Returns false only when no answer could be computed at all (the text does not parse or does
	 * not build). A property the patcher refused -- a binding, a named slot -- is reported in
	 * OutDiagnostics and does not stop the others: nine good writes and one complaint beats losing
	 * all ten to the tenth.
	 *
	 * OutEdits, when given, receives what was ATTEMPTED, refusals included. It exists so that the
	 * flush can report a count without running the pipeline a second time -- one implementation of
	 * a write-back is the whole point, and a second one kept for bookkeeping is how the two come to
	 * disagree about what a flush did.
	 */
	static bool ProduceText(const FString& InText, const UDreamWidgetTree* InLiveTree,
		FString& OutText, FDreamUIDiagnosticBag& OutDiagnostics,
		TArray<FDreamUIPropertyEdit>* OutEdits = nullptr);

	/**
	 * The property paths a designer gesture writes that a file usually does not mention.
	 *
	 * Everything else this considers is a property the TEXT already names, which is what keeps a
	 * flush from inserting the hundred reflected properties nobody wrote. Geometry is the exception
	 * that forces the list to exist: dragging a widget is the commonest edit there is, and the
	 * anchor block is exactly what the author did not have to type to get a sensible default.
	 *
	 * `AnchorData.*` and nothing else, deliberately. The other half of what a gesture writes --
	 * RelativeLocation, RelativeRotation, RelativeScale -- is an FVector and an FQuat, which the
	 * language has no spelling for; inventing one for a quaternion in a layout language is a
	 * decision for the grammar, not for the write-back.
	 */
	static const TArray<FString>& GetGeometryPropertyPaths();

	/**
	 * The panel-slot properties compared on every child of a layout, written or not.
	 *
	 * Alignment and padding are the first controls a designer touches, and they live on the slot --
	 * so gating them on the .dui already mentioning them would mean an edit that takes in the preview
	 * and never reaches the file. Kept as an allowlist because the slot also carries the layout's
	 * OUTPUT, and a computed value has no business in a source document.
	 */
	static const TArray<FString>& GetPanelSlotPropertyNames();

	/**
	 * Whether this property's value can be written as a literal this language can read back.
	 *
	 * The write-back walk asks it by printing and seeing whether anything came out; a details panel
	 * needs the same answer BEFORE offering the row, because a property that cannot be spelled is one
	 * the author can change, watch take effect in the preview, and lose at the next compile. That is
	 * the silent failure the whole pipeline exists to remove, and object references -- fonts,
	 * textures, materials -- are its most common shape.
	 *
	 * Exported so the panel and the walk cannot disagree. A second judge would grey rows the walk
	 * would have written, or offer rows it drops.
	 */
	static bool CanSpellAsLiteral(const FProperty* InLeaf, const void* InValuePtr);

	// -------------------------------------------------------------------------------------------
	// Observation. For tests and for the log; nothing about the feature depends on these.
	// -------------------------------------------------------------------------------------------

	/** How many times a flush actually wrote the document. The number a drag test asserts is 1. */
	int32 GetWriteCount() const { return WriteCount; }
	/** How many property edits the last flush produced, refusals included. */
	int32 GetLastEditCount() const { return LastEditCount; }
	/** What the last flush refused, in the patcher's own words. */
	const FDreamUIDiagnosticBag& GetLastDiagnostics() const { return LastDiagnostics; }

private:
	FDreamUITextWriteBack() = default;

	/** Subscribe to the host and the document. Separate from the constructor: both need SharedThis. */
	void Initialize(const TSharedPtr<FDreamWidgetPreviewHost>& InHost);

	void OnTemplateChanged();
	void OnDocumentTextChanged(EDreamUIDocumentChangeReason InReason);

	/** Ask for a regeneration now, or on the next tick when the transaction system is mid-restore. */
	void RequestRebuild(EDreamUIDocumentChangeReason InReason, bool bInDeferred);

	FDreamUIDocumentHandle DocumentHandle;
	TWeakPtr<FDreamWidgetPreviewHost> Host;

	FDelegateHandle TemplateChangedHandle;
	FDelegateHandle TextChangedHandle;
	FTSTicker::FDelegateHandle DeferredRebuildTickerHandle;

	FDreamUIWriteBackRebuildRequested RebuildRequestedDelegate;

	/** Set while our own SetContent runs, so the broadcast it causes is not read as somebody else's. */
	bool bIsWritingBack = false;
	bool bRebuildPending = false;
	EDreamUIDocumentChangeReason PendingRebuildReason = EDreamUIDocumentChangeReason::UndoRedo;

	int32 WriteCount = 0;
	int32 LastEditCount = 0;
	FDreamUIDiagnosticBag LastDiagnostics;
};
