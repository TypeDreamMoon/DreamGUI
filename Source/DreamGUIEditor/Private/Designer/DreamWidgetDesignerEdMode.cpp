// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "DreamWidgetDesignerEdMode.h"

#define LOCTEXT_NAMESPACE "DreamWidgetDesignerEdMode"

const FEditorModeID UDreamWidgetDesignerEdMode::EM_DreamWidgetDesigner = TEXT("EM_DreamWidgetDesigner");

UDreamWidgetDesignerEdMode::UDreamWidgetDesignerEdMode()
{
	// bVisible false: this is not a mode anyone picks from a toolbar, it is what the designer's
	// viewport activates on itself.
	Info = FEditorModeInfo(
		EM_DreamWidgetDesigner,
		LOCTEXT("DreamWidgetDesignerEdModeName", "DreamGUI Designer"),
		FSlateIcon(),
		false);
}

#undef LOCTEXT_NAMESPACE
