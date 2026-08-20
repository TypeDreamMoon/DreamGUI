// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DataFactory/DreamUIStaticSpriteAtlasDataFactory.h"
#include "Core/DreamUIStaticSpriteAtlasData.h"

#define LOCTEXT_NAMESPACE "DreamUIStaticSpriteAtalsDataFactory"


UDreamUIStaticSpriteAtlasDataFactory::UDreamUIStaticSpriteAtlasDataFactory()
{
	SupportedClass = UDreamUIStaticSpriteAtlasData::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}
UObject* UDreamUIStaticSpriteAtlasDataFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	auto NewAsset = NewObject<UDreamUIStaticSpriteAtlasData>(InParent, Class, Name, Flags | RF_Transactional);
	return NewAsset;
}

#undef LOCTEXT_NAMESPACE
