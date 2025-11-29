// Copyright 2019-Present LexLiu. All Rights Reserved.
#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#pragma once

/**
 * 
 */
class FLexLayoutContainerFlexBoxCustomization : public IDetailCustomization
{
public:
	FLexLayoutContainerFlexBoxCustomization();
	~FLexLayoutContainerFlexBoxCustomization();

	static TSharedRef<IDetailCustomization> MakeInstance();
	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
private:
	TArray<TWeakObjectPtr<class ULexLayoutContainerFlexBox>> TargetScriptArray;
};
