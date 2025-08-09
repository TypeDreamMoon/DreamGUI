// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/LexUITextData.h"
#include "Core/LexUIGeometry.h"
#include "Core/Components/LexText.h"
#include "Core/LexUIRichTextImageData.h"
#include "Core/LexUIFontData_BaseObject.h"

FLexUITextGeometryCache::FLexUITextGeometryCache(ULexText* InUIText)
{
	this->UIText = InUIText;
}
bool FLexUITextGeometryCache::SetInputParameters(
	const FString& InContent,
	int32 InVisibleCharCount,
	float InWidth,
	float InHeight,
	FVector2f InPivot,
	FColor InColor,
	float InCanvasGroupAlpha,
	FVector2f InFontSpace,
	float InFontSize,
	ELexUITextParagraphHorizontalAlign InParagraphHAlign,
	ELexUITextParagraphVerticalAlign InParagraphVAlign,
	ELexUITextOverflowType InOverflowType,
	ETextWrappingPolicy InWrappingPolicy,
	float InMaxHorizontalWidth,
	bool InUseKerning,
	ELexUITextFontStyle InFontStyle,
	bool InRichText,
	int32 InRichTextFilterFlags,
	ULexUIFontData_BaseObject* InFont
)
{
	if (!this->content.Equals(InContent))
	{
		this->content = InContent;
		bIsDirty = true;
	}
	if (this->visibleCharCount != InVisibleCharCount)
	{
		this->visibleCharCount = InVisibleCharCount;
		bIsDirty = true;
	}
	if (this->width != InWidth)
	{
		this->width = InWidth;
		bIsDirty = true;
	}
	if (this->height != InHeight)
	{
		this->height = InHeight;
		bIsDirty = true;
	}
	if (this->pivot != InPivot)
	{
		this->pivot = InPivot;
		bIsDirty = true;
	}
	if (this->color != InColor)
	{
		this->color = InColor;
		bIsColorDirty = true;
	}
	if (this->richText != InRichText)
	{
		this->richText = InRichText;
		bIsDirty = true;
	}
	if (this->richTextFilterFlags != InRichTextFilterFlags)
	{
		this->richTextFilterFlags = InRichTextFilterFlags;
		bIsDirty = true;
	}
	if (this->canvasGroupAlpha != InCanvasGroupAlpha)
	{
		this->canvasGroupAlpha = InCanvasGroupAlpha;
		if (this->richText)//CanvasGroupAlpha only affect rich text alpha
		{
			bIsColorDirty = true;
		}
	}
	if (this->fontSpace != InFontSpace)
	{
		this->fontSpace = InFontSpace;
		bIsDirty = true;
	}
	if (this->fontSize != InFontSize)
	{
		this->fontSize = InFontSize;
		bIsDirty = true;
	}
	if (this->paragraphHAlign != InParagraphHAlign)
	{
		this->paragraphHAlign = InParagraphHAlign;
		bIsDirty = true;
	}
	if (this->paragraphVAlign != InParagraphVAlign)
	{
		this->paragraphVAlign = InParagraphVAlign;
		bIsDirty = true;
	}
	if (this->overflowType != InOverflowType)
	{
		this->overflowType = InOverflowType;
		bIsDirty = true;
	}
	if (this->WrappingPolicy != InWrappingPolicy)
	{
		this->WrappingPolicy = InWrappingPolicy;
		bIsDirty = true;
	}
	if (this->maxHorizontalWidth != InMaxHorizontalWidth)
	{
		this->maxHorizontalWidth = InMaxHorizontalWidth;
		bIsDirty = true;
	}
	if (this->useKerning != InUseKerning)
	{
		this->useKerning = InUseKerning;
		bIsDirty = true;
	}
	if (this->fontStyle != InFontStyle)
	{
		this->fontStyle = InFontStyle;
		bIsDirty = true;
	}
	if (this->font != InFont)
	{
		this->font = InFont;
		bIsDirty = true;
	}
	return bIsDirty;
}

void FLexUITextGeometryCache::MarkDirty()
{
	bIsDirty = true;
}

void FLexUITextGeometryCache::ConditionalCalculateGeometry()
{
	if (bIsColorDirty && !bIsDirty)
	{
		bIsColorDirty = false;
		FLexUIGeometry::UpdateUIColor(this->UIText->GetGeometry(), this->color);
	}
	else if (bIsDirty)
	{
		auto RenderCanvas = this->UIText->GetWidget()->GetRenderCanvas();
		if (!RenderCanvas)return;
		bIsDirty = false;
		bIsColorDirty = false;
		FLexUIGeometry::UpdateUIText(
			this->content
			, this->visibleCharCount
			, this->width
			, this->height
			, this->pivot
			, this->color
			, (uint8)(this->canvasGroupAlpha * 255)
			, this->fontSpace
			, this->UIText->GetGeometry()
			, this->fontSize
			, this->paragraphHAlign
			, this->paragraphVAlign
			, this->overflowType
			, this->WrappingPolicy
			, this->maxHorizontalWidth
			, this->useKerning
			, this->fontStyle
			, this->textRealSize
			, this->textPreferredSize
			, RenderCanvas
			, this->UIText.Get()
			, this->cacheLinePropertyArray
			, this->cacheCharPropertyArray
			, this->cacheRichTextCustomTagArray
			, this->cacheRichTextImageTagArray
			, this->font.Get()
			, this->richText
			, this->richTextFilterFlags
			);

		auto& vertices = this->UIText->GetGeometry()->Vertices;
		auto FontTextureMark = this->font->GetFontTextureMark();
		for (int i = 0; i < vertices.Num(); i++)
		{
			vertices[i].TextureCoordinate[1].Y = FontTextureMark;//Mark UV1.Y so material will know it should use FontTexture 
		}
		
		this->UIText->GenerateRichTextImageObject();
	}
}

