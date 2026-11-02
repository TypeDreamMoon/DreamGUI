// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#pragma once

/**
 * 
 */
class FDreamUIFontData_FreeTypeRenderCustomization : public IDetailCustomization
{
public:

	static TSharedRef<IDetailCustomization> MakeInstance();
	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
private:
	TWeakObjectPtr<class UDreamUIFontData_FreeTypeRender> TargetScriptPtr;
	// These take the property utilities, not the layout builder: the builder does not survive the refresh
	// they ask for, and a widget callback can fire after it is gone.
	FReply OnReloadButtonClicked(TWeakPtr<class IPropertyUtilities> InPropertyUtilities);
	FText OnGetFontFilePath()const;
	void OnPathTextChanged(const FString& InText, TSharedRef<IPropertyHandle> InPathProperty);
	void OnPathTextCommitted(const FString& InText, TSharedRef<IPropertyHandle> InPathProperty, TWeakPtr<class IPropertyUtilities> InPropertyUtilities);
	void ForceRefresh(TWeakPtr<class IPropertyUtilities> InPropertyUtilities);
	TArray<TSharedPtr<FString>> FontFaceOptions;
	TSharedRef<ITableRow> FontFaceOptions_GenerateComboItem(TSharedPtr<FString> InItem, const TSharedRef<STableViewBase>& OwnerTable);
	void FontFaceOptions_OnComboChanged(TSharedPtr<FString> Item, ESelectInfo::Type SelectInfo, TSharedRef<IPropertyHandle> InProperty, TWeakPtr<class IPropertyUtilities> InPropertyUtilities);
	FText FontFaceOptions_GetCurrentFace()const;

	FText GetCurrentValue() const;
	void OnFontFaceComboSelectionChanged(TSharedPtr<FString> InSelectedItem, ESelectInfo::Type SelectInfo, TSharedRef<IPropertyHandle> fontFaceHandle);

	/**
	 * The "Resolve Sample" row: a sample text and a language, and for each grapheme cluster of the sample the face that
	 * draws it -- asked of FDreamFontFaceResolver, as a text's layout asks it, so what the row says is what a text does.
	 */
	void AddResolveSampleRow(class IDetailCategoryBuilder& Category);
	/** Resolves the sample again: it changed, its language did, or the fallbacks it is resolved through did. */
	void RefreshResolveSample();
	void OnResolveSampleTextChanged(const FText& InText);
	void OnResolveSampleCulturePicked(const FString& InCultureName);
	TSharedPtr<class SVerticalBox> ResolveResultsBox;
	/**
	 * The sample and its culture (empty for the game's language). They outlive the panel -- a reload or a face change
	 * builds it again -- and every font's panel shares them, so two fonts can be compared on the same text.
	 */
	static FString ResolveSampleText;
	static FString ResolveSampleCulture;
};
