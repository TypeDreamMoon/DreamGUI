// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Core/Text/DreamFontFaceResolver.h"
#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"

/*
 * Face resolution as the pure function it is: a face table, a lambda for what each face has and one for which faces are
 * colour faces. No FreeType and no font asset -- the shaper and layout tests cover the real fonts.
 */
namespace DreamFontFaceResolverTestLocal
{
	/** A family of made-up faces: what each has and which are colour faces, and how often the resolver asked about each. */
	struct FMockFaces
	{
		TArray<TSet<uint32>> Has;
		TArray<bool> Color;
		TArray<int32> CoverageAsked;
		TArray<int32> ColorAsked;

		explicit FMockFaces(int32 Count)
		{
			Has.SetNum(Count);
			Color.Init(false, Count);
			CoverageAsked.Init(0, Count);
			ColorAsked.Init(0, Count);
		}

		void ResetAsked()
		{
			CoverageAsked.Init(0, Has.Num());
			ColorAsked.Init(0, Has.Num());
		}

		/** Resolves Cluster with every face but the style faces regular (RegularFaces of them). */
		FDreamFontFaceChoice Resolve(const FDreamFontFaceTable& Table, int32 RegularFaces, const TArray<uint32>& Cluster,
			TConstArrayView<FString> Cultures = TConstArrayView<FString>(), int32 StyledFace = 0, bool bAllowColorFaces = true)
		{
			FDreamFontFaceQuery Query;
			Query.Cluster = Cluster;
			Query.Cultures = Cultures;
			Query.StyledFace = StyledFace;
			Query.bAllowColorFaces = bAllowColorFaces;
			Query.Presentation = FDreamFontFaceResolver::GetPresentation(Query.Cluster);
			return FDreamFontFaceResolver::Resolve(Table, RegularFaces, Query,
				[this](int32 FaceIndex, uint32 Codepoint)
				{
					if (!Has.IsValidIndex(FaceIndex))
					{
						return false;
					}
					CoverageAsked[FaceIndex]++;
					return Has[FaceIndex].Contains(Codepoint);
				},
				[this](int32 FaceIndex)
				{
					if (!Color.IsValidIndex(FaceIndex))
					{
						return false;
					}
					ColorAsked[FaceIndex]++;
					return Color[FaceIndex];
				});
		}
	};

	/** A table of Count faces with the defaults: every code point, any language, scale 1, not preferred. */
	FDreamFontFaceTable MakeTable(int32 Count)
	{
		FDreamFontFaceTable Table;
		Table.Faces.SetNum(Count);
		return Table;
	}

	bool ContainsCulture(const TArray<FString>& Names, const TCHAR* Name)
	{
		return Names.ContainsByPredicate([Name](const FString& Candidate) { return Candidate.Equals(Name, ESearchCase::IgnoreCase); });
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamFontFaceResolverRangesTest,
	"DreamGUI.Text.FaceResolver.AFaceIsNeverUsedForABaseOutsideItsRanges",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamFontFaceResolverRangesTest::RunTest(const FString& Parameters)
{
	using namespace DreamFontFaceResolverTestLocal;
	FMockFaces Faces(3);
	Faces.Has[0] = { 'A' };
	Faces.Has[1] = { 'A', 'B', 0x4E16 };
	Faces.Has[2] = { 'A', 'B', 0x4E16 };
	FDreamFontFaceTable Table = MakeTable(3);
	Table.Faces[1].Ranges = { FInt32Interval(0x4E00, 0x9FFF) };

	TestEqual(TEXT("the primary face draws what it has"), Faces.Resolve(Table, 3, { 'A' }).FaceIndex, 0);
	const FDreamFontFaceChoice B = Faces.Resolve(Table, 3, { 'B' });
	TestEqual(TEXT("a B goes past the CJK-only face, which has one, to the next"), B.FaceIndex, 2);
	TestTrue(TEXT("which covers it"), B.bCoversCluster && B.bCoversBase);
	TestEqual(TEXT("the CJK-only face was not even asked about the B"), Faces.CoverageAsked[1], 0);
	TestEqual(TEXT("an ideograph goes to the face whose ranges hold it, first in list order"), Faces.Resolve(Table, 3, { 0x4E16 }).FaceIndex, 1);

	// A range keeps a preferred face out too: preferred over the primary only for the code points it holds.
	Table.Faces[1].bPreferOverPrimary = true;
	Faces.Has[0].Add(0x4E16);
	TestEqual(TEXT("the preferred face wins its own ranges over the primary"), Faces.Resolve(Table, 3, { 0x4E16 }).FaceIndex, 1);
	TestEqual(TEXT("and nothing outside them"), Faces.Resolve(Table, 3, { 'A' }).FaceIndex, 0);

	// An empty interval holds nothing, so a face with only that is never used.
	Table.Faces[2].Ranges = { FInt32Interval() };
	Faces.Has[0].Remove('A');
	Faces.Has[1].Remove('A');
	const FDreamFontFaceChoice Nobody = Faces.Resolve(Table, 3, { 'A' });
	TestEqual(TEXT("with no face allowed the A, it is the primary's missing glyph"), Nobody.FaceIndex, 0);
	TestFalse(TEXT("said as such"), Nobody.bCoversBase);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamFontFaceResolverCulturesTest,
	"DreamGUI.Text.FaceResolver.HanTakesTheFaceOfItsLanguageAndCoverageBeatsCulture",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamFontFaceResolverCulturesTest::RunTest(const FString& Parameters)
{
	using namespace DreamFontFaceResolverTestLocal;
	// Roboto, GenEi for Japanese, Droid for Simplified Chinese: the design's example. GenEi lacks U+4EEC.
	FMockFaces Faces(3);
	Faces.Has[0] = { 'a' };
	Faces.Has[1] = { 0x76F4, 0x89D2 };
	Faces.Has[2] = { 0x76F4, 0x89D2, 0x4EEC };
	FDreamFontFaceTable Table = MakeTable(3);
	Table.Faces[1].Cultures = { TEXT("ja") };
	Table.Faces[2].Cultures = { TEXT("zh-Hans") };

	const FDreamTextLanguage Japanese = FDreamTextLanguage::Make(TEXT("ja"));
	const FDreamTextLanguage Simplified = FDreamTextLanguage::Make(TEXT("zh-Hans"));
	const FDreamTextLanguage China = FDreamTextLanguage::Make(TEXT("zh-CN"));
	const FDreamTextLanguage English = FDreamTextLanguage::Make(TEXT("en"));
	TestTrue(TEXT("Japanese names itself"), ContainsCulture(Japanese.PrioritizedCultureNames, TEXT("ja")));
	// zh-CN carries no script, and the engine works it out from the region: it falls back to zh-Hans.
	if (!TestTrue(TEXT("zh-CN falls back to zh-Hans"), ContainsCulture(China.PrioritizedCultureNames, TEXT("zh-Hans"))))
	{
		AddInfo(FString::Printf(TEXT("zh-CN gave %s"), *FString::Join(China.PrioritizedCultureNames, TEXT(", "))));
	}
	TestTrue(TEXT("and to zh"), ContainsCulture(China.PrioritizedCultureNames, TEXT("zh")));

	const TArray<uint32> Straight = { 0x76F4 };
	TestEqual(TEXT("Japanese text takes Han from the Japanese face"), Faces.Resolve(Table, 3, Straight, Japanese.PrioritizedCultureNames).FaceIndex, 1);
	TestEqual(TEXT("Simplified Chinese from the Chinese face"), Faces.Resolve(Table, 3, Straight, Simplified.PrioritizedCultureNames).FaceIndex, 2);
	TestEqual(TEXT("zh-CN too, through the names it falls back to"), Faces.Resolve(Table, 3, Straight, China.PrioritizedCultureNames).FaceIndex, 2);
	TestEqual(TEXT("English text takes the faces of other languages in list order"), Faces.Resolve(Table, 3, Straight, English.PrioritizedCultureNames).FaceIndex, 1);
	TestEqual(TEXT("so does text with no language"), Faces.Resolve(Table, 3, Straight).FaceIndex, 1);

	const FDreamFontFaceChoice Plural = Faces.Resolve(Table, 3, { 0x4EEC }, Japanese.PrioritizedCultureNames);
	TestEqual(TEXT("a character the Japanese face lacks comes from the Chinese one in Japanese text: coverage beats culture"), Plural.FaceIndex, 2);
	TestTrue(TEXT("whole"), Plural.bCoversCluster);

	// Cultures match whatever their case.
	Table.Faces[2].Cultures = { TEXT("ZH-hans") };
	TestEqual(TEXT("a culture matches whatever its case"), Faces.Resolve(Table, 3, Straight, Simplified.PrioritizedCultureNames).FaceIndex, 2);

	// A face for any language stands between those naming the language and those naming another.
	FMockFaces Four(4);
	Four.Has[0] = { 'a' };
	Four.Has[1] = { 0x76F4 };
	Four.Has[2] = { 0x76F4 };
	Four.Has[3] = { 0x76F4 };
	FDreamFontFaceTable FourTable = MakeTable(4);
	FourTable.Faces[1].Cultures = { TEXT("ko") };
	FourTable.Faces[3].Cultures = { TEXT("ja") };
	TestEqual(TEXT("the face naming the language first"), Four.Resolve(FourTable, 4, Straight, Japanese.PrioritizedCultureNames).FaceIndex, 3);
	TestEqual(TEXT("then the one for any language, before one for another language"), Four.Resolve(FourTable, 4, Straight, Simplified.PrioritizedCultureNames).FaceIndex, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamFontFaceResolverPreferredTest,
	"DreamGUI.Text.FaceResolver.APreferredFallbackWinsOverThePrimaryInItsLanguage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamFontFaceResolverPreferredTest::RunTest(const FString& Parameters)
{
	using namespace DreamFontFaceResolverTestLocal;
	// Every face has the kana; faces 1 and 2 are preferred over the primary for the kana block.
	FMockFaces Faces(3);
	Faces.Has[0] = { 'a', 0x3042 };
	Faces.Has[1] = { 0x3042 };
	Faces.Has[2] = { 0x3042 };
	FDreamFontFaceTable Table = MakeTable(3);
	Table.Faces[1].bPreferOverPrimary = true;
	Table.Faces[1].Cultures = { TEXT("ja") };
	Table.Faces[1].Ranges = { FInt32Interval(0x3040, 0x30FF) };
	Table.Faces[2].bPreferOverPrimary = true;
	Table.Faces[2].Ranges = { FInt32Interval(0x3040, 0x30FF) };
	const FDreamTextLanguage Japanese = FDreamTextLanguage::Make(TEXT("ja"));
	const FDreamTextLanguage Korean = FDreamTextLanguage::Make(TEXT("ko"));
	const TArray<uint32> Kana = { 0x3042 };

	TestEqual(TEXT("in its language a preferred face wins over the primary"), Faces.Resolve(Table, 3, Kana, Japanese.PrioritizedCultureNames).FaceIndex, 1);
	TestEqual(TEXT("in another, the preferred face for any language does"), Faces.Resolve(Table, 3, Kana, Korean.PrioritizedCultureNames).FaceIndex, 2);
	Table.Faces[2].bPreferOverPrimary = false;
	TestEqual(TEXT("and with none for that language the primary keeps it"), Faces.Resolve(Table, 3, Kana, Korean.PrioritizedCultureNames).FaceIndex, 0);
	TestEqual(TEXT("outside its ranges a preferred face is not preferred"), Faces.Resolve(Table, 3, { 'a' }, Japanese.PrioritizedCultureNames).FaceIndex, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamFontFaceResolverStyledFaceTest,
	"DreamGUI.Text.FaceResolver.TheStyledFaceIsTriedFirst",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamFontFaceResolverStyledFaceTest::RunTest(const FString& Parameters)
{
	using namespace DreamFontFaceResolverTestLocal;
	// Two regular faces and a bold face after them (index 2), which is not in the table.
	FMockFaces Faces(3);
	Faces.Has[0] = { 'a', 'b' };
	Faces.Has[1] = { 'a', 'b', 'c' };
	Faces.Has[2] = { 'a' };
	FDreamFontFaceTable Table = MakeTable(2);
	Table.Faces[1].bPreferOverPrimary = true;

	TestEqual(TEXT("a bold cluster the bold face has comes from it, before even a preferred face"), Faces.Resolve(Table, 2, { 'a' }, {}, 2).FaceIndex, 2);
	TestEqual(TEXT("one it lacks takes the regular order"), Faces.Resolve(Table, 2, { 'b' }, {}, 2).FaceIndex, 1);
	TestEqual(TEXT("a style face takes scale 1"), Table.GetScale(2), 1.0f);
	Table.Faces[1].bPreferOverPrimary = false;
	TestEqual(TEXT("without the preference the primary"), Faces.Resolve(Table, 2, { 'b' }, {}, 2).FaceIndex, 0);
	TestEqual(TEXT("and a fallback for what only it has"), Faces.Resolve(Table, 2, { 'c' }, {}, 2).FaceIndex, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamFontFaceResolverBasePassTest,
	"DreamGUI.Text.FaceResolver.WithNoFaceForTheWholeClusterTheFirstWithItsBaseDrawsIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamFontFaceResolverBasePassTest::RunTest(const FString& Parameters)
{
	using namespace DreamFontFaceResolverTestLocal;
	FMockFaces Faces(3);
	Faces.Has[0] = { 'a' };
	Faces.Has[1] = { 'b' };
	Faces.Has[2] = { 'a', 0x0301 };
	const FDreamFontFaceTable Table = MakeTable(3);
	const TArray<uint32> Accented = { 'a', 0x0301 };

	const FDreamFontFaceChoice Whole = Faces.Resolve(Table, 3, Accented);
	TestEqual(TEXT("the face with the whole cluster wins over the earlier one with only its base"), Whole.FaceIndex, 2);
	TestTrue(TEXT("and covers it"), Whole.bCoversCluster);

	Faces.Has[2] = { 'a' };
	const FDreamFontFaceChoice BaseOnly = Faces.Resolve(Table, 3, Accented);
	TestEqual(TEXT("with nobody having the accent, the first face with the letter"), BaseOnly.FaceIndex, 0);
	TestFalse(TEXT("not covering the cluster"), BaseOnly.bCoversCluster);
	TestTrue(TEXT("but its base"), BaseOnly.bCoversBase);

	Faces.Has[0].Reset();
	TestEqual(TEXT("the base pass keeps the candidate order"), Faces.Resolve(Table, 3, Accented).FaceIndex, 2);

	const FDreamFontFaceChoice Nobody = Faces.Resolve(Table, 3, { 'z' });
	TestEqual(TEXT("what nobody has is the primary's missing glyph"), Nobody.FaceIndex, 0);
	TestFalse(TEXT("covering nothing"), Nobody.bCoversCluster || Nobody.bCoversBase);
	TestEqual(TEXT("an empty cluster is face 0"), Faces.Resolve(Table, 3, {}).FaceIndex, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamFontFaceResolverPresentationOrderTest,
	"DreamGUI.Text.FaceResolver.EmojiTryColourFacesFirstAndTextLast",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamFontFaceResolverPresentationOrderTest::RunTest(const FString& Parameters)
{
	using namespace DreamFontFaceResolverTestLocal;
	// A monochrome primary that has the heart, and a colour face that has it too.
	FMockFaces Faces(3);
	Faces.Has[0] = { 'a', 0x2764 };
	Faces.Has[1] = { 0x2764, 0x1F600 };
	Faces.Color[1] = true;
	Faces.Has[2] = { 'a' };
	FDreamFontFaceTable Table = MakeTable(3);

	const FDreamFontFaceChoice Emoji = Faces.Resolve(Table, 3, { 0x2764, 0xFE0F });
	TestEqual(TEXT("an emoji heart comes from the colour face"), Emoji.FaceIndex, 1);
	TestTrue(TEXT("which says so"), Emoji.bColor);
	TestEqual(TEXT("a text heart from the monochrome one"), Faces.Resolve(Table, 3, { 0x2764, 0xFE0E }).FaceIndex, 0);
	TestEqual(TEXT("a bare heart is text by default"), Faces.Resolve(Table, 3, { 0x2764 }).FaceIndex, 0);
	const FDreamFontFaceChoice Smile = Faces.Resolve(Table, 3, { 0x1F600 });
	TestEqual(TEXT("what only the colour face has comes from it"), Smile.FaceIndex, 1);
	TestTrue(TEXT("in colour"), Smile.bColor);
	const FDreamFontFaceChoice TextSmile = Faces.Resolve(Table, 3, { 0x1F600, 0xFE0E });
	TestEqual(TEXT("even when the text form is asked for and no other face has it"), TextSmile.FaceIndex, 1);

	Table.bPreferColorEmoji = false;
	const FDreamFontFaceChoice AsListed = Faces.Resolve(Table, 3, { 0x2764, 0xFE0F });
	TestEqual(TEXT("without the font's preference an emoji takes the faces in order"), AsListed.FaceIndex, 0);
	TestFalse(TEXT("so not in colour"), AsListed.bColor);
	Table.bPreferColorEmoji = true;

	// The colour face as the primary: text still goes to a monochrome face first.
	FMockFaces ColorPrimary(2);
	ColorPrimary.Has[0] = { 0x2764 };
	ColorPrimary.Color[0] = true;
	ColorPrimary.Has[1] = { 0x2764 };
	const FDreamFontFaceTable TwoFaces = MakeTable(2);
	TestEqual(TEXT("text goes past a colour primary"), ColorPrimary.Resolve(TwoFaces, 2, { 0x2764 }).FaceIndex, 1);
	TestEqual(TEXT("an emoji stays on it"), ColorPrimary.Resolve(TwoFaces, 2, { 0x2764, 0xFE0F }).FaceIndex, 0);

	// Faces are asked about only as far as the walk goes: a letter the primary has never touches the fallbacks.
	Faces.ResetAsked();
	TestEqual(TEXT("a letter from the primary"), Faces.Resolve(Table, 3, { 'a' }).FaceIndex, 0);
	TestEqual(TEXT("the colour face's coverage is never asked"), Faces.CoverageAsked[1], 0);
	TestEqual(TEXT("nor whether it is a colour face"), Faces.ColorAsked[1], 0);
	TestEqual(TEXT("nor anything of the last face"), Faces.CoverageAsked[2] + Faces.ColorAsked[2], 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamFontFaceResolverIgnorablesTest,
	"DreamGUI.Text.FaceResolver.CoverageIgnoresJoinersSelectorsAndTags",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamFontFaceResolverIgnorablesTest::RunTest(const FString& Parameters)
{
	using namespace DreamFontFaceResolverTestLocal;
	TestTrue(TEXT("ZWJ"), FDreamFontFaceResolver::IsDefaultIgnorable(0x200D));
	TestTrue(TEXT("VS1"), FDreamFontFaceResolver::IsDefaultIgnorable(0xFE00));
	TestTrue(TEXT("VS16"), FDreamFontFaceResolver::IsDefaultIgnorable(0xFE0F));
	TestTrue(TEXT("tag space"), FDreamFontFaceResolver::IsDefaultIgnorable(0xE0020));
	TestTrue(TEXT("cancel tag"), FDreamFontFaceResolver::IsDefaultIgnorable(0xE007F));
	TestTrue(TEXT("VS17"), FDreamFontFaceResolver::IsDefaultIgnorable(0xE0100));
	TestTrue(TEXT("VS256"), FDreamFontFaceResolver::IsDefaultIgnorable(0xE01EF));
	TestFalse(TEXT("ZWNJ is not"), FDreamFontFaceResolver::IsDefaultIgnorable(0x200C));
	TestFalse(TEXT("the keycap mark is not"), FDreamFontFaceResolver::IsDefaultIgnorable(0x20E3));
	TestFalse(TEXT("the language tag is not"), FDreamFontFaceResolver::IsDefaultIgnorable(0xE0001));
	TestFalse(TEXT("past the selectors is not"), FDreamFontFaceResolver::IsDefaultIgnorable(0xE01F0));
	TestFalse(TEXT("a letter is not"), FDreamFontFaceResolver::IsDefaultIgnorable('A'));

	// A colour face like the engine's Noto: no U+FE0F, no ZWJ, no tag glyphs in this mock -- and it still covers the clusters.
	FMockFaces Faces(2);
	Faces.Has[0] = { 'a' };
	Faces.Has[1] = { 0x2764, 0x1F468, 0x1F469, 0x1F3F4 };
	Faces.Color[1] = true;
	const FDreamFontFaceTable Table = MakeTable(2);
	const FDreamFontFaceChoice Heart = Faces.Resolve(Table, 2, { 0x2764, 0xFE0F });
	TestEqual(TEXT("a heart with VS16 from the face without VS16"), Heart.FaceIndex, 1);
	TestTrue(TEXT("covered whole"), Heart.bCoversCluster);
	const FDreamFontFaceChoice Couple = Faces.Resolve(Table, 2, { 0x1F468, 0x200D, 0x1F469 });
	TestTrue(TEXT("a ZWJ sequence from a face without the joiner"), Couple.FaceIndex == 1 && Couple.bCoversCluster);
	const FDreamFontFaceChoice England = Faces.Resolve(Table, 2, { 0x1F3F4, 0xE0067, 0xE0062, 0xE0065, 0xE006E, 0xE0067, 0xE007F });
	TestTrue(TEXT("a tag flag from a face without the tags"), England.FaceIndex == 1 && England.bCoversCluster);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamFontFaceResolverPresentationTest,
	"DreamGUI.Text.FaceResolver.PresentationFollowsTheSelectorsAndTheSequence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamFontFaceResolverPresentationTest::RunTest(const FString& Parameters)
{
	auto Presentation = [](const TArray<uint32>& Cluster) { return FDreamFontFaceResolver::GetPresentation(Cluster); };
	const EDreamTextPresentation AsText = EDreamTextPresentation::Text;
	const EDreamTextPresentation AsEmoji = EDreamTextPresentation::Emoji;
	TestEqual(TEXT("a keycap with VS15 is text"), Presentation({ '1', 0xFE0E, 0x20E3 }), AsText);
	TestEqual(TEXT("a keycap with VS16 is emoji"), Presentation({ '1', 0xFE0F, 0x20E3 }), AsEmoji);
	TestEqual(TEXT("a keycap without a selector is emoji"), Presentation({ '1', 0x20E3 }), AsEmoji);
	TestEqual(TEXT("a bare heart is text"), Presentation({ 0x2764 }), AsText);
	TestEqual(TEXT("a heart with VS16 is emoji"), Presentation({ 0x2764, 0xFE0F }), AsEmoji);
	TestEqual(TEXT("a heart with VS15 is text"), Presentation({ 0x2764, 0xFE0E }), AsText);
	TestEqual(TEXT("a flag pair is emoji"), Presentation({ 0x1F1EF, 0x1F1F5 }), AsEmoji);
	TestEqual(TEXT("a skin tone makes an emoji"), Presentation({ 0x1F44D, 0x1F3FD }), AsEmoji);
	TestEqual(TEXT("even after a letter"), Presentation({ 'a', 0x1F3FD }), AsEmoji);
	TestEqual(TEXT("a ZWJ sequence is emoji"), Presentation({ 0x1F468, 0x200D, 0x1F469 }), AsEmoji);
	TestEqual(TEXT("a tag sequence is emoji"), Presentation({ 0x1F3F4, 0xE0067, 0xE0062, 0xE0065, 0xE006E, 0xE0067, 0xE007F }), AsEmoji);
	TestEqual(TEXT("a smiley is emoji by default"), Presentation({ 0x1F600 }), AsEmoji);
	TestEqual(TEXT("a letter is text"), Presentation({ 'A' }), AsText);
	TestEqual(TEXT("an empty cluster is text"), Presentation({}), AsText);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextLanguageMakeTest,
	"DreamGUI.Text.FaceResolver.ALanguageKnowsTheNamesItFallsBackTo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextLanguageMakeTest::RunTest(const FString& Parameters)
{
	using namespace DreamFontFaceResolverTestLocal;
	const FString Current = FInternationalization::Get().GetCurrentLanguage()->GetName();
	const FDreamTextLanguage Game = FDreamTextLanguage::Make(FString());
	TestEqual(TEXT("no name is the game's current language"), Game.Name, Current);
	if (!Current.IsEmpty())
	{
		TestTrue(TEXT("which names itself among its cultures"), Game.PrioritizedCultureNames.Num() > 0);
	}

	const FDreamTextLanguage Japanese = FDreamTextLanguage::Make(TEXT("ja"));
	TestEqual(TEXT("a name is kept as given"), Japanese.Name, FString(TEXT("ja")));
	TestTrue(TEXT("Japanese"), ContainsCulture(Japanese.PrioritizedCultureNames, TEXT("ja")));
	const FDreamTextLanguage Again = FDreamTextLanguage::Make(TEXT("ja"));
	TestEqual(TEXT("made again, the same names"), Again.PrioritizedCultureNames, Japanese.PrioritizedCultureNames);

	const FDreamTextLanguage Traditional = FDreamTextLanguage::Make(TEXT("zh-Hant-TW"));
	TestTrue(TEXT("zh-Hant-TW falls back to zh-Hant"), ContainsCulture(Traditional.PrioritizedCultureNames, TEXT("zh-Hant")));
	TestFalse(TEXT("and not to zh-Hans"), ContainsCulture(Traditional.PrioritizedCultureNames, TEXT("zh-Hans")));

	// A culture the engine has no data for keeps its own name and language, never the engine's English last resort.
	const FDreamTextLanguage Unknown = FDreamTextLanguage::Make(TEXT("xx-YY"));
	TestTrue(TEXT("an unknown culture keeps its language"), ContainsCulture(Unknown.PrioritizedCultureNames, TEXT("xx")));
	TestFalse(TEXT("and is not English"), ContainsCulture(Unknown.PrioritizedCultureNames, TEXT("en")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamFontFaceResolverNoColorFacesTest,
	"DreamGUI.Text.FaceResolver.WithColourFacesExcludedAClusterResolvesAsIfTheFontHadNone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A caller that cannot draw a colour glyph (FDreamFontFaceQuery::bAllowColorFaces off) never gets a colour face: an emoji
 * goes to a monochrome face that has it, else to the primary's missing glyph, and text resolves as it always does.
 */
bool FDreamFontFaceResolverNoColorFacesTest::RunTest(const FString& Parameters)
{
	using namespace DreamFontFaceResolverTestLocal;
	FMockFaces Faces(3);
	Faces.Has[0] = { 'A' };
	Faces.Has[1] = { 0x1F600 };
	Faces.Color[1] = true;
	Faces.Has[2] = { 0x1F600 };
	FDreamFontFaceTable Table = MakeTable(3);
	Table.bPreferColorEmoji = true;

	const FDreamFontFaceChoice Colour = Faces.Resolve(Table, 3, { 0x1F600 });
	TestEqual(TEXT("An emoji goes to the colour face first"), Colour.FaceIndex, 1);
	TestTrue(TEXT("in colour"), Colour.bColor);
	const FDreamFontFaceChoice Monochrome = Faces.Resolve(Table, 3, { 0x1F600 }, TConstArrayView<FString>(), 0, false);
	TestEqual(TEXT("With colour faces excluded, to the monochrome face that has it"), Monochrome.FaceIndex, 2);
	TestFalse(TEXT("not in colour"), Monochrome.bColor);
	TestTrue(TEXT("which covers it"), Monochrome.bCoversCluster);

	Faces.Has[2].Remove(0x1F600);
	const FDreamFontFaceChoice Missing = Faces.Resolve(Table, 3, { 0x1F600 }, TConstArrayView<FString>(), 0, false);
	TestNotEqual(TEXT("When only a colour face has it, not to that face"), Missing.FaceIndex, 1);
	TestFalse(TEXT("nor in colour"), Missing.bColor);
	TestFalse(TEXT("and said to be missing"), Missing.bCoversBase);

	TestEqual(TEXT("Text resolves as it always does"), Faces.Resolve(Table, 3, { 'A' }, TConstArrayView<FString>(), 0, false).FaceIndex, 0);
	return true;
}

#endif
