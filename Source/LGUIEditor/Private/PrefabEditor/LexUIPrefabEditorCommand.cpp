// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "LexUIPrefabEditorCommand.h"

#define LOCTEXT_NAMESPACE "LGUIPrefabEditorCommand"

void FLexUIPrefabEditorCommand::RegisterCommands()
{
	UI_COMMAND(Apply, "Apply", "Apply changes to prefab for use in runtime.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(RawDataViewer, "RawDataViewer", "Open raw data viewer panel of this prefab.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(OpenPrefabHelperObject, "PrefabHelperObject", "Open PrefabHelperObject details panel of this prefab.", EUserInterfaceActionType::Button, FInputChord());
}

#undef LOCTEXT_NAMESPACE