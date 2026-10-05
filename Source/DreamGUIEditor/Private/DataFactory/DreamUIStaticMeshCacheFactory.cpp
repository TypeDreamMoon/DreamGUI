// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DataFactory/DreamUIStaticMeshCacheFactory.h"
#include "Extensions/DreamStaticMesh.h"

#define LOCTEXT_NAMESPACE "UDreamUIStaticMeshCacheFactory"


UDreamUIStaticMeshCacheFactory::UDreamUIStaticMeshCacheFactory()
{
	SupportedClass = UDreamUIStaticMeshCacheData::StaticClass();
	MenuSection = GraphicsSection();
	MenuLabel = NSLOCTEXT("DreamUIAssetMenu", "StaticMeshCache", "Static Mesh Cache");
	MenuToolTip = NSLOCTEXT("DreamUIAssetMenu", "StaticMeshCacheToolTip", "A static mesh's geometry kept for a Dream Static Mesh widget, which cannot read the mesh at runtime.");
	bCreateNew = true;
	bEditAfterNew = true;
}
UObject* UDreamUIStaticMeshCacheFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	UDreamUIStaticMeshCacheData* NewAsset = NewObject<UDreamUIStaticMeshCacheData>(InParent, Class, Name, Flags | RF_Transactional);
	return NewAsset;
}

#undef LOCTEXT_NAMESPACE
