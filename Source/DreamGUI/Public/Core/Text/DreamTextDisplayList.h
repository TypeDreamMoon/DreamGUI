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
	/** True when the colour came from a <color> tag, or a custom style that replaces it, rather than the text's own colour. */
	bool bHasColor = false;
	/**
	 * A custom style's Multiply colour that met no colour of its own to multiply. It multiplies whatever the glyph is
	 * painted with, at paint time, because the text's own colour is a paint input the layout never sees.
	 */
	bool bHasMultiplyColor = false;
	FColor MultiplyColor = FColor::White;
	bool bBold = false;
	bool bItalic = false;
	/**
	 * Bold or italic that the face drawing the item does not have, so the painter makes it: bold as a dilation (distance
	 * fields) or an emboldened raster, italic as a shear. False when a real bold or italic face draws the item, and when
	 * the item does not ask for the style at all.
	 */
	bool bSyntheticBold = false;
	bool bSyntheticItalic = false;
	bool bUnderline = false;
	bool bStrikethrough = false;
	/**
	 * 0 none, 1 superscript, 2 subscript. Mirrors DreamUIRichTextParser::ESupOrSubMode without pulling the parser in. The
	 * baseline shift it stands for is already in the item's Pen and in its line's box.
	 */
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
	/** Position the glyph's offsets are measured from: its pen position on the baseline, with the shaper's offsets and any superscript shift applied. */
	FVector2f Pen = FVector2f::ZeroVector;
	/** Glyph metrics and atlas UVs, already adjusted for canvas scale, kerning and the font's vertical offset. */
	FDreamUICharData Glyph;
	/** XAdvance plus the letter spacing it carries: how wide a stretch of underline or strikethrough this item contributes. */
	float AdvanceWithSpace = 0.0f;
	/** Where that stretch starts, relative to Pen.X: the pen box of the glyph, not its ink, so a run of them joins up seamlessly. */
	float DecorationOffset = 0.0f;
	FDreamTextItemStyle Style;
	/**
	 * Texels and placement the underline is drawn with; valid only when Style.bUnderline. YOffset is the top of the strip
	 * and Height its thickness, both measured from Pen.Y. With the font's own underline metrics the UVs are one texel inside
	 * its '_' (the atlas has no white texel); without them they are the '_' collapsed to its centre column.
	 */
	FDreamUICharData UnderlineGlyph;
	/** Same as UnderlineGlyph, for the strikethrough and the font's '-'; valid only when Style.bStrikethrough. */
	FDreamUICharData StrikethroughGlyph;
	/** The painter emits a quad for this item. Glyphs past a Truncate/Ellipsis cut are laid out but not emitted. */
	bool bEmit = false;
	/** Counts towards the visible-char sequence TextAnimation addresses; the ellipsis glyph does not. */
	bool bCountsAsVisible = false;
};

/** A stretch of one line that runs in one direction, in visual order: what selection highlights are drawn from. */
struct FDreamTextVisualRun
{
	int32 LineIndex = 0;
	/** Source range, in the same UTF-16 offsets as FDreamTextGlyphItem::SourceIndex, half-open. */
	int32 SourceStart = 0;
	int32 SourceEnd = 0;
	/** Left and right edges in the text's local space, after alignment. */
	float Left = 0.0f;
	float Right = 0.0f;
	bool bRightToLeft = false;
};

/** Everything layout knows after a pass. */
struct DREAMGUI_API FDreamTextDisplayList
{
	TArray<FDreamTextGlyphItem> Items;
	/**
	 * Caret lines, first line first -- the caret contract UITextInput reads. Each line lists one caret per grapheme
	 * cluster in logical order, then the line's end caret; a caret stands at its cluster's leading edge.
	 */
	TArray<FDreamUITextLineProperty> Lines;
	TArray<FDreamUIText_RichTextCustomTag> CustomTags;
	/**
	 * The elements each custom tag covers, inclusive, parallel to CustomTags (Y below X when the tag holds nothing). The
	 * painter matches items against these, spaces included, which a range of visible characters cannot do.
	 */
	TArray<FIntPoint> CustomTagElementRanges;
	TArray<FDreamUIText_RichTextImageTag> Images;
	TArray<FDreamUIText_Emoji> Emojis;
	/** Every line's directional runs, line by line and left to right within a line. */
	TArray<FDreamTextVisualRun> VisualRuns;
	/** Size of the text ignoring automatic wrapping -- what a content-sized parent asks for. */
	FVector2f PreferredSize = FVector2f::ZeroVector;
	/** True when Truncate or Ellipsis cut something off. */
	bool bTruncated = false;
	/** Some glyphs were still on the font's worker: their quads are missing until the font's OnGlyphsReady. */
	bool bHasPendingGlyphs = false;
	/**
	 * Visible characters, counted per ELEMENT: two glyphs out of one code point are one character, and
	 * a character whose glyph has not landed yet still has its place. Equal, by construction, to the
	 * number of FDreamUITextCharProperty entries the painter writes -- which is what TextAnimation and
	 * the rich-text tag ranges address.
	 */
	int32 VisibleCharCount = 0;

	void Reset()
	{
		Items.Reset();
		Lines.Reset();
		CustomTags.Reset();
		CustomTagElementRanges.Reset();
		Images.Reset();
		Emojis.Reset();
		VisualRuns.Reset();
		PreferredSize = FVector2f::ZeroVector;
		bTruncated = false;
		bHasPendingGlyphs = false;
		VisibleCharCount = 0;
	}
};
