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
	/**
	 * The text's own colour, as the rich-text parser's base. It is not part of equality: the layout
	 * emits nothing that depends on it (untagged glyphs are painted with FDreamTextPaintParams::
	 * BaseColor), so a fade must not cost a layout.
	 */
	FColor Color = FColor::White;
	FVector2f FontSpace = FVector2f::ZeroVector;
	float FontSize = 16.0f;
	EDreamUITextParagraphHorizontalAlign ParagraphHAlign = EDreamUITextParagraphHorizontalAlign::Left;
	EDreamUITextParagraphVerticalAlign ParagraphVAlign = EDreamUITextParagraphVerticalAlign::Bottom;
	EDreamUITextOverflowType OverflowType = EDreamUITextOverflowType::HorizontalOverflow;
	ETextWrappingPolicy WrappingPolicy = ETextWrappingPolicy::AllowPerCharacterWrapping;
	EDreamTextPhraseWrap PhraseWrap = EDreamTextPhraseWrap::Off;
	bool bUseKerning = false;
	EDreamUITextFontStyle FontStyle = EDreamUITextFontStyle::None;
	/** Underline / strike the whole run, before any markup: the text's own style, not a tag. */
	bool bUnderline = false;
	bool bStrikethrough = false;
	/** Case the content is laid out in; the caller has already applied it to Content. */
	EDreamUITextTransformPolicy TextTransform = EDreamUITextTransformPolicy::None;
	/** Auto asks the bidi algorithm; the other two override the paragraph's direction. */
	EDreamTextFlowDirection FlowDirection = EDreamTextFlowDirection::Auto;
	/**
	 * The language the text is in, a culture name ("ja", "zh-Hans"): which fallback faces' cultures match
	 * (FDreamUIFontFallback::Cultures) and what HarfBuzz shapes with (locl). UDreamText::MakeLayoutInput fills it with
	 * UDreamText::Language, or the game's current language when that is empty, so a culture switch is an ordinary input
	 * change. Empty here: the game's current language when the layout runs. A rich text's <lang=xx> overrides it inside
	 * the tag. Line breaking stays on the game's culture whatever this says.
	 */
	FString Language;
	/**
	 * Tab stops, every TabSize spaces -- the primary face's space at the text's size, plus the letter spacing -- measured
	 * from the line's start edge: CSS tab-size. A tab narrower than half a space jumps to the next stop; 0 gives a tab no
	 * width. The same with or without shaping.
	 */
	float TabSize = 8.0f;
	/** Where a justified line gets its extra room. Read only with ParagraphHAlign Justify. */
	EDreamTextJustify TextJustify = EDreamTextJustify::Auto;
	/** How a justified paragraph's last line, and a line a newline ends, align. Read only with ParagraphHAlign Justify. */
	EDreamTextLastLineAlign LastLineAlign = EDreamTextLastLineAlign::Auto;
	/** Wrap even when the overflow policy is not VerticalOverflow (UMG's AutoWrapText). */
	bool bAutoWrapText = false;
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
	/**
	 * Let the font's optional ligatures (liga, clig) and contextual alternates (calt) form, as a browser does by default.
	 * A ligature is one glyph over several characters, so the caller leaves this off while anything animates the text
	 * character by character. Ligatures stay off whenever FontSpace.X is not 0, as CSS asks of letter-spacing. Off, calt
	 * goes with them in Latin, Greek, Cyrillic, Armenian, Georgian and runs of digits and punctuation, where it is only
	 * style; every other script keeps the font's default, since a joining or Brahmic script can need it, and every script
	 * keeps the forms it requires (Arabic's lam-alef, Indic conjuncts).
	 */
	bool bAllowLigatures = false;

	TWeakObjectPtr<UDreamUIFontData_BaseObject> Font;
	TWeakObjectPtr<UDreamUIRichTextImageData_BaseObject> RichTextImageData;
	TWeakObjectPtr<UDreamUIRichTextCustomStyleData> RichTextCustomStyleData;

	/** Every field but Color takes part, Language, TabSize, TextJustify and LastLineAlign included. */
	bool operator==(const FDreamTextLayoutInput& Other) const;
	bool operator!=(const FDreamTextLayoutInput& Other) const { return !(*this == Other); }
};

/** What FDreamTextLayoutState holds; defined where the layout is. */
struct FDreamTextLayoutStateData;

/**
 * What one text's layout keeps between layouts, so that the next lays out again only what changed (retained incremental
 * layout): each paragraph as it was measured -- its elements, glyphs and quads, bidi levels, grapheme and break bits --
 * its lines, and each line as it was placed, before Finish moved it into the box. A layout through a state takes a
 * paragraph whose text and style are unchanged as it was, measures the others again (an edited one keeps the lines before
 * the edit and those after it that still start where they did), and places again only the lines whose content or place
 * changed. What it produces is what a layout without one produces, field for field.
 *
 * Opaque: only FDreamTextLayoutEngine reads or writes it. A font, an atlas, a font's layout or face epoch, or any input
 * that measuring reads being different drops what was kept; Reset drops it too.
 */
class DREAMGUI_API FDreamTextLayoutState
{
public:
	FDreamTextLayoutState();
	~FDreamTextLayoutState();
	FDreamTextLayoutState(const FDreamTextLayoutState&) = delete;
	FDreamTextLayoutState& operator=(const FDreamTextLayoutState&) = delete;

	/** Forget the kept layout: the next layout through this state starts from nothing. */
	void Reset();
	/** A layout is kept for the next one to build on. */
	bool HasLayout() const;
	/** About what the kept layout takes up, in bytes. */
	SIZE_T GetAllocatedSize() const;

private:
	friend class FDreamTextLayoutEngine;
	TUniquePtr<FDreamTextLayoutStateData> Data;
};

/** A step of a layout, as FDreamTextLayoutStats times it: the DreamUI_TextLayout_* profiler scopes, and the shaper's calls. */
enum class EDreamTextLayoutStage : uint8
{
	Prepare,
	/** Rich-text parsing, the plain text, and sorting the elements into kinds and paragraphs. */
	Preprocess,
	/** Finding the kept paragraphs that are this layout's, and comparing an edited one with what it was. */
	Lookup,
	/** Taking paragraphs and lines from the kept layout, and keeping this one. */
	Reuse,
	/** Grapheme clusters, and measuring the paragraphs that were not taken. Shape is part of it. */
	Measure,
	/** Inside Measure (and inside Place, for an ellipsis): the shaper's calls (FDreamTextShaper::ShapeParagraph), its shape cache included. */
	Shape,
	/** Break opportunities, and breaking the paragraphs into lines. */
	BreakLines,
	/** Placing the lines that were not taken. */
	Place,
	Finish,
	Count
};

/**
 * What layouts did since the last ResetStats: what the incremental layout's tests and the text benchmark read. Game
 * thread; every layout counts, with a state or without one.
 */
struct DREAMGUI_API FDreamTextLayoutStats
{
	int64 Layouts = 0;
	/** Layouts that built on a layout their state had kept. */
	int64 IncrementalLayouts = 0;
	/** Paragraphs taken from a kept layout as they were, and paragraphs measured (shaped, or measured per code point). */
	int64 ParagraphsReused = 0;
	int64 ParagraphsMeasured = 0;
	/** Lines placed, and lines whose placement was taken from a kept layout. */
	int64 LinesPlaced = 0;
	int64 LinesReused = 0;
	/** UTF-16 code units ICU's break iterators walked: grapheme clusters, line breaks and, under phrase wrap, words. */
	int64 IcuCodeUnits = 0;
	/** Glyph quads asked of the font: shaped glyphs, code points measured one by one, underline and strikethrough strokes. */
	int64 QuadFetches = 0;
	/** Time spent in each stage, in FPlatformTime::Cycles64 units. */
	uint64 Cycles[(int32)EDreamTextLayoutStage::Count] = {};

	double GetMilliseconds(EDreamTextLayoutStage Stage) const;
	/** The stage's profiler scope without its DreamUI_TextLayout_ prefix ("Measure"); "Shape" for the shaper. */
	static const TCHAR* GetStageName(EDreamTextLayoutStage Stage);
};

/**
 * Lays text out into a display list. Measures only: no vertex is written here. Elements are shaped
 * per paragraph, grouped into clusters (UAX #29 graphemes joined with the shaper's own clusters),
 * broken into lines (UAX #14), ordered per line by their bidi levels (UAX #9 rule L2) and stacked
 * with CSS's half-leading.
 */
class DREAMGUI_API FDreamTextLayoutEngine
{
public:
	/** Runs a layout. The font must be valid; the caller owns both structs. */
	static void Layout(const FDreamTextLayoutInput& Input, FDreamTextDisplayList& Out);
	/**
	 * Runs a layout that builds on what State kept of this text's last layout, and keeps this one in it for the next.
	 * The display list is the one Layout(Input, Out) makes, field for field. With DreamGUI.Text.IncrementalLayout 0, or a
	 * null State, it is that layout: nothing is built on and nothing kept (and State is emptied).
	 */
	static void Layout(const FDreamTextLayoutInput& Input, FDreamTextDisplayList& Out, FDreamTextLayoutState* State);
	/** DreamGUI.Text.IncrementalLayout is not 0. */
	static bool IsIncrementalLayoutEnabled();
	static FDreamTextLayoutStats GetStats();
	static void ResetStats();
};
