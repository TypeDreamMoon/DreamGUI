// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#pragma once

/**
 * 
 */
class FDreamUISpriteDataCustomization : public IDetailCustomization
{
public:
	~FDreamUISpriteDataCustomization();
	static TSharedRef<IDetailCustomization> MakeInstance();
	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
private:
	TWeakObjectPtr<class UDreamUISpriteData> TargetScriptPtr;

	TSharedPtr<FSlateBrush> spriteSlateBrush;
	TSharedPtr<SBox> ImageBox;
	/** Takes the view, not the layout builder: the builder does not survive a refresh and this is polled every frame. */
	FOptionalSize GetMinDesiredHeight(TWeakPtr<class IDetailsView> InDetailsView)const;
	FOptionalSize GetImageWidth()const;
	FOptionalSize GetImageHeight()const;
	FOptionalSize GetBorderLeftSize()const;
	FOptionalSize GetBorderRightSize()const;
	FOptionalSize GetBorderTopSize()const;
	FOptionalSize GetBorderBottomSize()const;
	bool IsApplyButtonEnabled(TSharedPtr<IPropertyHandle> BorderDirtyPropertyHandle)const;

	// These take the property utilities, not the layout builder: the builder does not survive the refresh
	// they ask for, and a widget callback can fire after it is gone.
	void OnPackingTagTextCommited(const FText& InText, ETextCommit::Type CommitType, TSharedRef<IPropertyHandle> InProperty, TWeakPtr<class IPropertyUtilities> InPropertyUtilities);
	FText GetPackingTagText(TSharedRef<IPropertyHandle> InProperty)const;
	TSharedRef<ITableRow> GenerateComboItem(TSharedPtr<FName> InItem, const TSharedRef<STableViewBase>& OwnerTable);
	void HandleRequiredParamComboChanged(TSharedPtr<FName> Item, ESelectInfo::Type SelectInfo, TSharedRef<IPropertyHandle> InProperty, TWeakPtr<class IPropertyUtilities> InPropertyUtilities);
	TArray<TSharedPtr<FName>> NameList;
	/** The packing-tag dropdown, so a new option list can redraw it without rebuilding the whole panel. */
	TWeakPtr<class STableViewBase> NameListView;
	/** Rebuilds NameList from the atlas manager and redraws the dropdown. */
	void RefreshNameList();
	/** The subscription to the atlas manager's OnAtlasMapChanged, held so it can be dropped again. */
	FDelegateHandle AtlasMapChangedHandle;
	TWeakObjectPtr<class UDreamUIDynamicSpriteAtlasManager> SubscribedAtlasManager;
	void UnsubscribeAtlasMapChanged();
	void CheckSprite();
};
