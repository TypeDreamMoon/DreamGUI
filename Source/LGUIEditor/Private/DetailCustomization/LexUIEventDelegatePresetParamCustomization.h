// Copyright 2019-Present LexLiu. All Rights Reserved.
#include "LexUIEventDelegateCustomization.h"

#pragma once

/**
 * 
 */
class LexUIEventDelegatePresetParamCustomization : public FLexUIEventDelegateCustomization
{
private:
	LexUIEventDelegatePresetParamCustomization() :FLexUIEventDelegateCustomization(false) {}
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance()
	{
		return MakeShareable(new LexUIEventDelegatePresetParamCustomization());
	}
};
