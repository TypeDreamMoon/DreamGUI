// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "AssetTypeActions_LexUIRichTextImageData.h"
#include "ContentBrowserModule.h"
#include "Core/LexUIRichTextImageData.h"

#define LOCTEXT_NAMESPACE "AssetTypeActions_LexUIRichTextImageData"

FAssetTypeActions_LexUIRichTextImageData::FAssetTypeActions_LexUIRichTextImageData(EAssetTypeCategories::Type InAssetType)
	: assetType(InAssetType)
{

}

bool FAssetTypeActions_LexUIRichTextImageData::CanFilter()
{
	return true;
}

void FAssetTypeActions_LexUIRichTextImageData::GetActions(const TArray<UObject*>& InObjects, FMenuBuilder& MenuBuilder)
{
	FAssetTypeActions_Base::GetActions(InObjects, MenuBuilder);
}

uint32 FAssetTypeActions_LexUIRichTextImageData::GetCategories()
{
	return assetType;
}

FText FAssetTypeActions_LexUIRichTextImageData::GetName()const
{
	return LOCTEXT("Name", "LexUI RichText Image Data");
}

UClass* FAssetTypeActions_LexUIRichTextImageData::GetSupportedClass()const
{
	return ULexUIRichTextImageData::StaticClass();
}

FColor FAssetTypeActions_LexUIRichTextImageData::GetTypeColor()const
{
	return FColor::White;
}

bool FAssetTypeActions_LexUIRichTextImageData::HasActions(const TArray<UObject*>& InObjects)const
{
	return true;
}


#undef LOCTEXT_NAMESPACE
