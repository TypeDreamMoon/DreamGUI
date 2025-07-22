// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "Core/LexUITextData.h"
#pragma once

/**
 * 
 */
class FLexLayoutSimpleAnchorSlotCustomization : public IDetailCustomization
{
public:
	FLexLayoutSimpleAnchorSlotCustomization();
	~FLexLayoutSimpleAnchorSlotCustomization();

	static TSharedRef<IDetailCustomization> MakeInstance();
	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override {};
	virtual void CustomizeDetails(const TSharedPtr<IDetailLayoutBuilder>& DetailBuilder) override;
private:
};
