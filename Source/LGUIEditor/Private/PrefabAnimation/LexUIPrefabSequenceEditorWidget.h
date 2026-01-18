// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "WorkflowOrientedApp/WorkflowUObjectDocuments.h"


class FBlueprintEditor;
class SLGUIPrefabSequenceEditorWidgetImpl;
class ULexUIPrefabSequence;
class ULexUIPrefabSequenceComponent;


class SLexUIPrefabSequenceEditorWidget
	: public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SLexUIPrefabSequenceEditorWidget){}
	SLATE_END_ARGS();

	void Construct(const FArguments&, TWeakPtr<FBlueprintEditor> InBlueprintEditor);
	void AssignSequence(ULexUIPrefabSequence* NewLGUIPrefabSequence);
	ULexUIPrefabSequence* GetSequence() const;
	FText GetDisplayLabel() const;

private:

	TWeakPtr<SLGUIPrefabSequenceEditorWidgetImpl> Impl;
};

