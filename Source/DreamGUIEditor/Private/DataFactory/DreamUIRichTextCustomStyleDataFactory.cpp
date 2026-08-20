// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DataFactory/DreamUIRichTextCustomStyleDataFactory.h"
#include "Core/DreamUIRichTextCustomStyleData.h"

#define LOCTEXT_NAMESPACE "UDreamUIRichTextCustomStyleDataFactory"


UDreamUIRichTextCustomStyleDataFactory::UDreamUIRichTextCustomStyleDataFactory()
{
	SupportedClass = UDreamUIRichTextCustomStyleData::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}
UObject* UDreamUIRichTextCustomStyleDataFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	auto NewAsset = NewObject<UDreamUIRichTextCustomStyleData>(InParent, Class, Name, Flags | RF_Transactional);
	return NewAsset;
}

#undef LOCTEXT_NAMESPACE
