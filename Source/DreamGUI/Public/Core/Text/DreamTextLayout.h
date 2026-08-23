// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Text/TextLayout.h"
#include "Core/DreamUITextData.h"
#include "Core/Text/DreamTextDisplayList.h"

class UDreamUIFontData_BaseObject;
class UDreamUIRichTextImageData_BaseObject;
class UDreamUIRichTextCustomStyleData;
class UDreamUIFontEmojiData;

/**
 * Every input the layout reads, as plain values. No widget, no canvas: what those used to supply
 * (root canvas scale, pixel snapping, world-space mode) is copied in here by the caller, so a layout
 * can be run -- and tested -- without either.
 *
 * Equality is what the geometry cache uses to decide whether a layout is stale.
 */
struct DREAMGUI_API FDreamTextLayoutInput
{
	FString Content;
	/** Content box, i.e. the widget's rect minus its margin, and the pivot the box is described by. */
	float Width = 0.0f;
	float Height = 0.0f;
	FVector2f Pivot = FVector2f::ZeroVector;
	/** The text's own colour. Rich-text <color> tags are parsed against it; untagged glyphs take it at paint time. */
	FColor Color = FColor::White;
	/** Render opacity the rich-text parser applies to tag colours. Ignored without rich text. */
	uint8 RenderOpacityForRichText = 255;
	FVector2f FontSpace = FVector2f::ZeroVector;
	float FontSize = 16.0f;
	EDreamUITextParagraphHorizontalAlign ParagraphHAlign = EDreamUITextParagraphHorizontalAlign::Left;
	EDreamUITextParagraphVerticalAlign ParagraphVAlign = EDreamUITextParagraphVerticalAlign::Bottom;
	EDreamUITextOverflowType OverflowType = EDreamUITextOverflowType::HorizontalOverflow;
	ETextWrappingPolicy WrappingPolicy = ETextWrappingPolicy::AllowPerCharacterWrapping;
	EDreamTextPhraseWrap PhraseWrap = EDreamTextPhraseWrap::Off;
	bool bUseKerning = false;
	EDreamUITextFontStyle FontStyle = EDreamUITextFontStyle::None;
	bool bRichText = false;
	int32 RichTextFilterFlags = 0xffffffff;
	/** Scales the gap between lines; 1 is the font's own line height. */
	float LineHeightPercentage = 1.0f;
	/** Wrap at this width instead of the box's when greater than zero. */
	float WrapTextAt = 0.0f;
	/** Expands each glyph's quad; only SDF fonts honour it. */
	float ExpandMeshSize = 0.0f;
	/** Pixels per unit for dynamically rasterized (bitmap) fonts, before the canvas scale. */
	float DynamicPixelsPerUnit = 1.0f;
	/** Root canvas scale, as the old pipeline read it off the canvas. */
	float RootCanvasScale = 1.0f;
	/** True when the root canvas renders to world space. */
	bool bRenderToWorldSpace = false;
	/** True when this text snaps to pixels (the font allows it and the hierarchy asks for it). */
	bool bPixelPerfect = false;

	TWeakObjectPtr<UDreamUIFontData_BaseObject> Font;
	TWeakObjectPtr<UDreamUIRichTextImageData_BaseObject> RichTextImageData;
	TWeakObjectPtr<UDreamUIRichTextCustomStyleData> RichTextCustomStyleData;

	bool operator==(const FDreamTextLayoutInput& Other) const;
	bool operator!=(const FDreamTextLayoutInput& Other) const { return !(*this == Other); }
};

/**
 * Lays text out into a display list. Measures only: no vertex is written here. The algorithm is
 * the old UpdateUIText's, line for line, so that swapping the painter in changed nothing visible;
 * the line breaker and the vertical model are what the next phases replace.
 */
class DREAMGUI_API FDreamTextLayoutEngine
{
public:
	/** Runs a layout. The font must be valid; the caller owns both structs. */
	static void Layout(const FDreamTextLayoutInput& Input, FDreamTextDisplayList& Out);
};
