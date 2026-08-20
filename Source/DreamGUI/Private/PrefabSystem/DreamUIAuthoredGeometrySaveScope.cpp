// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "PrefabSystem/DreamUIAuthoredGeometrySaveScope.h"

#if WITH_EDITOR

#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamWidget.h"

FDreamUIAuthoredGeometrySaveScope::FDreamUIAuthoredGeometrySaveScope(UDreamWidget* RootWidget)
{
	TArray<UDreamWidget*> Pending;
	TSet<const UDreamWidget*> Visited;
	if (IsValid(RootWidget))
	{
		Pending.Add(RootWidget);
	}
	while (Pending.Num() > 0)
	{
		UDreamWidget* Widget = Pending.Pop();
		if (!IsValid(Widget) || Visited.Contains(Widget))
		{
			continue;
		}
		Visited.Add(Widget);
		for (UDreamWidget* Child : Widget->GetChildren())
		{
			Pending.Add(Child);
		}

		UDreamPanelSlot* Slot = Widget->GetPanelSlot();
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

FDreamUIAuthoredGeometrySaveScope::~FDreamUIAuthoredGeometrySaveScope()
{
	for (int32 i = Entries.Num() - 1; i >= 0; i--)
	{
		const FEntry& Entry = Entries[i];
		UDreamWidget* Widget = Entry.Widget.Get();
		UDreamPanelSlot* Slot = Entry.Slot.Get();
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
