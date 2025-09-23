// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "AssetTypeActions_LexUIRichTextCustomStyleData.h"
#include "ContentBrowserModule.h"
#include "Core/LexUIRichTextCustomStyleData.h"

#define LOCTEXT_NAMESPACE "AssetTypeActions_LexUIRichTextCustomStyleData"

FAssetTypeActions_LexUIRichTextCustomStyleData::FAssetTypeActions_LexUIRichTextCustomStyleData(EAssetTypeCategories::Type InAssetType)
	: assetType(InAssetType)
{

}

bool FAssetTypeActions_LexUIRichTextCustomStyleData::CanFilter()
{
	return true;
}

void FAssetTypeActions_LexUIRichTextCustomStyleData::GetActions(const TArray<UObject*>& InObjects, FMenuBuilder& MenuBuilder)
{
	FAssetTypeActions_Base::GetActions(InObjects, MenuBuilder);
}

uint32 FAssetTypeActions_LexUIRichTextCustomStyleData::GetCategories()
{
	return assetType;
}

FText FAssetTypeActions_LexUIRichTextCustomStyleData::GetName()const
{
	return LOCTEXT("Name", "LexUI RichText Custom Style Data");
}

UClass* FAssetTypeActions_LexUIRichTextCustomStyleData::GetSupportedClass()const
{
	return ULexUIRichTextCustomStyleData::StaticClass();
}

FColor FAssetTypeActions_LexUIRichTextCustomStyleData::GetTypeColor()const
{
	return FColor::White;
}

bool FAssetTypeActions_LexUIRichTextCustomStyleData::HasActions(const TArray<UObject*>& InObjects)const
{
	return true;
}


#undef LOCTEXT_NAMESPACE
