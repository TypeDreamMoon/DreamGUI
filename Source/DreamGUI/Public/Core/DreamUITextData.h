// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Text/TextLayout.h"
#include "DreamUITextData.generated.h"

class FDreamUIGeometry;
class UDreamText;
class UDreamUIFontData_BaseObject;
class UDreamUIRichTextImageData;


UENUM(BlueprintType, Category = DreamGUI)
enum class EDreamUITextParagraphHorizontalAlign : uint8
{
	Left,
	Center,
	Right,
};

UENUM(BlueprintType, Category = DreamGUI)
enum class EDreamUITextParagraphVerticalAlign : uint8
{
	Top,
	Middle,
	Bottom,
};

UENUM(BlueprintType, Category = DreamGUI)
enum class EDreamUITextFontStyle :uint8
{
	None,
	Bold,
	Italic,
	BoldAndItalic,
};

/**
 * Whether CJK text wraps between words rather than between any two characters.
 *
 * The line-break rules (UAX #14) let a line end between any two ideographs, which is what every
 * browser does by default. CSS Text 4 added `word-break: auto-phrase` to keep words together instead;
 * this is the same idea, using the dictionary ICU ships for Chinese and Japanese. A word that does not
 * fit on a line still breaks inside under AllowPerCharacterWrapping, so it never makes text overflow.
 */
UENUM(BlueprintType, Category = DreamGUI)
enum class EDreamTextPhraseWrap : uint8
{
	/** Break between any two CJK characters, as the line-break rules allow. */
	Off,
	/** Break only between dictionary words inside CJK runs. */
	CJKDictionary,
};

/**
 * Case applied to the text on its way into the layout, UMG's ETextTransformPolicy. The Text property
 * itself is untouched -- this is presentation, so a caret index and a copy still see what was authored.
 */
UENUM(BlueprintType, Category = DreamGUI)
enum class EDreamUITextTransformPolicy : uint8
{
	None,
	ToLower,
	ToUpper,
};

/**
 * Which way the paragraph reads, UMG's ETextFlowDirection. Auto asks the bidi algorithm, which is what
 * every paragraph did before this existed; the other two override it, for a UI whose direction is the
 * game's setting rather than the string's content (an empty or all-neutral string has no direction of
 * its own, and a mixed one takes the first strong character's).
 */
UENUM(BlueprintType, Category = DreamGUI)
enum class EDreamTextFlowDirection : uint8
{
	Auto,
	LeftToRight,
	RightToLeft,
};

UENUM(BlueprintType, Category = DreamGUI)
enum class EDreamUITextOverflowType :uint8
{
	/** chars will go out of rect range horizontally */
	HorizontalOverflow = 0,
	/** chars will go out of rect range vertically */
	VerticalOverflow = 1,
	/** remove chars on right if out of range */
	Truncate = 2,
	/** replace chars with ... if out of range */
	Ellipsis = 3,
};

/**
 * How the built-in shader draws a text's glyphs beyond the plain face: outline, underlay (drop
 * shadow), glow, and how the unfilled part of a lyric line looks. Lengths are in em, so a style
 * reads the same at every font size. Only distance-field fonts (MTSDF) render these; bitmap fonts
 * draw the face alone.
 *
 * The style is stored per widget in the canvas's widget property texture, so texts with different
 * styles still batch into one draw.
 */
USTRUCT(BlueprintType, Category = DreamGUI)
struct DREAMGUI_API FDreamTextStyle
{
	GENERATED_BODY()

	/**
	 * Extra edge softness (blur) in em. 0 draws a crisp edge. Each glyph softens on its own, so past
	 * about 0.05 em the halos of neighbouring glyphs visibly merge; a real blur of a whole line is a
	 * post-process job, not a style.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face", meta = (ClampMin = "0", UIMax = "0.5"))
	float FaceSoftness = 0.0f;
	/** Grow (positive) or shrink (negative) the face, in em. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face", meta = (UIMin = "-0.2", UIMax = "0.2"))
	float FaceDilate = 0.0f;

	/** Outline colour; alpha 0 means no outline. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outline")
	FColor OutlineColor = FColor(0, 0, 0, 0);
	/** Outline width outside the face, in em. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outline", meta = (ClampMin = "0", UIMax = "0.5"))
	float OutlineWidth = 0.0f;
	/** Outline edge softness, in em. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outline", meta = (ClampMin = "0", UIMax = "0.5"))
	float OutlineSoftness = 0.0f;

	/** Underlay (drop shadow) colour; alpha 0 means no underlay. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Underlay")
	FColor UnderlayColor = FColor(0, 0, 0, 0);
	/** Underlay offset in em; +Y is down. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Underlay", meta = (UIMin = "-0.5", UIMax = "0.5"))
	FVector2f UnderlayOffset = FVector2f(0.05f, 0.05f);
	/** Underlay edge softness, in em. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Underlay", meta = (ClampMin = "0", UIMax = "0.5"))
	float UnderlaySoftness = 0.0f;
	/** Grow the underlay beyond the face, in em. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Underlay", meta = (UIMin = "-0.2", UIMax = "0.5"))
	float UnderlayDilate = 0.0f;

	/** Glow colour; alpha scales the glow's strength, 0 means no glow. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glow")
	FColor GlowColor = FColor(255, 255, 255, 0);
	/**
	 * How far the glow reaches outside the face, in em. Every reach is clamped to what the font's
	 * field holds (SDFRadius / SampleFontSize em), so a wider glow needs a font with a wider spread.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glow", meta = (ClampMin = "0", UIMax = "1"))
	float GlowWidth = 0.0f;
	/** Glow falloff exponent; higher keeps the glow tight to the face. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glow", meta = (ClampMin = "0.01", UIMax = "8"))
	float GlowPower = 1.0f;

	/** Alpha of the part of a glyph run that the fill progress has not reached yet (lyrics). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fill", meta = (ClampMin = "0", ClampMax = "1"))
	float FillDimAlpha = 0.35f;
	/** Width of the lit/unlit transition as a fraction of the run, 0..1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fill", meta = (ClampMin = "0.001", ClampMax = "1"))
	float FillFadeWidth = 0.15f;

	bool operator==(const FDreamTextStyle& Other) const;
	bool operator!=(const FDreamTextStyle& Other) const { return !(*this == Other); }

	/** Whether anything besides the face is drawn (outline, glow or underlay with a visible colour). */
	bool HasEffects() const;
	/** How far outside the glyph's edge the face reaches, in em: dilation (plus ExtraDilateEm, e.g. bold) and half the softness band. */
	float GetFaceReachEm(float ExtraDilateEm) const;
	/** How far outside the glyph's edge the effects reach, in em, with the glow at its widest boost. */
	float GetEffectReachEm(float ExtraDilateEm, float MaxGlowBoost) const;

	/** Number of R32 pixels the packed style takes in the widget property record. */
	static constexpr int32 PackedPixelCount = 9;
	/** Pixel index of the first style pixel in the record (after the four every widget has). */
	static constexpr int32 PackedPixelStart = 4;
	/** Packs the style the way DreamUIText.ush's DreamUIText_ReadStyle reads it: PackedPixelCount * 4 bytes. */
	void Pack(TArray<uint8>& OutBytes) const;
};

/**
 * A run of characters that fills together, for lyric-style progress: the characters from
 * StartCharIndex to EndCharIndex (inclusive, indices into the text) sweep from unlit to lit as
 * Progress goes 0..1, left to right across the run's glyphs.
 */
USTRUCT(BlueprintType, Category = DreamGUI)
struct DREAMGUI_API FDreamTextFillSegment
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = DreamGUI)
	int32 StartCharIndex = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = DreamGUI)
	int32 EndCharIndex = 0;
	/** 0 = none of the run is lit, 1 = all of it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = DreamGUI, meta = (ClampMin = "0", ClampMax = "1"))
	float Progress = 1.0f;
	/** Extra glow for this run, added to the style's GlowWidth as a fraction (1 = twice as wide). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = DreamGUI, meta = (ClampMin = "0"))
	float GlowBoost = 0.0f;
};

/** single char property */
struct FDreamUITextCaretProperty
{
	/** caret position. caret is on left side of char */
	FVector2f CaretPosition = FVector2f::ZeroVector;
	/** char index in text, -1 means line end caret */
	int32 CharIndex = 0;
};
/** a line of text property */
struct FDreamUITextLineProperty
{
	TArray<FDreamUITextCaretProperty> CaretPropertyList;
};
/** for range selection in TextInputComponent */
struct FDreamUITextSelectionProperty
{
	FVector2f Pos = FVector2f::ZeroVector;
	int32 Size = 0;
};
/** char property */
USTRUCT(BlueprintType, Category = DreamGUI)
struct FDreamUITextCharProperty
{
	GENERATED_BODY()
	/** char index in string */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = DreamGUI) int32 CharIndex = 0;
	/** vertex index in UIGeometry::vertices */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = DreamGUI) int32 StartVertIndex = 0;
	/** vertex count */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = DreamGUI) int32 VertCount = 0;
	/** triangle index in UIGeometry::triangles */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = DreamGUI) int32 StartTriangleIndex = 0;
	/** triangle indices count */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = DreamGUI) int32 IndicesCount = 0;

	/** center position of the char, in UIText's local space */
	//FVector2D CenterPosition;
};

USTRUCT(BlueprintType, Category = DreamGUI)
struct FDreamUIText_RichTextCustomTag
{
	GENERATED_BODY()
	/** Tag name */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = DreamGUI) FName TagName;
	/** start char index in cacheCharPropertyArray */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = DreamGUI) int32 CharIndexStart = 0;
	/** end char index in cacheCharPropertyArray */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = DreamGUI) int32 CharIndexEnd = 0;
	/**
	 * This range came from `<a=Id>` rather than `<Id>`: it is a hyperlink, and TagName is its id.
	 * UDreamText::FindHyperlinkByWorldPosition hit-tests these, and nothing else about them differs --
	 * a link is still a named character range, so a custom style and TextAnimation reach it as usual.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = DreamGUI) bool bHyperlink = false;
};

USTRUCT(BlueprintType, Category = DreamGUI)
struct FDreamUIText_RichTextImageTag
{
	GENERATED_BODY()
	/** Tag name */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = DreamGUI) FName TagName;
	/** image object position */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = DreamGUI) FVector2D Position = FVector2D::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = DreamGUI) FVector2D Size = FVector2D::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = DreamGUI) FColor TintColor = FColor::White;
};

USTRUCT(BlueprintType, Category = DreamGUI)
struct FDreamUIText_Emoji
{
	GENERATED_BODY()
	/** Emoji char */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = DreamGUI) int32 EmojiCode = 0;
	/** image object position */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = DreamGUI) FVector2D Position = FVector2D::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = DreamGUI) FVector2D Size = FVector2D::ZeroVector;
};

UENUM(BlueprintType, meta = (Bitflags), Category = DreamGUI)
enum class EDreamUIText_RichTextTagFilterFlags : uint8
{
	Bold, Italic, Underline, Strikethrough, Size, Color, Superscript, Subscript, CustomTag, Image,
	/** `<a=Id>...</a>`: a custom tag that can also be clicked. Appended, so saved flags keep their meaning. */
	Hyperlink
};
ENUM_CLASS_FLAGS(EDreamUIText_RichTextTagFilterFlags);

enum class EDreamUIText_CodeType
{
	Text = 0,
	Emoji = 1,
};
struct FDreamUIText_TextProcessingElement
{
	uint32 Unicode;
	int StringIndex;
	int Length;
	EDreamUIText_CodeType Type;
	/**
	 * The source spells this element as a rich-text character reference (`&lt;`), so its Length covers
	 * the reference while Unicode is the character it stands for. Anything that reads the source text
	 * back -- the line breaker's plain text, above all -- has to take the character, not the spelling.
	 */
	bool bEscaped = false;
};

/// <summary>
/// Commonly referenced Unicode characters in the text generation process.
/// </summary>
namespace FDreamUIText_CodePoint
{
	// constexpr uint32 SPACE = 0x20;
	// constexpr uint32 DOUBLE_QUOTE = 0x22;
	// constexpr uint32 NUMBER_SIGN = 0x23;
	// constexpr uint32 PERCENTAGE = 0x25;
	// constexpr uint32 PLUS = 0x2B;
	// constexpr uint32 MINUS = 0x2D;
	// constexpr uint32 PERIOD = 0x2E;
	//
	// constexpr uint32 HYPHEN_MINUS = 0x2D;
	// constexpr uint32 SOFT_HYPHEN = 0xAD;
	// constexpr uint32 HYPHEN = 0x2010;
	// constexpr uint32 NON_BREAKING_HYPHEN = 0x2011;
	// constexpr uint32 ZERO_WIDTH_SPACE = 0x200B;
	// constexpr uint32 RIGHT_SINGLE_QUOTATION = 0x2019;
	// constexpr uint32 APOSTROPHE = 0x27;
	// constexpr uint32 WORD_JOINER = 0x2060;
	constexpr uint32 HIGH_SURROGATE_START = 0xD800;
	constexpr uint32 HIGH_SURROGATE_END = 0xDBFF;
	constexpr uint32 LOW_SURROGATE_START = 0xDC00;
	constexpr uint32 LOW_SURROGATE_END = 0xDFFF;
	constexpr uint32 UNICODE_PLANE01_START = 0x10000;
	constexpr uint32 UNICODE_VS_BLACK = 0xFE0E;
	constexpr uint32 UNICODE_VS_COLOR = 0xFE0F;
	/** Zero width joiner: glues the parts of a family, a profession, a couple into one emoji. */
	constexpr uint32 UNICODE_ZWJ = 0x200D;
	/** Fitzpatrick skin tone modifiers, which follow the emoji they tint. */
	constexpr uint32 UNICODE_SKIN_TONE_START = 0x1F3FB;
	constexpr uint32 UNICODE_SKIN_TONE_END = 0x1F3FF;
	/** Regional indicators: exactly two of them make a flag. */
	constexpr uint32 UNICODE_REGIONAL_INDICATOR_START = 0x1F1E6;
	constexpr uint32 UNICODE_REGIONAL_INDICATOR_END = 0x1F1FF;
	/** Keycap: a digit, # or *, then U+FE0F, then this. */
	constexpr uint32 UNICODE_COMBINING_ENCLOSING_KEYCAP = 0x20E3;
	/** Tag characters, used by the subdivision flags (England, Scotland, Wales). */
	constexpr uint32 UNICODE_TAG_START = 0xE0020;
	constexpr uint32 UNICODE_TAG_END = 0xE007E;
	constexpr uint32 UNICODE_CANCEL_TAG = 0xE007F;

	inline uint32 ConvertToUTF32(uint32 highSurrogate, uint32 lowSurrogate)
	{
		return (((highSurrogate - FDreamUIText_CodePoint::HIGH_SURROGATE_START) << 10) | (lowSurrogate - FDreamUIText_CodePoint::LOW_SURROGATE_START)) + FDreamUIText_CodePoint::UNICODE_PLANE01_START;
	}
	inline bool IsEmoji(uint32 Codepoint)
	{
		// Emoji Block 1
		if (Codepoint >= 0x1F300 && Codepoint <= 0x1F5FF) return true;

		// Emoticons
		if (Codepoint >= 0x1F600 && Codepoint <= 0x1F64F) return true;

		// Transport & Map Symbols
		if (Codepoint >= 0x1F680 && Codepoint <= 0x1F6FF) return true;

		// Supplemental Symbols and Pictographs
		if (Codepoint >= 0x1F900 && Codepoint <= 0x1F9FF) return true;

		// Symbols and Pictographs Extended-A
		if (Codepoint >= 0x1FA70 && Codepoint <= 0x1FAFF) return true;

		// Flags (Regional Indicator Symbols)
		if (Codepoint >= 0x1F1E6 && Codepoint <= 0x1F1FF) return true;

		return false;
	}
	/**
	 * Code points that are emoji but whose DEFAULT presentation is text -- Unicode's Emoji=Yes,
	 * Emoji_Presentation=No. They stay font glyphs (which is what a text font draws for them, and what
	 * this pipeline has always drawn) unless U+FE0F asks for the emoji form. Approximated by block:
	 * carrying the real property table is not worth it here, and every case this rounds the wrong way
	 * rounds towards "render it with the font", which is the behaviour that was already there.
	 */
	inline bool IsTextPresentationEmoji(uint32 Codepoint)
	{
		if (Codepoint == 0x00A9 || Codepoint == 0x00AE || Codepoint == 0x2122) return true;//(c) (r) (tm)
		if (Codepoint == 0x203C || Codepoint == 0x2049) return true;//!! !?
		if (Codepoint == 0x2139) return true;//information
		if (Codepoint >= 0x2190 && Codepoint <= 0x21FF) return true;//arrows
		if (Codepoint >= 0x2300 && Codepoint <= 0x23FF) return true;//misc technical
		if (Codepoint >= 0x24C2 && Codepoint <= 0x24C2) return true;//circled M
		if (Codepoint >= 0x25A0 && Codepoint <= 0x25FF) return true;//geometric shapes
		if (Codepoint >= 0x2600 && Codepoint <= 0x27BF) return true;//misc symbols and dingbats
		if (Codepoint >= 0x2934 && Codepoint <= 0x2935) return true;
		if (Codepoint >= 0x2B00 && Codepoint <= 0x2BFF) return true;//misc symbols and arrows
		if (Codepoint == 0x3030 || Codepoint == 0x303D) return true;
		if (Codepoint == 0x3297 || Codepoint == 0x3299) return true;
		if (Codepoint >= 0x1F000 && Codepoint <= 0x1F2FF) return true;//mahjong, cards, enclosed
		return false;
	}
	inline bool IsVariationSelector(uint32 Codepoint)
	{
		return Codepoint == UNICODE_VS_BLACK || Codepoint == UNICODE_VS_COLOR;
	}
	inline bool IsSkinToneModifier(uint32 Codepoint)
	{
		return Codepoint >= UNICODE_SKIN_TONE_START && Codepoint <= UNICODE_SKIN_TONE_END;
	}
	inline bool IsRegionalIndicator(uint32 Codepoint)
	{
		return Codepoint >= UNICODE_REGIONAL_INDICATOR_START && Codepoint <= UNICODE_REGIONAL_INDICATOR_END;
	}
	inline bool IsTagCharacter(uint32 Codepoint)
	{
		return (Codepoint >= UNICODE_TAG_START && Codepoint <= UNICODE_TAG_END) || Codepoint == UNICODE_CANCEL_TAG;
	}
	inline bool IsKeycapBase(uint32 Codepoint)
	{
		return (Codepoint >= '0' && Codepoint <= '9') || Codepoint == '#' || Codepoint == '*';
	}
	/** Decodes one code point at InCharIndex, pairing surrogates. OutCodeUnits is 1 or 2. */
	inline uint32 DecodeCodePointAt(const FString& InString, int InStringLen, int InCharIndex, int& OutCodeUnits)
	{
		const uint32 First = (uint32)InString[InCharIndex];
		if (First >= HIGH_SURROGATE_START && First <= HIGH_SURROGATE_END && InCharIndex + 1 < InStringLen)
		{
			const uint32 Second = (uint32)InString[InCharIndex + 1];
			if (Second >= LOW_SURROGATE_START && Second <= LOW_SURROGATE_END)
			{
				OutCodeUnits = 2;
				return ConvertToUTF32(First, Second);
			}
		}
		OutCodeUnits = 1;
		return First;
	}
	/**
	 * Reads one element -- an emoji grapheme cluster, or a single code point -- starting at
	 * InOutCharIndex, and leaves the index on the cluster's LAST code unit, because every caller is a
	 * for-loop whose increment steps past it.
	 *
	 * A cluster is a base code point plus, in this order: a variation selector (which decides emoji or
	 * text presentation), a keycap mark, a partner regional indicator, and then any run of skin tone
	 * modifiers, tag characters and ZWJ-joined emoji. That is the subset of UAX #29 emoji needs. The
	 * element's Unicode is the cluster's BASE code point -- the emoji atlas is keyed by one code point
	 * (FDreamUIFontEmojiKey), so a sequence registers and looks up under its base, which is also why
	 * ApplyEmoji accepts the whole sequence but keys it the same way.
	 *
	 * Pure function of the string: no font, no asset, so it is directly unit testable.
	 */
	inline FDreamUIText_TextProcessingElement ReadCodePoint(const FString& InString, int InStringLen, int& InOutCharIndex)
	{
		const int32 Start = InOutCharIndex;
		int BaseUnits = 0;
		const uint32 Base = DecodeCodePointAt(InString, InStringLen, Start, BaseUnits);
		int32 Cursor = Start + BaseUnits;

		bool bEmoji = IsEmoji(Base);
		const bool bEmojiCapable = bEmoji || IsTextPresentationEmoji(Base) || IsKeycapBase(Base);

		auto PeekAt = [&InString, InStringLen](int32 At, uint32& OutCode, int32& OutEnd) -> bool
		{
			if (At >= InStringLen)return false;
			int Units = 0;
			OutCode = DecodeCodePointAt(InString, InStringLen, At, Units);
			OutEnd = At + Units;
			return true;
		};

		uint32 Next = 0;
		int32 NextEnd = 0;
		// A variation selector always belongs to the character before it; swallowing it even on a plain
		// glyph is what stops it from becoming an element of its own, i.e. a tofu box.
		if (PeekAt(Cursor, Next, NextEnd) && IsVariationSelector(Next))
		{
			if (bEmojiCapable)
			{
				bEmoji = Next == UNICODE_VS_COLOR;
			}
			Cursor = NextEnd;
		}
		if (IsKeycapBase(Base) && PeekAt(Cursor, Next, NextEnd) && Next == UNICODE_COMBINING_ENCLOSING_KEYCAP)
		{
			bEmoji = true;
			Cursor = NextEnd;
		}
		else if (IsRegionalIndicator(Base) && PeekAt(Cursor, Next, NextEnd) && IsRegionalIndicator(Next))
		{
			bEmoji = true;//a flag is exactly two regional indicators
			Cursor = NextEnd;
		}
		if (bEmoji)
		{
			while (PeekAt(Cursor, Next, NextEnd))
			{
				if (IsSkinToneModifier(Next) || IsVariationSelector(Next) || IsTagCharacter(Next)
					|| Next == UNICODE_COMBINING_ENCLOSING_KEYCAP)
				{
					Cursor = NextEnd;
					continue;
				}
				if (Next == UNICODE_ZWJ)
				{
					uint32 Joined = 0;
					int32 JoinedEnd = 0;
					// A joiner with nothing joinable after it is not part of the cluster: leaving it
					// out keeps a trailing ZWJ from eating the next character.
					if (PeekAt(NextEnd, Joined, JoinedEnd) && (IsEmoji(Joined) || IsTextPresentationEmoji(Joined)))
					{
						Cursor = JoinedEnd;
						continue;
					}
				}
				break;
			}
		}

		FDreamUIText_TextProcessingElement Element;
		Element.Unicode = Base;
		Element.StringIndex = Start;
		Element.Length = Cursor - Start;
		Element.Type = bEmoji ? EDreamUIText_CodeType::Emoji : EDreamUIText_CodeType::Text;
		InOutCharIndex = Cursor - 1;
		return Element;
	}
};

struct FDreamTextLayoutInput;
struct FDreamTextDisplayList;
struct FDreamTextPaintParams;

/**
 * The text component's layout cache: the last layout input, the display list it produced, and the
 * char properties the last paint wrote. Layout and paint are separate steps. A layout is re-run only
 * when its input changes (or the font says its glyphs did); a paint runs every time the geometry is
 * rebuilt, from the cached display list, which is cheap.
 */
struct DREAMGUI_API FDreamUITextGeometryCache
{
public:
	FDreamUITextGeometryCache();
	~FDreamUITextGeometryCache();
	FDreamUITextGeometryCache(const FDreamUITextGeometryCache&) = delete;
	FDreamUITextGeometryCache& operator=(const FDreamUITextGeometryCache&) = delete;

	/** Replace the layout input. Returns true when the layout is now stale. */
	bool SetLayoutInput(const FDreamTextLayoutInput& InInput);
	const FDreamTextLayoutInput& GetLayoutInput() const;
	/** Force the next EnsureLayout to run, for when the glyphs changed underneath an unchanged input. */
	void MarkDirty();
	bool IsLayoutDirty() const { return bIsDirty; }
	/** How many layouts have run on this cache; what a test reads to prove a query was free. */
	int32 GetLayoutRunCount() const { return LayoutRunCount; }
	/** Lays out if stale. Measures only -- no geometry is touched. Returns true if a layout ran. */
	bool EnsureLayout();
	/** Paints the display list into the geometry, laying out first if stale. */
	void Paint(FDreamUIGeometry& Geometry, const FDreamTextPaintParams& Params);

	const FDreamTextDisplayList& GetDisplayList() const;
	/** Emitted glyphs in order, as written by the last Paint. */
	const TArray<FDreamUITextCharProperty>& GetCharPropertyArray() const { return CharPropertyArray; }
	bool IsTextTruncated() const;
	FVector2f GetPreferredSize() const;
	const TArray<FDreamUITextLineProperty>& GetLines() const;
	const TArray<FDreamUIText_RichTextCustomTag>& GetCustomTags() const;
	const TArray<FDreamUIText_RichTextImageTag>& GetImageTags() const;
	const TArray<FDreamUIText_Emoji>& GetEmojis() const;

	/**
	 * Best Fit memo. The search probes several sizes, each a layout; remembering the answer for the
	 * input it was found against (with FontSize at the ceiling) makes the common call -- nothing
	 * changed -- free. MarkDirty forgets it, since a glyph change can move the answer.
	 */
	bool TryGetBestFit(const FDreamTextLayoutInput& InCeilingInput, float& OutSize) const;
	void SetBestFit(const FDreamTextLayoutInput& InCeilingInput, float InSize);
private:
	TUniquePtr<FDreamTextLayoutInput> Input;
	TUniquePtr<FDreamTextDisplayList> DisplayList;
	TArray<FDreamUITextCharProperty> CharPropertyArray;
	TUniquePtr<FDreamTextLayoutInput> BestFitKey;
	float BestFitSize = 0.0f;
	int32 LayoutRunCount = 0;
	bool bIsDirty = true;
};
