#pragma once
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Core/Components/LexLayoutSelfFlexBox.h"
#include "Core/Components/LexLayoutContainerFlexBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/SSegmentedControl.h"
#include "Widgets/Images/SImage.h"
#include "EditorStyleSet.h"

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

		// Determine if we should show the warning icon for parent-size-affected-by-children situation
		auto ShowWarning_Lambda = [OuterObjects, Type_PH, PropertyHandle]() -> EVisibility
		{
			// Check if current property's Type is Percent
			uint8 TypeValue = 0;
			if (Type_PH->GetValue(TypeValue) != FPropertyAccess::Success || (ELexLayoutSizeType)TypeValue != ELexLayoutSizeType::Percent)
			{
				return EVisibility::Collapsed;
			}

			auto ChildSelfFlexBox = Cast<ULexLayoutSelfFlexBox>(OuterObjects[0]);
			if (!ChildSelfFlexBox) return EVisibility::Collapsed;

			auto ChildWidget = ChildSelfFlexBox->GetWidget();
			if (!ChildWidget) return EVisibility::Collapsed;

			auto ParentWidget = ChildWidget->GetParent();
			if (!ParentWidget) return EVisibility::Collapsed;

			// Parent must have LexLayoutContainerFlexBox
			auto ParentLayoutContainer = Cast<ULexLayoutContainerFlexBox>(ParentWidget->GetLayoutContainer());
			if (!ParentLayoutContainer) return EVisibility::Collapsed;

			// Parent must have LexLayoutSelfFlexBox
			auto ParentLayoutSelf = Cast<ULexLayoutSelfFlexBox>(ParentWidget->GetLayoutSelf());
			if (!ParentLayoutSelf) return EVisibility::Collapsed;

			// Check if parent's corresponding Preferred is Auto
			FName PropName = PropertyHandle->GetProperty()->GetFName();
			if (PropName == ULexLayoutSelfFlexBox::GetPropertyName_PreferredWidth())
			{
				if (ParentLayoutSelf->GetPreferredWidth().bEnable == false) return EVisibility::Collapsed;
				if (ParentLayoutSelf->GetPreferredWidth().Type != ELexLayoutSizeType::Auto) return EVisibility::Collapsed;
			}
			else if (PropName == ULexLayoutSelfFlexBox::GetPropertyName_PreferredHeight())
			{
				if (ParentLayoutSelf->GetPreferredHeight().bEnable == false) return EVisibility::Collapsed;
				if (ParentLayoutSelf->GetPreferredHeight().Type != ELexLayoutSizeType::Auto) return EVisibility::Collapsed;
			}
			else
			{
				return EVisibility::Collapsed;
			}

			return EVisibility::Visible;
		};

		HeaderRow
		.NameContent()
		[
			SNew(SBox)
			.WidthOverride(1000)
			[
				SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					PropertyHandle->CreatePropertyNameWidget()
				]
				+SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(3, 0, 0, 0)
				[
					SNew(SImage)
					.Image(FAppStyle::Get().GetBrush("Icons.Warning"))
					.ColorAndOpacity(FLinearColor(1.0f, 0.6f, 0.0f))
					.ToolTipText(LOCTEXT("ParentSizeAffectedByChildren", "Parent size also affected by children, "))
					.Visibility_Lambda(ShowWarning_Lambda)
				]
				+SHorizontalBox::Slot()
				.FillWidth(1.0f)
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
								SNew(SNumericEntryBox<float>)
								.MinValue(0)
								.MaxValue(100)
								.AllowSpin(true)
								.MinSliderValue(0)
								.MaxSliderValue(100)
								.Font(IDetailLayoutBuilder::GetDetailFont())
								.OnValueChanged_Lambda([=](float Value)
								{
									Percent_PH->SetValue(Value * 0.01f);
								})
								.Value_Lambda([=]()
								{
									float Value = 0;
									if (Percent_PH->GetValue(Value) == FPropertyAccess::Success)
									{
										return Value * 100;
									}
									return Value;
								})
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