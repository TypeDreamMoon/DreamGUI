// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "AssetTypeActions_DreamUIRichTextImageData.h"
#include "Core/DreamUIRichTextImageData.h"

#define LOCTEXT_NAMESPACE "AssetTypeActions_DreamUIRichTextImageData"

FAssetTypeActions_DreamUIRichTextImageData::FAssetTypeActions_DreamUIRichTextImageData(EAssetTypeCategories::Type InAssetType)
	: assetType(InAssetType)
{

}

bool FAssetTypeActions_DreamUIRichTextImageData::CanFilter()
{
	return true;
}

void FAssetTypeActions_DreamUIRichTextImageData::GetActions(const TArray<UObject*>& InObjects, FMenuBuilder& MenuBuilder)
{
	FAssetTypeActions_Base::GetActions(InObjects, MenuBuilder);
}

uint32 FAssetTypeActions_DreamUIRichTextImageData::GetCategories()
{
	return assetType;
}

FText FAssetTypeActions_DreamUIRichTextImageData::GetName()const
{
	return LOCTEXT("Name", "DreamUI RichText Image Data");
}

UClass* FAssetTypeActions_DreamUIRichTextImageData::GetSupportedClass()const
{
	return UDreamUIRichTextImageData::StaticClass();
}

FColor FAssetTypeActions_DreamUIRichTextImageData::GetTypeColor()const
{
	return FColor::White;
}

bool FAssetTypeActions_DreamUIRichTextImageData::HasActions(const TArray<UObject*>& InObjects)const
{
	return true;
}


#undef LOCTEXT_NAMESPACE
