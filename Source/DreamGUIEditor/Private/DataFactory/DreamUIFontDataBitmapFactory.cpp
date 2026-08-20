// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DataFactory/DreamUIFontDataBitmapFactory.h"
#include "Core/DreamUIFontData_Bitmap.h"

#define LOCTEXT_NAMESPACE "UDreamUIFontDataBitmapFactory"


UDreamUIFontDataBitmapFactory::UDreamUIFontDataBitmapFactory()
{
	SupportedClass = UDreamUIFontData_Bitmap::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}
UObject* UDreamUIFontDataBitmapFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	UDreamUIFontData_Bitmap* NewAsset = NewObject<UDreamUIFontData_Bitmap>(InParent, Class, Name, Flags | RF_Transactional);
	return NewAsset;
}

#undef LOCTEXT_NAMESPACE
