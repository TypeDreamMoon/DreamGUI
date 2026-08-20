// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DreamUIEditorCommands.h"
#include "DreamGUIEditorStyle.h"

#define LOCTEXT_NAMESPACE "FDreamGUIEditorCommands"

FDreamUIEditorCommands::FDreamUIEditorCommands()
	: TCommands<FDreamUIEditorCommands>(TEXT("DreamGUIEditor"), NSLOCTEXT("Contexts", "DreamGUIEditor", "DreamGUIEditor Plugin"), NAME_None, FDreamGUIEditorStyle::GetStyleSetName())
{
}
void FDreamUIEditorCommands::RegisterCommands()
{
}

#undef LOCTEXT_NAMESPACE
