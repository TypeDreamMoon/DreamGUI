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
	/**
	 * Colour faces may draw emoji. Off when the text's material does not shade through MF_DreamUI_Shade (its
	 * DreamUIShadeMaterial::ShadeMarkerParameter), which alone decodes a colour glyph's quad: an emoji then resolves as if
	 * the font had no colour face -- its emoji data's image, else a monochrome face's glyph.
	 */
	bool bAllowColorFaces = true;

	TWeakObjectPtr<UDreamUIFontData_BaseObject> Font;
	TWeakObjectPtr<UDreamUIRichTextImageData_BaseObject> RichTextImageData;
	TWeakObjectPtr<UDreamUIRichTextCustomStyleData> RichTextCustomStyleData;

	/** Every field but Color takes part, Language, TabSize, TextJustify, LastLineAlign and bAllowColorFaces included. */
	bool operator==(const FDreamTextLayoutInput& Other) const;
	bool operator!=(const FDreamTextLayoutInput& Other) const { return !(*this == Other); }
};

/** What FDreamTextLayoutState holds; defined where the layout is. */
struct FDreamTextLayoutStateData;

/**
 * What one text's layout keeps between layouts, so that the next lays out again only what changed (retained incremental
 * layout): each paragraph as it was measured -- its elements, glyphs and quads, bidi levels, grapheme and break bits, the
 * scripts and segments its shaping found -- its lines, and each line as it was placed, before Finish moved it into the
 * box. A layout through a state finds what the edit changed from the text itself (the common start and end of the old and
 * the new content), takes every paragraph the edit did not touch where it stands, reads, measures and analyses only a
 * window around the edit inside the paragraph it touched, and edits the display list it wrote last time in place: the
 * lines the edit changed are placed again and spliced in, those after them moved. An edit it cannot see that way -- one
 * that adds or removes a paragraph, touches rich-text markup or an image, or edits a long paragraph that cannot be measured
 * through a window (right to left, or with the shape cache off) -- is laid out as round-4 layouts were: every element read
 * again, paragraphs found by their content. A text with tag paints, or under a clamp, has its display list placed whole.
 * What it produces is what a layout without one produces, field for field.
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
	/** Rich-text parsing, the plain text, and sorting the elements into kinds and paragraphs; for an edit seen from the text, the window's elements spliced into the kept ones. */
	Preprocess,
	/** Finding the kept paragraphs that are this layout's, by their content. */
	Lookup,
	/** Taking paragraphs and lines from the kept layout, and keeping this one. */
	Reuse,
	/** Grapheme clusters, and measuring the paragraphs that were not taken. Shape is part of it. */
	Measure,
	/** Inside Measure (and inside Place, for an ellipsis): the shaper's calls (FDreamTextShaper::ShapeParagraph and ShapeWindow), its shape cache included. */
	Shape,
	/** Break opportunities, and breaking the paragraphs into lines. */
	BreakLines,
	/** Placing the lines that were not taken. */
	Place,
	Finish,
	/** The edit found from the content: the common start and end of the text and the kept one, and the kept elements they come to. */
	Diff,
	/** Comparing a paragraph measured whole after an edit with the kept paragraphs it was edited from, element by element. */
	Compare,
	/** Editing the display list in place: the lines an edit changed spliced in, the lines after them rebased. */
	Patch,
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
	/**
	 * Paragraphs found in a kept layout by their content and taken as they were (those taken where they stood count in
	 * ParagraphsPositional), and paragraphs measured (shaped, or measured per code point).
	 */
	int64 ParagraphsReused = 0;
	int64 ParagraphsMeasured = 0;
	/** Lines placed, and lines whose placement was taken from a kept layout. */
	int64 LinesPlaced = 0;
	int64 LinesReused = 0;
	/** UTF-16 code units ICU's break iterators walked: grapheme clusters, line breaks and, under phrase wrap, words. */
	int64 IcuCodeUnits = 0;
	/** Glyph quads asked of the font: shaped glyphs, code points measured one by one, underline and strikethrough strokes. */
	int64 QuadFetches = 0;

	/*
	 * What the work an edit costs scales with: each counts the elements, paragraphs or items a step touched.
	 */
	/** Elements read from the source (ReadCodePoint, rich-text parsing): every one for a layout from nothing, the window's for an edit. */
	int64 ElementsParsed = 0;
	/** Elements an edit wrote into the kept arrays in place of the ones it replaced. */
	int64 ElementsSpliced = 0;
	/** Elements after an edit's window whose kept records were moved and their indices rebased, rather than read and measured again. */
	int64 ElementsRebased = 0;
	/** Paragraphs taken where they stood because the edit did not touch them: no hash, no comparison. */
	int64 ParagraphsPositional = 0;
	/** Paragraphs whose content hashes were worked out (they are worked out when a lookup needs them, and kept). */
	int64 ParagraphsHashed = 0;
	/** Elements measured: a window's inside an edited paragraph, or every element of a paragraph measured whole. */
	int64 WindowElements = 0;
	/** Elements grouped into clusters, given their letter spacing and fit width (FinishClusters). */
	int64 ClustersFinished = 0;
	/** Elements the preferred width's running pen was summed over. */
	int64 PenSumElements = 0;
	/** Elements whose grapheme, line or word boundary or break opportunity was worked out again. */
	int64 BitsRecomputed = 0;
	/** Display-list items made by placing a line. */
	int64 ItemsPlaced = 0;
	/** Items an in-place edit moved to another index in the display list. */
	int64 ItemsMoved = 0;
	/** Items an in-place edit gave another element, source or line index. */
	int64 ItemsRebased = 0;
	/** Items an in-place edit moved to a new position from where they were placed (their line moved down, or the box). */
	int64 ItemsRefinished = 0;
	/** Items copied from or into a kept layout's copy of the display list (DreamGUI.Text.InPlaceDisplayList 0). */
	int64 ItemsCopied = 0;
	/** Layouts DreamGUI.Text.VerifyIncremental held against a layout from nothing, and those that came out different. */
	int64 VerifiedLayouts = 0;
	int64 VerifyMismatches = 0;

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
	/**
	 * The switches of what an incremental layout does by itself, each falling back to how round-4 layouts did it when it is 0:
	 * DreamGUI.Text.IncrementalParse (the edit found from the content, paragraphs it did not touch taken where they stand,
	 * only its window read and spliced in), DreamGUI.Text.IncrementalMeasure (only a window inside the edited paragraph
	 * shaped again; needs the first), DreamGUI.Text.InPlaceDisplayList (the display list edited where it is rather than
	 * copied).
	 */
	static bool IsIncrementalParseEnabled();
	static bool IsIncrementalMeasureEnabled();
	static bool IsInPlaceDisplayListEnabled();
	static FDreamTextLayoutStats GetStats();
	static void ResetStats();

#if !UE_BUILD_SHIPPING
	/**
	 * Whether two states hold the same layout: every measured element field for field, glyphs, boundary bits, paragraphs,
	 * line ranges and spans, the lines' local coordinates, tag records, languages and inline objects; what only says how a
	 * state came to be (hashes not worked out yet, which kept paragraph a paragraph came from, the faces a state remembers
	 * using beyond the other's, the quads it remembers) is left out. A state an incremental layout kept is held against the
	 * state a layout from nothing keeps. False with the first difference in OutDifference.
	 */
	static bool DebugCompareStates(const FDreamTextLayoutState& A, const FDreamTextLayoutState& B, FString& OutDifference);
	/** Whether two display lists are the same field for field, Generation and LineStamps aside. False with the first difference. */
	static bool DebugCompareDisplayLists(const FDreamTextDisplayList& A, const FDreamTextDisplayList& B, FString& OutDifference);
#endif
};
