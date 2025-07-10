// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#pragma once

class ULexLayoutHorizontalAndVerticalSlot;
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
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override {};
	virtual void CustomizeDetails(const TSharedPtr<IDetailLayoutBuilder>& DetailBuilder) override;
private:
	bool GetAlignmentEnabled(bool HorizontalOrVertical, TArray<TWeakObjectPtr<UObject>> TargetObjects) const;
	TWeakObjectPtr<ULexLayoutHorizontalAndVerticalSlot> TargetScript;
};
