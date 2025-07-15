#pragma once
#include "DetailWidgetRow.h"
#include "Core/LexUITextData.h"
#include "Widgets/Input/SSegmentedControl.h"

#define LOCTEXT_NAMESPACE "LexTextAlignmentCustomization"

class FLexTextAlignmentCustomization : public IPropertyTypeCustomization
{
private:
	bool HorV = true;
public:
	FLexTextAlignmentCustomization(bool HorizontalOrVertical){HorV = HorizontalOrVertical;}
	static TSharedRef<IPropertyTypeCustomization> MakeInstance(bool HorizontalOrVertical)
	{
		return MakeShareable(new FLexTextAlignmentCustomization(HorizontalOrVertical));
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
		if (HorV)
		{
			Container->SetContent(
				SNew(SSegmentedControl<ELexUITextParagraphHorizontalAlign>)
				.Value_Lambda([=]
				{
					uint8 Value;
					if (PropertyHandle->GetValue(Value) == FPropertyAccess::Success)
					{
						return ELexUITextParagraphHorizontalAlign(Value);
					}
					return ELexUITextParagraphHorizontalAlign::Center;
				})
				.OnValueChanged_Lambda([=](ELexUITextParagraphHorizontalAlign NewValue)
				{
					PropertyHandle->SetValue((uint8)NewValue);
				})
				+ SSegmentedControl<ELexUITextParagraphHorizontalAlign>::Slot(ELexUITextParagraphHorizontalAlign::Left)
				.Icon(FAppStyle::GetBrush("HorizontalAlignment_Left"))
				.ToolTip(LOCTEXT("AlignTextLeft", "Align Text Left"))
				+ SSegmentedControl<ELexUITextParagraphHorizontalAlign>::Slot(ELexUITextParagraphHorizontalAlign::Center)
				.Icon(FAppStyle::GetBrush("HorizontalAlignment_Center"))
				.ToolTip(LOCTEXT("AlignTextCenter", "Align Text Center"))
				+ SSegmentedControl<ELexUITextParagraphHorizontalAlign>::Slot(ELexUITextParagraphHorizontalAlign::Right)
				.Icon(FAppStyle::GetBrush("HorizontalAlignment_Right"))
				.ToolTip(LOCTEXT("AlignTextRight", "Align Text Right"))
			);
		}
		else
		{
			Container->SetContent(
				SNew(SSegmentedControl<ELexUITextParagraphVerticalAlign>)
				.Value_Lambda([=]
				{
					uint8 Value;
					if (PropertyHandle->GetValue(Value) == FPropertyAccess::Success)
					{
						return ELexUITextParagraphVerticalAlign(Value);
					}
					return ELexUITextParagraphVerticalAlign::Middle;
				})
				.OnValueChanged_Lambda([=](ELexUITextParagraphVerticalAlign NewValue)
				{
					PropertyHandle->SetValue((uint8)NewValue);
				})
				+ SSegmentedControl<ELexUITextParagraphVerticalAlign>::Slot(ELexUITextParagraphVerticalAlign::Bottom)
				.Icon(FAppStyle::GetBrush("VerticalAlignment_Bottom"))
				.ToolTip(LOCTEXT("VAlignBottom", "Vertically Align Bottom"))
				+ SSegmentedControl<ELexUITextParagraphVerticalAlign>::Slot(ELexUITextParagraphVerticalAlign::Middle)
				.Icon(FAppStyle::GetBrush("VerticalAlignment_Center"))
				.ToolTip(LOCTEXT("VAlignMiddle", "Vertically Align Middle"))
				+ SSegmentedControl<ELexUITextParagraphVerticalAlign>::Slot(ELexUITextParagraphVerticalAlign::Top)
				.Icon(FAppStyle::GetBrush("VerticalAlignment_Top"))
				.ToolTip(LOCTEXT("VAlignTop", "Vertically Align Top"))
			);
		}
	}
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override{}
};
#undef LOCTEXT_NAMESPACE