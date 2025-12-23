// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DetailCustomization/LexVisualPostProcessCustomization.h"
#include "LexUIEditorUtils.h"
#include "Core/Components/LexVisualPostProcess.h"
#include "LGUIEditorModule.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "IDetailGroup.h"

#define LOCTEXT_NAMESPACE "UIPostProcessRenderableCustomization"
FLexVisualPostProcessCustomization::FLexVisualPostProcessCustomization()
{
}

FLexVisualPostProcessCustomization::~FLexVisualPostProcessCustomization()
{
	
}

TSharedRef<IDetailCustomization> FLexVisualPostProcessCustomization::MakeInstance()
{
	return MakeShareable(new FLexVisualPostProcessCustomization);
}
void FLexVisualPostProcessCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> targetObjects;
	DetailBuilder.GetObjectsBeingCustomized(targetObjects);
	TargetScriptArray.Empty();
	for (auto item : targetObjects)
	{
		if (auto validItem = Cast<ULexVisualPostProcess>(item.Get()))
		{
			TargetScriptArray.Add(validItem);
		}
	}
	if (TargetScriptArray.Num() == 0)
	{
		UE_LOG(LGUIEditor, Log, TEXT("[%s].%d Get TargetScript is null"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return;
	}

	IDetailCategoryBuilder& LGUICategory = DetailBuilder.EditCategory("LGUI");
	TArray<FName> NeedToHidePropertyNames;
	auto MaskTextureHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexVisualPostProcess, MaskTexture));
	MaskTextureHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([&]() {
		DetailBuilder.ForceRefreshDetails();
		}));
	IDetailGroup& MaskTextureGroup = LGUICategory.AddGroup(FName("MaskTexture"), LOCTEXT("MaskTexture", "MaskTexture"));
	MaskTextureGroup.HeaderProperty(MaskTextureHandle);
	MaskTextureGroup.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexVisualPostProcess, MaskTextureUVRect)));
}

#undef LOCTEXT_NAMESPACE