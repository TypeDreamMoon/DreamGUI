// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "LexUIEditorCommands.h"
#include "LGUIEditorStyle.h"

#define LOCTEXT_NAMESPACE "FLGUIEditorCommands"

FLexUIEditorCommands::FLexUIEditorCommands()
	: TCommands<FLexUIEditorCommands>(TEXT("LGUIEditor"), NSLOCTEXT("Contexts", "LGUIEditor", "LGUIEditor Plugin"), NAME_None, FLGUIEditorStyle::GetStyleSetName())
{
}
void FLexUIEditorCommands::RegisterCommands()
{
}

#undef LOCTEXT_NAMESPACE
