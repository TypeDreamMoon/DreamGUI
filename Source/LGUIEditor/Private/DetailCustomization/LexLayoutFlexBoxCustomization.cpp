// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DetailCustomization/LexLayoutFlexBoxCustomization.h"
#include "LGUIEditorUtils.h"
#include "LGUIEditorModule.h"
#include "DetailLayoutBuilder.h"
#include "Core/Components/LexLayoutCommonSlot.h"
#include "Core/Components/LexLayoutFlexBox.h"
#include "Core/Components/LexLayoutHorizontalAndVertical.h"
#include "PropertyType/LexLayoutHorizontalAndVerticalSizeControlCustomization.h"

#define LOCTEXT_NAMESPACE "LexLayoutFlexBoxCustomization"
FLexLayoutFlexBoxCustomization::FLexLayoutFlexBoxCustomization()
{
}

FLexLayoutFlexBoxCustomization::~FLexLayoutFlexBoxCustomization()
{
	
}

TSharedRef<IDetailCustomization> FLexLayoutFlexBoxCustomization::MakeInstance()
{
	return MakeShareable(new FLexLayoutFlexBoxCustomization);
}
void FLexLayoutFlexBoxCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> targetObjects;
	DetailBuilder.GetObjectsBeingCustomized(targetObjects);
	TargetScriptArray.Empty();
	for (auto item : targetObjects)
	{
		if (auto validItem = Cast<ULexLayoutFlexBox>(item.Get()))
		{
			TargetScriptArray.Add(validItem);
		}
	}
	if (TargetScriptArray.Num() == 0)
	{
		UE_LOG(LGUIEditor, Log, TEXT("[%s].%d Get TargetScript is null"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return;
	}
	
	DetailBuilder.RegisterInstancedCustomPropertyTypeLayout(FLexLayoutHorizontalAndVerticalSizeControl::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FLexLayoutHorizontalAndVerticalSizeControlCustomization::MakeInstance));
}

#undef LOCTEXT_NAMESPACE