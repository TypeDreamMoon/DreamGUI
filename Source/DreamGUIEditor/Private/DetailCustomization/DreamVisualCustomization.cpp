// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DetailCustomization/DreamVisualCustomization.h"
#include "Core/Components/DreamVisual.h"
#include "DreamGUIEditorModule.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"

#define LOCTEXT_NAMESPACE "UIBaseRenderableCustomization"
FDreamVisualCustomization::FDreamVisualCustomization()
{
}

FDreamVisualCustomization::~FDreamVisualCustomization()
{
	
}

TSharedRef<IDetailCustomization> FDreamVisualCustomization::MakeInstance()
{
	return MakeShareable(new FDreamVisualCustomization);
}
void FDreamVisualCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	// Inside the widget's Visual category a visual's own categories are flattened into one list (the widget adds it
	// with CreateCategoryNodes(false)), so the order of the categories IS the order of the rows: what the visual shows
	// first -- an Image's brush lives in "Image" -- then its settings, and how the pointer hits it last. This runs
	// before every subclass's customization (it is registered first), so they still order the rows inside "DreamGUI".
	DetailBuilder.EditCategory("Image").SetSortOrder(-20);
	DetailBuilder.EditCategory("DreamGUI").SetSortOrder(-10);
	DetailBuilder.EditCategory("DreamGUI-Raycast", LOCTEXT("RaycastCategory", "Raycast")).SetSortOrder(100);
}
#undef LOCTEXT_NAMESPACE