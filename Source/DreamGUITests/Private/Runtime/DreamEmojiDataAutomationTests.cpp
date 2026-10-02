// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Core/DreamUIFontEmojiData.h"
#include "Engine/World.h"
#include "Serialization/ObjectReader.h"
#include "Serialization/ObjectWriter.h"
#include "UObject/DreamGUIObjectVersion.h"
#include "DreamScopedWorld.h"

/*
 * Emoji data entries -- an author's own picture for an emoji -- are found by the whole emoji sequence, not only its first
 * code point: a thumbs up with a skin tone is not the plain thumbs up. U+FE0E and U+FE0F choose a presentation, not a
 * picture, and are left out of every key. A key saved before sequences stands for its first code point alone, and is
 * the same key as the one it is migrated to, so loading neither moves nor merges an entry.
 */
namespace DreamEmojiDataTestLocal
{
	using DreamTests::FScopedGameWorld;

	/** A string of these code points, those past the BMP as surrogate pairs: how a text spells them. */
	FString SpellCodepoints(const TArray<uint32>& Codepoints)
	{
		FString Result;
		for (const uint32 Codepoint : Codepoints)
		{
			if (Codepoint >= 0x10000)
			{
				const uint32 Offset = Codepoint - 0x10000;
				Result.AppendChar((TCHAR)(0xD800 + (Offset >> 10)));
				Result.AppendChar((TCHAR)(0xDC00 + (Offset & 0x3FF)));
			}
			else
			{
				Result.AppendChar((TCHAR)Codepoint);
			}
		}
		return Result;
	}

	bool SequenceIs(const FDreamUIFontEmojiKey& Key, const TArray<int32>& Expected)
	{
		return Key.Sequence == Expected;
	}

	/** An entry told apart from the others by its frame rate. */
	FDreamUIFontEmojiDataItem MakeItem(float Fps)
	{
		FDreamUIFontEmojiDataItem Item;
		Item.OverrideAnimationFps = Fps;
		return Item;
	}

	float FpsOf(const FDreamUIFontEmojiDataItem* Item)
	{
		return Item != nullptr ? Item->OverrideAnimationFps : -100.0f;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamEmojiDataSequenceKeyTest,
	"DreamGUI.Text.EmojiData.AKeyIsTheWholeSequenceWithoutVariationSelectors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The key a cluster is looked up by: every code point of it in order -- ZWJs, skin tones, the second flag letter, the
 * keycap mark kept -- except U+FE0E and U+FE0F, its first code point as EmojiCode. A heart in emoji presentation and in
 * text presentation is one key; a thumbs up with a skin tone and without are two. Text with nothing but a selector, or
 * nothing at all, is no key.
 */
bool FDreamEmojiDataSequenceKeyTest::RunTest(const FString& Parameters)
{
	using namespace DreamEmojiDataTestLocal;
	const FDreamUIFontEmojiKey Heart = UDreamUIFontEmojiData::MakeKey(SpellCodepoints({ 0x2764, 0xFE0F }));
	TestTrue(TEXT("a heart with U+FE0F is the heart alone"), SequenceIs(Heart, { 0x2764 }));
	TestEqual(TEXT("its first code point"), (int32)Heart.EmojiCode, 0x2764);
	TestEqual(TEXT("the selector is carried, not keyed"), (int32)Heart.VariantSelector, 0xFE0F);
	const FDreamUIFontEmojiKey TextHeart = UDreamUIFontEmojiData::MakeKey(SpellCodepoints({ 0x2764, 0xFE0E }));
	TestTrue(TEXT("in text presentation it is the same key"), TextHeart == Heart);
	TestTrue(TEXT("with the same hash"), GetTypeHash(TextHeart) == GetTypeHash(Heart));
	TestTrue(TEXT("and so is the bare heart"), UDreamUIFontEmojiData::MakeKey(SpellCodepoints({ 0x2764 })) == Heart);

	const FDreamUIFontEmojiKey ThumbsUpTone = UDreamUIFontEmojiData::MakeKey(SpellCodepoints({ 0x1F44D, 0x1F3FD }));
	TestTrue(TEXT("a skin tone is part of the key"), SequenceIs(ThumbsUpTone, { 0x1F44D, 0x1F3FD }));
	TestEqual(TEXT("the thumbs up is its first code point"), (int32)ThumbsUpTone.EmojiCode, 0x1F44D);
	TestFalse(TEXT("so it is not the plain thumbs up"), ThumbsUpTone == UDreamUIFontEmojiData::MakeKey(SpellCodepoints({ 0x1F44D })));
	TestTrue(TEXT("a ZWJ family keeps its joiners"), SequenceIs(UDreamUIFontEmojiData::MakeKey(SpellCodepoints({ 0x1F468, 0x200D, 0x1F469, 0x200D, 0x1F467 })),
		{ 0x1F468, 0x200D, 0x1F469, 0x200D, 0x1F467 }));
	TestTrue(TEXT("a flag is both its letters"), SequenceIs(UDreamUIFontEmojiData::MakeKey(SpellCodepoints({ 0x1F1EF, 0x1F1F5 })), { 0x1F1EF, 0x1F1F5 }));
	TestTrue(TEXT("a keycap keeps its mark and drops its selector"), SequenceIs(UDreamUIFontEmojiData::MakeKey(SpellCodepoints({ '1', 0xFE0F, 0x20E3 })), { '1', 0x20E3 }));
	TestTrue(TEXT("a lone selector is no key"), UDreamUIFontEmojiData::MakeKey(SpellCodepoints({ 0xFE0F })).Sequence.Num() == 0);
	TestTrue(TEXT("nor is nothing"), UDreamUIFontEmojiData::MakeKey(FString()).Sequence.Num() == 0 && UDreamUIFontEmojiData::MakeKey(FString()).EmojiCode == 0u);

#if WITH_EDITOR
	// What the details panel does with a pasted emoji: one cluster, segmented as a text segments it, keyed whole.
	FDreamUIFontEmojiKey Pasted;
	Pasted.EmojiChar = SpellCodepoints({ 0x1F44D, 0x1F3FD, 'x' });
	Pasted.ApplyEmoji();
	TestEqual(TEXT("a pasted emoji keeps its first cluster"), Pasted.EmojiChar, SpellCodepoints({ 0x1F44D, 0x1F3FD }));
	TestTrue(TEXT("and is keyed by the whole of it"), SequenceIs(Pasted, { 0x1F44D, 0x1F3FD }) && Pasted.EmojiCode == 0x1F44Du);
	FDreamUIFontEmojiKey PastedHeart;
	PastedHeart.EmojiChar = SpellCodepoints({ 0x2764, 0xFE0F });
	PastedHeart.ApplyEmoji();
	TestTrue(TEXT("a heart asking for emoji presentation is the heart"), PastedHeart == Heart && PastedHeart.VariantSelector == 0xFE0F);
	FDreamUIFontEmojiKey PastedLetter;
	PastedLetter.EmojiChar = TEXT("A");
	PastedLetter.ApplyEmoji();
	TestTrue(TEXT("a letter is no emoji"), PastedLetter.EmojiCode == 0u && PastedLetter.Sequence.Num() == 0 && PastedLetter.EmojiChar.IsEmpty());
#endif
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamEmojiDataOldKeyTest,
	"DreamGUI.Text.EmojiData.AnOldKeyIsTheSameKeyAsTheOneItIsMigratedTo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A key from before sequences has only EmojiCode, and stands for that one code point: equal, with an equal hash, to the
 * key with Sequence {EmojiCode} that loading turns it into, and to the key the text of that code point makes. So a map
 * keyed the old way finds its entries by the new keys, and filling Sequence in moves no entry.
 */
bool FDreamEmojiDataOldKeyTest::RunTest(const FString& Parameters)
{
	using namespace DreamEmojiDataTestLocal;
	const FDreamUIFontEmojiKey Old(0x1F600);
	FDreamUIFontEmojiKey Migrated = Old;
	Migrated.Sequence = { 0x1F600 };
	TestTrue(TEXT("an old key equals its migrated key"), Old == Migrated && Migrated == Old);
	TestTrue(TEXT("with the same hash"), GetTypeHash(Old) == GetTypeHash(Migrated));
	TestTrue(TEXT("and the key its text makes"), Old == UDreamUIFontEmojiData::MakeKey(SpellCodepoints({ 0x1F600 })));
	TestEqual(TEXT("it stands for one code point"), Old.GetSequenceLength(), 1);
	TestEqual(TEXT("its own"), Old.GetSequenceAt(0), 0x1F600);
	TestFalse(TEXT("it is not another code point's"), Old == FDreamUIFontEmojiKey(0x1F601));
	TestFalse(TEXT("nor a longer sequence starting with it"), Old == UDreamUIFontEmojiData::MakeKey(SpellCodepoints({ 0x1F600, 0x1F3FD })));

	TMap<FDreamUIFontEmojiKey, int32> Map;
	Map.Add(Old, 7);
	const int32* Found = Map.Find(Migrated);
	TestTrue(TEXT("a map keyed the old way finds the entry by the migrated key"), Found != nullptr && *Found == 7);
	Map.Add(Migrated, 8);
	TestEqual(TEXT("and adding the migrated key replaces it rather than adding a second"), Map.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamEmojiDataMigrationTest,
	"DreamGUI.Text.EmojiData.AnOldEmojiAssetLoadsWithSequenceKeys",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Emoji data saved before sequences (older than FDreamGUIObjectVersion::EmojiKeyBySequence) has keys with only EmojiCode.
 * Loading fills each one's Sequence with {EmojiCode}, where it sits in the map: every entry is still there, still found by
 * its code point and by the text of its single code point. A key that already has a sequence is left as it is.
 */
bool FDreamEmojiDataMigrationTest::RunTest(const FString& Parameters)
{
	using namespace DreamEmojiDataTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIFontEmojiData* Saved = NewObject<UDreamUIFontEmojiData>(TestWorld.World);
	TMap<FDreamUIFontEmojiKey, FDreamUIFontEmojiDataItem>& SavedMap = Saved->GetMutableDataMap();
	SavedMap.Add(FDreamUIFontEmojiKey(0x1F600), MakeItem(12.0f));
	SavedMap.Add(FDreamUIFontEmojiKey(0x1F44D), MakeItem(13.0f));
	SavedMap.Add(UDreamUIFontEmojiData::MakeKey(SpellCodepoints({ 0x1F44D, 0x1F3FD })), MakeItem(24.0f));
	TArray<uint8> Bytes;
	FObjectWriter Writer(Saved, Bytes);

	UDreamUIFontEmojiData* Loaded = NewObject<UDreamUIFontEmojiData>(TestWorld.World);
	{
		FObjectReader Reader(Bytes);
		Reader.SetCustomVersion(FDreamGUIObjectVersion::GUID, (int32)FDreamGUIObjectVersion::EmojiKeyBySequence - 1, TEXT("DreamGUIObjectVersion"));
		static_cast<UObject*>(Loaded)->Serialize(Reader);
	}
	const TMap<FDreamUIFontEmojiKey, FDreamUIFontEmojiDataItem>& LoadedMap = Loaded->GetDataMap();
	TestEqual(TEXT("every entry is still there"), LoadedMap.Num(), 3);
	for (const TPair<FDreamUIFontEmojiKey, FDreamUIFontEmojiDataItem>& Pair : LoadedMap)
	{
		TestTrue(FString::Printf(TEXT("the key of U+%X has its sequence filled in"), Pair.Key.EmojiCode), Pair.Key.Sequence.Num() > 0 && Pair.Key.Sequence[0] == (int32)Pair.Key.EmojiCode);
	}
	TestEqual(TEXT("an old key's entry is found by its code point"), FpsOf(Loaded->FindByCodepoint(0x1F600)), 12.0f);
	TestEqual(TEXT("and by its text"), FpsOf(Loaded->FindBySequence(SpellCodepoints({ 0x1F600 }))), 12.0f);
	TestEqual(TEXT("the plain thumbs up stays the plain one"), FpsOf(Loaded->FindByCodepoint(0x1F44D)), 13.0f);
	TestEqual(TEXT("and the toned one keeps its whole sequence"), FpsOf(Loaded->FindBySequence(SpellCodepoints({ 0x1F44D, 0x1F3FD }))), 24.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamEmojiDataLookupTest,
	"DreamGUI.Text.EmojiData.LookupsFindTheExactSequenceOrTheCodePoint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The two lookups a text's emoji goes through, in its precedence order. FindBySequence is exact (selectors aside): the
 * toned thumbs up finds its own entry, another tone finds nothing, and a single code point finds the entry keyed by it,
 * which every old entry is. FindByCodepoint is the entry of the cluster's first code point, the fallback below a colour
 * font. Nothing is found for no text, or for code point 0.
 */
bool FDreamEmojiDataLookupTest::RunTest(const FString& Parameters)
{
	using namespace DreamEmojiDataTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIFontEmojiData* Data = NewObject<UDreamUIFontEmojiData>(TestWorld.World);
	TMap<FDreamUIFontEmojiKey, FDreamUIFontEmojiDataItem>& Map = Data->GetMutableDataMap();
	Map.Add(FDreamUIFontEmojiKey(0x1F44D), MakeItem(1.0f));
	Map.Add(UDreamUIFontEmojiData::MakeKey(SpellCodepoints({ 0x1F44D, 0x1F3FD })), MakeItem(2.0f));
	Map.Add(UDreamUIFontEmojiData::MakeKey(SpellCodepoints({ 0x2764, 0xFE0F })), MakeItem(3.0f));

	TestEqual(TEXT("the toned thumbs up finds its own entry"), FpsOf(Data->FindBySequence(SpellCodepoints({ 0x1F44D, 0x1F3FD }))), 2.0f);
	TestNull(TEXT("another tone finds nothing: the lookup is exact"), Data->FindBySequence(SpellCodepoints({ 0x1F44D, 0x1F3FF })));
	TestEqual(TEXT("the plain thumbs up finds the entry keyed by its code point"), FpsOf(Data->FindBySequence(SpellCodepoints({ 0x1F44D }))), 1.0f);
	TestEqual(TEXT("which is also what its first code point finds"), FpsOf(Data->FindByCodepoint(0x1F44D)), 1.0f);
	TestEqual(TEXT("a heart without U+FE0F finds the heart"), FpsOf(Data->FindBySequence(SpellCodepoints({ 0x2764 }))), 3.0f);
	TestEqual(TEXT("so does one in text presentation"), FpsOf(Data->FindBySequence(SpellCodepoints({ 0x2764, 0xFE0E }))), 3.0f);
	TestEqual(TEXT("and its code point"), FpsOf(Data->FindByCodepoint(0x2764)), 3.0f);
	TestNull(TEXT("no text finds nothing"), Data->FindBySequence(FString()));
	TestNull(TEXT("a lone selector finds nothing"), Data->FindBySequence(SpellCodepoints({ 0xFE0F })));
	TestNull(TEXT("code point 0 finds nothing"), Data->FindByCodepoint(0));
	TestNull(TEXT("an emoji with no entry finds nothing"), Data->FindByCodepoint(0x1F600));

	FIntVector2 Size;
	const FDreamUIFontEmojiDataItem* NoFrames = Data->FindByCodepoint(0x1F44D);
	TestFalse(TEXT("an entry with no frame has no image size"), NoFrames != nullptr && UDreamUIFontEmojiData::GetItemImageSize(*NoFrames, Size));
	TestFalse(TEXT("nor does its code point"), Data->GetImageSize(0x1F44D, Size));
	return true;
}

#endif
