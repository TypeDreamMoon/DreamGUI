// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Core/DreamUITextData.h"
#include "Core/Text/DreamTextBreaker.h"
#include "Core/Text/DreamTextLayout.h"
#include "Engine/World.h"
#include "DreamTextTestFont.h"
#include "DreamScopedWorld.h"

#include <initializer_list>

/*
 * FDreamUIText_CodePoint::ReadCodePoint is where a string becomes the elements everything downstream
 * counts in: one caret step, one character index, one inline emoji object. It used to read exactly one
 * thing -- a surrogate pair, optionally with a variation selector -- so a BMP emoji was never an emoji,
 * a ZWJ family was five elements, a skin tone was an emoji plus a colour swatch, and a flag was two
 * letters in boxes. It is a pure function of the string, so this asserts on it directly: no font, no
 * widget, no atlas.
 */
namespace DreamTextCodePointTestLocal
{
	/** A string from code points, encoded the way FString holds them: UTF-16, surrogates and all. */
	FString Utf16(std::initializer_list<uint32> Codepoints)
	{
		FString Out;
		for (uint32 Codepoint : Codepoints)
		{
			if (Codepoint >= 0x10000)
			{
				const uint32 Value = Codepoint - 0x10000;
				Out.AppendChar((TCHAR)(0xD800 + (Value >> 10)));
				Out.AppendChar((TCHAR)(0xDC00 + (Value & 0x3FF)));
			}
			else
			{
				Out.AppendChar((TCHAR)Codepoint);
			}
		}
		return Out;
	}

	/** Every element the text pipeline reads out of a string, in order, driven exactly as it drives it. */
	TArray<FDreamUIText_TextProcessingElement> ReadAll(const FString& InString)
	{
		TArray<FDreamUIText_TextProcessingElement> Elements;
		const int Length = InString.Len();
		for (int CharIndex = 0; CharIndex < Length; CharIndex++)
		{
			Elements.Add(FDreamUIText_CodePoint::ReadCodePoint(InString, Length, CharIndex));
		}
		return Elements;
	}

	constexpr uint32 HEAVY_BLACK_HEART = 0x2764;
	constexpr uint32 HEAVY_CHECK_MARK = 0x2714;
	constexpr uint32 GRINNING_FACE = 0x1F600;
	constexpr uint32 MAN = 0x1F468;
	constexpr uint32 WOMAN = 0x1F469;
	constexpr uint32 GIRL = 0x1F467;
	constexpr uint32 THUMBS_UP = 0x1F44D;
	constexpr uint32 SKIN_TONE_MEDIUM = 0x1F3FD;
	constexpr uint32 REGIONAL_C = 0x1F1E8;
	constexpr uint32 REGIONAL_N = 0x1F1F3;
	constexpr uint32 REGIONAL_J = 0x1F1EF;
	constexpr uint32 REGIONAL_P = 0x1F1F5;
	constexpr uint32 WAVING_BLACK_FLAG = 0x1F3F4;
	constexpr uint32 INDEX_POINTING_UP = 0x261D;

	/** The grapheme-cluster starts the layout computes for a string's elements, from the elements ReadCodePoint reads. */
	TBitArray<> GraphemeStartsOf(const FString& InString, const TArray<FDreamUIText_TextProcessingElement>& Elements)
	{
		TArray<int32> PlainStart;
		TArray<uint32> Codepoints;
		for (const FDreamUIText_TextProcessingElement& Element : Elements)
		{
			PlainStart.Add(Element.StringIndex);
			Codepoints.Add(Element.Unicode);
		}
		TBitArray<> Starts;
		FDreamTextBreaker::ComputeGraphemeStarts(InString, PlainStart, Codepoints, Starts);
		return Starts;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextCodePointPlainTextIsUntouchedTest,
	"DreamGUI.Text.CodePoints.PlainTextStillReadsOneElementPerCodePoint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextCodePointPlainTextIsUntouchedTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextCodePointTestLocal;

	const TArray<FDreamUIText_TextProcessingElement> Ascii = ReadAll(TEXT("abc"));
	TestEqual(TEXT("three letters are three elements"), Ascii.Num(), 3);
	for (int32 i = 0; i < Ascii.Num(); i++)
	{
		TestEqual(TEXT("each one code unit long"), Ascii[i].Length, 1);
		TestEqual(TEXT("each starting where it is"), Ascii[i].StringIndex, i);
		TestTrue(TEXT("none of them emoji"), Ascii[i].Type == EDreamUIText_CodeType::Text);
	}

	// A lone astral emoji: the one case that always worked, still working.
	const TArray<FDreamUIText_TextProcessingElement> Face = ReadAll(Utf16({ GRINNING_FACE }));
	TestEqual(TEXT("a surrogate pair is one element"), Face.Num(), 1);
	TestEqual(TEXT("two code units long"), Face[0].Length, 2);
	TestTrue(TEXT("and it is an emoji"), Face[0].Type == EDreamUIText_CodeType::Emoji);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextCodePointVariationSelectorTest,
	"DreamGUI.Text.CodePoints.AVariationSelectorChoosesThePresentationInsteadOfBecomingATofuBox",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextCodePointVariationSelectorTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextCodePointTestLocal;
	const uint32 VS16 = FDreamUIText_CodePoint::UNICODE_VS_COLOR;
	const uint32 VS15 = FDreamUIText_CodePoint::UNICODE_VS_BLACK;

	// A heart on its own defaults to text presentation, which is what a text font draws for it.
	const TArray<FDreamUIText_TextProcessingElement> Bare = ReadAll(Utf16({ HEAVY_BLACK_HEART }));
	TestEqual(TEXT("a bare heart is one element"), Bare.Num(), 1);
	TestTrue(TEXT("drawn by the font, not the emoji atlas"), Bare[0].Type == EDreamUIText_CodeType::Text);

	// With U+FE0F it is asking for the emoji, and the selector belongs to it rather than standing
	// alone -- which is what used to put a box after every heart.
	const TArray<FDreamUIText_TextProcessingElement> Emoji = ReadAll(Utf16({ HEAVY_BLACK_HEART, VS16 }));
	TestEqual(TEXT("heart plus selector is ONE element"), Emoji.Num(), 1);
	TestEqual(TEXT("covering both code units"), Emoji[0].Length, 2);
	TestTrue(TEXT("and it is an emoji"), Emoji[0].Type == EDreamUIText_CodeType::Emoji);
	TestEqual(TEXT("named by its base code point"), (int32)Emoji[0].Unicode, (int32)HEAVY_BLACK_HEART);

	const TArray<FDreamUIText_TextProcessingElement> Check = ReadAll(Utf16({ HEAVY_CHECK_MARK, VS16 }));
	TestEqual(TEXT("a check mark behaves the same"), Check.Num(), 1);
	TestTrue(TEXT("as an emoji"), Check[0].Type == EDreamUIText_CodeType::Emoji);

	// U+FE0E asks for the opposite, on a code point that would otherwise be an emoji.
	const TArray<FDreamUIText_TextProcessingElement> Text = ReadAll(Utf16({ GRINNING_FACE, VS15 }));
	TestEqual(TEXT("face plus text selector is one element"), Text.Num(), 1);
	TestEqual(TEXT("three code units"), Text[0].Length, 3);
	TestTrue(TEXT("presented as text"), Text[0].Type == EDreamUIText_CodeType::Text);

	// A selector after something that is not emoji at all is still invisible, not a box.
	const TArray<FDreamUIText_TextProcessingElement> Letter = ReadAll(Utf16({ 'a', VS16, 'b' }));
	TestEqual(TEXT("a, its selector, b"), Letter.Num(), 2);
	TestEqual(TEXT("the selector rides with the a"), Letter[0].Length, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextCodePointClusterTest,
	"DreamGUI.Text.CodePoints.ZwjSequencesSkinTonesAndFlagsAreOneElementEach",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextCodePointClusterTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextCodePointTestLocal;
	const uint32 ZWJ = FDreamUIText_CodePoint::UNICODE_ZWJ;

	// Man + ZWJ + woman + ZWJ + girl: one family, not three people and two invisible boxes.
	const TArray<FDreamUIText_TextProcessingElement> Family = ReadAll(Utf16({ MAN, ZWJ, WOMAN, ZWJ, GIRL }));
	TestEqual(TEXT("a ZWJ sequence is one element"), Family.Num(), 1);
	TestEqual(TEXT("covering all eight code units"), Family[0].Length, 8);
	TestTrue(TEXT("and it is an emoji"), Family[0].Type == EDreamUIText_CodeType::Emoji);
	TestEqual(TEXT("named by the first of the sequence"), (int32)Family[0].Unicode, (int32)MAN);

	// A skin tone modifier belongs to the emoji before it, not to itself.
	const TArray<FDreamUIText_TextProcessingElement> Tinted = ReadAll(Utf16({ THUMBS_UP, SKIN_TONE_MEDIUM }));
	TestEqual(TEXT("a tinted emoji is one element"), Tinted.Num(), 1);
	TestEqual(TEXT("four code units"), Tinted[0].Length, 4);
	TestEqual(TEXT("named by the emoji, not the tone"), (int32)Tinted[0].Unicode, (int32)THUMBS_UP);

	// Two regional indicators are a flag; four are two flags, not one long one.
	const TArray<FDreamUIText_TextProcessingElement> Flag = ReadAll(Utf16({ REGIONAL_C, REGIONAL_N }));
	TestEqual(TEXT("a flag is one element"), Flag.Num(), 1);
	TestEqual(TEXT("four code units"), Flag[0].Length, 4);
	TestTrue(TEXT("and an emoji"), Flag[0].Type == EDreamUIText_CodeType::Emoji);

	const TArray<FDreamUIText_TextProcessingElement> TwoFlags = ReadAll(Utf16({ REGIONAL_C, REGIONAL_N, REGIONAL_J, REGIONAL_P }));
	TestEqual(TEXT("two flags are two elements"), TwoFlags.Num(), 2);
	TestEqual(TEXT("the second starting after the first"), TwoFlags[1].StringIndex, 4);

	// Keycap: the digit, the selector and the enclosing mark are one key.
	const TArray<FDreamUIText_TextProcessingElement> Keycap = ReadAll(Utf16({ '1', FDreamUIText_CodePoint::UNICODE_VS_COLOR, FDreamUIText_CodePoint::UNICODE_COMBINING_ENCLOSING_KEYCAP }));
	TestEqual(TEXT("a keycap is one element"), Keycap.Num(), 1);
	TestEqual(TEXT("three code units"), Keycap[0].Length, 3);
	TestTrue(TEXT("and an emoji"), Keycap[0].Type == EDreamUIText_CodeType::Emoji);

	// A joiner with nothing joinable after it must not swallow whatever comes next.
	const TArray<FDreamUIText_TextProcessingElement> Dangling = ReadAll(Utf16({ MAN, ZWJ }));
	TestEqual(TEXT("a dangling joiner stays outside the cluster"), Dangling.Num(), 2);
	TestEqual(TEXT("so the emoji is just the emoji"), Dangling[0].Length, 2);

	// A family whose members carry skin tones: every member, tone and joiner in one element.
	const TArray<FDreamUIText_TextProcessingElement> TintedFamily = ReadAll(Utf16({ MAN, SKIN_TONE_MEDIUM, ZWJ, WOMAN, SKIN_TONE_MEDIUM, ZWJ, GIRL, SKIN_TONE_MEDIUM }));
	TestEqual(TEXT("a family with skin tones is one element"), TintedFamily.Num(), 1);
	TestEqual(TEXT("covering all fourteen code units"), TintedFamily[0].Length, 14);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextCodePointThirdFlagLetterTest,
	"DreamGUI.Text.CodePoints.AThirdFlagLetterIsALetterOfItsOwnNotHalfAFlag",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextCodePointThirdFlagLetterTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextCodePointTestLocal;

	// J P C: the first two are Japan's flag; the C has no partner (UAX #29 GB12/GB13 pair them from the start).
	const FString Letters = Utf16({ REGIONAL_J, REGIONAL_P, REGIONAL_C });
	const TArray<FDreamUIText_TextProcessingElement> Elements = ReadAll(Letters);
	if (!TestEqual(TEXT("a flag and a lone letter are two elements"), Elements.Num(), 2))return false;
	TestEqual(TEXT("the flag is the first two letters"), Elements[0].Length, 4);
	TestTrue(TEXT("and an emoji"), Elements[0].Type == EDreamUIText_CodeType::Emoji);
	TestEqual(TEXT("the third letter starts after them"), Elements[1].StringIndex, 4);
	TestEqual(TEXT("and is one code point"), Elements[1].Length, 2);
	TestTrue(TEXT("a lone regional indicator has emoji presentation of its own"), Elements[1].Type == EDreamUIText_CodeType::Emoji);
	const TBitArray<> Starts = GraphemeStartsOf(Letters, Elements);
	TestTrue(TEXT("and it is a grapheme cluster of its own, not part of the flag"), Starts.Num() == 2 && Starts[1]);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextCodePointTextKeycapTest,
	"DreamGUI.Text.CodePoints.AKeycapAskedForInTextFormStaysText",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * U+FE0E between a digit and the enclosing keycap asks for the text form, and the keycap mark used to turn the cluster
 * back into an emoji whatever the selector had said.
 */
bool FDreamTextCodePointTextKeycapTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextCodePointTestLocal;
	const uint32 VS15 = FDreamUIText_CodePoint::UNICODE_VS_BLACK;
	const uint32 VS16 = FDreamUIText_CodePoint::UNICODE_VS_COLOR;
	const uint32 KEYCAP = FDreamUIText_CodePoint::UNICODE_COMBINING_ENCLOSING_KEYCAP;

	const TArray<FDreamUIText_TextProcessingElement> TextKeycap = ReadAll(Utf16({ '1', VS15, KEYCAP }));
	if (TestEqual(TEXT("a keycap with U+FE0E is one element"), TextKeycap.Num(), 1))
	{
		TestEqual(TEXT("digit, selector and mark"), TextKeycap[0].Length, 3);
		TestTrue(TEXT("and it stays text, as the selector asked"), TextKeycap[0].Type == EDreamUIText_CodeType::Text);
	}
	const TArray<FDreamUIText_TextProcessingElement> EmojiKeycap = ReadAll(Utf16({ '#', VS16, KEYCAP }));
	if (TestEqual(TEXT("a keycap with U+FE0F is one element"), EmojiKeycap.Num(), 1))
	{
		TestTrue(TEXT("and an emoji"), EmojiKeycap[0].Type == EDreamUIText_CodeType::Emoji);
	}
	const TArray<FDreamUIText_TextProcessingElement> BareKeycap = ReadAll(Utf16({ '7', KEYCAP }));
	if (TestEqual(TEXT("a keycap with no selector is one element"), BareKeycap.Num(), 1))
	{
		TestTrue(TEXT("and an emoji, as Unicode defaults a keycap"), BareKeycap[0].Type == EDreamUIText_CodeType::Emoji);
	}
	const TArray<FDreamUIText_TextProcessingElement> Digit = ReadAll(Utf16({ '7', VS15 }));
	if (TestEqual(TEXT("a digit and its selector are one element"), Digit.Num(), 1))
	{
		TestTrue(TEXT("and a digit is text"), Digit[0].Type == EDreamUIText_CodeType::Text);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextCodePointTagSequenceTest,
	"DreamGUI.Text.CodePoints.ATagSequenceFlagIsOneElement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextCodePointTagSequenceTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextCodePointTestLocal;

	// England: a black flag, the tags g b e n g, and the cancel tag.
	const FString England = Utf16({ WAVING_BLACK_FLAG, 0xE0067, 0xE0062, 0xE0065, 0xE006E, 0xE0067, 0xE007F });
	const TArray<FDreamUIText_TextProcessingElement> Flag = ReadAll(England + TEXT("x"));
	if (!TestEqual(TEXT("the subdivision flag and the letter after it are two elements"), Flag.Num(), 2))return false;
	TestEqual(TEXT("the flag holds its seven code points, fourteen code units"), Flag[0].Length, 14);
	TestTrue(TEXT("and is an emoji"), Flag[0].Type == EDreamUIText_CodeType::Emoji);
	TestEqual(TEXT("named by the flag it starts with"), (int32)Flag[0].Unicode, (int32)WAVING_BLACK_FLAG);
	TestEqual(TEXT("the letter follows the cancel tag"), Flag[1].StringIndex, 14);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextCodePointSkinToneOnALetterTest,
	"DreamGUI.Text.CodePoints.ASkinToneJoinsTheClusterOfWhateverItFollows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A skin tone after a letter is its own element -- a face draws it as a swatch -- but the same grapheme cluster as the
 * letter (UAX #29 GB9), so the two have one caret and are never split; the shaper draws each part from a face that has
 * it. After a pictograph, even one that is text by default, the tone makes an emoji modifier sequence: one element.
 */
bool FDreamTextCodePointSkinToneOnALetterTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextCodePointTestLocal;

	const FString Tinted = Utf16({ 'a', SKIN_TONE_MEDIUM });
	const TArray<FDreamUIText_TextProcessingElement> Elements = ReadAll(Tinted);
	if (!TestEqual(TEXT("the letter and the tone are two elements"), Elements.Num(), 2))return false;
	TestTrue(TEXT("the letter is text"), Elements[0].Type == EDreamUIText_CodeType::Text);
	TestEqual(TEXT("the tone starts after it"), Elements[1].StringIndex, 1);
	TestTrue(TEXT("and has emoji presentation"), Elements[1].Type == EDreamUIText_CodeType::Emoji);
	const TBitArray<> Starts = GraphemeStartsOf(Tinted, Elements);
	TestTrue(TEXT("but they are one grapheme cluster"), Starts.Num() == 2 && Starts[0] && !Starts[1]);

	const TArray<FDreamUIText_TextProcessingElement> Pointing = ReadAll(Utf16({ INDEX_POINTING_UP, SKIN_TONE_MEDIUM }));
	if (TestEqual(TEXT("a pictograph with a tone is one element"), Pointing.Num(), 1))
	{
		TestEqual(TEXT("three code units"), Pointing[0].Length, 3);
		TestTrue(TEXT("and an emoji, though the pictograph alone is text"), Pointing[0].Type == EDreamUIText_CodeType::Emoji);
	}
	const TArray<FDreamUIText_TextProcessingElement> Bare = ReadAll(Utf16({ INDEX_POINTING_UP }));
	if (TestEqual(TEXT("the pictograph alone is one element"), Bare.Num(), 1))
	{
		TestTrue(TEXT("and text by default"), Bare[0].Type == EDreamUIText_CodeType::Text);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextCodePointEscapedSequenceTest,
	"DreamGUI.Text.CodePoints.AnEmojiSpelledAsCharacterReferencesIsStillOneCluster",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * In rich text a thumbs-up and its skin tone can be spelled as two character references. Each is an element -- an
 * escaped element is always the one character it stands for -- but together they are one grapheme cluster, so there is
 * one caret before them and none between, as for the same emoji typed as itself.
 */
bool FDreamTextCodePointEscapedSequenceTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextCodePointTestLocal;
	DreamTests::FScopedGameWorld TestWorld;
	UDreamTextTestFont* Font = NewObject<UDreamTextTestFont>(TestWorld.World);

	auto Layout = [Font](const FString& Content, FDreamTextDisplayList& OutDL)
	{
		FDreamTextLayoutInput In;
		In.Content = Content;
		In.Width = 600.0f;
		In.Height = 200.0f;
		In.Pivot = FVector2f(0.5f, 0.5f);
		In.FontSize = 24.0f;
		In.bRichText = true;
		In.Font = Font;
		FDreamTextLayoutEngine::Layout(In, OutDL);
	};

	FDreamTextDisplayList Escaped;
	Layout(TEXT("&#x1F44D;&#x1F3FD;b"), Escaped);
	if (!TestEqual(TEXT("one line"), Escaped.Lines.Num(), 1))return false;
	const TArray<FDreamUITextCaretProperty>& Carets = Escaped.Lines[0].CaretPropertyList;
	if (TestEqual(TEXT("a caret before the emoji, one before b and the end caret"), Carets.Num(), 3))
	{
		TestEqual(TEXT("the first stands before the first reference"), Carets[0].CharIndex, 0);
		TestEqual(TEXT("the second before b, past both references"), Carets[1].CharIndex, 18);
		TestEqual(TEXT("the end caret at the end of the text"), Carets[2].CharIndex, 19);
	}
	TestEqual(TEXT("with no emoji data both references are glyphs, and b"), Escaped.VisibleCharCount, 3);
	TestEqual(TEXT("and no emoji object is asked for"), Escaped.Emojis.Num(), 0);

	FDreamTextDisplayList Typed;
	Layout(Utf16({ THUMBS_UP, SKIN_TONE_MEDIUM, 'b' }), Typed);
	if (TestEqual(TEXT("typed: one line"), Typed.Lines.Num(), 1))
	{
		TestEqual(TEXT("typed: the same three carets"), Typed.Lines[0].CaretPropertyList.Num(), 3);
	}
	TestEqual(TEXT("typed, the emoji is one element and one character"), Typed.VisibleCharCount, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextCodePointClassificationTest,
	"DreamGUI.Text.CodePoints.EmojiPresentationIsIcusAnswerAndTheTablesForWhatIcuDoesNotKnow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * HasEmojiPresentation is ICU's answer for a code point ICU knows -- its data is Unicode 12 -- and the table's for one
 * encoded since (and for every code point in a build without ICU); IsExtendedPictographic likewise. Where ICU is built
 * in, the two must agree on every code point, which holds the tables to ICU; an Emoji 14 code point only the table knows.
 */
bool FDreamTextCodePointClassificationTest::RunTest(const FString& Parameters)
{
	struct FCase
	{
		uint32 Codepoint;
		bool bEmojiPresentation;
		bool bPictograph;
		const TCHAR* Name;
	};
	const FCase Cases[] =
	{
		{ 'A', false, false, TEXT("a letter") },
		{ '1', false, false, TEXT("a digit, which a keycap starts with") },
		{ 0x00A9, false, true, TEXT("the copyright sign") },
		{ 0x2764, false, true, TEXT("the heavy black heart, text by default") },
		{ 0x231A, true, true, TEXT("the watch") },
		{ 0x1F600, true, true, TEXT("the grinning face") },
		{ 0x1F3F3, false, true, TEXT("the white flag, text by default") },
		{ 0x1F3FD, true, false, TEXT("a skin tone, which is no pictograph") },
		{ 0x1F1E6, true, false, TEXT("a regional indicator") },
		{ 0x1FAE0, true, true, TEXT("the melting face, Emoji 14, which ICU 64 does not know") },
		{ 0x1FAE8, true, true, TEXT("the shaking face, Emoji 15") },
		{ 0x1FAE9, true, true, TEXT("the face with bags under its eyes, Emoji 16") },
	};
	for (const FCase& Case : Cases)
	{
		TestEqual(*FString::Printf(TEXT("%s (U+%04X): emoji presentation"), Case.Name, Case.Codepoint),
			FDreamUIText_CodePoint::HasEmojiPresentation(Case.Codepoint), Case.bEmojiPresentation);
		TestEqual(*FString::Printf(TEXT("%s (U+%04X): the table's emoji presentation"), Case.Name, Case.Codepoint),
			FDreamUIText_CodePoint::HasEmojiPresentationFromTable(Case.Codepoint), Case.bEmojiPresentation);
		TestEqual(*FString::Printf(TEXT("%s (U+%04X): a pictograph"), Case.Name, Case.Codepoint),
			FDreamUIText_CodePoint::IsExtendedPictographic(Case.Codepoint), Case.bPictograph);
		TestEqual(*FString::Printf(TEXT("%s (U+%04X): the table's pictograph"), Case.Name, Case.Codepoint),
			FDreamUIText_CodePoint::IsExtendedPictographicFromTable(Case.Codepoint), Case.bPictograph);
	}

	// Every code point of the planes emoji live in: what the classifiers answer is what the tables answer.
	int32 PresentationDisagreements = 0;
	int32 PictographDisagreements = 0;
	for (uint32 Codepoint = 0; Codepoint < 0x40000; Codepoint++)
	{
		if (FDreamUIText_CodePoint::HasEmojiPresentation(Codepoint) != FDreamUIText_CodePoint::HasEmojiPresentationFromTable(Codepoint))
		{
			if (PresentationDisagreements++ < 8)
			{
				AddError(FString::Printf(TEXT("U+%04X: Emoji_Presentation is %d from ICU and %d from the table"), Codepoint,
					(int32)FDreamUIText_CodePoint::HasEmojiPresentation(Codepoint), (int32)FDreamUIText_CodePoint::HasEmojiPresentationFromTable(Codepoint)));
			}
		}
		if (FDreamUIText_CodePoint::IsExtendedPictographic(Codepoint) != FDreamUIText_CodePoint::IsExtendedPictographicFromTable(Codepoint))
		{
			if (PictographDisagreements++ < 8)
			{
				AddError(FString::Printf(TEXT("U+%04X: Extended_Pictographic is %d from ICU and %d from the table"), Codepoint,
					(int32)FDreamUIText_CodePoint::IsExtendedPictographic(Codepoint), (int32)FDreamUIText_CodePoint::IsExtendedPictographicFromTable(Codepoint)));
			}
		}
	}
	TestEqual(TEXT("ICU and the Emoji_Presentation table agree everywhere"), PresentationDisagreements, 0);
	TestEqual(TEXT("ICU and the Extended_Pictographic table agree everywhere"), PictographDisagreements, 0);
	return true;
}

#endif
