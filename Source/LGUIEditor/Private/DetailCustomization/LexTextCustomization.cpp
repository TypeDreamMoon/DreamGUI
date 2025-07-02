// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DetailCustomization/LexTextCustomization.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Core/Components/LexText.h"

#include "LGUIEditorModule.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "IDetailGroup.h"

#define LOCTEXT_NAMESPACE "UITextCustomization"
FLexTextCustomization::FLexTextCustomization()
{
}

FLexTextCustomization::~FLexTextCustomization()
{
}

TSharedRef<IDetailCustomization> FLexTextCustomization::MakeInstance()
{
	return MakeShareable(new FLexTextCustomization);
}
void FLexTextCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> targetObjects;
	DetailBuilder.GetObjectsBeingCustomized(targetObjects);
	TargetScriptPtr = Cast<ULexText>(targetObjects[0].Get());
	if (TargetScriptPtr == nullptr)
	{
		UE_LOG(LGUIEditor, Log, TEXT("[UITextCustomization]Get TargetScript is null"));
		return;
	}
	
	IDetailCategoryBuilder& category = DetailBuilder.EditCategory("LGUI");
	category.AddProperty(GET_MEMBER_NAME_CHECKED(ULexText, font));
	category.AddProperty(GET_MEMBER_NAME_CHECKED(ULexText, text));

	category.AddProperty(GET_MEMBER_NAME_CHECKED(ULexText, size));
	category.AddProperty(GET_MEMBER_NAME_CHECKED(ULexText, space));

	//text alignment
	{
		DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(ULexText, hAlign));
		DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(ULexText, vAlign));
		const FMargin OuterPadding(2, 0);
		const FMargin ContentPadding(2);
		auto hAlignPropertyHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexText, hAlign));
		auto vAlignPropertyHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexText, vAlign));
		category.AddCustomRow(LOCTEXT("Alignment", "Alignment"))
		.PropertyHandleList({ hAlignPropertyHandle, vAlignPropertyHandle })
		.CopyAction(FUIAction(
			FExecuteAction::CreateSP(this, &FLexTextCustomization::OnCopyAlignment)
		))
		.PasteAction(FUIAction(
			FExecuteAction::CreateSP(this, &FLexTextCustomization::OnPasteAlignment, hAlignPropertyHandle, vAlignPropertyHandle),
			FCanExecuteAction::CreateSP(this, &FLexTextCustomization::OnCanPasteAlignment)
		))
		.NameContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("Alignment", "Alignment"))
			.Font(IDetailLayoutBuilder::GetDetailFont())
		]
		.ValueContent()
		[
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(OuterPadding)
				[
					SNew( SCheckBox )
					.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
					.ToolTipText(LOCTEXT("AlignTextLeft", "Align Text Left"))
					.Padding(ContentPadding)
					.OnCheckStateChanged(this, &FLexTextCustomization::HandleHorizontalAlignmentCheckStateChanged, hAlignPropertyHandle, EUITextParagraphHorizontalAlign::Left)
					.IsChecked(this, &FLexTextCustomization::GetHorizontalAlignmentCheckState, hAlignPropertyHandle, EUITextParagraphHorizontalAlign::Left)
					[
						SNew(SImage)
						.Image(FAppStyle::GetBrush("HorizontalAlignment_Left"))
					]
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(OuterPadding)
				[
					SNew(SCheckBox)
					.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
					.ToolTipText(LOCTEXT("AlignTextCenter", "Align Text Center"))
					.Padding(ContentPadding)
					.OnCheckStateChanged(this, &FLexTextCustomization::HandleHorizontalAlignmentCheckStateChanged, hAlignPropertyHandle, EUITextParagraphHorizontalAlign::Center)
					.IsChecked(this, &FLexTextCustomization::GetHorizontalAlignmentCheckState, hAlignPropertyHandle, EUITextParagraphHorizontalAlign::Center)
					[
						SNew(SImage)
						.Image(FAppStyle::GetBrush("HorizontalAlignment_Center"))
					]
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(OuterPadding)
				[
					SNew(SCheckBox)
					.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
					.ToolTipText(LOCTEXT("AlignTextRight", "Align Text Right"))
					.Padding(ContentPadding)
					.OnCheckStateChanged(this, &FLexTextCustomization::HandleHorizontalAlignmentCheckStateChanged, hAlignPropertyHandle, EUITextParagraphHorizontalAlign::Right)
					.IsChecked(this, &FLexTextCustomization::GetHorizontalAlignmentCheckState, hAlignPropertyHandle, EUITextParagraphHorizontalAlign::Right)
					[
						SNew(SImage)
						.Image(FAppStyle::GetBrush("HorizontalAlignment_Right"))
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
			.AutoWidth()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(OuterPadding)
				[
					SNew( SCheckBox )
					.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
					.ToolTipText(LOCTEXT("VAlignTop", "Vertically Align Top"))
					.Padding(ContentPadding)
					.OnCheckStateChanged(this, &FLexTextCustomization::HandleVerticalAlignmentCheckStateChanged, vAlignPropertyHandle, EUITextParagraphVerticalAlign::Top)
					.IsChecked(this, &FLexTextCustomization::GetVerticalAlignmentCheckState, vAlignPropertyHandle, EUITextParagraphVerticalAlign::Top)
					[
						SNew(SImage)
						.Image(FAppStyle::GetBrush("VerticalAlignment_Top"))
					]
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(OuterPadding)
				[
					SNew(SCheckBox)
					.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
					.ToolTipText(LOCTEXT("VAlignMiddle", "Vertically Align Middle"))
					.Padding(ContentPadding)
					.OnCheckStateChanged(this, &FLexTextCustomization::HandleVerticalAlignmentCheckStateChanged, vAlignPropertyHandle, EUITextParagraphVerticalAlign::Middle)
					.IsChecked(this, &FLexTextCustomization::GetVerticalAlignmentCheckState, vAlignPropertyHandle, EUITextParagraphVerticalAlign::Middle)
					[
						SNew(SImage)
						.Image(FAppStyle::GetBrush("VerticalAlignment_Center"))
					]
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(OuterPadding)
				[
					SNew(SCheckBox)
					.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
					.ToolTipText(LOCTEXT("VAlignBottom", "Vertically Align Bottom"))
					.Padding(ContentPadding)
					.OnCheckStateChanged(this, &FLexTextCustomization::HandleVerticalAlignmentCheckStateChanged, vAlignPropertyHandle, EUITextParagraphVerticalAlign::Bottom)
					.IsChecked(this, &FLexTextCustomization::GetVerticalAlignmentCheckState, vAlignPropertyHandle, EUITextParagraphVerticalAlign::Bottom)
					[
						SNew(SImage)
						.Image(FAppStyle::GetBrush("VerticalAlignment_Bottom"))
					]
				]
			]
		]
		.OverrideResetToDefault(FResetToDefaultOverride::Create(
			TAttribute<bool>::CreateLambda([=]
				{
					return hAlignPropertyHandle->CanResetToDefault() || vAlignPropertyHandle->CanResetToDefault();
				}),
				FSimpleDelegate::CreateLambda([=]() 
				{
					hAlignPropertyHandle->ResetToDefault(); vAlignPropertyHandle->ResetToDefault();
				})
		))
		;
	}

	auto OverflowTypeHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexText, overflowType));
	OverflowTypeHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FLexTextCustomization::ForceRefresh, &DetailBuilder));
	auto AdjustWidthHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexText, adjustWidth));
	AdjustWidthHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FLexTextCustomization::ForceRefresh, &DetailBuilder));
	auto AdjustHeightHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexText, adjustHeight));
	AdjustHeightHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FLexTextCustomization::ForceRefresh, &DetailBuilder));

	EUITextOverflowType OverflowType;
	OverflowTypeHandle->GetValue(*(uint8*)&OverflowType);
	TArray<FName> needToHidePropertyName;
	if (OverflowType == EUITextOverflowType::HorizontalOverflow)
	{
		needToHidePropertyName.Add(GET_MEMBER_NAME_CHECKED(ULexText, adjustHeight));
		needToHidePropertyName.Add(GET_MEMBER_NAME_CHECKED(ULexText, adjustHeightRange));
		needToHidePropertyName.Add(GET_MEMBER_NAME_CHECKED(ULexText, maxHorizontalWidth));
		bool AdjustWidth;
		AdjustWidthHandle->GetValue(AdjustWidth);
		if (!AdjustWidth)
		{
			needToHidePropertyName.Add(GET_MEMBER_NAME_CHECKED(ULexText, adjustWidthRange));
		}
	}
	else if (OverflowType == EUITextOverflowType::VerticalOverflow)
	{
		needToHidePropertyName.Add(GET_MEMBER_NAME_CHECKED(ULexText, adjustWidth));
		needToHidePropertyName.Add(GET_MEMBER_NAME_CHECKED(ULexText, adjustWidthRange));
		needToHidePropertyName.Add(GET_MEMBER_NAME_CHECKED(ULexText, maxHorizontalWidth));
		bool AdjustHeight;
		AdjustHeightHandle->GetValue(AdjustHeight);
		if (!AdjustHeight)
		{
			needToHidePropertyName.Add(GET_MEMBER_NAME_CHECKED(ULexText, adjustHeightRange));
		}
	}
	else if (OverflowType == EUITextOverflowType::HorizontalAndVerticalOverflow)
	{
		bool AdjustWidth;
		AdjustWidthHandle->GetValue(AdjustWidth);
		if (!AdjustWidth)
		{
			needToHidePropertyName.Add(GET_MEMBER_NAME_CHECKED(ULexText, adjustWidthRange));
		}
		bool AdjustHeight;
		AdjustHeightHandle->GetValue(AdjustHeight);
		if (!AdjustHeight)
		{
			needToHidePropertyName.Add(GET_MEMBER_NAME_CHECKED(ULexText, adjustHeightRange));
		}
	}
	else
	{
		needToHidePropertyName.Add(GET_MEMBER_NAME_CHECKED(ULexText, adjustHeight));
		needToHidePropertyName.Add(GET_MEMBER_NAME_CHECKED(ULexText, adjustWidth));
		needToHidePropertyName.Add(GET_MEMBER_NAME_CHECKED(ULexText, adjustHeightRange));
		needToHidePropertyName.Add(GET_MEMBER_NAME_CHECKED(ULexText, adjustWidthRange));
		needToHidePropertyName.Add(GET_MEMBER_NAME_CHECKED(ULexText, maxHorizontalWidth));
	}

	auto RichTextHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexText, richText));
	RichTextHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FLexTextCustomization::ForceRefresh, &DetailBuilder));
	bool richText = false;
	RichTextHandle->GetValue(richText);
	if (richText)
	{
		IDetailGroup& RichTextGroup = category.AddGroup(FName("RichText"), RichTextHandle->GetPropertyDisplayName());
		RichTextGroup.HeaderProperty(RichTextHandle);
		RichTextGroup.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexText, richTextTagFilterFlags)));
		RichTextGroup.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexText, richTextCustomStyleData)));
		RichTextGroup.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexText, richTextImageData)));
		RichTextGroup.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexText, listRichTextImageObjectInOutliner)));
		RichTextGroup.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexText, createdRichTextImageObjectArray)));
	}
	else
	{
		category.AddProperty(RichTextHandle);
		needToHidePropertyName.Add(GET_MEMBER_NAME_CHECKED(ULexText, richTextCustomStyleData));
		needToHidePropertyName.Add(GET_MEMBER_NAME_CHECKED(ULexText, richTextImageData));
		needToHidePropertyName.Add(GET_MEMBER_NAME_CHECKED(ULexText, listRichTextImageObjectInOutliner));
		needToHidePropertyName.Add(GET_MEMBER_NAME_CHECKED(ULexText, createdRichTextImageObjectArray));
		needToHidePropertyName.Add(GET_MEMBER_NAME_CHECKED(ULexText, richTextTagFilterFlags));
	}

	for (auto item : needToHidePropertyName)
	{
		DetailBuilder.HideProperty(item);
	}

	auto fontHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexText, font));
	fontHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([=, this] {
		TargetScriptPtr->OnPostChangeFontProperty();
	}));
	fontHandle->SetOnPropertyValuePreChange(FSimpleDelegate::CreateLambda([=, this]{
		TargetScriptPtr->OnPreChangeFontProperty();
	}));

	auto richTextImageDataHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexText, richTextImageData));
	richTextImageDataHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([=, this] {
		TargetScriptPtr->OnPostChangeRichTextImageDataProperty();
		}));
	richTextImageDataHandle->SetOnPropertyValuePreChange(FSimpleDelegate::CreateLambda([=, this] {
		TargetScriptPtr->OnPreChangeRichTextImageDataProperty();
		}));

	auto richTextCustomStyleDataHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexText, richTextCustomStyleData));
	richTextCustomStyleDataHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([=, this] {
		TargetScriptPtr->OnPostChangeRichTextCustomStyleDataProperty();
		}));
	richTextCustomStyleDataHandle->SetOnPropertyValuePreChange(FSimpleDelegate::CreateLambda([=, this] {
		TargetScriptPtr->OnPreChangeRichTextCustomStyleDataProperty();
		}));
}
void FLexTextCustomization::ForceRefresh(IDetailLayoutBuilder* DetailBuilder)
{
	if (auto Script = TargetScriptPtr.Get())
	{
		DetailBuilder->ForceRefreshDetails();
	}
}
void FLexTextCustomization::HandleHorizontalAlignmentCheckStateChanged(ECheckBoxState InCheckboxState, TSharedRef<IPropertyHandle> PropertyHandle, EUITextParagraphHorizontalAlign ToAlignment)
{
	PropertyHandle->SetValue((uint8)ToAlignment);
}
void FLexTextCustomization::HandleVerticalAlignmentCheckStateChanged(ECheckBoxState InCheckboxState, TSharedRef<IPropertyHandle> PropertyHandle, EUITextParagraphVerticalAlign ToAlignment)
{
	PropertyHandle->SetValue((uint8)ToAlignment);
}
ECheckBoxState FLexTextCustomization::GetHorizontalAlignmentCheckState(TSharedRef<IPropertyHandle> PropertyHandle, EUITextParagraphHorizontalAlign ForAlignment) const
{
	uint8 Value;
	if (PropertyHandle->GetValue(Value) == FPropertyAccess::Result::Success)
	{
		return Value == (uint8)ForAlignment ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	}

	return ECheckBoxState::Unchecked;
}
ECheckBoxState FLexTextCustomization::GetVerticalAlignmentCheckState(TSharedRef<IPropertyHandle> PropertyHandle, EUITextParagraphVerticalAlign ForAlignment) const
{
	uint8 Value;
	if (PropertyHandle->GetValue(Value) == FPropertyAccess::Result::Success)
	{
		return Value == (uint8)ForAlignment ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	}

	return ECheckBoxState::Unchecked;
}

#define BEGIN_ALIGNMENT_CLIPBOARD TEXT("Begin LGUI UIWidget")
void FLexTextCustomization::OnCopyAlignment()
{
	if (TargetScriptPtr.IsValid())
	{
		FString CopiedText = FString::Printf(TEXT("%s, hAlign=%d, vAlign=%d"), BEGIN_ALIGNMENT_CLIPBOARD, (int)TargetScriptPtr->hAlign, (int)TargetScriptPtr->vAlign);
		FPlatformApplicationMisc::ClipboardCopy(*CopiedText);
	}
}
void FLexTextCustomization::OnPasteAlignment(TSharedRef<IPropertyHandle> HAlignPropertyHandle, TSharedRef<IPropertyHandle> VAlignPropertyHandle)
{
	FString PastedText;
	FPlatformApplicationMisc::ClipboardPaste(PastedText);
	if (PastedText.StartsWith(BEGIN_ALIGNMENT_CLIPBOARD))
	{
		uint8 tempUInt8;
		FParse::Value(*PastedText, TEXT("hAlign="), tempUInt8);
		HAlignPropertyHandle->SetValue(tempUInt8);
		FParse::Value(*PastedText, TEXT("vAlign="), tempUInt8);
		VAlignPropertyHandle->SetValue(tempUInt8);
	}
}
bool FLexTextCustomization::OnCanPasteAlignment()const
{
	FString PastedText;
	FPlatformApplicationMisc::ClipboardPaste(PastedText);
	return PastedText.StartsWith(BEGIN_ALIGNMENT_CLIPBOARD);
}
#undef LOCTEXT_NAMESPACE