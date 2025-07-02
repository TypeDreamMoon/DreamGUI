// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "Core/LexUITextData.h"
#pragma once

/**
 * 
 */
class FLexTextCustomization : public IDetailCustomization
{
public:
	FLexTextCustomization();
	~FLexTextCustomization();

	static TSharedRef<IDetailCustomization> MakeInstance();
	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
private:
	TWeakObjectPtr<class ULexText> TargetScriptPtr;
	void ForceRefresh(IDetailLayoutBuilder* DetailBuilder);
	void HandleHorizontalAlignmentCheckStateChanged(ECheckBoxState InCheckboxState, TSharedRef<IPropertyHandle> PropertyHandle, EUITextParagraphHorizontalAlign ToAlignment);
	ECheckBoxState GetHorizontalAlignmentCheckState(TSharedRef<IPropertyHandle> PropertyHandle, EUITextParagraphHorizontalAlign ForAlignment) const;
	void HandleVerticalAlignmentCheckStateChanged(ECheckBoxState InCheckboxState, TSharedRef<IPropertyHandle> PropertyHandle, EUITextParagraphVerticalAlign ToAlignment);
	ECheckBoxState GetVerticalAlignmentCheckState(TSharedRef<IPropertyHandle> PropertyHandle, EUITextParagraphVerticalAlign ForAlignment) const;

	void OnCopyAlignment();
	void OnPasteAlignment(TSharedRef<IPropertyHandle> HAlignPropertyHandle, TSharedRef<IPropertyHandle> VAlignPropertyHandle);
	bool OnCanPasteAlignment()const;
};
