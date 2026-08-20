// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "AssetTypeActions/AssetTypeActions_DreamUIStaticMeshCache.h"
#include "Extensions/DreamStaticMesh.h"

#define LOCTEXT_NAMESPACE "AssetTypeActions_DreamUIStaticMeshCache"

FAssetTypeActions_DreamUIStaticMeshCache::FAssetTypeActions_DreamUIStaticMeshCache(EAssetTypeCategories::Type InAssetType)
	: assetType(InAssetType)
{

}

bool FAssetTypeActions_DreamUIStaticMeshCache::CanFilter()
{
	return true;
}

void FAssetTypeActions_DreamUIStaticMeshCache::GetActions(const TArray<UObject*>& InObjects, FMenuBuilder& MenuBuilder)
{
	FAssetTypeActions_Base::GetActions(InObjects, MenuBuilder);
}

uint32 FAssetTypeActions_DreamUIStaticMeshCache::GetCategories()
{
	return assetType;
}

FText FAssetTypeActions_DreamUIStaticMeshCache::GetName()const
{
	return LOCTEXT("Name", "DreamUI StaticMesh Cache");
}

UClass* FAssetTypeActions_DreamUIStaticMeshCache::GetSupportedClass()const
{
	return UDreamUIStaticMeshCacheData::StaticClass();
}

FColor FAssetTypeActions_DreamUIStaticMeshCache::GetTypeColor()const
{
	return FColor::White;
}

bool FAssetTypeActions_DreamUIStaticMeshCache::HasActions(const TArray<UObject*>& InObjects)const
{
	return true;
}


#undef LOCTEXT_NAMESPACE
