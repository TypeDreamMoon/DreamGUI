// Copyright 2019-Present LexLiu. All Rights Reserved.
#include "DreamUIEventDelegateCustomization.h"

#pragma once

/**
 * 
 */
class DreamUIEventDelegatePresetParamCustomization : public FDreamUIEventDelegateCustomization
{
private:
	DreamUIEventDelegatePresetParamCustomization() :FDreamUIEventDelegateCustomization(false) {}
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance()
	{
		return MakeShareable(new DreamUIEventDelegatePresetParamCustomization());
	}
};
