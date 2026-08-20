#pragma once
#include "DetailWidgetRow.h"
#include "Core/DreamUITextData.h"
#include "Widgets/Input/SSegmentedControl.h"

#define LOCTEXT_NAMESPACE "DreamTextFontStyleCustomization"

class FDreamTextFontStyleCustomization : public IPropertyTypeCustomization
{
public:
	FDreamTextFontStyleCustomization(){}
	static TSharedRef<IPropertyTypeCustomization> MakeInstance()
	{
		return MakeShareable(new FDreamTextFontStyleCustomization());
	}
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override
	{
		auto Container = SNew(SBox);
		HeaderRow
		.IsEnabled(TAttribute<bool>(PropertyHandle, &IPropertyHandle::IsEditable))
		.NameContent()
		[
			PropertyHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		[
			Container
		];

		Container->SetContent(
			SNew(SSegmentedControl<EDreamUITextFontStyle>)
			.Value_Lambda([=]
			{
				uint8 Value;
				if (PropertyHandle->GetValue(Value) == FPropertyAccess::Success)
				{
					return (EDreamUITextFontStyle)Value;
				}
				return EDreamUITextFontStyle::None;
			})
			.OnValueChanged_Lambda([=](EDreamUITextFontStyle NewValue)
			{
				PropertyHandle->SetValue((uint8)NewValue);
			})
			+ SSegmentedControl<EDreamUITextFontStyle>::Slot(EDreamUITextFontStyle::None)
			.ToolTip(LOCTEXT("None_Tooltip", "No style"))
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("None", "Off"))
				.Font(IDetailLayoutBuilder::GetDetailFont())
			]
			+ SSegmentedControl<EDreamUITextFontStyle>::Slot(EDreamUITextFontStyle::Bold)
			.ToolTip(LOCTEXT("Bold_Tooltip", "Bold"))
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("Bold", "B"))
				.Font(IDetailLayoutBuilder::GetDetailFont())
			]
			+ SSegmentedControl<EDreamUITextFontStyle>::Slot(EDreamUITextFontStyle::Italic)
			.ToolTip(LOCTEXT("Italic_Tooltip", "Italic"))
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("Italic", "I"))
				.Font(IDetailLayoutBuilder::GetDetailFont())
			]
			+ SSegmentedControl<EDreamUITextFontStyle>::Slot(EDreamUITextFontStyle::BoldAndItalic)
			.ToolTip(LOCTEXT("Bold&Italic_Tooltip", "Bold and Italic"))
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("Bold&Italic", "B&I"))
				.Font(IDetailLayoutBuilder::GetDetailFont())
			]
		);
	}
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override{}
};
#undef LOCTEXT_NAMESPACE