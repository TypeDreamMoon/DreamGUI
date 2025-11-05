#pragma once
#include "DetailWidgetRow.h"
#include "Core/LexUITextData.h"
#include "Widgets/Input/SSegmentedControl.h"

#define LOCTEXT_NAMESPACE "LexTextFontStyleCustomization"

class FLexTextFontStyleCustomization : public IPropertyTypeCustomization
{
public:
	FLexTextFontStyleCustomization(){}
	static TSharedRef<IPropertyTypeCustomization> MakeInstance()
	{
		return MakeShareable(new FLexTextFontStyleCustomization());
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
			SNew(SSegmentedControl<ELexUITextFontStyle>)
			.Value_Lambda([=]
			{
				uint8 Value;
				if (PropertyHandle->GetValue(Value) == FPropertyAccess::Success)
				{
					return (ELexUITextFontStyle)Value;
				}
				return ELexUITextFontStyle::None;
			})
			.OnValueChanged_Lambda([=](ELexUITextFontStyle NewValue)
			{
				PropertyHandle->SetValue((uint8)NewValue);
			})
			+ SSegmentedControl<ELexUITextFontStyle>::Slot(ELexUITextFontStyle::None)
			.Text(LOCTEXT("None", "Off"))
			.ToolTip(LOCTEXT("None_Tooltip", "No style"))
			+ SSegmentedControl<ELexUITextFontStyle>::Slot(ELexUITextFontStyle::Bold)
			.Text(LOCTEXT("Bold", "B"))
			.ToolTip(LOCTEXT("Bold_Tooltip", "Bold"))
			+ SSegmentedControl<ELexUITextFontStyle>::Slot(ELexUITextFontStyle::Italic)
			.Text(LOCTEXT("Italic", "I"))
			.ToolTip(LOCTEXT("Italic_Tooltip", "Italic"))
			+ SSegmentedControl<ELexUITextFontStyle>::Slot(ELexUITextFontStyle::BoldAndItalic)
			.Text(LOCTEXT("Bold&Italic", "B&I"))
			.ToolTip(LOCTEXT("Bold&Italic_Tooltip", "Bold and Italic"))
		);
	}
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override{}
};
#undef LOCTEXT_NAMESPACE