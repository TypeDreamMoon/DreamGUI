// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DataFactory/LGUIStaticSpriteAtalsDataFactory.h"
#include "Core/LexUIStaticSpriteAtlasData.h"

#define LOCTEXT_NAMESPACE "LGUIStaticSpriteAtalsDataFactory"


ULGUIStaticSpriteAtalsDataFactory::ULGUIStaticSpriteAtalsDataFactory()
{
	SupportedClass = ULexUIStaticSpriteAtlasData::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}
UObject* ULGUIStaticSpriteAtalsDataFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	auto NewAsset = NewObject<ULexUIStaticSpriteAtlasData>(InParent, Class, Name, Flags | RF_Transactional);
	return NewAsset;
}

#undef LOCTEXT_NAMESPACE
