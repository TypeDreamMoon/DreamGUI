// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DetailCustomization/DreamLayoutContainerCustomization.h"
#include "DreamUIEditorUtils.h"
#include "DreamGUIEditorModule.h"
#include "DetailLayoutBuilder.h"
#include "Core/Components/DreamLayout.h"


#define LOCTEXT_NAMESPACE "DreamLayoutContainerCustomization"
FDreamLayoutContainerCustomization::FDreamLayoutContainerCustomization()
{
}

FDreamLayoutContainerCustomization::~FDreamLayoutContainerCustomization()
{
	
}

TSharedRef<IDetailCustomization> FDreamLayoutContainerCustomization::MakeInstance()
{
	return MakeShareable(new FDreamLayoutContainerCustomization);
}
void FDreamLayoutContainerCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> TargetObjects;
	DetailBuilder.GetObjectsBeingCustomized(TargetObjects);
	TargetScriptArray.Empty();
	for (auto item : TargetObjects)
	{
		if (auto validItem = Cast<UDreamLayoutContainer>(item.Get()))
		{
			TargetScriptArray.Add(validItem);
		}
	}
	if (TargetScriptArray.Num() == 0)
	{
		UE_LOG(DreamGUIEditor, Log, TEXT("[%s].%d Get TargetScript is null"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return;
	}
}

#undef LOCTEXT_NAMESPACE

