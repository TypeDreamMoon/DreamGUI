// Copyright 2026-Present LexLiu. All Rights Reserved.

#include "PrefabSystem/LexUIAuthoredGeometrySaveScope.h"

#if WITH_EDITOR

#include "Core/Components/LexPanelSlot.h"
#include "Core/Components/LexWidget.h"

FLexUIAuthoredGeometrySaveScope::FLexUIAuthoredGeometrySaveScope(ULexWidget* RootWidget)
{
	TArray<ULexWidget*> Pending;
	TSet<const ULexWidget*> Visited;
	if (IsValid(RootWidget))
	{
		Pending.Add(RootWidget);
	}
	while (Pending.Num() > 0)
	{
		ULexWidget* Widget = Pending.Pop();
		if (!IsValid(Widget) || Visited.Contains(Widget))
		{
			continue;
		}
		Visited.Add(Widget);
		for (ULexWidget* Child : Widget->GetChildren())
		{
			Pending.Add(Child);
		}

		ULexPanelSlot* Slot = Widget->GetPanelSlot();
		if (!IsValid(Slot) || !Slot->bLayoutGeometryApplied || !Slot->bHasAuthoredGeometry)
		{
			continue;
		}
		FEntry& Entry = Entries.AddDefaulted_GetRef();
		Entry.Widget = Widget;
		Entry.Slot = Slot;
		Entry.LiveAnchorData = Widget->AnchorData;
		Entry.LiveControlMask = Slot->LayoutGeometryControlMask;
		// Raw writes on purpose: the arranged state comes back verbatim in the destructor, so the swap
		// must not mark anything dirty or trigger a layout/transform update in between.
		Widget->AnchorData = Slot->ComposeAuthoredAnchorData(Entry.LiveAnchorData);
		Slot->bLayoutGeometryApplied = false;
		Slot->LayoutGeometryControlMask = 0;
	}
}

FLexUIAuthoredGeometrySaveScope::~FLexUIAuthoredGeometrySaveScope()
{
	for (int32 i = Entries.Num() - 1; i >= 0; i--)
	{
		const FEntry& Entry = Entries[i];
		ULexWidget* Widget = Entry.Widget.Get();
		ULexPanelSlot* Slot = Entry.Slot.Get();
		if (IsValid(Widget))
		{
			Widget->AnchorData = Entry.LiveAnchorData;
		}
		if (IsValid(Slot))
		{
			Slot->bLayoutGeometryApplied = true;
			Slot->LayoutGeometryControlMask = Entry.LiveControlMask;
		}
	}
}

#endif
