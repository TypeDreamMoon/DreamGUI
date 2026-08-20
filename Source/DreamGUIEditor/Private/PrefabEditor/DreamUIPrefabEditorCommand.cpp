// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "DreamUIPrefabEditorCommand.h"

#define LOCTEXT_NAMESPACE "DreamGUIPrefabEditorCommand"

void FDreamUIPrefabEditorCommand::RegisterCommands()
{
	UI_COMMAND(Apply, "Apply", "Apply changes to prefab for use in runtime.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(SaveOnApply_Never, "Never", "Never save the prefab asset when changes are applied.", EUserInterfaceActionType::RadioButton, FInputChord());
	UI_COMMAND(SaveOnApply_SuccessOnly, "On Success Only", "Save the prefab asset when Apply completes without validation issues.", EUserInterfaceActionType::RadioButton, FInputChord());
	UI_COMMAND(SaveOnApply_Always, "Always", "Save the prefab asset after Apply even when validation reports issues.", EUserInterfaceActionType::RadioButton, FInputChord());
	UI_COMMAND(RawDataViewer, "RawDataViewer", "Open raw data viewer panel of this prefab.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(OverridesViewer, "Overrides", "Open the sub-prefab override statistics panel: every property pinned on a nested instance, searchable. Pinned properties shadow edits made in the sub-prefab asset.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(OpenPrefabHelperObject, "PrefabHelperObject", "Open PrefabHelperObject details panel of this prefab.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(OpenBehaviourBlueprint, "Behaviour", "Open this prefab's companion behaviour blueprint (a UDreamUIBehaviour script on the root widget), creating BP_<PrefabName> next to the prefab if there is none yet. Like a UMG Widget Blueprint's Graph.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(ToggleScreenSpacePreview, "Screen Space", "Build the preview the way play builds it: as a ScreenSpaceOverlay canvas. This is what lets a declared Perspective take effect at all -- the remap is baked into the geometry, so it then shows in any viewport. It does NOT change what projects the image; the editor always draws through the editor camera. To also stand where play stands, use Canvas Eye. Off previews in world space, better for inspecting geometry in 3D. Persisted with the prefab's editor data when the asset is saved.", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(FrameFromCanvasEye, "Canvas Eye", "Switch to the 3D view and stand the camera exactly where the canvas's own virtual camera stands, with its field of view. Perspective bakes geometry for that eye, so this is the one pose where the editor shows the foreshortening play will show -- anywhere else exaggerates or skews it. The canvas rect then exactly fills the frame, so the framing matches the 2D view and only content with real depth looks different. Orbit away afterwards to inspect; press again to return.", EUserInterfaceActionType::Button, FInputChord());
}

#undef LOCTEXT_NAMESPACE
