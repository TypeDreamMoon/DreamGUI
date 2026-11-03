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

/**
 * What a flush could not make the file's shape agree about, by node id.
 *
 * The compile that follows a structural edit rebuilds the tree from the file, so anything listed here
 * is about to be undone by it: an Unwritten node disappears, an Unremoved one comes back. Reported so
 * the author hears it from the designer instead of watching the edit revert. Empty when every shape
 * change landed, which is the only state a flush of a value edit should ever produce.
 */
struct DREAMGUIEDITOR_API FDreamUIStructuralResidue
{
	/** On the live tree, and still nowhere in the text after every round. */
	TArray<FString> Unwritten;
	/** In the text, and gone from the live tree, but the file still declares it. */
	TArray<FString> Unremoved;
	/** In both, under a different parent in the text than in the live tree. */
	TArray<FString> Unmoved;
	/**
	 * `Node: + Class` -- an object gone from the live node that no `+` line of the node built (its style's, its type's),
	 * so nothing could be taken out of the file for it.
	 */
	TArray<FString> Unsettled;
	/**
	 * `Node.Property = value` -- lines a change of type or of panel took out because the new one has no such property.
	 * Not a failure, and not part of IsEmpty: the change landed, and this is what it cost, for the author to hear.
	 */
	TArray<FString> Dropped;

	bool IsEmpty() const { return Unwritten.IsEmpty() && Unremoved.IsEmpty() && Unmoved.IsEmpty() && Unsettled.IsEmpty(); }
	void Reset() { Unwritten.Reset(); Unremoved.Reset(); Unmoved.Reset(); Unsettled.Reset(); Dropped.Reset(); }
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
	 * parse, the tree does not build, the file could not be written. "Nothing had changed" writes
	 * nothing unless a previous disk write is still owed, which is retried. Having no host is a no-op.
	 */
	bool Flush(FString& OutError);

	/** Flush an explicit tree. Flush() is this with the host's, and the two share every line below. */
	bool FlushTree(const UDreamWidgetTree* InLiveTree, FString& OutError,
		const UObject* InLiveDefaults = nullptr);

	/**
	 * One statement -- a SetStatement or a RemoveProperty: a binding, a route, a two-way binding, or a value written in
	 * place of one -- applied to the document as it stands, in one transaction, with no tree compared.
	 *
	 * Not a flush, on purpose. A binding or a route is one line and no tree state; and a flush right after `Text = "Hi"`
	 * became `Text <- GetTitle()` would read the template's "Hi" against a line that no longer holds a literal, and
	 * offer it back as an edit the patcher refuses. The compile that follows rebuilds the template from the file.
	 *
	 * True when the line was written or the file already said it; false, with the patcher's reason, when it was refused.
	 */
	bool ApplyStatementEdit(const FDreamUIStructuralEdit& InEdit, FString& OutError);

	/**
	 * A new text for the document, made by a caller that edited it as a whole (the timeline write-back): checked to parse,
	 * then written as one write of this write-back's own, in one transaction. True when written or unchanged.
	 */
	bool ApplyTextChange(const FString& InNewText, FString& OutError);

	/** The document's text as it stands; empty when there is no document. */
	FString GetDocumentText() const;

	/**
	 * A live value as a `.dui` literal, spelled the way the value pass spells it -- `24`, `(160, 48)`, `#1E1E1E`, `"OK"`,
	 * `[1, 2]` -- or false when the type or this value has no spelling. A node reference has no tree to be spelled
	 * against here, so only `None` prints.
	 */
	static bool PrintValueLiteral(const FProperty* InLeaf, const void* InValuePtr, FString& OutText);

	/**
	 * Every value the text tree does not hold as the live one does: OutEdits the ones a line could say (CollectEdits with
	 * the dirty set off), OutUnwritable the ones no line can -- `Node.Property` for a value that differs and has no
	 * spelling, or one whose value the language cannot spell. What a conversion asks of the file it made: both empty is
	 * a file that builds the tree it came from.
	 */
	static void CollectUnwritableDifferences(const FDreamUIAst& InAst, const UDreamWidgetTree* InLiveTree,
		const UDreamWidgetTree* InTextTree, TArray<FDreamUIPropertyEdit>& OutEdits, TArray<FString>& OutUnwritable,
		FDreamUIDiagnosticBag& OutDiagnostics);

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
	 *
	 * What the newer syntax adds to the walk, each for the same reason -- the comparison must not
	 * report a value the file already produces, nor write one where the author did not write:
	 *
	 *   - An anonymous node pairs by the id the parser made for it, exactly like a named one.
	 *   - A node whose TYPE is a layout container (`VerticalBox Column { Spacing = 29 }`) has that
	 *     container's values compared with its own and written as bare lines, as its author wrote them.
	 *   - The `+` lines and `@slot` lines of a node's STYLE are in both trees, so untouched they cancel
	 *     out; a changed slot value becomes an override on the node, and a changed value of a style's
	 *     component is never written into the shared style: it is asked for as an override on the node
	 *     (OutStyleOverrides), or said (DUI7005) when the caller does not ask.
	 *   - `Shown` and Visibility are one value: only the face the file spells is compared.
	 *   - A slot FILL is the component's slot, not a widget of this file, and a transient widget (the
	 *     copies a `for` makes at run time) is nobody's line; both are left out.
	 */
	/*
	 * bInUseDirtySet: honour what NoteDirtyProperty was told about InLiveTree, when anything was.
	 * False sweeps everything regardless.
	 *
	 * InSweepNodeIds: the nodes a shape pass has just written into the file. When given, those nodes
	 * are swept whole -- they have no dirty entries, and every value they hold is one the author made --
	 * and every OTHER node writes only what the dirty set names, with no sweep to fall back on. A flush
	 * that changed the shape is a designer gesture, and the sweep over untouched nodes is exactly what
	 * would put a panel's layout arithmetic into lines nobody edited.
	 *
	 * OutStyleOverrides: when given, a changed value that the node's STYLE gives it through a `+ Class` line is not
	 * refused (DUI7005) but asked for as an InsertComponent of that class on the node -- the override line the value is
	 * then written into, leaving the shared style alone. ProduceText applies those and runs the pass again.
	 */
	static void CollectEdits(const FDreamUIAst& InAst, const UDreamWidgetTree* InLiveTree,
		const UDreamWidgetTree* InTextTree, TArray<FDreamUIPropertyEdit>& OutEdits,
		FDreamUIDiagnosticBag& OutDiagnostics, bool bInUseDirtySet = true,
		const TSet<FString>* InSweepNodeIds = nullptr, TArray<FDreamUIStructuralEdit>* OutStyleOverrides = nullptr);

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
	 *
	 * THE SHAPE IS WRITTEN IN ROUNDS. A subtree added at once is one insert per node, and every insert
	 * of a batch is planned against the text as it came in -- where the new parent of a new child does
	 * not exist yet. So each round writes the nodes whose parent the CURRENT text already declares,
	 * the text is read back, and the next round finds the children's parents there. Removals happen in
	 * the first round only (a removal never waits for another), and an insert the patcher refused is
	 * not asked again. A node written by a round is then checked once for the `+` lines it needs.
	 *
	 * OutResidue, when given, receives what the shape still disagrees about once every round is done:
	 * see FDreamUIStructuralResidue.
	 */
	static bool ProduceText(const FString& InText, const UDreamWidgetTree* InLiveTree,
		FString& OutText, FDreamUIDiagnosticBag& OutDiagnostics, TArray<FDreamUIPropertyEdit>* OutEdits = nullptr,
		const UObject* InLiveDefaults = nullptr, FDreamUIStructuralResidue* OutResidue = nullptr);

	/**
	 * How a `.dui` would spell this widget's type in this file: its `use … as` alias, an `@Resource`, a
	 * built-in tag, a layout container (`VerticalBox`), a registry tag (`Native.Button`) or the path of
	 * the widget blueprint it nests. What an inserted node is written with, exported so that a check
	 * made BEFORE the insert spells the node the way the insert will.
	 */
	static FString DescribeWidgetForText(const UDreamWidget* InWidget, const FDreamUIAst& InAst);

	/**
	 * How the two trees DISAGREE ABOUT SHAPE, as patcher commands: added, removed and reordered nodes.
	 *
	 * The comparison is the same one every value goes through, and that is what makes it safe. Both
	 * trees come out of the same builder, so everything the builder synthesises -- an `each` block's
	 * content widget, the list entry it adds to a template, the behaviours a layout container pulls
	 * in -- is present in BOTH and cancels out. A widget that exists only in the live tree can
	 * therefore only be one the designer made; one that exists only in the reference tree can only be
	 * one the designer deleted. Diffing the live tree against the FILE instead would report every
	 * synthesised object as a missing line.
	 *
	 * A rename is deliberately not inferred: from a diff it is a delete plus an insert, and guessing
	 * which pairs with which would write `(was:)` clauses that migrate the wrong thing. A caller that
	 * KNOWS a rename happened issues FDreamUIStructuralEdit directly.
	 *
	 * A node added is written with the type this FILE would use for it: its `use … as` alias when the
	 * class has one here (`Row Item_9`, `nier.Row Item_9`), else its `@Resource`, else the registry tag
	 * or the path; and a plain widget that lays out its children as the container (`VerticalBox Box`),
	 * the one spelling that keeps the container through the next compile. Transient widgets -- what a
	 * `for` makes at run time -- are never added, and a slot fill is never removed.
	 *
	 * A node is only inserted under a parent InAst already declares, and a removed node's descendants
	 * are not removed on their own: they go with it, and a second removal over the same lines would
	 * make the patcher refuse the pair. ProduceText calls this once per round; see there.
	 *
	 * bInIncludeRemovals: false collects inserts only. InSkipInsertIds: nodes not to insert (the ones a
	 * round already failed to write). InComponentCheckIds: nodes this flush has just written, whose `+`
	 * lines -- behaviours and a layout container their type does not already spell -- are added now that
	 * the node is in the file.
	 *
	 * A node BOTH trees have is compared too, for what the designer can change about a node without
	 * adding or removing one: its TYPE (a new visual, a different nested widget blueprint, another panel
	 * when the type is the panel's name) becomes a RetypeNode, with a RemoveProperty for each line the new
	 * type has no property for; and its `+` lines follow its objects -- one the live node gained is
	 * inserted, one it lost is removed (only a line the node itself writes; one from its style or type is
	 * reported in OutUnsettled), and behaviours the live node holds in another order are moved. Paired by
	 * class, so whatever both trees build alike cancels out. A node is retyped or has its lines changed in
	 * one round, and the next round reads the result back.
	 *
	 * OutSweepKeys: what the value pass should sweep whole -- a retyped node's id, and `Id+Class` for a `+`
	 * line just inserted. OutDropped: `Id.Property = value` for each line a retype took out. InAttempted:
	 * signatures of edits on existing nodes earlier rounds already tried, never emitted twice.
	 */
	static void CollectStructuralEdits(const FDreamUIAst& InAst, const UDreamWidgetTree* InLiveTree,
		const UDreamWidgetTree* InTextTree, TArray<FDreamUIStructuralEdit>& OutEdits,
		bool bInIncludeRemovals = true, const TSet<FString>* InSkipInsertIds = nullptr,
		const TSet<FString>* InComponentCheckIds = nullptr, TSet<FString>* OutSweepKeys = nullptr,
		TArray<FString>* OutDropped = nullptr, TArray<FString>* OutUnsettled = nullptr,
		const TSet<FString>* InAttempted = nullptr);

	/** The signature by which a round recognises an edit an earlier round already tried. */
	static FString MakeEditSignature(const FDreamUIStructuralEdit& InEdit);

	/**
	 * Which of InNode's `+` lines built InComponent on InWidget -- the widget the text was built into, or the
	 * designer's template before it is edited -- or INDEX_NONE when none did: the object came from the node's style,
	 * from its type, or with another component. Paired the way the value pass pairs them.
	 */
	static int32 FindComponentLine(const FDreamUINode& InNode, const UDreamWidget* InWidget, const UObject* InComponent);

	/** How a `+` line names this class, verified against the builder's reading of it, or empty when nothing does. */
	static FString DescribeComponentForText(const UClass* InClass);

	// -------------------------------------------------------------------------------------------
	// The dirty set: what the designer says it touched
	// -------------------------------------------------------------------------------------------

	/**
	 * Note that something wrote InPropertyName on InNodeId's InTarget, so the next flush writes THAT.
	 *
	 * WHY THIS EXISTS. The flush compares every property reflection can reach on both trees and
	 * writes the ones that differ. That fails CLOSED -- a property nobody listed is still written --
	 * which is why it was chosen over a table, and it has one hole that is not a matter of taste: a
	 * value can differ for a reason that is not an edit. The anchor block of a widget inside a layout
	 * container is the case that bit -- the panel ARRANGES it, so the live value is layout output,
	 * and every flush wrote the panel's arithmetic into the author's file as if they had typed it.
	 *
	 * So the rule is now: if anything has told this tree what it touched, the flush writes only
	 * those; if nothing has, the full sweep runs exactly as before. Both halves are deliberate. The
	 * first makes the common path say what it means. The second keeps the sweep as the fallback for
	 * every caller that does not report -- a commandlet, a test, a tool -- because losing an edit is
	 * worse than writing a line twice, which is the trade the sweep was chosen for.
	 *
	 * Keyed on the TREE rather than kept per write-back, because the reporters (the preview host's
	 * two migration paths) reach a tree long before they could reach a write-back, and because two
	 * designers open at once then cannot see each other's edits.
	 *
	 * MATCHED BY NODE AND HEAD. A report names the property it migrated; the flush asks about the
	 * leaves under it. So `AnchorData` reported writes `AnchorData.SizeDelta` and every other leaf of
	 * it that differs, and the target and component index only say where the report came from.
	 */
	static void NoteDirtyProperty(const UDreamWidgetTree* InTree, const FString& InNodeId,
		EDreamUIPatchTarget InTarget, int32 InComponentIndex, const FString& InPropertyName);

	/** Forget one tree's set. The flush calls it once it has written; a closing designer too. */
	static void ClearDirtyProperties(const UDreamWidgetTree* InTree);

	/** How many properties this tree is carrying as dirty. For tests and for the log. */
	static int32 NumDirtyProperties(const UDreamWidgetTree* InTree);

	// -------------------------------------------------------------------------------------------
	// The structural queue: what the designer says it did to the shape
	// -------------------------------------------------------------------------------------------

	/**
	 * Note one change to the shape of InTree that the next flush has to write as it was made.
	 *
	 * WHY THIS EXISTS. Adding and removing a node can be read off the two trees, and the shape pass
	 * does exactly that. A RENAME cannot: from the trees it is one node gone and another arrived, and
	 * writing it that way deletes the node's whole block -- its values, its routes, its children -- and
	 * writes a bare new one with no `(was:)` to carry its graph references across. A MOVE can be read
	 * off but not its order against the other edits of one gesture. So the gestures that know they
	 * renamed or moved something say so here, and the flush applies the queue first, one edit at a time
	 * against the text as it stands, before the shape pass reads what is left off the trees.
	 *
	 * A MoveNode is placed where the node stands in the live tree WHEN THE FLUSH RUNS (its parent, the
	 * fill of a named slot it is in, the sibling it now stands before), so a caller only has to say
	 * which node moved. An edit that cannot be planned yet -- a move into a node this same gesture is
	 * creating -- waits for the shape pass to write that node and is tried again.
	 *
	 * Keyed on the tree for the reasons the dirty set is, and consumed by the flush that writes it.
	 */
	static void QueueStructuralEdit(const UDreamWidgetTree* InTree, const FDreamUIStructuralEdit& InEdit);

	/** Forget one tree's queue. The flush calls it once it has written. */
	static void ClearQueuedStructuralEdits(const UDreamWidgetTree* InTree);

	/** How many structural edits this tree is carrying. For tests and for the log. */
	static int32 NumQueuedStructuralEdits(const UDreamWidgetTree* InTree);



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
	/** What the last flush could not make the file's shape agree about. Empty when it all landed. */
	const FDreamUIStructuralResidue& GetLastStructuralResidue() const { return LastResidue; }

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
	FDreamUIStructuralResidue LastResidue;
};
