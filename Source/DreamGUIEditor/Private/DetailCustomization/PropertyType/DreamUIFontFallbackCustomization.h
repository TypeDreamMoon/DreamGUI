// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"
#include "Misc/Attribute.h"
#include "Templates/Function.h"

class SWidget;

/**
 * One entry of a font's fallback list (FDreamUIFontFallback). The header sums the entry up on one line -- its font, the
 * languages it is meant for, what its ranges cover, its scale -- so a list of them reads at a glance; the rows under it
 * are the entry's own, with an "Add Unicode block" menu on the ranges (each range shown in hex, with the block it is),
 * the engine's culture picker on the cultures, and a slider on the scale.
 */
class FDreamUIFontFallbackCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, class IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
};

/**
 * The engine's culture picker (SCulturePicker, from InternationalizationSettings) behind a combo button. The fallback
 * entries add cultures with it, a text's Language is picked with it, and so is the font's "Resolve Sample" language.
 */
namespace DreamUICulturePicker
{
	/**
	 * @param InButtonText  What the button says: usually the current choice, see GetCultureDisplayText.
	 * @param InNoneLabel   When not empty, an entry above the list that stands for no culture; picking it passes an empty name.
	 * @param InOnPicked    Called with the picked culture's name ("ja", "zh-Hans") once the menu has closed.
	 * @param InGetCurrent  The culture the list opens on; may be unset.
	 */
	TSharedRef<SWidget> MakeComboButton(const TAttribute<FText>& InButtonText, const FText& InToolTip, const FText& InNoneLabel,
		TFunction<void(const FString&)> InOnPicked, TFunction<FString()> InGetCurrent = TFunction<FString()>());

	/** "Japanese (ja)"; the name as it is for a culture the engine does not know; InEmptyText for no name at all. */
	FText GetCultureDisplayText(const FString& InCultureName, const FText& InEmptyText);
}
