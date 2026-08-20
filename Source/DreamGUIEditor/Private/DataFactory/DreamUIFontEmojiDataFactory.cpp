// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DreamUIFontEmojiDataFactory.h"
#include "Core/DreamUIFontEmojiData.h"

#define LOCTEXT_NAMESPACE "DreamUIFontEmojiDataFactory"


UDreamUIFontEmojiDataFactory::UDreamUIFontEmojiDataFactory()
{
	SupportedClass = UDreamUIFontEmojiData::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}
UObject* UDreamUIFontEmojiDataFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	auto NewAsset = NewObject<UDreamUIFontEmojiData>(InParent, Class, Name, Flags | RF_Transactional);
	return NewAsset;
}

#undef LOCTEXT_NAMESPACE
