// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DetailCustomization/UIToggleCustomization.h"
#include "DreamDetailsMultiSelect.h"
#include "DreamUIEditorUtils.h"
#include "Core/Components/DreamWidget.h"
#include "IDetailGroup.h"
#include "Interaction/UIToggleGroup.h"
#include "Interaction/UIToggle.h"
#include "DreamGUIEditorModule.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "IPropertyUtilities.h"
#include "Core/Components/DreamImage.h"

#define LOCTEXT_NAMESPACE "UIToggleCustomization"

TSharedRef<IDetailCustomization> FUIToggleCustomization::MakeInstance()
{
	return MakeShareable(new FUIToggleCustomization);
}
void FUIToggleCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> targetObjects;
	DetailBuilder.GetObjectsBeingCustomized(targetObjects);
	// An empty list is a real state, not a can't-happen: a details panel rebuilds while a selection
	// is being cleared, and a customization registered for a class is also constructed for rows the
	// panel has no object behind. Indexing [0] there reads off the end of an empty array.
	TargetScriptPtr = targetObjects.Num() > 0 ? Cast<UUIToggle>(targetObjects[0].Get()) : nullptr;
	if (TargetScriptPtr != nullptr)
	{
		
	}
	else
	{
		UE_LOG(DreamGUIEditor, Log, TEXT("[%s].%d Get TargetScript is null"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
	}

	// The refresh these delegates ask for destroys the layout builder, so they keep the utilities instead.
	const TWeakPtr<IPropertyUtilities> WeakUtilities = DetailBuilder.GetPropertyUtilities();

	IDetailCategoryBuilder& category = DetailBuilder.EditCategory("DreamGUI-Toggle");
	auto ToggleTransition_PH = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUIToggle, ToggleTransitionType));
	ToggleTransition_PH->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FUIToggleCustomization::ForceRefresh, WeakUtilities));

	auto ToggleTransitionTarget_PH = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUIToggle, ToggleTransitionTarget));
	ToggleTransitionTarget_PH->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FUIToggleCustomization::ForceRefresh, WeakUtilities));
	UDreamVisual* ToggleTransitionTarget_Visual = nullptr;
	ToggleTransitionTarget_PH->GetValue(*(UObject**)&ToggleTransitionTarget_Visual);

	UUISelectableTransition* CustomTransition = nullptr;
	auto CustomTransition_PH = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUIToggle, CustomToggleTransition));
	CustomTransition_PH->GetValue(*(UObject**)&CustomTransition);

	// Every row of every transition is in the group; each shows for the transitions it applies to by its EditCondition.
	// Named after its header property, as a group with a header property is found by.
	IDetailGroup& TransitionGroup = category.AddGroup(GET_MEMBER_NAME_CHECKED(UUIToggle, ToggleTransitionType), LOCTEXT("Transition", "Transition"));
	TransitionGroup.HeaderProperty(ToggleTransition_PH);
	TransitionGroup.AddPropertyRow(ToggleTransitionTarget_PH);
	const uint8 TransitionType = DreamDetailsMultiSelect::ValueOr<uint8>(ToggleTransition_PH, MAX_uint8);
	if (TransitionType == (uint8)(EUISelectableTransitionType::ImageBrush) && ToggleTransitionTarget_Visual && !ToggleTransitionTarget_Visual->IsA<UDreamImage>())
	{
		TransitionGroup.AddWidgetRow()
			.FilterString(LOCTEXT("TransitionTarget_ImageBrush_Tip", "If use ImageBrush, Target must be DreamImage"))
			.ValueContent()
			.MinDesiredWidth(500)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Text(LOCTEXT("TransitionTarget_ImageBrush_Tip", "If use ImageBrush, Target must be DreamImage"))
				.Font(IDetailLayoutBuilder::GetDetailFont())
			];
	}
	TransitionGroup.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUIToggle, OffColor)));
	TransitionGroup.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUIToggle, OnColor)));
	TransitionGroup.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUIToggle, OffImageBrush)));
	TransitionGroup.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUIToggle, OnImageBrush)));
	TransitionGroup.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUIToggle, ToggleDuration)));
	TransitionGroup.AddPropertyRow(CustomTransition_PH);
}
void FUIToggleCustomization::ForceRefresh(TWeakPtr<IPropertyUtilities> InPropertyUtilities)
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