#pragma once
#include "DetailWidgetRow.h"
#include "Core/Components/LexLayoutFlexBox.h"
#include "Widgets/Input/SSegmentedControl.h"

#define LOCTEXT_NAMESPACE "LexLayoutHorizontalAlignment"

class FLexLayoutHorizontalAlignmentCustomization : public IPropertyTypeCustomization
{
public:
	FLexLayoutHorizontalAlignmentCustomization(){}
	static TSharedRef<IPropertyTypeCustomization> MakeInstance()
	{
		return MakeShareable(new FLexLayoutHorizontalAlignmentCustomization());
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
			SNew(SSegmentedControl<ELexLayoutHorizontalAlignment>)
			.Value_Lambda([=]
			{
				uint8 Value;
				if (PropertyHandle->GetValue(Value) == FPropertyAccess::Success)
				{
					return ELexLayoutHorizontalAlignment(Value);
				}
				return ELexLayoutHorizontalAlignment::Left;
			})
			.OnValueChanged_Lambda([=](ELexLayoutHorizontalAlignment NewState)
			{
				PropertyHandle->SetValue((uint8)NewState);
			})
			+ SSegmentedControl<ELexLayoutHorizontalAlignment>::Slot(ELexLayoutHorizontalAlignment::Left)
			.Icon(FAppStyle::GetBrush("HorizontalAlignment_Left"))
			.ToolTip(LOCTEXT("AlignLeft", "Align Left"))
			+ SSegmentedControl<ELexLayoutHorizontalAlignment>::Slot(ELexLayoutHorizontalAlignment::Center)
			.Icon(FAppStyle::GetBrush("HorizontalAlignment_Center"))
			.ToolTip(LOCTEXT("AlignCenter", "Align Center"))
			+ SSegmentedControl<ELexLayoutHorizontalAlignment>::Slot(ELexLayoutHorizontalAlignment::Right)
			.Icon(FAppStyle::GetBrush("HorizontalAlignment_Right"))
			.ToolTip(LOCTEXT("AlignRight", "Align Right"))
		];
	}
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override{}
};
#undef LOCTEXT_NAMESPACE

#define LOCTEXT_NAMESPACE "LexLayoutVerticalAlignment"

class FLexLayoutVerticalAlignmentCustomization : public IPropertyTypeCustomization
{
public:
	FLexLayoutVerticalAlignmentCustomization(){}
	static TSharedRef<IPropertyTypeCustomization> MakeInstance()
	{
		return MakeShareable(new FLexLayoutVerticalAlignmentCustomization());
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
			SNew(SSegmentedControl<ELexLayoutVerticalAlignment>)
			.Value_Lambda([=]
			{
				uint8 Value;
				if (PropertyHandle->GetValue(Value) == FPropertyAccess::Success)
				{
					return ELexLayoutVerticalAlignment(Value);
				}
				return ELexLayoutVerticalAlignment::Bottom;
			})
			.OnValueChanged_Lambda([=](ELexLayoutVerticalAlignment NewState)
			{
				PropertyHandle->SetValue((uint8)NewState);
			})
			+ SSegmentedControl<ELexLayoutVerticalAlignment>::Slot(ELexLayoutVerticalAlignment::Bottom)
			.Icon(FAppStyle::GetBrush("VerticalAlignment_Bottom"))
			.ToolTip(LOCTEXT("AlignBottom", "Align Bottom"))
			+ SSegmentedControl<ELexLayoutVerticalAlignment>::Slot(ELexLayoutVerticalAlignment::Middle)
			.Icon(FAppStyle::GetBrush("VerticalAlignment_Center"))
			.ToolTip(LOCTEXT("AlignCenter", "Align Center"))
			+ SSegmentedControl<ELexLayoutVerticalAlignment>::Slot(ELexLayoutVerticalAlignment::Top)
			.Icon(FAppStyle::GetBrush("VerticalAlignment_Top"))
			.ToolTip(LOCTEXT("AlignTop", "Align Top"))
		];
	}
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override{}
};
#undef LOCTEXT_NAMESPACE