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

	/** Function or variable on the user widget supplying TArray<UObject*>. */
	UPROPERTY()
	FName SourceName;

	UPROPERTY()
	bool bSourceIsFunction = true;

	/** The loop variable's spelling, kept for messages. */
	UPROPERTY()
	FName LoopVariable;

	UPROPERTY()
	TArray<FDreamWidgetEntryBinding> EntryBindings;
};
