// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DetailCustomization/UIBaseRenderableCustomization.h"
#include "Core/Components/LexVisual.h"
#include "LGUIEditorModule.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"

#define LOCTEXT_NAMESPACE "UIBaseRenderableCustomization"
FLexVisualCustomization::FLexVisualCustomization()
{
}

FLexVisualCustomization::~FLexVisualCustomization()
{
	
}

TSharedRef<IDetailCustomization> FLexVisualCustomization::MakeInstance()
{
	return MakeShareable(new FLexVisualCustomization);
}
void FLexVisualCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{

}
#undef LOCTEXT_NAMESPACE