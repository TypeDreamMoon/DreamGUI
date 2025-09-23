// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "AssetTypeActions_LexUIFontData.h"
#include "ContentBrowserModule.h"
#include "Core/LexUIFontData_Bitmap.h"

#define LOCTEXT_NAMESPACE "AssetTypeActions_LexUIFontData"

FAssetTypeActions_LexUIFontData::FAssetTypeActions_LexUIFontData(EAssetTypeCategories::Type InAssetType)
	: assetType(InAssetType)
{

}

bool FAssetTypeActions_LexUIFontData::CanFilter()
{
	return true;
}

void FAssetTypeActions_LexUIFontData::GetActions(const TArray<UObject*>& InObjects, FMenuBuilder& MenuBuilder)
{
	FAssetTypeActions_Base::GetActions(InObjects, MenuBuilder);
}

uint32 FAssetTypeActions_LexUIFontData::GetCategories()
{
	return assetType;
}

FText FAssetTypeActions_LexUIFontData::GetName()const
{
	return LOCTEXT("Name", "LexUI Font Data");
}

UClass* FAssetTypeActions_LexUIFontData::GetSupportedClass()const
{
	return ULexUIFontData_Bitmap::StaticClass();
}

FColor FAssetTypeActions_LexUIFontData::GetTypeColor()const
{
	return FColor::White;
}

bool FAssetTypeActions_LexUIFontData::HasActions(const TArray<UObject*>& InObjects)const
{
	return true;
}


#undef LOCTEXT_NAMESPACE
