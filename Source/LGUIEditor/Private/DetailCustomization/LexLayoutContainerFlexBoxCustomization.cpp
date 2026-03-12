// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DetailCustomization/LexLayoutContainerFlexBoxCustomization.h"
#include "LexUIEditorUtils.h"
#include "LGUIEditorModule.h"
#include "DetailLayoutBuilder.h"
#include "Core/Components/LexLayoutContainerFlexBox.h"
#include "PropertyType/LexLayoutFlexBoxDirectionCustomization.h"

#define LOCTEXT_NAMESPACE "LexLayoutFlexBoxCustomization"
FLexLayoutContainerFlexBoxCustomization::FLexLayoutContainerFlexBoxCustomization()
{
}

FLexLayoutContainerFlexBoxCustomization::~FLexLayoutContainerFlexBoxCustomization()
{
	
}

TSharedRef<IDetailCustomization> FLexLayoutContainerFlexBoxCustomization::MakeInstance()
{
	return MakeShareable(new FLexLayoutContainerFlexBoxCustomization);
}
void FLexLayoutContainerFlexBoxCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> targetObjects;
	DetailBuilder.GetObjectsBeingCustomized(targetObjects);
	TargetScriptArray.Empty();
	for (auto item : targetObjects)
	{
		if (auto validItem = Cast<ULexLayoutContainerFlexBox>(item.Get()))
		{
			TargetScriptArray.Add(validItem);
		}
	}
	if (TargetScriptArray.Num() == 0)
	{
		UE_LOG(LGUIEditor, Log, TEXT("[%s].%d Get TargetScript is null"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return;
	}

	DetailBuilder.GetDetailsViewSharedPtr()->RegisterInstancedCustomPropertyTypeLayout(TEXT("ELexLayoutFlexBoxDirectionType"), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FLexLayoutFlexBoxDirectionCustomization::MakeInstance));
}

#undef LOCTEXT_NAMESPACE