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
			.ToolTip(LOCTEXT("None_Tooltip", "No style"))
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("None", "Off"))
				.Font(IDetailLayoutBuilder::GetDetailFont())
			]
			+ SSegmentedControl<ELexUITextFontStyle>::Slot(ELexUITextFontStyle::Bold)
			.ToolTip(LOCTEXT("Bold_Tooltip", "Bold"))
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("Bold", "B"))
				.Font(IDetailLayoutBuilder::GetDetailFont())
			]
			+ SSegmentedControl<ELexUITextFontStyle>::Slot(ELexUITextFontStyle::Italic)
			.ToolTip(LOCTEXT("Italic_Tooltip", "Italic"))
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("Italic", "I"))
				.Font(IDetailLayoutBuilder::GetDetailFont())
			]
			+ SSegmentedControl<ELexUITextFontStyle>::Slot(ELexUITextFontStyle::BoldAndItalic)
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