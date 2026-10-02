// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIGeometry.h"
#include "Core/Text/DreamTextLayout.h"
#include "Core/Text/DreamTextPainter.h"
#include "Core/Text/DreamTextBreaker.h"
#include "Core/FRichTextParser.h"
#include "Core/DreamUIRichTextCustomStyleData.h"

#include <initializer_list>
#include "Engine/World.h"
#include "DreamTextTestFont.h"
#include "DreamScopedWorld.h"

/*
 * The text pipeline at the display-list level. These run the layout engine and the painter directly
 * against a font with made-up metrics, so they assert on structure -- lines, items, carets, tags,
 * what gets emitted -- rather than on pixel positions that would only restate the mock.
 */
namespace DreamTextLayoutTestLocal
{
	using DreamTests::FScopedGameWorld;

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
	FDreamTextCaretMoveWithoutLayoutTest,
	"DreamGUI.Text.Caret.MovingTheCaretOfATextThatWasNeverLaidOutMovesNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A text with no render canvas -- every text in a headless tree and in a Blueprint authoring tree -- is never
 * laid out and has no lines. MoveCaret read lines anyway: the last one for a move past the end, the caller's
 * line for up and down, the first and the last for Home and End, each an index into an empty array. Every
 * move here has to answer that nothing moved, and leave the caret it was handed alone.
 */
bool FDreamTextCaretMoveWithoutLayoutTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextLayoutTestLocal;
	FScopedGameWorld TestWorld;
	UDreamWidget* Widget = NewObject<UDreamWidget>(TestWorld.World, NAME_None, RF_Public | RF_Transactional);
	Widget->SetWidth(200.0f);
	Widget->SetHeight(40.0f);
	UDreamText* Text = Widget->CreateNewVisual<UDreamText>();
	if (!TestNotNull(TEXT("text visual"), Text))return false;
	Text->SetFont(NewObject<UDreamTextTestFont>(TestWorld.World));
	Text->SetText(FText::FromString(TEXT("never laid out")));
	if (!TestEqual(TEXT("with no render canvas the text has no lines"), Text->GetCacheTextGeometryData().GetLines().Num(), 0))return false;

	// 0 left, 1 right, 2 up, 3 down, 4 start, 5 end
	for (int32 MoveType = 0; MoveType <= 5; MoveType++)
	{
		int32 CaretIndex = 3;
		int32 LineIndex = 2;
		FVector2f CaretPosition(0.0f, 0.0f);
		TestFalse(*FString::Printf(TEXT("move %d reports that nothing moved"), MoveType),
			Text->MoveCaret(MoveType, CaretIndex, LineIndex, CaretPosition));
		TestEqual(*FString::Printf(TEXT("and move %d left the caret where it was"), MoveType), CaretIndex, 3);
		TestEqual(*FString::Printf(TEXT("on the line it was on before move %d"), MoveType), LineIndex, 2);
	}
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextFadingDoesNotCostALayoutTest,
	"DreamGUI.Text.Pipeline.FadingAColourDoesNotCostALayout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextFadingDoesNotCostALayoutTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextLayoutTestLocal;
	FScopedGameWorld TestWorld;
	UDreamTextTestFont* Font = NewObject<UDreamTextTestFont>(TestWorld.World);

	// GetFinalColor() folds the whole hierarchy's render opacity into the alpha, so a widget fading
	// anywhere above this text hands the cache a new colour on every single frame. None of it reaches
	// the display list -- untagged glyphs take their colour from the painter -- so none of it may cost
	// a rich-text parse, a shaping pass and an ICU line break.
	FDreamUITextGeometryCache Cache;
	FDreamTextLayoutInput In = MakeInput(Font,
		TEXT("a paragraph long enough that laying it out again every frame would be worth noticing"), 200.0f, 200.0f);
	In.OverflowType = EDreamUITextOverflowType::VerticalOverflow;
	Cache.SetLayoutInput(In);
	TestTrue(TEXT("the first layout runs"), Cache.EnsureLayout());
	const int32 AfterFirstLayout = Cache.GetLayoutRunCount();

	for (int32 Frame = 1; Frame <= 8; Frame++)
	{
		In.Color = FColor(255, 255, 255, (uint8)(255 - Frame * 16));
		TestFalse(TEXT("a colour change does not dirty the layout"), Cache.SetLayoutInput(In));
		Cache.EnsureLayout();
	}
	TestEqual(TEXT("so no layout ran for any frame of the fade"), Cache.GetLayoutRunCount(), AfterFirstLayout);

	// Rich text keeps <color> tags in the display list, but they are stored with the alpha the author
	// wrote and faded at paint time, so its layout is no more colour-sensitive than plain text.
	FDreamUITextGeometryCache RichCache;
	FDreamTextLayoutInput RichIn = MakeInput(Font, TEXT("plain <color=#00ff00>green</color> plain"), 400.0f, 200.0f);
	RichIn.bRichText = true;
	RichCache.SetLayoutInput(RichIn);
	TestTrue(TEXT("the rich text lays out once"), RichCache.EnsureLayout());
	const int32 RichAfterFirstLayout = RichCache.GetLayoutRunCount();
	RichIn.Color = FColor(255, 0, 0, 64);
	TestFalse(TEXT("and fading it does not dirty it either"), RichCache.SetLayoutInput(RichIn));
	TestEqual(TEXT("still one layout"), RichCache.GetLayoutRunCount(), RichAfterFirstLayout);

	// Not a dead comparison: anything that does change the glyphs still dirties it.
	RichIn.FontSize += 1.0f;
	TestTrue(TEXT("a font size change still dirties the layout"), RichCache.SetLayoutInput(RichIn));
	TestTrue(TEXT("and lays out again"), RichCache.EnsureLayout());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextTagColourFadesAtPaintTimeTest,
	"DreamGUI.Text.Pipeline.RichTextTagColoursFadeWithoutReParsing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextTagColourFadesAtPaintTimeTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextLayoutTestLocal;
	FScopedGameWorld TestWorld;
	UDreamTextTestFont* Font = NewObject<UDreamTextTestFont>(TestWorld.World);

	FDreamTextLayoutInput In = MakeInput(Font, TEXT("plain <color=#00ff00>green</color>"), 400.0f, 120.0f);
	In.bRichText = true;
	FDreamTextDisplayList DL;
	FDreamTextLayoutEngine::Layout(In, DL);

	// One display list, two fade values: the tagged glyphs dim with the untagged ones.
	FDreamTextPaintParams Opaque = MakePaint(FColor(255, 255, 255, 255));
	FDreamTextPaintParams Half = MakePaint(FColor(255, 255, 255, 128));
	Half.RichTextTagOpacity = 0.5f;

	FDreamUIGeometry A, B;
	TArray<FDreamUITextCharProperty> CharsA, CharsB;
	FDreamTextPainter::Paint(DL, Opaque, A, CharsA);
	FDreamTextPainter::Paint(DL, Half, B, CharsB);

	int32 FullGreen = 0, HalfGreen = 0;
	for (const auto& Vertex : A.Vertices)
	{
		if (Vertex.Color.R == 0 && Vertex.Color.G == 255 && Vertex.Color.A == 255)FullGreen++;
	}
	for (const auto& Vertex : B.Vertices)
	{
		if (Vertex.Color.R == 0 && Vertex.Color.G == 255 && Vertex.Color.A == 128)HalfGreen++;
	}
	TestEqual(TEXT("the tagged glyphs are opaque green at full opacity"), FullGreen, 5 * 4);
	TestEqual(TEXT("and half transparent green at half"), HalfGreen, 5 * 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextRasterCapDoesNotShrinkTheTextTest,
	"DreamGUI.Text.Pipeline.AFontSizePastTheRasterCapKeepsItsSizeUnderAScaledCanvas",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextRasterCapDoesNotShrinkTheTextTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextLayoutTestLocal;
	FScopedGameWorld TestWorld;
	UDreamTextTestFont* Font = NewObject<UDreamTextTestFont>(TestWorld.World);

	// A scaled canvas rasterizes at the device size and measures back in text units. The font caps the
	// size it will rasterize (200 here), and the measurement has to be divided by the ratio that was
	// actually achieved: dividing by the ratio that was ASKED for made a 60pt title inside a 4x canvas
	// come out at 50, with nothing said anywhere.
	FDreamTextLayoutInput Unscaled = MakeInput(Font, TEXT("Title"), 2000.0f, 400.0f);
	Unscaled.FontSize = 60.0f;
	FDreamTextDisplayList Reference;
	FDreamTextLayoutEngine::Layout(Unscaled, Reference);
	TestTrue(TEXT("the reference measures something"), Reference.PreferredSize.X > 0.0f);

	FDreamTextLayoutInput UnderCap = Unscaled;
	UnderCap.RootCanvasScale = 2.0f;//120, inside the cap
	FDreamTextDisplayList UnderCapDL;
	FDreamTextLayoutEngine::Layout(UnderCap, UnderCapDL);
	TestEqual(TEXT("a canvas scale inside the cap measures the same text"), UnderCapDL.PreferredSize.X, Reference.PreferredSize.X, 0.05f);

	FDreamTextLayoutInput OverCap = Unscaled;
	OverCap.RootCanvasScale = 4.0f;//240, past the cap
	FDreamTextDisplayList OverCapDL;
	FDreamTextLayoutEngine::Layout(OverCap, OverCapDL);
	TestEqual(TEXT("and so does one past it"), OverCapDL.PreferredSize.X, Reference.PreferredSize.X, 0.05f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextDoubledCharactersKernTest,
	"DreamGUI.Text.Pipeline.ARepeatedCharacterKernsAgainstItsNeighbour",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextDoubledCharactersKernTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextLayoutTestLocal;
	FScopedGameWorld TestWorld;
	UDreamTextTestFont* Font = NewObject<UDreamTextTestFont>(TestWorld.World);

	// "ll" used to lose its kerning entirely: the guard that stops the FIRST character from being
	// paired with itself was written as "the two characters differ", which is also true of every
	// doubled pair in the language.
	auto MeasureWidth = [&](const TCHAR* Content, bool bUseKerning)
	{
		FDreamTextLayoutInput In = MakeInput(Font, Content, 800.0f, 200.0f);
		In.bUseKerning = bUseKerning;
		FDreamTextDisplayList DL;
		FDreamTextLayoutEngine::Layout(In, DL);
		return DL.PreferredSize.X;
	};

	// The mock's kerning for both of these pairs is positive, so kerning them makes them wider.
	TestTrue(TEXT("a doubled pair kerns"), MeasureWidth(TEXT("ll"), true) > MeasureWidth(TEXT("ll"), false) + 0.01f);
	TestTrue(TEXT("and so does a mixed pair, as it always did"), MeasureWidth(TEXT("lx"), true) > MeasureWidth(TEXT("lx"), false) + 0.01f);
	// One character has no left neighbour, so kerning cannot change it either way.
	TestEqual(TEXT("a single character is not kerned against anything"), MeasureWidth(TEXT("l"), true), MeasureWidth(TEXT("l"), false), 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextExpandMeshSizeIsAPaintInputTest,
	"DreamGUI.Text.Pipeline.ExpandMeshSizeComesFromTheAskingTextNotTheSharedFont",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextExpandMeshSizeIsAPaintInputTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextLayoutTestLocal;
	FScopedGameWorld TestWorld;
	UDreamTextTestFont* Font = NewObject<UDreamTextTestFont>(TestWorld.World);

	const FDreamTextGlyphPaintStyle NoExpand = Font->GetGlyphPaintStyle(FVector2f(1.0f, 1.0f), 0.0f);
	const FDreamTextGlyphPaintStyle Expanded = Font->GetGlyphPaintStyle(FVector2f(1.0f, 1.0f), 4.0f);
	TestTrue(TEXT("the expand size the caller passes is what widens the quad margin"),
		Expanded.QuadMarginTexels > NoExpand.QuadMarginTexels);

	// A font asset is shared, and paint does not run in step with layout: a text whose layout is still
	// clean used to paint with whatever expand size the LAST text to lay out had left on the font.
	Font->PrepareForLayout(16.0f);
	const FDreamTextGlyphPaintStyle StillNoExpand = Font->GetGlyphPaintStyle(FVector2f(1.0f, 1.0f), 0.0f);
	TestEqual(TEXT("another text laying out does not reach this one's paint"),
		StillNoExpand.QuadMarginTexels, NoExpand.QuadMarginTexels, 0.0001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextPendingGlyphKeepsItsIndexTest,
	"DreamGUI.Text.Pipeline.AGlyphStillOnTheRasterizerKeepsItsCharacterIndex",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextPendingGlyphKeepsItsIndexTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextLayoutTestLocal;
	FScopedGameWorld TestWorld;
	UDreamTextTestFont* Font = NewObject<UDreamTextTestFont>(TestWorld.World);

	FDreamTextDisplayList DL;
	FDreamTextLayoutEngine::Layout(MakeInput(Font, TEXT("abc"), 600.0f, 200.0f), DL);
	if (!TestEqual(TEXT("three items"), DL.Items.Num(), 3))return false;
	TestEqual(TEXT("three characters"), DL.VisibleCharCount, 3);

	// Exactly the state the layout leaves a glyph in while the font rasterizes it on a worker: it
	// counts as a character, it has no quad yet. TextAnimation addresses characters by index, so if
	// the entry were missing every character after it would shift the moment OnGlyphsReady landed.
	DL.Items[1].bEmit = false;

	FDreamUIGeometry Geometry;
	TArray<FDreamUITextCharProperty> Chars;
	FDreamTextPainter::Paint(DL, MakePaint(), Geometry, Chars);
	TestEqual(TEXT("the glyph that has not landed still has a character entry"), Chars.Num(), 3);
	TestEqual(TEXT("with no vertices of its own"), Chars[1].VertCount, 0);
	TestEqual(TEXT("and the character after it keeps its index"), Chars[2].CharIndex, 2);
	TestEqual(TEXT("the display list counts the characters the painter writes"), DL.VisibleCharCount, Chars.Num());
	TestEqual(TEXT("and only the landed glyphs have quads"), Geometry.OriginVertices.Num(), 2 * 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextBreakerNoIcuFallbackTest,
	"DreamGUI.Text.Breaker.TheNoIcuFallbackBreaksCjkAndKeepsKinsoku",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextBreakerNoIcuFallbackTest::RunTest(const FString& Parameters)
{
	// A build without ICU gets the engine's legacy iterator, which breaks on whitespace and nothing
	// else -- so a script that does not write spaces never wrapped at all. This is the replacement,
	// asserted directly because it is a pure function of the code points and the editor has ICU.
	auto Breaks = [this](std::initializer_list<uint32> Codepoints)
	{
		TArray<uint32> Elements(Codepoints);
		TBitArray<> CanBreak;
		FDreamTextBreaker::ComputeFallbackBreakOpportunities(Elements, CanBreak);
		TArray<int32> Result;
		for (int32 i = 0; i < Elements.Num(); i++)
		{
			if (CanBreak[i])Result.Add(i);
		}
		return Result;
	};

	// English: after the space, and nowhere inside a word.
	{
		const TArray<int32> Points = Breaks({ 'a', 'b', ' ', 'c', 'd' });
		TestEqual(TEXT("one break in \"ab cd\""), Points.Num(), 1);
		if (Points.Num() == 1)TestEqual(TEXT("which is the c"), Points[0], 3);
	}
	// A line never starts at the very first element, whatever it is.
	{
		const TArray<int32> Points = Breaks({ 0x4E2D, 0x6587 });
		TestEqual(TEXT("two ideographs break once, between them"), Points.Num(), 1);
		if (Points.Num() == 1)TestEqual(TEXT("before the second"), Points[0], 1);
	}
	// Kinsoku: a closing mark never starts a line, an opening bracket never ends one.
	{
		const TArray<int32> Points = Breaks({ 0x4E2D, 0x3002, 0x6587 });//中。文
		TestEqual(TEXT("one break around the full stop"), Points.Num(), 1);
		if (Points.Num() == 1)TestEqual(TEXT("after it, never before it"), Points[0], 2);
	}
	{
		const TArray<int32> Points = Breaks({ 0x4E2D, 0x300C, 0x6587 });//中「文
		TestEqual(TEXT("one break around the opening bracket"), Points.Num(), 1);
		if (Points.Num() == 1)TestEqual(TEXT("before it, never after it"), Points[0], 1);
	}
	// A hyphen breaks after itself, unless it is a minus between digits.
	{
		const TArray<int32> Points = Breaks({ 'e', '-', 'm', 'a', 'i', 'l' });
		TestEqual(TEXT("one break after the hyphen"), Points.Num(), 1);
		if (Points.Num() == 1)TestEqual(TEXT("at the m"), Points[0], 2);
	}
	{
		const TArray<int32> Points = Breaks({ '3', '-', '4' });
		TestEqual(TEXT("a range of numbers does not break"), Points.Num(), 0);
	}
	// Breaking BEFORE a space is pointless; the layout hangs trailing spaces outside the line anyway.
	{
		const TArray<int32> Points = Breaks({ 0x4E2D, ' ', 0x6587 });
		TestEqual(TEXT("one break, not two"), Points.Num(), 1);
		if (Points.Num() == 1)TestEqual(TEXT("after the space"), Points[0], 2);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextEscapeTest,
	"DreamGUI.Text.RichText.ACharacterReferenceWritesWhatTheMarkupWouldEat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextEscapeTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextLayoutTestLocal;
	FScopedGameWorld TestWorld;
	UDreamTextTestFont* Font = NewObject<UDreamTextTestFont>(TestWorld.World);

	// "&lt;b&gt;" is five characters that mean "<b>", not a tag and not nine characters. There was no
	// way at all to show a literal '<' before: the only escape was to misspell the tag.
	FDreamTextLayoutInput In = MakeInput(Font, TEXT("&lt;b&gt;"), 600.0f, 200.0f);
	In.bRichText = true;
	FDreamTextDisplayList DL;
	FDreamTextLayoutEngine::Layout(In, DL);
	TestEqual(TEXT("three characters"), DL.VisibleCharCount, 3);
	if (DL.Items.Num() >= 3)
	{
		TestEqual(TEXT("the first is a less-than"), (int32)DL.Items[0].Codepoint, (int32)'<');
		TestEqual(TEXT("then the b"), (int32)DL.Items[1].Codepoint, (int32)'b');
		TestEqual(TEXT("then the greater-than"), (int32)DL.Items[2].Codepoint, (int32)'>');
		// The caret still names where the reference starts in the SOURCE, so an editor walks the markup.
		TestEqual(TEXT("the second character starts at source index 4"), DL.Items[1].SourceIndex, 4);
	}
	// And the tag it spells out is not parsed: bold is off.
	for (const auto& Item : DL.Items)
	{
		if (Item.Style.bBold)
		{
			AddError(TEXT("an escaped tag was parsed as a tag"));
			break;
		}
	}

	// Numeric references, and the round trip through EscapeText.
	FDreamTextLayoutInput Numeric = MakeInput(Font, TEXT("&#65;&#x42;"), 600.0f, 200.0f);
	Numeric.bRichText = true;
	FDreamTextDisplayList NumericDL;
	FDreamTextLayoutEngine::Layout(Numeric, NumericDL);
	if (TestEqual(TEXT("two characters"), NumericDL.Items.Num(), 2))
	{
		TestEqual(TEXT("decimal 65 is A"), (int32)NumericDL.Items[0].Codepoint, (int32)'A');
		TestEqual(TEXT("hex 42 is B"), (int32)NumericDL.Items[1].Codepoint, (int32)'B');
	}
	TestEqual(TEXT("EscapeText is the inverse"), DreamUIRichTextParser::FRichTextParser::EscapeText(TEXT("a<b>&c")), FString(TEXT("a&lt;b&gt;&amp;c")));

	// A plain text draws every character as itself, as UMG's TextBlock does.
	FDreamTextLayoutInput Plain = MakeInput(Font, TEXT("&lt;"), 600.0f, 200.0f);
	FDreamTextDisplayList PlainDL;
	FDreamTextLayoutEngine::Layout(Plain, PlainDL);
	TestEqual(TEXT("plain text keeps all four characters"), PlainDL.Items.Num(), 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextTagRangeMatchesCharPropertiesTest,
	"DreamGUI.Text.RichText.ATagRangeIndexesTheCharactersThePainterWrote",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextTagRangeMatchesCharPropertiesTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextLayoutTestLocal;
	FScopedGameWorld TestWorld;
	UDreamTextTestFont* Font = NewObject<UDreamTextTestFont>(TestWorld.World);

	// A custom tag's range and the painter's character list used to be two different counts. They
	// agreed until something was thrown away -- and a truncated line throws characters away.
	FDreamTextLayoutInput In = MakeInput(Font, TEXT("ab<Tag>cd</Tag>efghijklmnopqrstuvwxyz"), 600.0f, 200.0f);
	In.bRichText = true;
	FDreamTextDisplayList Wide;
	FDreamTextLayoutEngine::Layout(In, Wide);
	if (!TestEqual(TEXT("one tag"), Wide.CustomTags.Num(), 1))return false;
	TestEqual(TEXT("the tag starts at the third character"), Wide.CustomTags[0].CharIndexStart, 2);
	TestEqual(TEXT("and ends at the fourth"), Wide.CustomTags[0].CharIndexEnd, 3);

	FDreamUIGeometry Geometry;
	TArray<FDreamUITextCharProperty> Chars;
	FDreamTextPainter::Paint(Wide, MakePaint(), Geometry, Chars);
	TestEqual(TEXT("the display list counts what the painter wrote"), Wide.VisibleCharCount, Chars.Num());
	if (Chars.IsValidIndex(Wide.CustomTags[0].CharIndexStart))
	{
		// Visible character 2 of "abcdef..." is the 'c', element 2, which is where <Tag> opens.
		TestEqual(TEXT("and the range names the right character"), Chars[Wide.CustomTags[0].CharIndexStart].CharIndex, 2);
	}

	// Now a box too narrow for the whole string: whatever the clamp removed, the range still points
	// inside the list, which is what TextAnimation indexes with.
	FDreamTextLayoutInput Narrow = In;
	Narrow.Width = 120.0f;
	Narrow.OverflowType = EDreamUITextOverflowType::Truncate;
	FDreamTextDisplayList Cut;
	FDreamTextLayoutEngine::Layout(Narrow, Cut);
	TestTrue(TEXT("the narrow box truncates"), Cut.bTruncated);
	FDreamUIGeometry CutGeometry;
	TArray<FDreamUITextCharProperty> CutChars;
	FDreamTextPainter::Paint(Cut, MakePaint(), CutGeometry, CutChars);
	TestEqual(TEXT("the counts still agree after a clamp"), Cut.VisibleCharCount, CutChars.Num());
	if (TestEqual(TEXT("still one tag"), Cut.CustomTags.Num(), 1))
	{
		TestTrue(TEXT("the tag range starts inside the character list"),
			CutChars.IsValidIndex(Cut.CustomTags[0].CharIndexStart));
		TestTrue(TEXT("and ends inside it too"),
			CutChars.IsValidIndex(Cut.CustomTags[0].CharIndexEnd));
		TestTrue(TEXT("and is not inside out"),
			Cut.CustomTags[0].CharIndexEnd >= Cut.CustomTags[0].CharIndexStart);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextHyperlinkTagTest,
	"DreamGUI.Text.RichText.AnAnchorTagIsAClickableNamedRange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextHyperlinkTagTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextLayoutTestLocal;
	FScopedGameWorld TestWorld;
	UDreamTextTestFont* Font = NewObject<UDreamTextTestFont>(TestWorld.World);

	FDreamTextLayoutInput In = MakeInput(Font, TEXT("go <a=Buy>here</a> now"), 900.0f, 200.0f);
	In.bRichText = true;
	FDreamTextDisplayList DL;
	FDreamTextLayoutEngine::Layout(In, DL);
	if (!TestEqual(TEXT("one tag"), DL.CustomTags.Num(), 1))return false;
	TestEqual(TEXT("named by its id"), DL.CustomTags[0].TagName, FName(TEXT("Buy")));
	TestTrue(TEXT("and marked as a hyperlink"), DL.CustomTags[0].bHyperlink);
	// "go here now" without the spaces is g,o,h,e,r,e,n,o,w; the link covers h,e,r,e = 2..5.
	TestEqual(TEXT("the link starts at the h"), DL.CustomTags[0].CharIndexStart, 2);
	TestEqual(TEXT("and ends at the second e"), DL.CustomTags[0].CharIndexEnd, 5);

	// A plain custom tag is not a link, which is what tells the two apart at the hit test.
	FDreamTextLayoutInput Plain = MakeInput(Font, TEXT("go <Buy>here</Buy> now"), 900.0f, 200.0f);
	Plain.bRichText = true;
	FDreamTextDisplayList PlainDL;
	FDreamTextLayoutEngine::Layout(Plain, PlainDL);
	if (TestEqual(TEXT("still one tag"), PlainDL.CustomTags.Num(), 1))
	{
		TestFalse(TEXT("but not a hyperlink"), PlainDL.CustomTags[0].bHyperlink);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextImageSizeTagTest,
	"DreamGUI.Text.RichText.AnImageTagCanSayHowBigItIs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextImageSizeTagTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextLayoutTestLocal;
	FScopedGameWorld TestWorld;
	UDreamTextTestFont* Font = NewObject<UDreamTextTestFont>(TestWorld.World);

	auto ImageQuad = [&](const TCHAR* Content, FVector2f& OutSize) -> bool
	{
		FDreamTextLayoutInput In = MakeInput(Font, Content, 900.0f, 300.0f);
		In.bRichText = true;
		In.FontSize = 20.0f;
		FDreamTextDisplayList DL;
		FDreamTextLayoutEngine::Layout(In, DL);
		for (const auto& Item : DL.Items)
		{
			if (Item.Kind == EDreamTextItemKind::Image)
			{
				OutSize = FVector2f(Item.Glyph.Width, Item.Glyph.Height);
				return true;
			}
		}
		return false;
	};

	// No size: the image is as tall as the font, which is what it always was.
	FVector2f Default(0.0f, 0.0f);
	if (TestTrue(TEXT("<img=x/> places an image"), ImageQuad(TEXT("<img=x/>"), Default)))
	{
		TestEqual(TEXT("as tall as the font size"), Default.Y, 20.0f, 0.01f);
	}
	// One size is the height; the width follows, and with no image data behind it that is square.
	FVector2f Sized(0.0f, 0.0f);
	if (TestTrue(TEXT("<img=x,48/> places an image"), ImageQuad(TEXT("<img=x,48/>"), Sized)))
	{
		TestEqual(TEXT("as tall as it asked"), Sized.Y, 48.0f, 0.01f);
		TestEqual(TEXT("and as wide"), Sized.X, 48.0f, 0.01f);
	}
	// Two sizes set both.
	FVector2f Both(0.0f, 0.0f);
	if (TestTrue(TEXT("<img=x,30,60/> places an image"), ImageQuad(TEXT("<img=x,30,60/>"), Both)))
	{
		TestEqual(TEXT("the width is the first"), Both.X, 30.0f, 0.01f);
		TestEqual(TEXT("the height the second"), Both.Y, 60.0f, 0.01f);
	}
	// A malformed size is not a size: the tag fails to parse and renders as literal text, which is
	// the markup's one consistent rule about anything it does not understand.
	FVector2f Bad(0.0f, 0.0f);
	TestFalse(TEXT("<img=x,nope/> is not an image tag"), ImageQuad(TEXT("<img=x,nope/>"), Bad));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextComponentStyleTest,
	"DreamGUI.Text.Pipeline.TheTextsOwnUnderlineIsWhereMarkupStartsFrom",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextComponentStyleTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextLayoutTestLocal;
	FScopedGameWorld TestWorld;
	UDreamTextTestFont* Font = NewObject<UDreamTextTestFont>(TestWorld.World);

	// A component-level underline applies to plain text, which markup could never reach.
	FDreamTextLayoutInput In = MakeInput(Font, TEXT("abc"), 600.0f, 200.0f);
	In.bUnderline = true;
	In.bStrikethrough = true;
	FDreamTextDisplayList DL;
	FDreamTextLayoutEngine::Layout(In, DL);
	for (const auto& Item : DL.Items)
	{
		if (!Item.bEmit)continue;
		TestTrue(TEXT("every glyph is underlined"), Item.Style.bUnderline);
		TestTrue(TEXT("and struck through"), Item.Style.bStrikethrough);
	}
	// And in rich text, `<u>` nests on top of it rather than replacing it: a text that is underlined
	// stays underlined outside the tag.
	FDreamTextLayoutInput Rich = MakeInput(Font, TEXT("a<u>b</u>c"), 600.0f, 200.0f);
	Rich.bRichText = true;
	Rich.bUnderline = true;
	FDreamTextDisplayList RichDL;
	FDreamTextLayoutEngine::Layout(Rich, RichDL);
	for (const auto& Item : RichDL.Items)
	{
		if (!Item.bEmit)continue;
		TestTrue(TEXT("the character after </u> is still underlined"), Item.Style.bUnderline);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextMultiLineEllipsisTest,
	"DreamGUI.Text.Pipeline.AWrappedEllipsisElidesTheLastLineThatFits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextMultiLineEllipsisTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextLayoutTestLocal;
	FScopedGameWorld TestWorld;
	UDreamTextTestFont* Font = NewObject<UDreamTextTestFont>(TestWorld.World);
	const FString Content = TEXT("one two three four five six seven eight nine ten eleven twelve thirteen");

	// Ellipsis on its own is a one-line feature: it does not wrap, so it cuts at the right edge.
	FDreamTextLayoutInput Single = MakeInput(Font, Content, 200.0f, 200.0f);
	Single.OverflowType = EDreamUITextOverflowType::Ellipsis;
	FDreamTextDisplayList SingleDL;
	FDreamTextLayoutEngine::Layout(Single, SingleDL);
	TestEqual(TEXT("one line without wrapping"), SingleDL.Lines.Num(), 1);

	// With AutoWrapText the same policy wraps and elides the last line that fits the BOX, which is
	// what a UMG text with AutoWrapText and an Ellipsis overflow policy does.
	FDreamTextLayoutInput Wrapped = Single;
	Wrapped.bAutoWrapText = true;
	// 24pt text at 1.25 line height is 30 per line: three lines fit in 100.
	Wrapped.Height = 100.0f;
	FDreamTextDisplayList WrappedDL;
	FDreamTextLayoutEngine::Layout(Wrapped, WrappedDL);
	TestTrue(TEXT("it wraps into more than one line"), WrappedDL.Lines.Num() > 1);
	TestTrue(TEXT("but not more than fit the box"), WrappedDL.Lines.Num() <= 4);
	TestTrue(TEXT("and it says it truncated"), WrappedDL.bTruncated);
	const FDreamTextGlyphItem* Last = nullptr;
	for (int32 i = WrappedDL.Items.Num() - 1; i >= 0; i--)
	{
		if (WrappedDL.Items[i].bEmit) { Last = &WrappedDL.Items[i]; break; }
	}
	if (TestNotNull(TEXT("something is drawn"), Last))
	{
		TestEqual(TEXT("the last thing drawn is the ellipsis"), (int32)Last->Codepoint, 0x2026);
		TestTrue(TEXT("which sits inside the box"), ItemRight(*Last) <= BoxRight(Wrapped) + 0.5f);
	}
	// A box tall enough for the whole paragraph is not elided at all.
	FDreamTextLayoutInput Tall = Wrapped;
	Tall.Height = 4000.0f;
	FDreamTextDisplayList TallDL;
	FDreamTextLayoutEngine::Layout(Tall, TallDL);
	TestFalse(TEXT("a box that fits is not truncated"), TallDL.bTruncated);
	TestTrue(TEXT("and keeps more lines"), TallDL.Lines.Num() > WrappedDL.Lines.Num());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextMinDesiredWidthTest,
	"DreamGUI.Text.Pipeline.MinDesiredWidthIsAFloorUnderWhatTheParentMeasures",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextMinDesiredWidthTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextLayoutTestLocal;
	FScopedGameWorld TestWorld;

	// A preferred width needs a laid-out text, and a layout needs a render canvas: the canvas is where
	// the root scale and the world-space flag come from.
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

	Text->SetText(FText::FromString(TEXT("x")));
	const float Natural = Text->GetPreferredWidth();
	if (!TestTrue(TEXT("a laid-out text has a preferred width"), Natural > 0.0f))return false;

	// A label that is momentarily short must not collapse the row around it, which is what UMG's
	// MinDesiredWidth is for. It is a floor, not a size: a longer text still asks for more.
	Text->SetMinDesiredWidth(Natural + 100.0f);
	TestEqual(TEXT("the floor is what the parent measures"), Text->GetPreferredWidth(), Natural + 100.0f, 0.01f);
	Text->SetMinDesiredWidth(1.0f);
	TestEqual(TEXT("a floor under the text changes nothing"), Text->GetPreferredWidth(), Natural, 0.01f);
	// The height is never floored by it, which is the difference between a floor and a size.
	TestTrue(TEXT("the height is its own"), Text->GetPreferredHeight() > 0.0f);

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextBitmapShadowAndOutlineTest,
	"DreamGUI.Text.Painter.ABitmapFontDrawsItsShadowAndOutlineAsOffsetCopies",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextBitmapShadowAndOutlineTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextLayoutTestLocal;
	FScopedGameWorld TestWorld;
	UDreamTextTestFont* Font = NewObject<UDreamTextTestFont>(TestWorld.World);

	FDreamTextDisplayList DL;
	FDreamTextLayoutEngine::Layout(MakeInput(Font, TEXT("ab"), 600.0f, 200.0f), DL);

	FDreamUIGeometry Plain;
	TArray<FDreamUITextCharProperty> PlainChars;
	FDreamTextPainter::Paint(DL, MakePaint(), Plain, PlainChars);
	const int32 PlainVertices = Plain.OriginVertices.Num();
	TestEqual(TEXT("two glyphs, four vertices each"), PlainVertices, 2 * 4);

	// A bitmap atlas holds the face and nothing else, so a shadow is the glyphs drawn again, offset.
	FDreamTextPaintParams WithShadow = MakePaint(FColor::White);
	WithShadow.BitmapShadowColor = FColor(0, 0, 0, 255);
	WithShadow.BitmapShadowOffsetEm = FVector2f(0.1f, 0.1f);
	FDreamUIGeometry Shadowed;
	TArray<FDreamUITextCharProperty> ShadowedChars;
	FDreamTextPainter::Paint(DL, WithShadow, Shadowed, ShadowedChars);
	TestEqual(TEXT("a shadow doubles the quads"), Shadowed.OriginVertices.Num(), PlainVertices * 2);
	TestEqual(TEXT("and changes nothing about the characters"), ShadowedChars.Num(), PlainChars.Num());
	int32 Black = 0;
	for (const auto& Vertex : Shadowed.Vertices)
	{
		if (Vertex.Color.R == 0 && Vertex.Color.G == 0 && Vertex.Color.B == 0)Black++;
	}
	TestEqual(TEXT("half the vertices are the shadow's"), Black, PlainVertices);
	// A character's vertices are its shadow copy, then its face; the shadow is BEHIND the face because its triangles
	// are drawn first. The copy is offset from the face it belongs to.
	if (Shadowed.OriginVertices.Num() == PlainVertices * 2 && ShadowedChars.Num() > 0)
	{
		const FDreamUITextCharProperty& First = ShadowedChars[0];
		const int32 FaceVertex = First.StartVertIndex + First.VertCount - 4;
		TestTrue(TEXT("and it is offset from the face"),
			!FMath::IsNearlyEqual(Shadowed.OriginVertices[First.StartVertIndex].Position.Y, Shadowed.OriginVertices[FaceVertex].Position.Y, 0.001f));
	}

	// An outline is eight taps around the glyph, the standard stand-in for a real one.
	FDreamTextPaintParams WithOutline = MakePaint(FColor::White);
	WithOutline.BitmapOutlineColor = FColor(255, 0, 0, 255);
	WithOutline.BitmapOutlineWidthEm = 0.05f;
	FDreamUIGeometry Outlined;
	TArray<FDreamUITextCharProperty> OutlinedChars;
	FDreamTextPainter::Paint(DL, WithOutline, Outlined, OutlinedChars);
	TestEqual(TEXT("an outline is eight more copies"), Outlined.OriginVertices.Num(), PlainVertices * 9);
	TestEqual(TEXT("still two characters"), OutlinedChars.Num(), PlainChars.Num());

	// A distance-field font draws the real thing in the shader, so it ignores all of this.
	FDreamTextPaintParams Field = WithOutline;
	Field.bDistanceField = true;
	FDreamUIGeometry FieldGeometry;
	TArray<FDreamUITextCharProperty> FieldChars;
	FDreamTextPainter::Paint(DL, Field, FieldGeometry, FieldChars);
	TestEqual(TEXT("a distance-field font draws one copy"), FieldGeometry.OriginVertices.Num(), PlainVertices);
	return true;
}

namespace DreamTextLayoutTestLocal
{
	/** The emitted item of an element, or null. */
	const FDreamTextGlyphItem* FindItem(const FDreamTextDisplayList& DL, int32 ElementIndex)
	{
		for (const FDreamTextGlyphItem& Item : DL.Items)
		{
			if (Item.ElementIndex == ElementIndex && Item.Kind == EDreamTextItemKind::Glyph)return &Item;
		}
		return nullptr;
	}

	/** Which pass of a bitmap font's painting a vertex colour belongs to: shadow (black), outline (red), face (white). */
	int32 PassOfColour(const FColor& Colour)
	{
		if (Colour.R == 0 && Colour.G == 0 && Colour.B == 0)return 0;
		if (Colour.R == 255 && Colour.G == 0 && Colour.B == 0)return 1;
		return 2;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextBitmapPassesTest,
	"DreamGUI.Text.Painter.EveryBitmapShadowAndOutlineIsDrawnBeforeAnyFace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A bitmap font's shadow and outline were written per glyph -- shadow, eight outline copies, face -- so the next glyph's
 * outline was drawn over this glyph's face wherever the two met: any negative letter spacing, or a tight pair with an
 * outline. Slate draws them as whole-run passes. The index buffer has to reference every shadow quad first, then every
 * outline quad, then every face; and a character's vertex range still covers all of its copies, its triangle range its face.
 */
bool FDreamTextBitmapPassesTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextLayoutTestLocal;
	FScopedGameWorld TestWorld;
	UDreamTextTestFont* Font = NewObject<UDreamTextTestFont>(TestWorld.World);

	FDreamTextLayoutInput In = MakeInput(Font, TEXT("abc"), 600.0f, 200.0f);
	In.FontSpace.X = -6.0f;
	FDreamTextDisplayList DL;
	FDreamTextLayoutEngine::Layout(In, DL);

	FDreamTextPaintParams Params = MakePaint(FColor::White);
	Params.BitmapShadowColor = FColor(0, 0, 0, 255);
	Params.BitmapShadowOffsetEm = FVector2f(0.1f, 0.1f);
	Params.BitmapOutlineColor = FColor(255, 0, 0, 255);
	Params.BitmapOutlineWidthEm = 0.05f;
	FDreamUIGeometry Geometry;
	TArray<FDreamUITextCharProperty> Chars;
	FDreamTextPainter::Paint(DL, Params, Geometry, Chars);
	if (!TestEqual(TEXT("three characters"), Chars.Num(), 3))return false;
	TestEqual(TEXT("a shadow, eight outline taps and a face per glyph"), Geometry.OriginVertices.Num(), 3 * 10 * 4);

	int32 PreviousPass = 0;
	bool bInOrder = true;
	for (int32 Index = 0; Index < Geometry.Triangles.Num(); Index++)
	{
		const int32 Pass = PassOfColour(Geometry.Vertices[Geometry.Triangles[Index]].Color);
		if (Pass < PreviousPass)
		{
			bInOrder = false;
			AddError(FString::Printf(TEXT("index %d draws pass %d after pass %d"), Index, Pass, PreviousPass));
			break;
		}
		PreviousPass = Pass;
	}
	TestTrue(TEXT("every shadow triangle precedes every outline triangle, and every outline triangle every face triangle"), bInOrder);
	for (int32 i = 0; i < Chars.Num(); i++)
	{
		TestEqual(*FString::Printf(TEXT("character %d covers all ten of its quads"), i), Chars[i].VertCount, 10 * 4);
		TestEqual(*FString::Printf(TEXT("character %d's triangles are its face's"), i), Chars[i].IndicesCount, 6);
		TestEqual(*FString::Printf(TEXT("and character %d's sit in the face block"), i),
			PassOfColour(Geometry.Vertices[Geometry.Triangles[Chars[i].StartTriangleIndex]].Color), 2);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextAdjacentTagsTest,
	"DreamGUI.Text.RichText.OneCharacterAdjacentAndRepeatedTagsEachKeepTheirOwnRange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A tag's start and its end were one mark on an element. A tag around one character had its start overwritten by its
 * end and vanished; a closing tag followed straight by an opening one lost its end to the start, so the first tag never
 * closed and ran to the end of the text (and a click on the next link reported the first); and an end tag closed the
 * first tag of its name, so a name used twice left the second one open. Every tag keeps exactly its own characters.
 */
bool FDreamTextAdjacentTagsTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextLayoutTestLocal;
	FScopedGameWorld TestWorld;
	UDreamTextTestFont* Font = NewObject<UDreamTextTestFont>(TestWorld.World);

	FDreamTextLayoutInput In = MakeInput(Font, TEXT("<a=x>1</a> and <a=y>22</a><a=z>3</a>"), 900.0f, 200.0f);
	In.bRichText = true;
	FDreamTextDisplayList DL;
	FDreamTextLayoutEngine::Layout(In, DL);
	// The visible characters are 1, a, n, d, 2, 2, 3.
	if (!TestEqual(TEXT("three links"), DL.CustomTags.Num(), 3))return false;
	struct FExpectedTag { const TCHAR* Name; int32 Start; int32 End; };
	const FExpectedTag Expected[] = { { TEXT("x"), 0, 0 }, { TEXT("y"), 4, 5 }, { TEXT("z"), 6, 6 } };
	for (int32 i = 0; i < 3; i++)
	{
		const FDreamUIText_RichTextCustomTag& Tag = DL.CustomTags[i];
		TestEqual(*FString::Printf(TEXT("link %d is %s"), i, Expected[i].Name), Tag.TagName, FName(Expected[i].Name));
		TestTrue(*FString::Printf(TEXT("link %d is a hyperlink"), i), Tag.bHyperlink);
		TestEqual(*FString::Printf(TEXT("link %s starts at its first character"), Expected[i].Name), Tag.CharIndexStart, Expected[i].Start);
		TestEqual(*FString::Printf(TEXT("link %s ends at its last character"), Expected[i].Name), Tag.CharIndexEnd, Expected[i].End);
	}

	// The parser takes every one-letter tag for one of its own (b, i, u, s), so a custom tag needs a longer name.
	FDreamTextLayoutInput Repeated = MakeInput(Font, TEXT("<kw>a</kw> <kw>b</kw>"), 900.0f, 200.0f);
	Repeated.bRichText = true;
	FDreamTextDisplayList RepeatedDL;
	FDreamTextLayoutEngine::Layout(Repeated, RepeatedDL);
	if (TestEqual(TEXT("a name used twice is two tags"), RepeatedDL.CustomTags.Num(), 2))
	{
		TestEqual(TEXT("the first covers a"), RepeatedDL.CustomTags[0].CharIndexStart, 0);
		TestEqual(TEXT("and only a"), RepeatedDL.CustomTags[0].CharIndexEnd, 0);
		TestEqual(TEXT("the second covers b"), RepeatedDL.CustomTags[1].CharIndexStart, 1);
		TestEqual(TEXT("and closes at b"), RepeatedDL.CustomTags[1].CharIndexEnd, 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextCustomStyleEveryCharacterTest,
	"DreamGUI.Text.RichText.ACustomStyleColoursEveryCharacterOfItsTagNestedTagsIncluded",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A custom style's Replace colour set the colour without saying there was one, so the painter drew the text's own colour
 * instead; and the style reached only the character right after its tag -- the next tag the parser met, a nested <b>,
 * rebuilt the state without it. Every character of the tag takes the style, a tag nested inside it still has the last
 * word on what it sets, and a Multiply style multiplies the text's colour when the glyph is painted.
 */
bool FDreamTextCustomStyleEveryCharacterTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextLayoutTestLocal;
	FScopedGameWorld TestWorld;
	UDreamTextTestFont* Font = NewObject<UDreamTextTestFont>(TestWorld.World);
	UDreamUIRichTextCustomStyleData* Styles = NewObject<UDreamUIRichTextCustomStyleData>(TestWorld.World);
	TMap<FName, FDreamUIRichTextCustomStyleItemData> StyleMap;
	FDreamUIRichTextCustomStyleItemData Warn;
	Warn.colorType = EDreamUIRichTextCustomStyleData_ColorType::Replace;
	Warn.color = FColor::Red;
	StyleMap.Add(FName(TEXT("warn")), Warn);
	FDreamUIRichTextCustomStyleItemData Dim;
	Dim.colorType = EDreamUIRichTextCustomStyleData_ColorType::Multiply;
	Dim.color = FColor(0, 255, 0, 255);
	StyleMap.Add(FName(TEXT("dim")), Dim);
	Styles->SetDataMap(StyleMap);

	FDreamTextLayoutInput In = MakeInput(Font, TEXT("<warn>ab<b>c</b>d<color=blue>e</color></warn><dim>f</dim>"), 900.0f, 200.0f);
	In.bRichText = true;
	In.RichTextCustomStyleData = Styles;
	FDreamTextDisplayList DL;
	FDreamTextLayoutEngine::Layout(In, DL);
	if (!TestEqual(TEXT("six glyphs"), DL.Items.Num(), 6))return false;
	for (int32 i = 0; i < 4; i++)
	{
		TestTrue(*FString::Printf(TEXT("character %d says it has a colour"), i), DL.Items[i].Style.bHasColor);
		TestEqual(*FString::Printf(TEXT("character %d is red"), i), DL.Items[i].Style.Color, FColor::Red);
	}
	TestTrue(TEXT("c is bold as well"), DL.Items[2].Style.bBold);
	TestFalse(TEXT("d after the bold is not"), DL.Items[3].Style.bBold);
	TestEqual(TEXT("a colour tag inside the style has the last word"), DL.Items[4].Style.Color, FColor::Blue);
	TestTrue(TEXT("f waits for the text's colour to multiply"), DL.Items[5].Style.bHasMultiplyColor);
	TestFalse(TEXT("and has no colour of its own"), DL.Items[5].Style.bHasColor);

	FDreamUIGeometry Geometry;
	TArray<FDreamUITextCharProperty> Chars;
	FDreamTextPainter::Paint(DL, MakePaint(FColor(10, 200, 30, 255)), Geometry, Chars);
	if (!TestEqual(TEXT("six characters painted"), Chars.Num(), 6))return false;
	TestEqual(TEXT("a paints red"), Geometry.Vertices[Chars[0].StartVertIndex].Color, FColor::Red);
	TestEqual(TEXT("c paints red too"), Geometry.Vertices[Chars[2].StartVertIndex].Color, FColor::Red);
	TestEqual(TEXT("f paints the text's colour times the style's"), Geometry.Vertices[Chars[5].StartVertIndex].Color, FColor(0, 200, 0, 255));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextClampDropsInlineObjectsTest,
	"DreamGUI.Text.Pipeline.TruncationTakesTheInlineImagesItCutsOffWithIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Truncate and Ellipsis cut glyphs and nothing else: an image after the cut, or in the stretch an ellipsis strips to
 * make room for itself, was still handed to the rich text for a sprite and drawn past the edge of the box.
 */
bool FDreamTextClampDropsInlineObjectsTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextLayoutTestLocal;
	FScopedGameWorld TestWorld;
	UDreamTextTestFont* Font = NewObject<UDreamTextTestFont>(TestWorld.World);

	auto Layout = [Font](const TCHAR* Content, EDreamUITextOverflowType Overflow, float Width, FDreamTextDisplayList& OutDL)
	{
		FDreamTextLayoutInput In = MakeInput(Font, Content, Width, 200.0f);
		In.bRichText = true;
		In.OverflowType = Overflow;
		FDreamTextLayoutEngine::Layout(In, OutDL);
	};
	const float WideLeft = BoxLeft(MakeInput(Font, TEXT(""), 2000.0f, 200.0f));

	// Where the e starts, from the whole line in a wide box; then a box that ends inside the e.
	FDreamTextDisplayList Wide;
	Layout(TEXT("abcdefghij<img=x/>k"), EDreamUITextOverflowType::HorizontalOverflow, 2000.0f, Wide);
	if (!TestEqual(TEXT("the wide box places the image"), Wide.Images.Num(), 1))return false;
	const FDreamTextGlyphItem* E = FindItem(Wide, 4);
	if (!TestNotNull(TEXT("the e is laid out"), E))return false;
	FDreamTextDisplayList Cut;
	Layout(TEXT("abcdefghij<img=x/>k"), EDreamUITextOverflowType::Truncate, E->Pen.X - WideLeft + 1.0f, Cut);
	TestTrue(TEXT("the narrow box truncates"), Cut.bTruncated);
	TestEqual(TEXT("and the image past the cut is not placed"), Cut.Images.Num(), 0);

	// An image just inside the box survives a truncation there, but not the room an ellipsis needs.
	FDreamTextDisplayList WideImage;
	Layout(TEXT("abc<img=x/>defghij"), EDreamUITextOverflowType::HorizontalOverflow, 2000.0f, WideImage);
	float ImageEnd = 0.0f;
	for (const FDreamTextGlyphItem& Item : WideImage.Items)
	{
		if (Item.Kind == EDreamTextItemKind::Image)
		{
			ImageEnd = Item.Pen.X - WideLeft + Item.AdvanceWithSpace;
			break;
		}
	}
	FDreamTextDisplayList Truncated;
	Layout(TEXT("abc<img=x/>defghij"), EDreamUITextOverflowType::Truncate, ImageEnd + 2.0f, Truncated);
	TestEqual(TEXT("truncating just after the image keeps it"), Truncated.Images.Num(), 1);
	FDreamTextDisplayList Elided;
	Layout(TEXT("abc<img=x/>defghij"), EDreamUITextOverflowType::Ellipsis, ImageEnd + 2.0f, Elided);
	TestTrue(TEXT("eliding there truncates"), Elided.bTruncated);
	TestEqual(TEXT("and the ellipsis's room takes the image"), Elided.Images.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextEllipsisTakesTheRunsStyleTest,
	"DreamGUI.Text.Pipeline.AnEllipsisIsSetInTheSizeAndWeightOfTheTextItEnds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The ellipsis was always fetched at the text's own size and never bold, so a bold 48 px run elided inside a 16 px text
 * ended in a small, light dot-dot-dot. It is set in the size and weight of the text it ends.
 */
bool FDreamTextEllipsisTakesTheRunsStyleTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextLayoutTestLocal;
	FScopedGameWorld TestWorld;
	UDreamTextTestFont* Font = NewObject<UDreamTextTestFont>(TestWorld.World);

	FDreamTextLayoutInput In = MakeInput(Font, TEXT("<size=48><b>abcdefghijklmnopqrstuvwxyz</b></size>"), 300.0f, 200.0f);
	In.FontSize = 16.0f;
	In.bRichText = true;
	In.OverflowType = EDreamUITextOverflowType::Ellipsis;
	FDreamTextDisplayList DL;
	FDreamTextLayoutEngine::Layout(In, DL);
	const FDreamTextGlyphItem* Dots = nullptr;
	for (const FDreamTextGlyphItem& Item : DL.Items)
	{
		if (Item.Codepoint == 0x2026 && Item.bEmit)Dots = &Item;
	}
	if (!TestNotNull(TEXT("the line ends in an ellipsis"), Dots))return false;
	TestEqual(TEXT("as wide as a bold 48 px ellipsis"), Dots->Glyph.XAdvance, Font->GetCharData(0x2026, 48.0f, true).XAdvance, 0.001f);
	TestEqual(TEXT("and the size it is drawn at is the run's"), Dots->Style.Size, 48.0f, 0.001f);
	TestTrue(TEXT("and its weight"), Dots->Style.bBold);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextTallImageGrowsItsLineTest,
	"DreamGUI.Text.RichText.ATallInlineImageGrowsItsLineAndCanSitOnTheBaseline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A line's box came from its fonts alone while an image was centred on it, so a 48-tall image in 20 px text overlapped
 * the lines around it and the paragraph's height left it out. The line grows to hold it. An image can also be told to
 * sit on the baseline, like a letter.
 */
bool FDreamTextTallImageGrowsItsLineTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextLayoutTestLocal;
	FScopedGameWorld TestWorld;
	UDreamTextTestFont* Font = NewObject<UDreamTextTestFont>(TestWorld.World);

	FDreamTextLayoutInput In = MakeInput(Font, TEXT("a<img=x,48/>\nb"), 600.0f, 300.0f);
	In.FontSize = 20.0f;
	In.bRichText = true;
	FDreamTextDisplayList DL;
	FDreamTextLayoutEngine::Layout(In, DL);
	if (!TestEqual(TEXT("two lines"), DL.Lines.Num(), 2))return false;
	if (!TestEqual(TEXT("one image"), DL.Images.Num(), 1))return false;
	const FDreamTextGlyphItem* B = FindItem(DL, 3);
	if (!TestNotNull(TEXT("the b is laid out"), B))return false;
	const float ImageBottom = (float)(DL.Images[0].Position.Y - DL.Images[0].Size.Y * 0.5);
	const float NextLineTop = B->Pen.Y + 20.0f * 0.95f;
	TestTrue(*FString::Printf(TEXT("the image (bottom %.2f) stays above the next line's box (top %.2f)"), ImageBottom, NextLineTop),
		ImageBottom >= NextLineTop - 0.01f);
	TestTrue(TEXT("and the paragraph is tall enough for both"), DL.PreferredSize.Y >= 48.0f + 20.0f * 1.25f - 0.01f);

	FDreamTextLayoutInput OnBaseline = MakeInput(Font, TEXT("a<img=x,48,baseline/>"), 600.0f, 300.0f);
	OnBaseline.FontSize = 20.0f;
	OnBaseline.bRichText = true;
	FDreamTextDisplayList BaselineDL;
	FDreamTextLayoutEngine::Layout(OnBaseline, BaselineDL);
	const FDreamTextGlyphItem* A = FindItem(BaselineDL, 0);
	if (TestEqual(TEXT("baseline: one image"), BaselineDL.Images.Num(), 1) && TestNotNull(TEXT("baseline: the a is laid out"), A))
	{
		const float BaselineImageBottom = (float)(BaselineDL.Images[0].Position.Y - BaselineDL.Images[0].Size.Y * 0.5);
		TestEqual(TEXT("a baseline image's bottom edge is on the baseline"), BaselineImageBottom, A->Pen.Y, 0.01f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextImageSizeIgnoresRasterScaleTest,
	"DreamGUI.Text.RichText.AnInlineImageIsSizedInTextUnitsUnderAnyScale",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * An image was sized from the size a glyph is rasterized at. In world space with DynamicPixelsPerUnit at 4, a bitmap
 * font's image came out four times the font size; under a screen canvas scaled by 2, the size a tag asked for came out
 * halved. Both sizes are in text units, like the font size, and no raster scale applies to them.
 */
bool FDreamTextImageSizeIgnoresRasterScaleTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextLayoutTestLocal;
	FScopedGameWorld TestWorld;
	UDreamTextTestFont* Font = NewObject<UDreamTextTestFont>(TestWorld.World);
	Font->bMockSupportsDynamicPixelsPerUnit = true;

	FDreamTextLayoutInput WorldSpace = MakeInput(Font, TEXT("a<img=x/>"), 600.0f, 300.0f);
	WorldSpace.FontSize = 20.0f;
	WorldSpace.bRichText = true;
	WorldSpace.bRenderToWorldSpace = true;
	WorldSpace.DynamicPixelsPerUnit = 4.0f;
	FDreamTextDisplayList WorldDL;
	FDreamTextLayoutEngine::Layout(WorldSpace, WorldDL);
	if (TestEqual(TEXT("world space: one image"), WorldDL.Images.Num(), 1))
	{
		TestEqual(TEXT("as tall as the font, not four times it"), (float)WorldDL.Images[0].Size.Y, 20.0f, 0.01f);
	}

	FDreamTextLayoutInput Scaled = MakeInput(Font, TEXT("a<img=x,48/>"), 600.0f, 300.0f);
	Scaled.FontSize = 20.0f;
	Scaled.bRichText = true;
	Scaled.RootCanvasScale = 2.0f;
	FDreamTextDisplayList ScaledDL;
	FDreamTextLayoutEngine::Layout(Scaled, ScaledDL);
	if (TestEqual(TEXT("scaled canvas: one image"), ScaledDL.Images.Num(), 1))
	{
		TestEqual(TEXT("as tall as the tag asked, not half of it"), (float)ScaledDL.Images[0].Size.Y, 48.0f, 0.01f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextSpacingSkipsMarksTest,
	"DreamGUI.Text.Pipeline.LetterSpacingNeverSeparatesAnAccentFromItsLetter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Letter spacing was added after every element, so a combining accent -- an element of its own -- was pushed one
 * spacing away from the letter it belongs on. Spacing goes between grapheme clusters only.
 */
bool FDreamTextSpacingSkipsMarksTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextLayoutTestLocal;
	FScopedGameWorld TestWorld;
	UDreamTextTestFont* Font = NewObject<UDreamTextTestFont>(TestWorld.World);

	auto Positions = [Font](float Spacing, float& OutE, float& OutAccent, float& OutX)
	{
		FDreamTextLayoutInput In = MakeInput(Font, TEXT("e\U00000301x"), 600.0f, 200.0f);
		In.FontSpace.X = Spacing;
		FDreamTextDisplayList DL;
		FDreamTextLayoutEngine::Layout(In, DL);
		const FDreamTextGlyphItem* E = FindItem(DL, 0);
		const FDreamTextGlyphItem* Accent = FindItem(DL, 1);
		const FDreamTextGlyphItem* X = FindItem(DL, 2);
		OutE = E ? E->Pen.X : 0.0f;
		OutAccent = Accent ? Accent->Pen.X : 0.0f;
		OutX = X ? X->Pen.X : 0.0f;
		return E != nullptr && Accent != nullptr && X != nullptr;
	};
	float E0 = 0.0f, Accent0 = 0.0f, X0 = 0.0f, E3 = 0.0f, Accent3 = 0.0f, X3 = 0.0f;
	if (!TestTrue(TEXT("unspaced: three glyphs"), Positions(0.0f, E0, Accent0, X0)))return false;
	if (!TestTrue(TEXT("spaced: three glyphs"), Positions(3.0f, E3, Accent3, X3)))return false;
	TestEqual(TEXT("the accent stays where it was on its letter"), Accent3 - E3, Accent0 - E0, 0.001f);
	TestEqual(TEXT("and the next letter moves by one spacing, not two"), (X3 - E3) - (X0 - E0), 3.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextGraphemeClusterTest,
	"DreamGUI.Text.Breaker.AGraphemeClusterHasOneCaretAndIsNeverSplitAcrossLines",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Every element had a caret and could be cut from the next one, so a caret stood between a letter and its accent and
 * the per-character fallback put an accent on a line of its own. A grapheme cluster (UAX #29) is one caret stop and one
 * unit to the breaker: carets of e, U+0301, e stand at offsets 0, 2 and 3, and a decomposed letter with two accents stays
 * whole even in a box one pixel wide.
 */
bool FDreamTextGraphemeClusterTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextLayoutTestLocal;
	FScopedGameWorld TestWorld;
	UDreamTextTestFont* Font = NewObject<UDreamTextTestFont>(TestWorld.World);

	FDreamTextDisplayList DL;
	FDreamTextLayoutEngine::Layout(MakeInput(Font, TEXT("e\U00000301e"), 600.0f, 200.0f), DL);
	if (!TestEqual(TEXT("one line"), DL.Lines.Num(), 1))return false;
	const TArray<FDreamUITextCaretProperty>& Carets = DL.Lines[0].CaretPropertyList;
	if (TestEqual(TEXT("two clusters and the end: three carets"), Carets.Num(), 3))
	{
		TestEqual(TEXT("before the accented e"), Carets[0].CharIndex, 0);
		TestEqual(TEXT("before the plain e, past the accent"), Carets[1].CharIndex, 2);
		TestEqual(TEXT("at the end"), Carets[2].CharIndex, 3);
	}

	FDreamTextLayoutInput Narrow = MakeInput(Font, TEXT("e\U00000323\U00000302"), 1.0f, 600.0f);
	Narrow.OverflowType = EDreamUITextOverflowType::VerticalOverflow;
	Narrow.WrappingPolicy = ETextWrappingPolicy::AllowPerCharacterWrapping;
	FDreamTextDisplayList NarrowDL;
	FDreamTextLayoutEngine::Layout(Narrow, NarrowDL);
	TestEqual(TEXT("a letter with two accents stays on one line"), NarrowDL.Lines.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextHalfLeadingTest,
	"DreamGUI.Text.Pipeline.ExtraLineHeightGoesHalfAboveAndHalfBelowTheLine",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A line-height percentage over 100% put all of its extra space under the line, so a single line in a tall line box sat
 * at the top of it. CSS splits the leading -- half above, half below -- and the line sits in the middle. At 100% a line set
 * in one face stays where the old formula put it; a line that mixes faces with different leading can move a little, its
 * box being the union of the boxes on it, as in CSS.
 */
bool FDreamTextHalfLeadingTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextLayoutTestLocal;
	FScopedGameWorld TestWorld;
	UDreamTextTestFont* Font = NewObject<UDreamTextTestFont>(TestWorld.World);
	const float Ascent = 24.0f * 0.95f;
	const float Descent = 24.0f * 0.3f;
	const float LineHeight = 24.0f * 1.25f;

	FDreamTextLayoutInput Tall = MakeInput(Font, TEXT("Ag"), 600.0f, 200.0f);
	Tall.LineHeightPercentage = 2.0f;
	FDreamTextDisplayList TallDL;
	FDreamTextLayoutEngine::Layout(Tall, TallDL);
	const float Top = Tall.Height * (0.5f - Tall.Pivot.Y) + Tall.Height * 0.5f;
	if (!TestTrue(TEXT("something is laid out"), TallDL.Items.Num() > 0))return false;
	const float Baseline = TallDL.Items[0].Pen.Y;
	TestEqual(TEXT("the line is twice as tall"), TallDL.PreferredSize.Y, LineHeight * 2.0f, 0.01f);
	const float Above = (Top - Baseline) - Ascent;
	const float Below = (Baseline - (Top - LineHeight * 2.0f)) - Descent;
	TestEqual(TEXT("as much room above the ascent as below the descent"), Above, Below, 0.01f);
	TestEqual(TEXT("which is half the extra height"), Above, LineHeight * 0.5f, 0.01f);

	// At 100%, lines in one face sit where the old formula put them: the baseline (LineHeight - (Ascent + Descent)) / 2 +
	// Ascent below the line's top, the next line LineHeight + FontSpace.Y further down.
	FDreamTextLayoutInput Normal = MakeInput(Font, TEXT("ab\ncd"), 600.0f, 200.0f);
	Normal.FontSpace.Y = 5.0f;
	FDreamTextDisplayList NormalDL;
	FDreamTextLayoutEngine::Layout(Normal, NormalDL);
	const FDreamTextGlyphItem* FirstLine = FindItem(NormalDL, 0);
	const FDreamTextGlyphItem* SecondLine = FindItem(NormalDL, 3);
	if (TestNotNull(TEXT("the first line is laid out"), FirstLine) && TestNotNull(TEXT("the second line is laid out"), SecondLine))
	{
		const float OldBaselineDrop = (LineHeight - (Ascent + Descent)) * 0.5f + Ascent;
		TestEqual(TEXT("the first baseline is where it was"), FirstLine->Pen.Y, Top - OldBaselineDrop, 0.001f);
		TestEqual(TEXT("and so is the second"), SecondLine->Pen.Y, Top - (LineHeight + 5.0f) - OldBaselineDrop, 0.001f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextSuperscriptLineBoxTest,
	"DreamGUI.Text.RichText.ASuperscriptIsShiftedAsChromeShiftsItAndItsLineMakesRoom",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Superscript and subscript were 0.8 the size and moved half an em only when painted, so the line box never knew about
 * them and a superscript could overlap the line above. The layout shifts them now, as Blink does -- at 1/1.2 the size,
 * the baseline a third of the parent's size plus a pixel up, or a fifth plus a pixel down -- and the line grows to hold them.
 */
bool FDreamTextSuperscriptLineBoxTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextLayoutTestLocal;
	FScopedGameWorld TestWorld;
	UDreamTextTestFont* Font = NewObject<UDreamTextTestFont>(TestWorld.World);

	FDreamTextLayoutInput Sup = MakeInput(Font, TEXT("x<sup>2</sup>"), 600.0f, 200.0f);
	Sup.bRichText = true;
	FDreamTextDisplayList SupDL;
	FDreamTextLayoutEngine::Layout(Sup, SupDL);
	const FDreamTextGlyphItem* X = FindItem(SupDL, 0);
	const FDreamTextGlyphItem* Two = FindItem(SupDL, 1);
	if (TestNotNull(TEXT("x is laid out"), X) && TestNotNull(TEXT("the superscript is laid out"), Two))
	{
		TestEqual(TEXT("a superscript is 1/1.2 the size"), Two->Style.Size, 20.0f, 0.01f);
		TestEqual(TEXT("and its baseline is a third of 24 plus one higher"), Two->Pen.Y, X->Pen.Y + 9.0f, 0.01f);
		TestEqual(TEXT("its raised box grows the line"), SupDL.PreferredSize.Y, 20.0f * 0.95f + 9.0f + 24.0f * 0.3f, 0.01f);
	}

	FDreamTextLayoutInput Sub = MakeInput(Font, TEXT("x<sub>2</sub>"), 600.0f, 200.0f);
	Sub.bRichText = true;
	FDreamTextDisplayList SubDL;
	FDreamTextLayoutEngine::Layout(Sub, SubDL);
	const FDreamTextGlyphItem* SubX = FindItem(SubDL, 0);
	const FDreamTextGlyphItem* SubTwo = FindItem(SubDL, 1);
	if (TestNotNull(TEXT("sub: x is laid out"), SubX) && TestNotNull(TEXT("the subscript is laid out"), SubTwo))
	{
		TestEqual(TEXT("a subscript's baseline is a fifth of 24 plus one lower"), SubTwo->Pen.Y, SubX->Pen.Y - 5.8f, 0.01f);
		TestEqual(TEXT("and its lowered box grows the line"), SubDL.PreferredSize.Y, 24.0f * 0.95f + 20.0f * 0.3f + 5.8f, 0.01f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextStyleSizeAndSuperscriptTest,
	"DreamGUI.Text.RichText.ACustomStyleSizeAndASuperscriptFoldInTheOrderTheyNest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A custom style's size was written over the size the parser had worked out: a <sup> inside a sized style lost its step
 * down and was raised by a third of the text's size rather than the style's, and a superscript style around a <size>
 * shrank the size the tag names. Style sizes, size tags and superscripts fold in the order their tags were opened, as
 * CSS cascades font-size and vertical-align.
 */
bool FDreamTextStyleSizeAndSuperscriptTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextLayoutTestLocal;
	FScopedGameWorld TestWorld;
	UDreamTextTestFont* Font = NewObject<UDreamTextTestFont>(TestWorld.World);
	UDreamUIRichTextCustomStyleData* Styles = NewObject<UDreamUIRichTextCustomStyleData>(TestWorld.World);
	TMap<FName, FDreamUIRichTextCustomStyleItemData> StyleMap;
	FDreamUIRichTextCustomStyleItemData Warn;
	Warn.sizeType = EDreamUIRichTextCustomStyleData_SizeType::SizeValue;
	Warn.size = 30;
	StyleMap.Add(FName(TEXT("warn")), Warn);
	FDreamUIRichTextCustomStyleItemData Up;
	Up.supOrSub = EDreamUIRichTextCustomStyleData_SupOrSubType::Superscript;
	StyleMap.Add(FName(TEXT("up")), Up);
	Styles->SetDataMap(StyleMap);
	auto Layout = [Font, Styles](const TCHAR* Content, FDreamTextDisplayList& OutDL)
	{
		FDreamTextLayoutInput In = MakeInput(Font, Content, 600.0f, 200.0f);
		In.bRichText = true;
		In.RichTextCustomStyleData = Styles;
		FDreamTextLayoutEngine::Layout(In, OutDL);
	};

	// The text is 24 and the style 30: the superscript inside the style is a step down from 30, raised by a third of 30
	// plus one.
	FDreamTextDisplayList Sized;
	Layout(TEXT("a<warn><sup>x</sup></warn>"), Sized);
	const FDreamTextGlyphItem* A = FindItem(Sized, 0);
	const FDreamTextGlyphItem* X = FindItem(Sized, 1);
	if (TestNotNull(TEXT("a is laid out"), A) && TestNotNull(TEXT("x is laid out"), X))
	{
		TestEqual(TEXT("a superscript inside a 30 style is 30 / 1.2"), X->Style.Size, 25.0f, 0.01f);
		TestEqual(TEXT("raised by a third of 30 plus one"), X->Pen.Y, A->Pen.Y + 11.0f, 0.01f);
	}

	// A <size> inside a superscript style: the size the tag names, on the baseline the superscript raised from 24.
	FDreamTextDisplayList Raised;
	Layout(TEXT("a<up><size=40>y</size></up>"), Raised);
	const FDreamTextGlyphItem* RaisedA = FindItem(Raised, 0);
	const FDreamTextGlyphItem* Y = FindItem(Raised, 1);
	if (TestNotNull(TEXT("a is laid out beside y"), RaisedA) && TestNotNull(TEXT("y is laid out"), Y))
	{
		TestEqual(TEXT("a size inside a superscript style is the size it names"), Y->Style.Size, 40.0f, 0.01f);
		TestEqual(TEXT("on a baseline a third of 24 plus one up"), Y->Pen.Y, RaisedA->Pen.Y + 9.0f, 0.01f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextUnderlineIsOneStrokeTest,
	"DreamGUI.Text.Painter.AnUnderlineIsOneStrokeAcrossTheSpacesBetweenWords",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Each glyph drew its own piece of underline and a space drew none, so `<u>a b</u>` had a gap where a browser draws one
 * line; and under negative letter spacing the pieces had negative widths and blended twice where they overlapped. A run
 * of the same decoration is one quad, across the spaces between words and not past a space hanging off the line's end.
 */
bool FDreamTextUnderlineIsOneStrokeTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextLayoutTestLocal;
	FScopedGameWorld TestWorld;
	UDreamTextTestFont* Font = NewObject<UDreamTextTestFont>(TestWorld.World);

	FDreamTextLayoutInput In = MakeInput(Font, TEXT("<u>a b </u>"), 600.0f, 200.0f);
	In.bRichText = true;
	FDreamTextDisplayList DL;
	FDreamTextLayoutEngine::Layout(In, DL);
	FDreamUIGeometry Geometry;
	TArray<FDreamUITextCharProperty> Chars;
	FDreamTextPainter::Paint(DL, MakePaint(), Geometry, Chars);
	if (!TestEqual(TEXT("two glyphs and one stroke"), Geometry.OriginVertices.Num(), 2 * 4 + 4))return false;
	const FDreamTextGlyphItem* A = FindItem(DL, 0);
	const FDreamTextGlyphItem* B = FindItem(DL, 2);
	if (!TestNotNull(TEXT("a is laid out"), A) || !TestNotNull(TEXT("b is laid out"), B))return false;
	const float StrokeLeft = Geometry.OriginVertices[8].Position.Y;
	const float StrokeRight = Geometry.OriginVertices[9].Position.Y;
	TestEqual(TEXT("the stroke starts at a"), StrokeLeft, A->Pen.X + A->DecorationOffset, 0.001f);
	TestEqual(TEXT("and runs through the space to the end of b, not past the space after it"), StrokeRight,
		B->Pen.X + B->DecorationOffset + B->AdvanceWithSpace, 0.001f);

	FDreamTextLayoutInput Tight = MakeInput(Font, TEXT("<u>abc def</u>"), 600.0f, 200.0f);
	Tight.bRichText = true;
	Tight.FontSpace.X = -5.0f;
	FDreamTextDisplayList TightDL;
	FDreamTextLayoutEngine::Layout(Tight, TightDL);
	FDreamUIGeometry TightGeometry;
	TArray<FDreamUITextCharProperty> TightChars;
	FDreamTextPainter::Paint(TightDL, MakePaint(), TightGeometry, TightChars);
	const int32 StrokeQuads = TightGeometry.OriginVertices.Num() / 4 - TightChars.Num();
	TestEqual(TEXT("negative spacing: still one stroke"), StrokeQuads, 1);
	for (int32 Quad = TightChars.Num(); Quad < TightGeometry.OriginVertices.Num() / 4; Quad++)
	{
		const float Left = TightGeometry.OriginVertices[Quad * 4].Position.Y;
		const float Right = TightGeometry.OriginVertices[Quad * 4 + 1].Position.Y;
		TestTrue(*FString::Printf(TEXT("stroke %d has no negative width"), Quad), Right >= Left);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextAnimatedStrokesTest,
	"DreamGUI.Text.Painter.WhileCharactersAnimateOneByOneEachCarriesItsOwnPieceOfUnderline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A stroke drawn as one strip across a run belongs to no character, so TextAnimation, which moves, fades and reveals the
 * vertices of each character's range, left an underline standing under letters that had flown off or faded out. While
 * anything animates the characters one by one, every underline vertex lies inside some character's vertex range, and
 * a glyph's piece reaches across the space after it, so the still text shows no gap between words.
 */
bool FDreamTextAnimatedStrokesTest::RunTest(const FString& Parameters)
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
	Child->SetWidth(400.0f);
	Child->SetHeight(80.0f);
	UDreamText* Text = Child->CreateNewVisual<UDreamText>();
	if (!TestNotNull(TEXT("text visual"), Text))return false;
	Text->SetFont(NewObject<UDreamTextTestFont>(TestWorld.World));
	Child->OnRegister();
	if (!TestTrue(TEXT("child attaches"), Child->TrySetParent(Root, false)))return false;
	Text->SetRichText(true);
	Text->SetText(FText::FromString(TEXT("<u>ab cd</u>")));

	FDreamTextDisplayList DL;
	FDreamTextLayoutEngine::Layout(UDreamText::MakeLayoutInput(Text, Text->GetFontSize()), DL);
	const FDreamTextPaintParams Still = UDreamText::MakePaintParams(Text);
	TestFalse(TEXT("with nothing animating the characters, strokes are drawn in runs"), Still.bStrokesPerCharacter);
	FDreamUIGeometry StillGeometry;
	TArray<FDreamUITextCharProperty> StillChars;
	FDreamTextPainter::Paint(DL, Still, StillGeometry, StillChars);

	// Any live object can stand for an animator; UObject itself is abstract.
	UObject* Animator = NewObject<UDreamTextTestFont>(TestWorld.World);
	Text->RegisterPerCharacterAnimation(Animator);
	const FDreamTextPaintParams Animated = UDreamText::MakePaintParams(Text);
	TestTrue(TEXT("a registered animator has the strokes drawn per character"), Animated.bStrokesPerCharacter);
	FDreamUIGeometry Geometry;
	TArray<FDreamUITextCharProperty> Chars;
	FDreamTextPainter::Paint(DL, Animated, Geometry, Chars);
	if (!TestEqual(TEXT("four characters"), Chars.Num(), 4) || !TestEqual(TEXT("four characters in the still text"), StillChars.Num(), 4))return false;
	for (int32 Vertex = 0; Vertex < Geometry.OriginVertices.Num(); Vertex++)
	{
		bool bInsideCharacter = false;
		for (const FDreamUITextCharProperty& Char : Chars)
		{
			bInsideCharacter |= Vertex >= Char.StartVertIndex && Vertex < Char.StartVertIndex + Char.VertCount;
		}
		if (!bInsideCharacter)
		{
			AddError(FString::Printf(TEXT("vertex %d lies inside no character's range"), Vertex));
			break;
		}
	}
	for (int32 i = 0; i < Chars.Num(); i++)
	{
		TestEqual(*FString::Printf(TEXT("character %d holds its glyph and its piece of underline"), i), Chars[i].VertCount, StillChars[i].VertCount * 2);
	}
	// A character's vertices are its glyph's, then its piece's; a piece's first two vertices are its bottom left and right.
	const int32 PieceUnderB = Chars[1].StartVertIndex + Chars[1].VertCount / 2;
	const int32 PieceUnderC = Chars[2].StartVertIndex + Chars[2].VertCount / 2;
	TestTrue(TEXT("the piece under b reaches across the space to the piece under c"),
		Geometry.OriginVertices[PieceUnderB + 1].Position.Y >= Geometry.OriginVertices[PieceUnderC].Position.Y - 0.001f);

	Text->UnregisterPerCharacterAnimation(Animator);
	TestFalse(TEXT("once the animator lets go, strokes are runs again"), UDreamText::MakePaintParams(Text).bStrokesPerCharacter);
	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextTagColourOverrideTest,
	"DreamGUI.Text.Painter.ATagColourOverrideRecoloursItsTagWithoutALayout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A hovered or pressed link changes colour at paint time: an override names a tag and the colour its glyphs take, and
 * painting again with another override must not lay the text out again.
 */
bool FDreamTextTagColourOverrideTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextLayoutTestLocal;
	FScopedGameWorld TestWorld;
	UDreamTextTestFont* Font = NewObject<UDreamTextTestFont>(TestWorld.World);

	FDreamUITextGeometryCache Cache;
	FDreamTextLayoutInput In = MakeInput(Font, TEXT("<a=x>ab</a> <a=y>cd</a>"), 600.0f, 200.0f);
	In.bRichText = true;
	Cache.SetLayoutInput(In);
	FDreamUIGeometry Geometry;

	TArray<TPair<int32, FColor>> Hovered;
	Hovered.Add(TPair<int32, FColor>(1, FColor::Red));
	FDreamTextPaintParams Params = MakePaint(FColor::White);
	Params.TagColorOverrides = &Hovered;
	Cache.Paint(Geometry, Params);
	const int32 LayoutsAfterFirstPaint = Cache.GetLayoutRunCount();
	const TArray<FDreamUITextCharProperty>& Chars = Cache.GetCharPropertyArray();
	if (!TestEqual(TEXT("four characters"), Chars.Num(), 4))return false;
	TestEqual(TEXT("a keeps the text's colour"), Geometry.Vertices[Chars[0].StartVertIndex].Color, FColor::White);
	TestEqual(TEXT("b too"), Geometry.Vertices[Chars[1].StartVertIndex].Color, FColor::White);
	TestEqual(TEXT("c takes the second link's override"), Geometry.Vertices[Chars[2].StartVertIndex].Color, FColor::Red);
	TestEqual(TEXT("and so does d"), Geometry.Vertices[Chars[3].StartVertIndex].Color, FColor::Red);

	TArray<TPair<int32, FColor>> Pressed;
	Pressed.Add(TPair<int32, FColor>(0, FColor::Blue));
	Params.TagColorOverrides = &Pressed;
	Cache.Paint(Geometry, Params);
	TestEqual(TEXT("painting with another override lays nothing out"), Cache.GetLayoutRunCount(), LayoutsAfterFirstPaint);
	TestEqual(TEXT("a takes the first link's override"), Geometry.Vertices[Cache.GetCharPropertyArray()[0].StartVertIndex].Color, FColor::Blue);
	TestEqual(TEXT("and c is back to the text's colour"), Geometry.Vertices[Cache.GetCharPropertyArray()[2].StartVertIndex].Color, FColor::White);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextEmojiFallsBackToAGlyphTest,
	"DreamGUI.Text.Pipeline.AnEmojiTheFontCanDrawIsAGlyphNotABlank",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * An emoji was always an inline placeholder for the font's emoji data, so a font without emoji data drew a blank even
 * when one of its faces had the code point. A face that has it draws it as a glyph; monochrome beats a blank.
 */
bool FDreamTextEmojiFallsBackToAGlyphTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextLayoutTestLocal;
	FScopedGameWorld TestWorld;
	UDreamTextTestFont* Font = NewObject<UDreamTextTestFont>(TestWorld.World);

	FDreamTextDisplayList DL;
	FDreamTextLayoutEngine::Layout(MakeInput(Font, TEXT("a\U0001F600b"), 600.0f, 200.0f), DL);
	TestEqual(TEXT("no emoji object is asked for"), DL.Emojis.Num(), 0);
	TestEqual(TEXT("three characters are drawn"), DL.VisibleCharCount, 3);
	const FDreamTextGlyphItem* Emoji = FindItem(DL, 1);
	if (TestNotNull(TEXT("the emoji is a glyph item"), Emoji))
	{
		TestTrue(TEXT("and it is emitted"), Emoji->bEmit);
		TestEqual(TEXT("its code point is the emoji's"), (int32)Emoji->Codepoint, 0x1F600);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextMarkOnASpaceTest,
	"DreamGUI.Text.Pipeline.ACombiningMarkOnASpaceIsDrawn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A space and the combining mark after it are one grapheme cluster, and a cluster led by a space was laid out as a Space
 * item and nothing else, so the mark -- a lone accent is written that way -- took its place and was never drawn. The
 * space draws nothing and the mark is a glyph of its own.
 */
bool FDreamTextMarkOnASpaceTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextLayoutTestLocal;
	FScopedGameWorld TestWorld;
	UDreamTextTestFont* Font = NewObject<UDreamTextTestFont>(TestWorld.World);

	FDreamTextDisplayList DL;
	FDreamTextLayoutEngine::Layout(MakeInput(Font, TEXT(" \U00000301"), 600.0f, 200.0f), DL);
	FDreamUIGeometry Geometry;
	TArray<FDreamUITextCharProperty> Chars;
	FDreamTextPainter::Paint(DL, MakePaint(), Geometry, Chars);
	TestEqual(TEXT("one glyph quad"), Geometry.OriginVertices.Num(), 4);
	if (TestEqual(TEXT("for one character"), Chars.Num(), 1))
	{
		TestEqual(TEXT("which is the mark"), Chars[0].CharIndex, 1);
	}
	return true;
}

#endif
