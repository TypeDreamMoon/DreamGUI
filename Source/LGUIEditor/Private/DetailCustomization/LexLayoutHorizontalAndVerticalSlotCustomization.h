// Copyright 2019-Present LexLiu. All Rights Reserved.
#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#pragma once

/**
 * 
 */
class FLexLayoutHorizontalAndVerticalSlotCustomization : public IDetailCustomization
{
public:
	FLexLayoutHorizontalAndVerticalSlotCustomization();
	~FLexLayoutHorizontalAndVerticalSlotCustomization();

	static TSharedRef<IDetailCustomization> MakeInstance();
	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
private:
	TArray<TWeakObjectPtr<class ULexLayoutHorizontalAndVerticalSlot>> TargetScriptArray;
};
