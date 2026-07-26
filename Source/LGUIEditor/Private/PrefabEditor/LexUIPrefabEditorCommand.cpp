// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "LexUIPrefabEditorCommand.h"

#define LOCTEXT_NAMESPACE "LGUIPrefabEditorCommand"

void FLexUIPrefabEditorCommand::RegisterCommands()
{
	UI_COMMAND(Apply, "Apply", "Apply changes to prefab for use in runtime.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(SaveOnApply_Never, "Never", "Never save the prefab asset when changes are applied.", EUserInterfaceActionType::RadioButton, FInputChord());
	UI_COMMAND(SaveOnApply_SuccessOnly, "On Success Only", "Save the prefab asset when Apply completes without validation issues.", EUserInterfaceActionType::RadioButton, FInputChord());
	UI_COMMAND(SaveOnApply_Always, "Always", "Save the prefab asset after Apply even when validation reports issues.", EUserInterfaceActionType::RadioButton, FInputChord());
	UI_COMMAND(RawDataViewer, "RawDataViewer", "Open raw data viewer panel of this prefab.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(OverridesViewer, "Overrides", "Open the sub-prefab override statistics panel: every property pinned on a nested instance, searchable. Pinned properties shadow edits made in the sub-prefab asset.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(OpenPrefabHelperObject, "PrefabHelperObject", "Open PrefabHelperObject details panel of this prefab.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(OpenBehaviourBlueprint, "Behaviour", "Open this prefab's companion behaviour blueprint (a ULexUIBehaviour script on the root widget), creating BP_<PrefabName> next to the prefab if there is none yet. Like a UMG Widget Blueprint's Graph.", EUserInterfaceActionType::Button, FInputChord());
}

#undef LOCTEXT_NAMESPACE
