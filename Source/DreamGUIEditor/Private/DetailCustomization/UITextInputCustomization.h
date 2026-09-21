// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#pragma once

/**
 * 
 */
class FUITextInputCustomization : public IDetailCustomization
{
public:
	FUITextInputCustomization();
	~FUITextInputCustomization();

	static TSharedRef<IDetailCustomization> MakeInstance();
	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
private:
	TWeakObjectPtr<class UUITextInput> TargetScriptPtr;
	/** Takes the utilities, not the layout builder: the builder does not survive the refresh it asks for. */
	void ForceRefresh(TSharedPtr<class IPropertyUtilities> PropertyUtilities);
};
