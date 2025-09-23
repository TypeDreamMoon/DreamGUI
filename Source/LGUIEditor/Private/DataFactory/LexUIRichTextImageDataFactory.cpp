// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DataFactory/LexUIRichTextImageDataFactory.h"
#include "Core/LexUIRichTextImageData.h"

#define LOCTEXT_NAMESPACE "ULexUIRichTextImageDataFactory"


ULexUIRichTextImageDataFactory::ULexUIRichTextImageDataFactory()
{
	SupportedClass = ULexUIRichTextImageData::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}
UObject* ULexUIRichTextImageDataFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	auto NewAsset = NewObject<ULexUIRichTextImageData>(InParent, Class, Name, Flags | RF_Transactional);
	return NewAsset;
}

#undef LOCTEXT_NAMESPACE
