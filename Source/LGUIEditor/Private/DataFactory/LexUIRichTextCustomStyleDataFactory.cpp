// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DataFactory/LexUIRichTextCustomStyleDataFactory.h"
#include "Core/LexUIRichTextCustomStyleData.h"

#define LOCTEXT_NAMESPACE "ULexUIRichTextCustomStyleDataFactory"


ULexUIRichTextCustomStyleDataFactory::ULexUIRichTextCustomStyleDataFactory()
{
	SupportedClass = ULexUIRichTextCustomStyleData::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}
UObject* ULexUIRichTextCustomStyleDataFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	auto NewAsset = NewObject<ULexUIRichTextCustomStyleData>(InParent, Class, Name, Flags | RF_Transactional);
	return NewAsset;
}

#undef LOCTEXT_NAMESPACE
