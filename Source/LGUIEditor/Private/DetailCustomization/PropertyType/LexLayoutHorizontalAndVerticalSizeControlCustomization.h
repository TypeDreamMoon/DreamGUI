#pragma once
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Core/Components/LexLayoutHorizontalAndVertical.h"

#define LOCTEXT_NAMESPACE "LexLayoutHorizontalAndVerticalSizeControlCustomization"

class FLexLayoutHorizontalAndVerticalSizeControlCustomization : public IPropertyTypeCustomization
{
public:
	FLexLayoutHorizontalAndVerticalSizeControlCustomization(){}
	static TSharedRef<IPropertyTypeCustomization> MakeInstance()
	{
		return MakeShareable(new FLexLayoutHorizontalAndVerticalSizeControlCustomization());
	}
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override
	{
		auto Width_PH = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexLayoutHorizontalAndVerticalSizeControl, bWidth));
		auto Height_PH = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexLayoutHorizontalAndVerticalSizeControl, bHeight));
		HeaderRow
		.IsEnabled(TAttribute<bool>(PropertyHandle, &IPropertyHandle::IsEditable))
		.NameContent()
		[
			PropertyHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		[
			SNew(SBox)
			.VAlign(VAlign_Center)
			[
				SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				[
					SNew(SHorizontalBox)
					+SHorizontalBox::Slot()
					.AutoWidth()
					[
						Width_PH->CreatePropertyValueWidget()
					]
					+SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.Padding(2, 0, 0, 0)
					[
						Width_PH->CreatePropertyNameWidget()
					]
				]
				+SHorizontalBox::Slot()
				[
					SNew(SHorizontalBox)
					+SHorizontalBox::Slot()
					.AutoWidth()
					[
						Height_PH->CreatePropertyValueWidget()
					]
					+SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.Padding(2, 0, 0, 0)
					[
						Height_PH->CreatePropertyNameWidget()
					]
				]
			]
		];
	}
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override{}
};

#undef LOCTEXT_NAMESPACE