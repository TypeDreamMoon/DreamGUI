// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Templates/Function.h"
#include "Core/DreamUIFontData_DistanceField.h"
#include "Core/DreamUIFontData_Bitmap.h"
#include "Core/Text/DreamFontFaceResolver.h"
#include "Engine/Texture2DArray.h"
#include "Engine/World.h"
#include "Serialization/ObjectReader.h"
#include "Serialization/ObjectWriter.h"
#include "UObject/DreamGUIObjectVersion.h"
#include "UObject/EnumProperty.h"
#include "UObject/UnrealType.h"
#include "DreamScopedWorld.h"

/*
 * A font's fallbacks as entries -- which font, for which code points and languages, at what scale -- and what the font
 * tells the rest of text about its faces: the face table the resolver reads, which faces are colour faces and which code
 * points they can draw, who a face is (for caches shared by every font), and when what a layout kept goes stale. Real
 * fonts the engine ships, opened as files the way a project's own font is.
 */
namespace DreamFontFallbackTestLocal
{
	using DreamTests::FScopedGameWorld;

	FString RuntimeFont(const TCHAR* Name)
	{
		return FPaths::Combine(FPaths::EngineContentDir(), TEXT("Slate/Fonts"), Name);
	}

	FString EditorFont(const TCHAR* Name)
	{
		return FPaths::Combine(FPaths::EngineContentDir(), TEXT("Editor/Slate/Fonts"), Name);
	}

	/** Set a protected enum property the way the details panel would, before the font is first used. */
	void SetFontEnumProperty(UObject* Object, const TCHAR* Name, int64 Value)
	{
		if (FEnumProperty* Property = FindFProperty<FEnumProperty>(Object->GetClass(), Name))
		{
			Property->GetUnderlyingProperty()->SetIntPropertyValue(Property->ContainerPtrToValuePtr<void>(Object), Value);
		}
	}

	template<typename TFont>
	TFont* MakeFallbackTestFont(UWorld* World, const FString& Path, TFunctionRef<void(TFont*)> BeforeLoad)
	{
		// Tests read glyphs back right away, so rasterize them on the spot whatever the frame budget says.
		UDreamUIFontData_FreeTypeRender::SetAsyncGlyphSyncBudgetOverride(MAX_int32);
		TFont* Font = NewObject<TFont>(World);
		BeforeLoad(Font);
		Font->SetFontFilePath(Path, false);
		Font->InitFont();
		Font->PrepareForLayout(0.0f);
		return Font;
	}

	UDreamUIFontData_DistanceField* MakeFieldFont(UWorld* World, const FString& Path)
	{
		return MakeFallbackTestFont<UDreamUIFontData_DistanceField>(World, Path, [](UDreamUIFontData_DistanceField*) {});
	}

	FDreamUIFontFallback MakeEntry(UDreamUIFontData_FreeTypeRender* Font)
	{
		FDreamUIFontFallback Entry;
		Entry.Font = Font;
		return Entry;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamFontFallbackEntryMigrationTest,
	"DreamGUI.Text.FontFallback.AnOldFallbackListLoadsAsEntriesInTheSameOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Fallbacks used to be a plain list of fonts (FallbackFontArray). A font saved then is loaded into entries with the
 * defaults -- every code point, any language, scale 1, not preferred over the font's own face -- in the same order, an
 * empty slot included, so face index i is still the same face as before; the old list is emptied. Read back as a package
 * of that age is: older than FDreamGUIObjectVersion::FontFallbackEntries.
 */
bool FDreamFontFallbackEntryMigrationTest::RunTest(const FString& Parameters)
{
	using namespace DreamFontFallbackTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIFontData_DistanceField* Saved = NewObject<UDreamUIFontData_DistanceField>(TestWorld.World);
	UDreamUIFontData_DistanceField* First = NewObject<UDreamUIFontData_DistanceField>(TestWorld.World);
	UDreamUIFontData_DistanceField* Third = NewObject<UDreamUIFontData_DistanceField>(TestWorld.World);
	FArrayProperty* OldList = FindFProperty<FArrayProperty>(UDreamUIFontData_FreeTypeRender::StaticClass(), TEXT("FallbackFontArray"));
	if (!TestNotNull(TEXT("the old list is still a property, so old assets load into it"), OldList))return false;
	FObjectPropertyBase* OldListInner = CastField<FObjectPropertyBase>(OldList->Inner);
	if (!TestNotNull(TEXT("of fonts"), OldListInner))return false;
	{
		// What a font saved before entries held: its fallbacks in the old list, one slot left empty.
		FScriptArrayHelper Helper(OldList, OldList->ContainerPtrToValuePtr<void>(Saved));
		for (UObject* Fallback : { (UObject*)First, (UObject*)nullptr, (UObject*)Third })
		{
			const int32 Index = Helper.AddValue();
			OldListInner->SetObjectPropertyValue(Helper.GetRawPtr(Index), Fallback);
		}
	}
	TArray<uint8> Bytes;
	FObjectWriter Writer(Saved, Bytes);

	UDreamUIFontData_DistanceField* Loaded = NewObject<UDreamUIFontData_DistanceField>(TestWorld.World);
	{
		FObjectReader Reader(Bytes);
		Reader.SetCustomVersion(FDreamGUIObjectVersion::GUID, (int32)FDreamGUIObjectVersion::FontFallbackEntries - 1, TEXT("DreamGUIObjectVersion"));
		static_cast<UObject*>(Loaded)->Serialize(Reader);
	}
	const TArray<FDreamUIFontFallback>& Entries = Loaded->GetFallbacks();
	if (!TestEqual(TEXT("one entry per old slot"), Entries.Num(), 3))return false;
	TestTrue(TEXT("in order: the first"), Entries[0].Font == First);
	TestNull(TEXT("the empty slot stays an empty face"), Entries[1].Font.Get());
	TestTrue(TEXT("the third"), Entries[2].Font == Third);
	for (int32 Index = 0; Index < Entries.Num(); Index++)
	{
		const FDreamUIFontFallback& Entry = Entries[Index];
		TestTrue(FString::Printf(TEXT("entry %d has the defaults: every code point, any language, scale 1, not preferred"), Index),
			Entry.Ranges.Num() == 0 && Entry.Cultures.IsEmpty() && Entry.Scale == 1.0f && !Entry.bPreferOverPrimary);
	}
	TestEqual(TEXT("face i is entry i - 1, as it always was"), Loaded->GetFaceCount(), 4);
	FScriptArrayHelper LoadedOldList(OldList, OldList->ContainerPtrToValuePtr<void>(Loaded));
	TestEqual(TEXT("and the old list is empty"), LoadedOldList.Num(), 0);
	const FDreamFontFaceTable& Table = Loaded->GetFaceTable();
	TestEqual(TEXT("the face table describes every regular face"), Table.Faces.Num(), 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamFontFallbackFaceTableTest,
	"DreamGUI.Text.FontFallback.TheFaceTableIsBuiltFromTheEntries",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The resolver reads a font's faces through its face table: face i + 1 is entry i, with its ranges as given, its cultures
 * split on ';' and trimmed (as Slate writes them, "ja; zh-Hans;"), its scale and its preference; face 0 is the font itself.
 * SetFallbacks drops entries with no font or with the font itself. A new list is a new table; the table is kept, not
 * rebuilt, between changes.
 */
bool FDreamFontFallbackFaceTableTest::RunTest(const FString& Parameters)
{
	using namespace DreamFontFallbackTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIFontData_DistanceField* Roboto = MakeFieldFont(TestWorld.World, RuntimeFont(TEXT("Roboto-Regular.ttf")));
	UDreamUIFontData_DistanceField* GenEi = MakeFieldFont(TestWorld.World, EditorFont(TEXT("GenEiGothicPro-Regular.otf")));
	UDreamUIFontData_DistanceField* Droid = MakeFieldFont(TestWorld.World, RuntimeFont(TEXT("DroidSansFallback.ttf")));

	FDreamUIFontFallback Japanese = MakeEntry(GenEi);
	Japanese.Ranges = { FInt32Interval(0x3040, 0x30FF), FInt32Interval(0x4E00, 0x9FFF) };
	Japanese.Cultures = TEXT(" ja ;; ja-JP;");
	Japanese.Scale = 1.25f;
	FDreamUIFontFallback Chinese = MakeEntry(Droid);
	Chinese.Cultures = TEXT("zh-Hans;zh-Hant");
	Chinese.bPreferOverPrimary = true;
	FDreamUIFontFallback NoFont;
	NoFont.Cultures = TEXT("ko");
	Roboto->SetFallbacks({ Japanese, NoFont, Chinese, MakeEntry(Roboto) });

	const TArray<FDreamUIFontFallback>& Entries = Roboto->GetFallbacks();
	if (!TestEqual(TEXT("the entry with no font and the font's own are dropped"), Entries.Num(), 2))return false;
	TestTrue(TEXT("the rest keep their order"), Entries[0].Font == GenEi && Entries[1].Font == Droid);
	TestEqual(TEXT("the font and its two fallbacks are three faces"), Roboto->GetFaceCount(), 3);

	const FDreamFontFaceTable& Table = Roboto->GetFaceTable();
	if (!TestEqual(TEXT("the table describes each of them"), Table.Faces.Num(), 3))return false;
	TestEqual(TEXT("face 0, the font itself, at scale 1"), Table.Faces[0].Scale, 1.0f);
	const FDreamFontFaceInfo& JapaneseFace = Table.Faces[1];
	if (TestEqual(TEXT("face 1 has its entry's two ranges"), JapaneseFace.Ranges.Num(), 2))
	{
		TestTrue(TEXT("kana"), JapaneseFace.Ranges[0].Min == 0x3040 && JapaneseFace.Ranges[0].Max == 0x30FF);
		TestTrue(TEXT("and the unified ideographs"), JapaneseFace.Ranges[1].Min == 0x4E00 && JapaneseFace.Ranges[1].Max == 0x9FFF);
	}
	if (TestEqual(TEXT("its cultures, split, trimmed, the empty one dropped"), JapaneseFace.Cultures.Num(), 2))
	{
		TestEqual(TEXT("ja"), JapaneseFace.Cultures[0], FString(TEXT("ja")));
		TestEqual(TEXT("ja-JP"), JapaneseFace.Cultures[1], FString(TEXT("ja-JP")));
	}
	TestEqual(TEXT("its scale"), JapaneseFace.Scale, 1.25f);
	TestFalse(TEXT("not preferred over the font's own face"), JapaneseFace.bPreferOverPrimary);
	const FDreamFontFaceInfo& ChineseFace = Table.Faces[2];
	TestEqual(TEXT("face 2 has every code point"), ChineseFace.Ranges.Num(), 0);
	TestTrue(TEXT("two cultures"), ChineseFace.Cultures.Num() == 2 && ChineseFace.Cultures[0] == TEXT("zh-Hans") && ChineseFace.Cultures[1] == TEXT("zh-Hant"));
	TestTrue(TEXT("and is preferred over the font's own face"), ChineseFace.bPreferOverPrimary);
	TestTrue(TEXT("emoji try colour faces first by default"), Table.bPreferColorEmoji);
	TestEqual(TEXT("a face past the table takes scale 1"), Table.GetScale(7), 1.0f);
	TestTrue(TEXT("asked again, the same table"), &Roboto->GetFaceTable() == &Table && Roboto->GetFaceTable().Faces.Num() == 3);

	// A new list, a new table.
	Roboto->SetFallbacks({ Chinese });
	const FDreamFontFaceTable& NewTable = Roboto->GetFaceTable();
	if (TestEqual(TEXT("a new list makes a new table"), NewTable.Faces.Num(), 2))
	{
		TestTrue(TEXT("face 1 is the Chinese entry now"), NewTable.Faces[1].bPreferOverPrimary && NewTable.Faces[1].Cultures.Num() == 2);
	}

#if WITH_EDITOR
	// bPreferColorEmoji is the table's too, after an edit of it.
	if (FBoolProperty* PreferColor = FindFProperty<FBoolProperty>(UDreamUIFontData_FreeTypeRender::StaticClass(), TEXT("bPreferColorEmoji")))
	{
		const uint32 EpochBefore = Roboto->GetLayoutEpoch();
		PreferColor->SetPropertyValue_InContainer(Roboto, false);
		FPropertyChangedEvent Changed(PreferColor);
		static_cast<UObject*>(Roboto)->PostEditChangeProperty(Changed);
		TestFalse(TEXT("an edit of bPreferColorEmoji reaches the table"), Roboto->GetFaceTable().bPreferColorEmoji);
		TestTrue(TEXT("and moves the layout epoch on"), Roboto->GetLayoutEpoch() != EpochBefore);
	}
#endif
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamFontFallbackLayoutEpochTest,
	"DreamGUI.Text.FontFallback.NewFallbacksMoveTheLayoutEpochAndResetTheFaces",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * What a text keeps between layouts (its retained layout, the shape cache) is checked against the font's layout epoch,
 * which must move on with anything that changes the font's layout that a text's input cannot see. A new fallback list
 * resets what the faces behind the indices fed -- face 1 answers as the new face, for code points and metrics alike, and
 * the atlas starts again -- and moves the epoch on; so do new style faces and other vertical metrics. Asking for the
 * metrics already in use moves nothing.
 */
bool FDreamFontFallbackLayoutEpochTest::RunTest(const FString& Parameters)
{
	using namespace DreamFontFallbackTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIFontData_DistanceField* Roboto = MakeFieldFont(TestWorld.World, RuntimeFont(TEXT("Roboto-Regular.ttf")));
	UDreamUIFontData_DistanceField* Droid = MakeFieldFont(TestWorld.World, RuntimeFont(TEXT("DroidSansFallback.ttf")));
	UDreamUIFontData_DistanceField* Arabic = MakeFieldFont(TestWorld.World, RuntimeFont(TEXT("NotoNaskhArabicUI-Regular.ttf")));
	UDreamUIFontData_DistanceField* RobotoBold = MakeFieldFont(TestWorld.World, RuntimeFont(TEXT("Roboto-Bold.ttf")));
	Roboto->SetFallbackFonts({ Droid });
	TestTrue(TEXT("face 1 is the CJK face"), Roboto->FaceHasCodepoint(1, 0x4E16));
	float Ascent = 0.0f, Descent = 0.0f, LineHeight = 0.0f;
	TestTrue(TEXT("and measures"), Roboto->GetFaceMetrics(1, 32.0f, Ascent, Descent, LineHeight));
	Roboto->GetCharData(0x4E16, 64.0f, false);
	UTexture2DArray* AtlasBefore = Roboto->GetFontTexture();

	const uint32 EpochBefore = Roboto->GetLayoutEpoch();
	Roboto->SetFallbacks({ MakeEntry(Arabic) });
	TestTrue(TEXT("a new fallback list moves the layout epoch on"), Roboto->GetLayoutEpoch() != EpochBefore);
	TestFalse(TEXT("face 1 no longer has the CJK face's characters"), Roboto->FaceHasCodepoint(1, 0x4E16));
	TestTrue(TEXT("it has the Arabic face's"), Roboto->FaceHasCodepoint(1, 0x0645));
	float ArabicAscent = 0.0f, ArabicDescent = 0.0f, ArabicLineHeight = 0.0f;
	Arabic->GetFaceMetrics(0, 32.0f, ArabicAscent, ArabicDescent, ArabicLineHeight);
	TestTrue(TEXT("face 1 measures"), Roboto->GetFaceMetrics(1, 32.0f, Ascent, Descent, LineHeight));
	TestEqual(TEXT("as the Arabic face"), LineHeight, ArabicLineHeight, 0.001f);
	TestTrue(TEXT("the atlas started again: its glyphs were keyed by the old faces"), Roboto->GetFontTexture() != AtlasBefore);
	TestTrue(TEXT("face 1 is the Arabic font's own face"), Roboto->GetFaceIdentity(1) == Arabic->GetFaceIdentity(0));

	const uint32 EpochAfterFallbacks = Roboto->GetLayoutEpoch();
	Roboto->SetStyleFonts(RobotoBold, nullptr, nullptr);
	TestTrue(TEXT("new style faces move it on"), Roboto->GetLayoutEpoch() != EpochAfterFallbacks);
	const uint32 EpochAfterStyles = Roboto->GetLayoutEpoch();
	Roboto->SetVerticalMetrics(EDreamUIFontVerticalMetrics::Typo);
	TestTrue(TEXT("other vertical metrics move it on"), Roboto->GetLayoutEpoch() != EpochAfterStyles);
	const uint32 EpochAfterMetrics = Roboto->GetLayoutEpoch();
	Roboto->SetVerticalMetrics(EDreamUIFontVerticalMetrics::Typo);
	TestEqual(TEXT("the metrics already in use move nothing"), Roboto->GetLayoutEpoch(), EpochAfterMetrics);
	Roboto->SetFallbackFonts({ Droid });
	TestTrue(TEXT("SetFallbackFonts is a new list too"), Roboto->GetLayoutEpoch() != EpochAfterMetrics);
	TestTrue(TEXT("its entry has the defaults"), Roboto->GetFallbacks().Num() == 1 && Roboto->GetFallbacks()[0].Scale == 1.0f && Roboto->GetFallbacks()[0].Cultures.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamFontFallbackFaceIdentityTest,
	"DreamGUI.Text.FontFallback.AFacesIdentityMovesOnWhenItsFontReloads",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The shape cache is shared by every font, so it keys a run by who the face is rather than by an index of one font: the
 * font asset whose own face it is, and that asset's face epoch. A fallback's face is the same face whichever font asks.
 * The epoch moves on when the face is torn down and opened again -- a reload, a new file, a culture swap -- so nothing
 * shaped with the old face is taken for the new one. A face that does not open is nobody's, and nothing is cached for it.
 */
bool FDreamFontFallbackFaceIdentityTest::RunTest(const FString& Parameters)
{
	using namespace DreamFontFallbackTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIFontData_DistanceField* Roboto = MakeFieldFont(TestWorld.World, RuntimeFont(TEXT("Roboto-Regular.ttf")));
	UDreamUIFontData_DistanceField* Droid = MakeFieldFont(TestWorld.World, RuntimeFont(TEXT("DroidSansFallback.ttf")));
	Roboto->SetFallbackFonts({ Droid });

	const FDreamUIFontFaceIdentity Own = Roboto->GetFaceIdentity(0);
	if (!TestTrue(TEXT("the font's own face has an identity"), Own.IsValid()))return false;
	TestTrue(TEXT("whose owner is the font"), Own.Owner == FObjectKey(Roboto));
	const FDreamUIFontFaceIdentity Fallback = Roboto->GetFaceIdentity(1);
	TestTrue(TEXT("a fallback's face is owned by the fallback"), Fallback.IsValid() && Fallback.Owner == FObjectKey(Droid));
	TestTrue(TEXT("and is the same face whichever font asks"), Fallback == Droid->GetFaceIdentity(0));
	TestFalse(TEXT("a face the font does not have is nobody's"), Roboto->GetFaceIdentity(9).IsValid());
	TestTrue(TEXT("asked again, the same identity"), Roboto->GetFaceIdentity(0) == Own);

	// A reload: the same file opened again is still another face.
	const uint32 LayoutEpochBefore = Roboto->GetLayoutEpoch();
	Roboto->SetFontFilePath(RuntimeFont(TEXT("Roboto-Regular.ttf")), false);
	const FDreamUIFontFaceIdentity Reloaded = Roboto->GetFaceIdentity(0);
	TestTrue(TEXT("after a reload the face has an identity"), Reloaded.IsValid());
	TestTrue(TEXT("the same owner"), Reloaded.Owner == Own.Owner);
	TestTrue(TEXT("a new epoch"), Reloaded.Epoch != Own.Epoch);
	TestTrue(TEXT("and the font's layout epoch moved on with it"), Roboto->GetLayoutEpoch() != LayoutEpochBefore);
	TestTrue(TEXT("the fallback's identity did not move"), Roboto->GetFaceIdentity(1) == Fallback);

	// The fallback reloads: face 1 of the font is another face too.
	Droid->SetFontFilePath(RuntimeFont(TEXT("DroidSansFallback.ttf")), false);
	const FDreamUIFontFaceIdentity FallbackReloaded = Roboto->GetFaceIdentity(1);
	TestTrue(TEXT("a reloaded fallback is a new face of the same owner"), FallbackReloaded.Owner == Fallback.Owner && FallbackReloaded.Epoch != Fallback.Epoch);

	// A face that does not open.
	AddExpectedErrorPlain(TEXT("not exist"), EAutomationExpectedErrorFlags::Contains, 0);
	UDreamUIFontData_DistanceField* Missing = NewObject<UDreamUIFontData_DistanceField>(TestWorld.World);
	Missing->SetFontFilePath(FPaths::Combine(FPaths::ProjectIntermediateDir(), TEXT("DreamGUITests_NoSuchFallbackFont.ttf")), false);
	TestFalse(TEXT("a face that does not open is nobody's"), Missing->GetFaceIdentity(0).IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamFontFallbackColorCoverageTest,
	"DreamGUI.Text.FontFallback.AColourFaceHasACodePointOnlyWhereItCanBeDrawn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Which face draws a character is decided by what each face has, and a colour face only has what can be drawn from it:
 * Noto Color Emoji's grin is a CBDT bitmap, which the outline field's BGRA atlas holds and the bitmap font's atlas holds,
 * so through either font the face has it and is a colour face. The single-channel field's atlas is R8 and holds no colour:
 * through such a font the face is no colour face, and a glyph with nothing but a bitmap is not there -- the next face, or
 * the text's emoji data, answers for it.
 */
bool FDreamFontFallbackColorCoverageTest::RunTest(const FString& Parameters)
{
	using namespace DreamFontFallbackTestLocal;
	const FString EmojiPath = EditorFont(TEXT("NotoColorEmoji.ttf"));
	if (!FPaths::FileExists(EmojiPath))
	{
		AddInfo(TEXT("The engine's Noto Color Emoji is not installed; nothing to check."));
		return true;
	}
	FScopedGameWorld TestWorld;
	UDreamUIFontData_DistanceField* Emoji = MakeFieldFont(TestWorld.World, EmojiPath);
	const uint32 Grin = 0x1F600;

	UDreamUIFontData_DistanceField* OutlineField = MakeFieldFont(TestWorld.World, RuntimeFont(TEXT("Roboto-Regular.ttf")));
	OutlineField->SetFallbackFonts({ Emoji });
	TestTrue(TEXT("through the outline field, the emoji face is a colour face"), OutlineField->IsColorFace(1));
	TestTrue(TEXT("and has the grin"), OutlineField->FaceHasCodepoint(1, Grin));
	TestFalse(TEXT("but not a letter it has no glyph for"), OutlineField->FaceHasCodepoint(1, 'A'));
	TestFalse(TEXT("the font's own face is no colour face"), OutlineField->IsColorFace(0));

	UDreamUIFontData_Bitmap* Bitmap = MakeFallbackTestFont<UDreamUIFontData_Bitmap>(TestWorld.World, RuntimeFont(TEXT("Roboto-Regular.ttf")), [](UDreamUIFontData_Bitmap*) {});
	Bitmap->SetFallbackFonts({ Emoji });
	TestTrue(TEXT("through a bitmap font, it is a colour face too"), Bitmap->IsColorFace(1));
	TestTrue(TEXT("and has the grin"), Bitmap->FaceHasCodepoint(1, Grin));

	UDreamUIFontData_DistanceField* SingleChannel = MakeFallbackTestFont<UDreamUIFontData_DistanceField>(TestWorld.World, RuntimeFont(TEXT("Roboto-Regular.ttf")),
		[](UDreamUIFontData_DistanceField* Font) { SetFontEnumProperty(Font, TEXT("SdfSource"), (int64)EDreamUISdfSource::BitmapSingleChannel); });
	SingleChannel->SetFallbackFonts({ Emoji });
	TestFalse(TEXT("through the single-channel field, it is no colour face"), SingleChannel->IsColorFace(1));
	TestFalse(TEXT("and a glyph with no outline is not there"), SingleChannel->FaceHasCodepoint(1, Grin));
	TestTrue(TEXT("while the font's own face still has its letters"), SingleChannel->FaceHasCodepoint(0, 'A'));
	return true;
}

#endif
