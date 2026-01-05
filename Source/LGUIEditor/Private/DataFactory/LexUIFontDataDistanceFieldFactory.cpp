// Copyright 2019-present LexLiu. All Rights Reserved.

#include "LexUIFontDataDistanceFieldFactory.h"
#include "Core/LexUIFontData_DistanceField.h"

#define LOCTEXT_NAMESPACE "LexUIFontDataDistanceFieldFactory"

ULexUIFontDataDistanceFieldFactory::ULexUIFontDataDistanceFieldFactory()
{
	SupportedClass = ULexUIFontData_DistanceField::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}
UObject* ULexUIFontDataDistanceFieldFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	auto LexUIFont = NewObject<ULexUIFontData_DistanceField>(InParent, Class, Name, Flags | RF_Transactional);
	if (SourceFont.IsValid())
	{
		LexUIFont->SetFontType(ELexUIDynamicFontDataType::EngineFont);
		LexUIFont->SetEngineFont(SourceFont.Get());
		LexUIFont->ReloadFont();
	}
	return LexUIFont;
}

#undef LOCTEXT_NAMESPACE
