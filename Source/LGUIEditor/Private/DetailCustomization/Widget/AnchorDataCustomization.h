#pragma once
#include "AnchorPreviewWidget.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IPropertyUtilities.h"
#include "Core/LexUITextData.h"
#include "Core/Components/LexCanvas.h"
#include "Core/Components/LexLayout.h"
#include "PrefabEditor/LGUIPrefabEditor.h"
#include "Utils/LexUIUtils.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/SSegmentedControl.h"

#define LOCTEXT_NAMESPACE "FAnchorDataCustomization"

class FAnchorDataCustomization : public IPropertyTypeCustomization
{
private:
	enum class EAnchorControlledByLayoutType
	{
		HorizontalAnchor,
		HorizontalAnchoredPosition,
		HorizontalSizeDelta,
		VerticalAnchor,
		VerticalAnchoredPosition,
		VerticalSizeDelta,
	};
	TArray<TWeakObjectPtr<class ULexWidget>> TargetScriptArray;
	static TArray<float> ValueRangeArray;
public:
	FAnchorDataCustomization(){}
	static TSharedRef<IPropertyTypeCustomization> MakeInstance()
	{
		return MakeShareable(new FAnchorDataCustomization());
	}
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override
	{
		auto TargetObjects = CustomizationUtils.GetPropertyUtilities()->GetSelectedObjects();
		TargetScriptArray.Empty();
		for (auto Item : TargetObjects)
		{
			if (auto ValidItem = Cast<ULexWidget>(Item.Get()))
			{
				TargetScriptArray.Add(ValidItem);
				if (ValidItem->GetWorld() != nullptr)
				{
					if (ValidItem->GetWorld()->WorldType == EWorldType::Editor)
					{
						ValidItem->EditorForceUpdate();
					}
				}
			}
		}
		
		auto AnchorHandle = PropertyHandle.ToSharedPtr();
		auto AnchorMinHandle = PropertyHandle->GetChildHandle("AnchorMin");
		auto AnchorMaxHandle = PropertyHandle->GetChildHandle("AnchorMax");
		auto AnchoredPositionHandle = PropertyHandle->GetChildHandle("AnchoredPosition");
		auto SizeDeltaHandle = PropertyHandle->GetChildHandle("SizeDelta");
		FVector2D AnchorMin, AnchorMax;
		AnchorMinHandle->GetValue(AnchorMin);
		AnchorMaxHandle->GetValue(AnchorMax);

		//anchors preset menu
		FVector2D anchorItemSize(42, 42);
		float itemBasePadding = 8;
		FMargin AnchorLabelMargin = FMargin(4, 2);
		FMargin AnchorValueMargin = FMargin(2, 2);

		auto MakeAnchorLabelWidget = [&](int AnchorLabelIndex) {
			return
				SNew(SBox)
				.Padding(AnchorLabelMargin)
				.VAlign(EVerticalAlignment::VAlign_Center)
				[
					SNew(STextBlock)
					.Text(this, &FAnchorDataCustomization::GetAnchorLabelText, AnchorMinHandle, AnchorMaxHandle, AnchorLabelIndex)
					.ToolTipText(this, &FAnchorDataCustomization::GetAnchorLabelTooltipText, AnchorMinHandle, AnchorMaxHandle, AnchorLabelIndex)
					.Font(IDetailLayoutBuilder::GetDetailFont())
				]
			;
		};
		auto PropertyUtilities = CustomizationUtils.GetPropertyUtilities();
		auto MakeAnchorValueWidget = [=, this](int AnchorValueIndex) {
			return
				SNew(SBox)
				.Padding(AnchorValueMargin)
				.VAlign(EVerticalAlignment::VAlign_Center)
				[
					SNew(SNumericEntryBox<float>)
					.AllowSpin(true)
					.MinSliderValue(this, &FAnchorDataCustomization::GetMinMaxSliderValue, AnchorHandle, AnchorValueIndex, true)
					.MaxSliderValue(this, &FAnchorDataCustomization::GetMinMaxSliderValue, AnchorHandle, AnchorValueIndex, false)
					.Delta(this, &FAnchorDataCustomization::GetSliderDeltaValue, AnchorHandle, AnchorValueIndex)
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.UndeterminedString( NSLOCTEXT( "PropertyEditor", "MultipleValues", "Multiple Values") )
					.Value(this, &FAnchorDataCustomization::GetAnchorValue, AnchorHandle, AnchorValueIndex)
					.OnValueChanged(this, &FAnchorDataCustomization::OnAnchorValueChanged, AnchorHandle, AnchorValueIndex)
					.OnValueCommitted(this, &FAnchorDataCustomization::OnAnchorValueCommitted, AnchorHandle, AnchorValueIndex)
					.OnBeginSliderMovement(this, &FAnchorDataCustomization::OnAnchorValueSliderMovementBegin)
					.OnEndSliderMovement(this, &FAnchorDataCustomization::OnAnchorValueSliderMovementEnd, AnchorHandle, AnchorValueIndex)
					.IsEnabled(this, &FAnchorDataCustomization::IsAnchorValueEnable, AnchorHandle, AnchorValueIndex)
				]
			;
		};
		auto MakeAnchorPreviewWidget = [=, this](LGUIAnchorPreviewWidget::UIAnchorHorizontalAlign HAlign, LGUIAnchorPreviewWidget::UIAnchorVerticalAlign VAlign) {
			return
				SNew(LGUIAnchorPreviewWidget::SAnchorPreviewWidget, anchorItemSize)
				.BasePadding(itemBasePadding)
				.SelectedHAlign(this, &FAnchorDataCustomization::GetAnchorHAlign, AnchorMinHandle, AnchorMaxHandle)
				.SelectedVAlign(this, &FAnchorDataCustomization::GetAnchorVAlign, AnchorMinHandle, AnchorMaxHandle)
				.PersistentHAlign(HAlign)
				.PersistentVAlign(VAlign)
				.ButtonEnable(true)
				.OnAnchorChange(this, &FAnchorDataCustomization::OnSelectAnchor, PropertyUtilities)
			;
		};//@todo: auto refresh SAnchorPreviewWidget when change from AnchorMinMax

		auto SplitLineColor = FLinearColor(0.5f, 0.5f, 0.5f);
		HeaderRow
		.IsEnabled(TAttribute<bool>(PropertyHandle, &IPropertyHandle::IsEditable))
		.NameContent()
		[
			SNew(SVerticalBox)
			+SVerticalBox::Slot()
			[
				SNew(SBox)
				.Visibility(this, &FAnchorDataCustomization::GetAnchorPresetButtonVisibility)
				[
					SNew(SComboButton)
					.ContentPadding(8)
					.HasDownArrow(false)
					.ToolTipText(this, &FAnchorDataCustomization::GetAnchorsTooltipText)
					.ButtonStyle(FLGUIEditorStyle::Get(), "AnchorButton")
					.ButtonContent()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.HAlign(EHorizontalAlignment::HAlign_Left)
						[
							SNew(SVerticalBox)
							+SVerticalBox::Slot()
							.Padding(FMargin(0, 0))
							[
								SNew(SBox)
								.Padding(FMargin(0, 0))
								.HAlign(EHorizontalAlignment::HAlign_Center)
								[
									SNew(STextBlock)
									.Text(this, &FAnchorDataCustomization::GetHAlignText, AnchorMinHandle, AnchorMaxHandle)
									.Font(IDetailLayoutBuilder::GetDetailFont())
								]
							]
							+SVerticalBox::Slot()
							.Padding(FMargin(0, 0))
							.AutoHeight()
							[
								TargetScriptArray[0]->GetUIParent() != nullptr
								?
								SNew(SBox)
								[
									SNew(LGUIAnchorPreviewWidget::SAnchorPreviewWidget, FVector2D(40, 40))
									.BasePadding(0)
									.ButtonEnable(false)
									.PersistentHAlign(this, &FAnchorDataCustomization::GetAnchorHAlign, AnchorMinHandle, AnchorMaxHandle)
									.PersistentVAlign(this, &FAnchorDataCustomization::GetAnchorVAlign, AnchorMinHandle, AnchorMaxHandle)
									//.SelectedHAlign(this, &FUIItemCustomization::GetAnchorHAlign, AnchorMinHandle, AnchorMaxHandle)
									//.SelectedVAlign(this, &FUIItemCustomization::GetAnchorVAlign, AnchorMinHandle, AnchorMaxHandle)
								]
								:
								SNew(SBox)
							]
						]
						+SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(SBox)
							.Padding(FMargin(0, 0))
							.HAlign(EHorizontalAlignment::HAlign_Center)
							[
								SNew(STextBlock)
								.Text(this, &FAnchorDataCustomization::GetVAlignText, AnchorMinHandle, AnchorMaxHandle)
								.Font(IDetailLayoutBuilder::GetDetailFont())
								.Justification(ETextJustify::Center)
								.RenderTransformPivot(FVector2D(0, 0.5f))
								.RenderTransform(FSlateRenderTransform(FQuat2D(FMath::DegreesToRadians(90)), FVector2D(-12, -10)))
							]
						]
					]
					.MenuContent()
					[
						SNew(SVerticalBox)
						+SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(SBorder)
							.Padding(4)
							[
								SNew(SVerticalBox)
								+SVerticalBox::Slot()
								[
									SNew(STextBlock)
									.Text(LOCTEXT("AnchorPresets", "Anchor Presets"))
								]
								+SVerticalBox::Slot()
								[
									SNew(STextBlock)
									.Text(LOCTEXT("AnchorPresetsHelperKeys", "Shift: Also set pivot		Alt: Also set position"))
									.Font(IDetailLayoutBuilder::GetDetailFont())
								]
							]
						]
						+SVerticalBox::Slot()
						.AutoHeight()
						.Padding(4)
						[
							SNew(SVerticalBox)
							+SVerticalBox::Slot()
							.AutoHeight()
							[
								SNew(SHorizontalBox)
								+SHorizontalBox::Slot()
								.AutoWidth()
								[
									SNew(SOverlay)
									+SOverlay::Slot()
									[
										SNew(SUniformGridPanel)
										+SUniformGridPanel::Slot(1, 0)
										[
											MakeAnchorPreviewWidget(LGUIAnchorPreviewWidget::UIAnchorHorizontalAlign::Left, LGUIAnchorPreviewWidget::UIAnchorVerticalAlign::None)
										]
										+SUniformGridPanel::Slot(2, 0) 
										[
											MakeAnchorPreviewWidget(LGUIAnchorPreviewWidget::UIAnchorHorizontalAlign::Center, LGUIAnchorPreviewWidget::UIAnchorVerticalAlign::None)
										]
										+SUniformGridPanel::Slot(3, 0) 
										[
											MakeAnchorPreviewWidget(LGUIAnchorPreviewWidget::UIAnchorHorizontalAlign::Right, LGUIAnchorPreviewWidget::UIAnchorVerticalAlign::None)
										]
										+SUniformGridPanel::Slot(4, 0) 
										[
											MakeAnchorPreviewWidget(LGUIAnchorPreviewWidget::UIAnchorHorizontalAlign::Stretch, LGUIAnchorPreviewWidget::UIAnchorVerticalAlign::None)
										]
										//Top
										+SUniformGridPanel::Slot(0, 1)
										[
											MakeAnchorPreviewWidget(LGUIAnchorPreviewWidget::UIAnchorHorizontalAlign::None, LGUIAnchorPreviewWidget::UIAnchorVerticalAlign::Top)
										]
										+SUniformGridPanel::Slot(1, 1)
										[
											MakeAnchorPreviewWidget(LGUIAnchorPreviewWidget::UIAnchorHorizontalAlign::Left, LGUIAnchorPreviewWidget::UIAnchorVerticalAlign::Top)
										]
										+SUniformGridPanel::Slot(2, 1) 
										[
											MakeAnchorPreviewWidget(LGUIAnchorPreviewWidget::UIAnchorHorizontalAlign::Center, LGUIAnchorPreviewWidget::UIAnchorVerticalAlign::Top)
										]
										+SUniformGridPanel::Slot(3, 1) 
										[
											MakeAnchorPreviewWidget(LGUIAnchorPreviewWidget::UIAnchorHorizontalAlign::Right, LGUIAnchorPreviewWidget::UIAnchorVerticalAlign::Top)
										]
										+SUniformGridPanel::Slot(4, 1) 
										[
											MakeAnchorPreviewWidget(LGUIAnchorPreviewWidget::UIAnchorHorizontalAlign::Stretch, LGUIAnchorPreviewWidget::UIAnchorVerticalAlign::Top)
										]
										//Center
										+SUniformGridPanel::Slot(0, 2)
										[
											MakeAnchorPreviewWidget(LGUIAnchorPreviewWidget::UIAnchorHorizontalAlign::None, LGUIAnchorPreviewWidget::UIAnchorVerticalAlign::Middle)
										]
										+SUniformGridPanel::Slot(1, 2)
										[
											MakeAnchorPreviewWidget(LGUIAnchorPreviewWidget::UIAnchorHorizontalAlign::Left, LGUIAnchorPreviewWidget::UIAnchorVerticalAlign::Middle)
										]
										+SUniformGridPanel::Slot(2, 2) 
										[
											MakeAnchorPreviewWidget(LGUIAnchorPreviewWidget::UIAnchorHorizontalAlign::Center, LGUIAnchorPreviewWidget::UIAnchorVerticalAlign::Middle)
										]
										+SUniformGridPanel::Slot(3, 2) 
										[
											MakeAnchorPreviewWidget(LGUIAnchorPreviewWidget::UIAnchorHorizontalAlign::Right, LGUIAnchorPreviewWidget::UIAnchorVerticalAlign::Middle)
										]
										+SUniformGridPanel::Slot(4, 2) 
										[
											MakeAnchorPreviewWidget(LGUIAnchorPreviewWidget::UIAnchorHorizontalAlign::Stretch, LGUIAnchorPreviewWidget::UIAnchorVerticalAlign::Middle)
										]
										//Bottom
										+SUniformGridPanel::Slot(0, 3)
										[
											MakeAnchorPreviewWidget(LGUIAnchorPreviewWidget::UIAnchorHorizontalAlign::None, LGUIAnchorPreviewWidget::UIAnchorVerticalAlign::Bottom)
										]
										+SUniformGridPanel::Slot(1, 3)
										[
											MakeAnchorPreviewWidget(LGUIAnchorPreviewWidget::UIAnchorHorizontalAlign::Left, LGUIAnchorPreviewWidget::UIAnchorVerticalAlign::Bottom)
										]
										+SUniformGridPanel::Slot(2, 3) 
										[
											MakeAnchorPreviewWidget(LGUIAnchorPreviewWidget::UIAnchorHorizontalAlign::Center, LGUIAnchorPreviewWidget::UIAnchorVerticalAlign::Bottom)
										]
										+SUniformGridPanel::Slot(3, 3) 
										[
											MakeAnchorPreviewWidget(LGUIAnchorPreviewWidget::UIAnchorHorizontalAlign::Right, LGUIAnchorPreviewWidget::UIAnchorVerticalAlign::Bottom)
										]
										+SUniformGridPanel::Slot(4, 3) 
										[
											MakeAnchorPreviewWidget(LGUIAnchorPreviewWidget::UIAnchorHorizontalAlign::Stretch, LGUIAnchorPreviewWidget::UIAnchorVerticalAlign::Bottom)
										]
										//Bottom stretch
										+SUniformGridPanel::Slot(0, 4)
										[
											MakeAnchorPreviewWidget(LGUIAnchorPreviewWidget::UIAnchorHorizontalAlign::None, LGUIAnchorPreviewWidget::UIAnchorVerticalAlign::Stretch)
										]
										+SUniformGridPanel::Slot(1, 4)
										[
											MakeAnchorPreviewWidget(LGUIAnchorPreviewWidget::UIAnchorHorizontalAlign::Left, LGUIAnchorPreviewWidget::UIAnchorVerticalAlign::Stretch)
										]
										+SUniformGridPanel::Slot(2, 4) 
										[
											MakeAnchorPreviewWidget(LGUIAnchorPreviewWidget::UIAnchorHorizontalAlign::Center, LGUIAnchorPreviewWidget::UIAnchorVerticalAlign::Stretch)
										]
										+SUniformGridPanel::Slot(3, 4) 
										[
											MakeAnchorPreviewWidget(LGUIAnchorPreviewWidget::UIAnchorHorizontalAlign::Right, LGUIAnchorPreviewWidget::UIAnchorVerticalAlign::Stretch)
										]
										+SUniformGridPanel::Slot(4, 4) 
										[
											MakeAnchorPreviewWidget(LGUIAnchorPreviewWidget::UIAnchorHorizontalAlign::Stretch, LGUIAnchorPreviewWidget::UIAnchorVerticalAlign::Stretch)
										]
									]
									//split line
									+ SOverlay::Slot()
									[
										SNew(SHorizontalBox)
										+ SHorizontalBox::Slot()
										.HAlign(EHorizontalAlignment::HAlign_Left)
										[
											SNew(SBox)
											.WidthOverride(anchorItemSize.X + 16)
											[
												SNew(SBox)
												.HAlign(EHorizontalAlignment::HAlign_Right)
												.WidthOverride(1)
												[
													SNew(SImage)
													.Image(FLGUIEditorStyle::Get().GetBrush("LGUIEditor.WhiteDot"))
													.ColorAndOpacity(SplitLineColor)
												]
											]
										]
									]
									+ SOverlay::Slot()
									[
										SNew(SHorizontalBox)
										+ SHorizontalBox::Slot()
										.HAlign(EHorizontalAlignment::HAlign_Right)
										[
											SNew(SBox)
											.WidthOverride(anchorItemSize.X + 16)
											[
												SNew(SBox)
												.HAlign(EHorizontalAlignment::HAlign_Left)
												.WidthOverride(1)
												[
													SNew(SImage)
													.Image(FLGUIEditorStyle::Get().GetBrush("LGUIEditor.WhiteDot"))
													.ColorAndOpacity(SplitLineColor)
												]
											]
										]
									]
									+ SOverlay::Slot()
									[
										SNew(SHorizontalBox)
										+ SHorizontalBox::Slot()
										.VAlign(EVerticalAlignment::VAlign_Top)
										[
											SNew(SBox)
											.HeightOverride(anchorItemSize.X + 16)
											[
												SNew(SBox)
												.VAlign(EVerticalAlignment::VAlign_Bottom)
												.HeightOverride(1)
												[
													SNew(SImage)
													.Image(FLGUIEditorStyle::Get().GetBrush("LGUIEditor.WhiteDot"))
													.ColorAndOpacity(SplitLineColor)
												]
											]
										]
									]
									+ SOverlay::Slot()
									[
										SNew(SHorizontalBox)
										+ SHorizontalBox::Slot()
										.VAlign(EVerticalAlignment::VAlign_Bottom)
										[
											SNew(SBox)
											.HeightOverride(anchorItemSize.X + 16)
											[
												SNew(SBox)
												.VAlign(EVerticalAlignment::VAlign_Top)
												.HeightOverride(1)
												[
													SNew(SImage)
													.Image(FLGUIEditorStyle::Get().GetBrush("LGUIEditor.WhiteDot"))
													.ColorAndOpacity(SplitLineColor)
												]
											]
										]
									]
								]
							]
						]
					]
				]
			]
		]
		.ValueContent()
		.MinDesiredWidth(500)
		[
			SNew(SBox)
			.IsEnabled(this, &FAnchorDataCustomization::IsAnchorEditable)
			[
				SNew(SVerticalBox)
				+SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SHorizontalBox)
					+SHorizontalBox::Slot()
					.FillWidth(0.5f)
					[
						MakeAnchorLabelWidget(0)
					]
					+SHorizontalBox::Slot()
					.FillWidth(0.5f)
					[
						MakeAnchorLabelWidget(1)
					]
				]
				+SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SHorizontalBox)
					+SHorizontalBox::Slot()
					.FillWidth(0.5f)
					[
						MakeAnchorValueWidget(0)
					]
					+SHorizontalBox::Slot()
					.FillWidth(0.5f)
					[
						MakeAnchorValueWidget(1)
					]
				]

				+SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SHorizontalBox)
					+SHorizontalBox::Slot()
					.FillWidth(0.5f)
					[
						MakeAnchorLabelWidget(2)
					]
					+SHorizontalBox::Slot()
					.FillWidth(0.5f)
					[
						MakeAnchorLabelWidget(3)
					]
				]
				+SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SHorizontalBox)
					+SHorizontalBox::Slot()
					.FillWidth(0.5f)
					[
						MakeAnchorValueWidget(2)
					]
					+SHorizontalBox::Slot()
					.FillWidth(0.5f)
					[
						MakeAnchorValueWidget(3)
					]
				]
			]
		];
		
	}
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override{}

private:
	bool IsAnchorEditable()const
	{
		if (TargetScriptArray.Num() > 0 && TargetScriptArray[0].IsValid())
		{
			auto Widget = TargetScriptArray[0];
			if (FLGUIPrefabEditor::ActorIsRootAgent(Widget->GetOwner()))return true;//special for PrefabEditor's agent root actor
			if (Widget->GetUIParent() != nullptr)return true;//not root
			if (Widget->IsCanvasWidget() && Widget->GetRenderCanvas() != nullptr && Widget->GetRenderCanvas()->IsRenderToScreenSpace())//is root canvas, and is render to screen space
			{
				return false;
			}
		}
		return true;
	}

	EVisibility GetAnchorPresetButtonVisibility()const
	{
		if (TargetScriptArray.Num() > 0 && TargetScriptArray[0].IsValid())
		{
			return TargetScriptArray[0]->GetUIParent() != nullptr ? EVisibility::Visible : EVisibility::Hidden;
		}
		return EVisibility::Hidden;
	}
	FText GetAnchorsTooltipText()const
	{
		return GetLayoutControlAnchorValue().AnyControl() ? LOCTEXT("ChangeAnchor_Tooltip", "Change anchor") : LOCTEXT("AnchorIsControlledByLayout", "Anchor is controlled by layout");
	}
	FText GetHAlignText(TSharedPtr<IPropertyHandle> AnchorMinHandle, TSharedPtr<IPropertyHandle> AnchorMaxHandle)const
	{
		if (TargetScriptArray.Num() == 0 || !TargetScriptArray[0].IsValid())return FText();

		FVector2D AnchorMinValue;
		AnchorMinHandle->GetValue(AnchorMinValue);
		FVector2D AnchorMaxValue;
		AnchorMaxHandle->GetValue(AnchorMaxValue);

		if (AnchorMinValue.X == AnchorMaxValue.X)
		{
			if (AnchorMinValue.X == 0)
			{
				return LOCTEXT("AnchorLeft", "Left");
			}
			else if (AnchorMinValue.X == 0.5f)
			{
				return LOCTEXT("AnchorCenter", "Center");
			}
			else if (AnchorMinValue.X == 1.0f)
			{
				return LOCTEXT("AnchorRight", "Right");
			}
			else
			{
				return LOCTEXT("AnchorCustom", "Custom");
			}
		}
		else if (AnchorMinValue.X == 0.0f && AnchorMaxValue.X == 1.0f)
		{
			return LOCTEXT("AnchorStretch", "Stretch");
		}
		else
		{
			return LOCTEXT("AnchorCustom", "Custom");
		}
	}
	FText GetVAlignText(TSharedPtr<IPropertyHandle> AnchorMinHandle, TSharedPtr<IPropertyHandle> AnchorMaxHandle)const
	{
		if (TargetScriptArray.Num() == 0 || !TargetScriptArray[0].IsValid())return FText();

		FVector2D AnchorMinValue;
		AnchorMinHandle->GetValue(AnchorMinValue);
		FVector2D AnchorMaxValue;
		AnchorMaxHandle->GetValue(AnchorMaxValue);

		if (AnchorMinValue.Y == AnchorMaxValue.Y)
		{
			if (AnchorMinValue.Y == 0)
			{
				return LOCTEXT("AnchorBottom", "Bottom");
			}
			else if (AnchorMinValue.Y == 0.5f)
			{
				return LOCTEXT("AnchorMiddle", "Middle");
			}
			else if (AnchorMinValue.Y == 1.0f)
			{
				return LOCTEXT("AnchorTop", "Top");
			}
			else
			{
				return LOCTEXT("AnchorCustom", "Custom");
			}
		}
		else if (AnchorMinValue.Y == 0.0f && AnchorMaxValue.Y == 1.0f)
		{
			return LOCTEXT("AnchorStretch", "Stretch");
		}
		else
		{
			return LOCTEXT("AnchorCustom", "Custom");
		}
	}

	FText GetAnchorLabelText(TSharedPtr<IPropertyHandle> AnchorMinHandle, TSharedPtr<IPropertyHandle> AnchorMaxHandle, int LabelIndex)const
	{
		if (TargetScriptArray.Num() == 0 || !TargetScriptArray[0].IsValid())return FText();

		FVector2D AnchorMinValue;
		AnchorMinHandle->GetValue(AnchorMinValue);
		FVector2D AnchorMaxValue;
		AnchorMaxHandle->GetValue(AnchorMaxValue);

		switch (LabelIndex)
		{
		case 0://anchored position y, stretch left
			{
				if (AnchorMinValue.X == AnchorMaxValue.X)
				{
					return LOCTEXT("AnchoredPositionX", "PosY");
				}
				else
				{
					return LOCTEXT("AnchoredLeft", "Left");
				}
			}
			break;
		case 1://anchored position z, stretch top
			{
				if (AnchorMinValue.Y == AnchorMaxValue.Y)
				{
					return LOCTEXT("AnchoredPositionY", "PosZ");
				}
				else
				{
					return LOCTEXT("AnchoredTop", "Top");
				}
			}
			break;
		case 2://width, stretch right
			{
				if (AnchorMinValue.X == AnchorMaxValue.X)
				{
					return LOCTEXT("Width", "Width");
				}
				else
				{
					return LOCTEXT("AnchoredRight", "Right");
				}
			}
			break;
		case 3://height, stretch bottom
			{
				if (AnchorMinValue.Y == AnchorMaxValue.Y)
				{
					return LOCTEXT("Height", "Height");
				}
				else
				{
					return LOCTEXT("AnchoredBottom", "Bottom");
				}
			}
			break;
		}
		return LOCTEXT("AnchorError", "Error");
	}

	FText GetAnchorLabelTooltipText(TSharedPtr<IPropertyHandle> AnchorMinHandle, TSharedPtr<IPropertyHandle> AnchorMaxHandle, int LabelTooltipIndex)const
	{
		if (TargetScriptArray.Num() == 0 || !TargetScriptArray[0].IsValid())return FText();

		FVector2D AnchorMinValue;
		AnchorMinHandle->GetValue(AnchorMinValue);
		FVector2D AnchorMaxValue;
		AnchorMaxHandle->GetValue(AnchorMaxValue);

		switch (LabelTooltipIndex)
		{
		default:
		case 0://anchored position x, stretch left
			{
				if (AnchorMinValue.X == AnchorMaxValue.X)
				{
					return FText::Format(LOCTEXT("AnchoredPositionX_Tooltip", "Horizontal anchored position. Related function: {0} / {1}."), FText::FromString(GET_FUNCTION_NAME_STRING_CHECKED(ULexWidget, GetAnchoredPosition)), FText::FromString(GET_FUNCTION_NAME_STRING_CHECKED(ULexWidget, SetAnchoredPosition)));
				}
				else
				{
					return FText::Format(LOCTEXT("AnchoredLeft_Tooltip", "Calculated distance to parent's left anchor point. Related function: {0} / {1}."), FText::FromString(GET_FUNCTION_NAME_STRING_CHECKED(ULexWidget, GetAnchorLeft)), FText::FromString(GET_FUNCTION_NAME_STRING_CHECKED(ULexWidget, SetAnchorLeft)));
				}
			}
			break;
		case 1://anchored position y, stretch top
			{
				if (AnchorMinValue.Y == AnchorMaxValue.Y)
				{
					return FText::Format(LOCTEXT("AnchoredPositionY_Tooltip", "Vertical anchored position. Related function: {0} / {1}."), FText::FromString(GET_FUNCTION_NAME_STRING_CHECKED(ULexWidget, GetAnchoredPosition)), FText::FromString(GET_FUNCTION_NAME_STRING_CHECKED(ULexWidget, SetAnchoredPosition)));
				}
				else
				{
					return FText::Format(LOCTEXT("AnchoredTop_Tooltip", "Calculated distance to parent's top anchor point. Related function: {0} / {1}."), FText::FromString(GET_FUNCTION_NAME_STRING_CHECKED(ULexWidget, GetAnchorLeft)), FText::FromString(GET_FUNCTION_NAME_STRING_CHECKED(ULexWidget, SetAnchorLeft)));
				}
			}
			break;
		case 2://width, stretch right
			{
				if (AnchorMinValue.X == AnchorMaxValue.X)
				{
					return FText::Format(LOCTEXT("Width_Tooltip", "Horizontal size. Related function: {0} / {1}."), FText::FromString(GET_FUNCTION_NAME_STRING_CHECKED(ULexWidget, GetWidth)), FText::FromString(GET_FUNCTION_NAME_STRING_CHECKED(ULexWidget, SetWidth)));
				}
				else
				{
					return FText::Format(LOCTEXT("AnchoredRight_Tooltip", "Calculated distance to parent's right anchor point. Related function: {0} / {1}."), FText::FromString(GET_FUNCTION_NAME_STRING_CHECKED(ULexWidget, GetAnchorLeft)), FText::FromString(GET_FUNCTION_NAME_STRING_CHECKED(ULexWidget, SetAnchorLeft)));
				}
			}
			break;
		case 3://height, stretch bottom
			{
				if (AnchorMinValue.Y == AnchorMaxValue.Y)
				{
					return FText::Format(LOCTEXT("Height_Tooltip", "Vertical size. Related function: {0} / {1}"), FText::FromString(GET_FUNCTION_NAME_STRING_CHECKED(ULexWidget, GetHeight)), FText::FromString(GET_FUNCTION_NAME_STRING_CHECKED(ULexWidget, SetHeight)));
				}
				else
				{
					return FText::Format(LOCTEXT("AnchoredBottom_Tooltip", "Calculated distance to parent's bottom anchor point. Related function: {0} / {0}."), FText::FromString(GET_FUNCTION_NAME_STRING_CHECKED(ULexWidget, GetAnchorLeft)), FText::FromString(GET_FUNCTION_NAME_STRING_CHECKED(ULexWidget, SetAnchorLeft)));
				}
			}
			break;
		}
		return LOCTEXT("AnchorError", "Error");
	}

	LGUIAnchorPreviewWidget::UIAnchorHorizontalAlign GetAnchorHAlign(TSharedPtr<IPropertyHandle> AnchorMinHandle, TSharedPtr<IPropertyHandle> AnchorMaxHandle)const
	{
		if (TargetScriptArray.Num() == 0 || !TargetScriptArray[0].IsValid())return LGUIAnchorPreviewWidget::UIAnchorHorizontalAlign::None;

		FVector2D AnchorMinValue;
		AnchorMinHandle->GetValue(AnchorMinValue);
		FVector2D AnchorMaxValue;
		AnchorMaxHandle->GetValue(AnchorMaxValue);

		LGUIAnchorPreviewWidget::UIAnchorHorizontalAlign AnchorHAlign = LGUIAnchorPreviewWidget::UIAnchorHorizontalAlign::None;
		if (AnchorMinValue.X == AnchorMaxValue.X)
		{
			if (AnchorMinValue.X == 0)
			{
				AnchorHAlign = LGUIAnchorPreviewWidget::UIAnchorHorizontalAlign::Left;
			}
			else if (AnchorMinValue.X == 0.5f)
			{
				AnchorHAlign = LGUIAnchorPreviewWidget::UIAnchorHorizontalAlign::Center;
			}
			else if (AnchorMinValue.X == 1.0f)
			{
				AnchorHAlign = LGUIAnchorPreviewWidget::UIAnchorHorizontalAlign::Right;
			}
		}
		else if (AnchorMinValue.X == 0.0f && AnchorMaxValue.X == 1.0f)
		{
			AnchorHAlign = LGUIAnchorPreviewWidget::UIAnchorHorizontalAlign::Stretch;
		}
		return AnchorHAlign;
	}
	LGUIAnchorPreviewWidget::UIAnchorVerticalAlign GetAnchorVAlign(TSharedPtr<IPropertyHandle> AnchorMinHandle, TSharedPtr<IPropertyHandle> AnchorMaxHandle)const
	{
		if (TargetScriptArray.Num() == 0 || !TargetScriptArray[0].IsValid())return LGUIAnchorPreviewWidget::UIAnchorVerticalAlign::None;

		FVector2D AnchorMinValue;
		AnchorMinHandle->GetValue(AnchorMinValue);
		FVector2D AnchorMaxValue;
		AnchorMaxHandle->GetValue(AnchorMaxValue);

		LGUIAnchorPreviewWidget::UIAnchorVerticalAlign AnchorVAlign = LGUIAnchorPreviewWidget::UIAnchorVerticalAlign::None;
		if (AnchorMinValue.Y == AnchorMaxValue.Y)
		{
			if (AnchorMinValue.Y == 0)
			{
				AnchorVAlign = LGUIAnchorPreviewWidget::UIAnchorVerticalAlign::Bottom;
			}
			else if (AnchorMinValue.Y == 0.5f)
			{
				AnchorVAlign = LGUIAnchorPreviewWidget::UIAnchorVerticalAlign::Middle;
			}
			else if (AnchorMinValue.Y == 1.0f)
			{
				AnchorVAlign = LGUIAnchorPreviewWidget::UIAnchorVerticalAlign::Top;
			}
		}
		else if (AnchorMinValue.Y == 0.0f && AnchorMaxValue.Y == 1.0f)
		{
			AnchorVAlign = LGUIAnchorPreviewWidget::UIAnchorVerticalAlign::Stretch;
		}
		return AnchorVAlign;
	}

	void OnSelectAnchor(LGUIAnchorPreviewWidget::UIAnchorHorizontalAlign HorizontalAlign, LGUIAnchorPreviewWidget::UIAnchorVerticalAlign VerticalAlign, TSharedPtr<IPropertyUtilities> PropertyUtilities)
	{
		if (TargetScriptArray.Num() == 0 || !TargetScriptArray[0].IsValid())return;

		bool ShiftPressed = FSlateApplication::Get().GetModifierKeys().IsShiftDown();
		bool AltPressed = FSlateApplication::Get().GetModifierKeys().IsAltDown();

		GEditor->BeginTransaction(LOCTEXT("ChangeAnchor_Transaction", "Change LGUI Anchor"));
		for (auto& UIItem : TargetScriptArray)
		{
			UIItem->Modify();
		}

		for (auto& Widget : TargetScriptArray)
		{
			FVector2D DesiredPivot = Widget->GetPivot();
			auto AnchorMin = Widget->GetAnchorMin();
			auto AnchorMax = Widget->GetAnchorMax();
			switch (HorizontalAlign)
			{
			case LGUIAnchorPreviewWidget::UIAnchorHorizontalAlign::None:
				break;
			case LGUIAnchorPreviewWidget::UIAnchorHorizontalAlign::Left:
				{
					DesiredPivot.X = 0;
					AnchorMin.X = AnchorMax.X = 0;
				}
				break;
			case LGUIAnchorPreviewWidget::UIAnchorHorizontalAlign::Center:
				{
					DesiredPivot.X = 0.5f;
					AnchorMin.X = AnchorMax.X = 0.5f;
				}
				break;
			case LGUIAnchorPreviewWidget::UIAnchorHorizontalAlign::Right:
				{
					DesiredPivot.X = 1.0f;
					AnchorMin.X = AnchorMax.X = 1.0f;
				}
				break;
			case LGUIAnchorPreviewWidget::UIAnchorHorizontalAlign::Stretch:
				{
					DesiredPivot.X = 0.5f;
					AnchorMin.X = 0;
					AnchorMax.X = 1.0f;
				}
				break;
			}
			switch (VerticalAlign)
			{
			case LGUIAnchorPreviewWidget::UIAnchorVerticalAlign::None:
				break;
			case LGUIAnchorPreviewWidget::UIAnchorVerticalAlign::Top:
				{
					DesiredPivot.Y = 1.0f;
					AnchorMin.Y = AnchorMax.Y = 1;
				}
				break;
			case LGUIAnchorPreviewWidget::UIAnchorVerticalAlign::Middle:
				{
					DesiredPivot.Y = 0.5f;
					AnchorMin.Y = AnchorMax.Y = 0.5f;
				}
				break;
			case LGUIAnchorPreviewWidget::UIAnchorVerticalAlign::Bottom:
				{
					DesiredPivot.Y = 0.0f;
					AnchorMin.Y = AnchorMax.Y = 0.0f;
				}
				break;
			case LGUIAnchorPreviewWidget::UIAnchorVerticalAlign::Stretch:
				{
					DesiredPivot.Y = 0.5f;
					AnchorMin.Y = 0;
					AnchorMax.Y = 1.0f;
				}
				break;
			}
			auto PrevRelativeLocation = Widget->GetRelativeLocation();
			auto PrevWidth = Widget->GetWidth();
			auto PrevHeight = Widget->GetHeight();
			Widget->SetAnchorMin(AnchorMin);
			Widget->SetAnchorMax(AnchorMax);
			Widget->MarkAllDirtyRecursive();
			Widget->SetWidth(PrevWidth);
			Widget->SetHeight(PrevHeight);
			Widget->SetRelativeLocation(PrevRelativeLocation);
			if (AltPressed)
			{
				switch (HorizontalAlign)
				{
				case LGUIAnchorPreviewWidget::UIAnchorHorizontalAlign::Left:
					{
						Widget->SetHorizontalAnchoredPosition(-Widget->GetLocalSpaceLeft());
					}
					break;
				case LGUIAnchorPreviewWidget::UIAnchorHorizontalAlign::Center:
					{
						Widget->SetHorizontalAnchoredPosition(Widget->GetWidth() * (Widget->GetPivot().X - 0.5f));
					}
					break;
				case LGUIAnchorPreviewWidget::UIAnchorHorizontalAlign::Right:
					{
						Widget->SetHorizontalAnchoredPosition(-Widget->GetLocalSpaceRight());
					}
					break;
				case LGUIAnchorPreviewWidget::UIAnchorHorizontalAlign::Stretch:
					{
						Widget->SetAnchorLeft(0);
						Widget->SetAnchorRight(0);
					}
					break;
				}
				switch (VerticalAlign)
				{
				case LGUIAnchorPreviewWidget::UIAnchorVerticalAlign::Top:
					{
						Widget->SetVerticalAnchoredPosition(-Widget->GetLocalSpaceTop());
					}
					break;
				case LGUIAnchorPreviewWidget::UIAnchorVerticalAlign::Middle:
					{
						Widget->SetVerticalAnchoredPosition(Widget->GetHeight() * (Widget->GetPivot().Y - 0.5f));
					}
					break;
				case LGUIAnchorPreviewWidget::UIAnchorVerticalAlign::Bottom:
					{
						Widget->SetVerticalAnchoredPosition(-Widget->GetLocalSpaceBottom());
					}
					break;
				case LGUIAnchorPreviewWidget::UIAnchorVerticalAlign::Stretch:
					{
						Widget->SetAnchorBottom(0);
						Widget->SetAnchorTop(0);
					}
					break;
				}
			}
			if (ShiftPressed)
			{
				FMargin PrevAnchorAsMargin(Widget->GetAnchorLeft(), Widget->GetAnchorTop(), Widget->GetAnchorRight(), Widget->GetAnchorBottom());
				Widget->SetPivot(DesiredPivot);
				Widget->SetAnchorLeft(PrevAnchorAsMargin.Left);
				Widget->SetAnchorRight(PrevAnchorAsMargin.Right);
				Widget->SetAnchorBottom(PrevAnchorAsMargin.Bottom);
				Widget->SetAnchorTop(PrevAnchorAsMargin.Top);
			}

			FLexUIUtils::NotifyPropertyChanged(Widget.Get(), "AnchorData");
		}
		TargetScriptArray[0]->EditorForceUpdate();
		PropertyUtilities->RequestForceRefresh();
		GEditor->EndTransaction();
	}

	FLexLayoutControlAnchorData GetLayoutControlAnchorValue()const
	{
		FLexLayoutControlAnchorData Result;
		if (TargetScriptArray.Num() == 0 || !TargetScriptArray[0].IsValid())return Result;

		auto Widget = TargetScriptArray[0];
		if (Widget.IsValid())
		{
			if (Widget->GetLayout())
			{
				Result = Widget->GetLayout()->GetLayoutControlAnchor(Widget.Get());
			}
			if (auto Parent = Widget->GetUIParent())
			{
				if (auto ParentLayout = Parent->GetLayout())
				{
					auto ParentResult = ParentLayout->GetLayoutControlAnchor(Widget.Get());
					Result.Or(ParentResult);
				}
			}
		}
		return Result;
	}

	bool IsAnchorControlledByMultipleLayout(TMap<EAnchorControlledByLayoutType, TArray<UObject*>>& Result)const
	{
		if (TargetScriptArray.Num() == 0 || !TargetScriptArray[0].IsValid())return false;

		// auto Widget = TargetScriptArray[0];
		// if (Widget.IsValid())
		// {
		// 	if (auto Manager = ULGUIManagerWorldSubsystem::GetInstance(World))
		// 	{
		// 		auto AllLayoutArray = Manager->GetAllLayoutArray();
		// 		if (AllLayoutArray.Num() > 0)
		// 		{
		// 			for (auto& Item : AllLayoutArray)
		// 			{
		// 				FLGUICanLayoutControlAnchor ItemLayoutControl;
		// 				if (ILGUILayoutInterface::Execute_GetCanLayoutControlAnchor(Item.Get(), TargetScriptArray[0].Get(), ItemLayoutControl))
		// 				{
		// 					if (ItemLayoutControl.bCanControlHorizontalAnchoredPosition)
		// 					{
		// 						Result.FindOrAdd(EAnchorControlledByLayoutType::HorizontalAnchoredPosition).Add(Item.Get());
		// 					}
		// 					if (ItemLayoutControl.bCanControlHorizontalSizeDelta)
		// 					{
		// 						Result.FindOrAdd(EAnchorControlledByLayoutType::HorizontalSizeDelta).Add(Item.Get());
		// 					}
		// 					if (ItemLayoutControl.bCanControlVerticalAnchoredPosition)
		// 					{
		// 						Result.FindOrAdd(EAnchorControlledByLayoutType::VerticalAnchoredPosition).Add(Item.Get());
		// 					}
		// 					if (ItemLayoutControl.bCanControlVerticalSizeDelta)
		// 					{
		// 						Result.FindOrAdd(EAnchorControlledByLayoutType::VerticalSizeDelta).Add(Item.Get());
		// 					}
		// 				}
		// 			}
		// 		}
		// 	}
		// }
		// for (auto& KeyValue : Result)
		// {
		// 	if (KeyValue.Value.Num() > 1)return true;
		// }
		return false;
	}

	bool GetLayoutControlHorizontalAnchoredPosition()const
	{
		return GetLayoutControlAnchorValue().bCanControlHorizontalAnchoredPosition;
	}
	bool GetLayoutControlVerticalAnchoredPosition()const
	{
		return GetLayoutControlAnchorValue().bCanControlVerticalAnchoredPosition;
	}
	bool GetLayoutControlHorizontalSizeDelta()const
	{
		return GetLayoutControlAnchorValue().bCanControlHorizontalSizeDelta;
	}
	bool GetLayoutControlVerticalSizeDelta()const
	{
		return GetLayoutControlAnchorValue().bCanControlVerticalSizeDelta;
	}
	
	TOptional<float> GetMinMaxSliderValue(TSharedPtr<IPropertyHandle> AnchorHandle, int AnchorValueIndex, bool MinOrMax)const
	{
		auto Value = GetAnchorValue(AnchorHandle, AnchorValueIndex).Get(0.0f);
		Value = FMath::Abs(Value);
		float MaxRangeValue = ValueRangeArray[ValueRangeArray.Num() - 1];
		float RangeValue = MaxRangeValue;
		for (int i = ValueRangeArray.Num() - 1; i >= 0; i--)
		{
			auto RangeValueItem = ValueRangeArray[i];
			if (Value > RangeValueItem)
			{
				break;
			}
			else
			{
				RangeValue = RangeValueItem;
			}
		}
		return RangeValue * 
			(RangeValue >= MaxRangeValue ? 1.0f : (FMath::Abs(Value - RangeValue) < KINDA_SMALL_NUMBER ? 2.0f : 1.0f))
			* (MinOrMax ? -1.0f : 1.0f);
	}

	float GetSliderDeltaValue(TSharedPtr<IPropertyHandle> AnchorHandle, int AnchorValueIndex) const
	{
		auto Value = GetAnchorValue(AnchorHandle, AnchorValueIndex).Get(0.0f);
		Value = FMath::Abs(Value);
		float MaxRangeValue = ValueRangeArray[ValueRangeArray.Num() - 1];
		float RangeValue = MaxRangeValue;
		for (int i = ValueRangeArray.Num() - 1; i >= 0; i--)
		{
			auto RangeValueItem = ValueRangeArray[i];
			if (Value > RangeValueItem)
			{
				break;
			}
			else
			{
				RangeValue = RangeValueItem;
			}
		}
		RangeValue = FMath::Max(1.0f, RangeValue);
		return RangeValue * 0.001f;
	}

	TOptional<float> GetAnchorValue(TSharedPtr<IPropertyHandle> AnchorHandle, int AnchorValueIndex)const
	{
		if (TargetScriptArray.Num() == 0 || !TargetScriptArray[0].IsValid())return TOptional<float>();

		auto AnchorMinHandle = AnchorHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIAnchorData, AnchorMin));
		auto AnchorMaxHandle = AnchorHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIAnchorData, AnchorMax));
		auto AnchoredPositionHandle = AnchorHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIAnchorData, AnchoredPosition));
		auto SizeDeltaHandle = AnchorHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIAnchorData, SizeDelta));

		FVector2D AnchorMinValue;
		auto AnchorMinValueAccessResult = AnchorMinHandle->GetValue(AnchorMinValue);
		FVector2D AnchorMaxValue;
		auto AnchorMaxValueAccessResult = AnchorMaxHandle->GetValue(AnchorMaxValue);
		FVector2D AnchoredPosition;
		auto AnchoredPositionAccessResult = AnchoredPositionHandle->GetValue(AnchoredPosition);
		FVector2D SizeDelta;
		auto SizeDeltaAccessResult = SizeDeltaHandle->GetValue(SizeDelta);

		switch (AnchorValueIndex)
		{
		default:
		case 0://anchored position x, stretch left
			{
				if (AnchorMinValueAccessResult == FPropertyAccess::Result::Success && AnchorMaxValueAccessResult == FPropertyAccess::Result::Success)
				{
					auto GetValue = [=](TWeakObjectPtr<ULexWidget> Item)->float {
						if (AnchorMinValue.X == AnchorMaxValue.X)
						{
							return Item->GetHorizontalAnchoredPosition();
						}
						else
						{
							return Item->GetAnchorLeft();
						}
					};
					if (AnchoredPositionAccessResult == FPropertyAccess::Result::Success)
					{
						return GetValue(TargetScriptArray[0]);
					}
					else if (AnchoredPositionAccessResult == FPropertyAccess::Result::MultipleValues)
					{
						bool bIsSameValue = true;
						float Value = 0;
						bool bIsFirst = true;
						for (auto& Item : TargetScriptArray)
						{
							if (bIsFirst)
							{
								Value = GetValue(Item);
								bIsFirst = false;
							}
							else
							{
								if (FMath::Abs(GetValue(Item) - Value) > KINDA_SMALL_NUMBER)
								{
									bIsSameValue = false;
									break;
								}
							}
						}
						if (bIsSameValue)
						{
							return Value;
						}
					}
				}
				return TOptional<float>();
			}
			break;
		case 1://anchored position y, stretch top
			{
				if (AnchorMinValueAccessResult == FPropertyAccess::Result::Success && AnchorMaxValueAccessResult == FPropertyAccess::Result::Success)
				{
					auto GetValue = [=](TWeakObjectPtr<ULexWidget> Item)->float {
						if (AnchorMinValue.Y == AnchorMaxValue.Y)
						{
							return Item->GetVerticalAnchoredPosition();
						}
						else
						{
							return Item->GetAnchorTop();
						}
					};
					if (AnchoredPositionAccessResult == FPropertyAccess::Result::Success)
					{
						return GetValue(TargetScriptArray[0]);
					}
					else if (AnchoredPositionAccessResult == FPropertyAccess::Result::MultipleValues)
					{
						bool bIsSameValue = true;
						float Value = 0;
						bool bIsFirst = true;
						for (auto& Item : TargetScriptArray)
						{
							if (bIsFirst)
							{
								Value = GetValue(Item);
								bIsFirst = false;
							}
							else
							{
								if (FMath::Abs(GetValue(Item) - Value) > KINDA_SMALL_NUMBER)
								{
									bIsSameValue = false;
									break;
								}
							}
						}
						if (bIsSameValue)
						{
							return Value;
						}
					}
				}
				return TOptional<float>();
			}
			break;
		case 2://width, stretch right
			{
				if (AnchorMinValueAccessResult == FPropertyAccess::Result::Success && AnchorMaxValueAccessResult == FPropertyAccess::Result::Success)
				{
					auto GetValue = [=](TWeakObjectPtr<ULexWidget> Item)->float {
						if (AnchorMinValue.X == AnchorMaxValue.X)
						{
							return Item->GetSizeDelta().X;
						}
						else
						{
							return Item->GetAnchorRight();
						}
					};
					if (SizeDeltaAccessResult == FPropertyAccess::Result::Success)
					{
						return GetValue(TargetScriptArray[0]);
					}
					else if (SizeDeltaAccessResult == FPropertyAccess::Result::MultipleValues)
					{
						bool bIsSameValue = true;
						float Value = 0;
						bool bIsFirst = true;
						for (auto& Item : TargetScriptArray)
						{
							if (bIsFirst)
							{
								Value = GetValue(Item);
								bIsFirst = false;
							}
							else
							{
								if (FMath::Abs(GetValue(Item) - Value) > KINDA_SMALL_NUMBER)
								{
									bIsSameValue = false;
									break;
								}
							}
						}
						if (bIsSameValue)
						{
							return Value;
						}
					}
				}
				return TOptional<float>();
			}
			break;
		case 3://height, stretch bottom
			{
				if (AnchorMinValueAccessResult == FPropertyAccess::Result::Success && AnchorMaxValueAccessResult == FPropertyAccess::Result::Success)
				{
					auto GetValue = [=](TWeakObjectPtr<ULexWidget> Item)->float {
						if (AnchorMinValue.Y == AnchorMaxValue.Y)
						{
							return Item->GetSizeDelta().Y;
						}
						else
						{
							return Item->GetAnchorBottom();
						}
					};
					if (SizeDeltaAccessResult == FPropertyAccess::Result::Success)
					{
						return GetValue(TargetScriptArray[0]);
					}
					else if (SizeDeltaAccessResult == FPropertyAccess::Result::MultipleValues)
					{
						bool bIsSameValue = true;
						float Value = 0;
						bool bIsFirst = true;
						for (auto& Item : TargetScriptArray)
						{
							if (bIsFirst)
							{
								Value = GetValue(Item);
								bIsFirst = false;
							}
							else
							{
								if (FMath::Abs(GetValue(Item) - Value) > KINDA_SMALL_NUMBER)
								{
									bIsSameValue = false;
									break;
								}
							}
						}
						if (bIsSameValue)
						{
							return Value;
						}
					}
				}
				return TOptional<float>();
			}
			break;
		}
	}
	void ApplyValueChanged(float Value, TSharedPtr<IPropertyHandle> AnchorHandle, int AnchorValueIndex)
	{
		if (TargetScriptArray.Num() == 0 || !TargetScriptArray[0].IsValid())return;

		auto AnchorMinHandle = AnchorHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIAnchorData, AnchorMin));
		auto AnchorMaxHandle = AnchorHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIAnchorData, AnchorMax));
		auto AnchoredPositionHandle = AnchorHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIAnchorData, AnchoredPosition));
		auto SizeDeltaHandle = AnchorHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIAnchorData, SizeDelta));

		FVector2D AnchorMinValue;
		AnchorMinHandle->GetValue(AnchorMinValue);
		FVector2D AnchorMaxValue;
		AnchorMaxHandle->GetValue(AnchorMaxValue);

		switch (AnchorValueIndex)
		{
		case 0://anchored position x, stretch left
			{
				if (AnchorMinValue.X == AnchorMaxValue.X)
				{
					for (auto& Item : TargetScriptArray)
					{
						Item->SetHorizontalAnchoredPosition(Value);
					}
				}
				else
				{
					for (auto& Item : TargetScriptArray)
					{
						Item->SetAnchorLeft(Value);
					}
				}
			}
			break;
		case 1://anchored position y, stretch top
			{
				if (AnchorMinValue.Y == AnchorMaxValue.Y)
				{
					for (auto& Item : TargetScriptArray)
					{
						Item->SetVerticalAnchoredPosition(Value);
					}
				}
				else
				{
					for (auto& Item : TargetScriptArray)
					{
						Item->SetAnchorTop(Value);
					}
				}
			}
			break;
		case 2://width, stretch right
			{
				if (AnchorMinValue.X == AnchorMaxValue.X)
				{
					for (auto& Item : TargetScriptArray)
					{
						Item->SetWidth(Value);
					}
				}
				else
				{
					for (auto& Item : TargetScriptArray)
					{
						Item->SetAnchorRight(Value);
					}
				}
			}
			break;
		case 3://height, stretch bottom
			{
				if (AnchorMinValue.Y == AnchorMaxValue.Y)
				{
					for (auto& Item : TargetScriptArray)
					{
						Item->SetHeight(Value);
					}
				}
				else
				{
					for (auto& Item : TargetScriptArray)
					{
						Item->SetAnchorBottom(Value);
					}
				}
			}
			break;
		}

		auto AnchorProperty = FindFProperty<FProperty>(ULexWidget::StaticClass(), ULexWidget::GetPropertyName_AnchorData());
		auto RelativeLocationProperty = FindFProperty<FProperty>(USceneComponent::StaticClass(), FName(TEXT("RelativeLocation")));
		for (auto& Item : TargetScriptArray)
		{
			FLexUIUtils::NotifyPropertyChanged(Item.Get(), AnchorProperty);
			FLexUIUtils::NotifyPropertyChanged(Item.Get(), RelativeLocationProperty);
		}
	}
	void OnAnchorValueChanged(float Value, TSharedPtr<IPropertyHandle> AnchorHandle, int AnchorValueIndex)
	{
		GEditor->BeginTransaction(LOCTEXT("ChangeAnchorValue_Transaction", "Change LGUI Anchor Value"));
		for (auto& Item : TargetScriptArray)
		{
			Item->Modify();
		}
		ApplyValueChanged(Value, AnchorHandle, AnchorValueIndex);
		GEditor->EndTransaction();
	}
	void OnAnchorValueCommitted(float Value, ETextCommit::Type commitType, TSharedPtr<IPropertyHandle> AnchorHandle, int AnchorValueIndex)
	{
		GEditor->BeginTransaction(LOCTEXT("CommitAnchorValue_Transaction", "Commit LGUI Anchor Value"));
		for (auto& Item : TargetScriptArray)
		{
			Item->Modify();
		}
		ApplyValueChanged(Value, AnchorHandle, AnchorValueIndex);
		GEditor->EndTransaction();
	}

	void OnAnchorValueSliderMovementBegin()
	{
		GEditor->BeginTransaction(LOCTEXT("SlideAnchorValue_Transaction", "Slide LGUI Anchor Value"));
		for (auto& Item : TargetScriptArray)
		{
			Item->Modify();
		}
	}

	void OnAnchorValueSliderMovementEnd(float Value, TSharedPtr<IPropertyHandle> AnchorHandle, int AnchorValueIndex)
	{
		ApplyValueChanged(Value, AnchorHandle, AnchorValueIndex);
		GEditor->EndTransaction();
	}

	bool IsAnchorValueEnable(TSharedPtr<IPropertyHandle> AnchorHandle, int AnchorValueIndex)const
	{
		if (TargetScriptArray.Num() == 0 || !TargetScriptArray[0].IsValid())return false;

		auto AnchorMinHandle = AnchorHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIAnchorData, AnchorMin));
		auto AnchorMaxHandle = AnchorHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIAnchorData, AnchorMax));
		auto AnchoredPositionHandle = AnchorHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIAnchorData, AnchoredPosition));
		auto SizeDeltaHandle = AnchorHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIAnchorData, SizeDelta));

		FVector2D AnchorMinValue;
		auto AnchorMinValueAccessResult = AnchorMinHandle->GetValue(AnchorMinValue);
		FVector2D AnchorMaxValue;
		auto AnchorMaxValueAccessResult = AnchorMaxHandle->GetValue(AnchorMaxValue);
		FVector2D AnchoredPosition;
		auto AnchoredPositionAccessResult = AnchoredPositionHandle->GetValue(AnchoredPosition);
		FVector2D SizeDelta;
		auto SizeDeltaAccessResult = SizeDeltaHandle->GetValue(SizeDelta);

		switch (AnchorValueIndex)
		{
		default:
		case 0://anchored position x, stretch left
			{
				if (AnchorMinValueAccessResult == FPropertyAccess::Result::Success && AnchorMaxValueAccessResult == FPropertyAccess::Result::Success
					&& AnchoredPositionAccessResult == FPropertyAccess::Result::Success
					)
				{
					if (AnchorMinValue.X == AnchorMaxValue.X)
					{
						return !GetLayoutControlHorizontalAnchoredPosition();
					}
					else
					{
						return !GetLayoutControlHorizontalAnchoredPosition() && !GetLayoutControlHorizontalSizeDelta();
					}
				}
				else
				{
					return true;
				}
			}
			break;
		case 1://anchored position y, stretch top
			{
				if (AnchorMinValueAccessResult == FPropertyAccess::Result::Success && AnchorMaxValueAccessResult == FPropertyAccess::Result::Success
					&& AnchoredPositionAccessResult == FPropertyAccess::Result::Success
					)
				{
					if (AnchorMinValue.Y == AnchorMaxValue.Y)
					{
						return !GetLayoutControlVerticalAnchoredPosition();
					}
					else
					{
						return !GetLayoutControlVerticalAnchoredPosition() && !GetLayoutControlVerticalSizeDelta();
					}
				}
				else
				{
					return true;
				}
			}
			break;
		case 2://width, stretch right
			{
				if (AnchorMinValueAccessResult == FPropertyAccess::Result::Success && AnchorMaxValueAccessResult == FPropertyAccess::Result::Success
					&& SizeDeltaAccessResult == FPropertyAccess::Result::Success
					)
				{
					if (AnchorMinValue.X == AnchorMaxValue.X)
					{
						return !GetLayoutControlHorizontalSizeDelta();
					}
					else
					{
						return !GetLayoutControlHorizontalAnchoredPosition() && !GetLayoutControlHorizontalSizeDelta();
					}
				}
				else
				{
					return true;
				}
			}
			break;
		case 3://height, stretch bottom
			{
				if (AnchorMinValueAccessResult == FPropertyAccess::Result::Success && AnchorMaxValueAccessResult == FPropertyAccess::Result::Success
					&& SizeDeltaAccessResult == FPropertyAccess::Result::Success
					)
				{
					if (AnchorMinValue.Y == AnchorMaxValue.Y)
					{
						return !GetLayoutControlVerticalSizeDelta();
					}
					else
					{
						return !GetLayoutControlVerticalAnchoredPosition() && !GetLayoutControlVerticalSizeDelta();
					}
				}
				else
				{
					return true;
				}
			}
			break;
		}
	}
};

TArray<float> FAnchorDataCustomization::ValueRangeArray = {
	1.0f, 10.0f, 100.0f, 1000.0f, 10000.0f
};
#undef LOCTEXT_NAMESPACE