// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DetailCustomization/LexLayoutContainerCustomization.h"
#include "LGUIEditorUtils.h"
#include "LGUIEditorModule.h"
#include "DetailLayoutBuilder.h"
#include "Core/Components/LexLayout.h"


#define LOCTEXT_NAMESPACE "LexLayoutContainerCustomization"
FLexLayoutContainerCustomization::FLexLayoutContainerCustomization()
{
}

FLexLayoutContainerCustomization::~FLexLayoutContainerCustomization()
{
	
}

TSharedRef<IDetailCustomization> FLexLayoutContainerCustomization::MakeInstance()
{
	return MakeShareable(new FLexLayoutContainerCustomization);
}
void FLexLayoutContainerCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> TargetObjects;
	DetailBuilder.GetObjectsBeingCustomized(TargetObjects);
	TargetScriptArray.Empty();
	for (auto item : TargetObjects)
	{
		if (auto validItem = Cast<ULexLayoutContainer>(item.Get()))
		{
			TargetScriptArray.Add(validItem);
		}
	}
	if (TargetScriptArray.Num() == 0)
	{
		UE_LOG(LGUIEditor, Log, TEXT("[%s].%d Get TargetScript is null"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return;
	}
}

#undef LOCTEXT_NAMESPACE

