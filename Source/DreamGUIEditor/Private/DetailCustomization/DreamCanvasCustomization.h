// Copyright 2019-Present LexLiu. All Rights Reserved.
#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#pragma once

/**
 * 
 */
class FDreamCanvasCustomization : public IDetailCustomization
{
public:
	FDreamCanvasCustomization();
	~FDreamCanvasCustomization();

	static TSharedRef<IDetailCustomization> MakeInstance();
	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
private:
	TArray<TWeakObjectPtr<class UDreamCanvas>> TargetScriptArray;
	void ForceRefresh(class IDetailLayoutBuilder* DetailBuilder);
	FText GetDrawcallInfo()const;
	FText GetDrawcallInfoTooltip()const;
	void OnCopySortOrder();
	void OnPasteSortOrder(TSharedRef<class IPropertyHandle> PropertyHandle);
	FReply OnClickFixClipTextureSetting(TSharedRef<IPropertyHandle> ClipTextureHandle);
	bool IsFixClipTextureEnabled(TSharedRef<IPropertyHandle> ClipTextureHandle)const;

	TSharedPtr<class IDetailsView> PropertyView;

	TSharedPtr<SHorizontalBox> ValueBox;
	FOptionalSize GetValueWidth()const;
};
