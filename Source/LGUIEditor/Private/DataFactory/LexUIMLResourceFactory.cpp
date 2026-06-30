// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DataFactory/LexUIMLResourceFactory.h"
#include "XMLSupport/LexUIML.h"

#define LOCTEXT_NAMESPACE "LexUIMLResourceFactory"


ULexUIMLResourceFactory::ULexUIMLResourceFactory()
{
	SupportedClass = ULexUIMLResource::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}

UObject* ULexUIMLResourceFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	return NewObject<ULexUIMLResource>(InParent, Class, Name, Flags | RF_Transactional);
}

#undef LOCTEXT_NAMESPACE
