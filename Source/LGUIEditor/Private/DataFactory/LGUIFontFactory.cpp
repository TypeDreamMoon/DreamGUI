// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DataFactory/LGUIFontFactory.h"
#include "Core/LexUIFontData_Bitmap.h"

#define LOCTEXT_NAMESPACE "ULGUIFontFactory"


ULGUIFontFactory::ULGUIFontFactory()
{
	SupportedClass = ULexUIFontData_Bitmap::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}
UObject* ULGUIFontFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	ULexUIFontData_Bitmap* NewAsset = NewObject<ULexUIFontData_Bitmap>(InParent, Class, Name, Flags | RF_Transactional);
	return NewAsset;
}

#undef LOCTEXT_NAMESPACE
