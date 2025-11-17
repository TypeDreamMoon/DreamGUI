#pragma once
#include "DetailWidgetRow.h"
#include "LGUIEditorStyle.h"
#include "Core/Components/LexLayoutFlexBoxSelf.h"
#include "Widgets/Input/SSegmentedControl.h"

#define LOCTEXT_NAMESPACE "LexLayoutAspectRatioCustomization"

class FLexLayoutAspectRatioCustomization : public IPropertyTypeCustomization
{
private:
	TSharedPtr<SCheckBox> AspectRatioCheckBox;
public:
	FLexLayoutAspectRatioCustomization(){}
	static TSharedRef<IPropertyTypeCustomization> MakeInstance()
	{
		return MakeShareable(new FLexLayoutAspectRatioCustomization());
	}
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override
	{
		auto Type_PH = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexLayoutAspectRatio, Type));
		auto Value_PH = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexLayoutAspectRatio, Value));
		auto TypeEnumProperty = CastField<FEnumProperty>(Type_PH->GetProperty());
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
				.AutoWidth()
				[
					SNew(SHorizontalBox)
					+SHorizontalBox::Slot()
					.VAlign(VAlign_Center)
					.AutoWidth()
					.Padding(FMargin(5, 0))
					[
						SAssignNew(AspectRatioCheckBox, SCheckBox)
						.Style(FAppStyle::Get(), "TransparentCheckBox")
						.IsEnabled_Lambda([Type_PH]()
						{
							ELexLayoutAspectRatioType Value;
							if (Type_PH->GetValue(*(uint8*)&Value) == FPropertyAccess::Success)
							{
								return Value != ELexLayoutAspectRatioType::None;
							}
							return true;
						})
						.ToolTipText(TypeEnumProperty->GetEnum()->GetToolTipTextByIndex((int)ELexLayoutAspectRatioType::None))
						.OnCheckStateChanged_Lambda([Type_PH](ECheckBoxState CheckType)
						{
							if (CheckType == ECheckBoxState::Checked)
							{
								Type_PH->SetValue((uint8)ELexLayoutAspectRatioType::WidthControlHeight);
							}
							else
							{
								Type_PH->SetValue((uint8)ELexLayoutAspectRatioType::None);
							}
						})
						.IsChecked_Lambda([Type_PH]()
						{
							ELexLayoutAspectRatioType Value;
							if (Type_PH->GetValue(*(uint8*)&Value) == FPropertyAccess::Success)
							{
								return Value == ELexLayoutAspectRatioType::None ? ECheckBoxState::Unchecked : ECheckBoxState::Checked;
							}
							return ECheckBoxState::Undetermined;
						})
						[
							SNew(SImage)
							.ColorAndOpacity(FSlateColor::UseForeground())
							.Image_Lambda([Type_PH]()
							{
								ELexLayoutAspectRatioType Value;
								if (Type_PH->GetValue(*(uint8*)&Value) == FPropertyAccess::Success)
								{
									return Value == ELexLayoutAspectRatioType::None ? FAppStyle::Get().GetBrush("Icons.Unlock") : FAppStyle::Get().GetBrush("Icons.Lock");
								}
								return FAppStyle::Get().GetBrush("Icons.Unlock");
							})
						]
					]
					+SHorizontalBox::Slot()
					.VAlign(VAlign_Center)
					.FillWidth(1)
					[
						SNew(SSegmentedControl<ELexLayoutAspectRatioType>)
						.Value_Lambda([=]
						{
							uint8 Value;
							if (Type_PH->GetValue(Value) == FPropertyAccess::Success)
							{
								return ELexLayoutAspectRatioType(Value);
							}
							return ELexLayoutAspectRatioType::None;
						})
						.OnValueChanged_Lambda([=](ELexLayoutAspectRatioType NewValue)
						{
							Type_PH->SetValue((uint8)NewValue);
						})
						// + SSegmentedControl<ELexLayoutAspectRatioType>::Slot(ELexLayoutAspectRatioType::None)
						// .Text(LOCTEXT("AspectNone", "N"))
						// .ToolTip(TypeEnumProperty->GetEnum()->GetToolTipTextByIndex((int)ELexLayoutAspectRatioType::None))
						
						+ SSegmentedControl<ELexLayoutAspectRatioType>::Slot(ELexLayoutAspectRatioType::WidthControlHeight)
						// .Icon(FAppStyle::GetBrush("HorizontalAlignment_Fill"))
						//.Text(LOCTEXT("AspectWidth", "W"))
						.ToolTip(TypeEnumProperty->GetEnum()->GetToolTipTextByIndex((int)ELexLayoutAspectRatioType::WidthControlHeight))
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("AspectWidth", "W"))
							.Font(IDetailLayoutBuilder::GetDetailFont())
						]
						+ SSegmentedControl<ELexLayoutAspectRatioType>::Slot(ELexLayoutAspectRatioType::HeightControlWidth)
						// .Icon(FAppStyle::GetBrush("VerticalAlignment_Fill"))
						//.Text(LOCTEXT("AspectHeight", "H"))
						.ToolTip(TypeEnumProperty->GetEnum()->GetToolTipTextByIndex((int)ELexLayoutAspectRatioType::HeightControlWidth))
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("AspectHeight", "H"))
							.Font(IDetailLayoutBuilder::GetDetailFont())
						]
					]
				]
				+SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.Padding(2, 0, 0, 0)
				[
					SNew(SBox)
					.MinDesiredWidth(1000)
					.IsEnabled_Lambda([=]
					{
						ELexLayoutAspectRatioType Value;
						if (Type_PH->GetValue(*(uint8*)&Value) == FPropertyAccess::Success)
						{
							return Value != ELexLayoutAspectRatioType::None;
						}
						return false;
					})
					[
						Value_PH->CreatePropertyValueWidget()
					]
				]
			]
		];
	}
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override{}
};

#undef LOCTEXT_NAMESPACE