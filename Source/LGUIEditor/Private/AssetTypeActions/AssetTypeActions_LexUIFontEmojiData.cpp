// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "AssetTypeActions_LexUIFontEmojiData.h"
#include "Core/LexUIFontEmojiData.h"

#define LOCTEXT_NAMESPACE "AssetTypeActions_LexUIFontEmojiData"

FAssetTypeActions_LexUIFontEmojiData::FAssetTypeActions_LexUIFontEmojiData(EAssetTypeCategories::Type InAssetType)
	: assetType(InAssetType)
{

}

bool FAssetTypeActions_LexUIFontEmojiData::CanFilter()
{
	return true;
}

void FAssetTypeActions_LexUIFontEmojiData::GetActions(const TArray<UObject*>& InObjects, FMenuBuilder& MenuBuilder)
{
	FAssetTypeActions_Base::GetActions(InObjects, MenuBuilder);
}

uint32 FAssetTypeActions_LexUIFontEmojiData::GetCategories()
{
	return assetType;
}

FText FAssetTypeActions_LexUIFontEmojiData::GetName()const
{
	return LOCTEXT("Name", "LexUI Font Emoji Data");
}

UClass* FAssetTypeActions_LexUIFontEmojiData::GetSupportedClass()const
{
	return ULexUIFontEmojiData::StaticClass();
}

FColor FAssetTypeActions_LexUIFontEmojiData::GetTypeColor()const
{
	return FColor::White;
}

bool FAssetTypeActions_LexUIFontEmojiData::HasActions(const TArray<UObject*>& InObjects)const
{
	return true;
}


#undef LOCTEXT_NAMESPACE
