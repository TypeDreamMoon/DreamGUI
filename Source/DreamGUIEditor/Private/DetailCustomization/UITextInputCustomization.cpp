// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DetailCustomization/UITextInputCustomization.h"
#include "DreamDetailsMultiSelect.h"
#include "Interaction/UITextInput.h"

#include "DreamGUIEditorModule.h"
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
	TargetScriptPtr = Cast<UUITextInput>(targetObjects[0].Get());
	if (TargetScriptPtr == nullptr)
	{
		UE_LOG(DreamGUIEditor, Log, TEXT("[%s].%d Get TargetScript is null"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return;
	}

	IDetailCategoryBuilder& category = DetailBuilder.EditCategory("DreamGUI-Input");

	auto InputTypeHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUITextInput, InputType));
	InputTypeHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([&DetailBuilder] {DetailBuilder.ForceRefreshDetails(); }));
	// Custom as the fallback because it is the value that hides NOTHING: a selection that disagrees
	// still has objects using CustomValidation, and hiding it would hide a live property from them.
	const auto InputType = (EUITextInputType)DreamDetailsMultiSelect::ValueOr<uint8>(
		InputTypeHandle, (uint8)EUITextInputType::Custom);
	if (InputType != EUITextInputType::Custom)
	{
		DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UUITextInput, CustomValidation));
	}
	auto DisplayTypeHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUITextInput, DisplayType));
	DisplayTypeHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([&DetailBuilder] {DetailBuilder.ForceRefreshDetails(); }));
	// Password for the same reason: it is the branch that hides nothing.
	const auto DisplayType = (EUITextInputDisplayType)DreamDetailsMultiSelect::ValueOr<uint8>(
		DisplayTypeHandle, (uint8)EUITextInputDisplayType::Password);
	switch (DisplayType)
	{
	case EUITextInputDisplayType::Standard:
		DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UUITextInput, PasswordChar));
		break;
	case EUITextInputDisplayType::Password:
		break;
	}

	auto AllowMultilineHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUITextInput, bAllowMultiLine));
	AllowMultilineHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([&DetailBuilder] {DetailBuilder.ForceRefreshDetails(); }));
	if (DreamDetailsMultiSelect::AllEqual(AllowMultilineHandle, false))
	{
		DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UUITextInput, MultiLineSubmitFunctionKeys));
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