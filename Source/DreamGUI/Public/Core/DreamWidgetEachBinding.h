// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DreamWidgetPropertyBinding.h"
#include "DreamWidgetEachBinding.generated.h"

/**
 * One `Prop <- Item.Member` line inside an `each` body: what to write, on which widget of the CELL,
 * from which member of the item. Resolved per cell at SetCell time -- the cell's widgets are clones
 * of the template, addressed by the display names the clone carries across.
 */
USTRUCT()
struct DREAMGUI_API FDreamWidgetEntryBinding
{
	GENERATED_BODY()

	/** The template-subtree widget the value lands on, by display name -- clones keep it. */
	UPROPERTY()
	FName TargetWidgetDisplayName;

	UPROPERTY()
	EDreamWidgetBindingTarget Target = EDreamWidgetBindingTarget::Widget;

	UPROPERTY()
	int32 BehaviourIndex = INDEX_NONE;

	UPROPERTY()
	FName PropertyName;

	/** Resolved by the builder through the same setter rule every binding uses. */
	UPROPERTY()
	FName SetterName;

	/** The member read off the item object. */
	UPROPERTY()
	FName ItemMember;
};

/**
 * One `Event -> Item.Func` line inside a loop body: the event, on which widget of the copy (or cell), routed to a
 * function of THAT copy's item. Bound per copy by the adapter -- a `for` when it makes or re-aims a copy, an `each` at
 * SetCell, unbinding the previous item first when a cell is recycled -- never on the class, which has no item.
 *
 * The function takes nothing, or exactly what the event sends (bCallWithoutArguments says which the author wrote:
 * `-> Item.Use()` or `-> Item.Use`). Checked by the compiler when the source's element class is known
 * (LoopItemRouteMismatch); against an array of UObject only at run time, where a function that fits neither way is
 * skipped.
 */
USTRUCT()
struct DREAMGUI_API FDreamWidgetEntryRoute
{
	GENERATED_BODY()

	/** The template-subtree widget whose event it is, by display name -- clones keep it. */
	UPROPERTY()
	FName TargetWidgetDisplayName;

	UPROPERTY()
	EDreamWidgetBindingTarget Target = EDreamWidgetBindingTarget::Widget;

	UPROPERTY()
	int32 BehaviourIndex = INDEX_NONE;

	/**
	 * A BlueprintAssignable multicast delegate, an FDreamUIEventDelegate property, or a single-cast delegate -- the three
	 * kinds a route names. A single-cast one is SET to the item's function (its one slot), the others are appended to.
	 */
	UPROPERTY()
	FName EventName;

	/** The function called on the item. */
	UPROPERTY()
	FName ItemFunction;

	/** `-> Item.Func()`: call with no arguments. Without the parentheses the event's own arguments are forwarded. */
	UPROPERTY()
	bool bCallWithoutArguments = false;

#if WITH_EDITORONLY_DATA
	/** Where the route was written, for LoopItemRouteMismatch. See FDreamWidgetEachBinding::SourceLine. */
	UPROPERTY()
	int32 SourceLine = 0;

	UPROPERTY()
	int32 SourceColumn = 0;
#endif // WITH_EDITORONLY_DATA
};

/**
 * One `each Item in Source { Template }` block, compiled: which widget hosts the list view, which
 * widget is the cell template, where the items come from, and what each cell writes from its item.
 *
 * The runtime resolves it at Initialize the way property bindings are resolved: host and template
 * through the class properties their ids became, the source through reflection -- a nullary
 * UFUNCTION returning TArray<UObject*>, or a TArray<UObject*> variable, whose FieldNotify
 * broadcast (when it has one) is what refreshes the list without anyone calling refresh.
 */
USTRUCT()
struct DREAMGUI_API FDreamWidgetEachBinding
{
	GENERATED_BODY()

	/** The widget carrying the UUIRecyclableScrollView-family behaviour, by variable name. */
	UPROPERTY()
	FName HostWidgetName;

	/** The template root inside the host, by variable name. */
	UPROPERTY()
	FName TemplateWidgetName;

	/**
	 * The builder-synthesized content widget under the host, by variable name. Carried here because
	 * the view's own Content pointer is authored against the ARCHETYPE tree: copied into an
	 * instance it still aims at the archetype, and every cell the view cloned landed in the
	 * invisible template tree. Resolve re-aims it per instance, exactly as it does the template.
	 */
	UPROPERTY()
	FName ContentWidgetName;

	/** Function or variable on the user widget supplying TArray<UObject*>. With SourcePath, its LAST segment. */
	UPROPERTY()
	FName SourceName;

	/** Describes SourceName -- the last segment when there is a SourcePath. */
	UPROPERTY()
	bool bSourceIsFunction = true;

	/**
	 * Set when the source is a member path (`in Inventory.Items`, `in Inventory.Filtered()`): every segment, the last
	 * equal to SourceName. Empty for the one-segment source every loop had before, which reads SourceName on the user
	 * widget exactly as it always did. Items are read through DreamUIBindingPath (ResolveOwner, then ReadObjectArray), and
	 * the owning widget refreshes the adapter when anything along the path changes (an FDreamUIBindingObserver client).
	 */
	UPROPERTY()
	TArray<FName> SourcePath;

	/** The loop variable's spelling, kept for messages. */
	UPROPERTY()
	FName LoopVariable;

	/**
	 * A `for` rather than an `each`: one copy of the template per item, made inside the host panel itself -- no list
	 * view, no virtualization, no synthesized content (ContentWidgetName stays None). The template stays in the tree
	 * collapsed, and the copies take its place among the host's children, in item order. Run by the core
	 * (UDreamUIForAdapter); an `each` is run by the list views' module through IDreamUIEachBindingHandler.
	 */
	UPROPERTY()
	bool bInPanel = false;

#if WITH_EDITORONLY_DATA
	/**
	 * Where the `each` header was written, 1-based, 0 for a block that came from anywhere but a
	 * .dui -- the same pair FDreamWidgetPropertyBinding and FDreamWidgetEventBinding carry, spelled
	 * the same on purpose so one AuthoredLocation helper reads all three.
	 *
	 * DUI6006 and DUI6007 are the readers. Both ask whether the SOURCE exists on the class being
	 * compiled, which is a question that only has an answer after the class is built and therefore
	 * long after the AST that knew this line is gone.
	 */
	UPROPERTY()
	int32 SourceLine = 0;

	UPROPERTY()
	int32 SourceColumn = 0;
#endif // WITH_EDITORONLY_DATA

	UPROPERTY()
	TArray<FDreamWidgetEntryBinding> EntryBindings;

	/** The body's `-> Item.Func` lines. */
	UPROPERTY()
	TArray<FDreamWidgetEntryRoute> EntryRoutes;
};
