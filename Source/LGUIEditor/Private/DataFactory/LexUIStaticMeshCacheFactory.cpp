// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DataFactory/LexUIStaticMeshCacheFactory.h"
#include "Extensions/LexStaticMesh.h"

#define LOCTEXT_NAMESPACE "ULexUIStaticMeshCacheFactory"


ULexUIStaticMeshCacheFactory::ULexUIStaticMeshCacheFactory()
{
	SupportedClass = ULexUIStaticMeshCacheData::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}
UObject* ULexUIStaticMeshCacheFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	ULexUIStaticMeshCacheData* NewAsset = NewObject<ULexUIStaticMeshCacheData>(InParent, Class, Name, Flags | RF_Transactional);
	return NewAsset;
}

#undef LOCTEXT_NAMESPACE
