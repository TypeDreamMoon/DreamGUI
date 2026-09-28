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

void UDreamWidgetLifecycleCountingBehaviour::OnRegister()
{
	Super::OnRegister();
	++RegisterCount;
}

void UDreamWidgetLifecycleCountingBehaviour::OnUnregister()
{
	Super::OnUnregister();
	++UnregisterCount;
}

void UDreamWidgetLifecycleRecordingBehaviour::OnRegister()
{
	Super::OnRegister();
	Record(TEXT("Register"));
}

void UDreamWidgetLifecycleRecordingBehaviour::OnUnregister()
{
	Super::OnUnregister();
	Record(TEXT("Unregister"));
}

void UDreamWidgetLifecycleRecordingBehaviour::Awake()
{
	Super::Awake();
	Record(TEXT("Awake"));
}

void UDreamWidgetLifecycleRecordingBehaviour::OnEnable()
{
	Super::OnEnable();
	Record(TEXT("Enable"));
}

void UDreamWidgetLifecycleRecordingBehaviour::OnDisable()
{
	Super::OnDisable();
	Record(TEXT("Disable"));
}

void UDreamWidgetLifecycleRecordingBehaviour::OnDestroy()
{
	Super::OnDestroy();
	Record(TEXT("Destroy"));
}

void UDreamWidgetLifecycleRecordingBehaviour::Record(const TCHAR* InStep) const
{
	if (Log.IsValid())
	{
		const UDreamWidget* Widget = GetWidget();
		Log->Add(FString::Printf(TEXT("%s %s"), InStep, Widget != nullptr ? *Widget->GetDisplayName() : TEXT("?")));
	}
}
