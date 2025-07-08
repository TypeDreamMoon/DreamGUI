// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "Widget/AnchorPreviewWidget.h"
#pragma once

/**
 * 
 */
class FLexWidgetCustomization : public IDetailCustomization
{
public:
	FLexWidgetCustomization();
	~FLexWidgetCustomization();

	static TSharedRef<IDetailCustomization> MakeInstance();
	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
private:
	TArray<TWeakObjectPtr<class ULexWidget>> TargetScriptArray;
	static TArray<float> ValueRangeArray;
	
	void ForceUpdateUI();

	void OnCopyHierarchyIndex();
	void OnPasteHierarchyIndex(TSharedRef<IPropertyHandle> PropertyHandle);
	FReply OnClickIncreaseOrDecreaseHierarchyIndex(bool IncreaseOrDecrease, TSharedRef<IPropertyHandle> HierarchyIndexHandle);

	EVisibility GetDisplayNameWarningVisibility()const;
	FReply OnClickFixDisplayNameButton(bool singleOrAll, TSharedRef<IPropertyHandle> DisplayNameHandle);

	void OnPrePivotChange();
	void OnPivotChanged();
};
