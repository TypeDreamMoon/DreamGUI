// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DataFactory/DreamUIRichTextImageDataFactory.h"
#include "Core/DreamUIRichTextImageData.h"

#define LOCTEXT_NAMESPACE "UDreamUIRichTextImageDataFactory"


UDreamUIRichTextImageDataFactory::UDreamUIRichTextImageDataFactory()
{
	SupportedClass = UDreamUIRichTextImageData::StaticClass();
	MenuSection = RichTextSection();
	MenuLabel = NSLOCTEXT("DreamUIAssetMenu", "RichTextImages", "Rich Text Images");
	MenuToolTip = NSLOCTEXT("DreamUIAssetMenu", "RichTextImagesToolTip", "Images for rich text: each name places a sprite, or an animated run of sprites, inline in a text.");
	bCreateNew = true;
	bEditAfterNew = true;
}
UObject* UDreamUIRichTextImageDataFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	auto NewAsset = NewObject<UDreamUIRichTextImageData>(InParent, Class, Name, Flags | RF_Transactional);
	return NewAsset;
}

#undef LOCTEXT_NAMESPACE
