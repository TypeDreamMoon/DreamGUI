// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "LexUIFontEmojiDataFactory.h"
#include "Core/LexUIFontEmojiData.h"

#define LOCTEXT_NAMESPACE "LexUIFontEmojiDataFactory"


ULexUIFontEmojiDataFactory::ULexUIFontEmojiDataFactory()
{
	SupportedClass = ULexUIFontEmojiData::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}
UObject* ULexUIFontEmojiDataFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	auto NewAsset = NewObject<ULexUIFontEmojiData>(InParent, Class, Name, Flags | RF_Transactional);
	return NewAsset;
}

#undef LOCTEXT_NAMESPACE
