// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "AssetTypeActions_DreamUIFontEmojiData.h"
#include "Core/DreamUIFontEmojiData.h"

#define LOCTEXT_NAMESPACE "AssetTypeActions_DreamUIFontEmojiData"

FAssetTypeActions_DreamUIFontEmojiData::FAssetTypeActions_DreamUIFontEmojiData(EAssetTypeCategories::Type InAssetType)
	: assetType(InAssetType)
{

}

bool FAssetTypeActions_DreamUIFontEmojiData::CanFilter()
{
	return true;
}

void FAssetTypeActions_DreamUIFontEmojiData::GetActions(const TArray<UObject*>& InObjects, FMenuBuilder& MenuBuilder)
{
	FAssetTypeActions_Base::GetActions(InObjects, MenuBuilder);
}

uint32 FAssetTypeActions_DreamUIFontEmojiData::GetCategories()
{
	return assetType;
}

FText FAssetTypeActions_DreamUIFontEmojiData::GetName()const
{
	return LOCTEXT("Name", "DreamUI Font Emoji Data");
}

UClass* FAssetTypeActions_DreamUIFontEmojiData::GetSupportedClass()const
{
	return UDreamUIFontEmojiData::StaticClass();
}

FColor FAssetTypeActions_DreamUIFontEmojiData::GetTypeColor()const
{
	return FColor::White;
}

bool FAssetTypeActions_DreamUIFontEmojiData::HasActions(const TArray<UObject*>& InObjects)const
{
	return true;
}


#undef LOCTEXT_NAMESPACE
