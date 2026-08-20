// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "Framework/Commands/Commands.h"

class FDreamUIEditorCommands : public TCommands<FDreamUIEditorCommands>
{
public:

	FDreamUIEditorCommands();
	// TCommands<> interface
	virtual void RegisterCommands() override;
};