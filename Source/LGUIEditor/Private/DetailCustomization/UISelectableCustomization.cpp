// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DetailCustomization/UISelectableCustomization.h"
#include "LGUIEditorUtils.h"
#include "Core/Components/LexWidget.h"
#include "IDetailGroup.h"
#include "Interaction/UISelectableComponent.h"
#include "Core/Components/UISprite.h"
#include "LGUIEditorModule.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "Core/LexUISettings.h"
#include "Core/Components/LexImage.h"

#define LOCTEXT_NAMESPACE "UISelectableCustomization"

TSharedRef<IDetailCustomization> FUISelectableCustomization::MakeInstance()
{
	return MakeShareable(new FUISelectableCustomization);
}
FUISelectableCustomization::~FUISelectableCustomization()
{
	//SLGUISpriteSelector::CloseTab();
}
void FUISelectableCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> targetObjects;
	DetailBuilder.GetObjectsBeingCustomized(targetObjects);
	TargetScriptPtr = Cast<UUISelectableComponent>(targetObjects[0].Get());
	if (TargetScriptPtr != nullptr)
	{
		
	}
	else
	{
		UE_LOG(LGUIEditor, Log, TEXT("[%s].%d Get TargetScript is null"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
	}

	LGUIEditorUtils::ShowError_MultiComponentNotAllowed(&DetailBuilder, TargetScriptPtr.Get(), LOCTEXT("MultipleUISelectableComponentError", "Multiple UISelectable component in one actor is not allowed!"));

	IDetailCategoryBuilder& category = DetailBuilder.EditCategory("LGUI-Selectable");
	auto Transition_PH = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUISelectableComponent, Transition));
	Transition_PH->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FUISelectableCustomization::ForceRefresh, &DetailBuilder));

	auto TransitionTarget_PH = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUISelectableComponent, TransitionTarget));
	TransitionTarget_PH->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FUISelectableCustomization::ForceRefresh, &DetailBuilder));

	ULexVisual* TransitionTarget_Visual = nullptr;
	TransitionTarget_PH->GetValue(*(UObject**)&TransitionTarget_Visual);
	auto TransitionTarget_Image = Cast<ULexImage>(TransitionTarget_Visual);

	UUISelectableTransitionComponent* TargetTweenComp = nullptr;
	auto CustomTransition_PH = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUISelectableComponent, CustomTransition));
	CustomTransition_PH->GetValue(*(UObject**)&TargetTweenComp);

	uint8 TransitionType;
	Transition_PH->GetValue(TransitionType);
	TArray<FName> NeedToHidePropertyNamesForTransition;
	IDetailGroup& TransitionGroup = category.AddGroup(FName("Transition"), Transition_PH->GetPropertyDisplayName());
	TransitionGroup.HeaderProperty(Transition_PH);
	if (TransitionType == (uint8)(ELexUISelectableTransitionType::None))
	{
		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUISelectableComponent, TransitionTarget));

		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUISelectableComponent, NormalColor));
		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUISelectableComponent, HoveredColor));
		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUISelectableComponent, PressedColor));
		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUISelectableComponent, DisabledColor));
		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUISelectableComponent, AnimDuration));

		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUISelectableComponent, NormalImageBrush));
		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUISelectableComponent, HoveredImageBrush));
		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUISelectableComponent, PressedImageBrush));
		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUISelectableComponent, DisabledImageBrush));
		
		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUISelectableComponent, CustomTransition));
	}
	else if (TransitionType == (uint8)(ELexUISelectableTransitionType::Color))
	{
		TransitionGroup.AddPropertyRow(TransitionTarget_PH);
		if (!TransitionTarget_Visual)
		{
			TransitionGroup.AddWidgetRow()
				.ValueContent()
				.MinDesiredWidth(500)
				[
					SNew(STextBlock)
					.AutoWrapText(true)
					.Text(LOCTEXT("TransitionActor_Color_Tip", "If use Color transition, Target must be a LexVisual"))
					.ColorAndOpacity(FLinearColor(FColor::Red))
					.Font(IDetailLayoutBuilder::GetDetailFont())
				];
		}
		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUISelectableComponent, NormalImageBrush));
		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUISelectableComponent, HoveredImageBrush));
		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUISelectableComponent, PressedImageBrush));
		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUISelectableComponent, DisabledImageBrush));

		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUISelectableComponent, CustomTransition));

		TransitionGroup.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUISelectableComponent, NormalColor)));
		TransitionGroup.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUISelectableComponent, HoveredColor)));
		TransitionGroup.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUISelectableComponent, PressedColor)));
		TransitionGroup.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUISelectableComponent, DisabledColor)));
		TransitionGroup.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUISelectableComponent, AnimDuration)));
	}
	else if (TransitionType == (uint8)(ELexUISelectableTransitionType::ImageBrush))
	{
		TransitionGroup.AddPropertyRow(TransitionTarget_PH);
		if (!TransitionTarget_Image)
		{
			TransitionGroup.AddWidgetRow()
				.ValueContent()
				.MinDesiredWidth(500)
				[
					SNew(STextBlock)
					.AutoWrapText(true)
					.Text(LOCTEXT("TransitionTarget_ImageBrush_Tip", "If use ImageBrush, Target must be a LexImage"))
					.ColorAndOpacity(FLinearColor(FColor::Red))
					.Font(IDetailLayoutBuilder::GetDetailFont())
				];
		}
		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUISelectableComponent, NormalColor));
		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUISelectableComponent, HoveredColor));
		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUISelectableComponent, PressedColor));
		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUISelectableComponent, DisabledColor));
		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUISelectableComponent, AnimDuration));

		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUISelectableComponent, CustomTransition));
		
		TransitionGroup.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUISelectableComponent, NormalImageBrush)));
		TransitionGroup.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUISelectableComponent, HoveredImageBrush)));
		TransitionGroup.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUISelectableComponent, PressedImageBrush)));
		TransitionGroup.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUISelectableComponent, DisabledImageBrush)));
	}
	else if (TransitionType == (uint8)(ELexUISelectableTransitionType::Custom))
	{
		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUISelectableComponent, TransitionTarget));

		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUISelectableComponent, NormalColor));
		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUISelectableComponent, HoveredColor));
		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUISelectableComponent, PressedColor));
		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUISelectableComponent, DisabledColor));
		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUISelectableComponent, AnimDuration));

		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUISelectableComponent, NormalImageBrush));
		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUISelectableComponent, HoveredImageBrush));
		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUISelectableComponent, PressedImageBrush));
		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUISelectableComponent, DisabledImageBrush));

		TransitionGroup.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUISelectableComponent, CustomTransition)));
	}

	IDetailCategoryBuilder& NavigationCategory = DetailBuilder.EditCategory("LGUI-Selectable-Navigation");
	NavigationCategory.AddProperty(GET_MEMBER_NAME_CHECKED(UUISelectableComponent, bCanNavigateHere));
	
	auto navigationLeftHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUISelectableComponent, NavigationLeft));
	auto navigationRightHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUISelectableComponent, NavigationRight));
	auto navigationUpHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUISelectableComponent, NavigationUp));
	auto navigationDownHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUISelectableComponent, NavigationDown));
	auto navigationPrevHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUISelectableComponent, NavigationPrev));
	auto navigationNextHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUISelectableComponent, NavigationNext));
	
	ELexUISelectableNavigationMode tempEnumValue;
	navigationLeftHandle->GetValue(*(uint8*)&tempEnumValue);
	navigationLeftHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FUISelectableCustomization::ForceRefresh, &DetailBuilder));
	auto navigationLeftValue = tempEnumValue;
	navigationRightHandle->GetValue(*(uint8*)&tempEnumValue);
	navigationRightHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FUISelectableCustomization::ForceRefresh, &DetailBuilder));
	auto navigationRightValue = tempEnumValue;
	navigationUpHandle->GetValue(*(uint8*)&tempEnumValue);
	navigationUpHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FUISelectableCustomization::ForceRefresh, &DetailBuilder));
	auto navigationUpValue = tempEnumValue;
	navigationDownHandle->GetValue(*(uint8*)&tempEnumValue);
	navigationDownHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FUISelectableCustomization::ForceRefresh, &DetailBuilder));
	auto navigationDownValue = tempEnumValue;

	NavigationCategory.AddProperty(navigationLeftHandle);
	if (navigationLeftValue == ELexUISelectableNavigationMode::Explicit)
	{
		LGUIEditorUtils::CreateSubDetail(&NavigationCategory, &DetailBuilder, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUISelectableComponent, NavigationLeftSpecific)));
	}
	NavigationCategory.AddProperty(navigationRightHandle);
	if (navigationRightValue == ELexUISelectableNavigationMode::Explicit)
	{
		LGUIEditorUtils::CreateSubDetail(&NavigationCategory, &DetailBuilder, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUISelectableComponent, NavigationRightSpecific)));
	}
	NavigationCategory.AddProperty(navigationUpHandle);
	if (navigationUpValue == ELexUISelectableNavigationMode::Explicit)
	{
		LGUIEditorUtils::CreateSubDetail(&NavigationCategory, &DetailBuilder, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUISelectableComponent, NavigationUpSpecific)));
	}
	NavigationCategory.AddProperty(navigationDownHandle);
	if (navigationDownValue == ELexUISelectableNavigationMode::Explicit)
	{
		LGUIEditorUtils::CreateSubDetail(&NavigationCategory, &DetailBuilder, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUISelectableComponent, NavigationDownSpecific)));
	}

	navigationNextHandle->GetValue(*(uint8*)&tempEnumValue);
	navigationNextHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FUISelectableCustomization::ForceRefresh, &DetailBuilder));
	auto navigationNextValue = (ELexUISelectableNavigationMode)tempEnumValue;
	navigationPrevHandle->GetValue(*(uint8*)&tempEnumValue);
	navigationPrevHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FUISelectableCustomization::ForceRefresh, &DetailBuilder));
	auto navigationPrevValue = (ELexUISelectableNavigationMode)tempEnumValue;
	NavigationCategory.AddProperty(navigationPrevHandle);
	if (navigationPrevValue == ELexUISelectableNavigationMode::Explicit)
	{
		LGUIEditorUtils::CreateSubDetail(&NavigationCategory, &DetailBuilder, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUISelectableComponent, NavigationPrevSpecific)));
	}
	NavigationCategory.AddProperty(navigationNextHandle);
	if (navigationNextValue == ELexUISelectableNavigationMode::Explicit)
	{
		LGUIEditorUtils::CreateSubDetail(&NavigationCategory, &DetailBuilder, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUISelectableComponent, NavigationNextSpecific)));
	}
	NavigationCategory.AddCustomRow(LOCTEXT("VisualizeNavigation", "VisualizeNavigation"))
		.NameContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("Visualize", "Visualize"))
			.Font(IDetailLayoutBuilder::GetDetailFont())
		]
		.ValueContent()
		[
			SNew(SCheckBox)
			.IsChecked_Lambda([]() {
				return GetDefault<ULexUIEditorSettings>()->bDrawSelectableNavigationVisualizer ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
			.OnCheckStateChanged_Lambda([=](ECheckBoxState State)
			{
				GEditor->BeginTransaction(LOCTEXT("ToggleNavigationVisualizer_Transaction", "Toggle Navigation Visualizer"));
				auto LGUIEditorSetting = GetMutableDefault<ULexUIEditorSettings>();
				LGUIEditorSetting->Modify();
				LGUIEditorSetting->bDrawSelectableNavigationVisualizer = State == ECheckBoxState::Checked;
				GEditor->EndTransaction();
			})
		]
	;
	
	if (navigationLeftValue != ELexUISelectableNavigationMode::Explicit)
	{
		NeedToHidePropertyNamesForTransition.Add((GET_MEMBER_NAME_CHECKED(UUISelectableComponent, NavigationLeftSpecific)));
	}
	if (navigationRightValue != ELexUISelectableNavigationMode::Explicit)
	{
		NeedToHidePropertyNamesForTransition.Add((GET_MEMBER_NAME_CHECKED(UUISelectableComponent, NavigationRightSpecific)));
	}
	if (navigationUpValue != ELexUISelectableNavigationMode::Explicit)
	{
		NeedToHidePropertyNamesForTransition.Add((GET_MEMBER_NAME_CHECKED(UUISelectableComponent, NavigationUpSpecific)));
	}
	if (navigationDownValue != ELexUISelectableNavigationMode::Explicit)
	{
		NeedToHidePropertyNamesForTransition.Add((GET_MEMBER_NAME_CHECKED(UUISelectableComponent, NavigationDownSpecific)));
	}
	if (navigationNextValue != ELexUISelectableNavigationMode::Explicit)
	{
		NeedToHidePropertyNamesForTransition.Add((GET_MEMBER_NAME_CHECKED(UUISelectableComponent, NavigationNextSpecific)));
	}
	if (navigationPrevValue != ELexUISelectableNavigationMode::Explicit)
	{
		NeedToHidePropertyNamesForTransition.Add((GET_MEMBER_NAME_CHECKED(UUISelectableComponent, NavigationPrevSpecific)));
	}
	
	for (auto item : NeedToHidePropertyNamesForTransition)
	{
		DetailBuilder.HideProperty(item);
	}
}
void FUISelectableCustomization::ForceRefresh(IDetailLayoutBuilder* DetailBuilder)
{
	if (TargetScriptPtr.IsValid())
	{
		DetailBuilder->ForceRefreshDetails();
	}
}
#undef LOCTEXT_NAMESPACE