// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Extensions/Lyrics/DreamLyricsData.h"

/*
 * The lyric formats a player meets: Apple-style TTML with word spans, agents, backing vocals and
 * translations; plain and enhanced LRC. What matters is what the view reads off them: words with
 * times, the spaces between words, line order and flags.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLyricsParseTimeTest,
	"DreamGUI.Lyrics.ParseTimeReadsEveryClockFormat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamLyricsParseTimeTest::RunTest(const FString& Parameters)
{
	float S = 0.0f;
	TestTrue(TEXT("mm:ss.mmm"), UDreamLyricsLibrary::ParseTime(TEXT("01:02.500"), S)); TestEqual(TEXT("mm:ss.mmm value"), S, 62.5f, 0.001f);
	TestTrue(TEXT("hh:mm:ss.mmm"), UDreamLyricsLibrary::ParseTime(TEXT("1:01:02.250"), S)); TestEqual(TEXT("hh:mm:ss.mmm value"), S, 3662.25f, 0.001f);
	TestTrue(TEXT("ss.mmm"), UDreamLyricsLibrary::ParseTime(TEXT("12.75"), S)); TestEqual(TEXT("ss.mmm value"), S, 12.75f, 0.001f);
	TestTrue(TEXT("seconds suffix"), UDreamLyricsLibrary::ParseTime(TEXT("3.5s"), S)); TestEqual(TEXT("seconds suffix value"), S, 3.5f, 0.001f);
	TestTrue(TEXT("millis suffix"), UDreamLyricsLibrary::ParseTime(TEXT("1500ms"), S)); TestEqual(TEXT("millis suffix value"), S, 1.5f, 0.001f);
	TestFalse(TEXT("garbage"), UDreamLyricsLibrary::ParseTime(TEXT("soon"), S));
	TestFalse(TEXT("empty"), UDreamLyricsLibrary::ParseTime(TEXT(""), S));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLyricsParseTTMLTest,
	"DreamGUI.Lyrics.TTMLKeepsWordsSpacesAgentsAndRoles",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamLyricsParseTTMLTest::RunTest(const FString& Parameters)
{
	const FString Xml = TEXT(
		"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
		"<tt xmlns=\"http://www.w3.org/ns/ttml\" xmlns:ttm=\"http://www.w3.org/ns/ttml#metadata\" xmlns:itunes=\"http://music.apple.com/lyric-ttml-internal\">\n"
		"<head><metadata><ttm:agent type=\"person\" xml:id=\"v1\"/><ttm:agent type=\"person\" xml:id=\"v2\"/></metadata></head>\n"
		"<body dur=\"00:10.000\"><div begin=\"00:00.000\" end=\"00:10.000\">\n"
		"<p begin=\"00:01.000\" end=\"00:03.000\" ttm:agent=\"v1\" itunes:key=\"L1\">"
		"<span begin=\"00:01.000\" end=\"00:01.500\">Hello</span> <span begin=\"00:01.500\" end=\"00:03.000\">world</span>"
		"<span ttm:role=\"x-translation\" xml:lang=\"zh-Hans\">你好世界</span>"
		"</p>\n"
		"<p begin=\"00:03.000\" end=\"00:06.000\" ttm:agent=\"v2\">"
		"<span begin=\"00:03.000\" end=\"00:04.000\">Second</span> <span begin=\"00:04.000\" end=\"00:06.000\">voice</span>"
		"<span ttm:role=\"x-bg\" begin=\"00:04.500\" end=\"00:06.000\"><span begin=\"00:04.500\" end=\"00:06.000\">(echo)</span></span>"
		"</p>\n"
		"<p begin=\"00:06.000\" end=\"00:08.000\">Plain &amp; untimed line</p>\n"
		"<!-- a comment --><p begin=\"00:00.000\" end=\"00:00.900\">Intro</p>\n"
		"</div></body></tt>");

	FDreamLyrics Lyrics;
	FString Error;
	if (!TestTrue(FString::Printf(TEXT("parses (%s)"), *Error), UDreamLyricsLibrary::ParseTTML(Xml, Lyrics, Error)))return false;
	// Sorted by time: Intro (0.0), Hello (1.0), Second (3.0), its backing vocal (4.5), Plain (6.0).
	if (!TestEqual(TEXT("five lines"), Lyrics.Lines.Num(), 5))return false;
	TestEqual(TEXT("lines sort by start"), Lyrics.Lines[0].GetText(), FString(TEXT("Intro")));

	const FDreamLyricLine& Hello = Lyrics.Lines[1];
	TestEqual(TEXT("two words"), Hello.Words.Num(), 2);
	TestEqual(TEXT("the space between spans stays with the word before it"), Hello.Words[0].Text, FString(TEXT("Hello ")));
	TestEqual(TEXT("last word has no trailing space"), Hello.Words[1].Text, FString(TEXT("world")));
	TestEqual(TEXT("joined text"), Hello.GetText(), FString(TEXT("Hello world")));
	TestEqual(TEXT("word start"), Hello.Words[1].StartTime, 1.5f, 0.001f);
	TestEqual(TEXT("word end"), Hello.Words[1].EndTime, 3.0f, 0.001f);
	TestEqual(TEXT("line times"), Hello.EndTime, 3.0f, 0.001f);
	TestFalse(TEXT("lead agent is not secondary"), Hello.bSecondary);
	TestEqual(TEXT("translation"), Hello.Translation, FString(TEXT("你好世界")));

	const FDreamLyricLine& Second = Lyrics.Lines[2];
	TestTrue(TEXT("second agent is secondary"), Second.bSecondary);
	TestEqual(TEXT("backing vocal is not part of the line's words"), Second.Words.Num(), 2);
	const FDreamLyricLine& Echo = Lyrics.Lines[3];
	TestTrue(TEXT("backing vocal is its own flagged line"), Echo.bBackground && Echo.bSecondary);
	TestEqual(TEXT("backing vocal text"), Echo.GetText(), FString(TEXT("(echo)")));
	TestEqual(TEXT("backing vocal start"), Echo.StartTime, 4.5f, 0.001f);

	const FDreamLyricLine& Plain = Lyrics.Lines[4];
	TestEqual(TEXT("untimed line is one word"), Plain.Words.Num(), 1);
	TestEqual(TEXT("entity decoded"), Plain.Words[0].Text, FString(TEXT("Plain & untimed line")));
	TestEqual(TEXT("untimed word spans the line"), Plain.Words[0].EndTime, 8.0f, 0.001f);

	TestEqual(TEXT("line at 2.0s"), Lyrics.FindLineAtTime(2.0f), 1);
	TestEqual(TEXT("line before the first"), Lyrics.FindLineAtTime(-1.0f), INDEX_NONE);

	FDreamLyrics Bad;
	TestFalse(TEXT("broken xml fails with a message"), UDreamLyricsLibrary::ParseTTML(TEXT("<tt><body><p>oops</body></tt>"), Bad, Error));
	TestFalse(TEXT("and says what"), Error.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLyricsParseLRCTest,
	"DreamGUI.Lyrics.LRCPlainAndEnhanced",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamLyricsParseLRCTest::RunTest(const FString& Parameters)
{
	const FString Text = TEXT(
		"[ar:Someone]\n"
		"[00:05.00]First line\n"
		"[00:01.50]<00:01.50>Enhanced <00:02.00>words <00:02.80>here\n"
		"[00:10.00][00:20.00]Chorus\n"
		"\n"
		"[00:30.000]Last\n");
	FDreamLyrics Lyrics;
	if (!TestTrue(TEXT("parses"), UDreamLyricsLibrary::ParseLRC(Text, Lyrics)))return false;
	if (!TestEqual(TEXT("five lines (the chorus twice)"), Lyrics.Lines.Num(), 5))return false;
	TestEqual(TEXT("sorted: enhanced first"), Lyrics.Lines[0].GetText(), FString(TEXT("Enhanced words here")));
	TestEqual(TEXT("enhanced words"), Lyrics.Lines[0].Words.Num(), 3);
	TestEqual(TEXT("enhanced word time"), Lyrics.Lines[0].Words[1].StartTime, 2.0f, 0.001f);
	TestEqual(TEXT("enhanced word end is the next word's start"), Lyrics.Lines[0].Words[1].EndTime, 2.8f, 0.001f);
	TestEqual(TEXT("line end is the next line's start"), Lyrics.Lines[0].EndTime, 5.0f, 0.001f);
	TestEqual(TEXT("plain line is one word"), Lyrics.Lines[1].Words.Num(), 1);
	TestEqual(TEXT("plain word spans the line"), Lyrics.Lines[1].Words[0].EndTime, 10.0f, 0.001f);
	TestEqual(TEXT("chorus repeats"), Lyrics.Lines[3].GetText(), FString(TEXT("Chorus")));
	TestEqual(TEXT("chorus second time"), Lyrics.Lines[3].StartTime, 20.0f, 0.001f);
	TestEqual(TEXT("last line gets a default length"), Lyrics.Lines[4].EndTime, 35.0f, 0.001f);
	return true;
}

#endif
