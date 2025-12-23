// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DetailCustomization/LexLayoutSelfFlexBoxCustomization.h"
#include "LexUIEditorUtils.h"
#include "LGUIEditorModule.h"
#include "DetailLayoutBuilder.h"
#include "Core/Components/LexLayoutSelfFlexBox.h"
#include "PropertyType/LexLayoutMinMaxSizeCustomization.h"
#include "PropertyType/LexLayoutSizeCustomization.h"

#define LOCTEXT_NAMESPACE "LexLayoutHorizontalAndVerticalSlotCustomization"
FLexLayoutSelfFlexBoxCustomization::FLexLayoutSelfFlexBoxCustomization()
{
}

FLexLayoutSelfFlexBoxCustomization::~FLexLayoutSelfFlexBoxCustomization()
{
	
}

TSharedRef<IDetailCustomization> FLexLayoutSelfFlexBoxCustomization::MakeInstance()
{
	return MakeShareable(new FLexLayoutSelfFlexBoxCustomization);
}
void FLexLayoutSelfFlexBoxCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> targetObjects;
	DetailBuilder.GetObjectsBeingCustomized(targetObjects);
	TargetScriptArray.Empty();
	for (auto item : targetObjects)
	{
		if (auto validItem = Cast<ULexLayoutSelfFlexBox>(item.Get()))
		{
			TargetScriptArray.Add(validItem);
		}
	}
	if (TargetScriptArray.Num() == 0)
	{
		UE_LOG(LGUIEditor, Log, TEXT("[%s].%d Get TargetScript is null"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return;
	}

	DetailBuilder.GetDetailsView()->RegisterInstancedCustomPropertyTypeLayout(FLexLayoutSize::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FLexLayoutSizeCustomization::MakeInstance));
	DetailBuilder.GetDetailsView()->RegisterInstancedCustomPropertyTypeLayout(FLexLayoutMinMaxSize::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FLexLayoutMinMaxSizeCustomization::MakeInstance));
}

#undef LOCTEXT_NAMESPACE