// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Text/DreamUIAst.h"
#include "Text/DreamUIDiagnostics.h"
#include "Text/DreamUISourceFile.h"
#include "Text/DreamUITextPatcher.h"

/*
 * What the designer can write back into a `rows` table, and what it refuses.
 *
 * A row's "header" is a run of values, so the one edit with a place to land is a column's value -- replaced in its cell,
 * the rest of the line untouched -- or a property the row's own block spells. Everything else (a value outside the
 * columns, a `+` block, removing, moving, naming a row, a node placed beside the table) would have to invent a line
 * inside a table, and is refused with the reason, leaving the text as it was.
 */

namespace DreamUIRowsWriteBackTestLocal
{
	const TCHAR* const Source =
		TEXT("VerticalBox Page {\n")
		TEXT("    rows Text : Caption (Text, FontSize, RenderTranslation) {\n")
		TEXT("        \"Save\", 24, (0, 0)\n")
		TEXT("        \"Load\", 18, (4, 2) { HAlign = Right }\n")
		TEXT("    }\n")
		TEXT("}\n")
		TEXT("style Caption { FontSize = 12 }\n");

	bool ParseInto(const FString& InText, FDreamUIAst& OutAst)
	{
		FDreamUIDiagnosticBag Diagnostics;
		return FDreamUISourceFile::Parse(InText, TEXT("/virtual/RowsWriteBack.dui"), OutAst, Diagnostics);
	}

	bool Reported(const FDreamUIDiagnosticBag& InDiagnostics, EDreamUIDiagnosticCode InCode)
	{
		return InDiagnostics.Diagnostics.ContainsByPredicate([InCode](const FDreamUIDiagnostic& InDiagnostic)
		{
			return InDiagnostic.Code == InCode;
		});
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIRowsCellWriteBackTest,
	"DreamGUI.Text.Syntax.ARowsCellIsWrittenBackWhereItStands",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIRowsCellWriteBackTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIRowsWriteBackTestLocal;

	FDreamUIAst Ast;
	if (!TestTrue(TEXT("the fixture parses"), ParseInto(Source, Ast)))
	{
		return false;
	}

	{
		FString Text = Source;
		FDreamUIDiagnosticBag Diagnostics;
		const bool bWritten = FDreamUITextPatcher::SetProperty(Text, Ast, TEXT("Page__Text_Load"), EDreamUIPatchTarget::Node,
			INDEX_NONE, TEXT("FontSize"), TEXT("20"), Diagnostics);
		TestTrue(TEXT("a number cell is written"), bWritten);
		TestTrue(TEXT("in its cell, the rest of the line as it was"),
			Text.Contains(TEXT("        \"Load\", 20, (4, 2) { HAlign = Right }\n")));
		TestTrue(TEXT("and the other row untouched"), Text.Contains(TEXT("        \"Save\", 24, (0, 0)\n")));
	}
	{
		FString Text = Source;
		FDreamUIDiagnosticBag Diagnostics;
		TestTrue(TEXT("a tuple cell is written"), FDreamUITextPatcher::SetProperty(Text, Ast, TEXT("Page__Text_Save"),
			EDreamUIPatchTarget::Node, INDEX_NONE, TEXT("RenderTranslation"), TEXT("(8, -3)"), Diagnostics));
		TestTrue(TEXT("whole, parentheses and all"), Text.Contains(TEXT("        \"Save\", 24, (8, -3)\n")));
	}
	{
		// The key cell too: the row is renamed by it on the next parse, which is what a key column means.
		FString Text = Source;
		FDreamUIDiagnosticBag Diagnostics;
		TestTrue(TEXT("the key cell is written"), FDreamUITextPatcher::SetProperty(Text, Ast, TEXT("Page__Text_Save"),
			EDreamUIPatchTarget::Node, INDEX_NONE, TEXT("Text"), TEXT("\"Save Game\""), Diagnostics));
		TestTrue(TEXT("with its new value"), Text.Contains(TEXT("        \"Save Game\", 24, (0, 0)\n")));
		FDreamUIAst Reparsed;
		if (TestTrue(TEXT("the result parses"), ParseInto(Text, Reparsed)) && Reparsed.Root.Children.Num() == 2)
		{
			TestEqual(TEXT("and names the row from its new key"), Reparsed.Root.Children[0].Id, FString(TEXT("Page__Text_Save_Game")));
		}
	}
	{
		// A property the row's own block spells is replaced where it stands, like any line.
		FString Text = Source;
		FDreamUIDiagnosticBag Diagnostics;
		TestTrue(TEXT("a line of the row's block is written"), FDreamUITextPatcher::SetProperty(Text, Ast, TEXT("Page__Text_Load"),
			EDreamUIPatchTarget::Node, INDEX_NONE, TEXT("HAlign"), TEXT("Center"), Diagnostics));
		TestTrue(TEXT("in the block"), Text.Contains(TEXT("        \"Load\", 18, (4, 2) { HAlign = Center }\n")));
	}
	{
		// The same value as the text already holds: nothing to write, so the file stays byte for byte.
		FString Text = Source;
		FDreamUIDiagnosticBag Diagnostics;
		TestTrue(TEXT("an unchanged cell is accepted"), FDreamUITextPatcher::SetProperty(Text, Ast, TEXT("Page__Text_Load"),
			EDreamUIPatchTarget::Node, INDEX_NONE, TEXT("FontSize"), TEXT("18"), Diagnostics));
		TestEqual(TEXT("and changes nothing"), Text, FString(Source));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIRowsRefusedWriteBackTest,
	"DreamGUI.Text.Syntax.WhatARowsLineCannotSpellIsRefusedNotInvented",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIRowsRefusedWriteBackTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIRowsWriteBackTestLocal;

	FDreamUIAst Ast;
	if (!TestTrue(TEXT("the fixture parses"), ParseInto(Source, Ast)))
	{
		return false;
	}

	{
		FString Text = Source;
		FDreamUIDiagnosticBag Diagnostics;
		TestFalse(TEXT("a value outside the columns is refused"), FDreamUITextPatcher::SetProperty(Text, Ast,
			TEXT("Page__Text_Save"), EDreamUIPatchTarget::Node, INDEX_NONE, TEXT("HAlign"), TEXT("Right"), Diagnostics));
		TestTrue(TEXT("as a syntax the write-back cannot spell"), Reported(Diagnostics, EDreamUIDiagnosticCode::PatchSyntaxNotWritable));
		TestEqual(TEXT("the text untouched"), Text, FString(Source));
	}
	{
		FString Text = Source;
		FDreamUIDiagnosticBag Diagnostics;
		TestFalse(TEXT("a slot line is refused"), FDreamUITextPatcher::SetProperty(Text, Ast,
			TEXT("Page__Text_Save"), EDreamUIPatchTarget::Slot, INDEX_NONE, TEXT("Padding"), TEXT("(0, 4, 0, 0)"), Diagnostics));
		TestTrue(TEXT("as well"), Reported(Diagnostics, EDreamUIDiagnosticCode::PatchSyntaxNotWritable));
		TestEqual(TEXT("the text untouched"), Text, FString(Source));
	}

	auto Structural = [this, &Ast](const TCHAR* InWhat, const FDreamUIStructuralEdit& InEdit, EDreamUIDiagnosticCode InCode)
	{
		FString Text = Source;
		FDreamUIDiagnosticBag Diagnostics;
		TestFalse(FString(InWhat) + TEXT(" is refused"), FDreamUITextPatcher::ApplyStructuralEdits(Text, Ast,
			TArrayView<const FDreamUIStructuralEdit>(&InEdit, 1), Diagnostics));
		TestTrue(FString(InWhat) + TEXT(", with the reason"), Reported(Diagnostics, InCode));
		TestEqual(FString(InWhat) + TEXT(": the text untouched"), Text, FString(Source));
	};

	FDreamUIStructuralEdit Remove;
	Remove.Kind = EDreamUIStructuralEditKind::RemoveNode;
	Remove.NodeId = TEXT("Page__Text_Save");
	Structural(TEXT("removing a row"), Remove, EDreamUIDiagnosticCode::PatchSyntaxNotWritable);

	FDreamUIStructuralEdit Rename;
	Rename.Kind = EDreamUIStructuralEditKind::RenameNode;
	Rename.NodeId = TEXT("Page__Text_Load");
	Rename.NewId = TEXT("LoadRow");
	Structural(TEXT("naming a row"), Rename, EDreamUIDiagnosticCode::PatchSyntaxNotWritable);

	FDreamUIStructuralEdit Move;
	Move.Kind = EDreamUIStructuralEditKind::MoveNode;
	Move.NodeId = TEXT("Page__Text_Load");
	Move.ParentId = TEXT("Page");
	Move.ChildIndex = 0;
	Structural(TEXT("moving a row"), Move, EDreamUIDiagnosticCode::PatchSyntaxNotWritable);

	FDreamUIStructuralEdit Component;
	Component.Kind = EDreamUIStructuralEditKind::InsertComponent;
	Component.NodeId = TEXT("Page__Text_Save");
	Component.ComponentClassName = TEXT("UIButton");
	Structural(TEXT("a '+' block on a row"), Component, EDreamUIDiagnosticCode::PatchSyntaxNotWritable);

	FDreamUIStructuralEdit Insert;
	Insert.Kind = EDreamUIStructuralEditKind::InsertNode;
	Insert.ParentId = TEXT("Page");
	Insert.TypeName = TEXT("Text");
	Insert.NewId = TEXT("Added");
	Structural(TEXT("a node beside the table"), Insert, EDreamUIDiagnosticCode::PatchTargetNotFound);
	return true;
}

#endif
