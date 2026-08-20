// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "AssetTypeActions_DreamUISpriteData.h"
#include "Core/DreamUISpriteData.h"

#define LOCTEXT_NAMESPACE "AssetTypeActions_DreamUISpriteData"

FAssetTypeActions_DreamUISpriteData::FAssetTypeActions_DreamUISpriteData(EAssetTypeCategories::Type InAssetType)
	: assetType(InAssetType)
{

}

bool FAssetTypeActions_DreamUISpriteData::CanFilter()
{
	return true;
}

void FAssetTypeActions_DreamUISpriteData::GetActions(const TArray<UObject*>& InObjects, FMenuBuilder& MenuBuilder)
{
	FAssetTypeActions_Base::GetActions(InObjects, MenuBuilder);
}

uint32 FAssetTypeActions_DreamUISpriteData::GetCategories()
{
	return assetType;
}

FText FAssetTypeActions_DreamUISpriteData::GetName()const
{
	return LOCTEXT("Name", "DreamUI Sprite Data");
}

UClass* FAssetTypeActions_DreamUISpriteData::GetSupportedClass()const
{
	return UDreamUISpriteData::StaticClass();
}

FColor FAssetTypeActions_DreamUISpriteData::GetTypeColor()const
{
	return FColor::White;
}

bool FAssetTypeActions_DreamUISpriteData::HasActions(const TArray<UObject*>& InObjects)const
{
	return true;
}

#undef LOCTEXT_NAMESPACE
