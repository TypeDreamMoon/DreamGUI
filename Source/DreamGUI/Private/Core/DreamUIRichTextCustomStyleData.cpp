// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/DreamUIRichTextCustomStyleData.h"
#include "DreamGUI.h"

void FDreamUIRichTextCustomStyleItemData::ApplyToRichTextParseResult(DreamUIRichTextParser::FRichTextParseResult& value)const
{
	value.Bold = this->bold;
	value.Italic = this->italic;
	value.Underline = this->underline;
	value.Strikethrough = this->strikethrough;
	switch (this->sizeType)
	{
	default:
	case EDreamUIRichTextCustomStyleData_SizeType::KeepOrigin:
		break;
	case EDreamUIRichTextCustomStyleData_SizeType::SizeValue:
		value.Size = this->size;
		break;
	case EDreamUIRichTextCustomStyleData_SizeType::SizeValueAsAdditional:
		value.Size += this->size;
		break;
	}
	switch (this->colorType)
	{
	default:
	case EDreamUIRichTextCustomStyleData_ColorType::KeepOrigin:
		break;
	case EDreamUIRichTextCustomStyleData_ColorType::Replace:
		value.Color = this->color;
		break;
	case EDreamUIRichTextCustomStyleData_ColorType::Multiply:
		value.Color = FDreamUIUtils::MultiplyColor(value.Color, this->color);
		break;
	}
	switch (this->supOrSub)
	{
	default:
	case EDreamUIRichTextCustomStyleData_SupOrSubType::KeepOrigin:
		break;
	case EDreamUIRichTextCustomStyleData_SupOrSubType::None:
		value.SupOrSubMode = DreamUIRichTextParser::ESupOrSubMode::None;
		break;
	case EDreamUIRichTextCustomStyleData_SupOrSubType::Superscript:
		value.SupOrSubMode = DreamUIRichTextParser::ESupOrSubMode::Sup;
		value.Size *= 0.8f;
		break;
	case EDreamUIRichTextCustomStyleData_SupOrSubType::Subscript:
		value.SupOrSubMode = DreamUIRichTextParser::ESupOrSubMode::Sub;
		value.Size *= 0.8f;
		break;
	}
}

#if WITH_EDITOR
void UDreamUIRichTextCustomStyleData::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	OnDataChange.Broadcast();
}
#endif
