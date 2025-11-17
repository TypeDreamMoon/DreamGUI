// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DetailCustomization/LexLayoutFlexBoxSelfCustomization.h"
#include "LGUIEditorUtils.h"
#include "LGUIEditorModule.h"
#include "DetailLayoutBuilder.h"
#include "Core/Components/LexLayoutFlexBoxSelf.h"
#include "PropertyType/LexLayoutAspectRatioCustomization.h"
#include "PropertyType/LexLayoutMinMaxSizeCustomization.h"
#include "PropertyType/LexLayoutSizeCustomization.h"

#define LOCTEXT_NAMESPACE "LexLayoutHorizontalAndVerticalSlotCustomization"
FLexLayoutFlexBoxSelfCustomization::FLexLayoutFlexBoxSelfCustomization()
{
}

FLexLayoutFlexBoxSelfCustomization::~FLexLayoutFlexBoxSelfCustomization()
{
	
}

TSharedRef<IDetailCustomization> FLexLayoutFlexBoxSelfCustomization::MakeInstance()
{
	return MakeShareable(new FLexLayoutFlexBoxSelfCustomization);
}
void FLexLayoutFlexBoxSelfCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> targetObjects;
	DetailBuilder.GetObjectsBeingCustomized(targetObjects);
	TargetScriptArray.Empty();
	for (auto item : targetObjects)
	{
		if (auto validItem = Cast<ULexLayoutFlexBoxSelf>(item.Get()))
		{
			TargetScriptArray.Add(validItem);
		}
	}
	if (TargetScriptArray.Num() == 0)
	{
		UE_LOG(LGUIEditor, Log, TEXT("[%s].%d Get TargetScript is null"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return;
	}

	DetailBuilder.GetDetailsView()->RegisterInstancedCustomPropertyTypeLayout(FLexLayoutAspectRatio::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FLexLayoutAspectRatioCustomization::MakeInstance));
	DetailBuilder.GetDetailsView()->RegisterInstancedCustomPropertyTypeLayout(FLexLayoutSize::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FLexLayoutSizeCustomization::MakeInstance));
	DetailBuilder.GetDetailsView()->RegisterInstancedCustomPropertyTypeLayout(FLexLayoutMinMaxSize::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FLexLayoutMinMaxSizeCustomization::MakeInstance));

	auto& Category = DetailBuilder.EditCategory("LayoutSelf");

	auto AspectRatio_PH = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexLayoutFlexBoxSelf, AspectRatio));
	auto AspectRatioType_PH = AspectRatio_PH->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexLayoutAspectRatio, Type));
	auto Width_PH = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexLayoutFlexBoxSelf, PreferredWidth));
	auto Height_PH = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexLayoutFlexBoxSelf, PreferredHeight));
	Category.AddProperty(AspectRatio_PH);
	Category.AddProperty(Width_PH).IsEnabled(TAttribute<bool>::CreateSPLambda(this, [AspectRatioType_PH]()
		{
			ELexLayoutAspectRatioType AspectRatioFitType = ELexLayoutAspectRatioType::None;
			if (AspectRatioType_PH->GetValue(*(uint8*)&AspectRatioFitType) == FPropertyAccess::Success)
			{
				if (AspectRatioFitType == ELexLayoutAspectRatioType::HeightControlWidth)
				{
					return false;
				}
			}
			return true;
		}));
	Category.AddProperty(Height_PH).IsEnabled(TAttribute<bool>::CreateSPLambda(this, [AspectRatioType_PH]()
		{
			ELexLayoutAspectRatioType AspectRatioFitType = ELexLayoutAspectRatioType::None;
			if (AspectRatioType_PH->GetValue(*(uint8*)&AspectRatioFitType) == FPropertyAccess::Success)
			{
				if (AspectRatioFitType == ELexLayoutAspectRatioType::WidthControlHeight)
				{
					return false;
				}
			}
			return true;
		}));
	
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
	// auto MinWidth_PH = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexLayoutCommonSlot, MinWidth));
	// auto MinHeight_PH = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexLayoutCommonSlot, MinHeight));
	// auto PreferredWidth_PH = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexLayoutCommonSlot, PreferredWidth));
	// auto PreferredHeight_PH = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexLayoutCommonSlot, PreferredHeight));
	// auto GrowWidth_PH = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexLayoutCommonSlot, GrowWidth));
	// auto GrowHeight_PH = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexLayoutCommonSlot, GrowHeight));
	// Category.AddProperty(ULexLayoutCommonSlot::GetPropertyName_IgnoreLayout());
	// CreateOverridePropertyWidget(MinWidth_PH, 0);
	// CreateOverridePropertyWidget(MinHeight_PH, 0);
	// CreateOverridePropertyWidget(PreferredWidth_PH, TAttribute<float>::CreateLambda([&]()
	// {
	// 	if (TargetScriptArray.Num() > 0 && TargetScriptArray[0].IsValid())
	// 	{
	// 		return TargetScriptArray[0]->GetWidget()->GetWidth();
	// 	}
	// 	return 0.0f;
	// }));
	// CreateOverridePropertyWidget(PreferredHeight_PH, TAttribute<float>::CreateLambda([&]()
	// {
	// 	if (TargetScriptArray.Num() > 0 && TargetScriptArray[0].IsValid())
	// 	{
	// 		return TargetScriptArray[0]->GetWidget()->GetHeight();
	// 	}
	// 	return 0.0f;
	// }));
	// CreateOverridePropertyWidget(GrowWidth_PH, 1);
	// CreateOverridePropertyWidget(GrowHeight_PH, 1);
	// DetailBuilder.HideProperty(MinWidth_PH);
	// DetailBuilder.HideProperty(MinHeight_PH);
	// DetailBuilder.HideProperty(PreferredWidth_PH);
	// DetailBuilder.HideProperty(PreferredHeight_PH);
	// DetailBuilder.HideProperty(GrowWidth_PH);
	// DetailBuilder.HideProperty(GrowHeight_PH);
}

#undef LOCTEXT_NAMESPACE