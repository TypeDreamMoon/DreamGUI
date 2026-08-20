// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DetailCustomization/DreamVisualPostProcessCustomization.h"
#include "DreamUIEditorUtils.h"
#include "Core/Components/DreamVisualPostProcess.h"
#include "DreamGUIEditorModule.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "IDetailGroup.h"

#define LOCTEXT_NAMESPACE "UIPostProcessRenderableCustomization"
FDreamVisualPostProcessCustomization::FDreamVisualPostProcessCustomization()
{
}

FDreamVisualPostProcessCustomization::~FDreamVisualPostProcessCustomization()
{
	
}

TSharedRef<IDetailCustomization> FDreamVisualPostProcessCustomization::MakeInstance()
{
	return MakeShareable(new FDreamVisualPostProcessCustomization);
}
void FDreamVisualPostProcessCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> targetObjects;
	DetailBuilder.GetObjectsBeingCustomized(targetObjects);
	TargetScriptArray.Empty();
	for (auto item : targetObjects)
	{
		if (auto validItem = Cast<UDreamVisualPostProcess>(item.Get()))
		{
			TargetScriptArray.Add(validItem);
		}
	}
	if (TargetScriptArray.Num() == 0)
	{
		UE_LOG(DreamGUIEditor, Log, TEXT("[%s].%d Get TargetScript is null"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return;
	}

	IDetailCategoryBuilder& DreamGUICategory = DetailBuilder.EditCategory("DreamGUI");
	TArray<FName> NeedToHidePropertyNames;
	auto MaskTextureHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamVisualPostProcess, MaskTexture));
	MaskTextureHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([&]() {
		DetailBuilder.ForceRefreshDetails();
		}));
	IDetailGroup& MaskTextureGroup = DreamGUICategory.AddGroup(FName("MaskTexture"), LOCTEXT("MaskTexture", "MaskTexture"));
	MaskTextureGroup.HeaderProperty(MaskTextureHandle);
	MaskTextureGroup.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamVisualPostProcess, MaskTextureUVRect)));
}

#undef LOCTEXT_NAMESPACE