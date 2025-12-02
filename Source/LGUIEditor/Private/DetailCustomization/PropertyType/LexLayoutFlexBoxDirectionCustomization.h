#pragma once
#include "DetailWidgetRow.h"
#include "LGUIEditorStyle.h"
#include "Core/Components/LexLayoutContainerFlexBox.h"
#include "Widgets/Input/SSegmentedControl.h"

#define LOCTEXT_NAMESPACE "LexLayoutFlexBoxDirectionCustomization"

class FLexLayoutFlexBoxDirectionCustomization : public IPropertyTypeCustomization
{
public:
	FLexLayoutFlexBoxDirectionCustomization(){}
	static TSharedRef<IPropertyTypeCustomization> MakeInstance()
	{
		return MakeShareable(new FLexLayoutFlexBoxDirectionCustomization());
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
				SNew(SSegmentedControl<ELexLayoutFlexBoxDirectionType>)
				.Value_Lambda([=]
				{
					uint8 Value;
					if (PropertyHandle->GetValue(Value) == FPropertyAccess::Success)
					{
						return (ELexLayoutFlexBoxDirectionType)Value;
					}
					return ELexLayoutFlexBoxDirectionType::Horizontal;
				})
				.OnValueChanged_Lambda([=](ELexLayoutFlexBoxDirectionType NewValue)
				{
					PropertyHandle->SetValue((uint8)NewValue);
				})
				+ SSegmentedControl<ELexLayoutFlexBoxDirectionType>::Slot(ELexLayoutFlexBoxDirectionType::Horizontal)
				.Icon(FLGUIEditorStyle::Get().GetBrush("LayoutDirection_Horizontal"))
				.ToolTip(LOCTEXT("LayoutDirectionHorizontal_Tooltip", "Horizontal"))
				+ SSegmentedControl<ELexLayoutFlexBoxDirectionType>::Slot(ELexLayoutFlexBoxDirectionType::HorizontalReverse)
				.Icon(FLGUIEditorStyle::Get().GetBrush("LayoutDirection_HorizontalReverse"))
				.ToolTip(LOCTEXT("LayoutDirectionHorizontalReverse_Tooltip", "Horizontal Reverse"))
				+ SSegmentedControl<ELexLayoutFlexBoxDirectionType>::Slot(ELexLayoutFlexBoxDirectionType::Vertical)
				.Icon(FLGUIEditorStyle::Get().GetBrush("LayoutDirection_Vertical"))
				.ToolTip(LOCTEXT("LayoutDirectionVertical_Tooltip", "Vertical"))
				+ SSegmentedControl<ELexLayoutFlexBoxDirectionType>::Slot(ELexLayoutFlexBoxDirectionType::VerticalReverse)
				.Icon(FLGUIEditorStyle::Get().GetBrush("LayoutDirection_VerticalReverse"))
				.ToolTip(LOCTEXT("LayoutDirectionVerticalReverse_Tooltip", "Vertical Reverse"))
			]
		];
	}
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override{}
};
#undef LOCTEXT_NAMESPACE