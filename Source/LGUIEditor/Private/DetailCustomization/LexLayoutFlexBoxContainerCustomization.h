// Copyright 2019-Present LexLiu. All Rights Reserved.
#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#pragma once

/**
 * 
 */
class FLexLayoutFlexBoxContainerCustomization : public IDetailCustomization
{
public:
	FLexLayoutFlexBoxContainerCustomization();
	~FLexLayoutFlexBoxContainerCustomization();

	static TSharedRef<IDetailCustomization> MakeInstance();
	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
private:
	TArray<TWeakObjectPtr<class ULexLayoutFlexBoxContainer>> TargetScriptArray;
};
