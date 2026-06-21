#pragma once
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Core/Components/LexLayoutSelfFlexBox.h"
#include "Core/Components/LexLayoutContainerFlexBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/SSegmentedControl.h"
#include "Widgets/Images/SImage.h"
#include "EditorStyleSet.h"

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

		// Determine if we should show the warning icon for parent-size-affected-by-children situation
		auto ShowWarning_Lambda = [OuterObjects, Type_PH, PropertyHandle]() -> EVisibility
		{
			// Check if current property's Type is Percent
			uint8 TypeValue = 0;
			if (Type_PH->GetValue(TypeValue) != FPropertyAccess::Success || (ELexLayoutMinMaxSizeType)TypeValue != ELexLayoutMinMaxSizeType::Percent)
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
			if (PropName == ULexLayoutSelfFlexBox::GetPropertyName_MinWidth() || PropName == ULexLayoutSelfFlexBox::GetPropertyName_MaxWidth())
			{
				if (ParentLayoutSelf->GetPreferredWidth().Type != ELexLayoutSizeType::Auto) return EVisibility::Collapsed;
			}
			else if (PropName == ULexLayoutSelfFlexBox::GetPropertyName_MinHeight() || PropName == ULexLayoutSelfFlexBox::GetPropertyName_MaxHeight())
			{
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
								SNew(SNumericEntryBox<float>)
								.MinValue(0)
								.MaxValue(100)
								.AllowSpin(true)
								.MinSliderValue(0)
								.MaxSliderValue(100)
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