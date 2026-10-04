// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamText.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamWidgetPropertyBinding.h"
#include "Core/DreamWidgetTree.h"
#include "Text/DreamUIAst.h"
#include "Text/DreamUIDiagnostics.h"
#include "Text/DreamUISourceFile.h"
#include "Text/DreamUITextBuilder.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

/*
 * `rows Type : Style (Column, Column) { values, values }`: one widget per line, the type, the style and the property
 * names written once. The parser expands a table into the ordinary anonymous children its lines stand for, so these
 * tests hold the expansion -- what each line becomes, where its values are, which id it gets -- and the refusals;
 * the build at the end is the proof that nothing downstream had to learn the shape.
 *
 * As in the other parser tests, a refusal is asserted by its code, its line and the count of diagnostics, never by
 * its wording.
 */

namespace DreamUIRowsTestLocal
{
	FString MakeSource(const TArray<FString>& InLines)
	{
		return FString::Join(InLines, TEXT("\n"));
	}

	bool ParseSource(const TArray<FString>& InLines, FDreamUIAst& OutAst, FDreamUIDiagnosticBag& OutDiagnostics)
	{
		return FDreamUISourceFile::Parse(MakeSource(InLines), TEXT("/virtual/Rows.dui"), OutAst, OutDiagnostics);
	}

	int32 CountOf(const FDreamUIDiagnosticBag& InDiagnostics, EDreamUIDiagnosticCode InCode)
	{
		int32 Count = 0;
		for (const FDreamUIDiagnostic& Diagnostic : InDiagnostics.Diagnostics)
		{
			Count += Diagnostic.Code == InCode ? 1 : 0;
		}
		return Count;
	}

	const FDreamUIProperty* PropertyByName(const FDreamUINode& InNode, const TCHAR* InName)
	{
		const FDreamUIProperty* Found = nullptr;
		for (const FDreamUIProperty& Property : InNode.Properties)
		{
			if (Property.Name == InName)
			{
				Found = &Property;
			}
		}
		return Found;
	}

	void Dump(FAutomationTestBase& InTest, const FDreamUIDiagnosticBag& InDiagnostics)
	{
		for (const FDreamUIDiagnostic& Diagnostic : InDiagnostics.Diagnostics)
		{
			InTest.AddInfo(Diagnostic.ToString());
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIRowsExpandTest,
	"DreamGUI.Text.Syntax.ARowsTableBecomesOneUnnamedWidgetPerLine",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIRowsExpandTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIRowsTestLocal;

	FDreamUIAst Ast;
	FDreamUIDiagnosticBag Diagnostics;
	const bool bParsed = ParseSource({
		TEXT("VerticalBox Page {"),                                       // 1
		TEXT("    Text Header {}"),                                       // 2
		TEXT("    rows Text : Caption (Text, FontSize) {"),               // 3
		TEXT("        \"City Ruins\",  24"),                              // 4
		TEXT("        \"Desert Zone\", 18 { HAlign = Right }"),           // 5
		TEXT("        // a comment between rows"),                       // 6
		TEXT("        \"Forest Zone\", 20; \"Factory\", 22"),             // 7
		TEXT("    }"),                                                    // 8
		TEXT("    Text Footer {}"),                                       // 9
		TEXT("}"),                                                        // 10
		TEXT("style Caption { FontSize = 12 }"),
	}, Ast, Diagnostics);
	Dump(*this, Diagnostics);
	TestTrue(TEXT("the file parses"), bParsed);
	TestEqual(TEXT("with nothing to say about it"), Diagnostics.Diagnostics.Num(), 0);

	const TArray<FDreamUINode>& Children = Ast.Root.Children;
	if (!TestEqual(TEXT("the table stands where it is written, between Header and Footer"), Children.Num(), 6))
	{
		return false;
	}
	TestEqual(TEXT("Header first"), Children[0].Id, FString(TEXT("Header")));
	TestEqual(TEXT("Footer last"), Children[5].Id, FString(TEXT("Footer")));

	const TCHAR* const Keys[] = { TEXT("City Ruins"), TEXT("Desert Zone"), TEXT("Forest Zone"), TEXT("Factory") };
	const TCHAR* const Ids[] = { TEXT("Page__Text_City_Ruins"), TEXT("Page__Text_Desert_Zone"), TEXT("Page__Text_Forest_Zone"), TEXT("Page__Text_Factory") };
	const TCHAR* const Sizes[] = { TEXT("24"), TEXT("18"), TEXT("20"), TEXT("22") };
	const int32 Lines[] = { 4, 5, 7, 7 };
	for (int32 Index = 0; Index < 4; ++Index)
	{
		const FDreamUINode& Row = Children[Index + 1];
		const FString What = FString::Printf(TEXT("row %d"), Index);
		TestEqual(What + TEXT(" is a widget of the table's type"), Row.TypeName, FString(TEXT("Text")));
		TestEqual(What + TEXT(" wears the table's style"), Row.StyleName, FString(TEXT("Caption")));
		TestTrue(What + TEXT(" is unnamed"), Row.bAnonymous);
		TestEqual(What + TEXT(" carries its key"), Row.RowKey, FString(Keys[Index]));
		TestEqual(What + TEXT(" is named from its key, not counted"), Row.Id, FString(Ids[Index]));
		TestEqual(What + TEXT(" is located at its line"), Row.Location.Line, Lines[Index]);

		const FDreamUIProperty* Text = PropertyByName(Row, TEXT("Text"));
		const FDreamUIProperty* Size = PropertyByName(Row, TEXT("FontSize"));
		if (TestNotNull(What + TEXT(" has the first column"), Text) && TestNotNull(What + TEXT(" has the second"), Size))
		{
			TestTrue(What + TEXT("'s values are cells"), Text->bRowCell && Size->bRowCell);
			TestEqual(What + TEXT("'s first value"), Text->Value.Raw, FString(Keys[Index]));
			TestEqual(What + TEXT("'s second value"), Size->Value.Raw, FString(Sizes[Index]));
			TestEqual(What + TEXT("'s cell is located at its value"), Size->Location.Column, Size->Value.Location.Column);
		}
	}
	// The block a line may end in: what that one row needs beyond the columns.
	const FDreamUIProperty* Extra = PropertyByName(Children[2], TEXT("HAlign"));
	if (TestNotNull(TEXT("the second row's block gave it HAlign"), Extra))
	{
		TestFalse(TEXT("which is an ordinary line, not a cell"), Extra->bRowCell);
	}
	TestNull(TEXT("and the others have none"), PropertyByName(Children[1], TEXT("HAlign")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIRowsIdTest,
	"DreamGUI.Text.Syntax.ARowsIdComesFromItsKeyAndStaysPutWhenRowsAreInserted",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIRowsIdTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIRowsTestLocal;

	auto IdsOf = [this](const TArray<FString>& InRows, FDreamUIDiagnosticBag& OutDiagnostics)
	{
		TArray<FString> Lines = { TEXT("Widget Root {"), TEXT("    rows Text (Text) {") };
		for (const FString& Row : InRows)
		{
			Lines.Add(TEXT("        ") + Row);
		}
		Lines.Add(TEXT("    }"));
		Lines.Add(TEXT("    Text {}"));
		Lines.Add(TEXT("}"));
		FDreamUIAst Ast;
		ParseSource(Lines, Ast, OutDiagnostics);
		Dump(*this, OutDiagnostics);
		TArray<FString> Ids;
		for (const FDreamUINode& Child : Ast.Root.Children)
		{
			Ids.Add(Child.Id);
		}
		return Ids;
	};

	{
		FDreamUIDiagnosticBag Diagnostics;
		const TArray<FString> Before = IdsOf({ TEXT("\"Save\""), TEXT("\"Load\"") }, Diagnostics);
		FDreamUIDiagnosticBag DiagnosticsAfter;
		const TArray<FString> After = IdsOf({ TEXT("\"New Game\""), TEXT("\"Save\""), TEXT("\"Load\"") }, DiagnosticsAfter);
		if (TestEqual(TEXT("two rows and an unnamed Text"), Before.Num(), 3) && TestEqual(TEXT("three rows and it"), After.Num(), 4))
		{
			TestEqual(TEXT("Save is named by its key"), Before[0], FString(TEXT("Root__Text_Save")));
			TestEqual(TEXT("and keeps the name with a row inserted before it"), After[1], Before[0]);
			TestEqual(TEXT("so does Load"), After[2], Before[1]);
			TestEqual(TEXT("an unnamed sibling of the same type is still counted, from zero -- rows take no count"), Before[2], FString(TEXT("Root__Text0")));
		}
	}
	{
		// What an id cannot hold collapses to one '_', none at the ends, at most 32 characters of key.
		FDreamUIDiagnosticBag Diagnostics;
		const TArray<FString> Ids = IdsOf({ TEXT("\"  W S / Up -- Down!  \""), TEXT("\"A very long first value that goes on and on and on\""), TEXT("12") }, Diagnostics);
		if (TestEqual(TEXT("three rows and the Text"), Ids.Num(), 4))
		{
			TestEqual(TEXT("punctuation and spaces become single underscores"), Ids[0], FString(TEXT("Root__Text_W_S_Up_Down")));
			TestEqual(TEXT("a long key is cut to 32 characters"), Ids[1], FString(TEXT("Root__Text_A_very_long_first_value_that_goe")));
			TestEqual(TEXT("a number is a key like any other"), Ids[2], FString(TEXT("Root__Text_12")));
		}
	}
	{
		// The same key twice: the second is bumped -- and told that its id now depends on order.
		FDreamUIDiagnosticBag Diagnostics;
		const TArray<FString> Ids = IdsOf({ TEXT("\"Exit\""), TEXT("\"Exit\"") }, Diagnostics);
		if (TestEqual(TEXT("two rows and the Text"), Ids.Num(), 3))
		{
			TestEqual(TEXT("the first keeps the key's id"), Ids[0], FString(TEXT("Root__Text_Exit")));
			TestEqual(TEXT("the second is bumped"), Ids[1], FString(TEXT("Root__Text_Exit_1")));
		}
		TestEqual(TEXT("with one DuplicateRowKey"), CountOf(Diagnostics, EDreamUIDiagnosticCode::DuplicateRowKey), 1);
		TestFalse(TEXT("which is a warning, not an error"), Diagnostics.HasErrors());
	}
	{
		// A tuple's spelling is a key like any other; a key nothing of which an id can hold is counted like any unnamed
		// widget, and the count is the one unnamed siblings of the type share.
		FDreamUIDiagnosticBag Diagnostics;
		const TArray<FString> Ids = IdsOf({ TEXT("(1, 2)"), TEXT("\"!!\"") }, Diagnostics);
		if (TestEqual(TEXT("two rows and the Text"), Ids.Num(), 3))
		{
			TestEqual(TEXT("the tuple's row is named from its spelling"), Ids[0], FString(TEXT("Root__Text_1_2")));
			TestEqual(TEXT("the row whose key is all punctuation is counted"), Ids[1], FString(TEXT("Root__Text0")));
			TestEqual(TEXT("and the unnamed Text after them counts on"), Ids[2], FString(TEXT("Root__Text1")));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIRowsRefusalTest,
	"DreamGUI.Text.Syntax.ATableThatDoesNotReadIsMalformedRowsAndMakesNoWidget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIRowsRefusalTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIRowsTestLocal;

	auto Expect = [this](const TCHAR* InWhat, const TArray<FString>& InLines, EDreamUIDiagnosticCode InCode, int32 InLine, int32 InRows)
	{
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		ParseSource(InLines, Ast, Diagnostics);
		Dump(*this, Diagnostics);
		TestEqual(FString(InWhat) + TEXT(": one diagnostic"), Diagnostics.Diagnostics.Num(), 1);
		if (Diagnostics.Diagnostics.Num() == 1)
		{
			TestEqual(FString(InWhat) + TEXT(": its code"), Diagnostics.Diagnostics[0].Code, InCode);
			TestEqual(FString(InWhat) + TEXT(": its line"), Diagnostics.Diagnostics[0].Location.Line, InLine);
		}
		TestEqual(FString(InWhat) + TEXT(": the rows that read"), Ast.Root.Children.Num(), InRows);
	};

	Expect(TEXT("a row with a value too few"), {
		TEXT("Widget Root {"),
		TEXT("    rows Text (Text, FontSize) {"),
		TEXT("        \"A\", 12"),
		TEXT("        \"B\""),
		TEXT("    }"),
		TEXT("}"),
	}, EDreamUIDiagnosticCode::MalformedRows, 4, 1);

	Expect(TEXT("a row with a value too many"), {
		TEXT("Widget Root {"),
		TEXT("    rows Text (Text) {"),
		TEXT("        \"A\", 12"),
		TEXT("        \"B\""),
		TEXT("    }"),
		TEXT("}"),
	}, EDreamUIDiagnosticCode::MalformedRows, 3, 1);

	Expect(TEXT("a column named twice"), {
		TEXT("Widget Root {"),
		TEXT("    rows Text (Text, text) {"),
		TEXT("        \"A\", \"B\""),
		TEXT("    }"),
		TEXT("}"),
	}, EDreamUIDiagnosticCode::MalformedRows, 2, 1);

	Expect(TEXT("a column list that is not names"), {
		TEXT("Widget Root {"),
		TEXT("    rows Text (Text, 12) {"),
		TEXT("        \"A\", \"B\""),
		TEXT("    }"),
		TEXT("    Text After {}"),
		TEXT("}"),
	}, EDreamUIDiagnosticCode::MalformedRows, 2, 1);

	Expect(TEXT("a missing comma between values"), {
		TEXT("Widget Root {"),
		TEXT("    rows Text (Text, FontSize) {"),
		TEXT("        \"A\" 12"),
		TEXT("        \"B\", 14"),
		TEXT("    }"),
		TEXT("}"),
	}, EDreamUIDiagnosticCode::UnexpectedToken, 3, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIRowsStaysAWordTest,
	"DreamGUI.Text.Syntax.RowsIsAKeywordOnlyBeforeATypeAndAColumnList",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIRowsStaysAWordTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIRowsTestLocal;

	// A property called rows, and a node whose type is called rows (with an id, or none), read as they always did.
	FDreamUIAst Ast;
	FDreamUIDiagnosticBag Diagnostics;
	ParseSource({
		TEXT("Widget Root {"),
		TEXT("    rows = 3"),
		TEXT("    rows Grid { }"),
		TEXT("    rows : Card { }"),
		TEXT("}"),
		TEXT("style Card { }"),
	}, Ast, Diagnostics);
	Dump(*this, Diagnostics);
	TestEqual(TEXT("nothing to report"), Diagnostics.Diagnostics.Num(), 0);
	TestNotNull(TEXT("rows = 3 is a property"), PropertyByName(Ast.Root, TEXT("rows")));
	if (TestEqual(TEXT("and the other two are nodes"), Ast.Root.Children.Num(), 2))
	{
		TestEqual(TEXT("of type rows, named Grid"), Ast.Root.Children[0].Id, FString(TEXT("Grid")));
		TestEqual(TEXT("the first keeps its type"), Ast.Root.Children[0].TypeName, FString(TEXT("rows")));
		TestTrue(TEXT("the second is an unnamed node, not a table"), Ast.Root.Children[1].bAnonymous && Ast.Root.Children[1].RowKey.IsEmpty());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIRowsBuildTest,
	"DreamGUI.Text.Syntax.ARowsTableBuildsLikeTheNodesItStandsFor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIRowsBuildTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIRowsTestLocal;

	FDreamUIAst Ast;
	FDreamUIDiagnosticBag Diagnostics;
	Diagnostics.SourceName = TEXT("Rows.dui");
	if (!TestTrue(TEXT("the file parses"), ParseSource({
		TEXT("class /Game/UI/WBP_Rows"),
		TEXT("VerticalBox Page {"),
		TEXT("    rows Text : Caption (Text, FontSize) {"),
		TEXT("        \"Save\", 24"),
		TEXT("        \"Load\", 18 { Text NestedNote { Text = \"in Load\" } }"),
		TEXT("    }"),
		TEXT("}"),
		TEXT("style Caption { FontSize = 12 }"),
	}, Ast, Diagnostics)))
	{
		Dump(*this, Diagnostics);
		return false;
	}

	TArray<FDreamWidgetPropertyBinding> Bindings;
	TStrongObjectPtr<UDreamWidgetTree> Tree(FDreamUITextBuilder::Build(Ast, GetTransientPackage(), Diagnostics, Bindings));
	Dump(*this, Diagnostics);
	TestFalse(TEXT("it builds without an error"), Diagnostics.HasErrors());
	if (!TestNotNull(TEXT("into a tree"), Tree.Get()) || !TestNotNull(TEXT("with a root"), Tree->RootWidget.Get()))
	{
		return false;
	}

	const TArray<UDreamWidget*> Rows = Tree->RootWidget->GetChildren();
	if (!TestEqual(TEXT("two widgets, one per line"), Rows.Num(), 2))
	{
		return false;
	}
	const TCHAR* const Texts[] = { TEXT("Save"), TEXT("Load") };
	const float Sizes[] = { 24.0f, 18.0f };
	const TCHAR* const Ids[] = { TEXT("Page__Text_Save"), TEXT("Page__Text_Load") };
	for (int32 Index = 0; Index < 2; ++Index)
	{
		const UDreamText* Visual = Rows[Index] != nullptr ? Cast<UDreamText>(Rows[Index]->GetVisual()) : nullptr;
		if (!TestNotNull(FString::Printf(TEXT("row %d is a Text"), Index), Visual))
		{
			continue;
		}
		TestEqual(FString::Printf(TEXT("row %d is named from its key"), Index), Rows[Index]->GetDisplayName(), FString(Ids[Index]));
		TestEqual(FString::Printf(TEXT("row %d shows its first value"), Index), Visual->GetText().ToString(), FString(Texts[Index]));
		TestEqual(FString::Printf(TEXT("row %d's second value won over the style's"), Index), Visual->GetFontSize(), Sizes[Index]);
		// The localization key is made from the id, which is made from the key: stable across an inserted row.
		const TOptional<FString> Key = FTextInspector::GetKey(Visual->GetText());
		if (TestTrue(FString::Printf(TEXT("row %d's text has a key"), Index), Key.IsSet()))
		{
			TestEqual(FString::Printf(TEXT("row %d's key is its id and the column"), Index), Key.GetValue(), FString(Ids[Index]) + TEXT(".Text"));
		}
	}
	TestEqual(TEXT("the block's child went under its row"), Rows[1]->GetChildren().Num(), 1);
	return true;
}

#endif
