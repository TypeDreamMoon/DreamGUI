// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "AssetTypeActions_LexUIFontData_Bitmap.h"
#include "Core/LexUIFontData_Bitmap.h"

#define LOCTEXT_NAMESPACE "AssetTypeActions_LexUIFontData"

FAssetTypeActions_LexUIFontData_Bitmap::FAssetTypeActions_LexUIFontData_Bitmap(EAssetTypeCategories::Type InAssetType)
	: assetType(InAssetType)
{

}

bool FAssetTypeActions_LexUIFontData_Bitmap::CanFilter()
{
	return true;
}

void FAssetTypeActions_LexUIFontData_Bitmap::GetActions(const TArray<UObject*>& InObjects, FMenuBuilder& MenuBuilder)
{
	FAssetTypeActions_Base::GetActions(InObjects, MenuBuilder);
}

uint32 FAssetTypeActions_LexUIFontData_Bitmap::GetCategories()
{
	return assetType;
}

FText FAssetTypeActions_LexUIFontData_Bitmap::GetName()const
{
	return LOCTEXT("Name", "LexUI Font Data Bitmap");
}

UClass* FAssetTypeActions_LexUIFontData_Bitmap::GetSupportedClass()const
{
	return ULexUIFontData_Bitmap::StaticClass();
}

FColor FAssetTypeActions_LexUIFontData_Bitmap::GetTypeColor()const
{
	return FColor::White;
}

bool FAssetTypeActions_LexUIFontData_Bitmap::HasActions(const TArray<UObject*>& InObjects)const
{
	return true;
}


#undef LOCTEXT_NAMESPACE
