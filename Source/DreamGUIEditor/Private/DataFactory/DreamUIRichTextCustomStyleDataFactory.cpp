// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DataFactory/DreamUIRichTextCustomStyleDataFactory.h"
#include "Core/DreamUIRichTextCustomStyleData.h"

#define LOCTEXT_NAMESPACE "UDreamUIRichTextCustomStyleDataFactory"


UDreamUIRichTextCustomStyleDataFactory::UDreamUIRichTextCustomStyleDataFactory()
{
	SupportedClass = UDreamUIRichTextCustomStyleData::StaticClass();
	MenuSection = RichTextSection();
	MenuLabel = NSLOCTEXT("DreamUIAssetMenu", "RichTextStyles", "Rich Text Styles");
	MenuToolTip = NSLOCTEXT("DreamUIAssetMenu", "RichTextStylesToolTip", "Tags for rich text: each names a style -- colour, size, font, effects -- for the text the tag wraps.");
	bCreateNew = true;
	bEditAfterNew = true;
}
UObject* UDreamUIRichTextCustomStyleDataFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	auto NewAsset = NewObject<UDreamUIRichTextCustomStyleData>(InParent, Class, Name, Flags | RF_Transactional);
	return NewAsset;
}

#undef LOCTEXT_NAMESPACE
