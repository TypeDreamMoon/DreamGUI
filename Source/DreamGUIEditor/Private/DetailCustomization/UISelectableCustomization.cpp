// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DetailCustomization/UISelectableCustomization.h"
#include "DreamUIEditorUtils.h"
#include "IDetailGroup.h"
#include "Interaction/UISelectable.h"
#include "DreamGUIEditorModule.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "IPropertyUtilities.h"
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
	// An empty list is a real state, not a can't-happen: a details panel rebuilds while a selection
	// is being cleared, and indexing [0] there reads off the end of an empty array. The null branch
	// below already handles "no target", so this only has to reach it.
	TargetScriptPtr = targetObjects.Num() > 0 ? Cast<UUISelectable>(targetObjects[0].Get()) : nullptr;
	if (TargetScriptPtr != nullptr)
	{
		
	}
	else
	{
		UE_LOG(DreamGUIEditor, Log, TEXT("[%s].%d Get TargetScript is null"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
	}

	FDreamUIEditorUtils::ShowError_MultiComponentNotAllowed(&DetailBuilder, TargetScriptPtr.Get(), LOCTEXT("MultipleUISelectableComponentError", "Multiple UISelectable component in one actor is not allowed!"));

	// The refresh these delegates ask for destroys the layout builder, so they keep the utilities instead.
	const TWeakPtr<IPropertyUtilities> WeakUtilities = DetailBuilder.GetPropertyUtilities();

	IDetailCategoryBuilder& category = DetailBuilder.EditCategory("DreamGUI-Selectable");
	auto Transition_PH = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUISelectable, TransitionType));
	Transition_PH->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FUISelectableCustomization::ForceRefresh, WeakUtilities));

	auto TransitionTarget_PH = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUISelectable, TransitionTarget));
	TransitionTarget_PH->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FUISelectableCustomization::ForceRefresh, WeakUtilities));

	UDreamVisual* TransitionTarget_Visual = nullptr;
	TransitionTarget_PH->GetValue(*(UObject**)&TransitionTarget_Visual);

	UUISelectableTransition* TargetTweenComp = nullptr;
	auto CustomTransition_PH = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUISelectable, CustomTransition));
	CustomTransition_PH->GetValue(*(UObject**)&TargetTweenComp);

	// Every row of every transition is in the group, in this order; each shows for the transitions it applies to by its
	// EditCondition, so the panel need not be rebuilt to show another transition's rows.
	// Named after its header property, as a group with a header property is found by.
	IDetailGroup& TransitionGroup = category.AddGroup(GET_MEMBER_NAME_CHECKED(UUISelectable, TransitionType), Transition_PH->GetPropertyDisplayName());
	TransitionGroup.HeaderProperty(Transition_PH);
	TransitionGroup.AddPropertyRow(TransitionTarget_PH);
	uint8 TransitionType = (uint8)(EUISelectableTransitionType::None);
	if (Transition_PH->GetValue(TransitionType) == FPropertyAccess::Success && TransitionType == (uint8)(EUISelectableTransitionType::ImageBrush)
		&& TransitionTarget_Visual && !TransitionTarget_Visual->IsA<UDreamImage>())
	{
		TransitionGroup.AddWidgetRow()
			.FilterString(LOCTEXT("TransitionTarget_ImageBrush_Tip", "If use ImageBrush, Target must be a DreamImage"))
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
	// Focus sits after the four states it is an alternative to, behind the switch that turns it on: most controls never want
	// a look of their own for it. The switch still matters with a custom transition: it decides between OnFocused and
	// OnHovered, and a transition written before focus existed only implements the latter.
	for (const FName Name : {
		GET_MEMBER_NAME_CHECKED(UUISelectable, NormalColor), GET_MEMBER_NAME_CHECKED(UUISelectable, HoveredColor),
		GET_MEMBER_NAME_CHECKED(UUISelectable, PressedColor), GET_MEMBER_NAME_CHECKED(UUISelectable, DisabledColor),
		GET_MEMBER_NAME_CHECKED(UUISelectable, NormalImageBrush), GET_MEMBER_NAME_CHECKED(UUISelectable, HoveredImageBrush),
		GET_MEMBER_NAME_CHECKED(UUISelectable, PressedImageBrush), GET_MEMBER_NAME_CHECKED(UUISelectable, DisabledImageBrush),
		GET_MEMBER_NAME_CHECKED(UUISelectable, CustomTransition), GET_MEMBER_NAME_CHECKED(UUISelectable, bUseFocusedVisuals),
		GET_MEMBER_NAME_CHECKED(UUISelectable, FocusedColor), GET_MEMBER_NAME_CHECKED(UUISelectable, FocusedImageBrush),
		GET_MEMBER_NAME_CHECKED(UUISelectable, AnimDuration) })
	{
		TransitionGroup.AddPropertyRow(DetailBuilder.GetProperty(Name));
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
	navigationLeftHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FUISelectableCustomization::ForceRefresh, WeakUtilities));
	auto navigationRightValue = UISelectableCustomizationLocal::ReadNavigationMode(navigationRightHandle);
	navigationRightHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FUISelectableCustomization::ForceRefresh, WeakUtilities));
	auto navigationUpValue = UISelectableCustomizationLocal::ReadNavigationMode(navigationUpHandle);
	navigationUpHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FUISelectableCustomization::ForceRefresh, WeakUtilities));
	auto navigationDownValue = UISelectableCustomizationLocal::ReadNavigationMode(navigationDownHandle);
	navigationDownHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FUISelectableCustomization::ForceRefresh, WeakUtilities));

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
	navigationNextHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FUISelectableCustomization::ForceRefresh, WeakUtilities));
	auto navigationPrevValue = UISelectableCustomizationLocal::ReadNavigationMode(navigationPrevHandle);
	navigationPrevHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FUISelectableCustomization::ForceRefresh, WeakUtilities));
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
	
	// A direction's explicit target shows, indented under it, while the direction is Explicit; its EditCondition hides it
	// otherwise.
}
void FUISelectableCustomization::ForceRefresh(TWeakPtr<IPropertyUtilities> InPropertyUtilities)
{
	if (TargetScriptPtr.IsValid())
	{
		if (const TSharedPtr<IPropertyUtilities> PropertyUtilities = InPropertyUtilities.Pin())
		{
			// Requested, not forced: this runs inside the property's own value-changed broadcast, and
			// the immediate variant tears the layout down while that broadcast is still on the stack.
			PropertyUtilities->RequestForceRefresh();
		}
	}
}
#undef LOCTEXT_NAMESPACE