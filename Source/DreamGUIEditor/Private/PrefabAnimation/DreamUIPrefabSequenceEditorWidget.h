// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "ISequencer.h"


class SDreamUIPrefabSequenceEditorWidgetImpl;
class UDreamUIPrefabSequence;

class SDreamUIPrefabSequenceEditorWidget
	: public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SDreamUIPrefabSequenceEditorWidget){}
	SLATE_END_ARGS();

	void Construct(const FArguments&);
	void AssignSequence(UDreamUIPrefabSequence* NewDreamUIPrefabSequence);
	UDreamUIPrefabSequence* GetSequence() const;
	FText GetDisplayLabel() const;
	TSharedPtr<ISequencer> GetSequencer() const;

private:

	TWeakPtr<SDreamUIPrefabSequenceEditorWidgetImpl> Impl;
};

