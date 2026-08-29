// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Text/DreamUIAst.h"
#include "Text/DreamUIDiagnostics.h"

/**
 * Which object inside a node's block an edit is aimed at.
 *
 * The three cases are the language's three property notations, and they exist here for the same
 * reason they exist in the grammar: the destination object is decided by the notation, not guessed
 * from the name. A bare `Padding` and an `@slot Padding` are two different properties on two
 * different objects, and a patcher that resolved by name alone would have to pick one.
 */
enum class EDreamUIPatchTarget : uint8
{
	/** A bare `Name = Value` line -- the widget, or its visual when the widget has none. */
	Node,
	/** An `@slot Name = Value` line -- the node's UDreamPanelSlot. */
	Slot,
	/**
	 * A property inside the ComponentIndex'th `+ Class { }` block of the node.
	 *
	 * Addressed by position rather than by class name because two `+` blocks of the same class on
	 * one node are legal, and a name would then be ambiguous exactly when it mattered. The index is
	 * the index into FDreamUINode::Components, which is author order, so a caller that got its
	 * component from the same AST it is patching against always has the right number.
	 */
	Component,
};

/** One property write. See FDreamUITextPatcher::SetProperties for what a batch of these means. */
struct FDreamUIPropertyEdit
{
	/** The id of the node to edit, matched case insensitively -- ids are FNames downstream. */
	FString NodeId;

	EDreamUIPatchTarget Target = EDreamUIPatchTarget::Node;

	/** Component target only: 0-based index into the node's `+` blocks, in the order they are written. */
	int32 ComponentIndex = INDEX_NONE;

	/** The UPROPERTY name, or a dotted path exactly as the language writes it: `AnchorData.SizeDelta`. */
	FString PropertyName;

	/** The value as .dui text -- `24`, `(160, 48)`, `#1E1E1E`, `"OK"`, `/Game/UI/F_Body`. */
	FString NewValueText;
};

/**
 * Writes a property value back into the .dui text it came from, and does nothing else to it.
 *
 * This is the component of the text pipeline that will be maintained for years, so its job is
 * deliberately the smallest one that makes the designer work: `.dui` is the single source of truth
 * and the designer is a graphical front end for it, which means a designer edit is a TEXT edit.
 * Structure is not editable from the designer at all -- no creating, deleting, reparenting or
 * renaming -- so this patcher never inserts a node, never removes one, and never reformats. It
 * replaces the text of one value, or inserts one line.
 *
 * WHY IT EDITS TEXT INSTEAD OF PRINTING THE AST. A printer would be a hundred lines shorter and it
 * would rewrite the author's whole file on every save: their alignment, their blank lines, their
 * comments, `(400,240)` renormalised to `(400, 240)`. Every one of those is a hunk in a diff nobody
 * made, and after a week of that nobody can review a .dui change. The AST carries a source location
 * on every node, every property and every value precisely so that this file can go the other way.
 *
 * WHY ONE EDIT PER CALL. Every FDreamUISourceLocation in the AST describes the text as it was
 * PARSED. The moment one edit changes the text, every location after the edit point is off by the
 * length that was inserted or removed -- silently, because a line and a column are still perfectly
 * valid numbers pointing at the wrong place, and the wrong place is somebody else's property. So a
 * caller that wants two edits either re-parses in between, or hands both to SetProperties at once,
 * which plans them all against the ONE state they were all measured in and then applies them from
 * the end of the file backwards so an earlier edit never moves a later one's target.
 *
 * WHAT IT REFUSES rather than guesses (all DUI7xxx, and all worth reading before adding a caller):
 *
 *   - A node id the file does not declare, or a component index it does not have. PatchTargetNotFound.
 *   - A property currently written as a binding (`Text <- GetTitle()`). Overwriting it with a
 *     literal would delete authored behaviour to store a value the binding was about to overwrite
 *     anyway, and the details panel showing the bound value makes that one drag away.
 *   - A `slot Name` node. The grammar refuses a block on a named slot, so there is nowhere in the
 *     file for a property to go; inserting one would produce a .dui that no longer parses.
 *   - A value text that cannot be read back as the value it was written as -- trailing junk, an
 *     embedded comment, an unterminated string, or a non-finite float. See the .cpp; that last one
 *     is the write-back half of the trap the implementation plan flagged before P5 started.
 *   - An AST whose locations do not describe the text it was handed. SourceFileChangedUnderEdit.
 *     Cheap to check (the text under a location has to start with the name the AST says is there)
 *     and it converts the one catastrophic failure mode -- a stale AST silently corrupting a file
 *     the author has open -- into a diagnostic.
 */
struct DREAMGUIEDITOR_API FDreamUITextPatcher
{
	/**
	 * Set one property to a new value, inserting the line when the file does not have it yet.
	 *
	 * InAst must be the AST of InOutText exactly as it is now -- the result of parsing this string,
	 * with nothing patched into it since. On success InOutText contains the edit and true is
	 * returned; on refusal InOutText is left untouched and OutDiagnostics gains one DUI7xxx entry
	 * (see the struct comment for the list). Writing a value that is already there byte for byte
	 * succeeds and changes nothing, which is what makes a repeated save produce an empty diff.
	 *
	 * InComponentIndex is read only when InTarget is Component; pass INDEX_NONE otherwise.
	 */
	static bool SetProperty(FString& InOutText, const FDreamUIAst& InAst,
		const FString& InNodeId,
		EDreamUIPatchTarget InTarget, int32 InComponentIndex,
		const FString& InPropertyName, const FString& InNewValueText,
		FDreamUIDiagnosticBag& OutDiagnostics);

	/**
	 * Apply several edits to one text, all measured against one AST.
	 *
	 * This is the shape the designer's flush actually has: a gesture ends and a handful of
	 * properties on one or two nodes are dirty at once. Doing them one at a time would mean
	 * re-parsing the file between each, and re-parsing is not merely slower -- it is a second place
	 * for the two states to disagree.
	 *
	 * The whole trick is in the order: every edit is RESOLVED against the unmodified text, and the
	 * resulting splices are then APPLIED from the highest offset down. An edit near the end of the
	 * file cannot move the target of one nearer the start, so no location is ever consulted after
	 * the text beneath it moved. Applying forwards instead would need every later offset shifted by
	 * the running delta, which works right up until one edit inserts a line and the accounting is
	 * off by an end-of-line's worth for the rest of the file.
	 *
	 * Two edits naming the same property are refused as a pair, because both would be planned
	 * against a text in which it appears once and the file would end up saying it twice. Edits that
	 * resolve are applied even when another one in the batch does not: a caller flushing nine good
	 * property values and one bad one is better off with the nine, and the tenth is reported. The
	 * return value is false if anything was refused.
	 */
	static bool SetProperties(FString& InOutText, const FDreamUIAst& InAst,
		TArrayView<const FDreamUIPropertyEdit> InEdits,
		FDreamUIDiagnosticBag& OutDiagnostics);
};
