// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "DataFactory/DreamUIAssetFactory.h"

#define LOCTEXT_NAMESPACE "DreamUIAssetFactory"

FText UDreamUIAssetFactory::FontsSection()
{
	return LOCTEXT("FontsSection", "Fonts");
}

FText UDreamUIAssetFactory::GraphicsSection()
{
	return LOCTEXT("GraphicsSection", "Graphics");
}

FText UDreamUIAssetFactory::RichTextSection()
{
	return LOCTEXT("RichTextSection", "Rich Text");
}

FText UDreamUIAssetFactory::GetDisplayName() const
{
	return MenuLabel.IsEmpty() ? Super::GetDisplayName() : MenuLabel;
}

FText UDreamUIAssetFactory::GetToolTip() const
{
	return MenuToolTip.IsEmpty() ? Super::GetToolTip() : MenuToolTip;
}

TArray<FAssetCategoryPath> UDreamUIAssetFactory::GetAssetMenuPathsForCategory(FName InCategory) const
{
	if (MenuSection.IsEmpty())
	{
		// No path of our own: the menu files the entry under its Basic section. A Basic of ours would be a second
		// section of that name, untranslated and sorted among the others instead of first.
		return Super::GetAssetMenuPathsForCategory(InCategory);
	}
	return { FAssetCategoryPath(FAssetCategoryPath(FText::FromName(InCategory)), FCategoryPath(MenuSection, ECategoryMenuType::Section)) };
}

#undef LOCTEXT_NAMESPACE
