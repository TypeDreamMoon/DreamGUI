// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Core/DreamWidgetTree.h"
#include "DreamUIValueFormatTestTypes.h"
#include "Math/NumericLimits.h"
#include "Text/DreamUISourceFile.h"
#include "Text/DreamUIValueFormat.h"
#include "UObject/UnrealType.h"

/*
 * The .dui front end, on its own: strings in, data out, not one UObject anywhere.
 *
 * That is the property worth protecting and the reason this file exists before the builder does.
 * The parser has to run in three places -- an editor compile, a packaged game loading a mod's
 * layout, and a headless CI gate that has no engine objects at all -- and the moment it can only be
 * tested through a world, the other two stop being provable.
 *
 * Two rules about what is asserted here, both learned the hard way elsewhere:
 *
 * The CODE is the contract, never the wording. Every negative case compares
 * EDreamUIDiagnosticCode, so a message can be rewritten the day somebody finds a clearer way to say
 * it without a test going red for it. Assertion text is kept clear of the diagnostic strings for a
 * second reason as well -- the automation framework's expected-error filter matches on substrings,
 * and DreamShader once lost real failures to assertion messages that happened to look like errors
 * somebody had declared expected.
 *
 * LINE AND COLUMN are asserted, not just the shape. The write-back patcher finds the line to edit
 * from these, so a location that is merely close produces an edit landing on the property above the
 * one the designer touched, silently, in a file nobody had open. A structural assertion alone would
 * pass through that.
 */

namespace DreamUIParserTestLocal
{
	/** Fixtures are written a line at a time so a test can say "line 12" and mean the twelfth entry. */
	FString MakeSource(const TArray<FString>& InLines)
	{
		return FString::Join(InLines, TEXT("\n"));
	}

	bool Parse(const FString& InSource, FDreamUIAst& OutAst, FDreamUIDiagnosticBag& OutDiagnostics)
	{
		return FDreamUISourceFile::Parse(InSource, TEXT("Fixture.dui"), OutAst, OutDiagnostics);
	}

	bool Reported(const FDreamUIDiagnosticBag& InDiagnostics, EDreamUIDiagnosticCode InCode)
	{
		return InDiagnostics.Diagnostics.ContainsByPredicate([InCode](const FDreamUIDiagnostic& InDiagnostic)
		{
			return InDiagnostic.Code == InCode;
		});
	}

	const FDreamUIDiagnostic* FirstOf(const FDreamUIDiagnosticBag& InDiagnostics, EDreamUIDiagnosticCode InCode)
	{
		return InDiagnostics.Diagnostics.FindByPredicate([InCode](const FDreamUIDiagnostic& InDiagnostic)
		{
			return InDiagnostic.Code == InCode;
		});
	}

	const FDreamUINode* ChildById(const FDreamUINode& InParent, const TCHAR* InId)
	{
		return InParent.Children.FindByPredicate([InId](const FDreamUINode& InChild)
		{
			return InChild.Id == InId;
		});
	}

	const FDreamUIProperty* PropertyByName(const TArray<FDreamUIProperty>& InProperties, const TCHAR* InName)
	{
		return InProperties.FindByPredicate([InName](const FDreamUIProperty& InProperty)
		{
			return InProperty.Name == InName;
		});
	}

	/**
	 * One mistake, one diagnostic, at a known line.
	 *
	 * The count is asserted as well as the code, and that is the interesting half: a recovery path
	 * that stumbles produces the right diagnostic followed by two invented ones, which is exactly the
	 * failure the recovery exists to prevent and exactly the failure a "contains the code" assertion
	 * cannot see.
	 */
	bool ExpectOneDiagnostic(FAutomationTestBase& InTest, const TCHAR* InWhat, const FString& InSource,
		EDreamUIDiagnosticCode InCode, int32 InLine)
	{
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		const bool bParsed = Parse(InSource, Ast, Diagnostics);

		bool bPassed = InTest.TestFalse(*FString::Printf(TEXT("%s is refused"), InWhat), bParsed);
		bPassed &= InTest.TestEqual(*FString::Printf(TEXT("%s produces exactly one complaint"), InWhat),
			Diagnostics.Diagnostics.Num(), 1);
		if (Diagnostics.Diagnostics.Num() != 1)
		{
			return false;
		}
		bPassed &= InTest.TestEqual(*FString::Printf(TEXT("%s is reported under the expected code"), InWhat),
			static_cast<int32>(Diagnostics.Diagnostics[0].Code), static_cast<int32>(InCode));
		bPassed &= InTest.TestEqual(*FString::Printf(TEXT("%s is reported on the expected line"), InWhat),
			Diagnostics.Diagnostics[0].Location.Line, InLine);
		return bPassed;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUITextWholeFileShapeTest,
	"DreamGUI.Text.AWholeFileParsesIntoTheShapeItDescribes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUITextWholeFileShapeTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIParserTestLocal;

	// The reference sample from the language's own design note, verbatim in shape: every notation the
	// grammar has, in one file, so that a change to one production cannot quietly break another. The
	// line numbers in the assertions below are the line numbers of this array.
	const FString Source = MakeSource({
		TEXT("// UI/SavePanel.dui"),                            //  1
		TEXT("class /Game/UI/WBP_SavePanel"),                   //  2
		TEXT(""),                                               //  3
		TEXT("style Card {"),                                   //  4
		TEXT("    RenderOpacity = 0.95"),                       //  5
		TEXT("    Clipping      = ClipToBounds"),               //  6
		TEXT("}"),                                              //  7
		TEXT(""),                                               //  8
		TEXT("Widget Root {"),                                  //  9
		TEXT("    AnchorData.AnchorMin = (0, 0)"),              // 10
		TEXT("    AnchorData.SizeDelta = (400, 240)"),          // 11
		TEXT("    Text                <- GetTitleText()"),      // 12
		TEXT(""),                                               // 13
		TEXT("    Image Bg : Card {"),                          // 14
		TEXT("        Brush.TintColor = #1E1E1E"),              // 15
		TEXT("    }"),                                          // 16
		TEXT(""),                                               // 17
		TEXT("    + UIButton {"),                               // 18
		TEXT("        NormalColor = #FFFFFF"),                  // 19
		TEXT("        OnClick     = HandleOk"),                 // 20
		TEXT("    }"),                                          // 21
		TEXT(""),                                               // 22
		TEXT("    /Game/UI/WBP_SlotCard Card1 {"),              // 23
		TEXT("        @slot FillWeight = 1"),                   // 24
		TEXT("    }"),                                          // 25
		TEXT(""),                                               // 26
		TEXT("    Text OkText (was: OkLabel) {"),               // 27
		TEXT("        Text = \"确定\" @key(\"SavePanel.Ok\")"), // 28
		TEXT("    }"),                                          // 29
		TEXT(""),                                               // 30
		TEXT("    each Slot in GetSlots() {"),                  // 31
		TEXT("        Widget RowMarker"),                       // 32
		TEXT("    }"),                                          // 33
		TEXT(""),                                               // 34
		TEXT("    slot Footer"),                                // 35
		TEXT("}")                                               // 36
	});

	FDreamUIAst Ast;
	FDreamUIDiagnosticBag Diagnostics;
	if (!TestTrue(TEXT("the reference file parses"), Parse(Source, Ast, Diagnostics)))
	{
		AddInfo(Diagnostics.ToString());
		return false;
	}
	TestEqual(TEXT("and raises nothing at all, warnings included"), Diagnostics.Diagnostics.Num(), 0);

	// The file header.
	TestEqual(TEXT("the class path is the one declared"), Ast.ClassPath, FString(TEXT("/Game/UI/WBP_SavePanel")));
	TestEqual(TEXT("and points at the path, not the keyword"), Ast.ClassPathLocation.Line, 2);
	TestEqual(TEXT("on the column the path starts at"), Ast.ClassPathLocation.Column, 7);

	// Styles.
	if (TestEqual(TEXT("the file declares one style"), Ast.Styles.Num(), 1))
	{
		TestEqual(TEXT("named as written"), Ast.Styles[0].Name, FString(TEXT("Card")));
		TestEqual(TEXT("with both of its properties"), Ast.Styles[0].Properties.Num(), 2);
		TestTrue(TEXT("and found by name"), Ast.FindStyle(TEXT("Card")) == &Ast.Styles[0]);
	}
	TestNull(TEXT("a style the file does not declare is not found"), Ast.FindStyle(TEXT("Nope")));

	// The root.
	if (!TestTrue(TEXT("the file produced a root"), Ast.bHasRoot))
	{
		return false;
	}
	const FDreamUINode& Root = Ast.Root;
	TestEqual(TEXT("the root keeps its type as written"), Root.TypeName, FString(TEXT("Widget")));
	TestEqual(TEXT("and its id"), Root.Id, FString(TEXT("Root")));
	TestEqual(TEXT("and starts where the type does"), Root.Location.Line, 9);
	TestEqual(TEXT("in the first column"), Root.Location.Column, 1);

	// Properties on the root: two dotted assignments and one binding.
	TestEqual(TEXT("the root carries three properties"), Root.Properties.Num(), 3);
	if (const FDreamUIProperty* AnchorMin = PropertyByName(Root.Properties, TEXT("AnchorData.AnchorMin")))
	{
		// The dotted path stays one string. Splitting it here would decide which segment is a struct,
		// which is a question only reflection can answer.
		TestEqual(TEXT("a dotted property starts at its name"), AnchorMin->Location.Line, 10);
		TestEqual(TEXT("in the column the name starts in"), AnchorMin->Location.Column, 5);
		TestEqual(TEXT("a parenthesised value is a tuple"), static_cast<int32>(AnchorMin->Value.Kind),
			static_cast<int32>(EDreamUIValueKind::Tuple));
		TestEqual(TEXT("whose raw text is the whole thing including brackets"), AnchorMin->Value.Raw, FString(TEXT("(0, 0)")));
		if (TestEqual(TEXT("with one element per comma"), AnchorMin->Value.Elements.Num(), 2))
		{
			TestEqual(TEXT("first element"), AnchorMin->Value.Elements[0], FString(TEXT("0")));
			TestEqual(TEXT("second element"), AnchorMin->Value.Elements[1], FString(TEXT("0")));
		}
		TestEqual(TEXT("and the value's own location is the open bracket"), AnchorMin->Value.Location.Column, 28);
	}
	else
	{
		AddError(TEXT("the dotted anchor property did not survive the parse"));
	}

	if (const FDreamUIProperty* Bound = PropertyByName(Root.Properties, TEXT("Text")))
	{
		TestTrue(TEXT("an arrow makes a binding rather than an assignment"), Bound->IsBinding());
		TestEqual(TEXT("naming the function without its brackets"), Bound->BindingFunction, FString(TEXT("GetTitleText")));
		TestEqual(TEXT("and a binding has no value"), Bound->Value.Raw, FString());
		TestEqual(TEXT("a binding starts at its property name"), Bound->Location.Line, 12);
	}
	else
	{
		AddError(TEXT("the bound property did not survive the parse"));
	}

	// A behaviour is not a child. Both live under the same node and neither may leak into the other.
	if (TestEqual(TEXT("the root has one behaviour"), Root.Components.Num(), 1))
	{
		TestEqual(TEXT("named as written"), Root.Components[0].ClassName, FString(TEXT("UIButton")));
		TestEqual(TEXT("with its own properties"), Root.Components[0].Properties.Num(), 2);
		TestEqual(TEXT("starting at the plus"), Root.Components[0].Location.Line, 18);
		TestEqual(TEXT("in the plus's column"), Root.Components[0].Location.Column, 5);
	}

	// Five children: the styled image, the nested user widget, the renamed text, the loop and the slot.
	TestEqual(TEXT("the root has five children"), Root.Children.Num(), 5);

	if (const FDreamUINode* Bg = ChildById(Root, TEXT("Bg")))
	{
		TestEqual(TEXT("a built-in tag is kept verbatim"), Bg->TypeName, FString(TEXT("Image")));
		TestEqual(TEXT("a colon clause records the style by name"), Bg->StyleName, FString(TEXT("Card")));
		if (TestEqual(TEXT("with the block's one property"), Bg->Properties.Num(), 1))
		{
			TestEqual(TEXT("a hash literal is a colour"), static_cast<int32>(Bg->Properties[0].Value.Kind),
				static_cast<int32>(EDreamUIValueKind::HexColor));
			TestEqual(TEXT("whose raw text drops the hash"), Bg->Properties[0].Value.Raw, FString(TEXT("1E1E1E")));
		}
	}
	else
	{
		AddError(TEXT("the styled child did not survive the parse"));
	}

	if (const FDreamUINode* Card1 = ChildById(Root, TEXT("Card1")))
	{
		// The type is an asset path and the parser does not care whether it loads. UnknownNodeType and
		// AssetNotFound belong to the builder, which is the half that can actually look.
		TestEqual(TEXT("an asset path is a legal node type"), Card1->TypeName, FString(TEXT("/Game/UI/WBP_SlotCard")));
		TestEqual(TEXT("a slot property does not land with the ordinary ones"), Card1->Properties.Num(), 0);
		if (TestEqual(TEXT("but in the slot list"), Card1->SlotProperties.Num(), 1))
		{
			TestEqual(TEXT("named without the annotation"), Card1->SlotProperties[0].Name, FString(TEXT("FillWeight")));
			TestEqual(TEXT("and located at the at-sign, where the line starts"), Card1->SlotProperties[0].Location.Column, 9);
		}
	}
	else
	{
		AddError(TEXT("the nested user widget did not survive the parse"));
	}

	if (const FDreamUINode* OkText = ChildById(Root, TEXT("OkText")))
	{
		TestEqual(TEXT("a rename clause records the old id"), OkText->WasId, FString(TEXT("OkLabel")));
		if (TestEqual(TEXT("with the block's one property"), OkText->Properties.Num(), 1))
		{
			TestEqual(TEXT("a quoted literal is a string"), static_cast<int32>(OkText->Properties[0].Value.Kind),
				static_cast<int32>(EDreamUIValueKind::String));
			TestEqual(TEXT("carrying the text without its quotes"), OkText->Properties[0].Value.Raw, FString(TEXT("确定")));
			TestEqual(TEXT("and the key the author overrode"), OkText->Properties[0].Value.LocalizationKeyOverride,
				FString(TEXT("SavePanel.Ok")));
		}
	}
	else
	{
		AddError(TEXT("the renamed child did not survive the parse"));
	}

	// The loop keeps no id of its own -- it is a construct, not a widget -- and its body is children.
	const FDreamUINode* Loop = Root.Children.FindByPredicate([](const FDreamUINode& InChild)
	{
		return InChild.Kind == EDreamUINodeKind::EachLoop;
	});
	if (Loop != nullptr)
	{
		TestEqual(TEXT("each produces a run-time loop, not a compile-time one"), static_cast<int32>(Loop->Kind),
			static_cast<int32>(EDreamUINodeKind::EachLoop));
		TestEqual(TEXT("with its variable"), Loop->LoopVariable, FString(TEXT("Slot")));
		TestEqual(TEXT("and its source function, brackets stripped"), Loop->LoopSourceFunction, FString(TEXT("GetSlots")));
		TestEqual(TEXT("a loop has no id"), Loop->Id, FString());
		TestEqual(TEXT("starting at its keyword"), Loop->Location.Line, 31);
		if (TestEqual(TEXT("and the body hangs off it"), Loop->Children.Num(), 1))
		{
			TestEqual(TEXT("the body's node kept its id"), Loop->Children[0].Id, FString(TEXT("RowMarker")));
			TestEqual(TEXT("a block is optional on a node with nothing in it"), Loop->Children[0].Properties.Num(), 0);
			TestEqual(TEXT("and it is located on its own line"), Loop->Children[0].Location.Line, 32);
		}
	}
	else
	{
		AddError(TEXT("the each loop did not survive the parse"));
	}

	if (const FDreamUINode* Footer = ChildById(Root, TEXT("Footer")))
	{
		TestEqual(TEXT("a slot declaration is a node of its own kind"), static_cast<int32>(Footer->Kind),
			static_cast<int32>(EDreamUINodeKind::NamedSlot));
		TestEqual(TEXT("whose name is its id"), Footer->Id, FString(TEXT("Footer")));
		TestEqual(TEXT("and which takes no type"), Footer->TypeName, FString());
		TestEqual(TEXT("located at its keyword"), Footer->Location.Line, 35);
	}
	else
	{
		AddError(TEXT("the named slot did not survive the parse"));
	}

	// The walk everything downstream uses. Root, four widget-ish children, one loop, one loop body.
	int32 Visited = 0;
	Ast.ForEachNode([&Visited](const FDreamUINode&) { ++Visited; });
	TestEqual(TEXT("the tree walk reaches every node including loop bodies"), Visited, 7);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUITextLocationsTest,
	"DreamGUI.Text.EveryElementRemembersTheColumnItWasWrittenIn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUITextLocationsTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIParserTestLocal;

	// Columns, not just lines. The patcher rewrites a value in place, and a location that is right
	// about the line and vague about the column is enough to overwrite the wrong half of it.
	const FString Source = MakeSource({
		TEXT("class /Game/UI/WBP_X"),          // 1
		TEXT("Widget Root {"),                 // 2
		TEXT("    FontSize = 24"),             // 3
		TEXT("    Image Bg {"),                // 4
		TEXT("        @slot FillWeight = 1"),  // 5
		TEXT("    }"),                         // 6
		TEXT("    + UIButton {"),              // 7
		TEXT("        OnClick = HandleOk"),    // 8
		TEXT("    }"),                         // 9
		TEXT("}")                              // 10
	});

	FDreamUIAst Ast;
	FDreamUIDiagnosticBag Diagnostics;
	if (!TestTrue(TEXT("the fixture parses"), Parse(Source, Ast, Diagnostics)))
	{
		AddInfo(Diagnostics.ToString());
		return false;
	}

	TestEqual(TEXT("the class path starts after 'class '"), Ast.ClassPathLocation.Column, 7);
	TestEqual(TEXT("the root is in the first column"), Ast.Root.Location.Column, 1);

	if (TestEqual(TEXT("the root has one property"), Ast.Root.Properties.Num(), 1))
	{
		TestEqual(TEXT("a property starts at its name, past the indent"), Ast.Root.Properties[0].Location.Column, 5);
		TestEqual(TEXT("and its value starts after the equals and its space"), Ast.Root.Properties[0].Value.Location.Column, 16);
		TestEqual(TEXT("both on the line they were written on"), Ast.Root.Properties[0].Value.Location.Line, 3);
	}

	if (TestEqual(TEXT("the root has one child"), Ast.Root.Children.Num(), 1))
	{
		const FDreamUINode& Bg = Ast.Root.Children[0];
		TestEqual(TEXT("a nested node starts at its type"), Bg.Location.Column, 5);
		TestEqual(TEXT("on its own line"), Bg.Location.Line, 4);
		if (TestEqual(TEXT("with one slot property"), Bg.SlotProperties.Num(), 1))
		{
			TestEqual(TEXT("whose location is the at-sign that begins the line"), Bg.SlotProperties[0].Location.Column, 9);
			TestEqual(TEXT("and whose value is past the annotation and the name"), Bg.SlotProperties[0].Value.Location.Column, 28);
		}
	}

	if (TestEqual(TEXT("the root has one behaviour"), Ast.Root.Components.Num(), 1))
	{
		TestEqual(TEXT("a behaviour starts at its plus"), Ast.Root.Components[0].Location.Column, 5);
		if (TestEqual(TEXT("carrying one property"), Ast.Root.Components[0].Properties.Num(), 1))
		{
			TestEqual(TEXT("indented inside the behaviour's own block"), Ast.Root.Components[0].Properties[0].Location.Column, 9);
			TestEqual(TEXT("on the behaviour block's line"), Ast.Root.Components[0].Properties[0].Location.Line, 8);
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUITextLiteralsTest,
	"DreamGUI.Text.EveryLiteralKeepsTheTextItWasWrittenWith",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUITextLiteralsTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIParserTestLocal;

	// Raw is what the patcher writes back untouched, so what matters about each of these is that the
	// text survives byte for byte. A tuple that came back as "(400, 240)" after being written
	// "(400,240)" would rewrite a line nobody edited, once per save, forever.
	const FString Source = MakeSource({
		TEXT("Widget Root {"),
		TEXT("    A = 0.95"),
		TEXT("    B = -3"),
		TEXT("    C = \"quoted \\\"inner\\\" and\\ttab\""),
		TEXT("    D = (400, 240)"),
		TEXT("    E = ( 8 ,8, 8,8 )"),
		TEXT("    F = #1E1E1EFF"),
		TEXT("    G = ClipToBounds"),
		TEXT("    H = /Game/UI/F_Body"),
		TEXT("    I = \"确定\""),
		TEXT("    J = ()"),
		TEXT("    K = (1, 2,)"),
		TEXT("    L = 1e+20"),
		TEXT("    M = -1.5e-45"),
		TEXT("    N = 1E5"),
		TEXT("}")
	});

	FDreamUIAst Ast;
	FDreamUIDiagnosticBag Diagnostics;
	if (!TestTrue(TEXT("every literal shape parses"), Parse(Source, Ast, Diagnostics)))
	{
		AddInfo(Diagnostics.ToString());
		return false;
	}
	TestEqual(TEXT("with nothing to complain about"), Diagnostics.Diagnostics.Num(), 0);
	if (!TestEqual(TEXT("all fourteen land as properties"), Ast.Root.Properties.Num(), 14))
	{
		return false;
	}

	const TArray<FDreamUIProperty>& Properties = Ast.Root.Properties;

	TestEqual(TEXT("a decimal is a number"), static_cast<int32>(Properties[0].Value.Kind), static_cast<int32>(EDreamUIValueKind::Number));
	TestEqual(TEXT("kept as text, not reparsed and reprinted"), Properties[0].Value.Raw, FString(TEXT("0.95")));

	TestEqual(TEXT("a leading minus belongs to the number"), static_cast<int32>(Properties[1].Value.Kind), static_cast<int32>(EDreamUIValueKind::Number));
	TestEqual(TEXT("sign included"), Properties[1].Value.Raw, FString(TEXT("-3")));

	TestEqual(TEXT("a quoted literal is a string"), static_cast<int32>(Properties[2].Value.Kind), static_cast<int32>(EDreamUIValueKind::String));
	TestEqual(TEXT("with its escapes resolved and its quotes gone"), Properties[2].Value.Raw, FString(TEXT("quoted \"inner\" and\ttab")));
	TestEqual(TEXT("and no key override unless one was written"), Properties[2].Value.LocalizationKeyOverride, FString());

	TestEqual(TEXT("brackets make a tuple"), static_cast<int32>(Properties[3].Value.Kind), static_cast<int32>(EDreamUIValueKind::Tuple));
	TestEqual(TEXT("whose raw text is the source span"), Properties[3].Value.Raw, FString(TEXT("(400, 240)")));
	if (TestEqual(TEXT("split on commas"), Properties[3].Value.Elements.Num(), 2))
	{
		TestEqual(TEXT("first"), Properties[3].Value.Elements[0], FString(TEXT("400")));
		TestEqual(TEXT("second"), Properties[3].Value.Elements[1], FString(TEXT("240")));
	}

	// Elements are trimmed, the span is not: the builder wants clean text to convert, the patcher
	// wants the author's spacing back exactly as it found it.
	TestEqual(TEXT("odd spacing survives in the raw span"), Properties[4].Value.Raw, FString(TEXT("( 8 ,8, 8,8 )")));
	if (TestEqual(TEXT("while the elements come out trimmed"), Properties[4].Value.Elements.Num(), 4))
	{
		TestEqual(TEXT("first"), Properties[4].Value.Elements[0], FString(TEXT("8")));
		TestEqual(TEXT("last"), Properties[4].Value.Elements[3], FString(TEXT("8")));
	}

	TestEqual(TEXT("a hash literal is a colour"), static_cast<int32>(Properties[5].Value.Kind), static_cast<int32>(EDreamUIValueKind::HexColor));
	TestEqual(TEXT("whose raw text excludes the hash, which is a delimiter"), Properties[5].Value.Raw, FString(TEXT("1E1E1EFF")));

	TestEqual(TEXT("a bare word is an identifier, not an enum -- that is reflection's call"),
		static_cast<int32>(Properties[6].Value.Kind), static_cast<int32>(EDreamUIValueKind::Identifier));
	TestEqual(TEXT("kept verbatim"), Properties[6].Value.Raw, FString(TEXT("ClipToBounds")));

	TestEqual(TEXT("an unquoted path is an asset path"), static_cast<int32>(Properties[7].Value.Kind), static_cast<int32>(EDreamUIValueKind::AssetPath));
	TestEqual(TEXT("with its leading slash"), Properties[7].Value.Raw, FString(TEXT("/Game/UI/F_Body")));

	TestEqual(TEXT("non-ASCII text survives the lexer intact"), Properties[8].Value.Raw, FString(TEXT("确定")));

	TestEqual(TEXT("an empty tuple has no elements rather than one empty one"), Properties[9].Value.Elements.Num(), 0);
	TestEqual(TEXT("and still keeps its brackets as raw text"), Properties[9].Value.Raw, FString(TEXT("()")));

	// Tolerated for the same reason a stray semicolon is: it is a habit from every other language, and
	// letting it through as a phantom element would surface as an arity complaint about a count the
	// author never wrote.
	TestEqual(TEXT("a trailing comma does not invent an element"), Properties[10].Value.Elements.Num(), 2);

	// Scientific notation is not a nicety here: DreamUIValueFormat::Print emits it for large and tiny
	// magnitudes, so this is text the DESIGNER writes back into a .dui. `e` is an ordinary identifier
	// character, which is exactly how a lexer comes to reject its own editor's output.
	TestEqual(TEXT("an exponent is part of the number"), static_cast<int32>(Properties[11].Value.Kind),
		static_cast<int32>(EDreamUIValueKind::Number));
	TestEqual(TEXT("sign and all"), Properties[11].Value.Raw, FString(TEXT("1e+20")));
	TestEqual(TEXT("a negative mantissa with a negative exponent"), Properties[12].Value.Raw, FString(TEXT("-1.5e-45")));
	TestEqual(TEXT("and a capital E, with no sign"), Properties[13].Value.Raw, FString(TEXT("1E5")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUITextTriviaTest,
	"DreamGUI.Text.CommentsAndStraySemicolonsAreNotSyntax",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUITextTriviaTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIParserTestLocal;

	{
		// Semicolons are not an error, on purpose. The usual author of a .dui is a model that has
		// spent its life ending statements with one, and refusing the file over punctuation that
		// changes nothing costs a whole round trip to fix something nobody had to care about.
		const FString Source = MakeSource({
			TEXT("// leading line comment"),               // 1
			TEXT("/* a block"),                            // 2
			TEXT("   comment across lines */"),            // 3
			TEXT("Widget Root {   // trailing comment"),   // 4
			TEXT("    A = 1; B = 2"),                      // 5
			TEXT("    /* inline */ C = 3;"),               // 6
			TEXT("}")                                      // 7
		});

		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		if (!TestTrue(TEXT("a file full of comments and semicolons parses"), Parse(Source, Ast, Diagnostics)))
		{
			AddInfo(Diagnostics.ToString());
			return false;
		}
		TestEqual(TEXT("and none of it is reported"), Diagnostics.Diagnostics.Num(), 0);
		if (TestEqual(TEXT("a semicolon ends a statement rather than being dropped"), Ast.Root.Properties.Num(), 3))
		{
			TestEqual(TEXT("first"), Ast.Root.Properties[0].Name, FString(TEXT("A")));
			TestEqual(TEXT("second, on the same line as the first"), Ast.Root.Properties[1].Name, FString(TEXT("B")));
			TestEqual(TEXT("and both on line five"), Ast.Root.Properties[1].Location.Line, 5);
			TestEqual(TEXT("third"), Ast.Root.Properties[2].Name, FString(TEXT("C")));
			TestEqual(TEXT("which starts after the comment it follows"), Ast.Root.Properties[2].Location.Column, 18);
		}
		TestEqual(TEXT("the root's line survives the comments above it"), Ast.Root.Location.Line, 4);
	}

	{
		// A block comment that crosses a line still ends the statement it started on, the way a
		// newline would. Swallowing the line break with the comment joins two statements the author
		// wrote apart, and the complaint then names a token nowhere near the comment.
		const FString Source = MakeSource({
			TEXT("Widget Root {"),        // 1
			TEXT("    A = 1 /* one"),     // 2
			TEXT("    two */ B = 2"),     // 3
			TEXT("}")                     // 4
		});

		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		if (!TestTrue(TEXT("a comment spanning lines parses"), Parse(Source, Ast, Diagnostics)))
		{
			AddInfo(Diagnostics.ToString());
			return false;
		}
		if (TestEqual(TEXT("and leaves two separate statements"), Ast.Root.Properties.Num(), 2))
		{
			TestEqual(TEXT("the first is where it was written"), Ast.Root.Properties[0].Location.Line, 2);
			TestEqual(TEXT("and the line counter kept up with the comment"), Ast.Root.Properties[1].Location.Line, 3);
			TestEqual(TEXT("column included"), Ast.Root.Properties[1].Location.Column, 12);
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUITextLexicalDiagnosticsTest,
	"DreamGUI.Text.TextThatIsNotATokenIsRefusedWhereItStands",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUITextLexicalDiagnosticsTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIParserTestLocal;

	// A run of unrecognised characters is one complaint, not one per character. A Source File
	// pointed at a binary would otherwise fill the message log with thousands of them and bury
	// whatever else the file got wrong.
	ExpectOneDiagnostic(*this, TEXT("a stray run of punctuation"), MakeSource({
		TEXT("Widget Root {"),
		TEXT("    A = 1"),
		TEXT("}"),
		TEXT("$$$")
	}), EDreamUIDiagnosticCode::UnexpectedCharacter, 4);

	// Strings do not span lines, so a missing quote costs one line rather than the rest of the file.
	ExpectOneDiagnostic(*this, TEXT("a string with no closing quote"), MakeSource({
		TEXT("Widget Root {"),
		TEXT("    Title = \"no closing quote"),
		TEXT("}")
	}), EDreamUIDiagnosticCode::UnterminatedString, 2);

	ExpectOneDiagnostic(*this, TEXT("a block comment with no end"), MakeSource({
		TEXT("Widget Root {"),
		TEXT("    A = 1"),
		TEXT("}"),
		TEXT("/* never closed")
	}), EDreamUIDiagnosticCode::UnterminatedComment, 4);

	ExpectOneDiagnostic(*this, TEXT("a number with two decimal points"), MakeSource({
		TEXT("Widget Root {"),
		TEXT("    A = 1.2.3"),
		TEXT("}")
	}), EDreamUIDiagnosticCode::MalformedNumber, 2);

	ExpectOneDiagnostic(*this, TEXT("a lone minus where a number belongs"), MakeSource({
		TEXT("Widget Root {"),
		TEXT("    A = -"),
		TEXT("}")
	}), EDreamUIDiagnosticCode::MalformedNumber, 2);

	ExpectOneDiagnostic(*this, TEXT("a colour with five digits"), MakeSource({
		TEXT("Widget Root {"),
		TEXT("    A = #12345"),
		TEXT("}")
	}), EDreamUIDiagnosticCode::MalformedHexColor, 2);

	// An exponent needs digits after it. The 'e' is swallowed rather than left behind, so `400e` is
	// one complaint about one word and not a clean 400 followed by a mystery identifier.
	ExpectOneDiagnostic(*this, TEXT("an exponent with nothing after it"), MakeSource({
		TEXT("Widget Root {"),
		TEXT("    A = 1e"),
		TEXT("}")
	}), EDreamUIDiagnosticCode::MalformedNumber, 2);

	ExpectOneDiagnostic(*this, TEXT("an exponent on a whole number with nothing after it"), MakeSource({
		TEXT("Widget Root {"),
		TEXT("    A = 400e"),
		TEXT("}")
	}), EDreamUIDiagnosticCode::MalformedNumber, 2);

	ExpectOneDiagnostic(*this, TEXT("an exponent sign with no digits"), MakeSource({
		TEXT("Widget Root {"),
		TEXT("    A = 1e+"),
		TEXT("}")
	}), EDreamUIDiagnosticCode::MalformedNumber, 2);

	// Digits glued to letters is the shape the lexer refuses to judge on its own -- in a value it is
	// a number, and this is where it gets told so. The same text in a node header is a bad id
	// instead; see the collision test. One complaint either way, never both.
	ExpectOneDiagnostic(*this, TEXT("a unit glued onto a value"), MakeSource({
		TEXT("Widget Root {"),
		TEXT("    A = 24px"),
		TEXT("}")
	}), EDreamUIDiagnosticCode::MalformedNumber, 2);

	// Tuple elements are raw source slices, so this is the one place inside a tuple that looks at a
	// token at all. Without it the bad element would reach the builder with no line of its own.
	ExpectOneDiagnostic(*this, TEXT("a unit glued onto a tuple element"), MakeSource({
		TEXT("Widget Root {"),
		TEXT("    A = (24px, 3)"),
		TEXT("}")
	}), EDreamUIDiagnosticCode::MalformedNumber, 2);

	// The lexical complaint is enough on its own: the token still has the right shape, so the parser
	// finds a value where a value belongs and the statement around it does not produce a second one.
	{
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		Parse(MakeSource({
			TEXT("Widget Root {"),
			TEXT("    A = #12345"),
			TEXT("}")
		}), Ast, Diagnostics);
		TestEqual(TEXT("a lexically bad value still lands as a property"), Ast.Root.Properties.Num(), 1);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUITextGrammarDiagnosticsTest,
	"DreamGUI.Text.TokensThatDoNotFormTheGrammarAreNamedWithWhatWasExpected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUITextGrammarDiagnosticsTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIParserTestLocal;

	ExpectOneDiagnostic(*this, TEXT("a third word in a node header"), MakeSource({
		TEXT("Widget Root {"),
		TEXT("    Image Bg Extra { }"),
		TEXT("}")
	}), EDreamUIDiagnosticCode::UnexpectedToken, 2);

	// Reported at the brace that never closed, not at the end of the file: the brace is the place the
	// reader has to go, and "unexpected end of file" on the last line is the least useful true thing
	// a parser can say.
	ExpectOneDiagnostic(*this, TEXT("a block that never closes"), MakeSource({
		TEXT("Widget Root {"),
		TEXT("    Image Bg {"),
		TEXT("}")
	}), EDreamUIDiagnosticCode::UnclosedBlock, 1);

	// A closing brace ends the hunt for the bracket. Running past it would consume the rest of the
	// node and report the damage somewhere it did not happen.
	ExpectOneDiagnostic(*this, TEXT("a tuple that never closes"), MakeSource({
		TEXT("Widget Root {"),
		TEXT("    A = (1, 2"),
		TEXT("}")
	}), EDreamUIDiagnosticCode::UnclosedTuple, 2);

	ExpectOneDiagnostic(*this, TEXT("a node with no id"), MakeSource({
		TEXT("Widget Root {"),
		TEXT("    Image {"),
		TEXT("    }"),
		TEXT("}")
	}), EDreamUIDiagnosticCode::MissingNodeId, 2);

	ExpectOneDiagnostic(*this, TEXT("a property whose value is missing"), MakeSource({
		TEXT("Widget Root {"),
		TEXT("    FontSize ="),
		TEXT("}")
	}), EDreamUIDiagnosticCode::MissingPropertyValue, 2);

	// A dotted name can only be a property, so the missing operator is unambiguous here in a way it
	// is not for a bare word -- which is why the bare word reports MissingNodeId above and names both
	// readings in its message.
	ExpectOneDiagnostic(*this, TEXT("a property with neither an equals nor an arrow"), MakeSource({
		TEXT("Widget Root {"),
		TEXT("    AnchorData.SizeDelta"),
		TEXT("}")
	}), EDreamUIDiagnosticCode::MissingPropertyValue, 2);

	ExpectOneDiagnostic(*this, TEXT("a binding given an argument"), MakeSource({
		TEXT("Widget Root {"),
		TEXT("    Text <- GetTitle(1)"),
		TEXT("}")
	}), EDreamUIDiagnosticCode::UnexpectedToken, 2);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUITextRootDiagnosticsTest,
	"DreamGUI.Text.AFileWithoutExactlyOneRootIsRefused",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUITextRootDiagnosticsTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIParserTestLocal;

	{
		// Styles alone are not a tree. Reported once, about the file, because there is no place in it
		// to point at.
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		const bool bParsed = Parse(MakeSource({
			TEXT("style Card {"),
			TEXT("    A = 1"),
			TEXT("}")
		}), Ast, Diagnostics);

		TestFalse(TEXT("a file with no root is refused"), bParsed);
		TestEqual(TEXT("with one complaint"), Diagnostics.Diagnostics.Num(), 1);
		TestTrue(TEXT("about the root"), Reported(Diagnostics, EDreamUIDiagnosticCode::MalformedRoot));
		TestFalse(TEXT("and the tree says it has no root"), Ast.bHasRoot);
		TestEqual(TEXT("though the styles it did read are kept"), Ast.Styles.Num(), 1);
	}

	{
		// The first root wins and the intruder is named where it stands, so a paste error points at
		// the paste rather than at the file.
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		const bool bParsed = Parse(MakeSource({
			TEXT("Widget First { }"),
			TEXT("Widget Second { }")
		}), Ast, Diagnostics);

		TestFalse(TEXT("a file with two roots is refused"), bParsed);
		TestEqual(TEXT("with one complaint"), Diagnostics.Diagnostics.Num(), 1);
		if (const FDreamUIDiagnostic* Diagnostic = FirstOf(Diagnostics, EDreamUIDiagnosticCode::MalformedRoot))
		{
			TestEqual(TEXT("pointing at the second one"), Diagnostic->Location.Line, 2);
		}
		else
		{
			AddError(TEXT("the second root was not reported"));
		}
		TestTrue(TEXT("and the first root is the one kept"), Ast.bHasRoot);
		TestEqual(TEXT("by name"), Ast.Root.Id, FString(TEXT("First")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUITextClauseDiagnosticsTest,
	"DreamGUI.Text.TheHeaderAndEachNodeClauseFailWithTheirOwnCode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUITextClauseDiagnosticsTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIParserTestLocal;

	ExpectOneDiagnostic(*this, TEXT("a second class declaration"), MakeSource({
		TEXT("class /Game/UI/A"),
		TEXT("class /Game/UI/B"),
		TEXT("Widget Root { }")
	}), EDreamUIDiagnosticCode::MalformedClassDeclaration, 2);

	ExpectOneDiagnostic(*this, TEXT("a class declaration without a path"), MakeSource({
		TEXT("class Widget"),
		TEXT("Widget Root { }")
	}), EDreamUIDiagnosticCode::MalformedClassDeclaration, 1);

	ExpectOneDiagnostic(*this, TEXT("a class declaration inside a node"), MakeSource({
		TEXT("Widget Root {"),
		TEXT("    class /Game/UI/A"),
		TEXT("}")
	}), EDreamUIDiagnosticCode::MalformedClassDeclaration, 2);

	ExpectOneDiagnostic(*this, TEXT("a rename clause missing its colon"), MakeSource({
		TEXT("Widget Root {"),
		TEXT("    Text Ok (was OkLabel) { }"),
		TEXT("}")
	}), EDreamUIDiagnosticCode::MalformedWasClause, 2);

	// The override renames a localization entry, and only a string produces one. On a number it would
	// name an entry that never gets created -- a setting that looks applied and does nothing.
	ExpectOneDiagnostic(*this, TEXT("a key override on a number"), MakeSource({
		TEXT("Widget Root {"),
		TEXT("    A = 24 @key(\"X\")"),
		TEXT("}")
	}), EDreamUIDiagnosticCode::MalformedKeyOverride, 2);

	ExpectOneDiagnostic(*this, TEXT("a key override whose key is not quoted"), MakeSource({
		TEXT("Widget Root {"),
		TEXT("    A = \"x\" @key(Y)"),
		TEXT("}")
	}), EDreamUIDiagnosticCode::MalformedKeyOverride, 2);

	ExpectOneDiagnostic(*this, TEXT("a loop header missing its 'in'"), MakeSource({
		TEXT("Widget Root {"),
		TEXT("    for Item GetItems() { }"),
		TEXT("}")
	}), EDreamUIDiagnosticCode::MalformedLoopHeader, 2);

	ExpectOneDiagnostic(*this, TEXT("a loop source written without brackets"), MakeSource({
		TEXT("Widget Root {"),
		TEXT("    each Item in GetItems { }"),
		TEXT("}")
	}), EDreamUIDiagnosticCode::MalformedLoopHeader, 2);

	// The clauses are accepted in either order. The canonical spelling puts the rename first, but
	// refusing the other order buys nothing -- neither reading is ambiguous.
	{
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		const bool bParsed = Parse(MakeSource({
			TEXT("style Card { A = 1 }"),
			TEXT("Widget Root {"),
			TEXT("    Text Ok : Card (was: OkLabel) { }"),
			TEXT("}")
		}), Ast, Diagnostics);

		TestTrue(TEXT("a style clause before a rename clause parses"), bParsed);
		if (TestEqual(TEXT("producing one child"), Ast.Root.Children.Num(), 1))
		{
			TestEqual(TEXT("with its style"), Ast.Root.Children[0].StyleName, FString(TEXT("Card")));
			TestEqual(TEXT("and its old id"), Ast.Root.Children[0].WasId, FString(TEXT("OkLabel")));
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUITextNameCollisionTest,
	"DreamGUI.Text.NamesThatCollideAreRefusedRatherThanFixedUp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUITextNameCollisionTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIParserTestLocal;

	// Never uniquified into Bg_1. The id is the node's identity all at once -- guid, member variable,
	// binding key, localization key -- so inventing a name for the author would silently repoint
	// whichever of the two their bindings meant.
	ExpectOneDiagnostic(*this, TEXT("two nodes sharing an id"), MakeSource({
		TEXT("Widget Root {"),
		TEXT("    Image Bg { }"),
		TEXT("    Text Bg { }"),
		TEXT("}")
	}), EDreamUIDiagnosticCode::DuplicateNodeId, 3);

	// Case insensitively, because this id becomes an FName member variable downstream and FName does
	// not distinguish case. Catching it here names both lines; catching it at class generation names
	// neither.
	ExpectOneDiagnostic(*this, TEXT("two ids differing only in case"), MakeSource({
		TEXT("Widget Root {"),
		TEXT("    Image Bg { }"),
		TEXT("    Text bg { }"),
		TEXT("}")
	}), EDreamUIDiagnosticCode::DuplicateNodeId, 3);

	// A named slot shares the id namespace with every other node, because it becomes a member
	// variable the same way. DuplicateSlotName is therefore never raised here -- see the handover note.
	ExpectOneDiagnostic(*this, TEXT("a slot colliding with a widget"), MakeSource({
		TEXT("Widget Root {"),
		TEXT("    Image Footer { }"),
		TEXT("    slot Footer"),
		TEXT("}")
	}), EDreamUIDiagnosticCode::DuplicateNodeId, 3);

	ExpectOneDiagnostic(*this, TEXT("an id that is a keyword"), MakeSource({
		TEXT("Widget Root {"),
		TEXT("    Image for { }"),
		TEXT("}")
	}), EDreamUIDiagnosticCode::InvalidNodeId, 2);

	// SanitizeIdentifier WOULD take this, by prefixing an underscore -- which is the silent rename
	// the id rule exists to prevent: the .dui would say 2ndPanel and the class would declare
	// _2ndPanel, and every binding written against the name in the file would resolve to nothing.
	ExpectOneDiagnostic(*this, TEXT("an id that is a bare digit"), MakeSource({
		TEXT("Widget Root {"),
		TEXT("    Image 2 { }"),
		TEXT("}")
	}), EDreamUIDiagnosticCode::InvalidNodeId, 2);

	// One complaint, not two. The lexer cannot tell 2ndPanel (a bad name) from 24px (a bad number),
	// so it defers rather than guessing -- otherwise this line would first be accused of being a
	// malformed number, which is true and useless.
	ExpectOneDiagnostic(*this, TEXT("an id that begins with a digit"), MakeSource({
		TEXT("Widget Root {"),
		TEXT("    Image 2ndPanel { }"),
		TEXT("}")
	}), EDreamUIDiagnosticCode::InvalidNodeId, 2);

	ExpectOneDiagnostic(*this, TEXT("a style declared twice"), MakeSource({
		TEXT("style Card { A = 1 }"),
		TEXT("style Card { B = 2 }"),
		TEXT("Widget Root { }")
	}), EDreamUIDiagnosticCode::DuplicateStyle, 2);

	// Resolved after the whole file rather than while parsing, so a style declared below the node
	// that wears it still resolves -- the check is about existence, not about reading order.
	ExpectOneDiagnostic(*this, TEXT("a style nothing declares"), MakeSource({
		TEXT("Widget Root {"),
		TEXT("    Image Bg : Missing { }"),
		TEXT("}")
	}), EDreamUIDiagnosticCode::UnknownStyle, 2);

	{
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		const bool bParsed = Parse(MakeSource({
			TEXT("Widget Root {"),
			TEXT("    Image Bg : Card { }"),
			TEXT("}"),
			TEXT("style Card { A = 1 }")
		}), Ast, Diagnostics);
		TestTrue(TEXT("a style declared after the node that wears it still resolves"), bParsed);
	}

	{
		// Kept as the first declaration, so everything downstream that asks for Card gets one answer.
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		Parse(MakeSource({
			TEXT("style Card { A = 1 }"),
			TEXT("style Card { B = 2 }"),
			TEXT("Widget Root { }")
		}), Ast, Diagnostics);
		if (TestEqual(TEXT("a duplicate style is dropped, not appended"), Ast.Styles.Num(), 1))
		{
			TestEqual(TEXT("and the first declaration is the one kept"), Ast.Styles[0].Properties[0].Name, FString(TEXT("A")));
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUITextShadowedLoopVariableTest,
	"DreamGUI.Text.AShadowedLoopVariableWarnsWithoutFailingTheParse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUITextShadowedLoopVariableTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIParserTestLocal;

	{
		// A warning rather than an error, deliberately: nothing in today's grammar can reference a
		// loop variable, so a shadowed one provably cannot change the tree that gets built. This case
		// also pins the contract that a warning does not fail the parse -- if shadowing later becomes
		// an error, this test is where that decision gets recorded.
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		const bool bParsed = Parse(MakeSource({
			TEXT("Widget Root {"),                             // 1
			TEXT("    for Item in GetItems() {"),              // 2
			TEXT("        for Item in GetSubItems() {"),       // 3
			TEXT("            Widget Leaf"),                   // 4
			TEXT("        }"),                                 // 5
			TEXT("    }"),                                     // 6
			TEXT("}")                                          // 7
		}), Ast, Diagnostics);

		TestTrue(TEXT("a shadowed loop variable does not fail the parse"), bParsed);
		TestFalse(TEXT("and the bag holds no errors"), Diagnostics.HasErrors());
		TestEqual(TEXT("just the one remark"), Diagnostics.Diagnostics.Num(), 1);
		if (const FDreamUIDiagnostic* Diagnostic = FirstOf(Diagnostics, EDreamUIDiagnosticCode::ShadowedLoopVariable))
		{
			TestFalse(TEXT("raised as a warning"), Diagnostic->IsError());
			TestEqual(TEXT("on the inner loop's line"), Diagnostic->Location.Line, 3);
			TestEqual(TEXT("at the variable, not the keyword"), Diagnostic->Location.Column, 13);
		}
		else
		{
			AddError(TEXT("the shadowed loop variable was not remarked on"));
		}
	}

	{
		// Two loops side by side reuse nothing. This is the case that catches a scope stack which
		// pushes and forgets to pop -- it would report the second loop as shadowing the first.
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		const bool bParsed = Parse(MakeSource({
			TEXT("Widget Root {"),
			TEXT("    for Item in GetA() { }"),
			TEXT("    for Item in GetB() { }"),
			TEXT("}")
		}), Ast, Diagnostics);

		TestTrue(TEXT("sibling loops may share a variable name"), bParsed);
		TestEqual(TEXT("with nothing remarked on"), Diagnostics.Diagnostics.Num(), 0);
		if (TestEqual(TEXT("and both loops are in the tree"), Ast.Root.Children.Num(), 2))
		{
			TestEqual(TEXT("the first as a compile-time loop"), static_cast<int32>(Ast.Root.Children[0].Kind),
				static_cast<int32>(EDreamUINodeKind::ForLoop));
			TestEqual(TEXT("naming its source"), Ast.Root.Children[0].LoopSourceFunction, FString(TEXT("GetA")));
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUITextErrorRecoveryTest,
	"DreamGUI.Text.OneBadFileReportsAllOfItsProblemsAtOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUITextErrorRecoveryTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIParserTestLocal;

	// The whole reason recovery exists. A generated .dui arrives with several mistakes of the same
	// shape, and a front end that stops at the first turns one round trip into five. Four distinct
	// causes here, on four known lines, from one parse.
	const FString Source = MakeSource({
		TEXT("class /Game/UI/A"),                //  1
		TEXT("Widget Root {"),                   //  2
		TEXT("    FontSize ="),                  //  3  no value
		TEXT("    Brush.TintColor = #12345"),    //  4  five hex digits
		TEXT("    Image"),                       //  5  no id
		TEXT("    Text Title { }"),              //  6
		TEXT("    Text Title { }"),              //  7  duplicate id
		TEXT("}")                                //  8
	});

	FDreamUIAst Ast;
	FDreamUIDiagnosticBag Diagnostics;
	const bool bParsed = Parse(Source, Ast, Diagnostics);

	TestFalse(TEXT("the file is refused"), bParsed);
	TestTrue(TEXT("the bag knows it holds errors"), Diagnostics.HasErrors());
	TestEqual(TEXT("all four problems are reported from one parse"), Diagnostics.NumErrors(), 4);

	TestTrue(TEXT("the missing value is reported"), Reported(Diagnostics, EDreamUIDiagnosticCode::MissingPropertyValue));
	TestTrue(TEXT("the bad colour is reported"), Reported(Diagnostics, EDreamUIDiagnosticCode::MalformedHexColor));
	TestTrue(TEXT("the missing id is reported"), Reported(Diagnostics, EDreamUIDiagnosticCode::MissingNodeId));
	TestTrue(TEXT("the duplicate id is reported"), Reported(Diagnostics, EDreamUIDiagnosticCode::DuplicateNodeId));

	if (const FDreamUIDiagnostic* Diagnostic = FirstOf(Diagnostics, EDreamUIDiagnosticCode::MissingPropertyValue))
	{
		TestEqual(TEXT("on its own line"), Diagnostic->Location.Line, 3);
	}
	if (const FDreamUIDiagnostic* Diagnostic = FirstOf(Diagnostics, EDreamUIDiagnosticCode::MalformedHexColor))
	{
		TestEqual(TEXT("on its own line"), Diagnostic->Location.Line, 4);
	}
	if (const FDreamUIDiagnostic* Diagnostic = FirstOf(Diagnostics, EDreamUIDiagnosticCode::MissingNodeId))
	{
		TestEqual(TEXT("on its own line"), Diagnostic->Location.Line, 5);
	}
	if (const FDreamUIDiagnostic* Diagnostic = FirstOf(Diagnostics, EDreamUIDiagnosticCode::DuplicateNodeId))
	{
		TestEqual(TEXT("on the second of the two lines"), Diagnostic->Location.Line, 7);
	}

	// Recovery kept going rather than abandoning the tree, which is what makes the later diagnostics
	// possible at all -- the duplicate id on line 7 is only findable if lines 3 to 6 did not derail it.
	TestTrue(TEXT("and a tree still came out the other side"), Ast.bHasRoot);
	TestEqual(TEXT("with the class path it declared"), Ast.ClassPath, FString(TEXT("/Game/UI/A")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUITextDiagnosticFormattingTest,
	"DreamGUI.Text.ADiagnosticPrintsAsAJumpableFileLineColumn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUITextDiagnosticFormattingTest::RunTest(const FString& Parameters)
{
	// The format is MSVC's because that is what every editor's problem matcher already parses. This
	// is the one place the wording IS the contract, so it gets the one assertion on text in this file.
	TestEqual(TEXT("a code prints as four padded digits"),
		FDreamUIDiagnostic::CodeToString(EDreamUIDiagnosticCode::DuplicateNodeId), FString(TEXT("DUI3001")));
	TestEqual(TEXT("including the low end of a band"),
		FDreamUIDiagnostic::CodeToString(EDreamUIDiagnosticCode::UnexpectedCharacter), FString(TEXT("DUI1001")));

	FDreamUIDiagnosticBag Diagnostics;
	Diagnostics.SourceName = TEXT("Login.dui");
	Diagnostics.AddError(EDreamUIDiagnosticCode::DuplicateNodeId, FDreamUISourceLocation(42, 9), TEXT("two nodes are named 'OkBtn'"));
	Diagnostics.AddWarning(EDreamUIDiagnosticCode::ShadowedLoopVariable, FDreamUISourceLocation(7, 5), TEXT("'Item' shadows an outer loop"));

	if (TestEqual(TEXT("both landed in the bag"), Diagnostics.Diagnostics.Num(), 2))
	{
		TestEqual(TEXT("an error prints file, line, column, severity, code and text"),
			Diagnostics.Diagnostics[0].ToString(),
			FString(TEXT("Login.dui(42,9): error DUI3001: two nodes are named 'OkBtn'")));
		TestEqual(TEXT("a warning differs only in the severity word"),
			Diagnostics.Diagnostics[1].ToString(),
			FString(TEXT("Login.dui(7,5): warning DUI3008: 'Item' shadows an outer loop")));
	}

	// The bag counts errors, not diagnostics: a caller asks it one question and the warning above
	// must not answer it.
	TestEqual(TEXT("only the error counts as one"), Diagnostics.NumErrors(), 1);
	TestTrue(TEXT("and the bag reports that it holds one"), Diagnostics.HasErrors());

	// A location the parser never filled in degrades to a file name rather than printing (0,0), which
	// would read as a position rather than as the absence of one.
	FDreamUIDiagnostic Placeless(EDreamUIDiagnosticCode::SourceFileUnreadable, FDreamUISourceLocation(), TEXT("cannot read the file"));
	Placeless.SourceName = TEXT("Missing.dui");
	TestEqual(TEXT("a diagnostic with no position names only its file"),
		Placeless.ToString(), FString(TEXT("Missing.dui: error DUI6001: cannot read the file")));

	Diagnostics.Reset();
	TestEqual(TEXT("a reset bag is empty"), Diagnostics.Diagnostics.Num(), 0);
	TestFalse(TEXT("and holds no errors"), Diagnostics.HasErrors());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUITextBagIsPerFileTest,
	"DreamGUI.Text.OneSharedBagStillGivesAPerFileAnswer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUITextBagIsPerFileTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIParserTestLocal;

	// The compiler collects a whole project into one message log, so "did this file parse" cannot be
	// answered by asking the bag whether it holds errors. A bag arriving dirty must not make a clean
	// file look broken -- and the already-stamped diagnostics must keep naming the file they came from.
	FDreamUIDiagnosticBag Shared;

	FDreamUIAst BadAst;
	const bool bBadParsed = FDreamUISourceFile::Parse(TEXT("Widget {"), TEXT("Broken.dui"), BadAst, Shared);
	TestFalse(TEXT("the broken file is refused"), bBadParsed);
	TestTrue(TEXT("and left something in the bag"), Shared.Diagnostics.Num() > 0);

	FDreamUIAst GoodAst;
	const bool bGoodParsed = FDreamUISourceFile::Parse(TEXT("Widget Root { A = 1 }"), TEXT("Fine.dui"), GoodAst, Shared);
	TestTrue(TEXT("a clean file parsed into the same bag still reports success"), bGoodParsed);
	TestTrue(TEXT("even though the bag as a whole still holds errors"), Shared.HasErrors());
	TestTrue(TEXT("and the clean file produced a tree"), GoodAst.bHasRoot);

	if (Shared.Diagnostics.Num() > 0)
	{
		TestEqual(TEXT("the earlier diagnostic still names the file it came from"),
			Shared.Diagnostics[0].SourceName, FString(TEXT("Broken.dui")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUITextNonAsciiNameTest,
	"DreamGUI.Text.ANonAsciiNameIsALegalIdentifierAndSurvivesUnchanged",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUITextNonAsciiNameTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIParserTestLocal;

	// A node's id IS its DisplayName, and the runtime deliberately keeps non-ASCII display names --
	// UDreamWidgetTree::SanitizeIdentifier passes anything above 0x7F straight through, and there is
	// a case in DreamUserWidgetAutomationTests pinning that a CJK name survives as a variable name.
	// An ASCII-only .dui would therefore be a step BACKWARDS from the designer for a Chinese-speaking
	// team: a name they can type into the details panel would be one they cannot write in the file
	// the panel is supposed to be a front end for.
	const FString Source = MakeSource({
		TEXT("style 卡片 {"),                        // 1
		TEXT("    RenderOpacity = 0.95"),            // 2
		TEXT("}"),                                   // 3
		TEXT("Widget 根节点 {"),                      // 4
		TEXT("    字体大小 = 24"),                    // 5
		TEXT("    Image 背景 : 卡片 { }"),            // 6
		TEXT("    Text 按钮_OK (was: 旧名字) { }"),   // 7
		TEXT("    slot 页脚"),                        // 8
		TEXT("}")                                    // 9
	});

	FDreamUIAst Ast;
	FDreamUIDiagnosticBag Diagnostics;
	if (!TestTrue(TEXT("a file named entirely in Chinese parses"), Parse(Source, Ast, Diagnostics)))
	{
		AddInfo(Diagnostics.ToString());
		return false;
	}
	TestEqual(TEXT("with nothing reported"), Diagnostics.Diagnostics.Num(), 0);

	TestEqual(TEXT("the root keeps its id byte for byte"), Ast.Root.Id, FString(TEXT("根节点")));
	TestEqual(TEXT("and starts in the first column, counted in characters"), Ast.Root.Location.Column, 1);

	if (TestEqual(TEXT("a property may be named in Chinese too"), Ast.Root.Properties.Num(), 1))
	{
		TestEqual(TEXT("kept as written"), Ast.Root.Properties[0].Name, FString(TEXT("字体大小")));
		// Columns count CHARACTERS, not bytes -- four spaces, four CJK characters, ' = ', and the
		// value is at 12. A column counted in UTF-8 bytes would say 20 here and send the write-back
		// patcher eight characters past the value it meant to replace.
		TestEqual(TEXT("a property starts past its indent"), Ast.Root.Properties[0].Location.Column, 5);
		TestEqual(TEXT("and its value's column counts characters, not bytes"), Ast.Root.Properties[0].Value.Location.Column, 12);
	}

	if (TestEqual(TEXT("all three children came through"), Ast.Root.Children.Num(), 3))
	{
		TestEqual(TEXT("a style may be named in Chinese"), Ast.Root.Children[0].StyleName, FString(TEXT("卡片")));
		TestEqual(TEXT("a name may mix scripts and underscores"), Ast.Root.Children[1].Id, FString(TEXT("按钮_OK")));
		TestEqual(TEXT("so may a rename clause's old id"), Ast.Root.Children[1].WasId, FString(TEXT("旧名字")));
		TestEqual(TEXT("and a named slot"), Ast.Root.Children[2].Id, FString(TEXT("页脚")));
		TestEqual(TEXT("which is still a slot"), static_cast<int32>(Ast.Root.Children[2].Kind),
			static_cast<int32>(EDreamUINodeKind::NamedSlot));
	}
	TestNotNull(TEXT("and the Chinese style resolves"), Ast.FindStyle(TEXT("卡片")));

	// The assertion this test exists for. The parser's identifier rule is a COPY of
	// SanitizeIdentifier's, and the property that matters is not that the two look alike but that an
	// id this parser accepts comes out of the sanitizer unchanged. Let them drift and the .dui says
	// one name while the generated class declares another, silently, with every binding against that
	// name resolving to nothing and no diagnostic anywhere.
	Ast.ForEachNode([this](const FDreamUINode& InNode)
	{
		if (!InNode.Id.IsEmpty())
		{
			TestEqual(TEXT("an accepted id is already a legal variable name"),
				UDreamWidgetTree::SanitizeIdentifier(InNode.Id), InNode.Id);
		}
	});

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUITextPrintedNumberRoundTripTest,
	"DreamGUI.Text.EveryPrintedNumberLexesBackWithoutComplaint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUITextPrintedNumberRoundTripTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIParserTestLocal;

	/*
	 * The seam between the printer and the lexer, driven through the REAL lexer.
	 *
	 * DreamUIValueFormat has its own round-trip suite, and it is thorough, but it closes the loop
	 * with a hand-written relex of its own -- it had to, since the lexer did not exist when it was
	 * written. That is exactly the shape of test that cannot see an integration defect: both halves
	 * pass their own suite while the pair is broken. And the pair WAS broken. Print falls back to
	 * scientific notation for large and tiny magnitudes, `e` is an ordinary identifier character, and
	 * the lexer called `1e+20` a malformed number -- so a designer dragging an anchor to an extreme
	 * value wrote a file that no longer compiled, on a line nobody had touched by hand.
	 *
	 * So the assertion here is deliberately end to end and deliberately boring: print a value, drop
	 * the text into a .dui line, parse it, and require that nothing was reported and that the text
	 * came back identical. Anything the printer can emit, the lexer must accept.
	 */
	const UScriptStruct* FixtureStruct = FDreamUIValueFormatFixture::StaticStruct();
	const FProperty* Vector2fProperty = FixtureStruct->FindPropertyByName(TEXT("Vector2f"));
	const FProperty* Vector2DProperty = FixtureStruct->FindPropertyByName(TEXT("Vector2D"));
	const FProperty* MarginProperty = FixtureStruct->FindPropertyByName(TEXT("Margin"));
	const FProperty* ColorProperty = FixtureStruct->FindPropertyByName(TEXT("LinearColor"));
	if (!TestNotNull(TEXT("the shared fixture declares Vector2f"), Vector2fProperty)
		|| !TestNotNull(TEXT("and Vector2D"), Vector2DProperty)
		|| !TestNotNull(TEXT("and Margin"), MarginProperty)
		|| !TestNotNull(TEXT("and LinearColor"), ColorProperty))
	{
		return false;
	}

	FDreamUIValueFormatFixture Values;

	auto CheckLexesBack = [this, &Values](const TCHAR* InWhat, const FProperty* InProperty)
	{
		FString Printed;
		if (!TestTrue(FString::Printf(TEXT("%s has a short form to print"), InWhat),
			DreamUIValueFormat::Print(InProperty, InProperty->ContainerPtrToValuePtr<uint8>(&Values), Printed)))
		{
			return;
		}

		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		const FString Source = FString::Printf(TEXT("Widget R {\n    P = %s\n}"), *Printed);
		if (!TestTrue(FString::Printf(TEXT("%s lexes back with nothing to complain about"), InWhat),
			FDreamUISourceFile::Parse(Source, TEXT("Printed.dui"), Ast, Diagnostics)))
		{
			AddInfo(FString::Printf(TEXT("the printer wrote: %s"), *Printed));
			AddInfo(Diagnostics.ToString());
			return;
		}
		if (!TestEqual(*FString::Printf(TEXT("%s parses to one property"), InWhat), Ast.Root.Properties.Num(), 1))
		{
			return;
		}

		const FDreamUIValue& Value = Ast.Root.Properties[0].Value;
		FString AsWritten = Value.Raw;
		if (Value.Kind == EDreamUIValueKind::HexColor)
		{
			// Raw drops the '#' the way a string's Raw drops its quotes, so put it back before
			// comparing against the whole line's worth of text the printer produced.
			AsWritten = TEXT("#") + AsWritten;
		}
		TestEqual(*FString::Printf(TEXT("%s survives as the very same text"), InWhat), AsWritten, Printed);
	};

	// Ordinary UI magnitudes first, so a failure below is unmistakably about the extremes rather
	// than about the whole mechanism.
	Values.Vector2D = FVector2D(400.0, 240.0);
	CheckLexesBack(TEXT("an ordinary size"), Vector2DProperty);

	Values.Margin = FMargin(8.0f, 8.0f, 8.0f, 8.0f);
	CheckLexesBack(TEXT("an ordinary margin"), MarginProperty);

	Values.LinearColor = FLinearColor(0.1f, 0.2f, 0.3f, 0.4f);
	CheckLexesBack(TEXT("a colour with alpha"), ColorProperty);

	// And now the values that make Print reach for an exponent. These are the cases that were
	// actually broken, and they are the reason this test is not optional.
	Values.Vector2f = FVector2f(1.0e20f, 1.0e-45f);
	CheckLexesBack(TEXT("a float pair at both extremes"), Vector2fProperty);

	Values.Vector2f = FVector2f(TNumericLimits<float>::Max(), TNumericLimits<float>::Lowest());
	CheckLexesBack(TEXT("the largest float and the smallest"), Vector2fProperty);

	Values.Vector2f = FVector2f(TNumericLimits<float>::Min(), -TNumericLimits<float>::Min());
	CheckLexesBack(TEXT("the smallest normal float either way up"), Vector2fProperty);

	// Not an extreme, but the value that needs every digit it has -- the one a six-decimal printer
	// gets wrong without ever reaching for an exponent.
	Values.Vector2f = FVector2f(1.0f / 3.0f, -1.0f / 7.0f);
	CheckLexesBack(TEXT("a float that needs all of its digits"), Vector2fProperty);

	Values.Vector2D = FVector2D(1.0e300, -1.0e-300);
	CheckLexesBack(TEXT("a double pair past any float's range"), Vector2DProperty);

	Values.Vector2D = FVector2D(TNumericLimits<double>::Max(), TNumericLimits<double>::Lowest());
	CheckLexesBack(TEXT("the largest double and the smallest"), Vector2DProperty);

	// A tuple whose elements disagree about how they want to be written. Elements are raw source
	// slices, so this is also the case that proves an exponent survives being cut out of one.
	Values.Margin = FMargin(1.0e-45f, 0.0f, 1.0e20f, 0.5f);
	CheckLexesBack(TEXT("a margin mixing plain and exponent edges"), MarginProperty);

	return true;
}

#endif
