// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "ISequencer.h"


class SDreamWidgetAnimationEditorWidgetImpl;
class UDreamUIPrefabSequence;

class SDreamWidgetAnimationEditorWidget
	: public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SDreamWidgetAnimationEditorWidget){}
	SLATE_END_ARGS();

	void Construct(const FArguments&);
	/** The window whose tab manager hosts spawned side panels (the curve editor). Defaults to the level editor. */
	void SetToolkitHost(TSharedPtr<class IToolkitHost> InToolkitHost);
	void AssignSequence(UDreamUIPrefabSequence* NewDreamUIPrefabSequence);
	UDreamUIPrefabSequence* GetSequence() const;
	FText GetDisplayLabel() const;
	TSharedPtr<ISequencer> GetSequencer() const;

private:

	TWeakPtr<SDreamWidgetAnimationEditorWidgetImpl> Impl;
};

