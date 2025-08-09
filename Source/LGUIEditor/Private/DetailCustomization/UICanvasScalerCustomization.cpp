// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DetailCustomization/UICanvasScalerCustomization.h"
#include "Core/Components/LexCanvasScaler.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/SSlider.h"

#include "LGUIEditorModule.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "LGUIEditorUtils.h"
#include "Core/Components/LexCanvas.h"

#define LOCTEXT_NAMESPACE "UICanvasScalarCustomization"

TSharedRef<IDetailCustomization> FUICanvasScalerCustomization::MakeInstance()
{
	return MakeShareable(new FUICanvasScalerCustomization);
}
void FUICanvasScalerCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> targetObjects;
	DetailBuilder.GetObjectsBeingCustomized(targetObjects);
	TargetScriptPtr = Cast<ULexCanvasScaler>(targetObjects[0].Get());
	if (TargetScriptPtr != nullptr)
	{

	}
	else
	{
		UE_LOG(LGUIEditor, Log, TEXT("[%s].%d Get TargetScript is null"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
	}
	LGUIEditorUtils::ShowError_RequireComponent(&DetailBuilder, TargetScriptPtr.Get(), ULexCanvas::StaticClass());
	LGUIEditorUtils::ShowError_MultiComponentNotAllowed(&DetailBuilder, TargetScriptPtr.Get());

	TargetScriptPtr->ForceUpdate();

	IDetailCategoryBuilder& lguiCategory = DetailBuilder.EditCategory("LGUI");
	TArray<FName> needToHidePropertyNameArray;
	//add all property
	needToHidePropertyNameArray.Add(GET_MEMBER_NAME_CHECKED(ULexCanvasScaler, ProjectionType));
	needToHidePropertyNameArray.Add(GET_MEMBER_NAME_CHECKED(ULexCanvasScaler, FOVAngle));
	needToHidePropertyNameArray.Add(GET_MEMBER_NAME_CHECKED(ULexCanvasScaler, NearClipPlane));
	needToHidePropertyNameArray.Add(GET_MEMBER_NAME_CHECKED(ULexCanvasScaler, FarClipPlane));

	needToHidePropertyNameArray.Add(GET_MEMBER_NAME_CHECKED(ULexCanvasScaler, UIScaleMode));
	needToHidePropertyNameArray.Add(GET_MEMBER_NAME_CHECKED(ULexCanvasScaler, ReferenceResolution));
	needToHidePropertyNameArray.Add(GET_MEMBER_NAME_CHECKED(ULexCanvasScaler, ScreenMatchMode));
	needToHidePropertyNameArray.Add(GET_MEMBER_NAME_CHECKED(ULexCanvasScaler, MatchFromWidthToHeight));
	needToHidePropertyNameArray.Add(GET_MEMBER_NAME_CHECKED(ULexCanvasScaler, CustomScale));

	auto CreateSlider = [this, &lguiCategory](const FText& FilterString, TSharedPtr<IPropertyHandle> Property) {
		lguiCategory.AddCustomRow(FilterString)
		.PropertyHandleList({ Property })
		.NameContent()
		[
			SNew(SBox)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("Match", "Match"))
				.Font(IDetailLayoutBuilder::GetDetailFont())
			]
		]
		.ValueContent()
		.MinDesiredWidth(500)
		[
			SAssignNew(ValueBox, SHorizontalBox)
			+SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SBox)
				.WidthOverride(this, &FUICanvasScalerCustomization::GetValueWidth)
				[
					SNew(SVerticalBox)
					+SVerticalBox::Slot()
					[
						SNew(SSlider)
						.Value_Lambda([=]{
							float value = 0.0;
							Property->GetValue(value);
							return value;
							})
						.OnValueChanged_Lambda([=](float value){
							Property->SetValue(value);
							})
					]
					+SVerticalBox::Slot()
					[
						SNew(SHorizontalBox)
						+SHorizontalBox::Slot()
						[
							SNew(STextBlock)
							.Text(LOCTEXT("Width", "Width"))
							.Font(IDetailLayoutBuilder::GetDetailFont())
						]
						+ SHorizontalBox::Slot()
						.HAlign(EHorizontalAlignment::HAlign_Right)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("Height", "Height"))
							.Font(IDetailLayoutBuilder::GetDetailFont())
						]
					]
				]
			]
			+SHorizontalBox::Slot()
			.HAlign(EHorizontalAlignment::HAlign_Right)
			[
				SNew(SBox)
				.MinDesiredWidth(50)
				[
					Property->CreatePropertyValueWidget()
				]
			]
		]
		;
		};

	if (TargetScriptPtr->CheckCanvas())
	{
		auto canvas = TargetScriptPtr->Canvas;
		if (!canvas->IsRootCanvas())
		{
			auto Msg = LOCTEXT("OnlyForRootLGUICanvas", "This component is only valid for root LGUICanvas");
			LGUIEditorUtils::ShowError(&lguiCategory, Msg);
		}
		else
		{
			if (canvas->GetRenderMode() == ELexRenderMode::WorldSpace || canvas->GetRenderMode() == ELexRenderMode::WorldSpace_LGUI)
			{
				lguiCategory.AddCustomRow(LOCTEXT("WorldSpaceUIInfo", "WorldSpaceUIInfo"))
					.WholeRowContent()
					.MinDesiredWidth(500)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Font(IDetailLayoutBuilder::GetDetailFont())
						.Text(LOCTEXT("NothingHereForWorldSpaceUI", "Nothing here for WorldSpaceUI"))
						.AutoWrapText(true)
					];
			}
			else if (
				canvas->GetRenderMode() == ELexRenderMode::ScreenSpaceOverlay
				|| canvas->GetRenderMode() == ELexRenderMode::RenderTarget
				)
			{
				lguiCategory.AddProperty(GET_MEMBER_NAME_CHECKED(ULexCanvasScaler, UIScaleMode));

				DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexCanvasScaler, UIScaleMode))
					->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([&] { DetailBuilder.ForceRefreshDetails(); }));
				if (TargetScriptPtr->UIScaleMode == ELGUICanvasScaleMode::ScaleWithScreenSize)
				{
					lguiCategory.AddProperty(GET_MEMBER_NAME_CHECKED(ULexCanvasScaler, ReferenceResolution));
					lguiCategory.AddProperty(GET_MEMBER_NAME_CHECKED(ULexCanvasScaler, ScreenMatchMode));
					DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexCanvasScaler, ScreenMatchMode))
						->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([&] { DetailBuilder.ForceRefreshDetails(); }));
					switch (TargetScriptPtr->ScreenMatchMode)
					{
					case ELGUICanvasScreenMatchMode::Expand:
					case ELGUICanvasScreenMatchMode::Shrink:
					{
						
					}
					break;
					case ELGUICanvasScreenMatchMode::MatchWidthOrHeight:
					{
						auto matchProperty = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexCanvasScaler, MatchFromWidthToHeight));
						CreateSlider(LOCTEXT("MatchSlider", "MatchSlider"), matchProperty);
					}
					break;
					}
					needToHidePropertyNameArray.Add(GET_MEMBER_NAME_CHECKED(ULexCanvasScaler, CustomScale));
				}
				else if (TargetScriptPtr->UIScaleMode == ELGUICanvasScaleMode::ConstantPixelSize)
				{
					needToHidePropertyNameArray.Add(GET_MEMBER_NAME_CHECKED(ULexCanvasScaler, ReferenceResolution));
					needToHidePropertyNameArray.Add(GET_MEMBER_NAME_CHECKED(ULexCanvasScaler, ScreenMatchMode));
					needToHidePropertyNameArray.Add(GET_MEMBER_NAME_CHECKED(ULexCanvasScaler, MatchFromWidthToHeight));
					needToHidePropertyNameArray.Add(GET_MEMBER_NAME_CHECKED(ULexCanvasScaler, CustomScale));
				}
				else
				{
					lguiCategory.AddProperty(GET_MEMBER_NAME_CHECKED(ULexCanvasScaler, CustomScale));
					needToHidePropertyNameArray.Add(GET_MEMBER_NAME_CHECKED(ULexCanvasScaler, ReferenceResolution));
					needToHidePropertyNameArray.Add(GET_MEMBER_NAME_CHECKED(ULexCanvasScaler, ScreenMatchMode));
					needToHidePropertyNameArray.Add(GET_MEMBER_NAME_CHECKED(ULexCanvasScaler, MatchFromWidthToHeight));
				}

				auto projectionTypeHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexCanvasScaler, ProjectionType));
				projectionTypeHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([&] { DetailBuilder.ForceRefreshDetails(); }));
				if (TargetScriptPtr->ProjectionType == ECameraProjectionMode::Orthographic)
				{
					needToHidePropertyNameArray.Add(GET_MEMBER_NAME_CHECKED(ULexCanvasScaler, FOVAngle));
				}

				lguiCategory.AddProperty(GET_MEMBER_NAME_CHECKED(ULexCanvasScaler, ProjectionType));
				lguiCategory.AddProperty(GET_MEMBER_NAME_CHECKED(ULexCanvasScaler, FOVAngle));
				lguiCategory.AddProperty(GET_MEMBER_NAME_CHECKED(ULexCanvasScaler, NearClipPlane));
				lguiCategory.AddProperty(GET_MEMBER_NAME_CHECKED(ULexCanvasScaler, FarClipPlane));
			}
		}
	}
	for (auto item : needToHidePropertyNameArray)
	{
		DetailBuilder.HideProperty(item);
	}
}
FOptionalSize FUICanvasScalerCustomization::GetValueWidth()const
{
	return ValueBox->GetCachedGeometry().GetLocalSize().X - 60;
}
#undef LOCTEXT_NAMESPACE