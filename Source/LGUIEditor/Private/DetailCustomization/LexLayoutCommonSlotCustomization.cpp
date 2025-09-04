// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DetailCustomization/LexLayoutCommonSlotCustomization.h"
#include "LGUIEditorUtils.h"
#include "LGUIEditorModule.h"
#include "DetailLayoutBuilder.h"
#include "Core/Components/LexLayoutCommonSlot.h"
#include "Core/Components/LexLayoutHorizontalAndVertical.h"

#define LOCTEXT_NAMESPACE "LexLayoutHorizontalAndVerticalSlotCustomization"
FLexLayoutCommonSlotCustomization::FLexLayoutCommonSlotCustomization()
{
}

FLexLayoutCommonSlotCustomization::~FLexLayoutCommonSlotCustomization()
{
	
}

TSharedRef<IDetailCustomization> FLexLayoutCommonSlotCustomization::MakeInstance()
{
	return MakeShareable(new FLexLayoutCommonSlotCustomization);
}
void FLexLayoutCommonSlotCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> targetObjects;
	DetailBuilder.GetObjectsBeingCustomized(targetObjects);
	TargetScriptArray.Empty();
	for (auto item : targetObjects)
	{
		if (auto validItem = Cast<ULexLayoutCommonSlot>(item.Get()))
		{
			TargetScriptArray.Add(validItem);
		}
	}
	if (TargetScriptArray.Num() == 0)
	{
		UE_LOG(LGUIEditor, Log, TEXT("[%s].%d Get TargetScript is null"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return;
	}

	auto& Category = DetailBuilder.EditCategory("LayoutSlot");
	auto CreateOverridePropertyWidget = [&](TSharedPtr<IPropertyHandle> PropertyHandle, const TAttribute<float>& ValueWhenCheckOn)
	{
		Category.AddCustomRow(PropertyHandle->GetPropertyDisplayName())
		.NameContent()
		[
			PropertyHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		[
			SNew(SBox)
			.VAlign(VAlign_Center)
			[
				SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SCheckBox)
					.OnCheckStateChanged_Lambda([=](ECheckBoxState InCheckboxState){
						float Value = InCheckboxState == ECheckBoxState::Checked ? ValueWhenCheckOn.Get() : -1;
						PropertyHandle->SetValue(Value);
						})
					.IsChecked_Lambda([=] {
						float Value;
						if (PropertyHandle->GetValue(Value) == FPropertyAccess::Success)
						{
							return Value >= 0 ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
						}
						return ECheckBoxState::Unchecked;
						})
				]
				+SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.Padding(2, 0, 0, 0)
				[
					SNew(SBox)
					.Visibility(TAttribute<EVisibility>::CreateLambda([PropertyHandle]()
					{
						float Value;
						if (PropertyHandle->GetValue(Value) == FPropertyAccess::Success)
						{
							return Value >= 0 ? EVisibility::Visible : EVisibility::Collapsed;
						}
						return EVisibility::Collapsed;
					}))
					[
						PropertyHandle->CreatePropertyValueWidget()
					]
				]
			]
		]
		.PropertyHandleList({PropertyHandle})
		;
	};
	auto MinWidth_PH = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexLayoutCommonSlot, MinWidth));
	auto MinHeight_PH = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexLayoutCommonSlot, MinHeight));
	auto PreferredWidth_PH = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexLayoutCommonSlot, PreferredWidth));
	auto PreferredHeight_PH = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexLayoutCommonSlot, PreferredHeight));
	auto FlexibleWidth_PH = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexLayoutCommonSlot, FlexibleWidth));
	auto FlexibleHeight_PH = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexLayoutCommonSlot, FlexibleHeight));
	Category.AddProperty(GET_MEMBER_NAME_CHECKED(ULexLayoutCommonSlot, bIgnoreLayout));
	CreateOverridePropertyWidget(MinWidth_PH, 0);
	CreateOverridePropertyWidget(MinHeight_PH, 0);
	CreateOverridePropertyWidget(PreferredWidth_PH, TAttribute<float>::CreateLambda([&]()
	{
		if (TargetScriptArray.Num() > 0 && TargetScriptArray[0].IsValid())
		{
			return TargetScriptArray[0]->GetWidget()->GetWidth();
		}
		return 0.0f;
	}));
	CreateOverridePropertyWidget(PreferredHeight_PH, TAttribute<float>::CreateLambda([&]()
	{
		if (TargetScriptArray.Num() > 0 && TargetScriptArray[0].IsValid())
		{
			return TargetScriptArray[0]->GetWidget()->GetHeight();
		}
		return 0.0f;
	}));
	CreateOverridePropertyWidget(FlexibleWidth_PH, 1);
	CreateOverridePropertyWidget(FlexibleHeight_PH, 1);
	DetailBuilder.HideProperty(MinWidth_PH);
	DetailBuilder.HideProperty(MinHeight_PH);
	DetailBuilder.HideProperty(PreferredWidth_PH);
	DetailBuilder.HideProperty(PreferredHeight_PH);
	DetailBuilder.HideProperty(FlexibleWidth_PH);
	DetailBuilder.HideProperty(FlexibleHeight_PH);
}

#undef LOCTEXT_NAMESPACE