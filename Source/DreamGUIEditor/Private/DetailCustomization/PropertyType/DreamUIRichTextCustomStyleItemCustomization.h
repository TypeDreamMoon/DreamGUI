// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"

/**
 * One entry of a rich text's custom styles (FDreamUIRichTextCustomStyleItemData): its fields as the default layout shows
 * them, except its paint. That shows only while paintType is Set, as its Preset and Gradient: the paint's bEnabled is
 * not read for a custom style, so it is not offered, and it no longer greys the two out.
 */
class FDreamUIRichTextCustomStyleItemCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, class IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
};
