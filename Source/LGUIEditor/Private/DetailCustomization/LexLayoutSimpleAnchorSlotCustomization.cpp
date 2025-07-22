// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "LexLayoutSimpleAnchorSlotCustomization.h"
#include "DetailLayoutBuilder.h"
#include "Core/LexWidgetTypes.h"
#include "PropertyType/LexLayoutHorizontalAndVerticalAligmentCustomization.h"
#include "PropertyType/LexWidgetOffsetCustomization.h"

#define LOCTEXT_NAMESPACE "LexLayoutSimpleAnchor"
FLexLayoutSimpleAnchorSlotCustomization::FLexLayoutSimpleAnchorSlotCustomization()
{
}

FLexLayoutSimpleAnchorSlotCustomization::~FLexLayoutSimpleAnchorSlotCustomization()
{
}

TSharedRef<IDetailCustomization> FLexLayoutSimpleAnchorSlotCustomization::MakeInstance()
{
	return MakeShareable(new FLexLayoutSimpleAnchorSlotCustomization);
}
void FLexLayoutSimpleAnchorSlotCustomization::CustomizeDetails(const TSharedPtr<IDetailLayoutBuilder>& DetailBuilder)
{
	DetailBuilder->RegisterInstancedCustomPropertyTypeLayout("ELexLayoutHorizontalAlignment", FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FLexLayoutHorizontalAlignmentCustomization::MakeInstance));
	DetailBuilder->RegisterInstancedCustomPropertyTypeLayout("ELexLayoutVerticalAlignment", FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FLexLayoutVerticalAlignmentCustomization::MakeInstance));
	DetailBuilder->RegisterInstancedCustomPropertyTypeLayout(FLexWidgetOffset::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FLexWidgetOffsetCustomization::MakeInstance));
}

#undef LOCTEXT_NAMESPACE