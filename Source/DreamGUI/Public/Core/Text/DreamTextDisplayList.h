// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DreamUITextData.h"
#include "Core/DreamUIFontData_BaseObject.h"

/**
 * The text pipeline's middle layer: what layout produces and what the painter consumes.
 *
 * Nothing here knows about vertices, canvases or widgets. A display list is a list of positioned
 * glyph items plus the side tables the rest of the plugin reads (caret lines, char properties are
 * produced by the painter, rich-text tag ranges, inline objects). It is plain data, so it can be
 * built headlessly with a mock font and asserted on, and it can be painted any number of times
 * without laying out again -- which is what makes Best Fit and preferred-size queries cheap.
 */

/** What kind of thing a layout item is. */
enum class EDreamTextItemKind : uint8
{
	/** A glyph the painter emits a quad for. */
	Glyph,
	/** A space or tab: advances the pen, emits nothing. */
	Space,
	/** A rich-text <img> placeholder: advances the pen, emits nothing, the image object is created separately. */
	Image,
	/** An emoji: advances the pen, emits nothing, the emoji object is created separately. */
	Emoji,
};

/** The per-item slice of the rich-text state that reaches geometry. */
struct FDreamTextItemStyle
{
	float Size = 0.0f;
	FColor Color = FColor::White;
	/** True when the colour came from a <color> tag rather than the text's own colour. */
	bool bHasColor = false;
	bool bBold = false;
	bool bItalic = false;
	bool bUnderline = false;
	bool bStrikethrough = false;
	/** 0 none, 1 superscript, 2 subscript. Mirrors DreamUIRichTextParser::ESupOrSubMode without pulling the parser in. */
	uint8 SupOrSub = 0;
};

/** One laid-out element of the text. Positions are in the text's local space, after every alignment. */
struct FDreamTextGlyphItem
{
	EDreamTextItemKind Kind = EDreamTextItemKind::Glyph;
	uint32 Codepoint = 0;
	/** Index of this element in the processing array (the unit the old pipeline called "charIndex"). */
	int32 ElementIndex = 0;
	/** Index of the element's first UTF-16 unit in the source string. */
	int32 SourceIndex = 0;
	/** Which line this item sits on. */
	int32 LineIndex = 0;
	/** Position the glyph's offsets are measured from: the old pen position (line offset) at emission. */
	FVector2f Pen = FVector2f::ZeroVector;
	/** Glyph metrics and atlas UVs, already adjusted for canvas scale, kerning and the font's vertical offset. */
	FDreamUICharData Glyph;
	/** XAdvance plus the horizontal font space: the width decorations span. */
	float AdvanceWithSpace = 0.0f;
	FDreamTextItemStyle Style;
	/** Glyph used to draw the underline (the font's '_' collapsed to one texel column); valid only when Style.bUnderline. */
	FDreamUICharData UnderlineGlyph;
	/** Glyph used to draw the strikethrough (the font's '-' collapsed to one texel column); valid only when Style.bStrikethrough. */
	FDreamUICharData StrikethroughGlyph;
	/** The painter emits a quad for this item. Glyphs past a Truncate/Ellipsis cut are laid out but not emitted. */
	bool bEmit = false;
	/** Counts towards the visible-char sequence TextAnimation addresses; the ellipsis glyph does not. */
	bool bCountsAsVisible = false;
};

/** Everything layout knows after a pass. */
struct DREAMGUI_API FDreamTextDisplayList
{
	TArray<FDreamTextGlyphItem> Items;
	/** Caret lines, first line first -- the caret contract UITextInput reads. */
	TArray<FDreamUITextLineProperty> Lines;
	TArray<FDreamUIText_RichTextCustomTag> CustomTags;
	TArray<FDreamUIText_RichTextImageTag> Images;
	TArray<FDreamUIText_Emoji> Emojis;
	/** Size of the text ignoring automatic wrapping -- what a content-sized parent asks for. */
	FVector2f PreferredSize = FVector2f::ZeroVector;
	/** True when Truncate or Ellipsis cut something off. */
	bool bTruncated = false;
	/** Number of items that count as visible characters. */
	int32 VisibleCharCount = 0;

	void Reset()
	{
		Items.Reset();
		Lines.Reset();
		CustomTags.Reset();
		Images.Reset();
		Emojis.Reset();
		PreferredSize = FVector2f::ZeroVector;
		bTruncated = false;
		VisibleCharCount = 0;
	}
};
