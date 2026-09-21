// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/DreamUIRichTextCustomStyleData.h"
#include "DreamGUI.h"

namespace DreamUIRichTextCustomStyleLocal
{
	void ApplyBool(EDreamUIRichTextCustomStyleData_BoolType Type, bool& InOutValue)
	{
		switch (Type)
		{
		case EDreamUIRichTextCustomStyleData_BoolType::On:
			InOutValue = true;
			break;
		case EDreamUIRichTextCustomStyleData_BoolType::Off:
			InOutValue = false;
			break;
		default:
			break;//KeepOrigin: a tag inside a <b> stays bold
		}
	}
}

bool FDreamUIRichTextCustomStyleItemData::UpgradeLegacyBools()
{
	// A set legacy bool can only have come from an asset saved before the enums existed, because the
	// details panel no longer shows them. False maps to KeepOrigin, which is the whole point: the old
	// code forced all four flags off when they were unset.
	bool bChanged = false;
	auto Upgrade = [&bChanged](bool& InOutLegacy, EDreamUIRichTextCustomStyleData_BoolType& InOutType)
	{
		if (InOutLegacy)
		{
			InOutLegacy = false;
			if (InOutType == EDreamUIRichTextCustomStyleData_BoolType::KeepOrigin)
			{
				InOutType = EDreamUIRichTextCustomStyleData_BoolType::On;
			}
			bChanged = true;
		}
	};
	Upgrade(this->bold, this->boldType);
	Upgrade(this->italic, this->italicType);
	Upgrade(this->underline, this->underlineType);
	Upgrade(this->strikethrough, this->strikethroughType);
	return bChanged;
}

void FDreamUIRichTextCustomStyleItemData::ApplyToRichTextParseResult(DreamUIRichTextParser::FRichTextParseResult& value)const
{
	using namespace DreamUIRichTextCustomStyleLocal;
	ApplyBool(this->boldType, value.Bold);
	ApplyBool(this->italicType, value.Italic);
	ApplyBool(this->underlineType, value.Underline);
	ApplyBool(this->strikethroughType, value.Strikethrough);
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

void UDreamUIRichTextCustomStyleData::PostLoad()
{
	Super::PostLoad();
	for (auto& Pair : DataMap)
	{
		Pair.Value.UpgradeLegacyBools();
	}
}

#if WITH_EDITOR
void UDreamUIRichTextCustomStyleData::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	OnDataChange.Broadcast();
}
#endif
