// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "AssetTypeActions_LexUISpriteData.h"
#include "ContentBrowserModule.h"
#include "Core/LexUISpriteData.h"

#define LOCTEXT_NAMESPACE "AssetTypeActions_LexUISpriteData"

FAssetTypeActions_LexUISpriteData::FAssetTypeActions_LexUISpriteData(EAssetTypeCategories::Type InAssetType)
	: assetType(InAssetType)
{

}

bool FAssetTypeActions_LexUISpriteData::CanFilter()
{
	return true;
}

void FAssetTypeActions_LexUISpriteData::GetActions(const TArray<UObject*>& InObjects, FMenuBuilder& MenuBuilder)
{
	FAssetTypeActions_Base::GetActions(InObjects, MenuBuilder);
}

uint32 FAssetTypeActions_LexUISpriteData::GetCategories()
{
	return assetType;
}

FText FAssetTypeActions_LexUISpriteData::GetName()const
{
	return LOCTEXT("Name", "LexUI Sprite Data");
}

UClass* FAssetTypeActions_LexUISpriteData::GetSupportedClass()const
{
	return ULexUISpriteData::StaticClass();
}

FColor FAssetTypeActions_LexUISpriteData::GetTypeColor()const
{
	return FColor::White;
}

bool FAssetTypeActions_LexUISpriteData::HasActions(const TArray<UObject*>& InObjects)const
{
	return true;
}

#undef LOCTEXT_NAMESPACE
