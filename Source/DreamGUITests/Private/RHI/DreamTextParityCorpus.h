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

	/** One key of the fonts table: its faces in fallback order, the primary first. */
	struct FFontKey
	{
		FString Key;
		TArray<FString> Faces;
		/** The primary's true style faces; empty where it has none. */
		FString Bold;
		FString Italic;
		FString BoldItalic;
		/** DreamGUI draws the key with a bitmap font rather than a distance field. */
		bool bBitmap = false;
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
		FString Align = TEXT("start");
		FString Dir = TEXT("ltr");
		FString Wrap = TEXT("anywhere");
		FString Overflow;
		int32 MaxLines = 0;
		FString Transform;
		/** Outline width in em outside the face; 0 for none. */
		float OutlineEm = 0.0f;
		FColor OutlineColor = FColor::Black;
		FIntPoint Canvas = FIntPoint(1024, 160);
		bool bInverse = false;
		TArray<FString> Flags;

		bool HasFlag(const TCHAR* InFlag) const { return Flags.Contains(InFlag); }
		FColor GetInk() const { return bInverse ? FColor::White : FColor::Black; }
		FColor GetPaper() const { return bInverse ? FColor::Black : FColor::White; }
		/** The rectangle the text is laid out in, from (InPadding, InPadding) of the canvas. */
		FVector2D GetBox(int32 InPadding) const;
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
	 * the key's faces, the rest of them as its fallbacks in order, and the key's bold, italic and bold-italic files as
	 * its true style faces. A colour bitmap face (an emoji font) is left out: it has no outlines to make a distance
	 * field of. Shared by every test while one holds it; null, with the reason, when no face can be used.
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
	 * -N) <color=#hex> <a=id> and custom tags, closed by </name>, and the character references &lt; &gt; &amp; &quot;
	 * &apos; &nbsp; &#N; &#xN;. Out come the runs of equally styled text and the text with the markup taken out, which
	 * is what Chrome's text content of the same case is. OutSourceOffsets, when given, holds for every UTF-16 unit of
	 * that text the offset in the markup it was read from (a reference's '&'): DreamGUI numbers a rich text's carets by
	 * offsets into the markup.
	 */
	void ParseRichText(const FString& InMarkup, float InBaseSize, TArray<FRichRun>& OutRuns, FString& OutPlain,
		TArray<int32>* OutSourceOffsets = nullptr);
	/** The case's text as it is read on screen: the markup taken out for a rich case. */
	FString GetPlainText(const FCase& InCase);

	/** Code points of a UTF-16 string, surrogate pairs joined, each with the offset of its first unit. */
	void DecodeCodepoints(const FString& InText, TArray<uint32>& OutCodepoints, TArray<int32>& OutOffsets);
	/** Characters a font is not expected to draw: controls, joiners, marks of direction, variation selectors, the soft hyphen. */
	bool IsDefaultIgnorable(uint32 InCodepoint);

	/** The code points a font file maps, read once with FreeType. */
	struct FFaceCoverage
	{
		bool bLoaded = false;
		TSet<uint32> Codepoints;
	};
	const FFaceCoverage& GetFaceCoverage(const FString& InFile);
	/** The first face of the key, in fallback order, with a glyph for the code point; INDEX_NONE when none has one. */
	int32 FindCoveringFace(const FFontKey& InKey, uint32 InCodepoint);
}
