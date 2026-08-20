// Copyright 2019-present LexLiu. All Rights Reserved.

#include "AssetTypeActions_DreamUIFontData_DistanceField.h"
#include "Core/DreamUIFontData_DistanceField.h"

#define LOCTEXT_NAMESPACE "AssetTypeActions_DreamUIFontData_DistanceField"

FAssetTypeActions_DreamUIFontData_DistanceField::FAssetTypeActions_DreamUIFontData_DistanceField(EAssetTypeCategories::Type InAssetType)
	: assetType(InAssetType)
{

}

bool FAssetTypeActions_DreamUIFontData_DistanceField::CanFilter()
{
	return true;
}

void FAssetTypeActions_DreamUIFontData_DistanceField::GetActions(const TArray<UObject*>& InObjects, FMenuBuilder& MenuBuilder)
{
	FAssetTypeActions_Base::GetActions(InObjects, MenuBuilder);
}

uint32 FAssetTypeActions_DreamUIFontData_DistanceField::GetCategories()
{
	return assetType;
}

FText FAssetTypeActions_DreamUIFontData_DistanceField::GetName()const
{
	return LOCTEXT("Name", "DreamUI FontData DistanceField");
}

UClass* FAssetTypeActions_DreamUIFontData_DistanceField::GetSupportedClass()const
{
	return UDreamUIFontData_DistanceField::StaticClass();
}

FColor FAssetTypeActions_DreamUIFontData_DistanceField::GetTypeColor()const
{
	return FColor::White;
}

bool FAssetTypeActions_DreamUIFontData_DistanceField::HasActions(const TArray<UObject*>& InObjects)const
{
	return true;
}


#undef LOCTEXT_NAMESPACE
