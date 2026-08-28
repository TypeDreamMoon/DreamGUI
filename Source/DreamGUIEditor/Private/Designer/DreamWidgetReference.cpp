// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Designer/DreamWidgetReference.h"
#include "Designer/DreamWidgetPreviewHost.h"
#include "Core/Components/DreamWidget.h"

FDreamWidgetReference::FDreamWidgetReference(TSharedPtr<FDreamWidgetPreviewHost> InHost, TSharedPtr<FDreamWidgetHandle> InTemplateHandle)
	: Host(InHost)
	, TemplateHandle(InTemplateHandle)
{
}

UDreamWidget* FDreamWidgetReference::GetTemplate() const
{
	return TemplateHandle.IsValid() ? TemplateHandle->Widget.Get() : nullptr;
}

UDreamWidget* FDreamWidgetReference::GetPreview() const
{
	TSharedPtr<FDreamWidgetPreviewHost> PinnedHost = Host.Pin();
	if (!PinnedHost.IsValid())
	{
		return nullptr;
	}
	return PinnedHost->FindPreviewForTemplate(GetTemplate());
}

bool FDreamWidgetReference::IsValid() const
{
	// Both halves, deliberately. A template with no preview is a widget the last compile did not
	// produce -- a caller asking IsValid is about to touch geometry, and there is none to touch.
	return GetTemplate() != nullptr && GetPreview() != nullptr;
}
