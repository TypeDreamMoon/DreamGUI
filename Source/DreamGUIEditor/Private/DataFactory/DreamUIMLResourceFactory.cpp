// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DataFactory/DreamUIMLResourceFactory.h"
#include "XMLSupport/DreamUIML.h"

#define LOCTEXT_NAMESPACE "DreamUIMLResourceFactory"


UDreamUIMLResourceFactory::UDreamUIMLResourceFactory()
{
	SupportedClass = UDreamUIMLResource::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}

UObject* UDreamUIMLResourceFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	return NewObject<UDreamUIMLResource>(InParent, Class, Name, Flags | RF_Transactional);
}

#undef LOCTEXT_NAMESPACE
