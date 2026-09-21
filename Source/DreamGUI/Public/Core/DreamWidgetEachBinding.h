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

	/** Function or variable on the user widget supplying TArray<UObject*>. */
	UPROPERTY()
	FName SourceName;

	UPROPERTY()
	bool bSourceIsFunction = true;

	/** The loop variable's spelling, kept for messages. */
	UPROPERTY()
	FName LoopVariable;

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
};
