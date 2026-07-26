// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"
#include "LGUIEditorStyle.h"

class ULexUIPrefab;

class FLexUIPrefabEditorCommand : public TCommands<FLexUIPrefabEditorCommand>
{
public:
	FLexUIPrefabEditorCommand()
		: TCommands<FLexUIPrefabEditorCommand>(
			TEXT("LexUIPrefabEditor"), // Context name for fast lookup
			NSLOCTEXT("Contexts", "LexUIPrefabEditor", "LexUI Prefab Editor"), // Localized context name for displaying
			NAME_None, // Parent
			FLGUIEditorStyle::Get().GetStyleSetName() // Icon Style Set
			)
	{
	}

	// TCommand<> interface
	virtual void RegisterCommands() override;
	// End of TCommand<> interface

public:
	TSharedPtr<FUICommandInfo> Apply;
	TSharedPtr<FUICommandInfo> SaveOnApply_Never;
	TSharedPtr<FUICommandInfo> SaveOnApply_SuccessOnly;
	TSharedPtr<FUICommandInfo> SaveOnApply_Always;
	TSharedPtr<FUICommandInfo> RawDataViewer;
	TSharedPtr<FUICommandInfo> OverridesViewer;
	TSharedPtr<FUICommandInfo> OpenPrefabHelperObject;
	TSharedPtr<FUICommandInfo> OpenBehaviourBlueprint;
};
