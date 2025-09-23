// Copyright 2019-present LexLiu. All Rights Reserved.

#include "AssetTypeActions_LexUIFontData_DistanceField.h"
#include "Core/LexUIFontData_DistanceField.h"
#include "ContentBrowserModule.h"

#define LOCTEXT_NAMESPACE "AssetTypeActions_LexUIFontData_DistanceField"

FAssetTypeActions_LexUIFontData_DistanceField::FAssetTypeActions_LexUIFontData_DistanceField(EAssetTypeCategories::Type InAssetType)
	: assetType(InAssetType)
{

}

bool FAssetTypeActions_LexUIFontData_DistanceField::CanFilter()
{
	return true;
}

void FAssetTypeActions_LexUIFontData_DistanceField::GetActions(const TArray<UObject*>& InObjects, FMenuBuilder& MenuBuilder)
{
	FAssetTypeActions_Base::GetActions(InObjects, MenuBuilder);
}

uint32 FAssetTypeActions_LexUIFontData_DistanceField::GetCategories()
{
	return assetType;
}

FText FAssetTypeActions_LexUIFontData_DistanceField::GetName()const
{
	return LOCTEXT("Name", "LexUI FontData DistanceField");
}

UClass* FAssetTypeActions_LexUIFontData_DistanceField::GetSupportedClass()const
{
	return ULexUIFontData_DistanceField::StaticClass();
}

FColor FAssetTypeActions_LexUIFontData_DistanceField::GetTypeColor()const
{
	return FColor::White;
}

bool FAssetTypeActions_LexUIFontData_DistanceField::HasActions(const TArray<UObject*>& InObjects)const
{
	return true;
}


#undef LOCTEXT_NAMESPACE
