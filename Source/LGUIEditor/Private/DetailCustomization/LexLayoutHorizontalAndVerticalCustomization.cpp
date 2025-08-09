// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DetailCustomization/LexLayoutHorizontalAndVerticalCustomization.h"
#include "LGUIEditorUtils.h"
#include "LGUIEditorModule.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "IDetailGroup.h"
#include "Core/Components/LexLayoutHorizontalAndVertical.h"
#include "PropertyType/LexLayoutHorizontalAndVerticalSizeControlCustomization.h"

#define LOCTEXT_NAMESPACE "LexLayoutHorizontalAndVerticalCustomization"
FLexLayoutHorizontalAndVerticalCustomization::FLexLayoutHorizontalAndVerticalCustomization()
{
}

FLexLayoutHorizontalAndVerticalCustomization::~FLexLayoutHorizontalAndVerticalCustomization()
{
	
}

TSharedRef<IDetailCustomization> FLexLayoutHorizontalAndVerticalCustomization::MakeInstance()
{
	return MakeShareable(new FLexLayoutHorizontalAndVerticalCustomization);
}
void FLexLayoutHorizontalAndVerticalCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> targetObjects;
	DetailBuilder.GetObjectsBeingCustomized(targetObjects);
	TargetScriptArray.Empty();
	for (auto item : targetObjects)
	{
		if (auto validItem = Cast<ULexLayoutHorizontalAndVertical>(item.Get()))
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