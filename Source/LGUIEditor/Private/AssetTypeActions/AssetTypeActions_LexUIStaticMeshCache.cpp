// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "AssetTypeActions/AssetTypeActions_LexUIStaticMeshCache.h"
#include "Extensions/LexStaticMesh.h"

#define LOCTEXT_NAMESPACE "AssetTypeActions_LexUIStaticMeshCache"

FAssetTypeActions_LexUIStaticMeshCache::FAssetTypeActions_LexUIStaticMeshCache(EAssetTypeCategories::Type InAssetType)
	: assetType(InAssetType)
{

}

bool FAssetTypeActions_LexUIStaticMeshCache::CanFilter()
{
	return true;
}

void FAssetTypeActions_LexUIStaticMeshCache::GetActions(const TArray<UObject*>& InObjects, FMenuBuilder& MenuBuilder)
{
	FAssetTypeActions_Base::GetActions(InObjects, MenuBuilder);
}

uint32 FAssetTypeActions_LexUIStaticMeshCache::GetCategories()
{
	return assetType;
}

FText FAssetTypeActions_LexUIStaticMeshCache::GetName()const
{
	return LOCTEXT("Name", "LexUI StaticMesh Cache");
}

UClass* FAssetTypeActions_LexUIStaticMeshCache::GetSupportedClass()const
{
	return ULexUIStaticMeshCacheData::StaticClass();
}

FColor FAssetTypeActions_LexUIStaticMeshCache::GetTypeColor()const
{
	return FColor::White;
}

bool FAssetTypeActions_LexUIStaticMeshCache::HasActions(const TArray<UObject*>& InObjects)const
{
	return true;
}


#undef LOCTEXT_NAMESPACE
