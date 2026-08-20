// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "AssetTypeActions_DreamUIFontData_Bitmap.h"
#include "Core/DreamUIFontData_Bitmap.h"

#define LOCTEXT_NAMESPACE "AssetTypeActions_DreamUIFontData"

FAssetTypeActions_DreamUIFontData_Bitmap::FAssetTypeActions_DreamUIFontData_Bitmap(EAssetTypeCategories::Type InAssetType)
	: assetType(InAssetType)
{

}

bool FAssetTypeActions_DreamUIFontData_Bitmap::CanFilter()
{
	return true;
}

void FAssetTypeActions_DreamUIFontData_Bitmap::GetActions(const TArray<UObject*>& InObjects, FMenuBuilder& MenuBuilder)
{
	FAssetTypeActions_Base::GetActions(InObjects, MenuBuilder);
}

uint32 FAssetTypeActions_DreamUIFontData_Bitmap::GetCategories()
{
	return assetType;
}

FText FAssetTypeActions_DreamUIFontData_Bitmap::GetName()const
{
	return LOCTEXT("Name", "DreamUI Font Data Bitmap");
}

UClass* FAssetTypeActions_DreamUIFontData_Bitmap::GetSupportedClass()const
{
	return UDreamUIFontData_Bitmap::StaticClass();
}

FColor FAssetTypeActions_DreamUIFontData_Bitmap::GetTypeColor()const
{
	return FColor::White;
}

bool FAssetTypeActions_DreamUIFontData_Bitmap::HasActions(const TArray<UObject*>& InObjects)const
{
	return true;
}


#undef LOCTEXT_NAMESPACE
