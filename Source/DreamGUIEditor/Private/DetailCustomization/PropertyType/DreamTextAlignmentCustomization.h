#pragma once
#include "DetailWidgetRow.h"
#include "Core/DreamUITextData.h"
#include "Widgets/Input/SSegmentedControl.h"

#define LOCTEXT_NAMESPACE "DreamTextAlignmentCustomization"

class FDreamTextAlignmentCustomization : public IPropertyTypeCustomization
{
private:
	bool HorV = true;
public:
	FDreamTextAlignmentCustomization(bool HorizontalOrVertical){HorV = HorizontalOrVertical;}
	static TSharedRef<IPropertyTypeCustomization> MakeInstance(bool HorizontalOrVertical)
	{
		return MakeShareable(new FDreamTextAlignmentCustomization(HorizontalOrVertical));
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
				SNew(SSegmentedControl<EDreamUITextParagraphHorizontalAlign>)
				.Value_Lambda([=]
				{
					uint8 Value;
					if (PropertyHandle->GetValue(Value) == FPropertyAccess::Success)
					{
						return EDreamUITextParagraphHorizontalAlign(Value);
					}
					return EDreamUITextParagraphHorizontalAlign::Center;
				})
				.OnValueChanged_Lambda([=](EDreamUITextParagraphHorizontalAlign NewValue)
				{
					PropertyHandle->SetValue((uint8)NewValue);
				})
				+ SSegmentedControl<EDreamUITextParagraphHorizontalAlign>::Slot(EDreamUITextParagraphHorizontalAlign::Left)
				.Icon(FAppStyle::GetBrush("HorizontalAlignment_Left"))
				.ToolTip(LOCTEXT("AlignTextLeft", "Align Text Left"))
				+ SSegmentedControl<EDreamUITextParagraphHorizontalAlign>::Slot(EDreamUITextParagraphHorizontalAlign::Center)
				.Icon(FAppStyle::GetBrush("HorizontalAlignment_Center"))
				.ToolTip(LOCTEXT("AlignTextCenter", "Align Text Center"))
				+ SSegmentedControl<EDreamUITextParagraphHorizontalAlign>::Slot(EDreamUITextParagraphHorizontalAlign::Right)
				.Icon(FAppStyle::GetBrush("HorizontalAlignment_Right"))
				.ToolTip(LOCTEXT("AlignTextRight", "Align Text Right"))
			);
		}
		else
		{
			Container->SetContent(
				SNew(SSegmentedControl<EDreamUITextParagraphVerticalAlign>)
				.Value_Lambda([=]
				{
					uint8 Value;
					if (PropertyHandle->GetValue(Value) == FPropertyAccess::Success)
					{
						return EDreamUITextParagraphVerticalAlign(Value);
					}
					return EDreamUITextParagraphVerticalAlign::Middle;
				})
				.OnValueChanged_Lambda([=](EDreamUITextParagraphVerticalAlign NewValue)
				{
					PropertyHandle->SetValue((uint8)NewValue);
				})
				+ SSegmentedControl<EDreamUITextParagraphVerticalAlign>::Slot(EDreamUITextParagraphVerticalAlign::Bottom)
				.Icon(FAppStyle::GetBrush("VerticalAlignment_Bottom"))
				.ToolTip(LOCTEXT("VAlignBottom", "Vertically Align Bottom"))
				+ SSegmentedControl<EDreamUITextParagraphVerticalAlign>::Slot(EDreamUITextParagraphVerticalAlign::Middle)
				.Icon(FAppStyle::GetBrush("VerticalAlignment_Center"))
				.ToolTip(LOCTEXT("VAlignMiddle", "Vertically Align Middle"))
				+ SSegmentedControl<EDreamUITextParagraphVerticalAlign>::Slot(EDreamUITextParagraphVerticalAlign::Top)
				.Icon(FAppStyle::GetBrush("VerticalAlignment_Top"))
				.ToolTip(LOCTEXT("VAlignTop", "Vertically Align Top"))
			);
		}
	}
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override{}
};
#undef LOCTEXT_NAMESPACE