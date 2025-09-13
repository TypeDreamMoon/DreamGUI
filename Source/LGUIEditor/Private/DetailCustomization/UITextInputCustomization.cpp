// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DetailCustomization/UITextInputCustomization.h"
#include "Interaction/UITextInputComponent.h"

#include "LGUIEditorModule.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"

#define LOCTEXT_NAMESPACE "UITextComponentDetails"
FUITextInputCustomization::FUITextInputCustomization()
{
}

FUITextInputCustomization::~FUITextInputCustomization()
{
}

TSharedRef<IDetailCustomization> FUITextInputCustomization::MakeInstance()
{
	return MakeShareable(new FUITextInputCustomization);
}
void FUITextInputCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> targetObjects;
	DetailBuilder.GetObjectsBeingCustomized(targetObjects);
	TargetScriptPtr = Cast<UUITextInputComponent>(targetObjects[0].Get());
	if (TargetScriptPtr == nullptr)
	{
		UE_LOG(LGUIEditor, Log, TEXT("[%s].%d Get TargetScript is null"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return;
	}

	IDetailCategoryBuilder& category = DetailBuilder.EditCategory("LGUI-Input");

	auto InputTypeHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUITextInputComponent, InputType));
	InputTypeHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([&DetailBuilder] {DetailBuilder.ForceRefreshDetails(); }));
	ELexUITextInputType InputType;
	InputTypeHandle->GetValue(*(uint8*)&InputType);
	if (InputType != ELexUITextInputType::Custom)
	{
		DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UUITextInputComponent, CustomValidation));
	}
	auto DisplayTypeHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUITextInputComponent, DisplayType));
	DisplayTypeHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([&DetailBuilder] {DetailBuilder.ForceRefreshDetails(); }));
	ELexUITextInputDisplayType DisplayType;
	DisplayTypeHandle->GetValue(*(uint8*)&DisplayType);
	switch (DisplayType)
	{
	case ELexUITextInputDisplayType::Standard:
		DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UUITextInputComponent, PasswordChar));
		break;
	case ELexUITextInputDisplayType::Password:
		break;
	}

	auto OverflowTypeHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUITextInputComponent, OverflowType));
	OverflowTypeHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([&DetailBuilder] {DetailBuilder.ForceRefreshDetails(); }));
	ELexUITextInputOverflowType OverflowType;
	OverflowTypeHandle->GetValue(*(uint8*)&OverflowType);
	auto AllowMultilineHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUITextInputComponent, bAllowMultiLine));
	AllowMultilineHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([&DetailBuilder] {DetailBuilder.ForceRefreshDetails(); }));
	bool bAllowMultiLine;
	AllowMultilineHandle->GetValue(bAllowMultiLine);
	switch (OverflowType)
	{
	case ELexUITextInputOverflowType::ClampContent:
		DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UUITextInputComponent, MaxLineCount));
		DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UUITextInputComponent, MaxLineWidth));
		break;
	case ELexUITextInputOverflowType::OverflowToMax:
		if (bAllowMultiLine)
		{
			DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UUITextInputComponent, MaxLineWidth));
		}
		else
		{
			DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UUITextInputComponent, MaxLineCount));
		}
		break;
	}
	if (!bAllowMultiLine)
	{
		DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UUITextInputComponent, MultiLineSubmitFunctionKeys));
	}
}
void FUITextInputCustomization::ForceRefresh(IDetailLayoutBuilder* DetailBuilder)
{
	if (TargetScriptPtr.IsValid())
	{
		DetailBuilder->ForceRefreshDetails();
	}
}
#undef LOCTEXT_NAMESPACE