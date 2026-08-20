// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DataFactory/DreamUIStaticMeshCacheFactory.h"
#include "Extensions/DreamStaticMesh.h"

#define LOCTEXT_NAMESPACE "UDreamUIStaticMeshCacheFactory"


UDreamUIStaticMeshCacheFactory::UDreamUIStaticMeshCacheFactory()
{
	SupportedClass = UDreamUIStaticMeshCacheData::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}
UObject* UDreamUIStaticMeshCacheFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	UDreamUIStaticMeshCacheData* NewAsset = NewObject<UDreamUIStaticMeshCacheData>(InParent, Class, Name, Flags | RF_Transactional);
	return NewAsset;
}

#undef LOCTEXT_NAMESPACE
