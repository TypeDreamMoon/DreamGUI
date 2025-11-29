#pragma once
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Core/Components/LexLayoutSelfFlexBox.h"
#include "Widgets/Input/SSegmentedControl.h"

#define LOCTEXT_NAMESPACE "LexLayoutSizeCustomization"

class FLexLayoutSizeCustomization : public IPropertyTypeCustomization
{
public:
	FLexLayoutSizeCustomization(){}
	static TSharedRef<IPropertyTypeCustomization> MakeInstance()
	{
		return MakeShareable(new FLexLayoutSizeCustomization());
	}
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override
	{
		TArray<UObject*> OuterObjects;
		PropertyHandle->GetOuterObjects(OuterObjects);
		if(OuterObjects.Num() != 1)
		{
			return;
		}
		
		auto Enabled_PH = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexLayoutSize, bEnable));
		auto Type_PH = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexLayoutSize, Type));
		auto AutoValue_PH = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexLayoutSize, AutoValue));
		auto PixelValue_PH = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexLayoutSize, FixedValue));
		auto Percent_PH = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexLayoutSize, PercentValue));
		auto TypeEnumProperty = CastField<FEnumProperty>(Type_PH->GetProperty());
		HeaderRow
		.NameContent()
		[
			SNew(SBox)
			.WidthOverride(1000)
			[
				SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					PropertyHandle->CreatePropertyNameWidget()
				]
				+SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Right)
				[
					Enabled_PH->CreatePropertyValueWidget()
				]
			]
		]
		.ValueContent()
		[
			SNew(SBox)
			.WidthOverride(1000)
			.IsEnabled_Lambda([Enabled_PH]
			{
				bool Enabled = true;
				if (Enabled_PH->GetValue(Enabled) == FPropertyAccess::Success)
				{
					return Enabled;
				}
				return false;
			})
			[
				SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				.AutoWidth()
				.HAlign(HAlign_Left)
				[
					SNew(SSegmentedControl<ELexLayoutSizeType>)
					.Value_Lambda([=]
					{
						ELexLayoutSizeType Value;
						if (Type_PH->GetValue(*(uint8*)&Value) == FPropertyAccess::Success)
						{
							return Value;
						}
						return ELexLayoutSizeType::Fixed;
					})
					.OnValueChanged_Lambda([=](ELexLayoutSizeType NewState)
					{
						Type_PH->SetValue((uint8)NewState);
					})
					+ SSegmentedControl<ELexLayoutSizeType>::Slot(ELexLayoutSizeType::Auto)
					//.Icon(FLGUIEditorStyle::Get().GetBrush(HorizontalOrVertical ? "WidgetSize_ShrinkToChildren" : "WidgetSize_ShrinkToChildren_V"))
					.ToolTip(TypeEnumProperty->GetEnum()->GetToolTipTextByIndex((int)ELexLayoutSizeType::Auto))
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("LexLayoutSize_Auto", "A"))
						.Font(IDetailLayoutBuilder::GetDetailFont())
					]
					+ SSegmentedControl<ELexLayoutSizeType>::Slot(ELexLayoutSizeType::Fixed)
					// .Icon(FLGUIEditorStyle::Get().GetBrush("WidgetSize_Off"))
					.Text(LOCTEXT("LexLayoutSize_Fixed", "*"))
					.ToolTip(TypeEnumProperty->GetEnum()->GetToolTipTextByIndex((int)ELexLayoutSizeType::Fixed))
					+ SSegmentedControl<ELexLayoutSizeType>::Slot(ELexLayoutSizeType::Percent)
					//.Icon(FLGUIEditorStyle::Get().GetBrush(HorizontalOrVertical ? "WidgetSize_ExpandToParent" : "WidgetSize_ExpandToParent_V"))
					.Text(LOCTEXT("LexLayoutSize_Percent", "%"))
					.ToolTip(TypeEnumProperty->GetEnum()->GetToolTipTextByIndex((int)ELexLayoutSizeType::Percent))
				]
				+SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.Padding(2, 0, 0, 0)
				[
					SNew(SHorizontalBox)
					+SHorizontalBox::Slot()
					.FillWidth(0.5f)
					[
						SNew(SBox)
						.Visibility_Lambda([=]
						{
							ELexLayoutSizeType SizeType = ELexLayoutSizeType::Auto;
							if (Type_PH->GetValue(*(uint8*)(&SizeType)) == FPropertyAccess::Success)
							{
								if (SizeType == ELexLayoutSizeType::Auto)
								{
									return EVisibility::Visible;
								}
							}
							return EVisibility::Collapsed;
						})
						.IsEnabled_Lambda([=]
						{
							ELexLayoutSizeType SizeType = ELexLayoutSizeType::Auto;
							if (Type_PH->GetValue(*(uint8*)(&SizeType)) == FPropertyAccess::Success)
							{
								if (SizeType == ELexLayoutSizeType::Auto)
								{
									return true;
								}
							}
							return false;
						})
						.VAlign(VAlign_Center)
						[
							AutoValue_PH->CreatePropertyValueWidget()
						]
					]
					+SHorizontalBox::Slot()
					.FillWidth(0.5f)
					[
						SNew(SBox)
						.Visibility_Lambda([=]
						{
							ELexLayoutSizeType SizeType = ELexLayoutSizeType::Fixed;
							if (Type_PH->GetValue(*(uint8*)(&SizeType)) == FPropertyAccess::Success)
							{
								if (SizeType == ELexLayoutSizeType::Fixed)
								{
									return EVisibility::Visible;
								}
							}
							return EVisibility::Collapsed;
						})
						.IsEnabled_Lambda([=]
						{
							ELexLayoutSizeType SizeType = ELexLayoutSizeType::Fixed;
							if (Type_PH->GetValue(*(uint8*)(&SizeType)) == FPropertyAccess::Success)
							{
								if (SizeType == ELexLayoutSizeType::Fixed)
								{
									return true;
								}
							}
							return false;
						})
						.ToolTipText(PixelValue_PH->GetToolTipText())
						[
							SNew(SHorizontalBox)
							+SHorizontalBox::Slot()
							.FillWidth(1.0f)
							[
								PixelValue_PH->CreatePropertyValueWidget()
							]
							+SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							.Padding(FMargin(2, 0))
							[
								SNew(STextBlock)
								.Font(IDetailLayoutBuilder::GetDetailFont())
								.Text(LOCTEXT("px", "px"))
							]
						]
					]
					+SHorizontalBox::Slot()
					.FillWidth(0.5f)
					[
						SNew(SBox)
						.Visibility_Lambda([=]
						{
							ELexLayoutSizeType SizeType = ELexLayoutSizeType::Fixed;
							if (Type_PH->GetValue(*(uint8*)(&SizeType)) == FPropertyAccess::Success)
							{
								if (SizeType == ELexLayoutSizeType::Percent)
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
								Percent_PH->CreatePropertyValueWidget()
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