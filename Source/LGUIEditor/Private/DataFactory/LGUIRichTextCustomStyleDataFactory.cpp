// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DataFactory/LGUIRichTextCustomStyleDataFactory.h"
#include "Core/LexUIRichTextCustomStyleData.h"

#define LOCTEXT_NAMESPACE "ULGUIRichTextCustomStyleDataFactory"


ULGUIRichTextCustomStyleDataFactory::ULGUIRichTextCustomStyleDataFactory()
{
	SupportedClass = ULexUIRichTextCustomStyleData::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}
UObject* ULGUIRichTextCustomStyleDataFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	auto NewAsset = NewObject<ULexUIRichTextCustomStyleData>(InParent, Class, Name, Flags | RF_Transactional);
	return NewAsset;
}

#undef LOCTEXT_NAMESPACE
