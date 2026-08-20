// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "AssetTypeActions_DreamUIRichTextCustomStyleData.h"
#include "Core/DreamUIRichTextCustomStyleData.h"

#define LOCTEXT_NAMESPACE "AssetTypeActions_DreamUIRichTextCustomStyleData"

FAssetTypeActions_DreamUIRichTextCustomStyleData::FAssetTypeActions_DreamUIRichTextCustomStyleData(EAssetTypeCategories::Type InAssetType)
	: assetType(InAssetType)
{

}

bool FAssetTypeActions_DreamUIRichTextCustomStyleData::CanFilter()
{
	return true;
}

void FAssetTypeActions_DreamUIRichTextCustomStyleData::GetActions(const TArray<UObject*>& InObjects, FMenuBuilder& MenuBuilder)
{
	FAssetTypeActions_Base::GetActions(InObjects, MenuBuilder);
}

uint32 FAssetTypeActions_DreamUIRichTextCustomStyleData::GetCategories()
{
	return assetType;
}

FText FAssetTypeActions_DreamUIRichTextCustomStyleData::GetName()const
{
	return LOCTEXT("Name", "DreamUI RichText Custom Style Data");
}

UClass* FAssetTypeActions_DreamUIRichTextCustomStyleData::GetSupportedClass()const
{
	return UDreamUIRichTextCustomStyleData::StaticClass();
}

FColor FAssetTypeActions_DreamUIRichTextCustomStyleData::GetTypeColor()const
{
	return FColor::White;
}

bool FAssetTypeActions_DreamUIRichTextCustomStyleData::HasActions(const TArray<UObject*>& InObjects)const
{
	return true;
}


#undef LOCTEXT_NAMESPACE
