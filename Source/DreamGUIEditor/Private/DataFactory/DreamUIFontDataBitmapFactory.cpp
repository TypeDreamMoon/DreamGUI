// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DataFactory/DreamUIFontDataBitmapFactory.h"
#include "Core/DreamUIFontData_Bitmap.h"

#define LOCTEXT_NAMESPACE "UDreamUIFontDataBitmapFactory"


UDreamUIFontDataBitmapFactory::UDreamUIFontDataBitmapFactory()
{
	SupportedClass = UDreamUIFontData_Bitmap::StaticClass();
	MenuSection = FontsSection();
	MenuLabel = NSLOCTEXT("DreamUIAssetMenu", "LegacyBitmapFont", "Legacy Bitmap Font");
	MenuToolTip = NSLOCTEXT("DreamUIAssetMenu", "LegacyBitmapFontToolTip", "A font rasterised from a face at each size it is drawn at. No longer developed: make a Distance Field Font instead.");
	bCreateNew = true;
	bEditAfterNew = true;
}
UObject* UDreamUIFontDataBitmapFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	UDreamUIFontData_Bitmap* NewAsset = NewObject<UDreamUIFontData_Bitmap>(InParent, Class, Name, Flags | RF_Transactional);
	return NewAsset;
}

#undef LOCTEXT_NAMESPACE
