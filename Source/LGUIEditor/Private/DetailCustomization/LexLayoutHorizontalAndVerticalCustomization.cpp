// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "LexLayoutHorizontalAndVerticalCustomization.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"
#include "LGUIEditorModule.h"
#include "PropertyType/LexLayoutDirectionCustomization.h"
#include "PropertyType/LexLayoutHorizontalAndVerticalAligmentCustomization.h"
#include "HAL/PlatformApplicationMisc.h"
#include "PropertyType/LexLayoutSpacingCustomization.h"

#define LOCTEXT_NAMESPACE "LexLayoutHorizontalAndVertical"
UE_DISABLE_OPTIMIZATION

FLexLayoutHorizontalAndVerticalCustomization::FLexLayoutHorizontalAndVerticalCustomization()
{
}

FLexLayoutHorizontalAndVerticalCustomization::~FLexLayoutHorizontalAndVerticalCustomization()
{
}

TSharedRef<IDetailCustomization> FLexLayoutHorizontalAndVerticalCustomization::MakeInstance()
{
	return MakeShareable(new FLexLayoutHorizontalAndVerticalCustomization);
}
void FLexLayoutHorizontalAndVerticalCustomization::CustomizeDetails(const TSharedPtr<IDetailLayoutBuilder>& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> TargetObjects;
	DetailBuilder->GetObjectsBeingCustomized(TargetObjects);
	TargetScriptPtr = Cast<ULexLayoutHorizontalAndVertical>(TargetObjects[0].Get());
	if (TargetScriptPtr == nullptr)
	{
		UE_LOG(LGUIEditor, Log, TEXT("[FLexLayoutHorizontalAndVerticalCustomization]Get TargetScript is null"));
		return;
	}
	
	IDetailCategoryBuilder& Category = DetailBuilder->EditCategory("Layout");
	auto Direction_PH = DetailBuilder->GetProperty(GET_MEMBER_NAME_CHECKED(ULexLayoutHorizontalAndVertical, Direction));
	Category.AddProperty(Direction_PH);
	DetailBuilder->GetDetailsView()->RegisterInstancedCustomPropertyTypeLayout(TEXT("ELexLayoutDirection"), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FLexLayoutDirectionCustomization::MakeInstance));
	auto BeginClipboard_LexLayoutHorizontalAndVertical_Prefix = TEXT("Begin LexUI LexLayoutHorizontalAndVertical");
	auto HorizontalAlignment_PH = DetailBuilder->GetProperty(GET_MEMBER_NAME_CHECKED(ULexLayoutHorizontalAndVertical, HorizontalAlignment));
	auto VerticalAlignment_PH = DetailBuilder->GetProperty(GET_MEMBER_NAME_CHECKED(ULexLayoutHorizontalAndVertical, VerticalAlignment));
	Category.AddCustomRow(LOCTEXT("PositionAlignment_Row", "PositionAlignment"))
	.NameContent()
	[
		SNew(SBox)
		[
			SNew(STextBlock)
			.Font(DetailBuilder->GetDetailFont())
			.Text(LOCTEXT("PositionAlignment", "Alignment"))
		]
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
				.VAlign(VAlign_Center)
				.Padding(FMargin(2, 0))
				[
					SNew(STextBlock)
					.Font(DetailBuilder->GetDetailFont())
					.Text(LOCTEXT("HorizontalAlignment", "H"))
					.ToolTipText(LOCTEXT("HorizontalAlignment_Tooltip", "HorizontalAlignment"))
				]
				+SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNew(SBox)
					.IsEnabled_Lambda([=]
					{
						ELexLayoutDirection Direction;
						if (Direction_PH->GetValue(*(uint8*)&Direction) == FPropertyAccess::Success)
						{
							return Direction == ELexLayoutDirection::Horizontal;
						}
						return false;
					})
					[
						SNew(SSegmentedControl<ELexLayoutHorizontalAlignment>)
						.Value_Lambda([=]
						{
							uint8 Value;
							if (HorizontalAlignment_PH->GetValue(Value) == FPropertyAccess::Success)
							{
								return ELexLayoutHorizontalAlignment(Value);
							}
							return ELexLayoutHorizontalAlignment::Left;
						})
						.OnValueChanged_Lambda([=](ELexLayoutHorizontalAlignment NewState)
						{
							HorizontalAlignment_PH->SetValue((uint8)NewState);
						})
						+ SSegmentedControl<ELexLayoutHorizontalAlignment>::Slot(ELexLayoutHorizontalAlignment::Left)
						.Icon(FAppStyle::GetBrush("HorizontalAlignment_Left"))
						.ToolTip(LOCTEXT("AlignLeft", "Align Left"))
						+ SSegmentedControl<ELexLayoutHorizontalAlignment>::Slot(ELexLayoutHorizontalAlignment::Center)
						.Icon(FAppStyle::GetBrush("HorizontalAlignment_Center"))
						.ToolTip(LOCTEXT("AlignCenter", "Align Center"))
						+ SSegmentedControl<ELexLayoutHorizontalAlignment>::Slot(ELexLayoutHorizontalAlignment::Right)
						.Icon(FAppStyle::GetBrush("HorizontalAlignment_Right"))
						.ToolTip(LOCTEXT("AlignRight", "Align Right"))
					]
				]
			]
			+SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(FMargin(2, 0))
			[
				SNew(SBox)
				.WidthOverride(1)
				[
					SNew(SImage)
					.Image(FAppStyle::GetBrush("PropertyEditor.VerticalDottedLine"))
					.ColorAndOpacity(FLinearColor(1, 1, 1, 0.2f))
				]
			]
			+SHorizontalBox::Slot()
			.FillWidth(0.5f)
			[
				SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(FMargin(2, 0))
				[
					SNew(STextBlock)
					.Font(DetailBuilder->GetDetailFont())
					.Text(LOCTEXT("VerticalAlignment", "V"))
					.ToolTipText(LOCTEXT("VerticalAlignment_Tooltip", "VerticalAlignment"))
				]
				+SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNew(SBox)
					.IsEnabled_Lambda([=]
					{
						ELexLayoutDirection Direction;
						if (Direction_PH->GetValue(*(uint8*)&Direction) == FPropertyAccess::Success)
						{
							return Direction == ELexLayoutDirection::Vertical;
						}
						return false;
					})
					[
						SNew(SSegmentedControl<ELexLayoutVerticalAlignment>)
						.Value_Lambda([=]
						{
							uint8 Value;
							if (VerticalAlignment_PH->GetValue(Value) == FPropertyAccess::Success)
							{
								return ELexLayoutVerticalAlignment(Value);
							}
							return ELexLayoutVerticalAlignment::Bottom;
						})
						.OnValueChanged_Lambda([=](ELexLayoutVerticalAlignment NewState)
						{
							VerticalAlignment_PH->SetValue((uint8)NewState);
						})
						+ SSegmentedControl<ELexLayoutVerticalAlignment>::Slot(ELexLayoutVerticalAlignment::Bottom)
						.Icon(FAppStyle::GetBrush("VerticalAlignment_Bottom"))
						.ToolTip(LOCTEXT("AlignBottom", "Align Bottom"))
						+ SSegmentedControl<ELexLayoutVerticalAlignment>::Slot(ELexLayoutVerticalAlignment::Middle)
						.Icon(FAppStyle::GetBrush("VerticalAlignment_Center"))
						.ToolTip(LOCTEXT("AlignCenter", "Align Center"))
						+ SSegmentedControl<ELexLayoutVerticalAlignment>::Slot(ELexLayoutVerticalAlignment::Top)
						.Icon(FAppStyle::GetBrush("VerticalAlignment_Top"))
						.ToolTip(LOCTEXT("AlignTop", "Align Top"))
					]
				]
			]
		]
	]
	.PropertyHandleList({HorizontalAlignment_PH, VerticalAlignment_PH})
	.CopyAction(FUIAction(
		FExecuteAction::CreateSPLambda(this, [=]
		{
			uint8 HorizontalAlignment, VerticalAlignment;
			if (HorizontalAlignment_PH->GetValue(HorizontalAlignment) == FPropertyAccess::Success && VerticalAlignment_PH->GetValue(VerticalAlignment) == FPropertyAccess::Success)
			{
				FString CopiedText = FString::Printf(TEXT("%s, HorizontalAlignment=%d, VerticalAlignment=%d"), BeginClipboard_LexLayoutHorizontalAndVertical_Prefix, HorizontalAlignment, VerticalAlignment);
				FPlatformApplicationMisc::ClipboardCopy(*CopiedText);
			}
		})))
	.PasteAction(FUIAction(
		FExecuteAction::CreateSPLambda(this, [=]
		{
			FString PastedText;
			FPlatformApplicationMisc::ClipboardPaste(PastedText);
			if (PastedText.StartsWith(BeginClipboard_LexLayoutHorizontalAndVertical_Prefix))
			{
				uint8 tempUInt8;
				FParse::Value(*PastedText, TEXT("HorizontalAlignment="), tempUInt8);
				HorizontalAlignment_PH->SetValue(tempUInt8);
				FParse::Value(*PastedText, TEXT("VerticalAlignment="), tempUInt8);
				VerticalAlignment_PH->SetValue(tempUInt8);
			}
		}),
		FCanExecuteAction::CreateSPLambda(this, [=]
		{
			FString PastedText;
			FPlatformApplicationMisc::ClipboardPaste(PastedText);
			return PastedText.StartsWith(BeginClipboard_LexLayoutHorizontalAndVertical_Prefix);
		})))
	.OverrideResetToDefault(FResetToDefaultOverride::Create(
		TAttribute<bool>::CreateSPLambda(this, [=]
		{
			uint8 HorizontalAlignment, VerticalAlignment;
			if (HorizontalAlignment_PH->GetValue(HorizontalAlignment) == FPropertyAccess::Success && VerticalAlignment_PH->GetValue(VerticalAlignment) == FPropertyAccess::Success)
			{
				return HorizontalAlignment != (uint8)ELexLayoutHorizontalAlignment::Center || VerticalAlignment != (uint8)ELexLayoutVerticalAlignment::Middle;
			}
			return false;
		}), FSimpleDelegate::CreateSPLambda(this, [=]
		{
			FScopedTransaction Transaction(LOCTEXT("ResetValueToDefault", "Reset Value To Default"));
			for (auto Target : TargetObjects)
			{
				Target->Modify();
			}
			HorizontalAlignment_PH->SetValue((uint8)ELexLayoutHorizontalAlignment::Center);
			VerticalAlignment_PH->SetValue((uint8)ELexLayoutVerticalAlignment::Middle);
		})
	))
	;
	DetailBuilder->HideProperty(HorizontalAlignment_PH);
	DetailBuilder->HideProperty(VerticalAlignment_PH);

	DetailBuilder->RegisterInstancedCustomPropertyTypeLayout(FLexLayoutSpacing::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FLexLayoutSpacingCustomization::MakeInstance));
	auto Spacing_PH = DetailBuilder->GetProperty(GET_MEMBER_NAME_CHECKED(ULexLayoutHorizontalAndVertical, Spacing));
	Category.AddProperty(Spacing_PH);
}

UE_ENABLE_OPTIMIZATION
#undef LOCTEXT_NAMESPACE                                                                                                                                                                                                                     