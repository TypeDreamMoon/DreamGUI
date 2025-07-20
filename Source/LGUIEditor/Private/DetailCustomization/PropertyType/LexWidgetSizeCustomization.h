#pragma once
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "LGUIEditorStyle.h"
#include "Widgets/Input/SSegmentedControl.h"

#define LOCTEXT_NAMESPACE "LexWidgetSizeCustomization"

class FLexWidgetSizeCustomization : public IPropertyTypeCustomization
{
	TWeakPtr<IPropertyHandle> AspectRatioType_PH;
public:
	FLexWidgetSizeCustomization(TSharedPtr<IPropertyHandle> InAspectRatioType_PH)
	{
		AspectRatioType_PH = InAspectRatioType_PH;
	}
	static TSharedRef<IPropertyTypeCustomization> MakeInstance(TSharedPtr<IPropertyHandle> InAspectRatioType_PH)
	{
		return MakeShareable(new FLexWidgetSizeCustomization(InAspectRatioType_PH));
	}
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override
	{
		TArray<UObject*> OuterObjects;
		PropertyHandle->GetOuterObjects(OuterObjects);
		if(OuterObjects.Num() != 1)
		{
			return;
		}
		auto Widget = Cast<ULexWidget>(OuterObjects[0]);
		if (!Widget)return;
		
		auto HorizontalOrVertical = PropertyHandle->GetProperty()->GetFName() == "Width";
		auto Type_PH = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexWidgetSize, Type));
		auto Value_PH = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexWidgetSize, Value));
		auto Percent_PH = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexWidgetSize, Percent));
		auto TypeEnumProperty = CastField<FEnumProperty>(Type_PH->GetProperty());
		auto SizeConflictWarningText = LOCTEXT("SizeConflict", "This size is conflict with parent, will fallback to use value size");
		HeaderRow
		.IsEnabled(TAttribute<bool>::CreateSPLambda(this, [=, this]
		{
			if (!PropertyHandle->IsEditable())return false;
			if (!AspectRatioType_PH.IsValid())return false;
			auto AspectRatioFitType = ELexWidgetAspectRatioType::None;
			if (AspectRatioType_PH.Pin()->GetValue(*(uint8*)&AspectRatioFitType) == FPropertyAccess::Success)
			{
				if (AspectRatioFitType == ELexWidgetAspectRatioType::HeightControlWidth)
				{
					if (HorizontalOrVertical)
					{
						return false;
					}
				}
				else if (AspectRatioFitType == ELexWidgetAspectRatioType::WidthControlHeight)
				{
					if (!HorizontalOrVertical)
					{
						return false;
					}
				}
			}
			return true;
		}))
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
					.AutoWidth()
					.Padding(FMargin(2, 4))
					[
						SNew(SImage)
						.Visibility_Lambda([=, this]
						{
							ELexWidgetSizeType Type;
							if (Type_PH->GetValue(*(uint8*)(&Type)) == FPropertyAccess::Success)
							{
								if (Type == ELexWidgetSizeType::ExpandToParent)
								{
									if (auto Parent = Widget->GetUIParent())
									{
										if (HorizontalOrVertical)
										{
											if (Parent->GetWidth().Type == ELexWidgetSizeType::ShrinkToChildren)
											{
												return EVisibility::Visible;
											}
										}
										else
										{
											if (Parent->GetHeight().Type == ELexWidgetSizeType::ShrinkToChildren)
											{
												return EVisibility::Visible;
											}
										}
									}
								}
							}
							return EVisibility::Collapsed;
						})
						.Image(FAppStyle::Get().GetBrush("Icons.Warning"))
						.ColorAndOpacity(FAppStyle::Get().GetSlateColor("Colors.AccentYellow"))
						.ToolTipText(SizeConflictWarningText)
					]
					+SHorizontalBox::Slot()
					.FillWidth(1)
					[
						SNew(SSegmentedControl<ELexWidgetSizeType>)
						.Value_Lambda([=]
						{
							ELexWidgetSizeType Value;
							if (Type_PH->GetValue(*(uint8*)&Value) == FPropertyAccess::Success)
							{
								return Value;
							}
							return ELexWidgetSizeType::Fixed;
						})
						.OnValueChanged_Lambda([=](ELexWidgetSizeType NewState)
						{
							Type_PH->SetValue((uint8)NewState);
						})
						+ SSegmentedControl<ELexWidgetSizeType>::Slot(ELexWidgetSizeType::Fixed)
						// .Icon(FLexUIEditorStyle::Get().GetBrush("WidgetSize_Off"))
						.Text(LOCTEXT("LexWidgetSize_Fixed", "*"))
						.ToolTip(TypeEnumProperty->GetEnum()->GetToolTipTextByIndex((int)ELexWidgetSizeType::Fixed))
						+ SSegmentedControl<ELexWidgetSizeType>::Slot(ELexWidgetSizeType::ExpandToParent)
						.Icon(FLGUIEditorStyle::Get().GetBrush(HorizontalOrVertical ? "WidgetSize_ExpandToParent" : "WidgetSize_ExpandToParent_V"))
						.ToolTip(TypeEnumProperty->GetEnum()->GetToolTipTextByIndex((int)ELexWidgetSizeType::ExpandToParent))
						+ SSegmentedControl<ELexWidgetSizeType>::Slot(ELexWidgetSizeType::ShrinkToChildren)
						.Icon(FLGUIEditorStyle::Get().GetBrush(HorizontalOrVertical ? "WidgetSize_ShrinkToChildren" : "WidgetSize_ShrinkToChildren_V"))
						.ToolTip(TypeEnumProperty->GetEnum()->GetToolTipTextByIndex((int)ELexWidgetSizeType::ShrinkToChildren))
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
							auto SizeType = ELexWidgetSizeType::Fixed;
							if (Type_PH->GetValue(*(uint8*)(&SizeType)) == FPropertyAccess::Success)
							{
								if (SizeType == ELexWidgetSizeType::Fixed || SizeType == ELexWidgetSizeType::ShrinkToChildren)
								{
									return EVisibility::Visible;
								}
							}
							return EVisibility::Collapsed;
						})
						.IsEnabled_Lambda([=]
						{
							auto SizeType = ELexWidgetSizeType::Fixed;
							if (Type_PH->GetValue(*(uint8*)(&SizeType)) == FPropertyAccess::Success)
							{
								if (SizeType == ELexWidgetSizeType::Fixed)
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
							auto SizeType = ELexWidgetSizeType::Fixed;
							if (Type_PH->GetValue(*(uint8*)(&SizeType)) == FPropertyAccess::Success)
							{
								if (SizeType == ELexWidgetSizeType::ExpandToParent)
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