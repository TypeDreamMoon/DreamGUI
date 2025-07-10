#pragma once
#include "DetailWidgetRow.h"
#include "Core/Components/LexLayoutHorizontalAndVertical.h"

#define LOCTEXT_NAMESPACE "LexLayoutSpacingCustomization"

class FLexLayoutSpacingCustomization : public IPropertyTypeCustomization
{
public:
	FLexLayoutSpacingCustomization(){}
	static TSharedRef<IPropertyTypeCustomization> MakeInstance()
	{
		return MakeShareable(new FLexLayoutSpacingCustomization());
	}
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override
	{
		auto Type_PH = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexLayoutSpacing, Type));
		auto Value_PH = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexLayoutSpacing, Value));
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
				SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				.FillWidth(0.5f)
				[
					SNew(SBox)
					.IsEnabled_Lambda([=]
					{
						ELexLayoutSpacingType Value;
						if (Type_PH->GetValue(*(uint8*)&Value) == FPropertyAccess::Success)
						{
							return Value == ELexLayoutSpacingType::Fixed;
						}
						return false;
					})
					[
						Value_PH->CreatePropertyValueWidget()
					]
				]
				+SHorizontalBox::Slot()
				.FillWidth(0.5f)
				.Padding(2, 0, 0, 0)
				[
					SNew(SHorizontalBox)
					+SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(2, 0, 0, 0)
					[
						Type_PH->CreatePropertyValueWidget()
					]
					+SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(2, 0)
					.VAlign(VAlign_Center)
					[
						Type_PH->CreatePropertyNameWidget()
					]
				]
			]
		];
	}
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override{}
};

#undef LOCTEXT_NAMESPACE