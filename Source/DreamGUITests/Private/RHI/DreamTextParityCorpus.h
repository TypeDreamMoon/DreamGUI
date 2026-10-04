// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class UDreamUIFontData_FreeTypeRender;

/*
 * The text parity corpus (Resources/TextParity/corpus.json) as the tests read it, and the fonts it names.
 *
 * The corpus is shared three ways: the parity test draws its cases with DreamGUI and Slate, the Chrome reference script
 * draws the same cases with Chrome, and the .dui scenes take their non-default fonts from its fonts table. Everything
 * that turns a font key into faces lives here, so that the three agree on which files a key means.
 */
namespace DreamTextParity
{
	/** Source/DreamGUITests/Resources in the plugin. */
	FString GetResourcesDirectory();
	FString GetCorpusPath();
	/** Resources/TextParity/Chrome: what Tools/TextParity/Make-ChromeReference.ps1 writes, <case>.png and <case>.json. */
	FString GetChromeReferenceDirectory();
	/** <Project>/Saved/DreamGUITextParity: the pictures and the report the parity test writes. */
	FString GetOutputDirectory();
	/** A font file as the corpus spells it, $(EngineDir) and $(WindowsFonts) put in, as an absolute path. */
	FString ResolveFontPath(const FString& InSpelling);

	/**
	 * One face of a font key. In the fonts table a face is its file, or an object {file, lang, unicodeRange, scale} saying
	 * what a fallback is for: lang the cultures it is meant for (semicolon-separated, as FDreamUIFontFallback::Cultures
	 * and Slate's sub-fonts write them), unicodeRange the code points it may draw (CSS syntax: U+4E00-9FFF, U+30??), scale
	 * its size-adjust. DreamGUI makes them the fallback entry's Cultures, Ranges and Scale; Chrome's page the @font-face's
	 * unicode-range and size-adjust and a :lang() family list; Slate a sub-typeface's character ranges and scaling factor.
	 * The first face of a key is the font itself and takes only its file.
	 */
	struct FFontFace
	{
		FString File;
		FString Lang;
		/** As the corpus spells it, for Chrome; Ranges is the same read for DreamGUI and Slate. */
		FString UnicodeRange;
		TArray<FInt32Interval> Ranges;
		float Scale = 1.0f;

		/** Whether the face's ranges let it draw the code point: true for every code point when it has none. */
		bool AllowsCodepoint(uint32 InCodepoint) const;
		/** A face reads as its file, which is all a face of the table was before it could say what it is for. */
		operator const FString&() const { return File; }
	};

	/** One key of the fonts table: its faces in fallback order, the primary first. */
	struct FFontKey
	{
		FString Key;
		TArray<FFontFace> Faces;
		/** The primary's true style faces; empty where it has none. */
		FString Bold;
		FString Italic;
		FString BoldItalic;
		/** DreamGUI draws the key with a bitmap font rather than a distance field. */
		bool bBitmap = false;
		/** The key names a file not every machine has (a system font): a case that uses it is skipped where one is missing. */
		bool bOptional = false;

		/** Whether a face is meant for particular languages, which only Chrome and DreamGUI can follow per text. */
		bool HasLanguageFaces() const;
		/** Every file the key names that is not on disk. */
		TArray<FString> GetMissingFiles() const;
	};

	/** One case of the corpus, variants already made into cases of their own. */
	struct FCase
	{
		FString Id;
		/** The case a variant was made from; the case's own id when it is not a variant. */
		FString BaseId;
		FString Text;
		FString Font;
		float Size = 16.0f;
		/** The box width in pixels; 0 lays the text out on one line in a box as wide as the canvas less the padding. */
		float Width = 0.0f;
		/** 0 for the font's own line height, otherwise the multiple of it. */
		float LineHeight = 0.0f;
		float LetterSpacing = 0.0f;
		bool bRich = false;
		FString Lang = TEXT("en");
		/** start, end, center, left, right or justify. */
		FString Align = TEXT("start");
		/** For a justified case, CSS text-justify (auto, inter-word, inter-character, none) and text-align-last (auto, start, center, end, justify). */
		FString TextJustify = TEXT("auto");
		FString TextAlignLast = TEXT("auto");
		FString Dir = TEXT("ltr");
		FString Wrap = TEXT("anywhere");
		/** '', 'ellipsis', 'middleEllipsis', or 'clamp' with MaxLines. */
		FString Overflow;
		int32 MaxLines = 0;
		FString Transform;
		/** CSS tab-size in spaces; bTabSizeSet when the case states one, which Slate has no equivalent of. */
		float TabSize = 8.0f;
		bool bTabSizeSet = false;
		/** Outline width in em outside the face; 0 for none. */
		float OutlineEm = 0.0f;
		FColor OutlineColor = FColor::Black;
		/** A hard drop shadow: its offset in em, +Y down, and its colour. */
		bool bShadow = false;
		FVector2f ShadowOffsetEm = FVector2f::ZeroVector;
		FColor ShadowColor = FColor::Black;
		/**
		 * A gradient the whole text's face is filled with, as CSS: DreamGUI reads it with FDreamGradient::ParseCss into the
		 * text's FacePaint (measured across the text as a block), Chrome's page paints it with background-clip: text on the
		 * paragraph. Empty for a solid text; a rich case can paint a run with <gradient=css-without-spaces> instead.
		 */
		FString Fill;
		/** The canvas in CSS pixels, which is DreamGUI's canvas units and Slate's layout units. */
		FIntPoint Canvas = FIntPoint(1024, 160);
		/** Device pixels per CSS pixel: DreamGUI's canvas scale, Slate's DPI scale, Chrome's device scale factor. */
		float Scale = 1.0f;
		/** "off": DreamGUI draws the text from its font's distance field at every size (UDreamText::SmallTextRaster). */
		FString SmallTextRaster;
		/** What the case is measured against: "chrome", or "slate" where Chrome has nothing to compare (a middle ellipsis). */
		FString Reference = TEXT("chrome");
		/** How far DreamGUI may be from the reference, by measure; see the test for the names. Asserted unless reportOnly. */
		TArray<TPair<FString, double>> Targets;
		bool bInverse = false;
		TArray<FString> Flags;

		bool HasFlag(const TCHAR* InFlag) const { return Flags.Contains(InFlag); }
		/** Nothing about the case is asserted, neither its breaks nor its targets: known not to match yet. */
		bool IsReportOnly() const { return HasFlag(TEXT("reportOnly")); }
		/**
		 * The case paints with a gradient -- a Fill, or a rich text's <gradient=...> run -- and each side draws it a second
		 * time as a solid mask (GetMaskText), against which the two fills' colours are compared.
		 */
		bool HasFill() const;
		bool IsHeldToSlate() const { return Reference.Equals(TEXT("slate"), ESearchCase::IgnoreCase); }
		FColor GetInk() const { return bInverse ? FColor::White : FColor::Black; }
		FColor GetPaper() const { return bInverse ? FColor::Black : FColor::White; }
		/** The rectangle the text is laid out in, from (InPadding, InPadding) of the canvas, in CSS pixels. */
		FVector2D GetBox(int32 InPadding) const;
		/** The canvas in device pixels: what every picture of the case measures. */
		FIntPoint GetDeviceCanvas() const;
		/** Device pixels per CSS pixel as the pictures have it: the device canvas's width over the canvas's. */
		double GetDeviceScale() const;
	};

	struct FCorpus
	{
		int32 Padding = 16;
		TArray<FCase> Cases;
		/** The fonts table as read, looked up by key on demand. */
		TSharedPtr<FJsonObject> Fonts;

		const FCase* FindCase(const FString& InId) const;
		bool FindFontKey(const FString& InKey, FFontKey& OutKey) const;
	};

	/** Read the corpus and make its variants. False, with the reason, when the file cannot be read or is not a corpus. */
	bool LoadCorpus(FCorpus& OutCorpus, FString& OutError);

	/**
	 * DreamGUI's font for a key: a distance-field font (outline multi-channel, the class default) or a bitmap font over
	 * the key's faces, the rest of them as its fallback entries in order -- each with its face's cultures, ranges and
	 * scale -- and the key's bold, italic and bold-italic files as its true style faces. A colour face (an emoji font) is
	 * a fallback like any other. Shared by every test while one holds it; null, with the reason, when no face can be used.
	 */
	UDreamUIFontData_FreeTypeRender* GetDreamFont(const FFontKey& InKey, FString& OutError);

	/** The style DreamGUI's rich-text markup gives a run of characters. */
	struct FRichStyle
	{
		float Size = 16.0f;
		bool bBold = false;
		bool bItalic = false;
		bool bUnderline = false;
		bool bStrikethrough = false;
		/** 0 none, 1 superscript, 2 subscript. */
		uint8 SupOrSub = 0;
		bool bLink = false;
		bool bHasColor = false;
		FColor Color = FColor::Black;

		bool operator==(const FRichStyle& Other) const;
		bool operator!=(const FRichStyle& Other) const { return !(*this == Other); }
	};

	struct FRichRun
	{
		FString Text;
		FRichStyle Style;
	};

	/**
	 * DreamGUI's rich-text markup read the way UDreamText reads it: <b> <i> <u> <s> <sup> <sub> <size=N> (also +N and
	 * -N) <color=#hex> <a=id> <lang=xx> and custom tags, closed by </name>, and the character references &lt; &gt; &amp;
	 * &quot; &apos; &nbsp; &#N; &#xN;. Out come the runs of equally styled text and the text with the markup taken out,
	 * which is what Chrome's text content of the same case is. OutSourceOffsets, when given, holds for every UTF-16 unit of
	 * that text the offset in the markup it was read from (a reference's '&'): DreamGUI numbers a rich text's carets by
	 * offsets into the markup.
	 */
	void ParseRichText(const FString& InMarkup, float InBaseSize, TArray<FRichRun>& OutRuns, FString& OutPlain,
		TArray<int32>* OutSourceOffsets = nullptr);
	/** The case's text as it is read on screen: the markup taken out for a rich case. */
	FString GetPlainText(const FCase& InCase);
	/**
	 * The text a fill case's mask is drawn from: a rich case's <gradient=...> and </gradient> tags taken out, the rest of
	 * its markup kept, so that the mask is the same text in the ink colour; any other case's text as it is.
	 */
	FString GetMaskText(const FCase& InCase);

	/** Code points of a UTF-16 string, surrogate pairs joined, each with the offset of its first unit. */
	void DecodeCodepoints(const FString& InText, TArray<uint32>& OutCodepoints, TArray<int32>& OutOffsets);
	/** Characters a font is not expected to draw: controls, joiners, marks of direction, variation selectors, the soft hyphen. */
	bool IsDefaultIgnorable(uint32 InCodepoint);
	/** CSS unicode-range ("U+0025-00FF, U+4??, U+20AC") as inclusive intervals. False, with nothing out, when it does not read. */
	bool ParseUnicodeRange(const FString& InSpelling, TArray<FInt32Interval>& OutRanges);
	/**
	 * Whether a text in InTextLanguage is in one of a face's languages (InFaceLanguages, semicolon-separated): CSS :lang()
	 * matching, case-insensitive -- "zh" takes "zh-Hans", "zh-Hans" does not take "zh".
	 */
	bool LanguageMatches(const FString& InFaceLanguages, const FString& InTextLanguage);
	/**
	 * The order a text in InLanguage tries a key's faces in: the primary, then the fallbacks meant for its language, then
	 * those meant for any, then the rest, each group in table order. DreamGUI's face resolver tries fallback entries in
	 * this order, and Chrome's page gives the paragraph this family list through :lang().
	 */
	void GetFaceOrder(const FFontKey& InKey, const FString& InLanguage, TArray<int32>& OutOrder);

	/** The code points a font file maps, read once with FreeType. */
	struct FFaceCoverage
	{
		bool bLoaded = false;
		TSet<uint32> Codepoints;
	};
	const FFaceCoverage& GetFaceCoverage(const FString& InFile);
	/** The first face of the key, in the order a text in InLanguage tries them, that may and does draw the code point; INDEX_NONE when none does. */
	int32 FindCoveringFace(const FFontKey& InKey, uint32 InCodepoint, const FString& InLanguage = FString());
}
