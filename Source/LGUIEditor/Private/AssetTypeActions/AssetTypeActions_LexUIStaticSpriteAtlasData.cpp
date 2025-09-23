// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "AssetTypeActions_LexUIStaticSpriteAtlasData.h"
#include "ContentBrowserModule.h"
#include "Core/LexUIStaticSpriteAtlasData.h"

#define LOCTEXT_NAMESPACE "AssetTypeActions_LexUIStaticSpriteAtlasData"

FAssetTypeActions_LexUIStaticSpriteAtlasData::FAssetTypeActions_LexUIStaticSpriteAtlasData(EAssetTypeCategories::Type InAssetType)
	: assetType(InAssetType)
{

}

bool FAssetTypeActions_LexUIStaticSpriteAtlasData::CanFilter()
{
	return true;
}

void FAssetTypeActions_LexUIStaticSpriteAtlasData::GetActions(const TArray<UObject*>& InObjects, FMenuBuilder& MenuBuilder)
{
	FAssetTypeActions_Base::GetActions(InObjects, MenuBuilder);
}

uint32 FAssetTypeActions_LexUIStaticSpriteAtlasData::GetCategories()
{
	return assetType;
}

FText FAssetTypeActions_LexUIStaticSpriteAtlasData::GetName()const
{
	return LOCTEXT("Name", "LexUI Static Sprite Atlas Data");
}

UClass* FAssetTypeActions_LexUIStaticSpriteAtlasData::GetSupportedClass()const
{
	return ULexUIStaticSpriteAtlasData::StaticClass();
}

FColor FAssetTypeActions_LexUIStaticSpriteAtlasData::GetTypeColor()const
{
	return FColor::White;
}

bool FAssetTypeActions_LexUIStaticSpriteAtlasData::HasActions(const TArray<UObject*>& InObjects)const
{
	return true;
}


#undef LOCTEXT_NAMESPACE
