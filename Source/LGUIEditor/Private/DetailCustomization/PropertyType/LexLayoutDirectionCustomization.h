#pragma once
#include "DetailWidgetRow.h"
#include "LGUIEditorStyle.h"
#include "Core/Components/LexLayoutFlexBox.h"
#include "Widgets/Input/SSegmentedControl.h"

#define LOCTEXT_NAMESPACE "LexLayoutDirectionCustomization"

class FLexLayoutDirectionCustomization : public IPropertyTypeCustomization
{
public:
	FLexLayoutDirectionCustomization(){}
	static TSharedRef<IPropertyTypeCustomization> MakeInstance()
	{
		return MakeShareable(new FLexLayoutDirectionCustomization());
	}
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override
	{
		HeaderRow
		.IsEnabled(TAttribute<bool>(PropertyHandle, &IPropertyHandle::IsEditable))
		.NameContent()
		[
			PropertyHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		[
			SNew(SBox)
			.WidthOverride(1000)
			[
				SNew(SSegmentedControl<ELexLayoutDirection>)
				.Value_Lambda([=]
				{
					uint8 Value;
					if (PropertyHandle->GetValue(Value) == FPropertyAccess::Success)
					{
						return ELexLayoutDirection(Value);
					}
					return ELexLayoutDirection::Horizontal;
				})
				.OnValueChanged_Lambda([=](ELexLayoutDirection NewValue)
				{
					PropertyHandle->SetValue((uint8)NewValue);
				})
				+ SSegmentedControl<ELexLayoutDirection>::Slot(ELexLayoutDirection::Horizontal)
				.Icon(FLGUIEditorStyle::Get().GetBrush("LayoutDirection_Horizontal"))
				.ToolTip(LOCTEXT("LayoutDirectionHorizontal_Tooltip", "Horizontal"))
				+ SSegmentedControl<ELexLayoutDirection>::Slot(ELexLayoutDirection::HorizontalReverse)
				.Icon(FLGUIEditorStyle::Get().GetBrush("LayoutDirection_HorizontalReverse"))
				.ToolTip(LOCTEXT("LayoutDirectionHorizontalReverse_Tooltip", "Horizontal Reverse"))
				+ SSegmentedControl<ELexLayoutDirection>::Slot(ELexLayoutDirection::Vertical)
				.Icon(FLGUIEditorStyle::Get().GetBrush("LayoutDirection_Vertical"))
				.ToolTip(LOCTEXT("LayoutDirectionVertical_Tooltip", "Vertical"))
				+ SSegmentedControl<ELexLayoutDirection>::Slot(ELexLayoutDirection::VerticalReverse)
				.Icon(FLGUIEditorStyle::Get().GetBrush("LayoutDirection_VerticalReverse"))
				.ToolTip(LOCTEXT("LayoutDirectionVerticalReverse_Tooltip", "Vertical Reverse"))
			]
		];
	}
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override{}
};
#undef LOCTEXT_NAMESPACE