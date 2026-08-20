// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "AssetTypeActions_DreamUIStaticSpriteAtlasData.h"
#include "Core/DreamUIStaticSpriteAtlasData.h"

#define LOCTEXT_NAMESPACE "AssetTypeActions_DreamUIStaticSpriteAtlasData"

FAssetTypeActions_DreamUIStaticSpriteAtlasData::FAssetTypeActions_DreamUIStaticSpriteAtlasData(EAssetTypeCategories::Type InAssetType)
	: assetType(InAssetType)
{

}

bool FAssetTypeActions_DreamUIStaticSpriteAtlasData::CanFilter()
{
	return true;
}

void FAssetTypeActions_DreamUIStaticSpriteAtlasData::GetActions(const TArray<UObject*>& InObjects, FMenuBuilder& MenuBuilder)
{
	FAssetTypeActions_Base::GetActions(InObjects, MenuBuilder);
}

uint32 FAssetTypeActions_DreamUIStaticSpriteAtlasData::GetCategories()
{
	return assetType;
}

FText FAssetTypeActions_DreamUIStaticSpriteAtlasData::GetName()const
{
	return LOCTEXT("Name", "DreamUI Static Sprite Atlas Data");
}

UClass* FAssetTypeActions_DreamUIStaticSpriteAtlasData::GetSupportedClass()const
{
	return UDreamUIStaticSpriteAtlasData::StaticClass();
}

FColor FAssetTypeActions_DreamUIStaticSpriteAtlasData::GetTypeColor()const
{
	return FColor::White;
}

bool FAssetTypeActions_DreamUIStaticSpriteAtlasData::HasActions(const TArray<UObject*>& InObjects)const
{
	return true;
}


#undef LOCTEXT_NAMESPACE
