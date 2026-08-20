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

}
#undef LOCTEXT_NAMESPACE