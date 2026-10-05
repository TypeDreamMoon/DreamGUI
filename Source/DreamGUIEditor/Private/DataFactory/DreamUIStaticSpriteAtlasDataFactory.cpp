// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DataFactory/DreamUIStaticSpriteAtlasDataFactory.h"
#include "Core/DreamUIStaticSpriteAtlasData.h"

#define LOCTEXT_NAMESPACE "DreamUIStaticSpriteAtalsDataFactory"


UDreamUIStaticSpriteAtlasDataFactory::UDreamUIStaticSpriteAtlasDataFactory()
{
	SupportedClass = UDreamUIStaticSpriteAtlasData::StaticClass();
	MenuSection = GraphicsSection();
	MenuLabel = NSLOCTEXT("DreamUIAssetMenu", "SpriteAtlas", "Sprite Atlas");
	MenuToolTip = NSLOCTEXT("DreamUIAssetMenu", "SpriteAtlasToolTip", "The atlas that sprites set to static packing are packed into in the editor, mipmaps included.");
	bCreateNew = true;
	bEditAfterNew = true;
}
UObject* UDreamUIStaticSpriteAtlasDataFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	auto NewAsset = NewObject<UDreamUIStaticSpriteAtlasData>(InParent, Class, Name, Flags | RF_Transactional);
	return NewAsset;
}

#undef LOCTEXT_NAMESPACE
