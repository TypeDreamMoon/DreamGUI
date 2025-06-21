// Copyright 2019-present LexLiu. All Rights Reserved.

#include "LGUISDFFontDataFactory.h"
#include "Core/LexUIFontData_DistanceField.h"

#define LOCTEXT_NAMESPACE "LGUISDFFontDataFactory"

ULGUISDFFontDataFactory::ULGUISDFFontDataFactory()
{
	SupportedClass = ULexUIFontData_DistanceField::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}
UObject* ULGUISDFFontDataFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	return NewObject<ULexUIFontData_DistanceField>(InParent, Class, Name, Flags | RF_Transactional);
}

#undef LOCTEXT_NAMESPACE
