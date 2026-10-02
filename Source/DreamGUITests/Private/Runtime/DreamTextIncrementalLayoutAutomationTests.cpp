// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Math/RandomStream.h"
#include "HAL/IConsoleManager.h"
#include "Core/DreamUIFontData_DistanceField.h"
#include "Core/DreamUITextData.h"
#include "Core/Text/DreamTextLayout.h"
#include "Core/Text/DreamTextShapeCache.h"
#include "Engine/World.h"
#include "DreamTextTestFont.h"
#include "DreamScopedWorld.h"

#include <initializer_list>

/*
 * Retained incremental layout (FDreamTextLayoutState). A layout that builds on what the last one kept has to come out as a
 * layout from nothing does, field for field, whatever the edit was -- that is held over seeded runs of random edits on
 * multi-paragraph rich text in Latin, Arabic, CJK, Thai, emoji sequences, tags, tabs, justification and wrapping, on the
 * mock font and on the engine's own fonts. Then that it does only the work the edit asks for, read off the layout's and the
 * shaper's counters; and that whatever a kept layout cannot see -- the font's fallbacks, its atlas, the language, the
 * console switch -- makes the next layout start from nothing.
 */
namespace DreamTextIncrementalLayoutTestLocal
{
	using DreamTests::FScopedGameWorld;

	FString EngineFont(const TCHAR* Name)
	{
		return FPaths::Combine(FPaths::EngineContentDir(), TEXT("Slate/Fonts"), Name);
	}

	UDreamUIFontData_DistanceField* MakeFileFont(UWorld* World, const TCHAR* Name)
	{
		// The tests read quads back right away, so glyphs are rasterized on the spot whatever the frame budget says.
		UDreamUIFontData_FreeTypeRender::SetAsyncGlyphSyncBudgetOverride(MAX_int32);
		UDreamUIFontData_DistanceField* Font = NewObject<UDreamUIFontData_DistanceField>(World);
		Font->SetFontFilePath(EngineFont(Name), false);
		Font->InitFont();
		return Font;
	}

	/** Roboto, with Noto Naskh Arabic, Droid Sans Fallback (CJK) and Noto Sans Thai behind it. */
	UDreamUIFontData_DistanceField* MakeMixedScriptFont(UWorld* World)
	{
		UDreamUIFontData_DistanceField* Roboto = MakeFileFont(World, TEXT("Roboto-Regular.ttf"));
		Roboto->SetFallbackFonts({ MakeFileFont(World, TEXT("NotoNaskhArabicUI-Regular.ttf")),
			MakeFileFont(World, TEXT("DroidSansFallback.ttf")), MakeFileFont(World, TEXT("NotoSansThai-Regular.ttf")) });
		return Roboto;
	}

	FDreamTextLayoutInput MakeInput(UDreamUIFontData_BaseObject* Font, const FString& Content, float Width = 420.0f, float Height = 2000.0f)
	{
		FDreamTextLayoutInput In;
		In.Content = Content;
		In.Width = Width;
		In.Height = Height;
		In.Pivot = FVector2f(0.5f, 0.5f);
		In.FontSize = 24.0f;
		In.ParagraphHAlign = EDreamUITextParagraphHorizontalAlign::Left;
		In.ParagraphVAlign = EDreamUITextParagraphVerticalAlign::Top;
		In.OverflowType = EDreamUITextOverflowType::VerticalOverflow;
		In.bUseKerning = true;
		In.Font = Font;
		return In;
	}

	/** A console variable set for the test's length, and put back after. */
	struct FScopedConsoleInt
	{
		IConsoleVariable* Variable = nullptr;
		int32 Before = 0;

		FScopedConsoleInt(const TCHAR* Name, int32 Value)
		{
			Variable = IConsoleManager::Get().FindConsoleVariable(Name);
			if (Variable != nullptr)
			{
				Before = Variable->GetInt();
				Variable->Set(Value, ECVF_SetByCode);
			}
		}
		~FScopedConsoleInt()
		{
			if (Variable != nullptr)
			{
				Variable->Set(Before, ECVF_SetByCode);
			}
		}
		void Set(int32 Value)
		{
			if (Variable != nullptr)
			{
				Variable->Set(Value, ECVF_SetByCode);
			}
		}
	};

	FString DescribeFloat(float A, float B)
	{
		return FString::Printf(TEXT("%.9g vs %.9g"), A, B);
	}

	/** The first field two glyph entries differ in, or empty. */
	FString CharDataDifference(const FDreamUICharData& A, const FDreamUICharData& B)
	{
		if (A.Width != B.Width) return TEXT("Width ") + DescribeFloat(A.Width, B.Width);
		if (A.Height != B.Height) return TEXT("Height ") + DescribeFloat(A.Height, B.Height);
		if (A.XOffset != B.XOffset) return TEXT("XOffset ") + DescribeFloat(A.XOffset, B.XOffset);
		if (A.YOffset != B.YOffset) return TEXT("YOffset ") + DescribeFloat(A.YOffset, B.YOffset);
		if (A.XAdvance != B.XAdvance) return TEXT("XAdvance ") + DescribeFloat(A.XAdvance, B.XAdvance);
		if (A.MinUV != B.MinUV) return TEXT("MinUV");
		if (A.MaxUV != B.MaxUV) return TEXT("MaxUV");
		if (A.SliceIndex != B.SliceIndex) return TEXT("SliceIndex");
		if (A.FaceIndex != B.FaceIndex) return FString::Printf(TEXT("FaceIndex %d vs %d"), A.FaceIndex, B.FaceIndex);
		if (A.GlyphIndex != B.GlyphIndex) return FString::Printf(TEXT("GlyphIndex %u vs %u"), A.GlyphIndex, B.GlyphIndex);
		if (A.bPending != B.bPending) return TEXT("bPending");
		if (A.bColor != B.bColor) return TEXT("bColor");
		if (A.ColorTexelsPerEm != B.ColorTexelsPerEm) return TEXT("ColorTexelsPerEm");
		return FString();
	}

	FString StyleDifference(const FDreamTextItemStyle& A, const FDreamTextItemStyle& B)
	{
		if (A.Size != B.Size) return TEXT("Style.Size ") + DescribeFloat(A.Size, B.Size);
		if (A.Color != B.Color) return TEXT("Style.Color");
		if (A.bHasColor != B.bHasColor) return TEXT("Style.bHasColor");
		if (A.bHasMultiplyColor != B.bHasMultiplyColor) return TEXT("Style.bHasMultiplyColor");
		if (A.MultiplyColor != B.MultiplyColor) return TEXT("Style.MultiplyColor");
		if (A.bBold != B.bBold) return TEXT("Style.bBold");
		if (A.bItalic != B.bItalic) return TEXT("Style.bItalic");
		if (A.bSyntheticBold != B.bSyntheticBold) return TEXT("Style.bSyntheticBold");
		if (A.bSyntheticItalic != B.bSyntheticItalic) return TEXT("Style.bSyntheticItalic");
		if (A.bUnderline != B.bUnderline) return TEXT("Style.bUnderline");
		if (A.bStrikethrough != B.bStrikethrough) return TEXT("Style.bStrikethrough");
		if (A.SupOrSub != B.SupOrSub) return TEXT("Style.SupOrSub");
		return FString();
	}

	/** The first field in which two display lists differ, or an empty string when they are the same, field for field. */
	FString FirstDifference(const FDreamTextDisplayList& A, const FDreamTextDisplayList& B)
	{
		if (A.Items.Num() != B.Items.Num())
		{
			return FString::Printf(TEXT("%d items vs %d"), A.Items.Num(), B.Items.Num());
		}
		for (int32 i = 0; i < A.Items.Num(); i++)
		{
			const FDreamTextGlyphItem& X = A.Items[i];
			const FDreamTextGlyphItem& Y = B.Items[i];
			FString Field;
			if (X.Kind != Y.Kind) Field = TEXT("Kind");
			else if (X.Codepoint != Y.Codepoint) Field = FString::Printf(TEXT("Codepoint U+%04X vs U+%04X"), X.Codepoint, Y.Codepoint);
			else if (X.ElementIndex != Y.ElementIndex) Field = FString::Printf(TEXT("ElementIndex %d vs %d"), X.ElementIndex, Y.ElementIndex);
			else if (X.SourceIndex != Y.SourceIndex) Field = FString::Printf(TEXT("SourceIndex %d vs %d"), X.SourceIndex, Y.SourceIndex);
			else if (X.LineIndex != Y.LineIndex) Field = FString::Printf(TEXT("LineIndex %d vs %d"), X.LineIndex, Y.LineIndex);
			else if (X.Pen.X != Y.Pen.X) Field = TEXT("Pen.X ") + DescribeFloat(X.Pen.X, Y.Pen.X);
			else if (X.Pen.Y != Y.Pen.Y) Field = TEXT("Pen.Y ") + DescribeFloat(X.Pen.Y, Y.Pen.Y);
			else if (X.GlyphSize != Y.GlyphSize) Field = TEXT("GlyphSize ") + DescribeFloat(X.GlyphSize, Y.GlyphSize);
			else if (X.AdvanceWithSpace != Y.AdvanceWithSpace) Field = TEXT("AdvanceWithSpace ") + DescribeFloat(X.AdvanceWithSpace, Y.AdvanceWithSpace);
			else if (X.DecorationOffset != Y.DecorationOffset) Field = TEXT("DecorationOffset ") + DescribeFloat(X.DecorationOffset, Y.DecorationOffset);
			else if (X.bEmit != Y.bEmit) Field = TEXT("bEmit");
			else if (X.bCountsAsVisible != Y.bCountsAsVisible) Field = TEXT("bCountsAsVisible");
			else if (FString Glyph = CharDataDifference(X.Glyph, Y.Glyph); !Glyph.IsEmpty()) Field = TEXT("Glyph.") + Glyph;
			else if (FString Style = StyleDifference(X.Style, Y.Style); !Style.IsEmpty()) Field = Style;
			else if (FString Under = CharDataDifference(X.UnderlineGlyph, Y.UnderlineGlyph); !Under.IsEmpty()) Field = TEXT("UnderlineGlyph.") + Under;
			else if (FString Strike = CharDataDifference(X.StrikethroughGlyph, Y.StrikethroughGlyph); !Strike.IsEmpty()) Field = TEXT("StrikethroughGlyph.") + Strike;
			if (!Field.IsEmpty())
			{
				return FString::Printf(TEXT("item %d (U+%04X, element %d, line %d): %s"), i, Y.Codepoint, Y.ElementIndex, Y.LineIndex, *Field);
			}
		}
		if (A.Lines.Num() != B.Lines.Num())
		{
			return FString::Printf(TEXT("%d lines vs %d"), A.Lines.Num(), B.Lines.Num());
		}
		for (int32 Line = 0; Line < A.Lines.Num(); Line++)
		{
			const TArray<FDreamUITextCaretProperty>& X = A.Lines[Line].CaretPropertyList;
			const TArray<FDreamUITextCaretProperty>& Y = B.Lines[Line].CaretPropertyList;
			if (X.Num() != Y.Num())
			{
				return FString::Printf(TEXT("line %d: %d carets vs %d"), Line, X.Num(), Y.Num());
			}
			for (int32 k = 0; k < X.Num(); k++)
			{
				if (X[k].CaretPosition != Y[k].CaretPosition || X[k].CharIndex != Y[k].CharIndex)
				{
					return FString::Printf(TEXT("line %d caret %d: (%.9g, %.9g) %d vs (%.9g, %.9g) %d"), Line, k, X[k].CaretPosition.X,
						X[k].CaretPosition.Y, X[k].CharIndex, Y[k].CaretPosition.X, Y[k].CaretPosition.Y, Y[k].CharIndex);
				}
			}
		}
		if (A.CustomTags.Num() != B.CustomTags.Num() || A.CustomTagElementRanges != B.CustomTagElementRanges)
		{
			return TEXT("custom tags (count or element ranges)");
		}
		for (int32 k = 0; k < A.CustomTags.Num(); k++)
		{
			const FDreamUIText_RichTextCustomTag& X = A.CustomTags[k];
			const FDreamUIText_RichTextCustomTag& Y = B.CustomTags[k];
			if (X.TagName != Y.TagName || X.CharIndexStart != Y.CharIndexStart || X.CharIndexEnd != Y.CharIndexEnd || X.bHyperlink != Y.bHyperlink)
			{
				return FString::Printf(TEXT("custom tag %d"), k);
			}
		}
		if (A.Images.Num() != B.Images.Num())
		{
			return FString::Printf(TEXT("%d images vs %d"), A.Images.Num(), B.Images.Num());
		}
		for (int32 k = 0; k < A.Images.Num(); k++)
		{
			const FDreamUIText_RichTextImageTag& X = A.Images[k];
			const FDreamUIText_RichTextImageTag& Y = B.Images[k];
			if (X.TagName != Y.TagName || X.Position != Y.Position || X.Size != Y.Size || X.TintColor != Y.TintColor)
			{
				return FString::Printf(TEXT("image %d"), k);
			}
		}
		if (A.Emojis.Num() != B.Emojis.Num())
		{
			return FString::Printf(TEXT("%d emojis vs %d"), A.Emojis.Num(), B.Emojis.Num());
		}
		for (int32 k = 0; k < A.Emojis.Num(); k++)
		{
			const FDreamUIText_Emoji& X = A.Emojis[k];
			const FDreamUIText_Emoji& Y = B.Emojis[k];
			if (X.EmojiCode != Y.EmojiCode || !X.Sequence.Equals(Y.Sequence, ESearchCase::CaseSensitive) || X.Position != Y.Position || X.Size != Y.Size)
			{
				return FString::Printf(TEXT("emoji %d"), k);
			}
		}
		if (A.VisualRuns.Num() != B.VisualRuns.Num())
		{
			return FString::Printf(TEXT("%d visual runs vs %d"), A.VisualRuns.Num(), B.VisualRuns.Num());
		}
		for (int32 k = 0; k < A.VisualRuns.Num(); k++)
		{
			const FDreamTextVisualRun& X = A.VisualRuns[k];
			const FDreamTextVisualRun& Y = B.VisualRuns[k];
			if (X.LineIndex != Y.LineIndex || X.SourceStart != Y.SourceStart || X.SourceEnd != Y.SourceEnd || X.Left != Y.Left
				|| X.Right != Y.Right || X.bRightToLeft != Y.bRightToLeft)
			{
				return FString::Printf(TEXT("visual run %d: line %d [%d, %d) %.9g..%.9g vs line %d [%d, %d) %.9g..%.9g"), k, X.LineIndex,
					X.SourceStart, X.SourceEnd, X.Left, X.Right, Y.LineIndex, Y.SourceStart, Y.SourceEnd, Y.Left, Y.Right);
			}
		}
		if (A.PreferredSize != B.PreferredSize)
		{
			return FString::Printf(TEXT("preferred size (%.9g, %.9g) vs (%.9g, %.9g)"), A.PreferredSize.X, A.PreferredSize.Y, B.PreferredSize.X, B.PreferredSize.Y);
		}
		if (A.bTruncated != B.bTruncated) return TEXT("bTruncated");
		if (A.bHasPendingGlyphs != B.bHasPendingGlyphs) return TEXT("bHasPendingGlyphs");
		if (A.VisibleCharCount != B.VisibleCharCount) return FString::Printf(TEXT("VisibleCharCount %d vs %d"), A.VisibleCharCount, B.VisibleCharCount);
		if (A.ElementCount != B.ElementCount) return FString::Printf(TEXT("ElementCount %d vs %d"), A.ElementCount, B.ElementCount);
		return FString();
	}

	/** Lays In out through State and from nothing, and fails the test with the first difference when they are not the same. */
	bool LaysOutAsFresh(FAutomationTestBase& Test, const FDreamTextLayoutInput& In, FDreamTextLayoutState& State, FDreamTextDisplayList& OutKept, const FString& What)
	{
		FDreamTextLayoutEngine::Layout(In, OutKept, &State);
		FDreamTextDisplayList Fresh;
		FDreamTextLayoutEngine::Layout(In, Fresh);
		const FString Difference = FirstDifference(OutKept, Fresh);
		if (!Difference.IsEmpty())
		{
			Test.AddError(FString::Printf(TEXT("%s: the layout built on the kept one is not the fresh one -- %s. The text: \"%s\""), *What, *Difference,
				*In.Content.ReplaceCharWithEscapedChar()));
			return false;
		}
		return true;
	}

	/*
	 * The fuzz's material. Words in four scripts, emoji sequences, combining marks and a conjunct, spaces, tabs and newlines;
	 * rich text adds tags of every kind the parser knows, character references and inline images.
	 */
	const TCHAR* const FuzzWords[] =
	{
		TEXT("the"), TEXT("quick"), TEXT("brown"), TEXT("fox"), TEXT("jumps"), TEXT("over"), TEXT("lazy"), TEXT("dog"), TEXT("office"),
		TEXT("waffle"), TEXT("AVATAR"), TEXT("Toyota"), TEXT("layout"), TEXT("incremental"), TEXT("re-break"), TEXT("3.14"), TEXT("(nested)"),
		TEXT("\u0645\u0631\u062D\u0628\u0627"), TEXT("\u0628\u0627\u0644\u0639\u0631\u0628\u064A\u0629"), TEXT("\u0644\u0627"),
		TEXT("\u65E5\u672C\u8A9E"), TEXT("\u6F22\u5B57\u304B\u306A\u4EEE\u540D"), TEXT("\u3002"), TEXT("\u300C\u5F15\u7528\u300D"),
		TEXT("\u4E2D\u6587\u6BB5\u843D\u6392\u7248"), TEXT("\u0E20\u0E32\u0E29\u0E32\u0E44\u0E17\u0E22"), TEXT("\u0E2A\u0E27\u0E31\u0E2A\u0E14\u0E35"),
		TEXT("\U0001F44D\U0001F3FD"), TEXT("\U0001F468\u200D\U0001F469\u200D\U0001F467"), TEXT("\U0001F1EF\U0001F1F5"), TEXT("1\uFE0F\u20E3"),
		TEXT("\u2764\uFE0F"), TEXT("\U0001F600"), TEXT("e\u0301te\u0301"), TEXT("\u0915\u094D\u0937\u0924\u094D\u0930"),
	};
	const TCHAR* const FuzzSpaces[] = { TEXT(" "), TEXT(" "), TEXT(" "), TEXT("  "), TEXT("\t"), TEXT("\n"), TEXT("\r\n") };
	const TCHAR* const FuzzTags[] =
	{
		TEXT("<b>"), TEXT("</b>"), TEXT("<i>"), TEXT("</i>"), TEXT("<u>"), TEXT("</u>"), TEXT("<s>"), TEXT("</s>"), TEXT("<size=30>"),
		TEXT("</size>"), TEXT("<color=#ff0000>"), TEXT("</color>"), TEXT("<sup>"), TEXT("</sup>"), TEXT("<sub>"), TEXT("</sub>"),
		TEXT("<warn>"), TEXT("</warn>"), TEXT("<a=link>"), TEXT("</a>"), TEXT("<lang=ja>"), TEXT("<lang=zh-Hans>"), TEXT("</lang>"),
		TEXT("<img=Icon/>"), TEXT("&lt;"), TEXT("&amp;"),
	};

	template <typename T, int32 N>
	const TCHAR* Pick(FRandomStream& Random, T (&Array)[N])
	{
		return Array[Random.RandRange(0, N - 1)];
	}

	/** A run of words, spaces and (for rich text) tags. */
	FString MakePiece(FRandomStream& Random, bool bRich, int32 Words)
	{
		FString Piece;
		for (int32 k = 0; k < Words; k++)
		{
			if (bRich && Random.RandRange(0, 5) == 0)
			{
				Piece += Pick(Random, FuzzTags);
			}
			Piece += Pick(Random, FuzzWords);
			Piece += Random.RandRange(0, 9) == 0 ? Pick(Random, FuzzSpaces) : TEXT(" ");
		}
		return Piece;
	}

	bool IsLowSurrogate(TCHAR C)
	{
		return C >= 0xDC00 && C <= 0xDFFF;
	}

	/** Position moved back off the second half of a surrogate pair, so that nothing cuts a character in two. */
	int32 SnapPosition(const FString& Text, int32 Position)
	{
		Position = FMath::Clamp(Position, 0, Text.Len());
		if (Position > 0 && Position < Text.Len() && IsLowSurrogate(Text[Position]))
		{
			Position--;
		}
		return Position;
	}

	/** A position in Text that does not fall inside a surrogate pair: anywhere, or its end. */
	int32 RandomPosition(FRandomStream& Random, const FString& Text, bool bAtEnd = false)
	{
		return SnapPosition(Text, bAtEnd ? Text.Len() : Random.RandRange(0, Text.Len()));
	}

	/** One random edit, as someone at a keyboard makes them: typing, deleting, replacing, pasting, splitting and joining paragraphs, undoing. */
	FString Edit(FRandomStream& Random, const FString& Text, const TArray<FString>& History, bool bRich)
	{
		const int32 Kind = Text.Len() > 1500 ? Random.RandRange(25, 64) : Random.RandRange(0, 99);
		FString Result = Text;
		if (Kind < 25)
		{
			// A keystroke, at the end half the time.
			const TCHAR* const Keys[] = { TEXT("a"), TEXT("e"), TEXT("t"), TEXT(" "), TEXT("\u0645"), TEXT("\u6587"), TEXT("\u0E01"), TEXT("."), TEXT("\t") };
			Result.InsertAt(RandomPosition(Random, Text, Random.RandRange(0, 1) == 0), Pick(Random, Keys));
		}
		else if (Kind < 40)
		{
			// A backspace: one code point, both halves of a surrogate pair.
			const int32 Position = RandomPosition(Random, Text);
			if (Position > 0)
			{
				const int32 Units = Position >= 2 && IsLowSurrogate(Text[Position - 1]) ? 2 : 1;
				Result.RemoveAt(Position - Units, Units);
			}
		}
		else if (Kind < 55)
		{
			Result.InsertAt(RandomPosition(Random, Text), MakePiece(Random, bRich, Random.RandRange(1, 3)));
		}
		else if (Kind < 65)
		{
			const int32 Start = RandomPosition(Random, Text);
			const int32 End = SnapPosition(Text, Start + Random.RandRange(1, 24));
			if (End > Start)
			{
				Result.RemoveAt(Start, End - Start);
			}
		}
		else if (Kind < 75)
		{
			const int32 Start = RandomPosition(Random, Text);
			const int32 End = FMath::Max(SnapPosition(Text, Start + Random.RandRange(1, 12)), Start);
			Result = Text.Mid(0, Start) + MakePiece(Random, bRich, Random.RandRange(1, 2)) + Text.Mid(End);
		}
		else if (Kind < 83)
		{
			// A paste of a few lines.
			FString Paste = MakePiece(Random, bRich, Random.RandRange(2, 6)) + TEXT("\n") + MakePiece(Random, bRich, Random.RandRange(2, 6));
			Result.InsertAt(RandomPosition(Random, Text), Paste);
		}
		else if (Kind < 92)
		{
			// Enter, or a backspace at a line's start that joins two paragraphs.
			int32 Newline = INDEX_NONE;
			if (Random.RandRange(0, 1) == 0 && Text.FindChar(TEXT('\n'), Newline))
			{
				const int32 Count = Text.Len();
				const int32 From = Random.RandRange(0, Count - 1);
				Newline = Text.Find(TEXT("\n"), ESearchCase::CaseSensitive, ESearchDir::FromStart, From);
				if (Newline == INDEX_NONE)
				{
					Text.FindChar(TEXT('\n'), Newline);
				}
				Result.RemoveAt(Newline, 1);
			}
			else
			{
				Result.InsertAt(RandomPosition(Random, Text), TEXT("\n"));
			}
		}
		else if (History.Num() > 0)
		{
			// Undo: back to what the text was a few edits ago.
			Result = History[FMath::Max(0, History.Num() - Random.RandRange(1, 3))];
		}
		return Result;
	}

	/**
	 * Seeded edits on a text laid out through one state, each compared with a layout from nothing. Now and then the box
	 * changes width, which keeps every paragraph's measurement and breaks and places every line again.
	 */
	bool RunEditFuzz(FAutomationTestBase& Test, const FDreamTextLayoutInput& Base, int32 Seed, int32 Edits, const TCHAR* What,
		FDreamTextLayoutStats& OutStats)
	{
		FRandomStream Random(Seed);
		FDreamTextLayoutInput In = Base;
		const bool bRich = Base.bRichText;
		In.Content.Reset();
		const int32 Paragraphs = Random.RandRange(3, 6);
		for (int32 p = 0; p < Paragraphs; p++)
		{
			In.Content += MakePiece(Random, bRich, Random.RandRange(3, 14));
			if (p + 1 < Paragraphs)
			{
				In.Content += TEXT("\n");
			}
		}
		FDreamTextLayoutState State;
		FDreamTextDisplayList Kept;
		TArray<FString> History;
		FDreamTextLayoutEngine::ResetStats();
		for (int32 Step = 0; Step <= Edits; Step++)
		{
			if (Step > 0)
			{
				History.Add(In.Content);
				In.Content = Edit(Random, In.Content, History, bRich);
				if (Random.RandRange(0, 19) == 0)
				{
					In.Width = Base.Width * Random.FRandRange(0.6f, 1.4f);
				}
			}
			if (!LaysOutAsFresh(Test, In, State, Kept, FString::Printf(TEXT("%s, seed %d, edit %d"), What, Seed, Step)))
			{
				return false;
			}
		}
		OutStats = FDreamTextLayoutEngine::GetStats();
		return true;
	}

	/** Words of English, seeded: a paragraph of about Length characters. */
	FString MakeEnglish(int32 Seed, int32 Length)
	{
		const TCHAR* const Words[] = { TEXT("the"), TEXT("of"), TEXT("layout"), TEXT("text"), TEXT("which"), TEXT("paragraph"), TEXT("measure"),
			TEXT("glyph"), TEXT("quick"), TEXT("brown"), TEXT("fox"), TEXT("jumps"), TEXT("over"), TEXT("lazy"), TEXT("dog"), TEXT("and"), TEXT("a"),
			TEXT("line"), TEXT("break"), TEXT("kept"), TEXT("edit"), TEXT("typed"), TEXT("again"), TEXT("never"), TEXT("before") };
		FRandomStream Random(Seed);
		FString Text;
		while (Text.Len() < Length)
		{
			Text += Words[Random.RandRange(0, (int32)UE_ARRAY_COUNT(Words) - 1)];
			Text += TEXT(" ");
		}
		return Text.Left(Length);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextIncrementalLayoutFuzzMockTest,
	"DreamGUI.Text.IncrementalLayout.EveryEditLaysOutAsALayoutFromNothingWouldOnTheMockFont",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextIncrementalLayoutFuzzMockTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextIncrementalLayoutTestLocal;
	FScopedGameWorld TestWorld;
	FScopedConsoleInt Incremental(TEXT("DreamGUI.Text.IncrementalLayout"), 1);
	UDreamTextTestFont* Font = NewObject<UDreamTextTestFont>(TestWorld.World);

	struct FCase
	{
		const TCHAR* Name;
		TFunction<void(FDreamTextLayoutInput&)> Setup;
	};
	const FCase Cases[] =
	{
		{ TEXT("plain text, wrapped"), [](FDreamTextLayoutInput& In) {} },
		{ TEXT("rich text, wrapped and justified, the last line too, middle-aligned"), [](FDreamTextLayoutInput& In)
			{
				In.bRichText = true;
				In.ParagraphHAlign = EDreamUITextParagraphHorizontalAlign::Justify;
				In.LastLineAlign = EDreamTextLastLineAlign::Justify;
				In.ParagraphVAlign = EDreamUITextParagraphVerticalAlign::Middle;
			} },
		{ TEXT("rich text on one line a paragraph, centred, letter-spaced"), [](FDreamTextLayoutInput& In)
			{
				In.bRichText = true;
				In.OverflowType = EDreamUITextOverflowType::HorizontalOverflow;
				In.ParagraphHAlign = EDreamUITextParagraphHorizontalAlign::Center;
				In.FontSpace = FVector2f(2.0f, 3.0f);
			} },
		{ TEXT("rich text, wrapped, right-aligned, inter-character justification, tab size 4, negative letter spacing"), [](FDreamTextLayoutInput& In)
			{
				In.bRichText = true;
				In.ParagraphHAlign = EDreamUITextParagraphHorizontalAlign::Right;
				In.TabSize = 4.0f;
				In.FontSpace = FVector2f(-1.0f, 0.0f);
				In.ParagraphVAlign = EDreamUITextParagraphVerticalAlign::Bottom;
				In.Pivot = FVector2f(0.2f, 0.7f);
			} },
		{ TEXT("plain text, wrapped under an ellipsis in a short box"), [](FDreamTextLayoutInput& In)
			{
				In.OverflowType = EDreamUITextOverflowType::Ellipsis;
				In.bAutoWrapText = true;
				In.Height = 160.0f;
			} },
		{ TEXT("rich text, a line each, middle ellipsis"), [](FDreamTextLayoutInput& In)
			{
				In.bRichText = true;
				In.OverflowType = EDreamUITextOverflowType::MiddleEllipsis;
			} },
	};
	int64 Reused = 0;
	int64 LinesReused = 0;
	for (int32 c = 0; c < (int32)UE_ARRAY_COUNT(Cases); c++)
	{
		FDreamTextLayoutInput In = MakeInput(Font, FString());
		Cases[c].Setup(In);
		FDreamTextLayoutStats Stats;
		if (!RunEditFuzz(*this, In, 1000 + c, 150, Cases[c].Name, Stats))
		{
			continue;
		}
		Reused += Stats.ParagraphsReused;
		LinesReused += Stats.LinesReused;
	}
	// Equality alone would hold for a layout that never took anything from what it kept.
	TestTrue(TEXT("The edits took paragraphs from the kept layout"), Reused > 0);
	TestTrue(TEXT("and lines' placements"), LinesReused > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextIncrementalLayoutFuzzShapedTest,
	"DreamGUI.Text.IncrementalLayout.EveryEditLaysOutAsALayoutFromNothingWouldOnShapedFonts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextIncrementalLayoutFuzzShapedTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextIncrementalLayoutTestLocal;
	FScopedGameWorld TestWorld;
	FScopedConsoleInt Incremental(TEXT("DreamGUI.Text.IncrementalLayout"), 1);
	FScopedConsoleInt ShapeCache(TEXT("DreamGUI.Text.ShapeCache"), 1);
	UDreamUIFontData_DistanceField* Font = MakeMixedScriptFont(TestWorld.World);

	struct FCase
	{
		const TCHAR* Name;
		TFunction<void(FDreamTextLayoutInput&)> Setup;
	};
	const FCase Cases[] =
	{
		{ TEXT("rich text, wrapped, ligatures on"), [](FDreamTextLayoutInput& In)
			{
				In.bRichText = true;
				In.bAllowLigatures = true;
			} },
		{ TEXT("plain text, wrapped, justified between characters, phrase wrap"), [](FDreamTextLayoutInput& In)
			{
				In.ParagraphHAlign = EDreamUITextParagraphHorizontalAlign::Justify;
				In.TextJustify = EDreamTextJustify::InterCharacter;
				In.PhraseWrap = EDreamTextPhraseWrap::CJKDictionary;
			} },
		{ TEXT("rich text, wrapped, right to left, centred"), [](FDreamTextLayoutInput& In)
			{
				In.bRichText = true;
				In.FlowDirection = EDreamTextFlowDirection::RightToLeft;
				In.ParagraphHAlign = EDreamUITextParagraphHorizontalAlign::Center;
				In.ParagraphVAlign = EDreamUITextParagraphVerticalAlign::Bottom;
			} },
		{ TEXT("plain text, a line each, Japanese, letter-spaced"), [](FDreamTextLayoutInput& In)
			{
				In.OverflowType = EDreamUITextOverflowType::HorizontalOverflow;
				In.Language = TEXT("ja");
				In.FontSpace = FVector2f(1.5f, 0.0f);
			} },
	};
	int64 Reused = 0;
	int64 LinesReused = 0;
	for (int32 c = 0; c < (int32)UE_ARRAY_COUNT(Cases); c++)
	{
		FDreamTextLayoutInput In = MakeInput(Font, FString());
		Cases[c].Setup(In);
		FDreamTextLayoutStats Stats;
		if (!RunEditFuzz(*this, In, 2000 + c, 100, Cases[c].Name, Stats))
		{
			continue;
		}
		Reused += Stats.ParagraphsReused;
		LinesReused += Stats.LinesReused;
	}
	TestTrue(TEXT("The edits took paragraphs from the kept layout"), Reused > 0);
	TestTrue(TEXT("and lines' placements"), LinesReused > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextIncrementalLayoutKeystrokeTest,
	"DreamGUI.Text.IncrementalLayout.AKeystrokeAtTheEndOfALongParagraphShapesAndPlacesAtMostTwoOfEach",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextIncrementalLayoutKeystrokeTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextIncrementalLayoutTestLocal;
	FScopedGameWorld TestWorld;
	FScopedConsoleInt Incremental(TEXT("DreamGUI.Text.IncrementalLayout"), 1);
	FScopedConsoleInt ShapeCache(TEXT("DreamGUI.Text.ShapeCache"), 1);
	UDreamUIFontData_DistanceField* Font = MakeFileFont(TestWorld.World, TEXT("Roboto-Regular.ttf"));

	FDreamTextLayoutInput In = MakeInput(Font, MakeEnglish(7, 2000), 600.0f);
	FDreamTextLayoutState State;
	FDreamTextDisplayList Kept;
	if (!LaysOutAsFresh(*this, In, State, Kept, TEXT("The paragraph as it is")))
	{
		return false;
	}
	TestTrue(TEXT("The paragraph wraps into thirty lines or more"), Kept.Lines.Num() >= 30);
	const TCHAR* const Keys = TEXT("text, typed on");
	for (int32 Key = 0; Keys[Key] != 0; Key++)
	{
		In.Content.AppendChar(Keys[Key]);
		FDreamTextShapeCache::ResetStats();
		FDreamTextLayoutEngine::ResetStats();
		FDreamTextLayoutEngine::Layout(In, Kept, &State);
		const FDreamTextShapeCache::FStats Shaped = FDreamTextShapeCache::GetStats();
		const FDreamTextLayoutStats Laid = FDreamTextLayoutEngine::GetStats();
		const FString What = FString::Printf(TEXT("Keystroke %d ('%c')"), Key, Keys[Key]);
		TestEqual(What + TEXT(" built on the kept layout"), Laid.IncrementalLayouts, (int64)1);
		TestTrue(What + FString::Printf(TEXT(" made at most 2 hb_shape calls (%lld)"), Shaped.ShapeCalls), Shaped.ShapeCalls <= 2);
		TestTrue(What + FString::Printf(TEXT(" placed at most 2 lines (%lld)"), Laid.LinesPlaced), Laid.LinesPlaced <= 2);
		TestEqual(What + TEXT(": every other line kept its placement"), Laid.LinesPlaced + Laid.LinesReused, (int64)Kept.Lines.Num());
		TestEqual(What + TEXT(" measured the one paragraph"), Laid.ParagraphsMeasured, (int64)1);
		TestTrue(What + FString::Printf(TEXT(" asked ICU about the end of the paragraph only (%lld code units)"), Laid.IcuCodeUnits),
			Laid.IcuCodeUnits < 200);
		FDreamTextDisplayList Fresh;
		FDreamTextLayoutEngine::Layout(In, Fresh);
		const FString Difference = FirstDifference(Kept, Fresh);
		TestTrue(What + TEXT(" laid out as a layout from nothing would: ") + Difference, Difference.IsEmpty());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextIncrementalLayoutOneOfTwentyTest,
	"DreamGUI.Text.IncrementalLayout.TypingInOneOfTwentyParagraphsMeasuresThatParagraphAlone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextIncrementalLayoutOneOfTwentyTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextIncrementalLayoutTestLocal;
	FScopedGameWorld TestWorld;
	FScopedConsoleInt Incremental(TEXT("DreamGUI.Text.IncrementalLayout"), 1);
	UDreamTextTestFont* Mock = NewObject<UDreamTextTestFont>(TestWorld.World);
	UDreamUIFontData_DistanceField* Roboto = MakeFileFont(TestWorld.World, TEXT("Roboto-Regular.ttf"));

	FString Content;
	for (int32 p = 0; p < 20; p++)
	{
		Content += MakeEnglish(100 + p, 100);
		if (p < 19)
		{
			Content += TEXT("\n");
		}
	}
	for (UDreamUIFontData_BaseObject* Font : { (UDreamUIFontData_BaseObject*)Mock, (UDreamUIFontData_BaseObject*)Roboto })
	{
		const FString Name = Font == Mock ? TEXT("The mock font") : TEXT("Roboto");
		FDreamTextLayoutInput In = MakeInput(Font, Content, 500.0f);
		FDreamTextLayoutState State;
		FDreamTextDisplayList Kept;
		LaysOutAsFresh(*this, In, State, Kept, Name + TEXT(", before the edit"));
		// A word typed into the eighth paragraph, at its middle.
		int32 Position = 0;
		for (int32 p = 0; p < 7; p++)
		{
			Position = In.Content.Find(TEXT("\n"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Position) + 1;
		}
		Position += 50;
		for (int32 Key = 0; Key < 4; Key++)
		{
			In.Content.InsertAt(Position + Key, TEXT("word")[Key]);
			FDreamTextLayoutEngine::ResetStats();
			FDreamTextLayoutEngine::Layout(In, Kept, &State);
			const FDreamTextLayoutStats Laid = FDreamTextLayoutEngine::GetStats();
			const FString What = Name + FString::Printf(TEXT(", keystroke %d"), Key);
			TestEqual(What + TEXT(": one paragraph measured"), Laid.ParagraphsMeasured, (int64)1);
			TestEqual(What + TEXT(": nineteen taken as they were"), Laid.ParagraphsReused, (int64)19);
			TestTrue(What + FString::Printf(TEXT(": only the edited paragraph's lines placed again (%lld)"), Laid.LinesPlaced), Laid.LinesPlaced <= 5);
			TestEqual(What + TEXT(": every other line kept its placement"), Laid.LinesPlaced + Laid.LinesReused, (int64)Kept.Lines.Num());
			FDreamTextDisplayList Fresh;
			FDreamTextLayoutEngine::Layout(In, Fresh);
			const FString Difference = FirstDifference(Kept, Fresh);
			TestTrue(What + TEXT(": laid out as a layout from nothing would: ") + Difference, Difference.IsEmpty());
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextIncrementalLayoutInvalidationTest,
	"DreamGUI.Text.IncrementalLayout.FallbacksAnAtlasFlushALanguageOrTheSwitchMakeTheNextLayoutStartFromNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextIncrementalLayoutInvalidationTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextIncrementalLayoutTestLocal;
	FScopedGameWorld TestWorld;
	FScopedConsoleInt Incremental(TEXT("DreamGUI.Text.IncrementalLayout"), 1);
	UDreamUIFontData_DistanceField* Roboto = MakeFileFont(TestWorld.World, TEXT("Roboto-Regular.ttf"));
	UDreamUIFontData_DistanceField* Arabic = MakeFileFont(TestWorld.World, TEXT("NotoNaskhArabicUI-Regular.ttf"));
	UDreamUIFontData_DistanceField* Droid = MakeFileFont(TestWorld.World, TEXT("DroidSansFallback.ttf"));
	Roboto->SetFallbackFonts({ Arabic, Droid });

	FDreamTextLayoutInput In = MakeInput(Roboto, TEXT("A first paragraph \u65E5\u672C\u8A9E and \u0645\u0631\u062D\u0628\u0627.\nA second one, 12345.\nA third."), 300.0f);
	FDreamTextLayoutState State;
	FDreamTextDisplayList Kept;
	// Lays out with the state and says whether it built on what was kept; the result is held against a fresh layout.
	auto LayOut = [&](const TCHAR* What)
	{
		FDreamTextLayoutEngine::ResetStats();
		FDreamTextLayoutEngine::Layout(In, Kept, &State);
		const bool bBuiltOn = FDreamTextLayoutEngine::GetStats().IncrementalLayouts > 0;
		FDreamTextDisplayList Fresh;
		FDreamTextLayoutEngine::Layout(In, Fresh);
		const FString Difference = FirstDifference(Kept, Fresh);
		TestTrue(FString(What) + TEXT(": laid out as a layout from nothing would: ") + Difference, Difference.IsEmpty());
		return bBuiltOn;
	};

	LayOut(TEXT("The first layout"));
	TestTrue(TEXT("The same text again builds on what was kept"), LayOut(TEXT("The same text again")));

	Roboto->SetFallbackFonts({ Droid, Arabic });
	TestFalse(TEXT("Fallbacks swapped: the kept layout is not built on"), LayOut(TEXT("Fallbacks swapped")));
	TestTrue(TEXT("and the layout after it builds on that one"), LayOut(TEXT("After the swap")));

	FDreamUIFontFallback Japanese;
	Japanese.Font = Droid;
	Japanese.Cultures = TEXT("ja");
	FDreamUIFontFallback Arab;
	Arab.Font = Arabic;
	Roboto->SetFallbacks({ Arab, Japanese });
	TestFalse(TEXT("SetFallbacks: the kept layout is not built on"), LayOut(TEXT("SetFallbacks")));
	LayOut(TEXT("After SetFallbacks"));

	Roboto->FlushGlyphAtlas();
	TestFalse(TEXT("The atlas flushed: the kept layout, whose quads pointed into the old atlas, is not built on"), LayOut(TEXT("Atlas flushed")));
	LayOut(TEXT("After the flush"));

	In.Language = TEXT("ja");
	TestFalse(TEXT("Another language: the kept layout is not built on"), LayOut(TEXT("Language ja")));
	In.Content += TEXT(" More.");
	TestTrue(TEXT("An edit after it builds on the layout in the new language"), LayOut(TEXT("An edit in ja")));

	{
		FScopedConsoleInt Off(TEXT("DreamGUI.Text.IncrementalLayout"), 0);
		In.Content += TEXT(" And more.");
		TestFalse(TEXT("DreamGUI.Text.IncrementalLayout 0: nothing is built on"), LayOut(TEXT("Switched off")));
		TestFalse(TEXT("and nothing is kept"), State.HasLayout());
	}
	In.Content += TEXT(" Then some.");
	TestFalse(TEXT("Switched on again: the first layout has nothing to build on"), LayOut(TEXT("Switched on again")));
	TestTrue(TEXT("and keeps one"), State.HasLayout());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextIncrementalLayoutCacheTest,
	"DreamGUI.Text.IncrementalLayout.TheGeometryCacheKeepsALayoutWhenAskedOrWhenALongTextChangesTwiceInARow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextIncrementalLayoutCacheTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextIncrementalLayoutTestLocal;
	FScopedGameWorld TestWorld;
	FScopedConsoleInt Incremental(TEXT("DreamGUI.Text.IncrementalLayout"), 1);
	UDreamTextTestFont* Font = NewObject<UDreamTextTestFont>(TestWorld.World);

	// Asked for: from the first layout on.
	{
		FDreamUITextGeometryCache Cache;
		Cache.SetIncrementalLayout(true);
		FDreamTextLayoutInput In = MakeInput(Font, TEXT("A short field\nbeing typed into"));
		Cache.SetLayoutInput(In);
		Cache.EnsureLayout();
		TestTrue(TEXT("A text asked to keeps its layout"), Cache.IsIncrementalLayoutActive());
		In.Content += TEXT("!");
		Cache.SetLayoutInput(In);
		FDreamTextLayoutEngine::ResetStats();
		Cache.EnsureLayout();
		TestEqual(TEXT("and the next layout builds on it"), FDreamTextLayoutEngine::GetStats().IncrementalLayouts, (int64)1);
		Cache.MarkDirty();
		FDreamTextLayoutEngine::ResetStats();
		Cache.EnsureLayout();
		TestEqual(TEXT("MarkDirty drops what was kept"), FDreamTextLayoutEngine::GetStats().IncrementalLayouts, (int64)0);
		FDreamTextDisplayList Fresh;
		FDreamTextLayoutEngine::Layout(In, Fresh);
		const FString Difference = FirstDifference(Cache.GetDisplayList(), Fresh);
		TestTrue(TEXT("The cache's display list is a fresh layout's: ") + Difference, Difference.IsEmpty());
		Cache.SetIncrementalLayout(false);
		In.Content += TEXT("?");
		Cache.SetLayoutInput(In);
		Cache.EnsureLayout();
		TestFalse(TEXT("Asked no more, a short text lets go of it"), Cache.IsIncrementalLayoutActive());
	}

	// By itself: a long text whose content changed in two layouts in a row.
	{
		FDreamUITextGeometryCache Cache;
		FDreamTextLayoutInput In = MakeInput(Font, MakeEnglish(3, 300));
		Cache.SetLayoutInput(In);
		Cache.EnsureLayout();
		TestFalse(TEXT("A long text laid out once keeps nothing"), Cache.IsIncrementalLayoutActive());
		In.Width += 20.0f;
		Cache.SetLayoutInput(In);
		Cache.EnsureLayout();
		TestFalse(TEXT("nor laid out again at another width"), Cache.IsIncrementalLayoutActive());
		In.Content += TEXT("a");
		Cache.SetLayoutInput(In);
		Cache.EnsureLayout();
		TestFalse(TEXT("nor after one edit"), Cache.IsIncrementalLayoutActive());
		In.Content += TEXT("b");
		Cache.SetLayoutInput(In);
		Cache.EnsureLayout();
		TestTrue(TEXT("but after a second edit in a row it keeps its layout"), Cache.IsIncrementalLayoutActive());
		In.Content += TEXT("c");
		Cache.SetLayoutInput(In);
		FDreamTextLayoutEngine::ResetStats();
		Cache.EnsureLayout();
		TestEqual(TEXT("and the next edit builds on it"), FDreamTextLayoutEngine::GetStats().IncrementalLayouts, (int64)1);
		In.Content = MakeEnglish(3, 100);
		Cache.SetLayoutInput(In);
		Cache.EnsureLayout();
		TestFalse(TEXT("Cut under 256 elements, it lets go of it"), Cache.IsIncrementalLayoutActive());
	}

	// A short text edited over and over never keeps one.
	{
		FDreamUITextGeometryCache Cache;
		FDreamTextLayoutInput In = MakeInput(Font, MakeEnglish(4, 100));
		for (int32 Edit = 0; Edit < 5; Edit++)
		{
			In.Content += TEXT("x");
			Cache.SetLayoutInput(In);
			Cache.EnsureLayout();
		}
		TestFalse(TEXT("A short text edited five times keeps nothing"), Cache.IsIncrementalLayoutActive());
	}

	// The switch turns the asked-for kind off too.
	{
		FDreamUITextGeometryCache Cache;
		Cache.SetIncrementalLayout(true);
		FScopedConsoleInt Off(TEXT("DreamGUI.Text.IncrementalLayout"), 0);
		Cache.SetLayoutInput(MakeInput(Font, TEXT("Typed into, switched off")));
		Cache.EnsureLayout();
		TestFalse(TEXT("With DreamGUI.Text.IncrementalLayout 0 even a text that asks keeps nothing"), Cache.IsIncrementalLayoutActive());
	}
	return true;
}

#endif
