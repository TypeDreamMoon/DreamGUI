// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "DreamWidgetLifecycleTestTypes.h"

#include "Core/Components/DreamWidget.h"

void UDreamWidgetHierarchyMutationBehaviour::OnUnregister()
{
	Super::OnUnregister();
	if (IsValid(WidgetToDetach) && IsValid(ExternalParent))
	{
		WidgetToDetach->SetParent(ExternalParent, false);
	}
	if (IsValid(WidgetToAttach) && IsValid(GetWidget()))
	{
		WidgetToAttach->SetParent(GetWidget(), false);
	}
}

void UDreamWidgetCanvasProbeVisual::OnRenderCanvasChanged(UDreamCanvas* InOldCanvas, UDreamCanvas* InNewCanvas)
{
	CanvasChanges.Emplace(InOldCanvas, InNewCanvas);
	Super::OnRenderCanvasChanged(InOldCanvas, InNewCanvas);
}

void UDreamWidgetCanvasProbeVisual::MarkAllDirty()
{
	++MarkAllDirtyCount;
	Super::MarkAllDirty();
}
