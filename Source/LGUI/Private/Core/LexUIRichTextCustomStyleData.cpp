// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/LexUIRichTextCustomStyleData.h"
#include "LGUI.h"

void FLexUIRichTextCustomStyleItemData::ApplyToRichTextParseResult(LexUIRichTextParser::FRichTextParseResult& value)const
{
	value.Bold = this->bold;
	value.Italic = this->italic;
	value.Underline = this->underline;
	value.Strikethrough = this->strikethrough;
	switch (this->sizeType)
	{
	default:
	case ELexUIRichTextCustomStyleData_SizeType::KeepOrigin:
		break;
	case ELexUIRichTextCustomStyleData_SizeType::SizeValue:
		value.Size = this->size;
		break;
	case ELexUIRichTextCustomStyleData_SizeType::SizeValueAsAdditional:
		value.Size += this->size;
		break;
	}
	switch (this->colorType)
	{
	default:
	case ELexUIRichTextCustomStyleData_ColorType::KeepOrigin:
		break;
	case ELexUIRichTextCustomStyleData_ColorType::Replace:
		value.Color = this->color;
		break;
	case ELexUIRichTextCustomStyleData_ColorType::Multiply:
		value.Color = FLexUIUtils::MultiplyColor(value.Color, this->color);
		break;
	}
	switch (this->supOrSub)
	{
	default:
	case ELexUIRichTextCustomStyleData_SupOrSubType::KeepOrigin:
		break;
	case ELexUIRichTextCustomStyleData_SupOrSubType::None:
		value.SupOrSubMode = LexUIRichTextParser::ESupOrSubMode::None;
		break;
	case ELexUIRichTextCustomStyleData_SupOrSubType::Superscript:
		value.SupOrSubMode = LexUIRichTextParser::ESupOrSubMode::Sup;
		value.Size *= 0.8f;
		break;
	case ELexUIRichTextCustomStyleData_SupOrSubType::Subscript:
		value.SupOrSubMode = LexUIRichTextParser::ESupOrSubMode::Sub;
		value.Size *= 0.8f;
		break;
	}
}

#if WITH_EDITOR
void ULexUIRichTextCustomStyleData::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	OnDataChange.Broadcast();
}
#endif
