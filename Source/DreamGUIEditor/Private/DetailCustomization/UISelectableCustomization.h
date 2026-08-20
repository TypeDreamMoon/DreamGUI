// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "DreamUIEditorUtils.h"
#pragma once

/**
 * 
 */
class FUISelectableCustomization : public IDetailCustomization
{
public:
	~FUISelectableCustomization();
	static TSharedRef<IDetailCustomization> MakeInstance();
	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
private:
	TWeakObjectPtr<class UUISelectable> TargetScriptPtr;
	void ForceRefresh(IDetailLayoutBuilder* DetailBuilder);
};
