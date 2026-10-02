// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/DreamUIRichTextCustomStyleData.h"
#include "DreamGUI.h"

namespace DreamUIRichTextCustomStyleLocal
{
	/**
	 * Whether a style opened at InTagOrder may set a property last set at InOutOrder. A tag opened later is nested
	 * deeper, and the innermost tag wins; when this one does, it becomes the property's setter.
	 */
	bool TakeProperty(int32& InOutOrder, int32 InTagOrder)
	{
		if (InOutOrder >= InTagOrder)
		{
			return false;
		}
		InOutOrder = InTagOrder;
		return true;
	}

	void ApplyBool(EDreamUIRichTextCustomStyleData_BoolType Type, bool& InOutValue, int32& InOutOrder, int32 InTagOrder)
	{
		switch (Type)
		{
		case EDreamUIRichTextCustomStyleData_BoolType::On:
			if (TakeProperty(InOutOrder, InTagOrder))
			{
				InOutValue = true;
			}
			break;
		case EDreamUIRichTextCustomStyleData_BoolType::Off:
			if (TakeProperty(InOutOrder, InTagOrder))
			{
				InOutValue = false;
			}
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

void FDreamUIRichTextCustomStyleItemData::ApplyToRichTextParseResult(DreamUIRichTextParser::FRichTextParseResult& value, int32 InTagOrder)const
{
	using namespace DreamUIRichTextCustomStyleLocal;
	ApplyBool(this->boldType, value.Bold, value.BoldOrder, InTagOrder);
	ApplyBool(this->italicType, value.Italic, value.ItalicOrder, InTagOrder);
	ApplyBool(this->underlineType, value.Underline, value.UnderlineOrder, InTagOrder);
	ApplyBool(this->strikethroughType, value.Strikethrough, value.StrikethroughOrder, InTagOrder);
	switch (this->sizeType)
	{
	default:
	case EDreamUIRichTextCustomStyleData_SizeType::KeepOrigin:
		break;
	case EDreamUIRichTextCustomStyleData_SizeType::SizeValue:
		if (TakeProperty(value.SizeOrder, InTagOrder))
		{
			value.Size = this->size;
		}
		break;
	case EDreamUIRichTextCustomStyleData_SizeType::SizeValueAsAdditional:
		if (TakeProperty(value.SizeOrder, InTagOrder))
		{
			value.Size += this->size;
		}
		break;
	}
	switch (this->colorType)
	{
	default:
	case EDreamUIRichTextCustomStyleData_ColorType::KeepOrigin:
		break;
	case EDreamUIRichTextCustomStyleData_ColorType::Replace:
		if (TakeProperty(value.ColorOrder, InTagOrder))
		{
			// A colour of the style's own, which the painter draws instead of the text's colour; a Multiply still
			// waiting for the text's colour no longer has anything to multiply.
			value.Color = this->color;
			value.HasColor = true;
			value.bHasMultiplyColor = false;
			value.MultiplyColor = FColor::White;
		}
		break;
	case EDreamUIRichTextCustomStyleData_ColorType::Multiply:
		if (TakeProperty(value.ColorOrder, InTagOrder))
		{
			if (value.HasColor)
			{
				value.Color = FDreamUIUtils::MultiplyColor(value.Color, this->color);
			}
			else
			{
				// The text's own colour is a paint input (fading it must not cost a layout), so the product is
				// taken when the glyph is painted.
				value.MultiplyColor = FDreamUIUtils::MultiplyColor(value.MultiplyColor, this->color);
				value.bHasMultiplyColor = true;
			}
		}
		break;
	}
	switch (this->supOrSub)
	{
	default:
	case EDreamUIRichTextCustomStyleData_SupOrSubType::KeepOrigin:
		break;
	case EDreamUIRichTextCustomStyleData_SupOrSubType::None:
		if (TakeProperty(value.SupOrSubOrder, InTagOrder))
		{
			DreamUIRichTextParser::ApplySupOrSub(value, DreamUIRichTextParser::ESupOrSubMode::None);
		}
		break;
	case EDreamUIRichTextCustomStyleData_SupOrSubType::Superscript:
		if (TakeProperty(value.SupOrSubOrder, InTagOrder))
		{
			DreamUIRichTextParser::ApplySupOrSub(value, DreamUIRichTextParser::ESupOrSubMode::Sup);
		}
		break;
	case EDreamUIRichTextCustomStyleData_SupOrSubType::Subscript:
		if (TakeProperty(value.SupOrSubOrder, InTagOrder))
		{
			DreamUIRichTextParser::ApplySupOrSub(value, DreamUIRichTextParser::ESupOrSubMode::Sub);
		}
		break;
	}
}

void FDreamUIRichTextCustomStyleItemData::AppendSizeEffects(int32 InTagOrder, TArray<DreamUIRichTextParser::FSizeEffect>& OutEffects)const
{
	using namespace DreamUIRichTextParser;
	if (this->sizeType != EDreamUIRichTextCustomStyleData_SizeType::KeepOrigin)
	{
		FSizeEffect& Effect = OutEffects.AddDefaulted_GetRef();
		Effect.Order = InTagOrder;
		Effect.bSetsSize = true;
		Effect.bAdditional = this->sizeType == EDreamUIRichTextCustomStyleData_SizeType::SizeValueAsAdditional;
		Effect.Size = (float)this->size;
	}
	if (this->supOrSub != EDreamUIRichTextCustomStyleData_SupOrSubType::KeepOrigin)
	{
		FSizeEffect& Effect = OutEffects.AddDefaulted_GetRef();
		Effect.Order = InTagOrder;
		Effect.SupOrSub = this->supOrSub == EDreamUIRichTextCustomStyleData_SupOrSubType::Superscript ? ESupOrSubMode::Sup
			: (this->supOrSub == EDreamUIRichTextCustomStyleData_SupOrSubType::Subscript ? ESupOrSubMode::Sub : ESupOrSubMode::None);
	}
}

void UDreamUIRichTextCustomStyleData::SetDataMap(const TMap<FName, FDreamUIRichTextCustomStyleItemData>& InDataMap)
{
	DataMap = InDataMap;
	for (auto& Pair : DataMap)
	{
		Pair.Value.UpgradeLegacyBools();
	}
	OnDataChange.Broadcast();
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
