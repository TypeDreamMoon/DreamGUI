// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DetailCustomization/LexLayoutFlexBoxContainerCustomization.h"
#include "LGUIEditorUtils.h"
#include "LGUIEditorModule.h"
#include "DetailLayoutBuilder.h"
#include "Core/Components/LexLayoutFlexBoxContainer.h"
#include "PropertyType/LexLayoutFlexBoxDirectionCustomization.h"

#define LOCTEXT_NAMESPACE "LexLayoutFlexBoxCustomization"
FLexLayoutFlexBoxContainerCustomization::FLexLayoutFlexBoxContainerCustomization()
{
}

FLexLayoutFlexBoxContainerCustomization::~FLexLayoutFlexBoxContainerCustomization()
{
	
}

TSharedRef<IDetailCustomization> FLexLayoutFlexBoxContainerCustomization::MakeInstance()
{
	return MakeShareable(new FLexLayoutFlexBoxContainerCustomization);
}
void FLexLayoutFlexBoxContainerCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> targetObjects;
	DetailBuilder.GetObjectsBeingCustomized(targetObjects);
	TargetScriptArray.Empty();
	for (auto item : targetObjects)
	{
		if (auto validItem = Cast<ULexLayoutFlexBoxContainer>(item.Get()))
		{
			TargetScriptArray.Add(validItem);
		}
	}
	if (TargetScriptArray.Num() == 0)
	{
		UE_LOG(LGUIEditor, Log, TEXT("[%s].%d Get TargetScript is null"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return;
	}

	DetailBuilder.GetDetailsView()->RegisterInstancedCustomPropertyTypeLayout(TEXT("ELexLayoutFlexBoxDirectionType"), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FLexLayoutFlexBoxDirectionCustomization::MakeInstance));
}

#undef LOCTEXT_NAMESPACE