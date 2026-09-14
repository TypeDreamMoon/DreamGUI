// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DetailCustomization/UITextInputCustomization.h"
#include "DreamDetailsMultiSelect.h"
#include "Interaction/UITextInput.h"

#include "DreamGUIEditorModule.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "IPropertyUtilities.h"

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
	// An empty list is a real state -- the panel rebuilds while a selection is being cleared -- and
	// indexing [0] there reads off the end of an empty array. The null branch below already handles
	// "no target", so this only has to reach it.
	TargetScriptPtr = targetObjects.Num() > 0 ? Cast<UUITextInput>(targetObjects[0].Get()) : nullptr;
	if (TargetScriptPtr == nullptr)
	{
		UE_LOG(DreamGUIEditor, Log, TEXT("[%s].%d Get TargetScript is null"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return;
	}

	// The layout builder is owned by the details view and is thrown away by the very refresh these
	// delegates ask for, so a delegate must not capture it. IPropertyUtilities outlives a refresh.
	const TSharedPtr<IPropertyUtilities> PropertyUtilities = DetailBuilder.GetPropertyUtilities();

	IDetailCategoryBuilder& category = DetailBuilder.EditCategory("DreamGUI-Input");

	auto InputTypeHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUITextInput, InputType));
	InputTypeHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FUITextInputCustomization::ForceRefresh, PropertyUtilities));
	// Custom as the fallback because it is the value that hides NOTHING: a selection that disagrees
	// still has objects using CustomValidation, and hiding it would hide a live property from them.
	const auto InputType = (EUITextInputType)DreamDetailsMultiSelect::ValueOr<uint8>(
		InputTypeHandle, (uint8)EUITextInputType::Custom);
	if (InputType != EUITextInputType::Custom)
	{
		DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UUITextInput, CustomValidation));
	}
	auto DisplayTypeHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUITextInput, DisplayType));
	DisplayTypeHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FUITextInputCustomization::ForceRefresh, PropertyUtilities));
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
	AllowMultilineHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FUITextInputCustomization::ForceRefresh, PropertyUtilities));
	if (DreamDetailsMultiSelect::AllEqual(AllowMultilineHandle, false))
	{
		DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UUITextInput, MultiLineSubmitFunctionKeys));
	}
}
void FUITextInputCustomization::ForceRefresh(TSharedPtr<IPropertyUtilities> PropertyUtilities)
{
	if (TargetScriptPtr.IsValid() && PropertyUtilities.IsValid())
	{
		PropertyUtilities->ForceRefresh();
	}
}
#undef LOCTEXT_NAMESPACE