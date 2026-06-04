// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "ISequencer.h"


class SLexUIPrefabSequenceEditorWidgetImpl;
class ULexUIPrefabSequence;

class SLexUIPrefabSequenceEditorWidget
	: public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SLexUIPrefabSequenceEditorWidget){}
	SLATE_END_ARGS();

	void Construct(const FArguments&);
	void AssignSequence(ULexUIPrefabSequence* NewLexUIPrefabSequence);
	ULexUIPrefabSequence* GetSequence() const;
	FText GetDisplayLabel() const;
	TSharedPtr<ISequencer> GetSequencer() const;

private:

	TWeakPtr<SLexUIPrefabSequenceEditorWidgetImpl> Impl;
};

