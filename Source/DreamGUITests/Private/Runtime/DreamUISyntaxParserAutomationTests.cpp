// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Text/DreamUIAst.h"
#include "Text/DreamUIDiagnostics.h"
#include "Text/DreamUISourceFile.h"

/*
 * The newer .dui spellings, as the front end reads them: `use … as` (a component by file or by path, a library under a
 * namespace, the aliases a library passes on), `ns.` names in style clauses and resource references, anonymous nodes,
 * the `@slot` block and the `@fill` shorthands, styles that carry components and slot lines, `props`, `events`, `emit`
 * routes, slot declarations with a layout and a default, a host filling a component's slot, and `if` / `else`.
 *
 * Strings in, AST and diagnostics out, not one UObject: the parser has to stay runnable where no engine object exists,
 * and these tests are the proof that it does. Imports are served from a map of spellings, so a test says exactly which
 * files exist. As in the older parser tests, a refusal is asserted by its CODE and its line, never by its wording, and
 * the count of diagnostics is asserted with it -- a recovery path that stumbles shows up as the right code followed by
 * an invented one, which "contains the code" cannot see.
 *
 * And every one of these is ADDITIVE: the words they use stay ordinary names wherever they do not lead the statement
 * they belong to, which the last tests here hold the parser to.
 */

namespace DreamUISyntaxParserTestLocal
{
	/** Fixtures are written a line at a time so a test can say "line 3" and mean the third entry. */
	FString MakeSource(const TArray<FString>& InLines)
	{
		return FString::Join(InLines, TEXT("\n"));
	}

	/** `use` imports served from a map: the spelling is the key, and the resolved path is the spelling under /virtual/. */
	TFunction<bool(const FString&, FString&, FString&)> MakeMapReader(TMap<FString, FString> InFiles)
	{
		return [Files = MoveTemp(InFiles)](const FString& InSpelling, FString& OutResolvedPath, FString& OutText)
		{
			const FString* Found = Files.Find(InSpelling);
			if (Found == nullptr)
			{
				return false;
			}
			OutResolvedPath = TEXT("/virtual/") + InSpelling;
			OutText = *Found;
			return true;
		};
	}

	/** Parses as /virtual/Main.dui (or InSourceName) with InFiles to import from. */
	bool ParseWith(const FString& InSource, TMap<FString, FString> InFiles, FDreamUIAst& OutAst,
		FDreamUIDiagnosticBag& OutDiagnostics, const TCHAR* InSourceName = TEXT("/virtual/Main.dui"))
	{
		return FDreamUISourceFile::Parse(InSource, InSourceName, OutAst, OutDiagnostics, MakeMapReader(MoveTemp(InFiles)));
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

	bool IsCall(const FDreamUIExpression& InExpression, const TCHAR* InName)
	{
		return InExpression.Kind == FDreamUIExpression::EKind::Call && InExpression.Symbol == InName;
	}

	bool IsVariable(const FDreamUIExpression& InExpression, const TCHAR* InName)
	{
		return InExpression.Kind == FDreamUIExpression::EKind::VariableRef && InExpression.Symbol == InName;
	}

	bool IsResourceLiteral(const FDreamUIExpression& InExpression, const TCHAR* InName)
	{
		return InExpression.Kind == FDreamUIExpression::EKind::Literal
			&& InExpression.LiteralKind == EDreamUIValueKind::ResourceRef
			&& InExpression.LiteralRaw.Equals(InName, ESearchCase::CaseSensitive);
	}

	bool IsNot(const FDreamUIExpression& InExpression)
	{
		return InExpression.Kind == FDreamUIExpression::EKind::Unary && InExpression.Symbol == TEXT("!")
			&& InExpression.Operands.Num() == 1;
	}

	bool IsAnd(const FDreamUIExpression& InExpression)
	{
		return InExpression.Kind == FDreamUIExpression::EKind::Binary && InExpression.Symbol == TEXT("&&")
			&& InExpression.Operands.Num() == 2;
	}

	void Dump(FAutomationTestBase& InTest, const FDreamUIDiagnosticBag& InDiagnostics)
	{
		for (const FDreamUIDiagnostic& Diagnostic : InDiagnostics.Diagnostics)
		{
			InTest.AddInfo(Diagnostic.ToString());
		}
	}

	/** A clean parse, or the diagnostics written into the test's log so the failure says what went wrong. */
	bool ExpectClean(FAutomationTestBase& InTest, const TCHAR* InWhat, bool bInParsed, const FDreamUIDiagnosticBag& InDiagnostics)
	{
		const bool bClean = InTest.TestTrue(*FString::Printf(TEXT("%s parses"), InWhat), bInParsed)
			& InTest.TestEqual(*FString::Printf(TEXT("%s has nothing to complain about"), InWhat), InDiagnostics.Diagnostics.Num(), 0);
		if (!bClean)
		{
			Dump(InTest, InDiagnostics);
		}
		return bClean;
	}

	/** One mistake, one diagnostic, under the expected code, on the expected line. */
	bool ExpectOneDiagnostic(FAutomationTestBase& InTest, const TCHAR* InWhat, const FString& InSource,
		EDreamUIDiagnosticCode InCode, int32 InLine, TMap<FString, FString> InFiles = TMap<FString, FString>())
	{
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		const bool bParsed = ParseWith(InSource, MoveTemp(InFiles), Ast, Diagnostics);

		bool bPassed = InTest.TestFalse(*FString::Printf(TEXT("%s is refused"), InWhat), bParsed);
		bPassed &= InTest.TestEqual(*FString::Printf(TEXT("%s produces exactly one complaint"), InWhat),
			Diagnostics.Diagnostics.Num(), 1);
		if (Diagnostics.Diagnostics.Num() != 1)
		{
			Dump(InTest, Diagnostics);
			return false;
		}
		bPassed &= InTest.TestEqual(*FString::Printf(TEXT("%s is reported under the expected code"), InWhat),
			static_cast<int32>(Diagnostics.Diagnostics[0].Code), static_cast<int32>(InCode));
		bPassed &= InTest.TestEqual(*FString::Printf(TEXT("%s is reported on the expected line"), InWhat),
			Diagnostics.Diagnostics[0].Location.Line, InLine);
		if (!bPassed)
		{
			Dump(InTest, Diagnostics);
		}
		return bPassed;
	}
}

// ------------------------------------------------------------------------------------------------
// use … as
// ------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUISyntaxParseComponentUseTest,
	"DreamGUI.Text.Syntax.AUseAsOfAFileWithARootNamesItsClassAndMergesNothingOfIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * `use "Components/Row.dui" as Row` -- a file with a root is a component, and the importer takes its class and nothing
 * else: its styles and resources stay its own. The component's own `use` lines are not followed (it is read for its
 * header alone), which is asserted through Imports, the list the watcher recompiles from.
 */
bool FDreamUISyntaxParseComponentUseTest::RunTest(const FString& Parameters)
{
	using namespace DreamUISyntaxParserTestLocal;

	TMap<FString, FString> Files;
	Files.Add(TEXT("Components/Row.dui"), MakeSource({
		TEXT("class /Game/UI/WBP_Row"),
		TEXT("use \"Common.dui\""),
		TEXT("resources { Color RowInk = #101010 }"),
		TEXT("style RowOnly { RenderOpacity = 0.5 }"),
		TEXT("Widget Root : RowOnly { }")
	}));
	Files.Add(TEXT("Components/Bare.dui"), TEXT("Widget Root { }"));

	FDreamUIAst Ast;
	FDreamUIDiagnosticBag Diagnostics;
	const bool bParsed = ParseWith(MakeSource({
		TEXT("use \"Components/Row.dui\" as Row"),   // 1
		TEXT("use \"Components/Bare.dui\" as Bare"), // 2
		TEXT("Widget Root {"),                       // 3
		TEXT("    Row Row1 { }"),                    // 4
		TEXT("    Bare { }"),                        // 5
		TEXT("}")                                    // 6
	}), Files, Ast, Diagnostics);
	if (!ExpectClean(*this, TEXT("a file using two components"), bParsed, Diagnostics))
	{
		return false;
	}

	if (TestEqual(TEXT("both lines are aliases of this file's own"), Ast.ComponentAliases.Num(), 2))
	{
		const FDreamUIComponentAlias& Row = Ast.ComponentAliases[0];
		TestEqual(TEXT("the first is called what its 'as' says"), Row.Alias, FString(TEXT("Row")));
		TestEqual(TEXT("and names the class its file's class line gives"), Row.ClassPath, FString(TEXT("/Game/UI/WBP_Row")));
		TestEqual(TEXT("and the file it was read from, resolved"), Row.SourcePath, FString(TEXT("/virtual/Components/Row.dui")));
		TestEqual(TEXT("declared on the first line"), Row.Location.Line, 1);
		TestEqual(TEXT("at the name after 'as'"), Row.Location.Column, 29);
		TestEqual(TEXT("and stamped with the file that declared it"), Row.SourceName, FString(TEXT("/virtual/Main.dui")));

		const FDreamUIComponentAlias& Bare = Ast.ComponentAliases[1];
		TestTrue(TEXT("a component with no class line has no class path yet"), Bare.ClassPath.IsEmpty());
		TestEqual(TEXT("but its file is known"), Bare.SourcePath, FString(TEXT("/virtual/Components/Bare.dui")));
		TestEqual(TEXT("at the name after 'as'"), Bare.Location.Column, 30);
	}
	TestNotNull(TEXT("FindComponentAlias answers for the name, case insensitively"), Ast.FindComponentAlias(TEXT("row")));
	TestEqual(TEXT("nothing of the component's styles came along"), Ast.ImportedStyles.Num(), 0);
	TestEqual(TEXT("nor of its resources"), Ast.ImportedResources.Num(), 0);
	TestNull(TEXT("so its style is not in scope here"), Ast.FindStyle(TEXT("RowOnly")));
	TestEqual(TEXT("the watcher hears of both component files and of nothing they import"), Ast.Imports.Num(), 2);

	if (TestEqual(TEXT("the root holds both nodes"), Ast.Root.Children.Num(), 2))
	{
		TestEqual(TEXT("one typed by the alias as written"), Ast.Root.Children[0].TypeName, FString(TEXT("Row")));
		TestTrue(TEXT("and one typed by the other alias, with no id of its own"), Ast.Root.Children[1].bAnonymous
			&& Ast.Root.Children[1].TypeName == TEXT("Bare"));
	}

	// A file naming ITSELF as a component would be made of itself.
	{
		const FString Self = TEXT("use \"Self.dui\" as Me\nWidget Root { }");
		FDreamUIAst SelfAst;
		FDreamUIDiagnosticBag SelfDiagnostics;
		ParseWith(Self, {{TEXT("Self.dui"), Self}}, SelfAst, SelfDiagnostics, TEXT("/virtual/Self.dui"));
		TestEqual(TEXT("a file using itself as a component is one complaint"), SelfDiagnostics.Diagnostics.Num(), 1);
		TestTrue(TEXT("that the import cannot be honoured"), Reported(SelfDiagnostics, EDreamUIDiagnosticCode::ImportFailed));
	}

	ExpectOneDiagnostic(*this, TEXT("a component file that does not parse"), MakeSource({
		TEXT("use \"Broken.dui\" as Broken"),
		TEXT("Widget Root { }")
	}), EDreamUIDiagnosticCode::ImportFailed, 1, {{TEXT("Broken.dui"), TEXT("Widget Root {")}});

	ExpectOneDiagnostic(*this, TEXT("a component file that does not exist"), MakeSource({
		TEXT("use \"Missing.dui\" as Gone"),
		TEXT("Widget Root { }")
	}), EDreamUIDiagnosticCode::ImportFailed, 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUISyntaxParseComponentLibraryLoopTest,
	"DreamGUI.Text.Syntax.ALibraryMayNameAComponentThatStylesItselfFromThatLibrary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The shape a component family is actually written in: the library names the component (`use "Row.dui" as Row`), and
 * the component takes its styles from the library (`use "Lib.dui"`). Read whole, each would import the other and the
 * chain guard would call it a cycle; it is not one, because a component's class is all an importer takes from it.
 */
bool FDreamUISyntaxParseComponentLibraryLoopTest::RunTest(const FString& Parameters)
{
	using namespace DreamUISyntaxParserTestLocal;

	TMap<FString, FString> Files;
	Files.Add(TEXT("Lib.dui"), MakeSource({
		TEXT("use \"Row.dui\" as Row"),
		TEXT("style Card { RenderOpacity = 0.5 }")
	}));
	Files.Add(TEXT("Row.dui"), MakeSource({
		TEXT("use \"Lib.dui\""),
		TEXT("Widget Root : Card { }")
	}));

	{
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		const bool bParsed = ParseWith(MakeSource({
			TEXT("use \"Lib.dui\""),
			TEXT("Widget Root {"),
			TEXT("    Row Item { }"),
			TEXT("}")
		}), Files, Ast, Diagnostics);
		if (ExpectClean(*this, TEXT("a screen using the library"), bParsed, Diagnostics))
		{
			const FDreamUIComponentAlias* Row = Ast.FindComponentAlias(TEXT("Row"));
			if (TestNotNull(TEXT("the library's component came along with it"), Row))
			{
				TestEqual(TEXT("naming the component's file"), Row->SourcePath, FString(TEXT("/virtual/Row.dui")));
				TestEqual(TEXT("stamped with the library that declared it"), Row->SourceName, FString(TEXT("/virtual/Lib.dui")));
			}
			TestEqual(TEXT("as an imported alias, not one of the screen's own"), Ast.ComponentAliases.Num(), 0);
			TestNotNull(TEXT("and the library's style with it"), Ast.FindStyle(TEXT("Card")));
		}
	}
	{
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		const bool bParsed = ParseWith(Files[TEXT("Row.dui")], Files, Ast, Diagnostics, TEXT("/virtual/Row.dui"));
		if (ExpectClean(*this, TEXT("the component itself"), bParsed, Diagnostics))
		{
			TestNotNull(TEXT("styles itself from the library that names it"), Ast.FindStyle(TEXT("Card")));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUISyntaxParseClassPathUseTest,
	"DreamGUI.Text.Syntax.AUseOfAClassPathNeedsItsAsAndAnAsNeedsOneFreshName",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUISyntaxParseClassPathUseTest::RunTest(const FString& Parameters)
{
	using namespace DreamUISyntaxParserTestLocal;

	{
		// No reader at all: a class path needs no file read, so the plain overload takes it.
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		const bool bParsed = FDreamUISourceFile::Parse(MakeSource({
			TEXT("use /Game/UI/WBP_Row as Row"),
			TEXT("use /Script/DreamGUIControls.DreamButton as Button"),
			TEXT("Widget Root {"),
			TEXT("    Row Row1 { }"),
			TEXT("    Button Ok { }"),
			TEXT("}")
		}), TEXT("Main.dui"), Ast, Diagnostics);
		if (ExpectClean(*this, TEXT("two class-path aliases"), bParsed, Diagnostics)
			&& TestEqual(TEXT("both are this file's aliases"), Ast.ComponentAliases.Num(), 2))
		{
			TestEqual(TEXT("the first names its path"), Ast.ComponentAliases[0].ClassPath, FString(TEXT("/Game/UI/WBP_Row")));
			TestTrue(TEXT("and no file"), Ast.ComponentAliases[0].SourcePath.IsEmpty());
			TestEqual(TEXT("at the name after 'as'"), Ast.ComponentAliases[0].Location.Column, 25);
			TestEqual(TEXT("a script path is taken whole, dot and all"), Ast.ComponentAliases[1].ClassPath,
				FString(TEXT("/Script/DreamGUIControls.DreamButton")));
			TestEqual(TEXT("and nothing is listed for the watcher"), Ast.Imports.Num(), 0);
		}
	}

	ExpectOneDiagnostic(*this, TEXT("a class path with no 'as'"), MakeSource({
		TEXT("use /Game/UI/WBP_Row"),
		TEXT("Widget Root { }")
	}), EDreamUIDiagnosticCode::MalformedUseDeclaration, 1);

	ExpectOneDiagnostic(*this, TEXT("an 'as' with no name"), MakeSource({
		TEXT("use \"Row.dui\" as"),
		TEXT("Widget Root { }")
	}), EDreamUIDiagnosticCode::MalformedUseDeclaration, 1);

	ExpectOneDiagnostic(*this, TEXT("a dotted name after 'as'"), MakeSource({
		TEXT("use \"Row.dui\" as nier.Row"),
		TEXT("Widget Root { }")
	}), EDreamUIDiagnosticCode::MalformedUseDeclaration, 1);

	ExpectOneDiagnostic(*this, TEXT("a keyword after 'as'"), MakeSource({
		TEXT("use /Game/UI/WBP_Row as for"),
		TEXT("Widget Root { }")
	}), EDreamUIDiagnosticCode::MalformedUseDeclaration, 1);

	{
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		ParseWith(MakeSource({
			TEXT("use /Game/UI/WBP_RowA as Row"),
			TEXT("use /Game/UI/WBP_RowB as Row"),
			TEXT("Widget Root { }")
		}), {}, Ast, Diagnostics);
		TestEqual(TEXT("a name given twice is one complaint"), Diagnostics.Diagnostics.Num(), 1);
		if (const FDreamUIDiagnostic* Duplicate = FirstOf(Diagnostics, EDreamUIDiagnosticCode::DuplicateComponentAlias))
		{
			TestEqual(TEXT("at the second line"), Duplicate->Location.Line, 2);
		}
		else
		{
			AddError(TEXT("the second alias of one name was not refused"));
		}
		TestTrue(TEXT("and the first declaration is the one kept"), Ast.ComponentAliases.Num() == 1
			&& Ast.ComponentAliases[0].ClassPath == TEXT("/Game/UI/WBP_RowA"));
	}

	// An alias and a namespace are spelled the same way after it (`nier.X`, `nier`), so they share the names.
	ExpectOneDiagnostic(*this, TEXT("a namespace named like an alias"), MakeSource({
		TEXT("use /Game/UI/WBP_Row as nier"),
		TEXT("use \"Lib.dui\" as nier"),
		TEXT("Widget Root { }")
	}), EDreamUIDiagnosticCode::DuplicateComponentAlias, 2, {{TEXT("Lib.dui"), TEXT("style Card { }")}});

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUISyntaxParseNamespaceTest,
	"DreamGUI.Text.Syntax.AUseAsOfALibraryEntersEverythingItHasUnderTheNamespace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * `use "Lib.dui" as nier` on a file with no root. Every style, resource and alias the library has -- its own and what
 * it imported itself, here a palette under its own namespace -- is entered as `nier.X`, and the references BETWEEN them
 * are renamed with them: a library style that took the library's Card must still take it, not whatever the importer
 * calls Card.
 */
bool FDreamUISyntaxParseNamespaceTest::RunTest(const FString& Parameters)
{
	using namespace DreamUISyntaxParserTestLocal;

	TMap<FString, FString> Files;
	Files.Add(TEXT("Palette.dui"), MakeSource({
		TEXT("style Base { RenderOpacity = 1 }"),
		TEXT("resources { Color Swatch = #FF0000 }")
	}));
	Files.Add(TEXT("Lib.dui"), MakeSource({
		TEXT("use \"Palette.dui\" as pal"),
		TEXT("use /Game/UI/WBP_Row as Row"),
		TEXT("resources {"),
		TEXT("    Color Ink = #101010"),
		TEXT("    Color Accent = @Ink"),
		TEXT("}"),
		TEXT("style Card { Color = @Ink  Tint <- Mix(@Ink, @pal.Swatch) }"),
		TEXT("style Danger : Card {"),
		TEXT("    Color = @Accent"),
		TEXT("    + VerticalBox { Gap = @Ink }"),
		TEXT("    @slot Padding = @Ink"),
		TEXT("}"),
		TEXT("style Uses : pal.Base { }"),
		TEXT("style Outside { Color = @HostInk }")
	}));

	FDreamUIAst Ast;
	FDreamUIDiagnosticBag Diagnostics;
	const bool bParsed = ParseWith(MakeSource({
		TEXT("use \"Lib.dui\" as nier"),                                // 1
		TEXT("Widget Root {"),                                          // 2
		TEXT("    nier.Row R1 : nier.Danger { Color = @nier.Ink }"),    // 3
		TEXT("    Image Bg { Tint <- Blend(@nier.Accent) }"),           // 4
		TEXT("}")                                                       // 5
	}), Files, Ast, Diagnostics);
	if (!ExpectClean(*this, TEXT("a file using a library under a namespace"), bParsed, Diagnostics))
	{
		return false;
	}

	TestTrue(TEXT("the namespace is declared"), Ast.Namespaces.Num() == 1 && Ast.Namespaces[0] == TEXT("nier"));
	TestNull(TEXT("the library's style is not entered under its own name"), Ast.FindStyle(TEXT("Card")));
	TestNull(TEXT("nor its resource"), Ast.FindResource(TEXT("Ink")));
	TestNull(TEXT("nor its alias"), Ast.FindComponentAlias(TEXT("Row")));

	if (const FDreamUIStyle* Card = Ast.FindStyle(TEXT("nier.Card")))
	{
		const FDreamUIProperty* Color = PropertyByName(Card->Properties, TEXT("Color"));
		TestTrue(TEXT("a reference to the library's own resource is renamed with it"),
			Color != nullptr && Color->Value.Raw == TEXT("nier.Ink"));
		const FDreamUIProperty* Tint = PropertyByName(Card->Properties, TEXT("Tint"));
		const bool bTintShape = Tint != nullptr && Tint->BindingExpression.IsSet()
			&& Tint->BindingExpression->Operands.Num() == 2;
		if (TestTrue(TEXT("a binding in a library style keeps its shape"), bTintShape))
		{
			TestTrue(TEXT("and its resource literals are renamed too"),
				IsResourceLiteral(Tint->BindingExpression->Operands[0], TEXT("nier.Ink")));
			TestTrue(TEXT("including one the library itself took under its own namespace"),
				IsResourceLiteral(Tint->BindingExpression->Operands[1], TEXT("nier.pal.Swatch")));
		}
	}
	else
	{
		AddError(TEXT("the library's Card is not reachable as nier.Card"));
	}

	if (const FDreamUIStyle* Danger = Ast.FindStyle(TEXT("nier.Danger")))
	{
		TestEqual(TEXT("a base the library declares is renamed with it"), Danger->BaseName, FString(TEXT("nier.Card")));
		TestTrue(TEXT("a resource reference on a style line"), Danger->Properties.Num() == 1
			&& Danger->Properties[0].Value.Raw == TEXT("nier.Accent"));
		TestTrue(TEXT("on a style's component"), Danger->Components.Num() == 1
			&& Danger->Components[0].Properties.Num() == 1 && Danger->Components[0].Properties[0].Value.Raw == TEXT("nier.Ink"));
		TestTrue(TEXT("and on a style's slot line"), Danger->SlotProperties.Num() == 1
			&& Danger->SlotProperties[0].Value.Raw == TEXT("nier.Ink"));
	}
	else
	{
		AddError(TEXT("the library's Danger is not reachable as nier.Danger"));
	}

	const FDreamUIStyle* Uses = Ast.FindStyle(TEXT("nier.Uses"));
	TestTrue(TEXT("a base from the library's own namespace is renamed into this one"),
		Uses != nullptr && Uses->BaseName == TEXT("nier.pal.Base"));
	TestNotNull(TEXT("and that base is here under that name"), Ast.FindStyle(TEXT("nier.pal.Base")));
	const FDreamUIStyle* Outside = Ast.FindStyle(TEXT("nier.Outside"));
	TestTrue(TEXT("a name the library does not declare is left for wherever it resolves"),
		Outside != nullptr && Outside->Properties.Num() == 1 && Outside->Properties[0].Value.Raw == TEXT("HostInk"));

	const FDreamUIResource* Accent = Ast.FindResource(TEXT("nier.Accent"));
	TestTrue(TEXT("a resource naming another of the library's is renamed with it"),
		Accent != nullptr && Accent->Value.Raw == TEXT("nier.Ink"));
	TestNotNull(TEXT("the library's palette resource is here under both namespaces"), Ast.FindResource(TEXT("nier.pal.Swatch")));

	const FDreamUIComponentAlias* Row = Ast.FindComponentAlias(TEXT("nier.Row"));
	TestTrue(TEXT("the library's alias is entered under the namespace"),
		Row != nullptr && Row->ClassPath == TEXT("/Game/UI/WBP_Row"));

	if (TestEqual(TEXT("the root holds both nodes"), Ast.Root.Children.Num(), 2))
	{
		const FDreamUINode& R1 = Ast.Root.Children[0];
		TestEqual(TEXT("a namespaced node type is joined as written"), R1.TypeName, FString(TEXT("nier.Row")));
		TestEqual(TEXT("a namespaced style clause is joined as written"), R1.StyleName, FString(TEXT("nier.Danger")));
		TestTrue(TEXT("a namespaced resource value keeps its dots in Raw"), R1.Properties.Num() == 1
			&& R1.Properties[0].Value.Kind == EDreamUIValueKind::ResourceRef && R1.Properties[0].Value.Raw == TEXT("nier.Ink"));
		TestEqual(TEXT("located at its '@'"), R1.Properties[0].Value.Location.Column, 41);

		const FDreamUINode& Bg = Ast.Root.Children[1];
		const bool bBlendShape = Bg.Properties.Num() == 1 && Bg.Properties[0].BindingExpression.IsSet()
			&& Bg.Properties[0].BindingExpression->Operands.Num() == 1;
		TestTrue(TEXT("and a namespaced resource inside an expression too"), bBlendShape
			&& IsResourceLiteral(Bg.Properties[0].BindingExpression->Operands[0], TEXT("nier.Accent")));
	}

	TestEqual(TEXT("the watcher hears of the library and of what it imports"), Ast.Imports.Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUISyntaxParseUnknownNamespaceTest,
	"DreamGUI.Text.Syntax.AQualifiedNameWhoseNamespaceNothingDeclaresIsReportedWhereItIsWritten",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUISyntaxParseUnknownNamespaceTest::RunTest(const FString& Parameters)
{
	using namespace DreamUISyntaxParserTestLocal;

	// Once, as the namespace: "no such style" on top would send the reader looking for a misspelt style.
	ExpectOneDiagnostic(*this, TEXT("a style clause under an undeclared namespace"), MakeSource({
		TEXT("Widget Root {"),
		TEXT("    Image Bg : nier.Card { }"),
		TEXT("}")
	}), EDreamUIDiagnosticCode::UnknownNamespace, 2);

	{
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		ParseWith(MakeSource({
			TEXT("Widget Root {"),
			TEXT("    Image Bg : nier.Card { }"),
			TEXT("}")
		}), {}, Ast, Diagnostics);
		if (const FDreamUIDiagnostic* Unknown = FirstOf(Diagnostics, EDreamUIDiagnosticCode::UnknownNamespace))
		{
			TestEqual(TEXT("a style clause's namespace is reported at the qualified name"), Unknown->Location.Column, 16);
		}
	}

	ExpectOneDiagnostic(*this, TEXT("a resource value under an undeclared namespace"), MakeSource({
		TEXT("Widget Root {"),
		TEXT("    Color = @nier.Ink"),
		TEXT("}")
	}), EDreamUIDiagnosticCode::UnknownNamespace, 2);

	ExpectOneDiagnostic(*this, TEXT("a resource in an expression under an undeclared namespace"), MakeSource({
		TEXT("Widget Root {"),
		TEXT("    Tint <- Mix(@nier.Ink)"),
		TEXT("}")
	}), EDreamUIDiagnosticCode::UnknownNamespace, 2);

	ExpectOneDiagnostic(*this, TEXT("a style's base under an undeclared namespace"), MakeSource({
		TEXT("style Danger : nier.Base { }"),
		TEXT("Widget Root { }")
	}), EDreamUIDiagnosticCode::UnknownNamespace, 1);

	ExpectOneDiagnostic(*this, TEXT("a resource node type under an undeclared namespace"), MakeSource({
		TEXT("Widget Root {"),
		TEXT("    @nier.Row R1 { }"),
		TEXT("}")
	}), EDreamUIDiagnosticCode::UnknownNamespace, 2);

	// The namespace is there, the style in it is not: that one IS a missing style.
	ExpectOneDiagnostic(*this, TEXT("a style missing from a declared namespace"), MakeSource({
		TEXT("use \"Lib.dui\" as nier"),
		TEXT("Widget Root {"),
		TEXT("    Image Bg : nier.Missing { }"),
		TEXT("}")
	}), EDreamUIDiagnosticCode::UnknownStyle, 3, {{TEXT("Lib.dui"), TEXT("style Card { }")}});

	{
		// A registry scope is not a namespace and needs no `use`: `Native` is the widget registry's.
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		const bool bParsed = ParseWith(MakeSource({
			TEXT("Widget Root {"),
			TEXT("    Native.Button Ok { }"),
			TEXT("}")
		}), {}, Ast, Diagnostics);
		ExpectClean(*this, TEXT("a scoped registry tag"), bParsed, Diagnostics);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUISyntaxParseReExportTest,
	"DreamGUI.Text.Syntax.APlainUseBringsTheLibrarysComponentAliasesAndADiamondBringsThemOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A library's `use … as Row` lines come along with the library, whichever way it is used -- the `index.js` of a
 * component family. Deduped the way styles are, by declaration: a diamond brings one alias once, while one library used
 * plainly AND under a namespace is two sets of names, both kept.
 */
bool FDreamUISyntaxParseReExportTest::RunTest(const FString& Parameters)
{
	using namespace DreamUISyntaxParserTestLocal;

	TMap<FString, FString> Files;
	Files.Add(TEXT("Common.dui"), MakeSource({
		TEXT("use \"Components/Row.dui\" as Row"),
		TEXT("use /Game/UI/WBP_Tab as Tab"),
		TEXT("style ListRow { RenderOpacity = 1 }")
	}));
	Files.Add(TEXT("Components/Row.dui"), MakeSource({
		TEXT("class /Game/UI/WBP_Row"),
		TEXT("Widget Root { }")
	}));
	{
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		const bool bParsed = ParseWith(MakeSource({
			TEXT("use \"Common.dui\""),
			TEXT("Widget Root {"),
			TEXT("    Row Item_0 : ListRow { }"),
			TEXT("    Tab Tab_0 { }"),
			TEXT("}")
		}), Files, Ast, Diagnostics);
		if (ExpectClean(*this, TEXT("a screen using a component library plainly"), bParsed, Diagnostics))
		{
			TestEqual(TEXT("the screen declares no alias of its own"), Ast.ComponentAliases.Num(), 0);
			TestEqual(TEXT("both of the library's came along"), Ast.ImportedComponentAliases.Num(), 2);
			const FDreamUIComponentAlias* Row = Ast.FindComponentAlias(TEXT("Row"));
			if (TestNotNull(TEXT("the file alias is reachable"), Row))
			{
				TestEqual(TEXT("with the class its file names"), Row->ClassPath, FString(TEXT("/Game/UI/WBP_Row")));
				TestEqual(TEXT("and that file"), Row->SourcePath, FString(TEXT("/virtual/Components/Row.dui")));
				TestEqual(TEXT("stamped with the library"), Row->SourceName, FString(TEXT("/virtual/Common.dui")));
			}
			const FDreamUIComponentAlias* Tab = Ast.FindComponentAlias(TEXT("Tab"));
			TestTrue(TEXT("and so is the path alias"), Tab != nullptr && Tab->ClassPath == TEXT("/Game/UI/WBP_Tab"));
			TestEqual(TEXT("the watcher hears of the library and the component it names"), Ast.Imports.Num(), 2);
		}
	}
	{
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		const bool bParsed = ParseWith(MakeSource({
			TEXT("use \"Common.dui\" as c"),
			TEXT("Widget Root { }")
		}), Files, Ast, Diagnostics);
		if (ExpectClean(*this, TEXT("the same library under a namespace"), bParsed, Diagnostics))
		{
			TestNotNull(TEXT("brings its aliases under the namespace"), Ast.FindComponentAlias(TEXT("c.Row")));
			TestNull(TEXT("and not under their own names"), Ast.FindComponentAlias(TEXT("Row")));
		}
	}
	{
		TMap<FString, FString> Diamond;
		Diamond.Add(TEXT("B.dui"), TEXT("use \"D.dui\""));
		Diamond.Add(TEXT("C.dui"), TEXT("use \"D.dui\""));
		Diamond.Add(TEXT("D.dui"), TEXT("use /Game/UI/WBP_Row as Row"));
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		const bool bParsed = ParseWith(TEXT("use \"B.dui\"\nuse \"C.dui\"\nWidget Root { }"), Diamond, Ast, Diagnostics);
		if (ExpectClean(*this, TEXT("a diamond of libraries"), bParsed, Diagnostics))
		{
			int32 Rows = 0;
			for (const FDreamUIComponentAlias& Alias : Ast.ImportedComponentAliases)
			{
				Rows += Alias.Alias == TEXT("Row") ? 1 : 0;
			}
			TestEqual(TEXT("brings the shared alias once"), Rows, 1);
		}
	}
	{
		// Namespace first, then plain: the plain `use` still has everything to give, though the file is already
		// listed in Imports -- listed under another name is not merged.
		TMap<FString, FString> Lib;
		Lib.Add(TEXT("Lib.dui"), TEXT("style Card { RenderOpacity = 0.5 }"));
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		const bool bParsed = ParseWith(TEXT("use \"Lib.dui\" as lib\nuse \"Lib.dui\"\nWidget Root { }"), Lib, Ast, Diagnostics);
		if (ExpectClean(*this, TEXT("one library used under a namespace and plainly"), bParsed, Diagnostics))
		{
			TestNotNull(TEXT("is in scope under the namespace"), Ast.FindStyle(TEXT("lib.Card")));
			TestNotNull(TEXT("and under its own name"), Ast.FindStyle(TEXT("Card")));
			TestEqual(TEXT("as two entries"), Ast.ImportedStyles.Num(), 2);
			TestEqual(TEXT("while the watcher hears of the file once"), Ast.Imports.Num(), 1);
		}
	}
	return true;
}

// ------------------------------------------------------------------------------------------------
// anonymous nodes
// ------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUISyntaxParseAnonymousTest,
	"DreamGUI.Text.Syntax.AnUnnamedNodeIsNamedAfterItsParentItsTypeAndItsPlace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * `<parent id>__<type><n>`: the type with what an id cannot hold made '_', n counting the earlier unnamed siblings of
 * that type, a collision with an id the author wrote bumped with `_<n>`. Made in one pass over the finished tree, so a
 * written id further down the file is already known when the made one is chosen.
 */
bool FDreamUISyntaxParseAnonymousTest::RunTest(const FString& Parameters)
{
	using namespace DreamUISyntaxParserTestLocal;

	{
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		const bool bParsed = ParseWith(MakeSource({
			TEXT("Widget Root {"),                     //  1
			TEXT("    HorizontalBox {"),               //  2
			TEXT("        Spacing = 14"),              //  3
			TEXT("        Text { Text = \"Status\" }"),//  4
			TEXT("        Text : Caption { }"),        //  5
			TEXT("    }"),                             //  6
			TEXT("    Text { }"),                      //  7
			TEXT("    @Row { }"),                      //  8
			TEXT("    nier.Row { }"),                  //  9
			TEXT("    Text Root__Text1 { }"),          // 10
			TEXT("    Text { }"),                      // 11
			TEXT("}"),                                 // 12
			TEXT("style Caption { }")                  // 13
		}), {}, Ast, Diagnostics);
		if (!ExpectClean(*this, TEXT("a tree of unnamed nodes"), bParsed, Diagnostics))
		{
			return false;
		}
		const TArray<FDreamUINode>& Children = Ast.Root.Children;
		if (!TestEqual(TEXT("the root holds every node written in it"), Children.Num(), 6))
		{
			return false;
		}
		const FDreamUINode& Box = Children[0];
		TestTrue(TEXT("an unnamed node is marked so"), Box.bAnonymous);
		TestEqual(TEXT("and named after its parent and its type"), Box.Id, FString(TEXT("Root__HorizontalBox0")));
		TestTrue(TEXT("located at its type"), Box.Location.Line == 2 && Box.Location.Column == 5);
		TestTrue(TEXT("its own lines are its own"), Box.Properties.Num() == 1 && Box.Properties[0].Name == TEXT("Spacing"));
		if (TestEqual(TEXT("the box holds its two texts"), Box.Children.Num(), 2))
		{
			TestEqual(TEXT("an unnamed child of an unnamed node takes the made id as its parent's"),
				Box.Children[0].Id, FString(TEXT("Root__HorizontalBox0__Text0")));
			TestEqual(TEXT("the second of a type counts one"), Box.Children[1].Id, FString(TEXT("Root__HorizontalBox0__Text1")));
			TestEqual(TEXT("a style clause needs no id before it"), Box.Children[1].StyleName, FString(TEXT("Caption")));
		}
		TestEqual(TEXT("counts are per parent"), Children[1].Id, FString(TEXT("Root__Text0")));
		TestEqual(TEXT("a resource type loses its '@' to '_'"), Children[2].Id, FString(TEXT("Root___Row0")));
		TestTrue(TEXT("and is located at the '@'"), Children[2].Location.Line == 8 && Children[2].Location.Column == 5);
		TestEqual(TEXT("a qualified type its dot"), Children[3].Id, FString(TEXT("Root__nier_Row0")));
		TestFalse(TEXT("a written id is not marked unnamed"), Children[4].bAnonymous);
		TestEqual(TEXT("a made id that meets a written one is bumped"), Children[5].Id, FString(TEXT("Root__Text1_1")));
	}
	{
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		const bool bParsed = ParseWith(TEXT("Widget {\n    Text { }\n}"), {}, Ast, Diagnostics);
		if (ExpectClean(*this, TEXT("an unnamed root"), bParsed, Diagnostics))
		{
			TestEqual(TEXT("is named under Root"), Ast.Root.Id, FString(TEXT("Root__Widget0")));
			TestTrue(TEXT("and its children under it"), Ast.Root.Children.Num() == 1
				&& Ast.Root.Children[0].Id == TEXT("Root__Widget0__Text0"));
		}
	}
	{
		// A loop has no id and a fill's id is the component's slot name, so their widgets count along with the nearest
		// node that has one -- never meeting a sibling's made id, nor another instance's fill.
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		const bool bParsed = ParseWith(MakeSource({
			TEXT("Widget Root {"),
			TEXT("    Text { }"),
			TEXT("    for Item in Items {"),
			TEXT("        Text { }"),
			TEXT("    }"),
			TEXT("    ListPage Page {"),
			TEXT("        slot Detail {"),
			TEXT("            Text { }"),
			TEXT("        }"),
			TEXT("        Text { }"),
			TEXT("    }"),
			TEXT("}")
		}), {}, Ast, Diagnostics);
		if (ExpectClean(*this, TEXT("unnamed nodes in a loop and a fill"), bParsed, Diagnostics)
			&& TestEqual(TEXT("the root holds its text, the loop and the page"), Ast.Root.Children.Num(), 3))
		{
			TestEqual(TEXT("the root's own text"), Ast.Root.Children[0].Id, FString(TEXT("Root__Text0")));
			const FDreamUINode& Loop = Ast.Root.Children[1];
			TestTrue(TEXT("the loop's text counts along with the root's"), Loop.Children.Num() == 1
				&& Loop.Children[0].Id == TEXT("Root__Text1"));
			const FDreamUINode& Page = Ast.Root.Children[2];
			if (TestEqual(TEXT("the page holds its fill and its text"), Page.Children.Num(), 2))
			{
				const FDreamUINode& Fill = Page.Children[0];
				TestTrue(TEXT("the fill's text is named under the page"), Fill.Children.Num() == 1
					&& Fill.Children[0].Id == TEXT("Page__Text0"));
				TestEqual(TEXT("and the page's own counts on from it"), Page.Children[1].Id, FString(TEXT("Page__Text1")));
			}
		}
	}

	// A type alone on its line is still the mistake it was: as likely a property missing its '='.
	ExpectOneDiagnostic(*this, TEXT("a type with neither an id nor a block"), MakeSource({
		TEXT("Widget Root {"),
		TEXT("    Image"),
		TEXT("}")
	}), EDreamUIDiagnosticCode::MissingNodeId, 2);

	// A rename carries references to the new id; an unnamed node's is hidden from graphs, so it needs a written one.
	ExpectOneDiagnostic(*this, TEXT("a rename clause on an unnamed node"), MakeSource({
		TEXT("style Card { }"),
		TEXT("Widget Root {"),
		TEXT("    Image : Card (was: Old) { }"),
		TEXT("}")
	}), EDreamUIDiagnosticCode::MissingNodeId, 3);

	return true;
}

// ------------------------------------------------------------------------------------------------
// slot lines and styles
// ------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUISyntaxParseSlotLinesTest,
	"DreamGUI.Text.Syntax.TheSlotBlockAndTheFillShorthandsLandOnTheNodesSlot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUISyntaxParseSlotLinesTest::RunTest(const FString& Parameters)
{
	using namespace DreamUISyntaxParserTestLocal;

	FDreamUIAst Ast;
	FDreamUIDiagnosticBag Diagnostics;
	const bool bParsed = ParseWith(MakeSource({
		TEXT("Widget Root {"),                                           //  1
		TEXT("    Text A {"),                                            //  2
		TEXT("        @slot { SizeRule = Fill  Padding = (0, 8, 0, 0) }"), //  3
		TEXT("    }"),                                                   //  4
		TEXT("    Text B {"),                                            //  5
		TEXT("        @fill"),                                           //  6
		TEXT("    }"),                                                   //  7
		TEXT("    Text Cell {"),                                         //  8
		TEXT("        @fill 2"),                                         //  9
		TEXT("    }"),                                                   // 10
		TEXT("    Text Block {"),                                        // 11
		TEXT("        @slot {"),                                         // 12
		TEXT("            SizeRule = Fill"),                             // 13
		TEXT("            FillWeight = 3"),                              // 14
		TEXT("        }"),                                               // 15
		TEXT("    }"),                                                   // 16
		TEXT("}")                                                        // 17
	}), {}, Ast, Diagnostics);
	if (!ExpectClean(*this, TEXT("slot blocks and shorthands"), bParsed, Diagnostics))
	{
		return false;
	}

	if (const FDreamUINode* A = ChildById(Ast.Root, TEXT("A")))
	{
		if (TestEqual(TEXT("a one-line block holds both its lines"), A->SlotProperties.Num(), 2))
		{
			const FDreamUIProperty& SizeRule = A->SlotProperties[0];
			TestTrue(TEXT("each as written"), SizeRule.Name == TEXT("SizeRule") && SizeRule.Value.Raw == TEXT("Fill"));
			TestFalse(TEXT("which is not made"), SizeRule.bSynthesized);
			TestTrue(TEXT("located at its own name"), SizeRule.Location.Line == 3 && SizeRule.Location.Column == 17);
			TestTrue(TEXT("the second as written"), A->SlotProperties[1].Name == TEXT("Padding")
				&& A->SlotProperties[1].Value.Kind == EDreamUIValueKind::Tuple);
		}
		TestEqual(TEXT("slot lines are not the node's own"), A->Properties.Num(), 0);
	}
	if (const FDreamUINode* B = ChildById(Ast.Root, TEXT("B")))
	{
		if (TestEqual(TEXT("'@fill' is one slot line"), B->SlotProperties.Num(), 1))
		{
			const FDreamUIProperty& SizeRule = B->SlotProperties[0];
			TestTrue(TEXT("SizeRule = Fill"), SizeRule.Name == TEXT("SizeRule")
				&& SizeRule.Value.Kind == EDreamUIValueKind::Identifier && SizeRule.Value.Raw == TEXT("Fill"));
			TestTrue(TEXT("made by the front end"), SizeRule.bSynthesized);
			TestTrue(TEXT("located at the shorthand's '@'"), SizeRule.Location.Line == 6 && SizeRule.Location.Column == 9);
		}
	}
	if (const FDreamUINode* Cell = ChildById(Ast.Root, TEXT("Cell")))
	{
		if (TestEqual(TEXT("'@fill 2' is two slot lines"), Cell->SlotProperties.Num(), 2))
		{
			const FDreamUIProperty& Weight = Cell->SlotProperties[1];
			TestTrue(TEXT("the second FillWeight = 2"), Weight.Name == TEXT("FillWeight")
				&& Weight.Value.Kind == EDreamUIValueKind::Number && Weight.Value.Raw == TEXT("2"));
			TestTrue(TEXT("both made, both at the '@'"), Cell->SlotProperties[0].bSynthesized && Weight.bSynthesized
				&& Weight.Location.Line == 9 && Weight.Location.Column == 9);
		}
	}
	if (const FDreamUINode* Block = ChildById(Ast.Root, TEXT("Block")))
	{
		TestTrue(TEXT("a block across lines holds one line per line"), Block->SlotProperties.Num() == 2
			&& Block->SlotProperties[0].Location.Line == 13 && Block->SlotProperties[1].Location.Line == 14);
	}

	{
		// `fill` is the shorthand only alone or before a number; a resource of that name still names a node type.
		FDreamUIAst ResourceAst;
		FDreamUIDiagnosticBag ResourceDiagnostics;
		const bool bResourceParsed = ParseWith(MakeSource({
			TEXT("resources { Asset fill = /Game/UI/WBP_Fill }"),
			TEXT("Widget Root {"),
			TEXT("    @fill Filler { }"),
			TEXT("    @fill { }"),
			TEXT("}")
		}), {}, ResourceAst, ResourceDiagnostics);
		if (ExpectClean(*this, TEXT("a resource called fill"), bResourceParsed, ResourceDiagnostics))
		{
			TestTrue(TEXT("still types nodes, named and unnamed"), ResourceAst.Root.Children.Num() == 2
				&& ResourceAst.Root.Children[0].TypeName == TEXT("@fill") && ResourceAst.Root.Children[1].bAnonymous
				&& ResourceAst.Root.SlotProperties.Num() == 0);
		}
	}

	ExpectOneDiagnostic(*this, TEXT("'@fill' followed by something not a weight"), MakeSource({
		TEXT("Widget Root {"),
		TEXT("    Text T {"),
		TEXT("        @fill 2 3"),
		TEXT("    }"),
		TEXT("}")
	}), EDreamUIDiagnosticCode::UnexpectedToken, 3);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUISyntaxParseStyleBodyTest,
	"DreamGUI.Text.Syntax.AStyleCarriesComponentsAndSlotLinesWhileABehavioursBlockStillDoesNot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUISyntaxParseStyleBodyTest::RunTest(const FString& Parameters)
{
	using namespace DreamUISyntaxParserTestLocal;

	FDreamUIAst Ast;
	FDreamUIDiagnosticBag Diagnostics;
	const bool bParsed = ParseWith(MakeSource({
		TEXT("style RowColumn {"),                        // 1
		TEXT("    + VerticalBox { Spacing = 15 }"),       // 2
		TEXT("    @fill"),                                // 3
		TEXT("    @slot Padding = (0, 4, 0, 0)"),         // 4
		TEXT("    @slot { MinDesiredSize = (0, 48) }"),   // 5
		TEXT("    RenderOpacity = 0.5"),                  // 6
		TEXT("}"),                                        // 7
		TEXT("Widget Root { }")                           // 8
	}), {}, Ast, Diagnostics);
	if (ExpectClean(*this, TEXT("a style with components and slot lines"), bParsed, Diagnostics)
		&& TestEqual(TEXT("one style"), Ast.Styles.Num(), 1))
	{
		const FDreamUIStyle& Style = Ast.Styles[0];
		TestTrue(TEXT("its component, with its block"), Style.Components.Num() == 1
			&& Style.Components[0].ClassName == TEXT("VerticalBox") && Style.Components[0].Properties.Num() == 1
			&& Style.Components[0].Location.Line == 2);
		if (TestEqual(TEXT("its three slot lines"), Style.SlotProperties.Num(), 3))
		{
			TestTrue(TEXT("the shorthand, made"), Style.SlotProperties[0].Name == TEXT("SizeRule") && Style.SlotProperties[0].bSynthesized);
			TestTrue(TEXT("the one-liner, at its '@'"), Style.SlotProperties[1].Name == TEXT("Padding")
				&& Style.SlotProperties[1].Location.Line == 4 && Style.SlotProperties[1].Location.Column == 5);
			TestTrue(TEXT("the block's line"), Style.SlotProperties[2].Name == TEXT("MinDesiredSize"));
		}
		TestTrue(TEXT("and its own property"), Style.Properties.Num() == 1 && Style.Properties[0].Name == TEXT("RenderOpacity"));
	}

	// The blocks that share the property-only grammar keep refusing what they always refused.
	ExpectOneDiagnostic(*this, TEXT("a shorthand inside a behaviour's block"), MakeSource({
		TEXT("Widget Root {"),
		TEXT("    Text T {"),
		TEXT("        + UIButton { @fill }"),
		TEXT("    }"),
		TEXT("}")
	}), EDreamUIDiagnosticCode::UnexpectedToken, 3);

	ExpectOneDiagnostic(*this, TEXT("a component inside a behaviour's block"), MakeSource({
		TEXT("Widget Root {"),
		TEXT("    Text T {"),
		TEXT("        + UIButton { + Other { } }"),
		TEXT("    }"),
		TEXT("}")
	}), EDreamUIDiagnosticCode::UnexpectedToken, 3);

	ExpectOneDiagnostic(*this, TEXT("a child node inside a style"), MakeSource({
		TEXT("style Card {"),
		TEXT("    Text T { }"),
		TEXT("}"),
		TEXT("Widget Root { }")
	}), EDreamUIDiagnosticCode::UnexpectedToken, 2);

	return true;
}

// ------------------------------------------------------------------------------------------------
// props, events, emit
// ------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUISyntaxParsePropsTest,
	"DreamGUI.Text.Syntax.APropsBlockDeclaresTypedNamesWithOptionalDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUISyntaxParsePropsTest::RunTest(const FString& Parameters)
{
	using namespace DreamUISyntaxParserTestLocal;

	FDreamUIAst Ast;
	FDreamUIDiagnosticBag Diagnostics;
	const bool bParsed = ParseWith(MakeSource({
		TEXT("props {"),                                                 // 1
		TEXT("    Text Label"),                                          // 2
		TEXT("    Number ValueIndex = 0"),                               // 3
		TEXT("    Enum /Script/DevProject.ENieRRowKind Kind = Cycle"),   // 4
		TEXT("    Bool Ready = true; Color Tint = #FF6600"),             // 5
		TEXT("}"),                                                       // 6
		TEXT("props { Vector2 Size }"),                                  // 7
		TEXT("Widget Root { }")                                          // 8
	}), {}, Ast, Diagnostics);
	if (!ExpectClean(*this, TEXT("two props blocks"), bParsed, Diagnostics)
		|| !TestEqual(TEXT("whose lines accumulate"), Ast.Props.Num(), 6))
	{
		return false;
	}
	const FDreamUIPropDecl& Label = Ast.Props[0];
	TestTrue(TEXT("a type and a name"), Label.TypeName == TEXT("Text") && Label.Name == TEXT("Label") && !Label.DefaultValue.IsSet());
	TestTrue(TEXT("located at the type"), Label.Location.Line == 2 && Label.Location.Column == 5);
	const FDreamUIPropDecl& Index = Ast.Props[1];
	TestTrue(TEXT("a default value, as a value is written anywhere"), Index.DefaultValue.IsSet()
		&& Index.DefaultValue->Kind == EDreamUIValueKind::Number && Index.DefaultValue->Raw == TEXT("0"));
	const FDreamUIPropDecl& Kind = Ast.Props[2];
	TestTrue(TEXT("an enum takes its path before the name"), Kind.TypeName == TEXT("Enum")
		&& Kind.EnumPath == TEXT("/Script/DevProject.ENieRRowKind") && Kind.Name == TEXT("Kind"));
	TestTrue(TEXT("and its default as written"), Kind.DefaultValue.IsSet() && Kind.DefaultValue->Raw == TEXT("Cycle"));
	TestTrue(TEXT("a ';' ends a line as a line break does"), Ast.Props[3].Name == TEXT("Ready") && Ast.Props[4].Name == TEXT("Tint")
		&& Ast.Props[4].DefaultValue.IsSet() && Ast.Props[4].DefaultValue->Kind == EDreamUIValueKind::HexColor);
	TestTrue(TEXT("a type is recorded as written, for the compiler to check"), Ast.Props[5].TypeName == TEXT("Vector2"));

	{
		FDreamUIAst DuplicateAst;
		FDreamUIDiagnosticBag DuplicateDiagnostics;
		ParseWith(MakeSource({
			TEXT("props {"),
			TEXT("    Text Label"),
			TEXT("    String label"),
			TEXT("}"),
			TEXT("Widget Root { }")
		}), {}, DuplicateAst, DuplicateDiagnostics);
		TestEqual(TEXT("a name declared twice is one complaint"), DuplicateDiagnostics.Diagnostics.Num(), 1);
		const FDreamUIDiagnostic* Duplicate = FirstOf(DuplicateDiagnostics, EDreamUIDiagnosticCode::DuplicateProp);
		TestTrue(TEXT("on the second line, case insensitively"), Duplicate != nullptr && Duplicate->Location.Line == 3);
		TestTrue(TEXT("and the first is kept"), DuplicateAst.Props.Num() == 1 && DuplicateAst.Props[0].TypeName == TEXT("Text"));
	}

	ExpectOneDiagnostic(*this, TEXT("a props line with no name"), MakeSource({
		TEXT("props {"),
		TEXT("    Label"),
		TEXT("}"),
		TEXT("Widget Root { }")
	}), EDreamUIDiagnosticCode::MalformedPropsBlock, 2);

	ExpectOneDiagnostic(*this, TEXT("an enum prop with no path"), MakeSource({
		TEXT("props {"),
		TEXT("    Enum Kind"),
		TEXT("}"),
		TEXT("Widget Root { }")
	}), EDreamUIDiagnosticCode::MalformedPropsBlock, 2);

	ExpectOneDiagnostic(*this, TEXT("a default begun and not written"), MakeSource({
		TEXT("props {"),
		TEXT("    Number Gap ="),
		TEXT("}"),
		TEXT("Widget Root { }")
	}), EDreamUIDiagnosticCode::MalformedPropsBlock, 2);

	ExpectOneDiagnostic(*this, TEXT("a props line declaring two names"), MakeSource({
		TEXT("props {"),
		TEXT("    Text Label Extra"),
		TEXT("}"),
		TEXT("Widget Root { }")
	}), EDreamUIDiagnosticCode::MalformedPropsBlock, 2);

	// What the class declares belongs at the top, like `class`.
	ExpectOneDiagnostic(*this, TEXT("a props block inside a node"), MakeSource({
		TEXT("Widget Root {"),
		TEXT("    props { Text Label }"),
		TEXT("}")
	}), EDreamUIDiagnosticCode::MalformedPropsBlock, 2);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUISyntaxParseEventsTest,
	"DreamGUI.Text.Syntax.AnEventsBlockDeclaresNamesWithOptionalTypedParameters",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUISyntaxParseEventsTest::RunTest(const FString& Parameters)
{
	using namespace DreamUISyntaxParserTestLocal;

	FDreamUIAst Ast;
	FDreamUIDiagnosticBag Diagnostics;
	const bool bParsed = ParseWith(MakeSource({
		TEXT("events {"),                                                // 1
		TEXT("    Picked(Number Index, Enum /Script/Game.EKind Kind)"),  // 2
		TEXT("    Closed; Opened()"),                                    // 3
		TEXT("}"),                                                       // 4
		TEXT("events { Moved(Vector2 Delta) }"),                         // 5
		TEXT("Widget Root { }")                                          // 6
	}), {}, Ast, Diagnostics);
	if (!ExpectClean(*this, TEXT("two events blocks"), bParsed, Diagnostics)
		|| !TestEqual(TEXT("whose entries accumulate"), Ast.Events.Num(), 4))
	{
		return false;
	}
	const FDreamUIEventDecl* Picked = Ast.FindEvent(TEXT("Picked"));
	if (TestNotNull(TEXT("FindEvent answers for a declared name"), Picked)
		&& TestEqual(TEXT("with its two parameters"), Picked->Params.Num(), 2))
	{
		TestTrue(TEXT("located at its name"), Picked->Location.Line == 2 && Picked->Location.Column == 5);
		TestTrue(TEXT("a parameter is a type and a name"), Picked->Params[0].TypeName == TEXT("Number")
			&& Picked->Params[0].Name == TEXT("Index") && Picked->Params[0].Location.Column == 12);
		TestTrue(TEXT("an enum parameter takes its path"), Picked->Params[1].TypeName == TEXT("Enum")
			&& Picked->Params[1].EnumPath == TEXT("/Script/Game.EKind") && Picked->Params[1].Name == TEXT("Kind"));
	}
	TestTrue(TEXT("an entry with no parameters, ended by ';'"), Ast.Events[1].Name == TEXT("Closed") && Ast.Events[1].Params.Num() == 0);
	TestTrue(TEXT("and one with empty parentheses"), Ast.Events[2].Name == TEXT("Opened") && Ast.Events[2].Params.Num() == 0);
	TestTrue(TEXT("a one-line block"), Ast.Events[3].Name == TEXT("Moved") && Ast.Events[3].Params.Num() == 1);

	ExpectOneDiagnostic(*this, TEXT("an event declared twice"), MakeSource({
		TEXT("events {"),
		TEXT("    Picked"),
		TEXT("    Picked(Number Index)"),
		TEXT("}"),
		TEXT("Widget Root { }")
	}), EDreamUIDiagnosticCode::DuplicateEvent, 3);

	{
		FDreamUIAst DuplicateAst;
		FDreamUIDiagnosticBag DuplicateDiagnostics;
		ParseWith(MakeSource({
			TEXT("events {"),
			TEXT("    Picked(Number Index, String Index)"),
			TEXT("}"),
			TEXT("Widget Root { }")
		}), {}, DuplicateAst, DuplicateDiagnostics);
		TestEqual(TEXT("a parameter declared twice is one complaint"), DuplicateDiagnostics.Diagnostics.Num(), 1);
		TestTrue(TEXT("under the duplicate-event code"), Reported(DuplicateDiagnostics, EDreamUIDiagnosticCode::DuplicateEvent));
		TestTrue(TEXT("and the first parameter is kept"), DuplicateAst.Events.Num() == 1 && DuplicateAst.Events[0].Params.Num() == 1
			&& DuplicateAst.Events[0].Params[0].TypeName == TEXT("Number"));
	}

	ExpectOneDiagnostic(*this, TEXT("a parameter with no name"), MakeSource({
		TEXT("events {"),
		TEXT("    Picked(Number)"),
		TEXT("}"),
		TEXT("Widget Root { }")
	}), EDreamUIDiagnosticCode::MalformedEventsBlock, 2);

	ExpectOneDiagnostic(*this, TEXT("parameters that never close"), MakeSource({
		TEXT("events {"),
		TEXT("    Picked(Number Index"),
		TEXT("}"),
		TEXT("Widget Root { }")
	}), EDreamUIDiagnosticCode::MalformedEventsBlock, 2);

	ExpectOneDiagnostic(*this, TEXT("two entries on a line with nothing between them"), MakeSource({
		TEXT("events {"),
		TEXT("    Picked Closed"),
		TEXT("}"),
		TEXT("Widget Root { }")
	}), EDreamUIDiagnosticCode::MalformedEventsBlock, 2);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUISyntaxParseEmitTest,
	"DreamGUI.Text.Syntax.AnEmitRouteNamesTheEventAndCarriesItsArgumentsAsExpressions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUISyntaxParseEmitTest::RunTest(const FString& Parameters)
{
	using namespace DreamUISyntaxParserTestLocal;

	FDreamUIAst Ast;
	FDreamUIDiagnosticBag Diagnostics;
	const bool bParsed = ParseWith(MakeSource({
		TEXT("events { Picked(Number Index); Closed }"),
		TEXT("Widget Root {"),
		TEXT("    OnClicked -> emit Picked(Index)"),
		TEXT("    OnHovered -> emit Picked(Base() + 1, @Gap)"),
		TEXT("    OnPressed -> emit Closed"),
		TEXT("    OnReleased -> emit"),
		TEXT("    OnFocused -> Confirm"),
		TEXT("}")
	}), {}, Ast, Diagnostics);
	if (!ExpectClean(*this, TEXT("emit routes"), bParsed, Diagnostics)
		|| !TestEqual(TEXT("one property per route"), Ast.Root.Properties.Num(), 5))
	{
		return false;
	}
	const TArray<FDreamUIProperty>& Routes = Ast.Root.Properties;
	TestEqual(TEXT("an emit names its event"), Routes[0].EmitEvent, FString(TEXT("Picked")));
	TestTrue(TEXT("and leaves the handler for the compiler to make"), Routes[0].EventHandler.IsEmpty());
	TestTrue(TEXT("while still counting as an event route"), Routes[0].IsEventBinding());
	TestTrue(TEXT("its argument is an expression over the widget"), Routes[0].EmitArguments.Num() == 1
		&& IsVariable(Routes[0].EmitArguments[0], TEXT("Index")));
	TestTrue(TEXT("arguments are whole expressions, separated by commas"), Routes[1].EmitArguments.Num() == 2
		&& Routes[1].EmitArguments[0].Kind == FDreamUIExpression::EKind::Binary
		&& IsResourceLiteral(Routes[1].EmitArguments[1], TEXT("Gap")));
	TestTrue(TEXT("an event with no parameters takes no parentheses"), Routes[2].EmitEvent == TEXT("Closed")
		&& Routes[2].EmitArguments.Num() == 0);
	TestTrue(TEXT("'emit' with no event after it is still a handler's name"), Routes[3].EmitEvent.IsEmpty()
		&& Routes[3].EventHandler == TEXT("emit"));
	TestTrue(TEXT("and an ordinary route is what it was"), Routes[4].EventHandler == TEXT("Confirm") && Routes[4].EmitEvent.IsEmpty());

	ExpectOneDiagnostic(*this, TEXT("emit arguments that never close"), MakeSource({
		TEXT("Widget Root {"),
		TEXT("    OnClicked -> emit Picked(Index"),
		TEXT("}")
	}), EDreamUIDiagnosticCode::MalformedBindingExpression, 2);

	return true;
}

// ------------------------------------------------------------------------------------------------
// slots
// ------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUISyntaxParseSlotTest,
	"DreamGUI.Text.Syntax.ASlotDeclaresAHoleWithItsLayoutOrFillsAComponentsHoleWithWidgets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUISyntaxParseSlotTest::RunTest(const FString& Parameters)
{
	using namespace DreamUISyntaxParserTestLocal;

	FDreamUIAst Ast;
	FDreamUIDiagnosticBag Diagnostics;
	const bool bParsed = ParseWith(MakeSource({
		TEXT("style RowList { + VerticalBox { Spacing = 15 } }"),  //  1
		TEXT("Widget Root {"),                                    //  2
		TEXT("    Text Detail { }"),                              //  3
		TEXT("    slot Rows default : RowList {"),                //  4
		TEXT("        + VerticalBox { Spacing = 15 }"),           //  5
		TEXT("        RenderOpacity = 0.5"),                      //  6
		TEXT("        @slot SizeRule = Fill"),                    //  7
		TEXT("    }"),                                            //  8
		TEXT("    slot Footer"),                                  //  9
		TEXT("    ListPage Page_2 {"),                            // 10
		TEXT("        slot Detail { Text Note { } }"),            // 11
		TEXT("    }"),                                            // 12
		TEXT("    ListPage Page_3 {"),                            // 13
		TEXT("        slot Detail {"),                            // 14
		TEXT("            Text { }"),                             // 15
		TEXT("        }"),                                        // 16
		TEXT("    }"),                                            // 17
		TEXT("}")                                                 // 18
	}), {}, Ast, Diagnostics);
	// Two hosts filling their component's Detail, and a widget called Detail besides: a fill names a hole in another
	// class, so none of the three is a duplicate id.
	if (!ExpectClean(*this, TEXT("slot declarations and fills"), bParsed, Diagnostics))
	{
		return false;
	}

	if (const FDreamUINode* Rows = ChildById(Ast.Root, TEXT("Rows")))
	{
		TestEqual(TEXT("a declaration is a named slot"), static_cast<int32>(Rows->Kind), static_cast<int32>(EDreamUINodeKind::NamedSlot));
		TestTrue(TEXT("the default one"), Rows->bDefaultSlot);
		TestFalse(TEXT("which fills nothing"), Rows->bFillsSlot);
		TestEqual(TEXT("wearing a style"), Rows->StyleName, FString(TEXT("RowList")));
		TestTrue(TEXT("with its component, property and slot line"), Rows->Components.Num() == 1
			&& Rows->Properties.Num() == 1 && Rows->SlotProperties.Num() == 1);
		TestEqual(TEXT("and no children"), Rows->Children.Num(), 0);
	}
	else
	{
		AddError(TEXT("the slot declaration with a block did not survive"));
	}
	if (const FDreamUINode* Footer = ChildById(Ast.Root, TEXT("Footer")))
	{
		TestTrue(TEXT("a bare declaration is neither the default nor a fill"), !Footer->bDefaultSlot && !Footer->bFillsSlot);
	}
	if (const FDreamUINode* Page = ChildById(Ast.Root, TEXT("Page_2")))
	{
		const bool bFill = Page->Children.Num() == 1 && Page->Children[0].Kind == EDreamUINodeKind::NamedSlot
			&& Page->Children[0].bFillsSlot && Page->Children[0].Id == TEXT("Detail");
		if (TestTrue(TEXT("a slot holding widgets inside an instance is a fill of that slot"), bFill))
		{
			TestTrue(TEXT("holding the widgets"), Page->Children[0].Children.Num() == 1
				&& Page->Children[0].Children[0].Id == TEXT("Note"));
			TestFalse(TEXT("and is no default"), Page->Children[0].bDefaultSlot);
		}
	}
	if (const FDreamUINode* Page = ChildById(Ast.Root, TEXT("Page_3")))
	{
		TestTrue(TEXT("an unnamed widget in a fill is named under the instance"), Page->Children.Num() == 1
			&& Page->Children[0].Children.Num() == 1 && Page->Children[0].Children[0].Id == TEXT("Page_3__Text0"));
	}

	{
		// The old spelling, exactly as before.
		FDreamUIAst OldAst;
		FDreamUIDiagnosticBag OldDiagnostics;
		const bool bOldParsed = ParseWith(TEXT("Widget Root {\n    slot Footer (was: Bottom)\n}"), {}, OldAst, OldDiagnostics);
		if (ExpectClean(*this, TEXT("a slot with a rename clause"), bOldParsed, OldDiagnostics))
		{
			TestTrue(TEXT("keeps its old id"), OldAst.Root.Children.Num() == 1 && OldAst.Root.Children[0].WasId == TEXT("Bottom")
				&& !OldAst.Root.Children[0].bDefaultSlot);
		}
	}

	ExpectOneDiagnostic(*this, TEXT("a slot block that both declares and fills"), MakeSource({
		TEXT("Widget Root {"),
		TEXT("    slot Rows {"),
		TEXT("        RenderOpacity = 0.5"),
		TEXT("        Text T { }"),
		TEXT("    }"),
		TEXT("}")
	}), EDreamUIDiagnosticCode::MalformedSlotDeclaration, 4);

	ExpectOneDiagnostic(*this, TEXT("'default' on a fill"), MakeSource({
		TEXT("Widget Root {"),
		TEXT("    slot Detail default {"),
		TEXT("        Text T { }"),
		TEXT("    }"),
		TEXT("}")
	}), EDreamUIDiagnosticCode::MalformedSlotDeclaration, 2);

	ExpectOneDiagnostic(*this, TEXT("'default' written twice"), MakeSource({
		TEXT("Widget Root {"),
		TEXT("    slot Rows default default"),
		TEXT("}")
	}), EDreamUIDiagnosticCode::MalformedSlotDeclaration, 2);

	{
		FDreamUIAst TwoAst;
		FDreamUIDiagnosticBag TwoDiagnostics;
		ParseWith(MakeSource({
			TEXT("Widget Root {"),
			TEXT("    slot Rows default"),
			TEXT("    slot Others default"),
			TEXT("}")
		}), {}, TwoAst, TwoDiagnostics);
		TestEqual(TEXT("a second default slot is one complaint"), TwoDiagnostics.Diagnostics.Num(), 1);
		const FDreamUIDiagnostic* Second = FirstOf(TwoDiagnostics, EDreamUIDiagnosticCode::MultipleDefaultSlots);
		TestTrue(TEXT("at the second slot's 'default'"), Second != nullptr && Second->Location.Line == 3 && Second->Location.Column == 17);
		TestTrue(TEXT("and the first keeps the role"), TwoAst.Root.Children.Num() == 2
			&& TwoAst.Root.Children[0].bDefaultSlot && !TwoAst.Root.Children[1].bDefaultSlot);
	}
	return true;
}

// ------------------------------------------------------------------------------------------------
// if / else
// ------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUISyntaxParseConditionalTest,
	"DreamGUI.Text.Syntax.AnIfChainLowersIntoWidgetsShownWhenTheirBranchIsTaken",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * No node kind of its own: every widget of every branch becomes a child of the enclosing node, in order, with a made
 * `Shown <- …` -- the branch's condition after none of the earlier ones held. A widget writing its own Shown keeps it,
 * ANDed after the branch's.
 */
bool FDreamUISyntaxParseConditionalTest::RunTest(const FString& Parameters)
{
	using namespace DreamUISyntaxParserTestLocal;

	FDreamUIAst Ast;
	FDreamUIDiagnosticBag Diagnostics;
	const bool bParsed = ParseWith(MakeSource({
		TEXT("Widget Root {"),                               //  1
		TEXT("    if HasSave() {"),                          //  2
		TEXT("        Text Continue { Text = \"Continue\" }"),//  3
		TEXT("        Image Mark { }"),                      //  4
		TEXT("    } else if Loading {"),                     //  5
		TEXT("        Text Wait { }"),                       //  6
		TEXT("    }"),                                       //  7
		TEXT("    else {"),                                  //  8
		TEXT("        Text NoSave { Shown <- Ready() }"),    //  9
		TEXT("    }"),                                       // 10
		TEXT("    Text After { }"),                          // 11
		TEXT("}")                                            // 12
	}), {}, Ast, Diagnostics);
	if (!ExpectClean(*this, TEXT("an if chain"), bParsed, Diagnostics)
		|| !TestEqual(TEXT("every branch's widgets join the enclosing node, in order"), Ast.Root.Children.Num(), 5))
	{
		return false;
	}
	const TArray<FDreamUINode>& Children = Ast.Root.Children;
	TestTrue(TEXT("in the order written"), Children[0].Id == TEXT("Continue") && Children[1].Id == TEXT("Mark")
		&& Children[2].Id == TEXT("Wait") && Children[3].Id == TEXT("NoSave") && Children[4].Id == TEXT("After"));

	const FDreamUIProperty* ContinueShown = PropertyByName(Children[0].Properties, TEXT("Shown"));
	if (TestNotNull(TEXT("the first branch's widget is shown by a made line"), ContinueShown))
	{
		TestTrue(TEXT("made by the front end"), ContinueShown->bSynthesized);
		TestEqual(TEXT("a bare call condition rides the binding's function, as 'Shown <- HasSave()' would"),
			ContinueShown->BindingFunction, FString(TEXT("HasSave")));
		TestTrue(TEXT("located at the 'if'"), ContinueShown->Location.Line == 2 && ContinueShown->Location.Column == 5);
	}
	TestTrue(TEXT("every widget of the branch gets one"), PropertyByName(Children[1].Properties, TEXT("Shown")) != nullptr);

	const FDreamUIProperty* WaitShown = PropertyByName(Children[2].Properties, TEXT("Shown"));
	if (TestTrue(TEXT("an 'else if' widget is shown by an expression"), WaitShown != nullptr && WaitShown->BindingExpression.IsSet()))
	{
		const FDreamUIExpression& Condition = WaitShown->BindingExpression.GetValue();
		TestTrue(TEXT("!(HasSave()) && Loading"), IsAnd(Condition) && IsNot(Condition.Operands[0])
			&& IsCall(Condition.Operands[0].Operands[0], TEXT("HasSave")) && IsVariable(Condition.Operands[1], TEXT("Loading")));
		TestTrue(TEXT("located at its 'else'"), WaitShown->Location.Line == 5 && WaitShown->Location.Column == 7);
	}

	const FDreamUIProperty* NoSaveShown = PropertyByName(Children[3].Properties, TEXT("Shown"));
	if (TestTrue(TEXT("the else widget's own Shown is kept, combined"), NoSaveShown != nullptr && NoSaveShown->BindingExpression.IsSet()))
	{
		TestEqual(TEXT("as the one Shown line"), Children[3].Properties.Num(), 1);
		const FDreamUIExpression& Condition = NoSaveShown->BindingExpression.GetValue();
		const bool bElseShape = IsAnd(Condition) && IsAnd(Condition.Operands[0])
			&& IsNot(Condition.Operands[0].Operands[0]) && IsCall(Condition.Operands[0].Operands[0].Operands[0], TEXT("HasSave"))
			&& IsNot(Condition.Operands[0].Operands[1]) && IsVariable(Condition.Operands[0].Operands[1].Operands[0], TEXT("Loading"));
		TestTrue(TEXT("(!(HasSave()) && !(Loading)) && Ready()"), bElseShape && IsCall(Condition.Operands[1], TEXT("Ready")));
		TestTrue(TEXT("no longer a bare function"), NoSaveShown->BindingFunction.IsEmpty());
		TestTrue(TEXT("marked made, though it stays on the author's line"), NoSaveShown->bSynthesized
			&& NoSaveShown->Location.Line == 9);
	}
	TestNull(TEXT("a widget after the chain is not in it"), PropertyByName(Children[4].Properties, TEXT("Shown")));

	{
		// Nested: the inner block lowers first, then the outer one ANDs its condition in front.
		FDreamUIAst NestedAst;
		FDreamUIDiagnosticBag NestedDiagnostics;
		const bool bNestedParsed = ParseWith(MakeSource({
			TEXT("Widget Root {"),
			TEXT("    if Outer {"),
			TEXT("        if Inner {"),
			TEXT("            Text Deep { }"),
			TEXT("        }"),
			TEXT("    }"),
			TEXT("}")
		}), {}, NestedAst, NestedDiagnostics);
		if (ExpectClean(*this, TEXT("nested if blocks"), bNestedParsed, NestedDiagnostics)
			&& TestEqual(TEXT("flatten into the enclosing node"), NestedAst.Root.Children.Num(), 1))
		{
			const FDreamUIProperty* Shown = PropertyByName(NestedAst.Root.Children[0].Properties, TEXT("Shown"));
			TestTrue(TEXT("Outer && Inner"), Shown != nullptr && Shown->BindingExpression.IsSet()
				&& IsAnd(Shown->BindingExpression.GetValue())
				&& IsVariable(Shown->BindingExpression->Operands[0], TEXT("Outer"))
				&& IsVariable(Shown->BindingExpression->Operands[1], TEXT("Inner")));
		}
	}
	{
		// Shown written by hand is an ordinary binding.
		FDreamUIAst ShownAst;
		FDreamUIDiagnosticBag ShownDiagnostics;
		const bool bShownParsed = ParseWith(TEXT("Widget Root {\n    Text T { Shown <- HasSave() && !Busy }\n}"), {}, ShownAst, ShownDiagnostics);
		if (ExpectClean(*this, TEXT("a hand-written Shown binding"), bShownParsed, ShownDiagnostics))
		{
			const FDreamUIProperty* Shown = ShownAst.Root.Children.Num() == 1
				? PropertyByName(ShownAst.Root.Children[0].Properties, TEXT("Shown")) : nullptr;
			TestTrue(TEXT("parses as a binding the author wrote"), Shown != nullptr && Shown->BindingExpression.IsSet() && !Shown->bSynthesized);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUISyntaxParseConditionalErrorsTest,
	"DreamGUI.Text.Syntax.AnIfBlockHoldsWidgetsAndAnElseNeedsAnIfBeforeIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUISyntaxParseConditionalErrorsTest::RunTest(const FString& Parameters)
{
	using namespace DreamUISyntaxParserTestLocal;

	ExpectOneDiagnostic(*this, TEXT("an else with no if before it"), MakeSource({
		TEXT("Widget Root {"),
		TEXT("    Text Before { }"),
		TEXT("    else {"),
		TEXT("        Text A { }"),
		TEXT("    }"),
		TEXT("}")
	}), EDreamUIDiagnosticCode::MalformedConditional, 3);

	ExpectOneDiagnostic(*this, TEXT("an if with no condition"), MakeSource({
		TEXT("Widget Root {"),
		TEXT("    if {"),
		TEXT("        Text A { }"),
		TEXT("    }"),
		TEXT("}")
	}), EDreamUIDiagnosticCode::MalformedConditional, 2);

	ExpectOneDiagnostic(*this, TEXT("a property inside a branch"), MakeSource({
		TEXT("Widget Root {"),
		TEXT("    if Ready() {"),
		TEXT("        RenderOpacity = 0.5"),
		TEXT("    }"),
		TEXT("}")
	}), EDreamUIDiagnosticCode::MalformedConditional, 3);

	ExpectOneDiagnostic(*this, TEXT("a slot inside a branch"), MakeSource({
		TEXT("Widget Root {"),
		TEXT("    if Ready() {"),
		TEXT("        slot Footer"),
		TEXT("    }"),
		TEXT("}")
	}), EDreamUIDiagnosticCode::MalformedConditional, 3);

	ExpectOneDiagnostic(*this, TEXT("a loop inside a branch"), MakeSource({
		TEXT("Widget Root {"),
		TEXT("    if Ready() {"),
		TEXT("        for Item in Items {"),
		TEXT("            Text T { }"),
		TEXT("        }"),
		TEXT("    }"),
		TEXT("}")
	}), EDreamUIDiagnosticCode::MalformedConditional, 3);

	ExpectOneDiagnostic(*this, TEXT("an else followed by neither a block nor an if"), MakeSource({
		TEXT("Widget Root {"),
		TEXT("    if Ready() {"),
		TEXT("        Text A { }"),
		TEXT("    } else Other"),
		TEXT("}")
	}), EDreamUIDiagnosticCode::MalformedConditional, 4);

	ExpectOneDiagnostic(*this, TEXT("an if at the top of the file"), MakeSource({
		TEXT("Widget Root { }"),
		TEXT("if Ready() {"),
		TEXT("    Text A { }"),
		TEXT("}")
	}), EDreamUIDiagnosticCode::MalformedConditional, 2);

	return true;
}

// ------------------------------------------------------------------------------------------------
// compatibility
// ------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUISyntaxParseContextualWordsTest,
	"DreamGUI.Text.Syntax.TheNewWordsStayOrdinaryNamesWhereTheyDoNotLeadAStatement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * `as`, `props`, `events`, `emit`, `if`, `else`, `default`, `fill` became keywords after files were written that use them
 * as property names and ids. Each is a keyword only in the position it leads, so all of those files still mean what they
 * meant.
 */
bool FDreamUISyntaxParseContextualWordsTest::RunTest(const FString& Parameters)
{
	using namespace DreamUISyntaxParserTestLocal;

	FDreamUIAst Ast;
	FDreamUIDiagnosticBag Diagnostics;
	const bool bParsed = ParseWith(MakeSource({
		TEXT("Widget Root {"),
		TEXT("    as = 1"),
		TEXT("    props = 2"),
		TEXT("    events = 3"),
		TEXT("    emit = 4"),
		TEXT("    default = 5"),
		TEXT("    fill = 6"),
		TEXT("    if = 7"),
		TEXT("    else <- Other()"),
		TEXT("    Text as { }"),
		TEXT("    Text props { }"),
		TEXT("    Text emit { }"),
		TEXT("    Text if { }"),
		TEXT("    Text else { }"),
		TEXT("    Text default { }"),
		TEXT("}")
	}), {}, Ast, Diagnostics);
	if (ExpectClean(*this, TEXT("the new words as names"), bParsed, Diagnostics))
	{
		TestEqual(TEXT("eight properties"), Ast.Root.Properties.Num(), 8);
		TestEqual(TEXT("and six nodes with those ids"), Ast.Root.Children.Num(), 6);
		TestTrue(TEXT("none of them unnamed"), !Ast.Root.Children.ContainsByPredicate([](const FDreamUINode& InChild) { return InChild.bAnonymous; }));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUISyntaxParseLoopAndLineTest,
	"DreamGUI.Text.Syntax.AForTakesAFunctionOrAVariableAndTwoBindingsMayShareALine",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUISyntaxParseLoopAndLineTest::RunTest(const FString& Parameters)
{
	using namespace DreamUISyntaxParserTestLocal;

	FDreamUIAst Ast;
	FDreamUIDiagnosticBag Diagnostics;
	const bool bParsed = ParseWith(MakeSource({
		TEXT("Widget Root {"),
		TEXT("    for Option in GetOptions() {"),
		TEXT("        Row { Label <- Option.Label  Kind <- Option.Kind }"),
		TEXT("    }"),
		TEXT("    for Entry in Entries {"),
		TEXT("        Text Name { Text <- Entry.Name }"),
		TEXT("    }"),
		TEXT("}")
	}), {}, Ast, Diagnostics);
	if (!ExpectClean(*this, TEXT("two for loops"), bParsed, Diagnostics)
		|| !TestEqual(TEXT("both in the tree"), Ast.Root.Children.Num(), 2))
	{
		return false;
	}
	const FDreamUINode& ByFunction = Ast.Root.Children[0];
	TestTrue(TEXT("a for over a function"), ByFunction.Kind == EDreamUINodeKind::ForLoop && ByFunction.bLoopSourceIsFunction
		&& ByFunction.LoopSourceFunction == TEXT("GetOptions") && ByFunction.LoopVariable == TEXT("Option"));
	const FDreamUINode& ByVariable = Ast.Root.Children[1];
	TestTrue(TEXT("a for over a variable"), ByVariable.Kind == EDreamUINodeKind::ForLoop && !ByVariable.bLoopSourceIsFunction
		&& ByVariable.LoopSourceFunction == TEXT("Entries"));

	if (TestEqual(TEXT("the loop body holds its row"), ByFunction.Children.Num(), 1))
	{
		const TArray<FDreamUIProperty>& Lines = ByFunction.Children[0].Properties;
		TestTrue(TEXT("two bindings on one line are two properties"), Lines.Num() == 2
			&& Lines[0].BindingExpression.IsSet() && IsVariable(Lines[0].BindingExpression.GetValue(), TEXT("Option.Label"))
			&& Lines[1].BindingExpression.IsSet() && IsVariable(Lines[1].BindingExpression.GetValue(), TEXT("Option.Kind")));
	}
	return true;
}

#endif
