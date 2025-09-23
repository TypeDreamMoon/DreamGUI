// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DataFactory/LexUIStaticSpriteAtlasDataFactory.h"
#include "Core/LexUIStaticSpriteAtlasData.h"

#define LOCTEXT_NAMESPACE "LexUIStaticSpriteAtalsDataFactory"


ULexUIStaticSpriteAtlasDataFactory::ULexUIStaticSpriteAtlasDataFactory()
{
	SupportedClass = ULexUIStaticSpriteAtlasData::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}
UObject* ULexUIStaticSpriteAtlasDataFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	auto NewAsset = NewObject<ULexUIStaticSpriteAtlasData>(InParent, Class, Name, Flags | RF_Transactional);
	return NewAsset;
}

#undef LOCTEXT_NAMESPACE
