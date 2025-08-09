#pragma once
#include "DetailWidgetRow.h"
#include "LGUIEditorStyle.h"
#include "Widgets/Input/SSegmentedControl.h"

#define LOCTEXT_NAMESPACE "LexWidgetAspectRatioFitTypeCustomization"

class FLexWidgetAspectRatioCustomization : public IPropertyTypeCustomization
{
public:
	FLexWidgetAspectRatioCustomization(){}
	static TSharedRef<IPropertyTypeCustomization> MakeInstance()
	{
		return MakeShareable(new FLexWidgetAspectRatioCustomization());
	}
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override
	{
#if 0
		auto Type_PH = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexWidgetAspectRatio, Type));
		auto Value_PH = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexWidgetAspectRatio, Value));
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
					SNew(SSegmentedControl<ELexWidgetAspectRatioType>)
					.Value_Lambda([=]
					{
						uint8 Value;
						if (Type_PH->GetValue(Value) == FPropertyAccess::Success)
						{
							return ELexWidgetAspectRatioType(Value);
						}
						return ELexWidgetAspectRatioType::None;
					})
					.OnValueChanged_Lambda([=](ELexWidgetAspectRatioType NewValue)
					{
						Type_PH->SetValue((uint8)NewValue);
					})
					+ SSegmentedControl<ELexWidgetAspectRatioType>::Slot(ELexWidgetAspectRatioType::None)
					.Icon(FLGUIEditorStyle::Get().GetBrush("WidgetSize_Off"))
					// .Text(LOCTEXT("AspectNone", "OFF"))
					.ToolTip(LOCTEXT("AspectNone_Tooltip", "Not control size by aspect ratio"))
					+ SSegmentedControl<ELexWidgetAspectRatioType>::Slot(ELexWidgetAspectRatioType::WidthControlHeight)
					.Icon(FAppStyle::GetBrush("HorizontalAlignment_Fill"))
					// .Text(LOCTEXT("AspectWidth", "H"))
					.ToolTip(LOCTEXT("AspectWidth_Tooltip", "Width control height"))
					+ SSegmentedControl<ELexWidgetAspectRatioType>::Slot(ELexWidgetAspectRatioType::HeightControlWidth)
					.Icon(FAppStyle::GetBrush("VerticalAlignment_Fill"))
					// .Text(LOCTEXT("AspectHeight", "V"))
					.ToolTip(LOCTEXT("AspectHeight_Tooltip", "Height control width"))
				]
				+SHorizontalBox::Slot()
				.FillWidth(0.5f)
				.Padding(2, 0, 0, 0)
				[
					SNew(SHorizontalBox)
					+SHorizontalBox::Slot()
					.FillWidth(0.5f)
					[
						SNew(SBox)
						.IsEnabled_Lambda([=]
						{
							ELexWidgetAspectRatioType Value;
							if (Type_PH->GetValue(*(uint8*)&Value) == FPropertyAccess::Success)
							{
								return Value != ELexWidgetAspectRatioType::None;
							}
							return false;
						})
						[
							Value_PH->CreatePropertyValueWidget()
						]
					]
					+SHorizontalBox::Slot()
					.FillWidth(0.5f)
				]
			]
		];
#endif
	}
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override{}
};

#undef LOCTEXT_NAMESPACE