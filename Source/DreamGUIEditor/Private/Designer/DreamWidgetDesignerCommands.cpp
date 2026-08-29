// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "DreamWidgetDesignerCommands.h"

#define LOCTEXT_NAMESPACE "DreamGUIPrefabEditorCommand"

void FDreamWidgetDesignerCommands::RegisterCommands()
{
	UI_COMMAND(ToggleScreenSpacePreview, "Screen Space", "Build the preview the way play builds it: as a ScreenSpaceOverlay canvas. This is what lets a declared Perspective take effect at all -- the remap is baked into the geometry, so it then shows in any viewport. It does NOT change what projects the image; the editor always draws through the editor camera. To also stand where play stands, use Canvas Eye. Off previews in world space, better for inspecting geometry in 3D. Persisted with the prefab's editor data when the asset is saved.", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(FrameFromCanvasEye, "Canvas Eye", "Switch to the 3D view and stand the camera exactly where the canvas's own virtual camera stands, with its field of view. Perspective bakes geometry for that eye, so this is the one pose where the editor shows the foreshortening play will show -- anywhere else exaggerates or skews it. The canvas rect then exactly fills the frame, so the framing matches the 2D view and only content with real depth looks different. Orbit away afterwards to inspect; press again to return.", EUserInterfaceActionType::Button, FInputChord());
}

#undef LOCTEXT_NAMESPACE
