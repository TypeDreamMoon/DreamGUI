// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "Framework/Commands/Commands.h"

class FLexUIEditorCommands : public TCommands<FLexUIEditorCommands>
{
public:

	FLexUIEditorCommands();
	// TCommands<> interface
	virtual void RegisterCommands() override;
};