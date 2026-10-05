// Copyright 2019-present LexLiu. All Rights Reserved.

#include "DreamUIFontDataDistanceFieldFactory.h"
#include "Core/DreamUIFontData_DistanceField.h"

#define LOCTEXT_NAMESPACE "DreamUIFontDataDistanceFieldFactory"

UDreamUIFontDataDistanceFieldFactory::UDreamUIFontDataDistanceFieldFactory()
{
	SupportedClass = UDreamUIFontData_DistanceField::StaticClass();
	MenuSection = FontsSection();
	MenuLabel = NSLOCTEXT("DreamUIAssetMenu", "DistanceFieldFont", "Distance Field Font");
	MenuToolTip = NSLOCTEXT("DreamUIAssetMenu", "DistanceFieldFontToolTip", "A font drawn from distance fields of a font face: sharp at any size, with outlines and shadows. The font to make.");
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
