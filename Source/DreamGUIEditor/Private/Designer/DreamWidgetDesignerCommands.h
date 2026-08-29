// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once
#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"
#include "DreamGUIEditorStyle.h"


class FDreamWidgetDesignerCommands : public TCommands<FDreamWidgetDesignerCommands>
{
public:
	FDreamWidgetDesignerCommands()
		: TCommands<FDreamWidgetDesignerCommands>(
			TEXT("DreamWidgetDesigner"), // Context name for fast lookup
			NSLOCTEXT("Contexts", "DreamWidgetDesigner", "DreamUI Designer"), // Localized context name for displaying
			NAME_None, // Parent
			FDreamGUIEditorStyle::Get().GetStyleSetName() // Icon Style Set
			)
	{
	}

	// TCommand<> interface
	virtual void RegisterCommands() override;
	// End of TCommand<> interface

public:
	TSharedPtr<FUICommandInfo> ToggleScreenSpacePreview;
	TSharedPtr<FUICommandInfo> FrameFromCanvasEye;
};
