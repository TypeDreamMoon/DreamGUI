// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIGeometry.h"
#include "Core/Text/DreamTextLayout.h"
#include "Core/Text/DreamTextPainter.h"
#include "Engine/World.h"
#include "Tests/DreamTextTestFont.h"

/*
 * The text pipeline at the display-list level. These run the layout engine and the painter directly
 * against a font with made-up metrics, so they assert on structure -- lines, items, carets, tags,
 * what gets emitted -- rather than on pixel positions that would only restate the mock.
 */
namespace DreamTextLayoutTestLocal
{
	struct FScopedGameWorld
	{
		UWorld* World = nullptr;
		FScopedGameWorld() { World = UWorld::CreateWorld(EWorldType::Game, false); }
		~FScopedGameWorld() { if (World) { World->DestroyWorld(false); } }
	};

	FDreamTextLayoutInput MakeInput(UDreamUIFontData_BaseObject* Font, const FString& Content, float Width = 300.0f, float Height = 120.0f)
	{
		FDreamTextLayoutInput In;
		In.Content = Content;
		In.Width = Width;
		In.Height = Height;
		In.Pivot = FVector2f(0.5f, 0.5f);
		In.FontSize = 24.0f;
		In.ParagraphHAlign = EDreamUITextParagraphHorizontalAlign::Left;
		In.ParagraphVAlign = EDreamUITextParagraphVerticalAlign::Top;
		In.bUseKerning = true;
		In.Font = Font;
		return In;
	}

	FDreamTextPaintParams MakePaint(const FColor& Base = FColor::White)
	{
		FDreamTextPaintParams P;
		P.ItalicSlope = 0.26f;
		P.bWriteFontScaleToUV2 = true;
		P.FontScaleMultiplier = 4.0f;
		P.BaseColor = Base;
		return P;
	}

	int32 CountEmittedGlyphs(const FDreamTextDisplayList& DL)
	{
		int32 N = 0;
		for (const auto& Item : DL.Items) if (Item.bEmit) N++;
		return N;
	}

	/** Right edge of the box, in the text's local space, for a centred pivot. */
	float BoxRight(const FDreamTextLayoutInput& In) { return In.Width * (0.5f - In.Pivot.X) + In.Width * 0.5f; }
	float BoxLeft(const FDreamTextLayoutInput& In) { return In.Width * (0.5f - In.Pivot.X) - In.Width * 0.5f; }

	float ItemRight(const FDreamTextGlyphItem& Item) { return Item.Pen.X + Item.Glyph.XOffset + Item.Glyph.Width; }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextLayoutMeasuresWithoutEmittingTest,
	"DreamGUI.Text.Pipeline.LayoutMeasuresWithoutEmitting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextLayoutMeasuresWithoutEmittingTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextLayoutTestLocal;
	FScopedGameWorld TestWorld;
	UDreamTextTestFont* Font = NewObject<UDreamTextTestFont>(TestWorld.World);

	FDreamTextDisplayList Small, Large;
	FDreamTextLayoutInput In = MakeInput(Font, TEXT("Measure me"));
	FDreamTextLayoutEngine::Layout(In, Small);
	In.FontSize = 48.0f;
	FDreamTextLayoutEngine::Layout(In, Large);

	TestTrue(TEXT("a larger font measures wider"), Large.PreferredSize.X > Small.PreferredSize.X);
	TestTrue(TEXT("and taller"), Large.PreferredSize.Y > Small.PreferredSize.Y);
	TestEqual(TEXT("one line of items"), Small.Lines.Num(), 1);
	TestEqual(TEXT("every code unit is an item, spaces included"), Small.Items.Num(), 10);
	TestEqual(TEXT("spaces do not emit"), CountEmittedGlyphs(Small), 9);
	TestEqual(TEXT("visible count matches"), Small.VisibleCharCount, 9);

	// The painter is a separate step: the display list alone is not geometry.
	FDreamUIGeometry Geometry;
	TArray<FDreamUITextCharProperty> Chars;
	FDreamTextPainter::Paint(Small, MakePaint(), Geometry, Chars);
	TestEqual(TEXT("four vertices per emitted glyph"), Geometry.OriginVertices.Num(), 9 * 4);
	TestEqual(TEXT("six indices per emitted glyph"), Geometry.Triangles.Num(), 9 * 6);
	TestEqual(TEXT("one char property per emitted glyph"), Chars.Num(), 9);
	for (int32 i = 0; i < Chars.Num(); i++)
	{
		TestEqual(*FString::Printf(TEXT("char %d starts at vertex %d"), i, i * 4), Chars[i].StartVertIndex, i * 4);
		TestEqual(*FString::Printf(TEXT("char %d has four vertices"), i), Chars[i].VertCount, 4);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextLayoutWrapsInsideTheBoxTest,
	"DreamGUI.Text.Pipeline.VerticalOverflowWrapsInsideTheBox",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextLayoutWrapsInsideTheBoxTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextLayoutTestLocal;
	FScopedGameWorld TestWorld;
	UDreamTextTestFont* Font = NewObject<UDreamTextTestFont>(TestWorld.World);
	const FString Content = TEXT("The quick brown fox jumps over the lazy dog and keeps running through the field");

	FDreamTextLayoutInput In = MakeInput(Font, Content, 200.0f, 300.0f);
	FDreamTextDisplayList Single;
	FDreamTextLayoutEngine::Layout(In, Single);
	TestEqual(TEXT("horizontal overflow keeps one line"), Single.Lines.Num(), 1);

	In.OverflowType = EDreamUITextOverflowType::VerticalOverflow;
	FDreamTextDisplayList Wrapped;
	FDreamTextLayoutEngine::Layout(In, Wrapped);
	TestTrue(TEXT("vertical overflow wraps"), Wrapped.Lines.Num() > 1);
	// The preferred width is the unwrapped width. Not bit-identical: kerning across a break is
	// computed against the character before the dropped space, so a pair or two differ.
	TestEqual(TEXT("wrapping keeps the unwrapped preferred width (to within a kerning pair)"), Wrapped.PreferredSize.X, Single.PreferredSize.X, Single.PreferredSize.X * 0.01f);
	TestTrue(TEXT("the paragraph got taller"), Wrapped.PreferredSize.Y > Single.PreferredSize.Y);

	const float Right = BoxRight(In);
	for (const auto& Item : Wrapped.Items)
	{
		if (!Item.bEmit)continue;
		if (ItemRight(Item) > Right + 0.5f)
		{
			AddError(FString::Printf(TEXT("glyph U+%04X on line %d sticks out of the box: right edge %.2f > %.2f"), Item.Codepoint, Item.LineIndex, ItemRight(Item), Right));
			break;
		}
	}
	// Lines stack downward and each line's items agree on their line index.
	for (int32 i = 1; i < Wrapped.Items.Num(); i++)
	{
		if (Wrapped.Items[i].LineIndex < Wrapped.Items[i - 1].LineIndex)
		{
			AddError(TEXT("items are not in line order"));
			break;
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextLayoutTruncationTest,
	"DreamGUI.Text.Pipeline.TruncateAndEllipsisCutAtTheBox",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextLayoutTruncationTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextLayoutTestLocal;
	FScopedGameWorld TestWorld;
	UDreamTextTestFont* Font = NewObject<UDreamTextTestFont>(TestWorld.World);
	const FString Content = TEXT("This sentence is far too long to fit inside the box it was given");

	FDreamTextLayoutInput In = MakeInput(Font, Content, 200.0f, 60.0f);
	In.OverflowType = EDreamUITextOverflowType::Truncate;
	FDreamTextDisplayList Truncated;
	FDreamTextLayoutEngine::Layout(In, Truncated);
	TestTrue(TEXT("truncate reports itself"), Truncated.bTruncated);
	TestTrue(TEXT("truncate drops glyphs"), CountEmittedGlyphs(Truncated) < Content.Len() - 13 /* spaces */);
	TestEqual(TEXT("truncate still lays every item out"), Truncated.Items.Num(), Content.Len());
	TestEqual(TEXT("carets still cover the whole string"), Truncated.Lines[0].CaretPropertyList.Num(), Content.Len() + 1);

	In.OverflowType = EDreamUITextOverflowType::Ellipsis;
	FDreamTextDisplayList Ellipsis;
	FDreamTextLayoutEngine::Layout(In, Ellipsis);
	TestTrue(TEXT("ellipsis reports itself"), Ellipsis.bTruncated);
	const FDreamTextGlyphItem* Last = nullptr;
	for (int32 i = Ellipsis.Items.Num() - 1; i >= 0; i--)
	{
		if (Ellipsis.Items[i].bEmit) { Last = &Ellipsis.Items[i]; break; }
	}
	if (TestNotNull(TEXT("ellipsis emits something"), Last))
	{
		TestEqual(TEXT("the last emitted glyph is the ellipsis"), (int32)Last->Codepoint, 0x2026);
		TestFalse(TEXT("which is not a visible char for TextAnimation"), Last->bCountsAsVisible);
		TestTrue(TEXT("and it sits inside the box"), ItemRight(*Last) <= BoxRight(In) + 0.5f);
	}

	// The regression the old pass had: char properties must never point past the geometry.
	FDreamUIGeometry Geometry;
	TArray<FDreamUITextCharProperty> Chars;
	FDreamTextPainter::Paint(Ellipsis, MakePaint(), Geometry, Chars);
	TestEqual(TEXT("char properties count the visible glyphs only"), Chars.Num(), Ellipsis.VisibleCharCount);
	for (const auto& Prop : Chars)
	{
		if (Prop.StartVertIndex + Prop.VertCount > Geometry.OriginVertices.Num())
		{
			AddError(TEXT("a char property points past the painted vertices"));
			break;
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextLayoutRichTextTest,
	"DreamGUI.Text.Pipeline.RichTextTagsReachTheDisplayList",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextLayoutRichTextTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextLayoutTestLocal;
	FScopedGameWorld TestWorld;
	UDreamTextTestFont* Font = NewObject<UDreamTextTestFont>(TestWorld.World);

	FDreamTextLayoutInput In = MakeInput(Font, TEXT("ab<shake>cd</shake>e <color=#ff0000>red</color> <u>u</u><size=48>H</size>"), 600.0f, 120.0f);
	In.bRichText = true;
	FDreamTextDisplayList DL;
	FDreamTextLayoutEngine::Layout(In, DL);

	if (TestEqual(TEXT("one custom tag"), DL.CustomTags.Num(), 1))
	{
		TestEqual(TEXT("tag name"), DL.CustomTags[0].TagName, FName(TEXT("shake")));
		// Visible indices: a=0 b=1 c=2 d=3 e=4 ...
		TestEqual(TEXT("tag starts at the first tagged visible char"), DL.CustomTags[0].CharIndexStart, 2);
		TestEqual(TEXT("tag ends at the last tagged visible char"), DL.CustomTags[0].CharIndexEnd, 3);
	}

	int32 Red = 0, Underlined = 0, Big = 0;
	for (const auto& Item : DL.Items)
	{
		if (!Item.bEmit)continue;
		if (Item.Style.bHasColor && Item.Style.Color == FColor::Red)Red++;
		if (Item.Style.bUnderline)Underlined++;
		if (Item.Style.Size == 48.0f)Big++;
	}
	TestEqual(TEXT("three red glyphs"), Red, 3);
	TestEqual(TEXT("one underlined glyph"), Underlined, 1);
	TestEqual(TEXT("one 48pt glyph"), Big, 1);

	FDreamUIGeometry Geometry;
	TArray<FDreamUITextCharProperty> Chars;
	FDreamTextPainter::Paint(DL, MakePaint(FColor::White), Geometry, Chars);
	// 10 visible glyphs, the underlined one carries an extra strip.
	TestEqual(TEXT("underline adds a quad"), Geometry.OriginVertices.Num(), 10 * 4 + 4);
	int32 RedVertices = 0;
	for (const auto& V : Geometry.Vertices) if (V.Color == FColor::Red)RedVertices++;
	TestEqual(TEXT("red glyphs paint red"), RedVertices, 3 * 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextPainterColourIsPaintTimeTest,
	"DreamGUI.Text.Pipeline.ColourIsAPaintInputNotALayoutInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextPainterColourIsPaintTimeTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextLayoutTestLocal;
	FScopedGameWorld TestWorld;
	UDreamTextTestFont* Font = NewObject<UDreamTextTestFont>(TestWorld.World);

	FDreamTextLayoutInput In = MakeInput(Font, TEXT("plain <color=#00ff00>green</color>"), 400.0f, 120.0f);
	In.bRichText = true;
	FDreamTextDisplayList DL;
	FDreamTextLayoutEngine::Layout(In, DL);

	FDreamUIGeometry A, B;
	TArray<FDreamUITextCharProperty> CharsA, CharsB;
	FDreamTextPainter::Paint(DL, MakePaint(FColor::White), A, CharsA);
	FDreamTextPainter::Paint(DL, MakePaint(FColor::Blue), B, CharsB);

	if (!TestEqual(TEXT("same vertex count"), B.Vertices.Num(), A.Vertices.Num()))return false;
	int32 Blue = 0, Green = 0;
	for (int32 i = 0; i < A.Vertices.Num(); i++)
	{
		if (!A.OriginVertices[i].Position.Equals(B.OriginVertices[i].Position, 1e-4f))
		{
			AddError(TEXT("repainting with another colour moved a vertex"));
			break;
		}
		if (B.Vertices[i].Color == FColor::Blue)Blue++;
		if (B.Vertices[i].Color == FColor::Green)Green++;
	}
	TestEqual(TEXT("untagged glyphs take the new base colour"), Blue, 5 * 4);
	TestEqual(TEXT("tagged glyphs keep theirs"), Green, 5 * 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextCaretContractTest,
	"DreamGUI.Text.Pipeline.CaretsOnePerCodeUnitPlusLineEnd",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextCaretContractTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextLayoutTestLocal;
	FScopedGameWorld TestWorld;
	UDreamTextTestFont* Font = NewObject<UDreamTextTestFont>(TestWorld.World);

	FDreamTextLayoutInput In = MakeInput(Font, TEXT("ab cd\nefg"), 400.0f, 120.0f);
	FDreamTextDisplayList DL;
	FDreamTextLayoutEngine::Layout(In, DL);

	if (!TestEqual(TEXT("two lines"), DL.Lines.Num(), 2))return false;
	// "ab cd" + the newline's own caret; "efg" + the end caret.
	TestEqual(TEXT("first line carets"), DL.Lines[0].CaretPropertyList.Num(), 6);
	TestEqual(TEXT("second line carets"), DL.Lines[1].CaretPropertyList.Num(), 4);
	TestEqual(TEXT("the newline caret names its char"), DL.Lines[0].CaretPropertyList[5].CharIndex, 5);
	TestEqual(TEXT("the end caret names the string length"), DL.Lines[1].CaretPropertyList[3].CharIndex, 9);
	for (int32 c = 1; c < 5; c++)
	{
		if (DL.Lines[0].CaretPropertyList[c].CaretPosition.X <= DL.Lines[0].CaretPropertyList[c - 1].CaretPosition.X)
		{
			AddError(TEXT("carets on a line must advance"));
			break;
		}
	}
	TestTrue(TEXT("the second line sits below the first"), DL.Lines[1].CaretPropertyList[0].CaretPosition.Y < DL.Lines[0].CaretPropertyList[0].CaretPosition.Y);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextBestFitMemoTest,
	"DreamGUI.Text.Pipeline.BestFitRemembersItsAnswer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextBestFitMemoTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextLayoutTestLocal;
	FScopedGameWorld TestWorld;

	UDreamWidget* Root = NewObject<UDreamWidget>(TestWorld.World, NAME_None, RF_Public | RF_Transactional);
	Root->SetWidth(800.0f);
	Root->SetHeight(600.0f);
	Root->AddComponent<UDreamCanvas>();
	Root->OnRegister();
	Root->SetWidgetActive(true);
	UDreamWidget* Child = NewObject<UDreamWidget>(TestWorld.World, NAME_None, RF_Public | RF_Transactional);
	Child->SetWidth(120.0f);
	Child->SetHeight(80.0f);
	UDreamTextTestFont* Font = NewObject<UDreamTextTestFont>(TestWorld.World);
	UDreamText* Text = Child->CreateNewVisual<UDreamText>();
	if (!TestNotNull(TEXT("text visual"), Text))return false;
	Text->SetFont(Font);
	Child->OnRegister();
	if (!TestTrue(TEXT("child attaches"), Child->TrySetParent(Root, false)))return false;
	if (!TestNotNull(TEXT("child renders through a canvas"), Child->GetRenderCanvas()))return false;

	Text->SetText(FText::FromString(TEXT("Headline")));
	Text->SetFontSize(48.0f);
	Text->SetBestFitMinSize(8.0f);
	Text->SetBestFit(true);

	const float Width = Text->GetPreferredWidth();
	const int32 RunsAfterSearch = Text->GetCacheTextGeometryData().GetLayoutRunCount();
	TestTrue(TEXT("the search ran more than one layout"), RunsAfterSearch > 1);
	TestTrue(TEXT("best fit shrank the font"), Text->GetRenderedFontSize() < 48.0f);
	TestTrue(TEXT("best fit found a size that fits the box width"), Width <= 120.0f + 0.01f);

	const float WidthAgain = Text->GetPreferredWidth();
	TestEqual(TEXT("asking again costs no layout"), Text->GetCacheTextGeometryData().GetLayoutRunCount(), RunsAfterSearch);
	TestEqual(TEXT("and gives the same answer"), WidthAgain, Width);

	Text->SetText(FText::FromString(TEXT("Hi")));
	Text->GetPreferredWidth();
	TestTrue(TEXT("a content change re-runs the search"), Text->GetCacheTextGeometryData().GetLayoutRunCount() > RunsAfterSearch);
	TestEqual(TEXT("and short text gets the full size back"), Text->GetRenderedFontSize(), 48.0f);

	Root->DestroyWidget();
	return true;
}

namespace DreamTextLayoutTestLocal
{
	/** The code points on a line, in order, emitted or not. */
	FString LineText(const FDreamTextDisplayList& DL, int32 LineIndex)
	{
		FString Result;
		for (const auto& Item : DL.Items)
		{
			if (Item.LineIndex != LineIndex)continue;
			if (Item.Codepoint < 0x10000)
			{
				Result.AppendChar((TCHAR)Item.Codepoint);
			}
			else
			{
				Result.AppendChar(TEXT('?'));
			}
		}
		return Result;
	}

	float LineRight(const FDreamTextDisplayList& DL, int32 LineIndex)
	{
		float Right = -FLT_MAX;
		for (const auto& Item : DL.Items)
		{
			if (Item.LineIndex == LineIndex && Item.bEmit)Right = FMath::Max(Right, ItemRight(Item));
		}
		return Right;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextBreakerWordWrapTest,
	"DreamGUI.Text.Breaker.EnglishWrapsBetweenWordsAndSpacesHang",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextBreakerWordWrapTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextLayoutTestLocal;
	FScopedGameWorld TestWorld;
	UDreamTextTestFont* Font = NewObject<UDreamTextTestFont>(TestWorld.World);

	FDreamTextLayoutInput In = MakeInput(Font, TEXT("The quick brown fox jumps over the lazy dog and keeps running"), 180.0f, 400.0f);
	In.OverflowType = EDreamUITextOverflowType::VerticalOverflow;
	In.WrappingPolicy = ETextWrappingPolicy::DefaultWrapping;
	FDreamTextDisplayList DL;
	FDreamTextLayoutEngine::Layout(In, DL);

	if (!TestTrue(TEXT("the text wrapped"), DL.Lines.Num() > 2))return false;
	for (int32 l = 0; l < DL.Lines.Num(); l++)
	{
		const FString Text = LineText(DL, l);
		if (Text.IsEmpty())continue;
		TestFalse(*FString::Printf(TEXT("line %d '%s' does not start with a space"), l, *Text), Text[0] == TEXT(' '));
		if (l + 1 < DL.Lines.Num())
		{
			// A soft break lands between words: the line ends with the space that separated them.
			TestTrue(*FString::Printf(TEXT("line %d '%s' ends at a word boundary"), l, *Text), Text[Text.Len() - 1] == TEXT(' '));
		}
		// Hanging spaces are not counted: the end caret is past the last glyph but the glyphs fit.
		TestTrue(*FString::Printf(TEXT("line %d glyphs fit the box"), l), LineRight(DL, l) <= BoxRight(In) + 0.5f);
	}
	// Every element still has a caret somewhere, spaces included.
	int32 Carets = 0;
	for (const auto& Line : DL.Lines)Carets += Line.CaretPropertyList.Num();
	TestEqual(TEXT("one caret per code unit plus one per line end"), Carets, In.Content.Len() + DL.Lines.Num());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextBreakerLongWordPolicyTest,
	"DreamGUI.Text.Breaker.ALongWordOverflowsOrBreaksPerPolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextBreakerLongWordPolicyTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextLayoutTestLocal;
	FScopedGameWorld TestWorld;
	UDreamTextTestFont* Font = NewObject<UDreamTextTestFont>(TestWorld.World);

	FDreamTextLayoutInput In = MakeInput(Font, TEXT("a Supercalifragilisticexpialidocious word"), 150.0f, 400.0f);
	In.OverflowType = EDreamUITextOverflowType::VerticalOverflow;

	In.WrappingPolicy = ETextWrappingPolicy::DefaultWrapping;
	FDreamTextDisplayList Whole;
	FDreamTextLayoutEngine::Layout(In, Whole);
	bool bOverflows = false;
	for (int32 l = 0; l < Whole.Lines.Num(); l++)
	{
		if (LineRight(Whole, l) > BoxRight(In) + 0.5f)bOverflows = true;
	}
	TestTrue(TEXT("DefaultWrapping never breaks inside a word, so the long one overflows"), bOverflows);
	TestTrue(TEXT("but the words around it still wrap"), Whole.Lines.Num() >= 2);

	In.WrappingPolicy = ETextWrappingPolicy::AllowPerCharacterWrapping;
	FDreamTextDisplayList Split;
	FDreamTextLayoutEngine::Layout(In, Split);
	for (int32 l = 0; l < Split.Lines.Num(); l++)
	{
		TestTrue(*FString::Printf(TEXT("AllowPerCharacterWrapping keeps line %d inside the box"), l), LineRight(Split, l) <= BoxRight(In) + 0.5f);
	}
	TestTrue(TEXT("so the long word was split"), Split.Lines.Num() > Whole.Lines.Num());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextBreakerKinsokuTest,
	"DreamGUI.Text.Breaker.CJKPunctuationNeverStartsALine",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextBreakerKinsokuTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextLayoutTestLocal;
	FScopedGameWorld TestWorld;
	UDreamTextTestFont* Font = NewObject<UDreamTextTestFont>(TestWorld.World);
	Font->bMockHasKerning = false;

	// Every width from "one glyph" up to the whole string: whatever the break, closing punctuation
	// stays glued to the character before it, and an opening bracket to the one after.
	const FString Content = TEXT("你好，世界。再见！「引用」结束");
	bool bSawMultipleLines = false;
	for (float Width = 20.0f; Width <= 400.0f; Width += 7.0f)
	{
		FDreamTextLayoutInput In = MakeInput(Font, Content, Width, 600.0f);
		In.OverflowType = EDreamUITextOverflowType::VerticalOverflow;
		FDreamTextDisplayList DL;
		FDreamTextLayoutEngine::Layout(In, DL);
		if (DL.Lines.Num() > 1)bSawMultipleLines = true;
		for (int32 l = 0; l < DL.Lines.Num(); l++)
		{
			const FString Text = LineText(DL, l);
			if (Text.IsEmpty())continue;
			const TCHAR First = Text[0];
			const TCHAR Last = Text[Text.Len() - 1];
			if (First == TEXT('，') || First == TEXT('。') || First == TEXT('！') || First == TEXT('」'))
			{
				AddError(FString::Printf(TEXT("width %.0f: line %d '%s' starts with closing punctuation"), Width, l, *Text));
			}
			if (Last == TEXT('「') && l + 1 < DL.Lines.Num())
			{
				AddError(FString::Printf(TEXT("width %.0f: line %d '%s' ends with an opening bracket"), Width, l, *Text));
			}
		}
	}
	TestTrue(TEXT("the sweep produced wrapped layouts"), bSawMultipleLines);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextBreakerPhraseWrapTest,
	"DreamGUI.Text.Breaker.CJKDictionaryKeepsWordsTogether",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextBreakerPhraseWrapTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextLayoutTestLocal;
	FScopedGameWorld TestWorld;
	UDreamTextTestFont* Font = NewObject<UDreamTextTestFont>(TestWorld.World);
	Font->bMockHasKerning = false;

	// 我 爱 北京 天安门: the dictionary knows 北京 and 天安门 as words.
	const FString Content = TEXT("我爱北京天安门");
	struct FWord { int32 Start; int32 End; };
	const FWord Words[] = { {2, 4}, {4, 7} };

	// Word widths, from the unwrapped layout: a word wider than the box may legitimately be cut.
	FDreamTextDisplayList Unwrapped;
	{
		FDreamTextLayoutInput In = MakeInput(Font, Content, 1000.0f, 600.0f);
		FDreamTextLayoutEngine::Layout(In, Unwrapped);
	}
	auto WordWidth = [&](const FWord& W)
	{
		const auto& First = Unwrapped.Items[W.Start];
		const auto& Last = Unwrapped.Items[W.End - 1];
		return (Last.Pen.X + Last.Glyph.XAdvance) - First.Pen.X;
	};

	int32 MidWordBreaksOff = 0;
	int32 MidWordBreaksDict = 0;
	bool bSawWrapsDict = false;
	for (float Width = 30.0f; Width <= 200.0f; Width += 5.0f)
	{
		for (int32 Mode = 0; Mode < 2; Mode++)
		{
			FDreamTextLayoutInput In = MakeInput(Font, Content, Width, 600.0f);
			In.OverflowType = EDreamUITextOverflowType::VerticalOverflow;
			In.PhraseWrap = Mode == 0 ? EDreamTextPhraseWrap::Off : EDreamTextPhraseWrap::CJKDictionary;
			FDreamTextDisplayList DL;
			FDreamTextLayoutEngine::Layout(In, DL);
			if (Mode == 1 && DL.Lines.Num() > 1)bSawWrapsDict = true;
			// A break "inside a word" is a line whose first element is strictly inside a word's range.
			for (int32 l = 1; l < DL.Lines.Num(); l++)
			{
				int32 FirstElement = -1;
				for (const auto& Item : DL.Items)
				{
					if (Item.LineIndex == l) { FirstElement = Item.ElementIndex; break; }
				}
				if (FirstElement < 0)continue;
				for (const FWord& W : Words)
				{
					if (FirstElement > W.Start && FirstElement < W.End)
					{
						if (Mode == 0)
						{
							MidWordBreaksOff++;
						}
						else if (WordWidth(W) <= Width + 0.5f)
						{
							MidWordBreaksDict++;
							AddInfo(FString::Printf(TEXT("width %.0f: dictionary mode broke '%s' | '%s' though the word is %.1f wide"), Width, *LineText(DL, l - 1), *LineText(DL, l), WordWidth(W)));
						}
					}
				}
			}
		}
	}
	TestTrue(TEXT("without the dictionary some widths break inside 北京 or 天安门"), MidWordBreaksOff > 0);
	TestTrue(TEXT("the dictionary sweep wrapped"), bSawWrapsDict);
	TestEqual(TEXT("with the dictionary no break lands inside a word unless the word cannot fit"), MidWordBreaksDict, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextBreakerCaretsOnSoftBreakTest,
	"DreamGUI.Text.Breaker.TrailingSpaceBelongsToTheLineItEnds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextBreakerCaretsOnSoftBreakTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextLayoutTestLocal;
	FScopedGameWorld TestWorld;
	UDreamTextTestFont* Font = NewObject<UDreamTextTestFont>(TestWorld.World);
	Font->bMockHasKerning = false;

	// Find a width where "ab cd" wraps after the space.
	FDreamTextLayoutInput In = MakeInput(Font, TEXT("ab cd"), 100.0f, 200.0f);
	In.OverflowType = EDreamUITextOverflowType::VerticalOverflow;
	FDreamTextDisplayList Whole;
	FDreamTextLayoutEngine::Layout(In, Whole);
	const float WholeWidth = Whole.PreferredSize.X;
	In.Width = WholeWidth * 0.7f;
	FDreamTextDisplayList DL;
	FDreamTextLayoutEngine::Layout(In, DL);

	if (!TestEqual(TEXT("two lines"), DL.Lines.Num(), 2))return false;
	TestEqual(TEXT("first line keeps its trailing space"), LineText(DL, 0), FString(TEXT("ab ")));
	TestEqual(TEXT("second line starts on the word"), LineText(DL, 1), FString(TEXT("cd")));
	TestEqual(TEXT("first line carets: a b space end"), DL.Lines[0].CaretPropertyList.Num(), 4);
	TestEqual(TEXT("soft break end caret is nameless"), DL.Lines[0].CaretPropertyList[3].CharIndex, -1);
	TestEqual(TEXT("second line carets: c d end"), DL.Lines[1].CaretPropertyList.Num(), 3);
	TestEqual(TEXT("the last caret names the end of the string"), DL.Lines[1].CaretPropertyList[2].CharIndex, 5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextBaselineTest,
	"DreamGUI.Text.Pipeline.MixedSizesShareABaseline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextBaselineTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextLayoutTestLocal;
	FScopedGameWorld TestWorld;
	UDreamTextTestFont* Font = NewObject<UDreamTextTestFont>(TestWorld.World);

	FDreamTextLayoutInput In = MakeInput(Font, TEXT("small <size=48>BIG</size> small"), 600.0f, 200.0f);
	In.bRichText = true;
	FDreamTextDisplayList DL;
	FDreamTextLayoutEngine::Layout(In, DL);

	// Every glyph on the line sits on the same baseline, whatever its size.
	TOptional<float> Baseline;
	for (const auto& Item : DL.Items)
	{
		if (!Item.bEmit)continue;
		if (!Baseline.IsSet())Baseline = Item.Pen.Y;
		else if (!FMath::IsNearlyEqual(Item.Pen.Y, Baseline.GetValue(), 0.001f))
		{
			AddError(FString::Printf(TEXT("glyph U+%04X at size %.0f sits at %.2f, baseline is %.2f"), Item.Codepoint, Item.Style.Size, Item.Pen.Y, Baseline.GetValue()));
			break;
		}
	}

	// The line is as tall as the biggest font box, and its baseline is the biggest ascent down from
	// the paragraph top (the box the mock describes has no leading: ascent + descent = line height).
	TestEqual(TEXT("one line"), DL.Lines.Num(), 1);
	TestEqual(TEXT("the paragraph is as tall as the 48pt box"), DL.PreferredSize.Y, 48.0f * 1.25f, 0.01f);
	const float Top = In.Height * (0.5f - In.Pivot.Y) + In.Height * 0.5f;//Top alignment: paragraph top on the box top
	TestEqual(TEXT("baseline sits one 48pt ascent below the top"), Baseline.Get(0.0f), Top - 48.0f * 0.95f, 0.01f);
	TestEqual(TEXT("carets anchor on the line centre"), DL.Lines[0].CaretPropertyList[0].CaretPosition.Y, Top - 48.0f * 1.25f * 0.5f, 0.01f);

	// Plain text, for comparison: the baseline is the font's ascent below the top.
	FDreamTextLayoutInput Plain = MakeInput(Font, TEXT("plain"), 600.0f, 200.0f);
	FDreamTextDisplayList PlainDL;
	FDreamTextLayoutEngine::Layout(Plain, PlainDL);
	TestEqual(TEXT("plain baseline"), PlainDL.Items[0].Pen.Y, Top - 24.0f * 0.95f, 0.01f);
	return true;
}

#endif
