// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#pragma once

class ULexLayoutFlexBox;
/**
 * 
 */
class FLexLayoutFlexBoxCustomization : public IDetailCustomization
{
public:
	FLexLayoutFlexBoxCustomization();
	~FLexLayoutFlexBoxCustomization();

	static TSharedRef<IDetailCustomization> MakeInstance();
	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override {};
	virtual void CustomizeDetails(const TSharedPtr<IDetailLayoutBuilder>& DetailBuilder) override;
private:
	TWeakObjectPtr<ULexLayoutFlexBox> TargetScriptPtr;
};
