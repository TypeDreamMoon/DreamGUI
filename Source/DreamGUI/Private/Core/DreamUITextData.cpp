// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/DreamUITextData.h"
#include "Core/DreamUIGeometry.h"
#include "Core/Components/DreamText.h"
#include "Core/DreamUIFontData_BaseObject.h"
#include "Core/Components/DreamWidget.h"

FDreamUITextGeometryCache::FDreamUITextGeometryCache(UDreamText* InUIText)
{
	this->TextComp = InUIText;
}
bool FDreamUITextGeometryCache::SetInputParameters(
	const FString& InContent,
	float InWidth,
	float InHeight,
	FVector2f InPivot,
	FColor InColor,
	float InRenderOpacityForRichText,
	FVector2f InFontSpace,
	float InFontSize,
	EDreamUITextParagraphHorizontalAlign InParagraphHAlign,
	EDreamUITextParagraphVerticalAlign InParagraphVAlign,
	EDreamUITextOverflowType InOverflowType,
	ETextWrappingPolicy InWrappingPolicy,
	bool InUseKerning,
	EDreamUITextFontStyle InFontStyle,
	bool InRichText,
	int32 InRichTextFilterFlags,
	UDreamUIFontData_BaseObject* InFont
)
{
	if (!this->Content.Equals(InContent))
	{
		this->Content = InContent;
		bIsDirty = true;
	}
	if (this->Width != InWidth)
	{
		this->Width = InWidth;
		bIsDirty = true;
	}
	if (this->Height != InHeight)
	{
		this->Height = InHeight;
		bIsDirty = true;
	}
	if (this->Pivot != InPivot)
	{
		this->Pivot = InPivot;
		bIsDirty = true;
	}
	if (this->Color != InColor)
	{
		this->Color = InColor;
		bIsColorDirty = true;
	}
	if (this->bRichText != InRichText)
	{
		this->bRichText = InRichText;
		bIsDirty = true;
	}
	if (this->RichTextFilterFlags != InRichTextFilterFlags)
	{
		this->RichTextFilterFlags = InRichTextFilterFlags;
		bIsDirty = true;
	}
	if (this->RenderOpacityForRichText != InRenderOpacityForRichText)
	{
		this->RenderOpacityForRichText = InRenderOpacityForRichText;
		if (this->bRichText)
		{
			bIsColorDirty = true;
		}
	}
	if (this->FontSpace != InFontSpace)
	{
		this->FontSpace = InFontSpace;
		bIsDirty = true;
	}
	if (this->FontSize != InFontSize)
	{
		this->FontSize = InFontSize;
		bIsDirty = true;
	}
	if (this->ParagraphHAlign != InParagraphHAlign)
	{
		this->ParagraphHAlign = InParagraphHAlign;
		bIsDirty = true;
	}
	if (this->ParagraphVAlign != InParagraphVAlign)
	{
		this->ParagraphVAlign = InParagraphVAlign;
		bIsDirty = true;
	}
	if (this->OverflowType != InOverflowType)
	{
		this->OverflowType = InOverflowType;
		bIsDirty = true;
	}
	if (this->WrappingPolicy != InWrappingPolicy)
	{
		this->WrappingPolicy = InWrappingPolicy;
		bIsDirty = true;
	}
	if (this->bUseKerning != InUseKerning)
	{
		this->bUseKerning = InUseKerning;
		bIsDirty = true;
	}
	if (this->FontStyle != InFontStyle)
	{
		this->FontStyle = InFontStyle;
		bIsDirty = true;
	}
	if (this->Font != InFont)
	{
		this->Font = InFont;
		bIsDirty = true;
	}
	return bIsDirty;
}

void FDreamUITextGeometryCache::MarkDirty()
{
	bIsDirty = true;
}

void FDreamUITextGeometryCache::ConditionalCalculateGeometry()
{
	if (bIsColorDirty && !bIsDirty)
	{
		bIsColorDirty = false;
		FDreamUIGeometry::UpdateUIColor(this->TextComp->GetGeometry(), this->Color);
	}
	else if (bIsDirty)
	{
		auto RenderCanvas = this->TextComp->GetWidget()->GetRenderCanvas();
		if (!RenderCanvas)return;
		
		bIsDirty = false;
		bIsColorDirty = false;
		FDreamUIGeometry::UpdateUIText(
			this->Content
			, this->TextProcessingArray
			, this->Width
			, this->Height
			, this->Pivot
			, this->Color
			, (uint8)(this->RenderOpacityForRichText * 255)
			, this->FontSpace
			, this->TextComp->GetGeometry()
			, this->FontSize
			, this->ParagraphHAlign
			, this->ParagraphVAlign
			, this->OverflowType
			, this->WrappingPolicy
			, this->bUseKerning
			, this->FontStyle
			, this->textPreferredSize
			, this->textTruncated
			, RenderCanvas
			, this->TextComp.Get()
			, this->cacheLinePropertyArray
			, this->cacheCharPropertyArray
			, this->cacheRichTextCustomTagArray
			, this->cacheRichTextImageTagArray
			, this->cacheEmojiArray
			, this->Font.Get()
			, this->bRichText
			, this->RichTextFilterFlags
			);
		
		this->TextComp->GenerateRichTextImageObject();
		this->TextComp->GenerateEmojiObject();
	}
}

