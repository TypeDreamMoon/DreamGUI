// Copyright 2019-present LexLiu. All Rights Reserved.

#include "DreamUIFontDataDistanceFieldFactory.h"
#include "Core/DreamUIFontData_DistanceField.h"

#define LOCTEXT_NAMESPACE "DreamUIFontDataDistanceFieldFactory"

UDreamUIFontDataDistanceFieldFactory::UDreamUIFontDataDistanceFieldFactory()
{
	SupportedClass = UDreamUIFontData_DistanceField::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}
UObject* UDreamUIFontDataDistanceFieldFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	auto DreamUIFont = NewObject<UDreamUIFontData_DistanceField>(InParent, Class, Name, Flags | RF_Transactional);
	if (SourceFont.IsValid())
	{
		DreamUIFont->SetFontType(EDreamUIDynamicFontDataType::EngineFont);
		DreamUIFont->SetEngineFont(SourceFont.Get());
		DreamUIFont->ReloadFont();
	}
	return DreamUIFont;
}

#undef LOCTEXT_NAMESPACE
