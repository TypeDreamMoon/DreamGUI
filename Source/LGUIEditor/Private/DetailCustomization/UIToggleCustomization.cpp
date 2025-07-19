// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DetailCustomization/UIToggleCustomization.h"
#include "LGUIEditorUtils.h"
#include "Core/Components/LexWidget.h"
#include "IDetailGroup.h"
#include "Interaction/UIToggleGroupComponent.h"
#include "Interaction/UIToggleComponent.h"
#include "LGUIEditorModule.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "Core/Components/LexImage.h"

#define LOCTEXT_NAMESPACE "UIToggleCustomization"

TSharedRef<IDetailCustomization> FUIToggleCustomization::MakeInstance()
{
	return MakeShareable(new FUIToggleCustomization);
}
void FUIToggleCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> targetObjects;
	DetailBuilder.GetObjectsBeingCustomized(targetObjects);
	TargetScriptPtr = Cast<UUIToggleComponent>(targetObjects[0].Get());
	if (TargetScriptPtr != nullptr)
	{
		
	}
	else
	{
		UE_LOG(LGUIEditor, Log, TEXT("Get TargetScript is null"));
	}

	IDetailCategoryBuilder& category = DetailBuilder.EditCategory("LGUI-Toggle");
	auto ToggleTransition_PH = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUIToggleComponent, ToggleTransition));
	ToggleTransition_PH->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FUIToggleCustomization::ForceRefresh, &DetailBuilder));

	auto ToggleTransitionTarget_PH = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUIToggleComponent, ToggleTransitionTarget));
	ToggleTransitionTarget_PH->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FUIToggleCustomization::ForceRefresh, &DetailBuilder));
	ULexVisual* ToggleTransitionTarget_Visual = nullptr;
	ToggleTransitionTarget_PH->GetValue(*(UObject**)&ToggleTransitionTarget_Visual);
	auto ToggleTransitionTarget_Image = Cast<ULexImage>(ToggleTransitionTarget_Visual);

	UUISelectableTransitionComponent* CustomTransition = nullptr;
	auto CustomTransition_PH = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUIToggleComponent, CustomToggleTransition));
	CustomTransition_PH->GetValue(*(UObject**)&CustomTransition);

	uint8 TransitionType;
	ToggleTransition_PH->GetValue(TransitionType);
	TArray<FName> NeedToHidePropertyNamesForTransition;
	IDetailGroup& TransitionGroup = category.AddGroup(FName("Transition"), LOCTEXT("Transition", "Transition"));
	TransitionGroup.HeaderProperty(ToggleTransition_PH);
	if (TransitionType == (uint8)(ELexUISelectableTransitionType::None))
	{
		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUIToggleComponent, ToggleTransitionTarget));
		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUIToggleComponent, OffImageBrush));
		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUIToggleComponent, OnImageBrush));
		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUIToggleComponent, OffColor));
		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUIToggleComponent, OnColor));
		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUIToggleComponent, OnTransitionName));
		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUIToggleComponent, OffTransitionName));
		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUIToggleComponent, ToggleDuration));
	}
	else if (TransitionType == (uint8)(ELexUISelectableTransitionType::ImageBrush))
	{
		TransitionGroup.AddPropertyRow(ToggleTransitionTarget_PH);
		if (!ToggleTransitionTarget_Image)
		{
			TransitionGroup.AddWidgetRow()
				.ValueContent()
				.MinDesiredWidth(500)
				[
					SNew(STextBlock)
					.AutoWrapText(true)
					.Text(LOCTEXT("TransitionTarget_ImageBrush_Tip", "If use ImageBrush, Target must be LexImage"))
					.Font(IDetailLayoutBuilder::GetDetailFont())
				];
		}
		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUIToggleComponent, OffColor));
		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUIToggleComponent, OnColor));
		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUIToggleComponent, OnTransitionName));
		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUIToggleComponent, OffTransitionName));

		TransitionGroup.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUIToggleComponent, OffImageBrush)));
		TransitionGroup.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUIToggleComponent, OnImageBrush)));
		TransitionGroup.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUIToggleComponent, ToggleDuration)));
	}
	else if (TransitionType == (uint8)(ELexUISelectableTransitionType::Color))
	{
		TransitionGroup.AddPropertyRow(ToggleTransitionTarget_PH);
		if (!ToggleTransitionTarget_Visual)
		{
			TransitionGroup.AddWidgetRow()
				.ValueContent()
				.MinDesiredWidth(500)
				[
					SNew(STextBlock)
					.AutoWrapText(true)
					.Text(LOCTEXT("TransitionTarget_Color_Tip", "If use Color transition, Target must be LexVisual"))
					.ColorAndOpacity(FLinearColor(FColor::Red))
					.Font(IDetailLayoutBuilder::GetDetailFont())
				];
		}
		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUIToggleComponent, OffImageBrush));
		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUIToggleComponent, OnImageBrush));
		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUIToggleComponent, OnTransitionName));
		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUIToggleComponent, OffTransitionName));

		TransitionGroup.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUIToggleComponent, OffColor)));
		TransitionGroup.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUIToggleComponent, OnColor)));
		TransitionGroup.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUIToggleComponent, ToggleDuration)));
	}
	else if (TransitionType == (uint8)(ELexUISelectableTransitionType::Custom))
	{
		TransitionGroup.AddPropertyRow(ToggleTransitionTarget_PH);
		if (!CustomTransition)
		{
			TransitionGroup.AddWidgetRow()
				.ValueContent()
				.MinDesiredWidth(500)
				[
					SNew(STextBlock)
					.AutoWrapText(true)
					.Text(LOCTEXT("TransitionTarget_Custom_Tip", "If use TransitionComponent, Target must have UUISelectableTransitionComponent component"))
					.ColorAndOpacity(FLinearColor(FColor::Red))
					.Font(IDetailLayoutBuilder::GetDetailFont())
				];
		}
		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUIToggleComponent, OffImageBrush));
		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUIToggleComponent, OnImageBrush));
		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUIToggleComponent, OffColor));
		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUIToggleComponent, OnColor));
	}
	for (auto item : NeedToHidePropertyNamesForTransition)
	{
		DetailBuilder.HideProperty(item);
	}
}
void FUIToggleCustomization::ForceRefresh(IDetailLayoutBuilder* DetailBuilder)
{
	if (TargetScriptPtr.IsValid())
	{
		DetailBuilder->ForceRefreshDetails();
	}
}
#undef LOCTEXT_NAMESPACE