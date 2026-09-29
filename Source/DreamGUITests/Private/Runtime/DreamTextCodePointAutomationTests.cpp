// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Core/DreamUITextData.h"

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
	return true;
}

#endif
