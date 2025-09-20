// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DetailCustomization/LexTextCustomization.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Core/Components/LexText.h"

#include "LGUIEditorModule.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "IDetailGroup.h"
#include "MaterialDomain.h"
#include "PropertyType/LexTextAlignmentCustomization.h"

#define LOCTEXT_NAMESPACE "UITextCustomization"
FLexTextCustomization::FLexTextCustomization()
{
}

FLexTextCustomization::~FLexTextCustomization()
{
}

TSharedRef<IDetailCustomization> FLexTextCustomization::MakeInstance()
{
	return MakeShareable(new FLexTextCustomization);
}
void FLexTextCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> targetObjects;
	DetailBuilder.GetObjectsBeingCustomized(targetObjects);
	TargetScriptPtr = Cast<ULexText>(targetObjects[0].Get());
	if (TargetScriptPtr == nullptr)
	{
		UE_LOG(LGUIEditor, Log, TEXT("[%s].%d Get TargetScript is null"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return;
	}
	
	IDetailCategoryBuilder& LGUICategory = DetailBuilder.EditCategory("LGUI");
	LGUICategory.AddProperty(GET_MEMBER_NAME_CHECKED(ULexText, Font));
	LGUICategory.AddProperty(GET_MEMBER_NAME_CHECKED(ULexText, Text));

	LGUICategory.AddProperty(GET_MEMBER_NAME_CHECKED(ULexText, FontSize));
	LGUICategory.AddProperty(GET_MEMBER_NAME_CHECKED(ULexText, FontSpace));

	//text alignment
	{
		DetailBuilder.GetDetailsView()->RegisterInstancedCustomPropertyTypeLayout(TEXT("ELexUITextParagraphHorizontalAlign"), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FLexTextAlignmentCustomization::MakeInstance, true));
		DetailBuilder.GetDetailsView()->RegisterInstancedCustomPropertyTypeLayout(TEXT("ELexUITextParagraphVerticalAlign"), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FLexTextAlignmentCustomization::MakeInstance, false));
		LGUICategory.AddProperty(GET_MEMBER_NAME_CHECKED(ULexText, HAlign));
		LGUICategory.AddProperty(GET_MEMBER_NAME_CHECKED(ULexText, VAlign));
	}

	auto OverflowTypeHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexText, OverflowType));
	OverflowTypeHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FLexTextCustomization::ForceRefresh, &DetailBuilder));
	LGUICategory.AddProperty(OverflowTypeHandle);
	
	TArray<FName> needToHidePropertyName;
	auto RichTextHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexText, bRichText));
	RichTextHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FLexTextCustomization::ForceRefresh, &DetailBuilder));
	bool richText = false;
	RichTextHandle->GetValue(richText);
	if (richText)
	{
		IDetailGroup& RichTextGroup = LGUICategory.AddGroup(FName("RichText"), RichTextHandle->GetPropertyDisplayName());
		RichTextGroup.HeaderProperty(RichTextHandle);
		RichTextGroup.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexText, RichTextTagFilterFlags)));
		RichTextGroup.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexText, RichTextCustomStyleData)));
		RichTextGroup.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexText, RichTextImageData)));
		RichTextGroup.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexText, bListRichTextImageObjectInOutliner)));
		RichTextGroup.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexText, CreatedRichTextImageObjectArray)));
	}
	else
	{
		LGUICategory.AddProperty(RichTextHandle);
		needToHidePropertyName.Add(GET_MEMBER_NAME_CHECKED(ULexText, RichTextCustomStyleData));
		needToHidePropertyName.Add(GET_MEMBER_NAME_CHECKED(ULexText, RichTextImageData));
		needToHidePropertyName.Add(GET_MEMBER_NAME_CHECKED(ULexText, bListRichTextImageObjectInOutliner));
		needToHidePropertyName.Add(GET_MEMBER_NAME_CHECKED(ULexText, CreatedRichTextImageObjectArray));
		needToHidePropertyName.Add(GET_MEMBER_NAME_CHECKED(ULexText, RichTextTagFilterFlags));
	}

	for (auto item : needToHidePropertyName)
	{
		DetailBuilder.HideProperty(item);
	}

	auto fontHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexText, Font));
	fontHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([=, this] {
		TargetScriptPtr->OnPostChangeFontProperty();
	}));
	fontHandle->SetOnPropertyValuePreChange(FSimpleDelegate::CreateLambda([=, this]{
		TargetScriptPtr->OnPreChangeFontProperty();
	}));

	auto richTextImageDataHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexText, RichTextImageData));
	richTextImageDataHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([=, this] {
		TargetScriptPtr->OnPostChangeRichTextImageDataProperty();
		}));
	richTextImageDataHandle->SetOnPropertyValuePreChange(FSimpleDelegate::CreateLambda([=, this] {
		TargetScriptPtr->OnPreChangeRichTextImageDataProperty();
		}));

	auto richTextCustomStyleDataHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexText, RichTextCustomStyleData));
	richTextCustomStyleDataHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([=, this] {
		TargetScriptPtr->OnPostChangeRichTextCustomStyleDataProperty();
		}));
	richTextCustomStyleDataHandle->SetOnPropertyValuePreChange(FSimpleDelegate::CreateLambda([=, this] {
		TargetScriptPtr->OnPreChangeRichTextCustomStyleDataProperty();
		}));

	auto OverrideMaterial_PH = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexText, OverrideMaterial));
	OverrideMaterial_PH->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([=, &DetailBuilder] {
		DetailBuilder.ForceRefreshDetails();
		}));
	LGUICategory.AddProperty(OverrideMaterial_PH);
	LGUICategory.AddCustomRow(LOCTEXT("MaterialDomainErrorTipRow", "MaterialDomainErrorTip"))
	.Visibility(TAttribute<EVisibility>::CreateSPLambda(this, [=]()
	{
		UMaterialInterface* OverrideMaterial = nullptr;
		OverrideMaterial_PH->GetValue((UObject*&)OverrideMaterial);
		if (!OverrideMaterial)
		{
			return EVisibility::Collapsed;
		}
		auto Mat = OverrideMaterial->GetMaterial();
		if (!Mat)
		{
			return EVisibility::Collapsed;
		}
		if (Mat->MaterialDomain == EMaterialDomain::MD_Surface)
		{
			return EVisibility::Collapsed;
		}
		return EVisibility::Visible;
	}))
	.WholeRowContent()
	.MinDesiredWidth(500)
	.VAlign(VAlign_Center)
	[
		SNew(STextBlock)
		.Font(IDetailLayoutBuilder::GetDetailFont())
		.Text(LOCTEXT("MaterialDomainErrorTip", "OverrideMaterial should use Surface domain!"))
		.ColorAndOpacity(FLinearColor(FColor::Yellow))
		.AutoWrapText(true)
	]
	;
}
void FLexTextCustomization::ForceRefresh(IDetailLayoutBuilder* DetailBuilder)
{
	if (auto Script = TargetScriptPtr.Get())
	{
		DetailBuilder->ForceRefreshDetails();
	}
}
#undef LOCTEXT_NAMESPACE