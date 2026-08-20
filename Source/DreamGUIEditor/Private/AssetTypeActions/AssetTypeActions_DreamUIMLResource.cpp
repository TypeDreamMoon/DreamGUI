// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "AssetTypeActions_DreamUIMLResource.h"
#include "XMLSupport/DreamUIML.h"

#define LOCTEXT_NAMESPACE "AssetTypeActions_DreamUIMLResource"

FAssetTypeActions_DreamUIMLResource::FAssetTypeActions_DreamUIMLResource(EAssetTypeCategories::Type InAssetType)
	: FAssetTypeActions_Base(), AssetType(InAssetType)
{
}

FText FAssetTypeActions_DreamUIMLResource::GetName() const
{
	return LOCTEXT("Name", "DreamUI XAML Resource");
}

UClass* FAssetTypeActions_DreamUIMLResource::GetSupportedClass() const
{
	return UDreamUIMLResource::StaticClass();
}

uint32 FAssetTypeActions_DreamUIMLResource::GetCategories()
{
	return AssetType;
}

bool FAssetTypeActions_DreamUIMLResource::CanFilter()
{
	return true;
}

#undef LOCTEXT_NAMESPACE
