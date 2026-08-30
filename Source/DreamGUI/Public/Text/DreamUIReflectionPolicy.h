// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FProperty;

/**
 * Which properties the text pipeline may write, decided from reflection instead of from lists.
 *
 * The write-back used to carry the answer as hand-kept tables -- five geometry paths, nine slot
 * names -- and every property missing from a table was an edit that moved the preview and silently
 * died at the next compile. Rotation was the one that finally got noticed; the `+` component blocks
 * were quietly worse, comparing only what the file already wrote. A list fails OPEN: forgetting an
 * entry loses somebody's work. This policy fails CLOSED: forgetting an exclusion writes one
 * redundant line, and the write-back's convergence tests catch a redundant line that argues.
 *
 * THE ROOT FILTER is (CPF_Edit || native setter) && !hidden && writable-from-text, and each clause
 * earns its place:
 *
 *   Edit-or-setter, not Edit alone, because the transform properties are edited through a custom
 *   details section and carry no CPF_Edit -- their `Setter` specifier is what marks them as
 *   somebody's write surface. And not "every property", because UDreamWidget::WidgetGuid is a bare
 *   UPROPERTY() minted fresh on every build: under a reflective sweep it is never equal between the
 *   reference tree and the live one, so without this clause every flush would write guid fields
 *   into every author's file.
 *
 *   Hidden covers the properties whose VALUE another property already carries, where flags cannot:
 *   RelativeLocation is recomputed into the anchors and the anchors are what the file spells, so
 *   spelling location too would put one position in the file twice and let the copies argue at
 *   every compile. Marked meta=(DuiHidden) on our own declarations; the table is the escape hatch
 *   for properties on structs and classes whose headers are not ours to edit.
 *
 * THE LEAF RULE: a struct with a short form is one leaf, printed whole; a struct without one is
 * recursed into, which is what makes an arbitrary USTRUCT authorable with no new syntax -- the
 * dotted path already parses at any depth, so the sweep only has to PRINT the same shape.
 * Deriving beats listing here too: AnchorData has no short form on purpose, and recursing it
 * reproduces exactly the five geometry paths the old table wrote by hand. Containers are skipped at
 * every level this round; a leaf nothing can print is skipped by the printer downstream.
 */
namespace DreamUIReflection
{
	/**
	 * meta=(DuiHidden): the text pipeline does not see this property.
	 *
	 * For a property whose value is DERIVED -- recomputed from, mirrored by, or already spelled
	 * through another property -- so that writing it would author the same value twice.
	 */
	inline const TCHAR* MetaHidden = TEXT("DuiHidden");

	/** Hidden by meta or by the exclusion table. Null is hidden: there is nothing to write. */
	DREAMGUI_API bool IsHidden(const FProperty* InProperty);

	/**
	 * Excludes one property by its owner struct/class name and its own, for headers that are not
	 * ours to tag. Additive and process-wide; there is deliberately no removal, because a property
	 * excluded somewhere and swept somewhere else would sync or not depending on module load order.
	 */
	DREAMGUI_API void AddExclusion(FName InOwnerName, FName InPropertyName);

	/**
	 * Whether this property, sitting directly on a widget/visual/slot/behaviour, starts a writable
	 * sweep path. This is the same question the details panel's gate asks about a chain root, and it
	 * is exported so the two cannot drift: a panel that greys what the sweep writes, or writes what
	 * the sweep drops, is the contradiction the old lists kept creating.
	 */
	DREAMGUI_API bool IsSweepRoot(const FProperty* InProperty);

	/**
	 * Every dotted leaf path the text pipeline may write on an object of this class, in declaration
	 * order. "AnchorData.SizeDelta", "RelativeRotationEuler", "Style.Thickness". Cached per class;
	 * the answer is pure reflection and cannot change within a session.
	 */
	DREAMGUI_API const TArray<FString>& GetWritableLeafPaths(const UStruct* InScope);
}
