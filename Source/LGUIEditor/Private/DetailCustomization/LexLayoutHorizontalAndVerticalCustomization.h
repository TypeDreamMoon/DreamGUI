// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#pragma once

class ULexLayoutHorizontalAndVertical;
/**
 * 
 */
class FLexLayoutHorizontalAndVerticalCustomization : public IDetailCustomization
{
public:
	FLexLayoutHorizontalAndVerticalCustomization();
	~FLexLayoutHorizontalAndVerticalCustomization();

	static TSharedRef<IDetailCustomization> MakeInstance();
	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override {};
	virtual void CustomizeDetails(const TSharedPtr<IDetailLayoutBuilder>& DetailBuilder) override;
private:
	TWeakObjectPtr<ULexLayoutHorizontalAndVertical> TargetScriptPtr;
};
