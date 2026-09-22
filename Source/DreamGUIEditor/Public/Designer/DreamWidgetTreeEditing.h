// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/SubclassOf.h"

class UDreamWidget;
class UDreamUserWidget;
class UDreamWidgetBlueprint;
class UDreamWidgetTree;

/**
 * Structural edits to an authored hierarchy: create, delete, reparent, reorder, rename.
 *
 * These operate on the TEMPLATE tree -- the one on the UDreamWidgetBlueprint, which is what gets
 * saved -- never on a preview. The preview is rebuilt from the class afterwards; it is a consequence
 * of the edit, not a second place the edit has to be repeated.
 *
 * Every function here snapshots what it is about to write (UObject::Modify on the parent as well as
 * the child, because the hierarchy lives in the parent's Children array) and marks the Blueprint
 * structurally modified so the class is recompiled. None of them opens a transaction: a designer
 * gesture is usually several of these and has to undo as one, so the caller owns the FScopedTransaction.
 *
 * They are free functions rather than methods on the Blueprint because they are editor policy --
 * name uniqueness, capacity refusal, what counts as a legal parent -- and the asset should not carry
 * that. The Blueprint owns the data; this owns the rules for changing it.
 *
 * ALL FIVE REFUSE A HIERARCHY THAT CAME FROM A `.dui`, first thing, out loud. Structure belongs to
 * the text there and nothing writes it back, so an edit made here would live until the next compile
 * and then be silently gone. See DreamUITextAuthoringGate.h -- and note that it is SIX entry points,
 * not five: paste is FDreamWidgetBlueprintEditor::DesignerPasteWidgets, which builds its copies
 * straight onto the tree without coming through here.
 */
namespace DreamWidgetTreeEditing
{
	/**
	 * Add a widget of InWidgetClass under InParent.
	 *
	 * InParent must belong to InBlueprint's tree; passing null means the tree's root. InSiblingIndex
	 * of -1 appends. InDesiredDisplayName is made unique within the tree before it is applied -- two
	 * widgets sharing a display name collapse into one compiler variable, and which one it binds to
	 * depends on tree order.
	 *
	 * Returns null when the class is unusable or the parent refuses the child (a panel at capacity).
	 *
	 * InSlotName names the hole of InParent the widget fills, for a parent that is a placed control
	 * (a Button's "Content", an expandable area's "Header"); see BindWidgetIntoSlot for what is
	 * recorded. A name the parent's class does not declare is refused before anything is written.
	 */
	DREAMGUIEDITOR_API UDreamWidget* CreateWidget(UDreamWidgetBlueprint* InBlueprint, TSubclassOf<UDreamWidget> InWidgetClass,
		UDreamWidget* InParent = nullptr, int32 InSiblingIndex = -1, const FString& InDesiredDisplayName = FString(),
		FName InSlotName = NAME_None);

	/**
	 * Remove InWidget and everything under it from the hierarchy.
	 *
	 * Refuses the tree's root: a hierarchy with no root is not a state the compiler or the designer
	 * has an answer for, and the way to empty one is to delete its children.
	 */
	DREAMGUIEDITOR_API bool DeleteWidget(UDreamWidgetBlueprint* InBlueprint, UDreamWidget* InWidget);

	/**
	 * Move InWidget under InNewParent at InSiblingIndex (-1 appends).
	 *
	 * Refuses a cycle, a parent at capacity, and the tree's root (which has nowhere to go). Reordering
	 * within the same parent is the same call with the same parent.
	 *
	 * InSlotName is CreateWidget's: the hole of a placed control the widget goes into. Whatever slot
	 * of its previous parent the widget was bound to lets go of it either way -- a binding the
	 * runtime follows ahead of Children would otherwise pull the widget straight back.
	 */
	DREAMGUIEDITOR_API bool ReparentWidget(UDreamWidgetBlueprint* InBlueprint, UDreamWidget* InWidget,
		UDreamWidget* InNewParent, int32 InSiblingIndex = -1, FName InSlotName = NAME_None);

	/**
	 * Record which hole of InNested the host's InWidget fills.
	 *
	 * InWidget must already be InNested's child: nesting is the one form the runtime, the .dui
	 * language and the designer all agree on for "the host put this inside that control", and the
	 * slot name is the part nesting cannot spell. The name has to be one InNested's class declares.
	 * For the class's DEFAULT slot nothing is written -- nesting alone means that slot, which is what
	 * keeps a Button filled from the designer indistinguishable from one filled from a .dui -- and
	 * any earlier binding of InWidget to another slot of the same control is dropped. The caller
	 * has snapshotted InNested and notifies the structural change; this only writes the binding.
	 */
	DREAMGUIEDITOR_API bool BindWidgetIntoSlot(UDreamWidgetBlueprint* InBlueprint, UDreamUserWidget* InNested,
		FName InSlotName, UDreamWidget* InWidget);

	/** Drop every slot binding InNested holds to InWidget. True when there was one. */
	DREAMGUIEDITOR_API bool ForgetSlotBindings(UDreamUserWidget* InNested, const UDreamWidget* InWidget);

	/**
	 * Give InWidget a new display name, made unique within the tree first.
	 *
	 * The display name is not decoration: UDreamWidgetTree::MakeWidgetVariableName derives the
	 * compiler variable from it, so renaming here renames the variable the graph sees. Returns the
	 * name actually applied, which differs from InDesiredDisplayName when it had to be disambiguated.
	 */
	DREAMGUIEDITOR_API FString RenameWidget(UDreamWidgetBlueprint* InBlueprint, UDreamWidget* InWidget, const FString& InDesiredDisplayName);

	/**
	 * Copy InSource and everything under it, and put the copy under InNewParent.
	 *
	 * The copy is re-homed flat onto the tree and given fresh object names on the way. That is not
	 * tidiness: FNames only have to be unique within an outer, duplication nests the copies under
	 * their parents, and the template-to-preview correspondence is BY FName -- so a nested copy
	 * sharing its original's name would put two templates on one name and the designer would start
	 * resolving one of them to the other's preview.
	 *
	 * Display names are made unique too, for the same reason one step up: they are the compiler's
	 * variable names.
	 */
	DREAMGUIEDITOR_API UDreamWidget* DuplicateWidget(UDreamWidgetBlueprint* InBlueprint, UDreamWidget* InSource,
		UDreamWidget* InNewParent, int32 InSiblingIndex = -1);

	/**
	 * Keep InWidget's authored bindings pointing at the same behaviour after its component list moves.
	 *
	 * A binding names a behaviour by its POSITION in the widget's component array
	 * (FDreamWidgetPropertyBinding::BehaviourIndex), because an instanced sub-object shares no name
	 * with its authored copy. So removing or reordering a behaviour renumbers everything after it, and
	 * a binding that is not renumbered with it silently starts driving whichever behaviour moved into
	 * its slot -- another one of the same class, which nothing reports at all, or one of a different
	 * class, which is a compile error naming a binding the author did not touch.
	 *
	 * InOldIndex is where the behaviour was, InNewIndex where it is now. INDEX_NONE for InNewIndex
	 * means it was removed, and the bindings that named it are dropped with it. Adding needs no call:
	 * UDreamWidget::AddComponent appends, so no existing position changes.
	 *
	 * Does nothing, and dirties nothing, when no binding names this widget's behaviours.
	 */
	DREAMGUIEDITOR_API void RemapBehaviourBindings(UDreamWidgetBlueprint* InBlueprint, const UDreamWidget* InWidget,
		int32 InOldIndex, int32 InNewIndex);

	/** Visit InRoot and every descendant, parents first. */
	DREAMGUIEDITOR_API void ForEachWidgetInSubtree(UDreamWidget* InRoot, TFunctionRef<void(UDreamWidget*)> InPredicate);

	/** InDesired, suffixed until no other widget in InTree (InIgnore excepted) answers to it. */
	DREAMGUIEDITOR_API FString MakeUniqueDisplayName(const UDreamWidgetTree* InTree, const FString& InDesired, const UDreamWidget* InIgnore = nullptr);

	/** Whether InWidget is part of InBlueprint's authored tree at all. Guards every function here. */
	DREAMGUIEDITOR_API bool IsTemplateWidgetOf(const UDreamWidgetBlueprint* InBlueprint, const UDreamWidget* InWidget);

	/**
	 * Tell the Blueprint its class no longer matches its hierarchy.
	 *
	 * Called for you by everything above. Exposed because a caller that edits the tree directly --
	 * a migration, a test -- still owes the Blueprint this, and skipping it produces the worst
	 * possible symptom: an edit that is on disk and absent from every instance.
	 */
	DREAMGUIEDITOR_API void NotifyStructureChanged(UDreamWidgetBlueprint* InBlueprint);
}
