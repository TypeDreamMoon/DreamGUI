// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Text/DreamUISourceFile.h"
#include "Text/DreamUITextPatcher.h"

/*
 * The write-back half of the text pipeline: a designer edit turning into a changed line of .dui.
 *
 * Almost every assertion here compares the WHOLE FILE, byte for byte, and that is the point of the
 * file rather than an excess of rigour. What this component is for is leaving everything it did not
 * edit alone -- the author's alignment, their trailing comments, their blank lines, their choice of
 * line ending -- and an assertion that only looks at the edited line cannot see any of that going
 * wrong. It would pass just as happily on a patcher that reprinted the file from the tree, which is
 * exactly the design that was rejected: every save would be a whole-file diff and no .dui change
 * would ever be reviewable again.
 *
 * Two properties get their own tests because they are the ones a broken write-back is noticed by,
 * long after the fact:
 *
 *   IDEMPOTENCE. Writing the same value twice has to produce the same bytes. The designer flushes
 *   on every gesture end and on every focus change, so a patcher that is off by a space somewhere
 *   turns an idle editor session into a stream of diffs.
 *
 *   THE ROUND TRIP. What was written has to parse back as the value that was written. A patcher and
 *   a parser that disagree produce a file that saves and does not reopen, and the two halves are in
 *   different modules, so nothing else in the suite would catch it.
 *
 * The fixtures are written a line at a time and joined, so a test can say "line 8" and mean the
 * eighth entry, and so an expected file can be written as the input with one line different -- which
 * is what a reader actually wants to check.
 */

namespace DreamUIPatcherTestLocal
{
	FString MakeSource(const TArray<FString>& InLines)
	{
		return FString::Join(InLines, TEXT("\n"));
	}

	/** The reference file, in the shape the language's design note uses. Line numbers are the array's. */
	TArray<FString> SavePanelLines()
	{
		return {
			TEXT("// UI/SavePanel.dui"),                    //  1
			TEXT("class /Game/UI/WBP_SavePanel"),           //  2
			TEXT(""),                                       //  3
			TEXT("Widget Root {"),                          //  4
			TEXT("    AnchorData.SizeDelta = (400, 240)"),  //  5
			TEXT(""),                                       //  6
			TEXT("    Text Title {"),                       //  7
			TEXT("        FontSize   = 24   // 标题"),      //  8
			TEXT("        HAlign     = Left"),              //  9
			TEXT("    }"),                                  // 10
			TEXT(""),                                       // 11
			TEXT("    Widget OkBtn {"),                     // 12
			TEXT("        + UIButton {"),                   // 13
			TEXT("            NormalColor = #FFFFFF"),      // 14
			TEXT("        }"),                              // 15
			TEXT(""),                                       // 16
			TEXT("        Text OkText"),                    // 17
			TEXT("    }"),                                  // 18
			TEXT(""),                                       // 19
			TEXT("    Image Bg {"),                         // 20
			TEXT("    }"),                                  // 21
			TEXT(""),                                       // 22
			TEXT("    slot Footer"),                        // 23
			TEXT("}")                                       // 24
		};
	}

	FString SavePanel()
	{
		return MakeSource(SavePanelLines());
	}

	/** The reference file with one line replaced, which is what most expected results are. */
	FString SavePanelWith(int32 InOneBasedLine, const FString& InReplacement)
	{
		TArray<FString> Lines = SavePanelLines();
		Lines[InOneBasedLine - 1] = InReplacement;
		return MakeSource(Lines);
	}

	/** The reference file with lines inserted after one of them. */
	FString SavePanelInserting(int32 InAfterOneBasedLine, const TArray<FString>& InNewLines)
	{
		TArray<FString> Lines = SavePanelLines();
		Lines.Insert(InNewLines, InAfterOneBasedLine);
		return MakeSource(Lines);
	}

	/**
	 * A layout with the two notations the reference file has no room for, plus the two shapes that
	 * are awkward to edit: a block written entirely on one line, and a node id that is not ASCII.
	 * Both are legal, both occur, and both are places a naive patcher goes wrong -- the one-line
	 * block because the end of the line is outside it, and the CJK id because a column is a count of
	 * characters and not of bytes.
	 */
	TArray<FString> ListLines()
	{
		return {
			TEXT("Widget List {"),                                    // 1
			TEXT("    + LayoutContainerHorizontalBox { Spacing = 8 }"),// 2
			TEXT(""),                                                 // 3
			TEXT("    /Game/UI/WBP_SlotCard Card1 {"),                // 4
			TEXT("        @slot FillWeight = 1"),                     // 5
			TEXT("    }"),                                            // 6
			TEXT(""),                                                 // 7
			TEXT("    Text 确定按钮 { FontSize = 24 }"),              // 8
			TEXT("}")                                                 // 9
		};
	}

	FString List()
	{
		return MakeSource(ListLines());
	}

	FString ListWith(int32 InOneBasedLine, const FString& InReplacement)
	{
		TArray<FString> Lines = ListLines();
		Lines[InOneBasedLine - 1] = InReplacement;
		return MakeSource(Lines);
	}

	FString ListInserting(int32 InAfterOneBasedLine, const TArray<FString>& InNewLines)
	{
		TArray<FString> Lines = ListLines();
		Lines.Insert(InNewLines, InAfterOneBasedLine);
		return MakeSource(Lines);
	}

	bool Parse(const FString& InText, FDreamUIAst& OutAst, FDreamUIDiagnosticBag& OutDiagnostics)
	{
		return FDreamUISourceFile::Parse(InText, TEXT("Fixture.dui"), OutAst, OutDiagnostics);
	}

	/**
	 * Parse and patch in one step, which is the only correct way to call the patcher twice.
	 *
	 * Every location in the tree describes the text as it was parsed, so a second edit needs a
	 * second parse -- the alternative is SetProperties, which measures a whole batch against one
	 * state on purpose. A test helper that held one tree across two edits would be testing
	 * something no caller is allowed to do.
	 */
	bool Patch(FString& InOutText, const TCHAR* InNodeId, EDreamUIPatchTarget InTarget, int32 InComponentIndex,
		const TCHAR* InPropertyName, const TCHAR* InValue, FDreamUIDiagnosticBag& OutDiagnostics)
	{
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag ParseDiagnostics;
		if (!Parse(InOutText, Ast, ParseDiagnostics))
		{
			return false;
		}
		return FDreamUITextPatcher::SetProperty(InOutText, Ast, InNodeId, InTarget, InComponentIndex,
			InPropertyName, InValue, OutDiagnostics);
	}

	/** The same, for the ordinary case where the caller does not care about the diagnostics. */
	bool Patch(FString& InOutText, const TCHAR* InNodeId, EDreamUIPatchTarget InTarget, int32 InComponentIndex,
		const TCHAR* InPropertyName, const TCHAR* InValue)
	{
		FDreamUIDiagnosticBag Diagnostics;
		return Patch(InOutText, InNodeId, InTarget, InComponentIndex, InPropertyName, InValue, Diagnostics);
	}

	bool Reported(const FDreamUIDiagnosticBag& InDiagnostics, EDreamUIDiagnosticCode InCode)
	{
		return InDiagnostics.Diagnostics.ContainsByPredicate([InCode](const FDreamUIDiagnostic& InDiagnostic)
		{
			return InDiagnostic.Code == InCode;
		});
	}

	const FDreamUINode* FindNode(const FDreamUIAst& InAst, const TCHAR* InId)
	{
		const FDreamUINode* Found = nullptr;
		InAst.ForEachNode([&Found, InId](const FDreamUINode& InNode)
		{
			if (Found == nullptr && InNode.Id == InId)
			{
				Found = &InNode;
			}
		});
		return Found;
	}

	const FDreamUIProperty* FindProperty(const TArray<FDreamUIProperty>& InProperties, const TCHAR* InName)
	{
		return InProperties.FindByPredicate([InName](const FDreamUIProperty& InProperty)
		{
			return InProperty.Name == InName;
		});
	}

	/**
	 * A refusal is only correct if it left the file alone.
	 *
	 * Asserted on every negative case, because a half applied edit is worse than a rejected one: the
	 * caller is told nothing was written and the file says otherwise.
	 */
	bool ExpectRefusal(FAutomationTestBase& InTest, const TCHAR* InWhat, const FString& InOriginal,
		const FString& InAfter, bool bInReturned, const FDreamUIDiagnosticBag& InDiagnostics,
		EDreamUIDiagnosticCode InCode)
	{
		bool bPassed = InTest.TestFalse(*FString::Printf(TEXT("%s is refused"), InWhat), bInReturned);
		bPassed &= InTest.TestEqual(*FString::Printf(TEXT("%s leaves the file exactly as it was"), InWhat),
			InAfter, InOriginal);
		bPassed &= InTest.TestTrue(*FString::Printf(TEXT("%s is reported under the expected code"), InWhat),
			Reported(InDiagnostics, InCode));
		return bPassed;
	}
}

// ------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIPatcherKeepsFormattingTest,
	"DreamGUI.Text.AnEditedPropertyKeepsTheAuthorsSpacingAndTrailingComment",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIPatcherKeepsFormattingTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIPatcherTestLocal;

	// Line 8 is deliberately the ugly case: the name is padded out to align with its neighbour, the
	// value is padded again, and a comment follows in a script that is not ASCII. All three survive
	// because only the two characters of the value are inside the edited range.
	FString Text = SavePanel();
	TestTrue(TEXT("the value is written"), Patch(Text, TEXT("Title"), EDreamUIPatchTarget::Node, INDEX_NONE,
		TEXT("FontSize"), TEXT("26")));
	TestEqual(TEXT("and nothing else on the line moved"), Text,
		SavePanelWith(8, TEXT("        FontSize   = 26   // 标题")));

	// A tuple is replaced whole, parentheses included, and the author's spacing inside the ones that
	// were not edited is not renormalised -- which is why FDreamUIValue keeps the raw slice.
	Text = SavePanel();
	TestTrue(TEXT("a tuple is written"), Patch(Text, TEXT("Root"), EDreamUIPatchTarget::Node, INDEX_NONE,
		TEXT("AnchorData.SizeDelta"), TEXT("(160, 48)")));
	TestEqual(TEXT("over exactly the old tuple"), Text,
		SavePanelWith(5, TEXT("    AnchorData.SizeDelta = (160, 48)")));

	// A dotted path is one name, so the second segment is not mistaken for a property of its own.
	Text = SavePanel();
	TestTrue(TEXT("a dotted name is matched whole"), Patch(Text, TEXT("Root"), EDreamUIPatchTarget::Node,
		INDEX_NONE, TEXT("AnchorData.AnchorMax"), TEXT("(1, 1)")));
	TestEqual(TEXT("so a path the file does not have is inserted, not written over its neighbour"), Text,
		SavePanelInserting(5, { TEXT("    AnchorData.AnchorMax = (1, 1)") }));

	// Names are matched the way FName matches them, because that is what the id and the property
	// name both become downstream. Refusing a differently cased name would insert a second line
	// naming the same property, and the file would then say it twice.
	Text = SavePanel();
	TestTrue(TEXT("a differently cased node id and property name still find their line"),
		Patch(Text, TEXT("title"), EDreamUIPatchTarget::Node, INDEX_NONE, TEXT("fontsize"), TEXT("26")));
	TestEqual(TEXT("and the author's spelling of the name is left as they wrote it"), Text,
		SavePanelWith(8, TEXT("        FontSize   = 26   // 标题")));

	return true;
}

// ------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIPatcherInsertsMissingPropertyTest,
	"DreamGUI.Text.APropertyTheFileNeverWroteIsInsertedAfterTheLastOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIPatcherInsertsMissingPropertyTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIPatcherTestLocal;

	// The operation this whole grammar was chosen for. Every anchor drag performs it, because the
	// file does not say what the designer has not changed yet.
	FString Text = SavePanel();
	TestTrue(TEXT("an unwritten property is written"), Patch(Text, TEXT("Title"), EDreamUIPatchTarget::Node,
		INDEX_NONE, TEXT("AnchorData.AnchorMin"), TEXT("(0, 0)")));
	TestEqual(TEXT("after the last property, indented like it"), Text,
		SavePanelInserting(9, { TEXT("        AnchorData.AnchorMin = (0, 0)") }));

	// Properties before the subtree. The root's block holds one property, then a blank line, then
	// four children -- and the new line goes with the property, not in among the nodes. A file where
	// the two are interleaved is one nobody can read the shape of.
	Text = SavePanel();
	TestTrue(TEXT("a property on a node with children is written"), Patch(Text, TEXT("Root"),
		EDreamUIPatchTarget::Node, INDEX_NONE, TEXT("RenderOpacity"), TEXT("0.95")));
	TestEqual(TEXT("above the children, not below them"), Text,
		SavePanelInserting(5, { TEXT("    RenderOpacity = 0.95") }));

	return true;
}

// ------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIPatcherGrowsBlocksTest,
	"DreamGUI.Text.AnEmptyBlockAndABlocklessNodeBothTakeTheirFirstProperty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIPatcherGrowsBlocksTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIPatcherTestLocal;

	// An empty block has no line of its own to copy an indentation from, so one level in from the
	// node is the answer -- one level being whatever this file already uses, not four spaces because
	// four spaces is what the sample happens to use.
	FString Text = SavePanel();
	TestTrue(TEXT("an empty block takes a property"), Patch(Text, TEXT("Bg"), EDreamUIPatchTarget::Node,
		INDEX_NONE, TEXT("Brush.TintColor"), TEXT("#1E1E1E")));
	TestEqual(TEXT("indented one level in from the node that owns it"), Text,
		SavePanelInserting(20, { TEXT("        Brush.TintColor = #1E1E1E") }));

	// A node with no block at all. Not a rare shape -- it is what every node with nothing to say
	// looks like, and the designer's first edit to one is what creates the block. The brace goes on
	// the header line because that is where the grammar requires it: the parser checks for it
	// immediately after the header, with no separator skipping, so a brace on the next line would be
	// a syntax error rather than this node's block.
	Text = SavePanel();
	TestTrue(TEXT("a node with no block grows one"), Patch(Text, TEXT("OkText"), EDreamUIPatchTarget::Node,
		INDEX_NONE, TEXT("Text"), TEXT("\"OK\"")));
	TestEqual(TEXT("with the brace on the header line and the closing one lined up under it"), Text,
		MakeSource({
			TEXT("// UI/SavePanel.dui"),
			TEXT("class /Game/UI/WBP_SavePanel"),
			TEXT(""),
			TEXT("Widget Root {"),
			TEXT("    AnchorData.SizeDelta = (400, 240)"),
			TEXT(""),
			TEXT("    Text Title {"),
			TEXT("        FontSize   = 24   // 标题"),
			TEXT("        HAlign     = Left"),
			TEXT("    }"),
			TEXT(""),
			TEXT("    Widget OkBtn {"),
			TEXT("        + UIButton {"),
			TEXT("            NormalColor = #FFFFFF"),
			TEXT("        }"),
			TEXT(""),
			TEXT("        Text OkText {"),
			TEXT("            Text = \"OK\""),
			TEXT("        }"),
			TEXT("    }"),
			TEXT(""),
			TEXT("    Image Bg {"),
			TEXT("    }"),
			TEXT(""),
			TEXT("    slot Footer"),
			TEXT("}")
		}));

	// And the file it produced is a file the front end reads back, which is the only thing that
	// makes creating a block acceptable at all.
	FDreamUIAst Ast;
	FDreamUIDiagnosticBag Diagnostics;
	TestTrue(TEXT("the grown block parses"), Parse(Text, Ast, Diagnostics));
	TestEqual(TEXT("with nothing to complain about"), Diagnostics.Diagnostics.Num(), 0);

	return true;
}

// ------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIPatcherDispatchesByNotationTest,
	"DreamGUI.Text.TheThreeNotationsWriteToTheirOwnTargets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIPatcherDispatchesByNotationTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIPatcherTestLocal;

	// @slot, replaced. The statement starts at the '@', not at the name, and the value is still the
	// only thing inside the edited range.
	FString Text = List();
	TestTrue(TEXT("a slot property is written"), Patch(Text, TEXT("Card1"), EDreamUIPatchTarget::Slot,
		INDEX_NONE, TEXT("FillWeight"), TEXT("2")));
	TestEqual(TEXT("on the @slot line"), Text, ListWith(5, TEXT("        @slot FillWeight = 2")));

	// @slot, inserted. The notation is written back in front of the name, because the notation is
	// what decides the destination object -- a bare Padding and an @slot Padding are two properties
	// on two objects.
	Text = List();
	TestTrue(TEXT("an unwritten slot property is written"), Patch(Text, TEXT("Card1"),
		EDreamUIPatchTarget::Slot, INDEX_NONE, TEXT("Padding"), TEXT("(4, 4, 4, 4)")));
	TestEqual(TEXT("with its @slot marker"), Text,
		ListInserting(5, { TEXT("        @slot Padding = (4, 4, 4, 4)") }));

	// The same name on the same node, written the other way, has to reach the other object. This is
	// the case that a patcher resolving by name alone gets wrong, and it gets it wrong silently.
	Text = List();
	TestTrue(TEXT("the same name written bare is a different property"), Patch(Text, TEXT("Card1"),
		EDreamUIPatchTarget::Node, INDEX_NONE, TEXT("FillWeight"), TEXT("7")));
	TestEqual(TEXT("so it is inserted rather than overwriting the slot's"), Text,
		ListInserting(5, { TEXT("        FillWeight = 7") }));

	// A '+' block, addressed by the position it is written in.
	Text = SavePanel();
	TestTrue(TEXT("a behaviour property is written"), Patch(Text, TEXT("OkBtn"), EDreamUIPatchTarget::Component,
		0, TEXT("NormalColor"), TEXT("#C8C8C8")));
	TestEqual(TEXT("inside the '+' block"), Text, SavePanelWith(14, TEXT("            NormalColor = #C8C8C8")));

	Text = SavePanel();
	TestTrue(TEXT("an unwritten behaviour property is written"), Patch(Text, TEXT("OkBtn"),
		EDreamUIPatchTarget::Component, 0, TEXT("HoveredColor"), TEXT("#808080")));
	TestEqual(TEXT("after the last one in the same block"), Text,
		SavePanelInserting(14, { TEXT("            HoveredColor = #808080") }));

	return true;
}

// ------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIPatcherOneLineBlockTest,
	"DreamGUI.Text.ABlockWrittenOnOneLineStaysBalancedWhenALineIsAdded",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIPatcherOneLineBlockTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIPatcherTestLocal;

	// The trap: for a block written on one line, the end of the line is OUTSIDE the block. "Insert
	// after the last property" would put the new property after the closing brace, where it reads as
	// a property of the PARENT -- a silently different file that still parses.
	FString Text = List();
	TestTrue(TEXT("a one line behaviour block takes another property"), Patch(Text, TEXT("List"),
		EDreamUIPatchTarget::Component, 0, TEXT("Padding"), TEXT("(8, 8, 8, 8)")));
	TestEqual(TEXT("in front of the brace, which moves to its own line"), Text,
		MakeSource({
			TEXT("Widget List {"),
			TEXT("    + LayoutContainerHorizontalBox { Spacing = 8"),
			TEXT("        Padding = (8, 8, 8, 8)"),
			TEXT("    }"),
			TEXT(""),
			TEXT("    /Game/UI/WBP_SlotCard Card1 {"),
			TEXT("        @slot FillWeight = 1"),
			TEXT("    }"),
			TEXT(""),
			TEXT("    Text 确定按钮 { FontSize = 24 }"),
			TEXT("}")
		}));

	// A column is a count of characters, which is what the lexer counted. A CJK id is four
	// characters and twelve UTF-8 bytes, so a patcher that thought in bytes would land eight
	// characters past the value on this line -- and the runtime supports CJK ids on purpose, so this
	// is a shape real files have.
	Text = List();
	TestTrue(TEXT("a property after a non-ASCII id is found"), Patch(Text, TEXT("确定按钮"),
		EDreamUIPatchTarget::Node, INDEX_NONE, TEXT("FontSize"), TEXT("26")));
	TestEqual(TEXT("and only its value replaced"), Text, ListWith(8, TEXT("    Text 确定按钮 { FontSize = 26 }")));

	Text = List();
	TestTrue(TEXT("and a new property goes inside its braces"), Patch(Text, TEXT("确定按钮"),
		EDreamUIPatchTarget::Node, INDEX_NONE, TEXT("HAlign"), TEXT("Left")));
	TestEqual(TEXT("one level in from the node, not level with it"), Text,
		MakeSource({
			TEXT("Widget List {"),
			TEXT("    + LayoutContainerHorizontalBox { Spacing = 8 }"),
			TEXT(""),
			TEXT("    /Game/UI/WBP_SlotCard Card1 {"),
			TEXT("        @slot FillWeight = 1"),
			TEXT("    }"),
			TEXT(""),
			TEXT("    Text 确定按钮 { FontSize = 24"),
			TEXT("        HAlign = Left"),
			TEXT("    }"),
			TEXT("}")
		}));

	FDreamUIAst Ast;
	FDreamUIDiagnosticBag Diagnostics;
	TestTrue(TEXT("the split block parses"), Parse(Text, Ast, Diagnostics));
	TestEqual(TEXT("with nothing to complain about"), Diagnostics.Diagnostics.Num(), 0);

	return true;
}

// ------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIPatcherLineEndingsTest,
	"DreamGUI.Text.TheFileKeepsTheLineEndingsAndTheEndingItArrivedWith",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIPatcherLineEndingsTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIPatcherTestLocal;

	auto CountOf = [](const FString& InText, const TCHAR* InNeedle)
	{
		int32 Count = 0;
		int32 From = 0;
		int32 At = INDEX_NONE;
		while ((At = InText.Find(InNeedle, ESearchCase::CaseSensitive, ESearchDir::FromStart, From)) != INDEX_NONE)
		{
			++Count;
			From = At + FCString::Strlen(InNeedle);
		}
		return Count;
	};

	// A single LF inserted into a CRLF file is invisible in every editor and turns the next diff into
	// a whole-file rewrite for anybody with autocrlf on. One property changed, four hundred lines
	// modified, and a review nobody can read.
	const FString CrLfSource = SavePanel().Replace(TEXT("\n"), TEXT("\r\n"));
	FString Text = CrLfSource;
	TestTrue(TEXT("a CRLF file takes an inserted line"), Patch(Text, TEXT("Title"), EDreamUIPatchTarget::Node,
		INDEX_NONE, TEXT("AnchorData.AnchorMin"), TEXT("(0, 0)")));
	TestEqual(TEXT("and the inserted line ends the way the file does"), Text,
		SavePanelInserting(9, { TEXT("        AnchorData.AnchorMin = (0, 0)") }).Replace(TEXT("\n"), TEXT("\r\n")));
	TestEqual(TEXT("so the file has no lone line feed left in it"),
		CountOf(Text, TEXT("\n")), CountOf(Text, TEXT("\r\n")));

	// The same edit on the LF file must not pick up a carriage return from anywhere.
	Text = SavePanel();
	TestTrue(TEXT("an LF file takes an inserted line"), Patch(Text, TEXT("Title"), EDreamUIPatchTarget::Node,
		INDEX_NONE, TEXT("AnchorData.AnchorMin"), TEXT("(0, 0)")));
	TestEqual(TEXT("with no carriage returns anywhere in the result"), CountOf(Text, TEXT("\r")), 0);

	// The last byte of the file is the author's business. A patcher that helpfully appends a newline
	// makes one diff hunk at the end of every file it ever touches; one that removes one makes the
	// opposite. Neither is an edit anybody asked for, so the tail is simply never in range.
	Text = SavePanel();
	TestTrue(TEXT("a file with no trailing newline is edited"), Patch(Text, TEXT("Bg"),
		EDreamUIPatchTarget::Node, INDEX_NONE, TEXT("Brush.TintColor"), TEXT("#1E1E1E")));
	TestFalse(TEXT("and still has no trailing newline"), Text.EndsWith(TEXT("\n"), ESearchCase::CaseSensitive));

	Text = SavePanel() + TEXT("\n");
	TestTrue(TEXT("a file with a trailing newline is edited"), Patch(Text, TEXT("Bg"),
		EDreamUIPatchTarget::Node, INDEX_NONE, TEXT("Brush.TintColor"), TEXT("#1E1E1E")));
	TestTrue(TEXT("and still has exactly the one"), Text.EndsWith(TEXT("}\n"), ESearchCase::CaseSensitive));

	return true;
}

// ------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIPatcherRefusesUnlocatableTest,
	"DreamGUI.Text.AnEditWithNoHomeInTheFileIsRefusedRatherThanGuessed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIPatcherRefusesUnlocatableTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIPatcherTestLocal;

	const FString Original = SavePanel();

	{
		FString Text = Original;
		FDreamUIDiagnosticBag Diagnostics;
		const bool bWritten = Patch(Text, TEXT("Nope"), EDreamUIPatchTarget::Node, INDEX_NONE,
			TEXT("FontSize"), TEXT("26"), Diagnostics);
		ExpectRefusal(*this, TEXT("an id the file does not declare"), Original, Text, bWritten,
			Diagnostics, EDreamUIDiagnosticCode::PatchTargetNotFound);
	}

	{
		FString Text = Original;
		FDreamUIDiagnosticBag Diagnostics;
		const bool bWritten = Patch(Text, TEXT("OkBtn"), EDreamUIPatchTarget::Component, 3,
			TEXT("NormalColor"), TEXT("#000000"), Diagnostics);
		ExpectRefusal(*this, TEXT("a '+' block the node does not have"), Original, Text, bWritten,
			Diagnostics, EDreamUIDiagnosticCode::PatchTargetNotFound);
	}

	{
		// A named slot declares a hole and the grammar refuses it a block, so there is nowhere in the
		// file for a property to go. Writing one anyway would produce a .dui that no longer parses,
		// which is the one outcome this component is never allowed to have.
		FString Text = Original;
		FDreamUIDiagnosticBag Diagnostics;
		const bool bWritten = Patch(Text, TEXT("Footer"), EDreamUIPatchTarget::Node, INDEX_NONE,
			TEXT("RenderOpacity"), TEXT("0.5"), Diagnostics);
		ExpectRefusal(*this, TEXT("a property on a named slot"), Original, Text, bWritten,
			Diagnostics, EDreamUIDiagnosticCode::PatchTargetNotFound);
	}

	{
		// The details panel shows a bound property's current value like any other, so without this
		// one drag would replace authored behaviour with a number the binding overwrites on the next
		// tick. The refusal puts the decision back in the text, where the arrow is visible.
		const FString Bound = MakeSource({
			TEXT("Widget Root {"),
			TEXT("    Text Title {"),
			TEXT("        Text <- GetTitleText()"),
			TEXT("    }"),
			TEXT("}")
		});
		FString Text = Bound;
		FDreamUIDiagnosticBag Diagnostics;
		const bool bWritten = Patch(Text, TEXT("Title"), EDreamUIPatchTarget::Node, INDEX_NONE,
			TEXT("Text"), TEXT("\"Save\""), Diagnostics);
		ExpectRefusal(*this, TEXT("a value written over a binding"), Bound, Text, bWritten,
			Diagnostics, EDreamUIDiagnosticCode::PatchTargetNotFound);
	}

	return true;
}

// ------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIPatcherRefusesStaleTreeTest,
	"DreamGUI.Text.ATreeThatNoLongerDescribesTheTextIsRefused",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIPatcherRefusesStaleTreeTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIPatcherTestLocal;

	// The failure this guard exists for: a caller holding a tree from before somebody else changed
	// the file. Every location is still a perfectly valid line and column -- they just point at the
	// wrong line now, and the edit would land on a property nobody touched, in a file the author has
	// open. Cheap to catch (the text under a location has to start with the name the tree says is
	// there) and catastrophic to miss.
	FDreamUIAst Ast;
	FDreamUIDiagnosticBag ParseDiagnostics;
	TestTrue(TEXT("the file parses"), Parse(SavePanel(), Ast, ParseDiagnostics));

	const FString Moved = TEXT("// a line somebody else added\n") + SavePanel();
	FString Text = Moved;
	FDreamUIDiagnosticBag Diagnostics;
	const bool bWritten = FDreamUITextPatcher::SetProperty(Text, Ast, TEXT("Title"),
		EDreamUIPatchTarget::Node, INDEX_NONE, TEXT("FontSize"), TEXT("26"), Diagnostics);
	ExpectRefusal(*this, TEXT("an edit against a tree parsed from older text"), Moved, Text, bWritten,
		Diagnostics, EDreamUIDiagnosticCode::SourceFileChangedUnderEdit);

	// A tree that never parsed at all is the same problem in its most obvious form.
	FDreamUIAst Empty;
	FString Broken = TEXT("Widget Root {");
	const FString BrokenOriginal = Broken;
	FDreamUIDiagnosticBag EmptyDiagnostics;
	const bool bWrittenToEmpty = FDreamUITextPatcher::SetProperty(Broken, Empty, TEXT("Root"),
		EDreamUIPatchTarget::Node, INDEX_NONE, TEXT("RenderOpacity"), TEXT("0.5"), EmptyDiagnostics);
	ExpectRefusal(*this, TEXT("an edit against a tree with no root"), BrokenOriginal, Broken,
		bWrittenToEmpty, EmptyDiagnostics, EDreamUIDiagnosticCode::PatchTargetNotFound);

	return true;
}

// ------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIPatcherRefusesUnreadableValueTest,
	"DreamGUI.Text.AValueThatWouldNotSurviveBeingReadBackIsRefused",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIPatcherRefusesUnreadableValueTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIPatcherTestLocal;

	const FString Original = SavePanel();

	{
		// The one the implementation plan flagged before write-back existed. A float holding infinity
		// prints as `inf`, which the lexer reads as an IDENTIFIER -- so the file saves and then fails
		// to reopen, on a line the author never wrote. Refusing at the write is the option that does
		// not put two floating point spellings into a layout language.
		FString Text = Original;
		FDreamUIDiagnosticBag Diagnostics;
		const bool bWritten = Patch(Text, TEXT("Title"), EDreamUIPatchTarget::Node, INDEX_NONE,
			TEXT("FontSize"), TEXT("inf"), Diagnostics);
		ExpectRefusal(*this, TEXT("a non-finite number"), Original, Text, bWritten,
			Diagnostics, EDreamUIDiagnosticCode::MalformedNumber);
	}

	{
		// Parses perfectly well, and splicing it in would comment out the rest of the line -- the
		// closing brace included, on a one-line block.
		FString Text = Original;
		FDreamUIDiagnosticBag Diagnostics;
		const bool bWritten = Patch(Text, TEXT("Title"), EDreamUIPatchTarget::Node, INDEX_NONE,
			TEXT("FontSize"), TEXT("26 // and the rest"), Diagnostics);
		ExpectRefusal(*this, TEXT("a value with more than a value in it"), Original, Text, bWritten,
			Diagnostics, EDreamUIDiagnosticCode::UnexpectedToken);
	}

	{
		// The grammar's own opinion, asked before the text is touched rather than discovered when
		// the file is next opened.
		FString Text = Original;
		FDreamUIDiagnosticBag Diagnostics;
		const bool bWritten = Patch(Text, TEXT("Title"), EDreamUIPatchTarget::Node, INDEX_NONE,
			TEXT("Text"), TEXT("\"never closed"), Diagnostics);
		ExpectRefusal(*this, TEXT("a string with no closing quote"), Original, Text, bWritten,
			Diagnostics, EDreamUIDiagnosticCode::UnterminatedString);
	}

	{
		FString Text = Original;
		FDreamUIDiagnosticBag Diagnostics;
		const bool bWritten = Patch(Text, TEXT("Title"), EDreamUIPatchTarget::Node, INDEX_NONE,
			TEXT("FontSize"), TEXT("   "), Diagnostics);
		TestFalse(TEXT("a value that is nothing at all is refused"), bWritten);
		TestEqual(TEXT("and leaves the file exactly as it was"), Text, Original);
		TestTrue(TEXT("with something to say about it"), Diagnostics.Diagnostics.Num() > 0);
	}

	return true;
}

// ------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIPatcherRoundTripTest,
	"DreamGUI.Text.WhatWasWrittenIsWhatTheFileParsesBackTo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIPatcherRoundTripTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIPatcherTestLocal;

	// The property nothing else in the suite can check: the patcher and the parser live in different
	// modules, so a disagreement between them produces a file that saves and does not reopen, and
	// every test on either side passes.
	FString Text = SavePanel();
	TestTrue(TEXT("a value is replaced"), Patch(Text, TEXT("Title"), EDreamUIPatchTarget::Node, INDEX_NONE,
		TEXT("FontSize"), TEXT("26")));
	TestTrue(TEXT("a tuple is inserted"), Patch(Text, TEXT("Title"), EDreamUIPatchTarget::Node, INDEX_NONE,
		TEXT("AnchorData.SizeDelta"), TEXT("(160, 48)")));
	TestTrue(TEXT("a block is created for a node that had none"), Patch(Text, TEXT("OkText"),
		EDreamUIPatchTarget::Node, INDEX_NONE, TEXT("Text"), TEXT("\"确定\"")));
	TestTrue(TEXT("a behaviour property is inserted"), Patch(Text, TEXT("OkBtn"),
		EDreamUIPatchTarget::Component, 0, TEXT("HoveredColor"), TEXT("#808080")));

	FDreamUIAst Ast;
	FDreamUIDiagnosticBag Diagnostics;
	if (!TestTrue(TEXT("the patched file parses"), Parse(Text, Ast, Diagnostics)))
	{
		return false;
	}
	TestEqual(TEXT("with nothing to complain about"), Diagnostics.Diagnostics.Num(), 0);

	const FDreamUINode* Title = FindNode(Ast, TEXT("Title"));
	if (TestNotNull(TEXT("the node that was edited is still there"), Title))
	{
		const FDreamUIProperty* FontSize = FindProperty(Title->Properties, TEXT("FontSize"));
		if (TestNotNull(TEXT("holding the property that was replaced"), FontSize))
		{
			TestEqual(TEXT("with the value that was written"), FontSize->Value.Raw, FString(TEXT("26")));
		}

		const FDreamUIProperty* SizeDelta = FindProperty(Title->Properties, TEXT("AnchorData.SizeDelta"));
		if (TestNotNull(TEXT("and the property that was inserted"), SizeDelta))
		{
			TestEqual(TEXT("read back as a tuple"), static_cast<int32>(SizeDelta->Value.Kind),
				static_cast<int32>(EDreamUIValueKind::Tuple));
			TestEqual(TEXT("of two elements"), SizeDelta->Value.Elements.Num(), 2);
			TestEqual(TEXT("with the text that was written"), SizeDelta->Value.Raw, FString(TEXT("(160, 48)")));
		}
	}

	const FDreamUINode* OkText = FindNode(Ast, TEXT("OkText"));
	if (TestNotNull(TEXT("the node that grew a block is still there"), OkText))
	{
		const FDreamUIProperty* Written = FindProperty(OkText->Properties, TEXT("Text"));
		if (TestNotNull(TEXT("holding the property that created it"), Written))
		{
			// Raw is the unescaped string, quotes stripped, which is what the builder writes.
			TestEqual(TEXT("with the string that was written"), Written->Value.Raw, FString(TEXT("确定")));
		}
	}

	const FDreamUINode* OkBtn = FindNode(Ast, TEXT("OkBtn"));
	if (TestNotNull(TEXT("the node with the behaviour is still there"), OkBtn)
		&& TestEqual(TEXT("with its one '+' block"), OkBtn->Components.Num(), 1))
	{
		TestEqual(TEXT("now holding two properties"), OkBtn->Components[0].Properties.Num(), 2);
		const FDreamUIProperty* Hovered = FindProperty(OkBtn->Components[0].Properties, TEXT("HoveredColor"));
		if (TestNotNull(TEXT("the second of which is the one written"), Hovered))
		{
			TestEqual(TEXT("read back as a colour"), static_cast<int32>(Hovered->Value.Kind),
				static_cast<int32>(EDreamUIValueKind::HexColor));
			TestEqual(TEXT("with the digits that were written"), Hovered->Value.Raw, FString(TEXT("808080")));
		}
	}

	return true;
}

// ------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIPatcherIsIdempotentTest,
	"DreamGUI.Text.WritingTheSameValueTwiceChangesNothingTheSecondTime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIPatcherIsIdempotentTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIPatcherTestLocal;

	// The most important assertion in the file. The designer flushes on every gesture end and every
	// focus change, so an edit that is not byte stable turns an idle session into a stream of diffs
	// -- and it does it quietly, in a file the author is not looking at.
	{
		FString Once = SavePanel();
		TestTrue(TEXT("a value is replaced"), Patch(Once, TEXT("Title"), EDreamUIPatchTarget::Node,
			INDEX_NONE, TEXT("FontSize"), TEXT("26")));
		FString Twice = Once;
		TestTrue(TEXT("and replaced again with the same value"), Patch(Twice, TEXT("Title"),
			EDreamUIPatchTarget::Node, INDEX_NONE, TEXT("FontSize"), TEXT("26")));
		TestEqual(TEXT("which changes not one byte"), Twice, Once);
	}

	{
		// The insert path reaches idempotence a different way: the second write finds the property
		// the first one created and replaces its value with an identical slice, which plans no edit
		// at all. Worth its own case, because "insert if absent" is exactly the shape that appends a
		// duplicate line every time when it is wrong.
		FString Once = SavePanel();
		TestTrue(TEXT("a property is inserted"), Patch(Once, TEXT("Title"), EDreamUIPatchTarget::Node,
			INDEX_NONE, TEXT("AnchorData.AnchorMin"), TEXT("(0, 0)")));
		FString Twice = Once;
		TestTrue(TEXT("and written again with the same value"), Patch(Twice, TEXT("Title"),
			EDreamUIPatchTarget::Node, INDEX_NONE, TEXT("AnchorData.AnchorMin"), TEXT("(0, 0)")));
		TestEqual(TEXT("which changes not one byte"), Twice, Once);

		FString Thrice = Twice;
		TestTrue(TEXT("and a third time for good measure"), Patch(Thrice, TEXT("Title"),
			EDreamUIPatchTarget::Node, INDEX_NONE, TEXT("AnchorData.AnchorMin"), TEXT("(0, 0)")));
		TestEqual(TEXT("still not one byte"), Thrice, Twice);
	}

	{
		// Creating a block is the least reversible thing this component does, so it gets the same
		// treatment: the second write must find the block it made, not make another one.
		FString Once = SavePanel();
		TestTrue(TEXT("a block is created"), Patch(Once, TEXT("OkText"), EDreamUIPatchTarget::Node,
			INDEX_NONE, TEXT("Text"), TEXT("\"OK\"")));
		FString Twice = Once;
		TestTrue(TEXT("and written to again"), Patch(Twice, TEXT("OkText"), EDreamUIPatchTarget::Node,
			INDEX_NONE, TEXT("Text"), TEXT("\"OK\"")));
		TestEqual(TEXT("which changes not one byte"), Twice, Once);
	}

	{
		// Twenty writes, the way a drag produces them, ending on a value that has to be exactly the
		// last one asked for. A patcher that drifted -- rounding, reformatting, appending -- would
		// show it here and nowhere else.
		FString Text = SavePanel();
		for (int32 Step = 0; Step < 20; ++Step)
		{
			const FString Value = FString::Printf(TEXT("(%d, 48)"), 160 + Step);
			TestTrue(TEXT("every step of a drag is written"), Patch(Text, TEXT("Root"),
				EDreamUIPatchTarget::Node, INDEX_NONE, TEXT("AnchorData.SizeDelta"), *Value));
		}
		TestEqual(TEXT("and the file holds the last of them, and only that line changed"), Text,
			SavePanelWith(5, TEXT("    AnchorData.SizeDelta = (179, 48)")));
	}

	return true;
}

// ------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIPatcherBatchTest,
	"DreamGUI.Text.SeveralEditsInOnePassAllLandWhereTheyWouldHaveAlone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIPatcherBatchTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIPatcherTestLocal;

	auto MakeEdit = [](const TCHAR* InNodeId, EDreamUIPatchTarget InTarget, int32 InComponentIndex,
		const TCHAR* InName, const TCHAR* InValue)
	{
		FDreamUIPropertyEdit Edit;
		Edit.NodeId = InNodeId;
		Edit.Target = InTarget;
		Edit.ComponentIndex = InComponentIndex;
		Edit.PropertyName = InName;
		Edit.NewValueText = InValue;
		return Edit;
	};

	// Four edits measured against one tree and applied from the end of the file backwards. Two of
	// them are inserts, which change the length of the text, so an implementation that applied them
	// forwards would put the later ones progressively further out of place -- and the failure is a
	// line landing in the wrong block, not a crash.
	{
		FString Text = SavePanel();
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag ParseDiagnostics;
		TestTrue(TEXT("the file parses"), Parse(Text, Ast, ParseDiagnostics));

		const TArray<FDreamUIPropertyEdit> Edits = {
			MakeEdit(TEXT("Root"), EDreamUIPatchTarget::Node, INDEX_NONE, TEXT("AnchorData.SizeDelta"), TEXT("(360, 200)")),
			MakeEdit(TEXT("Title"), EDreamUIPatchTarget::Node, INDEX_NONE, TEXT("FontSize"), TEXT("26")),
			MakeEdit(TEXT("OkBtn"), EDreamUIPatchTarget::Component, 0, TEXT("HoveredColor"), TEXT("#808080")),
			MakeEdit(TEXT("Bg"), EDreamUIPatchTarget::Node, INDEX_NONE, TEXT("Brush.TintColor"), TEXT("#1E1E1E"))
		};

		FDreamUIDiagnosticBag Diagnostics;
		TestTrue(TEXT("all four are written"), FDreamUITextPatcher::SetProperties(Text, Ast, Edits, Diagnostics));
		TestEqual(TEXT("with nothing to report"), Diagnostics.Diagnostics.Num(), 0);
		TestEqual(TEXT("and every one of them landed where it would have on its own"), Text,
			MakeSource({
				TEXT("// UI/SavePanel.dui"),
				TEXT("class /Game/UI/WBP_SavePanel"),
				TEXT(""),
				TEXT("Widget Root {"),
				TEXT("    AnchorData.SizeDelta = (360, 200)"),
				TEXT(""),
				TEXT("    Text Title {"),
				TEXT("        FontSize   = 26   // 标题"),
				TEXT("        HAlign     = Left"),
				TEXT("    }"),
				TEXT(""),
				TEXT("    Widget OkBtn {"),
				TEXT("        + UIButton {"),
				TEXT("            NormalColor = #FFFFFF"),
				TEXT("            HoveredColor = #808080"),
				TEXT("        }"),
				TEXT(""),
				TEXT("        Text OkText"),
				TEXT("    }"),
				TEXT(""),
				TEXT("    Image Bg {"),
				TEXT("        Brush.TintColor = #1E1E1E"),
				TEXT("    }"),
				TEXT(""),
				TEXT("    slot Footer"),
				TEXT("}")
			}));
	}

	// Two inserts into the SAME block, which both resolve to the same insertion point. They come out
	// in the order they were asked for, which is the whole reason the splices carry the order they
	// were planned in as well as the offset they land at.
	{
		FString Text = SavePanel();
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag ParseDiagnostics;
		TestTrue(TEXT("the file parses"), Parse(Text, Ast, ParseDiagnostics));

		const TArray<FDreamUIPropertyEdit> Edits = {
			MakeEdit(TEXT("Title"), EDreamUIPatchTarget::Node, INDEX_NONE, TEXT("RenderOpacity"), TEXT("0.95")),
			MakeEdit(TEXT("Title"), EDreamUIPatchTarget::Node, INDEX_NONE, TEXT("Clipping"), TEXT("ClipToBounds"))
		};

		FDreamUIDiagnosticBag Diagnostics;
		TestTrue(TEXT("both are written"), FDreamUITextPatcher::SetProperties(Text, Ast, Edits, Diagnostics));
		TestEqual(TEXT("in the order they were asked for"), Text,
			SavePanelInserting(9, {
				TEXT("        RenderOpacity = 0.95"),
				TEXT("        Clipping = ClipToBounds")
			}));
	}

	// One edit that cannot be resolved does not cost the others theirs. A flush of ten dirty
	// properties is better served by nine writes and one complaint than by losing all ten -- and the
	// one that failed is named and located rather than dropped.
	{
		FString Text = SavePanel();
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag ParseDiagnostics;
		TestTrue(TEXT("the file parses"), Parse(Text, Ast, ParseDiagnostics));

		const TArray<FDreamUIPropertyEdit> Edits = {
			MakeEdit(TEXT("Nope"), EDreamUIPatchTarget::Node, INDEX_NONE, TEXT("FontSize"), TEXT("26")),
			MakeEdit(TEXT("Title"), EDreamUIPatchTarget::Node, INDEX_NONE, TEXT("FontSize"), TEXT("26"))
		};

		FDreamUIDiagnosticBag Diagnostics;
		TestFalse(TEXT("the batch reports that something was refused"),
			FDreamUITextPatcher::SetProperties(Text, Ast, Edits, Diagnostics));
		TestTrue(TEXT("under the expected code"), Reported(Diagnostics, EDreamUIDiagnosticCode::PatchTargetNotFound));
		TestEqual(TEXT("and the edit that could be resolved was still written"), Text,
			SavePanelWith(8, TEXT("        FontSize   = 26   // 标题")));
	}

	// Two values for one property in one write. Both are refused rather than one of them silently
	// winning: both were measured against a text in which the property appears once, so applying
	// them would either write the line twice or splice one over the other, and choosing between them
	// is not this component's decision to make.
	{
		const FString Original = SavePanel();
		FString Text = Original;
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag ParseDiagnostics;
		TestTrue(TEXT("the file parses"), Parse(Text, Ast, ParseDiagnostics));

		const TArray<FDreamUIPropertyEdit> Edits = {
			MakeEdit(TEXT("Title"), EDreamUIPatchTarget::Node, INDEX_NONE, TEXT("FontSize"), TEXT("26")),
			MakeEdit(TEXT("Title"), EDreamUIPatchTarget::Node, INDEX_NONE, TEXT("FontSize"), TEXT("28"))
		};

		FDreamUIDiagnosticBag Diagnostics;
		const bool bWritten = FDreamUITextPatcher::SetProperties(Text, Ast, Edits, Diagnostics);
		ExpectRefusal(*this, TEXT("one property given two values in one write"), Original, Text, bWritten,
			Diagnostics, EDreamUIDiagnosticCode::PatchTargetNotFound);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
