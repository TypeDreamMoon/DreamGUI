#pragma once
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "LGUIEditorStyle.h"
#include "Core/LexWidgetTypes.h"
#include "Widgets/Input/SSegmentedControl.h"

#define LOCTEXT_NAMESPACE "LexWidgetLengthCustomization"

class FLexWidgetOffsetCustomization : public IPropertyTypeCustomization
{
public:
	FLexWidgetOffsetCustomization()
	{
	}
	static TSharedRef<IPropertyTypeCustomization> MakeInstance()
	{
		return MakeShareable(new FLexWidgetOffsetCustomization());
	}
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override
	{
		static FName MetaKey_Horizontal = FName("Horizontal");
		auto HorizontalOrVertical = PropertyHandle->HasMetaData(MetaKey_Horizontal);
		auto Type_PH = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexWidgetOffset, Type));
		auto Value_PH = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexWidgetOffset, Value));
		auto Percent_PH = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexWidgetOffset, Percent));
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
				.FillWidth(0.5f)
				[
					SNew(SHorizontalBox)
					+SHorizontalBox::Slot()
					.FillWidth(1)
					[
						SNew(SSegmentedControl<ELexWidgetOffsetType>)
						.Value_Lambda([=]
						{
							ELexWidgetOffsetType Value;
							if (Type_PH->GetValue(*(uint8*)&Value) == FPropertyAccess::Success)
							{
								return Value;
							}
							return ELexWidgetOffsetType::Fixed;
						})
						.OnValueChanged_Lambda([=](ELexWidgetOffsetType NewState)
						{
							Type_PH->SetValue((uint8)NewState);
						})
						+ SSegmentedControl<ELexWidgetOffsetType>::Slot(ELexWidgetOffsetType::Fixed)
						// .Icon(FLexUIEditorStyle::Get().GetBrush("WidgetSize_Off"))
						.Text(LOCTEXT("LexWidgetLength_Fixed", "*"))
						.ToolTip(TypeEnumProperty->GetEnum()->GetToolTipTextByIndex((int)ELexWidgetOffsetType::Fixed))
						+ SSegmentedControl<ELexWidgetOffsetType>::Slot(ELexWidgetOffsetType::RelativeToParentSize)
						.Icon(FLGUIEditorStyle::Get().GetBrush(HorizontalOrVertical ? "WidgetSize_ExpandToParent" : "WidgetSize_ExpandToParent_V"))
						.ToolTip(TypeEnumProperty->GetEnum()->GetToolTipTextByIndex((int)ELexWidgetOffsetType::RelativeToParentSize))
						+ SSegmentedControl<ELexWidgetOffsetType>::Slot(ELexWidgetOffsetType::RelativeToSelfSize)
						.Icon(FLGUIEditorStyle::Get().GetBrush(HorizontalOrVertical ? "WidgetSize_ShrinkToChildren" : "WidgetSize_ShrinkToChildren_V"))
						.ToolTip(TypeEnumProperty->GetEnum()->GetToolTipTextByIndex((int)ELexWidgetOffsetType::RelativeToSelfSize))
					]
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
						.Visibility_Lambda([=]
						{
							ELexWidgetOffsetType SizeType = ELexWidgetOffsetType::Fixed;
							if (Type_PH->GetValue(*(uint8*)(&SizeType)) == FPropertyAccess::Success)
							{
								if (SizeType == ELexWidgetOffsetType::Fixed)
								{
									return EVisibility::Visible;
								}
							}
							return EVisibility::Collapsed;
						})
						.ToolTipText(Value_PH->GetToolTipText())
						[
							Value_PH->CreatePropertyValueWidget()
						]
					]
					+SHorizontalBox::Slot()
					.FillWidth(0.5f)
					[
						SNew(SBox)
						.Visibility_Lambda([=]
						{
							ELexWidgetOffsetType SizeType = ELexWidgetOffsetType::Fixed;
							if (Type_PH->GetValue(*(uint8*)(&SizeType)) == FPropertyAccess::Success)
							{
								if (SizeType == ELexWidgetOffsetType::RelativeToParentSize || SizeType == ELexWidgetOffsetType::RelativeToSelfSize)
								{
									return EVisibility::Visible;
								}
							}
							return EVisibility::Collapsed;
						})
						.ToolTipText(Percent_PH->GetToolTipText())
						[
							SNew(SHorizontalBox)
							+SHorizontalBox::Slot()
							.FillWidth(1.0f)
							[
								Percent_PH->CreatePropertyValueWidget(false)
							]
							+SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							.Padding(FMargin(2, 0))
							[
								SNew(STextBlock)
								.Font(IDetailLayoutBuilder::GetDetailFont())
								.Text(LOCTEXT("%", "%"))
							]
						]
					]
				]
			]
		];
	}
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override{}
};

#undef LOCTEXT_NAMESPACE