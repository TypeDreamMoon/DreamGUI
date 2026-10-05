// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DreamUIFontEmojiDataFactory.h"
#include "Core/DreamUIFontEmojiData.h"

#define LOCTEXT_NAMESPACE "DreamUIFontEmojiDataFactory"


UDreamUIFontEmojiDataFactory::UDreamUIFontEmojiDataFactory()
{
	SupportedClass = UDreamUIFontEmojiData::StaticClass();
	MenuSection = FontsSection();
	MenuLabel = NSLOCTEXT("DreamUIAssetMenu", "EmojiFont", "Emoji Font");
	MenuToolTip = NSLOCTEXT("DreamUIAssetMenu", "EmojiFontToolTip", "Emoji for a font: each one a sprite, or a run of sprites to animate, drawn in place of its characters.");
	bCreateNew = true;
	bEditAfterNew = true;
}
UObject* UDreamUIFontEmojiDataFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	auto NewAsset = NewObject<UDreamUIFontEmojiData>(InParent, Class, Name, Flags | RF_Transactional);
	return NewAsset;
}

#undef LOCTEXT_NAMESPACE
