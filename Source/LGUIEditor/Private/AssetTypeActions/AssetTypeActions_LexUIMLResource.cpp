// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "AssetTypeActions_LexUIMLResource.h"
#include "XMLSupport/LexUIML.h"

#define LOCTEXT_NAMESPACE "AssetTypeActions_LexUIMLResource"

FAssetTypeActions_LexUIMLResource::FAssetTypeActions_LexUIMLResource(EAssetTypeCategories::Type InAssetType)
	: FAssetTypeActions_Base(), AssetType(InAssetType)
{
}

FText FAssetTypeActions_LexUIMLResource::GetName() const
{
	return LOCTEXT("Name", "LexUI XAML Resource");
}

UClass* FAssetTypeActions_LexUIMLResource::GetSupportedClass() const
{
	return ULexUIMLResource::StaticClass();
}

uint32 FAssetTypeActions_LexUIMLResource::GetCategories()
{
	return AssetType;
}

bool FAssetTypeActions_LexUIMLResource::CanFilter()
{
	return true;
}

#undef LOCTEXT_NAMESPACE
