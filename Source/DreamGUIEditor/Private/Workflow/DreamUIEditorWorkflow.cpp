// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Workflow/DreamUIEditorWorkflow.h"

#include "DreamGUIEditorModule.h"
#include "Workflow/DreamUILayerStyleClipboard.h"

void DreamUIEditorWorkflow::Startup(FDreamGUIEditorModule& InModule)
{
	DreamUILayerStyleClipboard::RegisterMenuExtension(*InModule.GetWidgetContextMenuExtensibilityManager());
}

void DreamUIEditorWorkflow::Shutdown(FDreamGUIEditorModule& InModule)
{
	DreamUILayerStyleClipboard::UnregisterMenuExtension(*InModule.GetWidgetContextMenuExtensibilityManager());
}
