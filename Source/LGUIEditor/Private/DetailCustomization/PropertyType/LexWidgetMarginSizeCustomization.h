#pragma once
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "LGUIEditorStyle.h"
#include "Core/Components/LexWidget.h"
#include "Widgets/Input/SSegmentedControl.h"

#define LOCTEXT_NAMESPACE "LexWidgetSizeCustomization"

class FLexWidgetMarginSizeCustomization : public IPropertyTypeCustomization
{
public:
	FLexWidgetMarginSizeCustomization()
	{
	}
	static TSharedRef<IPropertyTypeCustomization> MakeInstance()
	{
		return MakeShareable(new FLexWidgetMarginSizeCustomization());
	}
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override
	{
#if 0
		TArray<UObject*> OuterObjects;
		PropertyHandle->GetOuterObjects(OuterObjects);
		if(OuterObjects.Num() != 1)
		{
			return;
		}
		auto Widget = Cast<ULexWidget>(OuterObjects[0]);
		if (!Widget)return;
		
		auto Type_PH = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexWidgetMarginSize, Type));
		auto Value_PH = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexWidgetMarginSize, Value));
		auto Percent_PH = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexWidgetMarginSize, Percent));
		auto TypeEnumProperty = CastField<FEnumProperty>(Type_PH->GetProperty());
		HeaderRow
		.NameContent()
		[
			PropertyHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		[
			SNew(SBox)
			//.WidthOverride(1000)
			[
				SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				.FillWidth(0.5f)
				[
					SNew(SSegmentedControl<ELexWidgetMarginSizeType>)
					.Value_Lambda([=]
					{
						ELexWidgetMarginSizeType Value;
						if (Type_PH->GetValue(*(uint8*)&Value) == FPropertyAccess::Success)
						{
							return Value;
						}
						return ELexWidgetMarginSizeType::Fixed;
					})
					.OnValueChanged_Lambda([=](ELexWidgetMarginSizeType NewState)
					{
						Type_PH->SetValue((uint8)NewState);
					})
					+ SSegmentedControl<ELexWidgetMarginSizeType>::Slot(ELexWidgetMarginSizeType::Fixed)
					// .Icon(FLexUIEditorStyle::Get().GetBrush("WidgetSize_Off"))
					.Text(LOCTEXT("LexWidgetSize_Fixed", "*"))
					.ToolTip(TypeEnumProperty->GetEnum()->GetToolTipTextByIndex((int)ELexWidgetMarginSizeType::Fixed))
					+ SSegmentedControl<ELexWidgetMarginSizeType>::Slot(ELexWidgetMarginSizeType::PercentOfParent)
					.Icon(FLGUIEditorStyle::Get().GetBrush("WidgetSize_ExpandToParent"))
					.ToolTip(TypeEnumProperty->GetEnum()->GetToolTipTextByIndex((int)ELexWidgetMarginSizeType::PercentOfParent))
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
							auto SizeType = ELexWidgetMarginSizeType::Fixed;
							if (Type_PH->GetValue(*(uint8*)(&SizeType)) == FPropertyAccess::Success)
							{
								if (SizeType == ELexWidgetMarginSizeType::Fixed)
								{
									return EVisibility::Visible;
								}
							}
							return EVisibility::Collapsed;
						})
						.IsEnabled_Lambda([=]
						{
							auto SizeType = ELexWidgetMarginSizeType::Fixed;
							if (Type_PH->GetValue(*(uint8*)(&SizeType)) == FPropertyAccess::Success)
							{
								if (SizeType == ELexWidgetMarginSizeType::Fixed)
								{
									return true;
								}
							}
							return false;
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
							auto SizeType = ELexWidgetMarginSizeType::Fixed;
							if (Type_PH->GetValue(*(uint8*)(&SizeType)) == FPropertyAccess::Success)
							{
								if (SizeType == ELexWidgetMarginSizeType::PercentOfParent)
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
#endif
	}
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override{}
};

#undef LOCTEXT_NAMESPACE