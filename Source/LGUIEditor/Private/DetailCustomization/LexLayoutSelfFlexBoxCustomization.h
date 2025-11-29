// Copyright 2019-Present LexLiu. All Rights Reserved.
#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#pragma once

/**
 * 
 */
class FLexLayoutSelfFlexBoxCustomization : public IDetailCustomization
{
public:
	FLexLayoutSelfFlexBoxCustomization();
	~FLexLayoutSelfFlexBoxCustomization();

	static TSharedRef<IDetailCustomization> MakeInstance();
	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
private:
	TArray<TWeakObjectPtr<class ULexLayoutSelfFlexBox>> TargetScriptArray;
};
