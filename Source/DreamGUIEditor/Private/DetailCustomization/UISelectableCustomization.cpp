// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DetailCustomization/UISelectableCustomization.h"
#include "DreamUIEditorUtils.h"
#include "IDetailGroup.h"
#include "Interaction/UISelectable.h"
#include "DreamGUIEditorModule.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "Core/DreamUISettings.h"
#include "Core/Components/DreamImage.h"

#define LOCTEXT_NAMESPACE "UISelectableCustomization"

namespace UISelectableCustomizationLocal
{
	/** Explicit is the mode that reveals extra rows, so a selection whose modes disagree must not be read as Explicit. */
	EUISelectableNavigationMode ReadNavigationMode(TSharedRef<IPropertyHandle> NavigationHandle)
	{
		EUISelectableNavigationMode Value = EUISelectableNavigationMode::None;
		if (NavigationHandle->GetValue(*(uint8*)&Value) != FPropertyAccess::Success)return EUISelectableNavigationMode::None;
		return Value;
	}
}

TSharedRef<IDetailCustomization> FUISelectableCustomization::MakeInstance()
{
	return MakeShareable(new FUISelectableCustomization);
}
FUISelectableCustomization::~FUISelectableCustomization()
{
}
void FUISelectableCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> targetObjects;
	DetailBuilder.GetObjectsBeingCustomized(targetObjects);
	TargetScriptPtr = Cast<UUISelectable>(targetObjects[0].Get());
	if (TargetScriptPtr != nullptr)
	{
		
	}
	else
	{
		UE_LOG(DreamGUIEditor, Log, TEXT("[%s].%d Get TargetScript is null"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
	}

	FDreamUIEditorUtils::ShowError_MultiComponentNotAllowed(&DetailBuilder, TargetScriptPtr.Get(), LOCTEXT("MultipleUISelectableComponentError", "Multiple UISelectable component in one actor is not allowed!"));

	IDetailCategoryBuilder& category = DetailBuilder.EditCategory("DreamGUI-Selectable");
	auto Transition_PH = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUISelectable, TransitionType));
	Transition_PH->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FUISelectableCustomization::ForceRefresh, &DetailBuilder));

	auto TransitionTarget_PH = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUISelectable, TransitionTarget));
	TransitionTarget_PH->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FUISelectableCustomization::ForceRefresh, &DetailBuilder));

	UDreamVisual* TransitionTarget_Visual = nullptr;
	TransitionTarget_PH->GetValue(*(UObject**)&TransitionTarget_Visual);

	UUISelectableTransition* TargetTweenComp = nullptr;
	auto CustomTransition_PH = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUISelectable, CustomTransition));
	CustomTransition_PH->GetValue(*(UObject**)&TargetTweenComp);

	uint8 TransitionType = (uint8)(EUISelectableTransitionType::None);
	//a selection that mixes transition types has no rows to agree on, so it falls into the branch that leaves only the header
	bool bIsSingleTransitionType = Transition_PH->GetValue(TransitionType) == FPropertyAccess::Success;
	TArray<FName> NeedToHidePropertyNamesForTransition;
	IDetailGroup& TransitionGroup = category.AddGroup(FName("Transition"), Transition_PH->GetPropertyDisplayName());
	TransitionGroup.HeaderProperty(Transition_PH);
	if (!bIsSingleTransitionType || TransitionType == (uint8)(EUISelectableTransitionType::None))
	{
		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUISelectable, TransitionTarget));

		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUISelectable, NormalColor));
		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUISelectable, HoveredColor));
		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUISelectable, PressedColor));
		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUISelectable, DisabledColor));
		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUISelectable, AnimDuration));

		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUISelectable, NormalImageBrush));
		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUISelectable, HoveredImageBrush));
		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUISelectable, PressedImageBrush));
		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUISelectable, DisabledImageBrush));

		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUISelectable, bUseFocusedVisuals));
		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUISelectable, FocusedColor));
		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUISelectable, FocusedImageBrush));
		
		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUISelectable, CustomTransition));
	}
	else if (TransitionType == (uint8)(EUISelectableTransitionType::Color))
	{
		TransitionGroup.AddPropertyRow(TransitionTarget_PH);
		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUISelectable, NormalImageBrush));
		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUISelectable, HoveredImageBrush));
		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUISelectable, PressedImageBrush));
		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUISelectable, DisabledImageBrush));
		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUISelectable, FocusedImageBrush));

		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUISelectable, CustomTransition));

		TransitionGroup.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUISelectable, NormalColor)));
		TransitionGroup.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUISelectable, HoveredColor)));
		TransitionGroup.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUISelectable, PressedColor)));
		TransitionGroup.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUISelectable, DisabledColor)));
		// Focus sits after the four states it is an alternative to, behind the switch that turns it on:
		// most controls never want a look of their own for it, and an always-visible colour row invites
		// a designer to set one that then does nothing.
		TransitionGroup.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUISelectable, bUseFocusedVisuals)));
		TransitionGroup.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUISelectable, FocusedColor)));
		TransitionGroup.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUISelectable, AnimDuration)));
	}
	else if (TransitionType == (uint8)(EUISelectableTransitionType::ImageBrush))
	{
		TransitionGroup.AddPropertyRow(TransitionTarget_PH);
		if (TransitionTarget_Visual && !TransitionTarget_Visual->IsA<UDreamImage>())
		{
			TransitionGroup.AddWidgetRow()
				.ValueContent()
				.MinDesiredWidth(500)
				[
					SNew(STextBlock)
					.AutoWrapText(true)
					.Text(LOCTEXT("TransitionTarget_ImageBrush_Tip", "If use ImageBrush, Target must be a DreamImage"))
					.ColorAndOpacity(FLinearColor(FColor::Red))
					.Font(IDetailLayoutBuilder::GetDetailFont())
				];
		}
		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUISelectable, NormalColor));
		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUISelectable, HoveredColor));
		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUISelectable, PressedColor));
		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUISelectable, DisabledColor));
		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUISelectable, FocusedColor));
		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUISelectable, AnimDuration));

		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUISelectable, CustomTransition));
		
		TransitionGroup.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUISelectable, NormalImageBrush)));
		TransitionGroup.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUISelectable, HoveredImageBrush)));
		TransitionGroup.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUISelectable, PressedImageBrush)));
		TransitionGroup.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUISelectable, DisabledImageBrush)));
		TransitionGroup.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUISelectable, bUseFocusedVisuals)));
		TransitionGroup.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUISelectable, FocusedImageBrush)));
	}
	else if (TransitionType == (uint8)(EUISelectableTransitionType::Custom))
	{
		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUISelectable, TransitionTarget));

		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUISelectable, NormalColor));
		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUISelectable, HoveredColor));
		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUISelectable, PressedColor));
		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUISelectable, DisabledColor));
		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUISelectable, AnimDuration));

		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUISelectable, NormalImageBrush));
		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUISelectable, HoveredImageBrush));
		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUISelectable, PressedImageBrush));
		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUISelectable, DisabledImageBrush));

		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUISelectable, FocusedColor));
		NeedToHidePropertyNamesForTransition.Add(GET_MEMBER_NAME_CHECKED(UUISelectable, FocusedImageBrush));

		TransitionGroup.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUISelectable, CustomTransition)));
		// The switch still matters with a custom transition: it is what decides between OnFocused and
		// OnHovered, and a transition written before focus existed only implements the latter.
		TransitionGroup.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUISelectable, bUseFocusedVisuals)));
	}

	IDetailCategoryBuilder& NavigationCategory = DetailBuilder.EditCategory("DreamGUI-Selectable-Navigation");
	NavigationCategory.AddProperty(GET_MEMBER_NAME_CHECKED(UUISelectable, bCanNavigateHere));
	
	auto navigationLeftHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUISelectable, NavigationLeft));
	auto navigationRightHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUISelectable, NavigationRight));
	auto navigationUpHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUISelectable, NavigationUp));
	auto navigationDownHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUISelectable, NavigationDown));
	auto navigationPrevHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUISelectable, NavigationPrev));
	auto navigationNextHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUISelectable, NavigationNext));
	
	auto navigationLeftValue = UISelectableCustomizationLocal::ReadNavigationMode(navigationLeftHandle);
	navigationLeftHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FUISelectableCustomization::ForceRefresh, &DetailBuilder));
	auto navigationRightValue = UISelectableCustomizationLocal::ReadNavigationMode(navigationRightHandle);
	navigationRightHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FUISelectableCustomization::ForceRefresh, &DetailBuilder));
	auto navigationUpValue = UISelectableCustomizationLocal::ReadNavigationMode(navigationUpHandle);
	navigationUpHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FUISelectableCustomization::ForceRefresh, &DetailBuilder));
	auto navigationDownValue = UISelectableCustomizationLocal::ReadNavigationMode(navigationDownHandle);
	navigationDownHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FUISelectableCustomization::ForceRefresh, &DetailBuilder));

	NavigationCategory.AddProperty(navigationLeftHandle);
	if (navigationLeftValue == EUISelectableNavigationMode::Explicit)
	{
		FDreamUIEditorUtils::CreateSubDetail(&NavigationCategory, &DetailBuilder, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUISelectable, NavigationLeftSpecific)));
	}
	NavigationCategory.AddProperty(navigationRightHandle);
	if (navigationRightValue == EUISelectableNavigationMode::Explicit)
	{
		FDreamUIEditorUtils::CreateSubDetail(&NavigationCategory, &DetailBuilder, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUISelectable, NavigationRightSpecific)));
	}
	NavigationCategory.AddProperty(navigationUpHandle);
	if (navigationUpValue == EUISelectableNavigationMode::Explicit)
	{
		FDreamUIEditorUtils::CreateSubDetail(&NavigationCategory, &DetailBuilder, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUISelectable, NavigationUpSpecific)));
	}
	NavigationCategory.AddProperty(navigationDownHandle);
	if (navigationDownValue == EUISelectableNavigationMode::Explicit)
	{
		FDreamUIEditorUtils::CreateSubDetail(&NavigationCategory, &DetailBuilder, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUISelectable, NavigationDownSpecific)));
	}

	auto navigationNextValue = UISelectableCustomizationLocal::ReadNavigationMode(navigationNextHandle);
	navigationNextHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FUISelectableCustomization::ForceRefresh, &DetailBuilder));
	auto navigationPrevValue = UISelectableCustomizationLocal::ReadNavigationMode(navigationPrevHandle);
	navigationPrevHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FUISelectableCustomization::ForceRefresh, &DetailBuilder));
	NavigationCategory.AddProperty(navigationPrevHandle);
	if (navigationPrevValue == EUISelectableNavigationMode::Explicit)
	{
		FDreamUIEditorUtils::CreateSubDetail(&NavigationCategory, &DetailBuilder, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUISelectable, NavigationPrevSpecific)));
	}
	NavigationCategory.AddProperty(navigationNextHandle);
	if (navigationNextValue == EUISelectableNavigationMode::Explicit)
	{
		FDreamUIEditorUtils::CreateSubDetail(&NavigationCategory, &DetailBuilder, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUISelectable, NavigationNextSpecific)));
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
				return GetDefault<UDreamUIEditorSettings>()->bDrawSelectableNavigationVisualizer ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
			.OnCheckStateChanged_Lambda([=](ECheckBoxState State)
			{
				GEditor->BeginTransaction(LOCTEXT("ToggleNavigationVisualizer_Transaction", "Toggle Navigation Visualizer"));
				auto DreamGUIEditorSetting = GetMutableDefault<UDreamUIEditorSettings>();
				DreamGUIEditorSetting->Modify();
				DreamGUIEditorSetting->bDrawSelectableNavigationVisualizer = State == ECheckBoxState::Checked;
				GEditor->EndTransaction();
			})
		]
	;
	
	if (navigationLeftValue != EUISelectableNavigationMode::Explicit)
	{
		NeedToHidePropertyNamesForTransition.Add((GET_MEMBER_NAME_CHECKED(UUISelectable, NavigationLeftSpecific)));
	}
	if (navigationRightValue != EUISelectableNavigationMode::Explicit)
	{
		NeedToHidePropertyNamesForTransition.Add((GET_MEMBER_NAME_CHECKED(UUISelectable, NavigationRightSpecific)));
	}
	if (navigationUpValue != EUISelectableNavigationMode::Explicit)
	{
		NeedToHidePropertyNamesForTransition.Add((GET_MEMBER_NAME_CHECKED(UUISelectable, NavigationUpSpecific)));
	}
	if (navigationDownValue != EUISelectableNavigationMode::Explicit)
	{
		NeedToHidePropertyNamesForTransition.Add((GET_MEMBER_NAME_CHECKED(UUISelectable, NavigationDownSpecific)));
	}
	if (navigationNextValue != EUISelectableNavigationMode::Explicit)
	{
		NeedToHidePropertyNamesForTransition.Add((GET_MEMBER_NAME_CHECKED(UUISelectable, NavigationNextSpecific)));
	}
	if (navigationPrevValue != EUISelectableNavigationMode::Explicit)
	{
		NeedToHidePropertyNamesForTransition.Add((GET_MEMBER_NAME_CHECKED(UUISelectable, NavigationPrevSpecific)));
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