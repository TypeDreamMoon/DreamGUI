// Copyright 2019-Present DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/NotifyHook.h"
#include "UObject/UnrealType.h"

/**
 * Announcing a write that did NOT go through a property handle.
 *
 * The designer's details panel edits PREVIEW widgets. A preview is rebuilt from the blueprint's
 * template tree, so a value that only ever reached the preview is gone at the next compile -- and the
 * only thing that carries it across is the details view's FNotifyHook (SDreamWidgetDesignerDetails),
 * which mirrors onto the template in NotifyPreChange/NotifyPostChange.
 *
 * A row that writes through IPropertyHandle::SetValue gets that for free. A row that calls a widget
 * setter instead does not: "Snap Size" computes one number and pushes it through SetWidth, which lands
 * inside AnchorData without any property node hearing about it. Such a row has to call the hook by
 * hand, in the pair the hook expects, naming the property it actually changed.
 *
 * Nothing here is designer-specific: outside the designer the details view has no such hook and these
 * are no-ops, which is exactly the old behaviour.
 */
namespace DreamDetailsTemplateMirror
{
	/** Snapshot the destination for undo before the write; harmless when there is no hook. */
	inline void NotifyPreChange(FNotifyHook* InNotifyHook, FProperty* InProperty)
	{
		if (InNotifyHook == nullptr || InProperty == nullptr)
		{
			return;
		}
		FEditPropertyChain Chain;
		Chain.AddHead(InProperty);
		InNotifyHook->NotifyPreChange(&Chain);
	}

	/**
	 * The write half. InChangedObjects are the objects the property lives ON -- for a widget's geometry
	 * that is the widget, never the visual whose panel the button sits in, because the mirror applies
	 * the chain to whatever the event names and refuses an object the property does not belong to.
	 */
	inline void NotifyPostChange(FNotifyHook* InNotifyHook, FProperty* InProperty, TArray<UObject*>& InChangedObjects)
	{
		if (InNotifyHook == nullptr || InProperty == nullptr || InChangedObjects.Num() == 0)
		{
			return;
		}
		FEditPropertyChain Chain;
		Chain.AddHead(InProperty);
		// ValueSet, not Interactive: the hook drops interactive changes on purpose, and a button click
		// is by definition the finished gesture.
		FPropertyChangedEvent ChangedEvent(InProperty, EPropertyChangeType::ValueSet, MakeArrayView(InChangedObjects));
		InNotifyHook->NotifyPostChange(ChangedEvent, &Chain);
	}
}
