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
	/** Weak, because the delegate that calls this outlives the layout that registered it. */
	void ForceRefresh(TWeakPtr<class IPropertyUtilities> InPropertyUtilities);
	FText GetDrawcallInfo()const;
	FText GetDrawcallInfoTooltip()const;
	/** The text GetDrawcallInfo last worked out, and when; it is polled every frame but recomputed on a timer. */
	mutable FText CachedDrawcallInfo;
	mutable double LastDrawcallInfoTime = 0.0;
	void OnCopySortOrder();
	void OnPasteSortOrder(TSharedRef<class IPropertyHandle> PropertyHandle);
	FReply OnClickFixClipTextureSetting(TSharedRef<IPropertyHandle> ClipTextureHandle);
	bool IsFixClipTextureEnabled(TSharedRef<IPropertyHandle> ClipTextureHandle)const;

	TSharedPtr<class IDetailsView> PropertyView;

	TSharedPtr<SHorizontalBox> ValueBox;
	FOptionalSize GetValueWidth()const;
};
