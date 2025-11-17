#pragma once
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Core/Components/LexLayoutFlexBoxSelf.h"
#include "Widgets/Input/SSegmentedControl.h"

#define LOCTEXT_NAMESPACE "LexLayoutMinMaxSizeCustomization"

class FLexLayoutMinMaxSizeCustomization : public IPropertyTypeCustomization
{
public:
	FLexLayoutMinMaxSizeCustomization(){}
	static TSharedRef<IPropertyTypeCustomization> MakeInstance()
	{
		return MakeShareable(new FLexLayoutMinMaxSizeCustomization());
	}
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override
	{
		TArray<UObject*> OuterObjects;
		PropertyHandle->GetOuterObjects(OuterObjects);
		if(OuterObjects.Num() != 1)
		{
			return;
		}
		
		auto Enabled_PH = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexLayoutMinMaxSize, bEnable));
		auto Type_PH = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexLayoutMinMaxSize, Type));
		auto PixelValue_PH = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexLayoutMinMaxSize, FixedValue));
		auto Percent_PH = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexLayoutMinMaxSize, PercentValue));
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
					SNew(SSegmentedControl<ELexLayoutMinMaxSizeType>)
					.Value_Lambda([=]
					{
						ELexLayoutMinMaxSizeType Value;
						if (Type_PH->GetValue(*(uint8*)&Value) == FPropertyAccess::Success)
						{
							return Value;
						}
						return ELexLayoutMinMaxSizeType::Fixed;
					})
					.OnValueChanged_Lambda([=](ELexLayoutMinMaxSizeType NewState)
					{
						Type_PH->SetValue((uint8)NewState);
					})
					+ SSegmentedControl<ELexLayoutMinMaxSizeType>::Slot(ELexLayoutMinMaxSizeType::Fixed)
					// .Icon(FLGUIEditorStyle::Get().GetBrush("WidgetSize_Off"))
					.Text(LOCTEXT("LexLayoutMinMaxSize_Fixed", "*"))
					.ToolTip(TypeEnumProperty->GetEnum()->GetToolTipTextByIndex((int)ELexLayoutMinMaxSizeType::Fixed))
					+ SSegmentedControl<ELexLayoutMinMaxSizeType>::Slot(ELexLayoutMinMaxSizeType::Percent)
					//.Icon(FLGUIEditorStyle::Get().GetBrush(HorizontalOrVertical ? "WidgetSize_ExpandToParent" : "WidgetSize_ExpandToParent_V"))
					.Text(LOCTEXT("LexLayoutMinMaxSize_Percent", "%"))
					.ToolTip(TypeEnumProperty->GetEnum()->GetToolTipTextByIndex((int)ELexLayoutMinMaxSizeType::Percent))
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
							ELexLayoutMinMaxSizeType SizeType = ELexLayoutMinMaxSizeType::Fixed;
							if (Type_PH->GetValue(*(uint8*)(&SizeType)) == FPropertyAccess::Success)
							{
								if (SizeType == ELexLayoutMinMaxSizeType::Fixed)
								{
									return EVisibility::Visible;
								}
							}
							return EVisibility::Collapsed;
						})
						.IsEnabled_Lambda([=]
						{
							ELexLayoutMinMaxSizeType SizeType = ELexLayoutMinMaxSizeType::Fixed;
							if (Type_PH->GetValue(*(uint8*)(&SizeType)) == FPropertyAccess::Success)
							{
								if (SizeType == ELexLayoutMinMaxSizeType::Fixed)
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
							ELexLayoutMinMaxSizeType SizeType = ELexLayoutMinMaxSizeType::Fixed;
							if (Type_PH->GetValue(*(uint8*)(&SizeType)) == FPropertyAccess::Success)
							{
								if (SizeType == ELexLayoutMinMaxSizeType::Percent)
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