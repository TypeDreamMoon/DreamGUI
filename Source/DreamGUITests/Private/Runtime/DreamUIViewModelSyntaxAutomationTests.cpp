// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamWidget.h"
#include "Core/DreamWidgetEachBinding.h"
#include "Core/DreamWidgetPropertyBinding.h"
#include "Core/DreamWidgetTree.h"
#include "Text/DreamUIAst.h"
#include "Text/DreamUIDiagnostics.h"
#include "Text/DreamUISourceFile.h"
#include "Text/DreamUITextBuilder.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

/*
 * The front half of view models (plan 17): what the parser makes of a `viewmodels` block, of a dotted call, of a route
 * or a two-way line that names a member path, and of a loop whose source is one -- and what the builder records from
 * all of it.
 *
 * The compiler's thunk pass sits between the two in a real compile and never runs here: it is what lowers an
 * expression, records what a line reads (BindingDependencies) and turns `-> Settings.Apply()` outside a loop into a
 * generated handler. So the builder cases hand the builder an AST in the shape that pass leaves -- the dependency
 * fields, a handler name next to a route target -- and a file parsed straight from text shows the shape it gets when
 * that pass did not run, or refused. Inside a loop body the pass never runs anyway, so those cases read like the
 * files they are about.
 *
 * Assertions are on diagnostic codes and recorded fields, never on message text, as in the rest of the text suites.
 */
namespace DreamUIViewModelSyntaxTestLocal
{
	FString Join(const TArray<FString>& InLines)
	{
		return FString::Join(InLines, TEXT("\n"));
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

	bool Reported(const FDreamUIDiagnosticBag& InDiagnostics, EDreamUIDiagnosticCode InCode)
	{
		return CountOf(InDiagnostics, InCode) > 0;
	}

	/** A whole file, parsed as a compile parses one that has no `use` to follow. */
	bool Parse(const TArray<FString>& InLines, FDreamUIAst& OutAst, FDreamUIDiagnosticBag& OutDiagnostics)
	{
		return FDreamUISourceFile::Parse(Join(InLines), TEXT("ViewModelSyntax.dui"), OutAst, OutDiagnostics);
	}

	/** One line inside `Widget Root { … }`, and the property it became -- null when it became none. */
	const FDreamUIProperty* ParseLine(const FString& InLine, FDreamUIAst& OutAst, FDreamUIDiagnosticBag& OutDiagnostics)
	{
		Parse({ TEXT("Widget Root {"), TEXT("    ") + InLine, TEXT("}") }, OutAst, OutDiagnostics);
		return OutAst.bHasRoot && OutAst.Root.Properties.Num() > 0 ? &OutAst.Root.Properties[0] : nullptr;
	}

	/** The first loop node anywhere under the root. */
	const FDreamUINode* FindLoop(const FDreamUIAst& InAst)
	{
		const FDreamUINode* Found = nullptr;
		if (InAst.bHasRoot)
		{
			InAst.ForEachNode([&Found](const FDreamUINode& InNode)
			{
				if (Found == nullptr && (InNode.Kind == EDreamUINodeKind::ForLoop || InNode.Kind == EDreamUINodeKind::EachLoop))
				{
					Found = &InNode;
				}
			});
		}
		return Found;
	}

	/** One file's AST, its tree, and everything the build recorded -- with every sink a compile offers. */
	struct FBuilt
	{
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		TStrongObjectPtr<UDreamWidgetTree> Tree;
		TArray<FDreamWidgetPropertyBinding> Bindings;
		TArray<FDreamWidgetEventBinding> EventBindings;
		TArray<FDreamWidgetEachBinding> EachBindings;

		bool Reported(EDreamUIDiagnosticCode InCode) const { return DreamUIViewModelSyntaxTestLocal::Reported(Diagnostics, InCode); }
		int32 Count(EDreamUIDiagnosticCode InCode) const { return CountOf(Diagnostics, InCode); }

		const FDreamWidgetPropertyBinding* FindBinding(const TCHAR* InWidgetName, const TCHAR* InPropertyName) const
		{
			return Bindings.FindByPredicate([InWidgetName, InPropertyName](const FDreamWidgetPropertyBinding& InBinding)
			{
				return InBinding.WidgetName == FName(InWidgetName) && InBinding.PropertyName == FName(InPropertyName);
			});
		}

		/** Every diagnostic into the test's log, so a red run says what the file said rather than only that it failed. */
		void Dump(FAutomationTestBase& InTest) const
		{
			for (const FDreamUIDiagnostic& Diagnostic : Diagnostics.Diagnostics)
			{
				InTest.AddInfo(Diagnostic.ToString());
			}
		}
	};

	void BuildAst(FBuilt& OutBuilt)
	{
		OutBuilt.Diagnostics.SourceName = TEXT("ViewModelByHand.dui");
		OutBuilt.Tree.Reset(FDreamUITextBuilder::Build(OutBuilt.Ast, GetTransientPackage(), OutBuilt.Diagnostics,
			OutBuilt.Bindings, &OutBuilt.EventBindings, &OutBuilt.EachBindings));
	}

	/** Parse and, when the parse holds, build. The thunk pass does not run: see the file comment. */
	void BuildText(FBuilt& OutBuilt, const TArray<FString>& InLines)
	{
		OutBuilt.Diagnostics.SourceName = TEXT("ViewModelSyntax.dui");
		if (FDreamUISourceFile::Parse(Join(InLines), OutBuilt.Diagnostics.SourceName, OutBuilt.Ast, OutBuilt.Diagnostics))
		{
			BuildAst(OutBuilt);
		}
	}

	FDreamUINode MakeNode(const FString& InTypeName, const FString& InId, int32 InLine = 1)
	{
		FDreamUINode Node;
		Node.Kind = EDreamUINodeKind::Widget;
		Node.TypeName = InTypeName;
		Node.Id = InId;
		Node.Location = FDreamUISourceLocation(InLine, 1);
		return Node;
	}

	void SetRoot(FDreamUIAst& OutAst, FDreamUINode InRoot)
	{
		OutAst.ClassPath = TEXT("/Game/UI/WBP_ViewModelByHand");
		OutAst.ClassPathLocation = FDreamUISourceLocation(1, 1);
		OutAst.Root = MoveTemp(InRoot);
		OutAst.bHasRoot = true;
	}

	/** `<Name> <- …` as the thunk pass leaves it: the generated function's name, and what the line reads. */
	FDreamUIProperty MakeLoweredBinding(const TCHAR* InName, const TCHAR* InFunction, TArray<TArray<FString>> InDependencies,
		bool bInRecorded, bool bInComplete, int32 InLine)
	{
		FDreamUIProperty Property;
		Property.Name = InName;
		Property.BindingFunction = InFunction;
		Property.BindingDependencies = MoveTemp(InDependencies);
		Property.bBindingDependenciesRecorded = bInRecorded;
		Property.bBindingDependenciesComplete = bInComplete;
		Property.Location = FDreamUISourceLocation(InLine, 5);
		return Property;
	}

	/** `for Item in <Source> { Widget Row { } }` inside `Widget Root`, as text. */
	TArray<FString> ForLoopOver(const FString& InSource)
	{
		return {
			TEXT("Widget Root {"),
			FString::Printf(TEXT("    for Item in %s {"), *InSource),
			TEXT("        Widget Row { }"),
			TEXT("    }"),
			TEXT("}"),
		};
	}
}

// =================================================================================================== parse

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIViewModelParseFormsTest,
	"DreamGUI.Text.ViewModel.Parse.EveryViewModelsFormIsRecordedWithItsSourceAndWhereItWasWritten",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIViewModelParseFormsTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIViewModelSyntaxTestLocal;

	FDreamUIAst Ast;
	FDreamUIDiagnosticBag Diagnostics;
	const bool bParsed = Parse({
		TEXT("viewmodels {"),                                                                                    //  1
		TEXT("    PlayerVM Player"),                                                                             //  2
		TEXT("    SettingsVM Settings = new"),                                                                   //  3
		TEXT("    InventoryVM Inventory = global"),                                                              //  4
		TEXT("    InventoryVM Stash = global \"Stash\""),                                                        //  5
		TEXT("    PartyVM Party = parent"),                                                                      //  6
		TEXT("    PartyVM Guild = parent \"Guild\""),                                                            //  7
		TEXT("    /Script/DreamGUITests.DreamTestPlayerVM Typed; /Game/UI/BP_VM.BP_VM_C Blueprinted = new"),     //  8
		TEXT("}"),                                                                                               //  9
		TEXT("viewmodels { /Game/UI/BP_VM Loose }"),                                                             // 10
		TEXT("Widget Root {"),                                                                                   // 11
		TEXT("}"),                                                                                               // 12
	}, Ast, Diagnostics);
	TestTrue(*FString::Printf(TEXT("the file parses (%s)"), *Diagnostics.ToString()), bParsed);
	TestEqual(TEXT("with nothing to say"), Diagnostics.Diagnostics.Num(), 0);
	if (!TestEqual(TEXT("every entry of both blocks is recorded, in order"), Ast.ViewModels.Num(), 9))
	{
		return false;
	}

	const FDreamUIViewModelDecl& Player = Ast.ViewModels[0];
	TestEqual(TEXT("a type and a name"), Player.TypeName, FString(TEXT("PlayerVM")));
	TestEqual(TEXT("the name"), Player.Name, FString(TEXT("Player")));
	TestTrue(TEXT("nothing after the name: the host gives it"), Player.Source == EDreamUIViewModelSource::Host);
	TestTrue(TEXT("and no source name"), Player.SourceName.IsEmpty());
	TestEqual(TEXT("located at its name: the line"), Player.Location.Line, 2);
	TestEqual(TEXT("and the column"), Player.Location.Column, 14);
	TestEqual(TEXT("the type located where it was written: the line"), Player.TypeLocation.Line, 2);
	TestEqual(TEXT("and the column"), Player.TypeLocation.Column, 5);

	TestTrue(TEXT("'= new'"), Ast.ViewModels[1].Name == TEXT("Settings") && Ast.ViewModels[1].Source == EDreamUIViewModelSource::New);
	TestTrue(TEXT("'= global'"), Ast.ViewModels[2].Name == TEXT("Inventory") && Ast.ViewModels[2].Source == EDreamUIViewModelSource::Global
		&& Ast.ViewModels[2].SourceName.IsEmpty());
	TestTrue(TEXT("'= global \"Stash\"' keeps the name it was registered under"), Ast.ViewModels[3].Name == TEXT("Stash")
		&& Ast.ViewModels[3].Source == EDreamUIViewModelSource::Global && Ast.ViewModels[3].SourceName == TEXT("Stash"));
	TestTrue(TEXT("'= parent'"), Ast.ViewModels[4].Name == TEXT("Party") && Ast.ViewModels[4].Source == EDreamUIViewModelSource::Parent
		&& Ast.ViewModels[4].SourceName.IsEmpty());
	TestTrue(TEXT("'= parent \"Guild\"'"), Ast.ViewModels[5].Name == TEXT("Guild")
		&& Ast.ViewModels[5].Source == EDreamUIViewModelSource::Parent && Ast.ViewModels[5].SourceName == TEXT("Guild"));

	const FDreamUIViewModelDecl& Typed = Ast.ViewModels[6];
	TestEqual(TEXT("a /Script/ path is a type, kept as written"), Typed.TypeName, FString(TEXT("/Script/DreamGUITests.DreamTestPlayerVM")));
	TestTrue(TEXT("and a ';' ends the entry like a line break"), Typed.Name == TEXT("Typed") && Typed.Source == EDreamUIViewModelSource::Host);
	const FDreamUIViewModelDecl& Blueprinted = Ast.ViewModels[7];
	TestEqual(TEXT("so is a Blueprint class path"), Blueprinted.TypeName, FString(TEXT("/Game/UI/BP_VM.BP_VM_C")));
	TestTrue(TEXT("with a source after it"), Blueprinted.Name == TEXT("Blueprinted") && Blueprinted.Source == EDreamUIViewModelSource::New);
	TestEqual(TEXT("both on the line that wrote them"), Blueprinted.Location.Line, 8);
	const FDreamUIViewModelDecl& Loose = Ast.ViewModels[8];
	TestTrue(TEXT("a second block adds to the first, on one line too"), Loose.TypeName == TEXT("/Game/UI/BP_VM") && Loose.Name == TEXT("Loose")
		&& Loose.Location.Line == 10);

	const FDreamUIViewModelDecl* Found = Ast.FindViewModel(TEXT("stash"));
	TestTrue(TEXT("FindViewModel answers by name, as every name lookup does: case insensitively"), Found != nullptr && Found->Name == TEXT("Stash"));
	TestNull(TEXT("and nothing for a name no entry has"), Ast.FindViewModel(TEXT("Nobody")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIViewModelParseContextualWordsTest,
	"DreamGUI.Text.ViewModel.Parse.TheNewWordsAreKeywordsOnlyWhereTheyLead",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * `viewmodels` leads a block only at the top of a file and before its '{'; `new`, `global` and `parent` mean a source only
 * after the `=` of one of its lines. Anywhere else they are the names they always were.
 */
bool FDreamUIViewModelParseContextualWordsTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIViewModelSyntaxTestLocal;

	{
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		const bool bParsed = Parse({
			TEXT("viewmodels {"),
			TEXT("    global parent = global \"new\""),
			TEXT("    new global"),
			TEXT("}"),
			TEXT("Widget Root {"),
			TEXT("}"),
		}, Ast, Diagnostics);
		TestTrue(*FString::Printf(TEXT("a type or a name may be one of the source words (%s)"), *Diagnostics.ToString()), bParsed);
		if (TestEqual(TEXT("both lines are entries"), Ast.ViewModels.Num(), 2))
		{
			TestTrue(TEXT("type 'global', name 'parent', from the registry under \"new\""), Ast.ViewModels[0].TypeName == TEXT("global")
				&& Ast.ViewModels[0].Name == TEXT("parent") && Ast.ViewModels[0].Source == EDreamUIViewModelSource::Global
				&& Ast.ViewModels[0].SourceName == TEXT("new"));
			TestTrue(TEXT("type 'new', name 'global', given by the host"), Ast.ViewModels[1].TypeName == TEXT("new")
				&& Ast.ViewModels[1].Name == TEXT("global") && Ast.ViewModels[1].Source == EDreamUIViewModelSource::Host);
		}
	}
	{
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		const FDreamUIProperty* Property = ParseLine(TEXT("viewmodels = 3"), Ast, Diagnostics);
		TestTrue(*FString::Printf(TEXT("a property called viewmodels is still a property (%s)"), *Diagnostics.ToString()),
			Property != nullptr && Property->Name == TEXT("viewmodels") && Property->Value.Raw == TEXT("3"));
		TestEqual(TEXT("and declares no view model"), Ast.ViewModels.Num(), 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIViewModelParseMalformedTest,
	"DreamGUI.Text.ViewModel.Parse.AViewModelsLineOfAnyOtherShapeIsDUI2021AndTheRestOfTheBlockStillReads",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIViewModelParseMalformedTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIViewModelSyntaxTestLocal;

	// Each line alone in a block, followed by a root, so the only thing wrong with the file is the line.
	const TCHAR* BadLines[] =
	{
		TEXT("PlayerVM Player = nonsense"),
		TEXT("PlayerVM Player ="),
		TEXT("PlayerVM"),
		TEXT("PlayerVM Player = global \"\""),
		TEXT("PlayerVM Player = parent \"   \""),
		TEXT("PlayerVM Player = new Extra"),
		TEXT("PlayerVM Player Extra"),
		TEXT("3 Player"),
		TEXT("\"PlayerVM\" Player"),
	};
	for (const TCHAR* BadLine : BadLines)
	{
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		const bool bParsed = Parse({
			TEXT("viewmodels {"),
			FString(TEXT("    ")) + BadLine,
			TEXT("}"),
			TEXT("Widget Root {"),
			TEXT("}"),
		}, Ast, Diagnostics);
		TestFalse(*FString::Printf(TEXT("'%s' fails the parse"), BadLine), bParsed);
		TestTrue(*FString::Printf(TEXT("'%s' is DUI2021 (%s)"), BadLine, *Diagnostics.ToString()),
			Reported(Diagnostics, EDreamUIDiagnosticCode::MalformedViewModelsBlock));
		TestEqual(*FString::Printf(TEXT("'%s' declares nothing"), BadLine), Ast.ViewModels.Num(), 0);
		TestTrue(*FString::Printf(TEXT("'%s' leaves the root read"), BadLine), Ast.bHasRoot);
	}

	{
		// Inside a node: what the class holds is said at the top of the file, as `props` and `events` are.
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		Parse({
			TEXT("Widget Root {"),
			TEXT("    viewmodels {"),
			TEXT("        PlayerVM Player"),
			TEXT("    }"),
			TEXT("    Widget After { }"),
			TEXT("}"),
		}, Ast, Diagnostics);
		TestEqual(TEXT("a block inside a node is DUI2021, once"), CountOf(Diagnostics, EDreamUIDiagnosticCode::MalformedViewModelsBlock), 1);
		TestEqual(TEXT("and declares nothing"), Ast.ViewModels.Num(), 0);
		TestTrue(TEXT("and the node goes on after it"), Ast.Root.Children.Num() == 1 && Ast.Root.Children[0].Id == TEXT("After"));
	}
	{
		// One bad line costs that line, not its neighbours.
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		Parse({
			TEXT("viewmodels {"),
			TEXT("    PlayerVM Player = nonsense"),
			TEXT("    SettingsVM Settings = new"),
			TEXT("}"),
			TEXT("Widget Root {"),
			TEXT("}"),
		}, Ast, Diagnostics);
		TestEqual(TEXT("the bad line is said once"), CountOf(Diagnostics, EDreamUIDiagnosticCode::MalformedViewModelsBlock), 1);
		TestTrue(TEXT("and the good one after it is recorded"), Ast.ViewModels.Num() == 1 && Ast.ViewModels[0].Name == TEXT("Settings")
			&& Ast.ViewModels[0].Source == EDreamUIViewModelSource::New);
	}
	{
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		Parse({ TEXT("viewmodels {"), TEXT("    PlayerVM Player") }, Ast, Diagnostics);
		TestTrue(TEXT("a block that never closes is UnclosedBlock"), Reported(Diagnostics, EDreamUIDiagnosticCode::UnclosedBlock));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIViewModelParseDuplicateTest,
	"DreamGUI.Text.ViewModel.Parse.ASecondViewModelOfOneNameIsDUI3024AndTheFirstIsKept",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIViewModelParseDuplicateTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIViewModelSyntaxTestLocal;

	FDreamUIAst Ast;
	FDreamUIDiagnosticBag Diagnostics;
	const bool bParsed = Parse({
		TEXT("viewmodels {"),
		TEXT("    PlayerVM Player"),
		TEXT("    OtherVM player = new"),
		TEXT("}"),
		TEXT("viewmodels { ThirdVM Player = global }"),
		TEXT("Widget Root {"),
		TEXT("}"),
	}, Ast, Diagnostics);
	TestFalse(TEXT("a duplicate fails the parse"), bParsed);
	TestEqual(TEXT("each later one is DUI3024 -- a name differing only in case included, since the name becomes an FName"),
		CountOf(Diagnostics, EDreamUIDiagnosticCode::DuplicateViewModel), 2);
	const bool bAllErrors = !Diagnostics.Diagnostics.ContainsByPredicate([](const FDreamUIDiagnostic& InDiagnostic)
	{
		return InDiagnostic.Code == EDreamUIDiagnosticCode::DuplicateViewModel && InDiagnostic.Severity != EDreamUISeverity::Error;
	});
	TestTrue(TEXT("as an error, the way a duplicate prop is"), bAllErrors);
	if (TestEqual(TEXT("the first one is kept, alone"), Ast.ViewModels.Num(), 1))
	{
		TestTrue(TEXT("as it was written"), Ast.ViewModels[0].TypeName == TEXT("PlayerVM")
			&& Ast.ViewModels[0].Source == EDreamUIViewModelSource::Host);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIViewModelParseDottedCallTest,
	"DreamGUI.Text.ViewModel.Parse.ADottedCallIsACallWithADottedSymbolAndNeverTheBareName",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * `Player.FormatGold(Player.Gold)` is a Call whose Symbol is the whole dotted path. What matters as much is where it
 * goes: a bare `Func()` rides BindingFunction as the one name of a function of the CLASS, and a dotted one must not --
 * it names a function of another object, which only the thunk pass can reach -- so it stays the expression.
 */
bool FDreamUIViewModelParseDottedCallTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIViewModelSyntaxTestLocal;

	{
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		const FDreamUIProperty* Property = ParseLine(TEXT("Text <- Player.FormatGold(Player.Gold)"), Ast, Diagnostics);
		if (TestTrue(*FString::Printf(TEXT("a dotted call with an argument parses (%s)"), *Diagnostics.ToString()),
			Property != nullptr && Property->BindingExpression.IsSet()))
		{
			const FDreamUIExpression& Call = Property->BindingExpression.GetValue();
			TestTrue(TEXT("as a Call"), Call.Kind == FDreamUIExpression::EKind::Call);
			TestEqual(TEXT("whose Symbol is the whole path"), Call.Symbol, FString(TEXT("Player.FormatGold")));
			TestTrue(TEXT("and whose argument is the dotted read it was"), Call.Operands.Num() == 1
				&& Call.Operands[0].Kind == FDreamUIExpression::EKind::VariableRef && Call.Operands[0].Symbol == TEXT("Player.Gold"));
			TestTrue(TEXT("nothing rides BindingFunction"), Property->BindingFunction.IsEmpty());
		}
	}
	{
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		const FDreamUIProperty* Property = ParseLine(TEXT("Text <- Player.Stats.Describe()"), Ast, Diagnostics);
		if (TestTrue(*FString::Printf(TEXT("a dotted call with no argument parses (%s)"), *Diagnostics.ToString()), Property != nullptr))
		{
			TestTrue(TEXT("and stays an expression, never the bare name"), Property->BindingFunction.IsEmpty() && Property->BindingExpression.IsSet());
			TestTrue(TEXT("a Call of the whole path"), Property->BindingExpression.IsSet()
				&& Property->BindingExpression.GetValue().Kind == FDreamUIExpression::EKind::Call
				&& Property->BindingExpression.GetValue().Symbol == TEXT("Player.Stats.Describe")
				&& Property->BindingExpression.GetValue().Operands.Num() == 0);
		}
	}
	{
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		const FDreamUIProperty* Property = ParseLine(TEXT("Text <- Player.Stats.Title"), Ast, Diagnostics);
		TestTrue(TEXT("a dotted read without parentheses is still the member path it was"), Property != nullptr
			&& Property->BindingExpression.IsSet()
			&& Property->BindingExpression.GetValue().Kind == FDreamUIExpression::EKind::VariableRef
			&& Property->BindingExpression.GetValue().Symbol == TEXT("Player.Stats.Title"));
	}
	{
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		const FDreamUIProperty* Property = ParseLine(TEXT("Text <- GetTitle()"), Ast, Diagnostics);
		TestTrue(TEXT("a bare call still travels as the one name"), Property != nullptr
			&& Property->BindingFunction == TEXT("GetTitle") && !Property->BindingExpression.IsSet());
	}
	{
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		ParseLine(TEXT("Text <- Player.Format(1"), Ast, Diagnostics);
		TestTrue(TEXT("an unclosed dotted call is DUI2011"), Reported(Diagnostics, EDreamUIDiagnosticCode::MalformedBindingExpression));
	}
	{
		// The condition of an `if` is the same grammar, and the `Shown` it makes has to take the same shape.
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		const bool bParsed = Parse({
			TEXT("Widget Root {"),
			TEXT("    if Settings.IsDirty() {"),
			TEXT("        Widget Hint { }"),
			TEXT("    }"),
			TEXT("    if HasSave() {"),
			TEXT("        Widget Continue { }"),
			TEXT("    }"),
			TEXT("}"),
		}, Ast, Diagnostics);
		if (TestTrue(*FString::Printf(TEXT("conditions on dotted calls parse (%s)"), *Diagnostics.ToString()), bParsed)
			&& TestEqual(TEXT("both branches' widgets are the root's"), Ast.Root.Children.Num(), 2))
		{
			const FDreamUINode& Hint = Ast.Root.Children[0];
			const FDreamUINode& Continue = Ast.Root.Children[1];
			TestTrue(TEXT("a dotted condition makes a Shown that keeps the expression"), Hint.Properties.Num() == 1
				&& Hint.Properties[0].BindingFunction.IsEmpty() && Hint.Properties[0].BindingExpression.IsSet()
				&& Hint.Properties[0].BindingExpression.GetValue().Symbol == TEXT("Settings.IsDirty"));
			TestTrue(TEXT("a bare one still the bare name"), Continue.Properties.Num() == 1
				&& Continue.Properties[0].BindingFunction == TEXT("HasSave"));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIViewModelParseMemberRouteTest,
	"DreamGUI.Text.ViewModel.Parse.ARouteToAMemberPathRecordsItsTargetAndArguments",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIViewModelParseMemberRouteTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIViewModelSyntaxTestLocal;

	{
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		const FDreamUIProperty* Property = ParseLine(TEXT("OnClicked -> Settings.Apply()"), Ast, Diagnostics);
		if (TestTrue(*FString::Printf(TEXT("'-> Path.Func()' parses (%s)"), *Diagnostics.ToString()), Property != nullptr))
		{
			TestEqual(TEXT("the path goes to RouteTarget whole"), Property->RouteTarget, FString(TEXT("Settings.Apply")));
			TestTrue(TEXT("the parentheses are remembered"), Property->bRouteHasArgumentList);
			TestEqual(TEXT("with nothing in them"), Property->RouteArguments.Num(), 0);
			TestTrue(TEXT("no handler yet: the thunk pass writes one"), Property->EventHandler.IsEmpty());
			TestTrue(TEXT("it is an event line"), Property->IsEventBinding() && !Property->IsBinding());
			TestTrue(TEXT("written with the arrow"), Property->RouteOperator == EDreamUIRouteOperator::Arrow);
		}
	}
	{
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		const FDreamUIProperty* Property = ParseLine(TEXT("OnValueChanged -> Settings.SetVolume"), Ast, Diagnostics);
		TestTrue(TEXT("'-> Path.Func' forwards, and says so by having no parentheses"), Property != nullptr
			&& Property->RouteTarget == TEXT("Settings.SetVolume") && !Property->bRouteHasArgumentList
			&& Property->RouteArguments.Num() == 0 && Property->EventHandler.IsEmpty());
	}
	{
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		const FDreamUIProperty* Property = ParseLine(TEXT("OnValueChanged -> Player.Stats.Adjust(Value * 2, @Step, Player.Gold)"), Ast, Diagnostics);
		if (TestTrue(*FString::Printf(TEXT("arguments are expressions (%s)"), *Diagnostics.ToString()), Property != nullptr))
		{
			TestEqual(TEXT("a deeper path is kept whole"), Property->RouteTarget, FString(TEXT("Player.Stats.Adjust")));
			if (TestEqual(TEXT("three arguments"), Property->RouteArguments.Num(), 3))
			{
				TestTrue(TEXT("the first an operator over the event's parameter"), Property->RouteArguments[0].Kind == FDreamUIExpression::EKind::Binary);
				TestTrue(TEXT("the second a resource"), Property->RouteArguments[1].Kind == FDreamUIExpression::EKind::Literal
					&& Property->RouteArguments[1].LiteralKind == EDreamUIValueKind::ResourceRef);
				TestTrue(TEXT("the third a member path"), Property->RouteArguments[2].Kind == FDreamUIExpression::EKind::VariableRef
					&& Property->RouteArguments[2].Symbol == TEXT("Player.Gold"));
			}
		}
	}
	{
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		const FDreamUIProperty* Property = ParseLine(TEXT("OnClicked -> Confirm"), Ast, Diagnostics);
		TestTrue(TEXT("a plain handler is what it always was"), Property != nullptr && Property->EventHandler == TEXT("Confirm")
			&& Property->RouteTarget.IsEmpty() && !Property->bRouteHasArgumentList);
	}
	{
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		const FDreamUIProperty* Property = ParseLine(TEXT("OnClicked -> emit Picked(1)"), Ast, Diagnostics);
		TestTrue(TEXT("and so is an emit"), Property != nullptr && Property->EmitEvent == TEXT("Picked")
			&& Property->EmitArguments.Num() == 1 && Property->RouteTarget.IsEmpty());
	}
	{
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		ParseLine(TEXT("OnClicked -> Settings."), Ast, Diagnostics);
		TestTrue(TEXT("a path that stops at a dot is DUI2011"), Reported(Diagnostics, EDreamUIDiagnosticCode::MalformedBindingExpression));
	}
	{
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		ParseLine(TEXT("OnClicked -> Settings.Apply(1"), Ast, Diagnostics);
		TestTrue(TEXT("an argument list that never closes is DUI2011"), Reported(Diagnostics, EDreamUIDiagnosticCode::MalformedBindingExpression));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIViewModelParseTwoWayPathTest,
	"DreamGUI.Text.ViewModel.Parse.ATwoWayLineTakesAMemberPath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIViewModelParseTwoWayPathTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIViewModelSyntaxTestLocal;

	{
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		const FDreamUIProperty* Property = ParseLine(TEXT("Value <-> Settings.Audio.Volume"), Ast, Diagnostics);
		TestTrue(*FString::Printf(TEXT("'<-> Path.Member' keeps the whole path (%s)"), *Diagnostics.ToString()), Property != nullptr
			&& Property->TwoWayProperty == TEXT("Settings.Audio.Volume") && Property->IsBinding());
	}
	{
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		const FDreamUIProperty* Property = ParseLine(TEXT("Value <-> Volume"), Ast, Diagnostics);
		TestTrue(TEXT("a variable of the class is what it always was"), Property != nullptr && Property->TwoWayProperty == TEXT("Volume"));
	}
	{
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		ParseLine(TEXT("Value <-> Settings."), Ast, Diagnostics);
		TestTrue(TEXT("a path that stops at a dot is DUI2011"), Reported(Diagnostics, EDreamUIDiagnosticCode::MalformedBindingExpression));
	}
	{
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		ParseLine(TEXT("Value <-> Settings.GetVolume()"), Ast, Diagnostics);
		TestTrue(TEXT("a call has nothing to write back into, and stays refused"), Diagnostics.HasErrors());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIViewModelParseLoopSourceTest,
	"DreamGUI.Text.ViewModel.Parse.ALoopSourceMayBeAMemberPathWhoseLastSegmentTheParenthesesDescribe",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIViewModelParseLoopSourceTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIViewModelSyntaxTestLocal;

	struct FCase
	{
		const TCHAR* Header;
		const TCHAR* Source;
		bool bFunction;
		EDreamUINodeKind Kind;
	};
	const FCase Cases[] =
	{
		{ TEXT("for Item in Inventory.Items"), TEXT("Inventory.Items"), false, EDreamUINodeKind::ForLoop },
		{ TEXT("each Row in Inventory.Filtered()"), TEXT("Inventory.Filtered"), true, EDreamUINodeKind::EachLoop },
		{ TEXT("for Item in Player.Stats.Rows()"), TEXT("Player.Stats.Rows"), true, EDreamUINodeKind::ForLoop },
		// The two one-word shapes, exactly as before.
		{ TEXT("for Item in Items"), TEXT("Items"), false, EDreamUINodeKind::ForLoop },
		{ TEXT("each Item in GetItems()"), TEXT("GetItems"), true, EDreamUINodeKind::EachLoop },
	};
	for (const FCase& Case : Cases)
	{
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		const bool bParsed = Parse({
			TEXT("Widget Root {"),
			FString::Printf(TEXT("    %s {"), Case.Header),
			TEXT("        Widget Row { }"),
			TEXT("    }"),
			TEXT("}"),
		}, Ast, Diagnostics);
		TestTrue(*FString::Printf(TEXT("'%s' parses (%s)"), Case.Header, *Diagnostics.ToString()), bParsed);
		const FDreamUINode* Loop = FindLoop(Ast);
		if (!TestNotNull(*FString::Printf(TEXT("'%s' is a loop"), Case.Header), Loop))
		{
			continue;
		}
		TestTrue(*FString::Printf(TEXT("'%s' is the loop it says"), Case.Header), Loop->Kind == Case.Kind);
		TestEqual(*FString::Printf(TEXT("'%s' keeps its source whole"), Case.Header), Loop->LoopSourceFunction, FString(Case.Source));
		TestTrue(*FString::Printf(TEXT("'%s' describes its last segment"), Case.Header), Loop->bLoopSourceIsFunction == Case.bFunction);
	}
	{
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		Parse({
			TEXT("Widget Root {"),
			TEXT("    for Item in Inventory. {"),
			TEXT("        Widget Row { }"),
			TEXT("    }"),
			TEXT("}"),
		}, Ast, Diagnostics);
		TestTrue(TEXT("a source that stops at a dot is a malformed header"), Reported(Diagnostics, EDreamUIDiagnosticCode::MalformedLoopHeader));
		TestNull(TEXT("and makes no loop"), FindLoop(Ast));
	}
	{
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		Parse({
			TEXT("Widget Root {"),
			TEXT("    for Item in Inventory.Filtered(1) {"),
			TEXT("        Widget Row { }"),
			TEXT("    }"),
			TEXT("}"),
		}, Ast, Diagnostics);
		TestTrue(TEXT("a source called with an argument is a malformed header, dotted or not"),
			Reported(Diagnostics, EDreamUIDiagnosticCode::MalformedLoopHeader));
	}
	return true;
}

// =================================================================================================== build

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIViewModelBuildDependenciesTest,
	"DreamGUI.Text.ViewModel.Build.WhatALineReadsRidesOntoItsBinding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The thunk pass records what each `<-` reads; the builder copies it onto the binding the run time subscribes from --
 * each path segment by segment, and both flags as they were, because "recorded and empty" (a constant) and "never
 * recorded" (an older compile, nobody lowered it) are different instructions to the run time.
 */
bool FDreamUIViewModelBuildDependenciesTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIViewModelSyntaxTestLocal;

	FBuilt Built;
	FDreamUINode Root = MakeNode(TEXT("Widget"), TEXT("Root"));
	// `RenderOpacity <- Player.Health > 0 && !Busy`, lowered.
	Root.Properties.Add(MakeLoweredBinding(TEXT("RenderOpacity"), TEXT("__DreamThunk_Root_RenderOpacity"),
		{ { TEXT("Player") }, { TEXT("Player"), TEXT("Health") }, { TEXT("Busy") } }, true, true, 2));
	// `ToolTipText <- "fixed"` -- recorded, and reads nothing.
	Root.Properties.Add(MakeLoweredBinding(TEXT("ToolTipText"), TEXT("__DreamThunk_Root_ToolTipText"), {}, true, true, 3));

	FDreamUINode Child = MakeNode(TEXT("Widget"), TEXT("Child"), 4);
	// `RenderOpacity <- Fade()`, which nobody lowered: nothing recorded.
	Child.Properties.Add(MakeLoweredBinding(TEXT("RenderOpacity"), TEXT("Fade"), {}, false, false, 5));
	// `ToolTipText <- Player.FormatGold(Player.Gold)`: recorded, and not complete -- a call with an argument.
	Child.Properties.Add(MakeLoweredBinding(TEXT("ToolTipText"), TEXT("__DreamThunk_Child_ToolTipText"),
		{ { TEXT("Player") }, { TEXT("Player"), TEXT("Gold") } }, true, false, 6));
	Root.Children.Add(MoveTemp(Child));

	FDreamUINode Odd = MakeNode(TEXT("Widget"), TEXT("Odd"), 7);
	// A path one of whose segments no FName can hold: hand-built only, and never watched.
	Odd.Properties.Add(MakeLoweredBinding(TEXT("RenderOpacity"), TEXT("__DreamThunk_Odd_RenderOpacity"),
		{ { TEXT("Player") }, { TEXT("Player"), FString::ChrN(NAME_SIZE + 4, TEXT('x')) } }, true, true, 8));
	Root.Children.Add(MoveTemp(Odd));

	SetRoot(Built.Ast, MoveTemp(Root));
	BuildAst(Built);
	if (!TestTrue(TEXT("the tree built"), Built.Tree.IsValid()))
	{
		Built.Dump(*this);
		return false;
	}

	const FDreamWidgetPropertyBinding* Full = Built.FindBinding(TEXT("Root"), TEXT("RenderOpacity"));
	if (TestNotNull(TEXT("the expression binding was recorded"), Full))
	{
		TestTrue(TEXT("with its dependencies recorded"), Full->bDependenciesRecorded);
		TestTrue(TEXT("and complete"), Full->bDependenciesComplete);
		if (TestEqual(TEXT("all three paths"), Full->Dependencies.Num(), 3))
		{
			TestTrue(TEXT("{Player}"), Full->Dependencies[0].Segments == TArray<FName>{ FName(TEXT("Player")) });
			TestTrue(TEXT("{Player, Health}, segment by segment"),
				Full->Dependencies[1].Segments == TArray<FName>({ FName(TEXT("Player")), FName(TEXT("Health")) }));
			TestTrue(TEXT("{Busy}"), Full->Dependencies[2].Segments == TArray<FName>{ FName(TEXT("Busy")) });
		}
		TestEqual(TEXT("and the function is still the generated one"), Full->FunctionName, FName(TEXT("__DreamThunk_Root_RenderOpacity")));
	}

	const FDreamWidgetPropertyBinding* Constant = Built.FindBinding(TEXT("Root"), TEXT("ToolTipText"));
	TestTrue(TEXT("a constant is recorded as reading nothing -- not as unknown"), Constant != nullptr
		&& Constant->bDependenciesRecorded && Constant->bDependenciesComplete && Constant->Dependencies.Num() == 0);

	const FDreamWidgetPropertyBinding* Unlowered = Built.FindBinding(TEXT("Child"), TEXT("RenderOpacity"));
	TestTrue(TEXT("a line nobody lowered records nothing, as every binding before it"), Unlowered != nullptr
		&& !Unlowered->bDependenciesRecorded && !Unlowered->bDependenciesComplete && Unlowered->Dependencies.Num() == 0
		&& Unlowered->FunctionName == FName(TEXT("Fade")));

	const FDreamWidgetPropertyBinding* Incomplete = Built.FindBinding(TEXT("Child"), TEXT("ToolTipText"));
	TestTrue(TEXT("an incomplete one keeps its paths and says it is incomplete"), Incomplete != nullptr
		&& Incomplete->bDependenciesRecorded && !Incomplete->bDependenciesComplete && Incomplete->Dependencies.Num() == 2);

	const FDreamWidgetPropertyBinding* Unwatchable = Built.FindBinding(TEXT("Odd"), TEXT("RenderOpacity"));
	TestTrue(TEXT("a path no name can spell is dropped, and the binding marked incomplete so it polls"), Unwatchable != nullptr
		&& Unwatchable->Dependencies.Num() == 1 && !Unwatchable->bDependenciesComplete);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIViewModelBuildTwoWayPathTest,
	"DreamGUI.Text.ViewModel.Build.ATwoWayMemberPathKeepsItsNotifyFieldAndCarriesItsDependencies",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIViewModelBuildTwoWayPathTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIViewModelSyntaxTestLocal;

	FBuilt Built;
	FDreamUINode Root = MakeNode(TEXT("Widget"), TEXT("Root"));
	FDreamUINode Volume = MakeNode(TEXT("Native.Slider"), TEXT("Volume"), 2);
	// `Value <-> Settings.Volume`, as the thunk pass leaves it: the getter in BindingFunction, the path read recorded.
	FDreamUIProperty Mirror = MakeLoweredBinding(TEXT("Value"), TEXT("__DreamThunk_Volume_Value"),
		{ { TEXT("Settings") }, { TEXT("Settings"), TEXT("Volume") } }, true, true, 3);
	Mirror.TwoWayProperty = TEXT("Settings.Volume");
	Volume.Properties.Add(MoveTemp(Mirror));
	Root.Children.Add(MoveTemp(Volume));
	SetRoot(Built.Ast, MoveTemp(Root));
	BuildAst(Built);
	if (!TestTrue(TEXT("the tree built"), Built.Tree.IsValid()))
	{
		Built.Dump(*this);
		return false;
	}

	const FDreamWidgetPropertyBinding* Binding = Built.FindBinding(TEXT("Volume"), TEXT("Value"));
	if (TestNotNull(TEXT("the forward half was recorded"), Binding))
	{
		TestEqual(TEXT("its NotifyField is the path as written, as a one-word one is the word"), Binding->NotifyField,
			FName(TEXT("Settings.Volume")));
		TestTrue(TEXT("and it carries what it reads"), Binding->bDependenciesRecorded && Binding->Dependencies.Num() == 2);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIViewModelBuildLoopSourceTest,
	"DreamGUI.Text.ViewModel.Build.ADottedLoopSourceIsSplitIntoAPathAndAOneWordSourceStaysAsItWas",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIViewModelBuildLoopSourceTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIViewModelSyntaxTestLocal;

	struct FCase
	{
		const TCHAR* Source;
		TArray<FName> Path;
		FName Name;
		bool bFunction;
	};
	const FCase Cases[] =
	{
		{ TEXT("Inventory.Items"), { TEXT("Inventory"), TEXT("Items") }, TEXT("Items"), false },
		{ TEXT("Inventory.Filtered()"), { TEXT("Inventory"), TEXT("Filtered") }, TEXT("Filtered"), true },
		{ TEXT("Player.Stats.Rows"), { TEXT("Player"), TEXT("Stats"), TEXT("Rows") }, TEXT("Rows"), false },
		{ TEXT("Items"), {}, TEXT("Items"), false },
		{ TEXT("GetItems()"), {}, TEXT("GetItems"), true },
	};
	for (const FCase& Case : Cases)
	{
		FBuilt Built;
		BuildText(Built, ForLoopOver(Case.Source));
		if (!TestTrue(*FString::Printf(TEXT("'in %s' builds"), Case.Source), Built.Tree.IsValid()))
		{
			Built.Dump(*this);
			continue;
		}
		if (!TestEqual(*FString::Printf(TEXT("'in %s' is one loop"), Case.Source), Built.EachBindings.Num(), 1))
		{
			continue;
		}
		const FDreamWidgetEachBinding& Loop = Built.EachBindings[0];
		TestTrue(*FString::Printf(TEXT("'in %s' has the path it should (%d segments)"), Case.Source, Loop.SourcePath.Num()),
			Loop.SourcePath == Case.Path);
		TestEqual(*FString::Printf(TEXT("'in %s' names its last segment"), Case.Source), Loop.SourceName, Case.Name);
		TestTrue(*FString::Printf(TEXT("'in %s' says what that segment is"), Case.Source), Loop.bSourceIsFunction == Case.bFunction);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIViewModelBuildEntryRouteTest,
	"DreamGUI.Text.ViewModel.Build.ARouteToTheItemInALoopBodyIsRecordedForEachCopyToBind",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIViewModelBuildEntryRouteTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIViewModelSyntaxTestLocal;

	FBuilt Built;
	BuildText(Built, {
		TEXT("Widget Root {"),                                      //  1
		TEXT("    for Item in Inventory.Items {"),                  //  2
		TEXT("        Widget Row {"),                               //  3
		TEXT("            Native.Button Use {"),                    //  4
		TEXT("                OnClicked -> Item.Use()"),            //  5
		TEXT("            }"),                                      //  6
		TEXT("            Native.Slider Amount {"),                 //  7
		TEXT("                OnValueChanged -> Item.UseWithValue"),//  8
		TEXT("            }"),                                      //  9
		TEXT("            Native.Button Plain {"),                  // 10
		TEXT("                OnClicked -> HandlePlain"),           // 11
		TEXT("            }"),                                      // 12
		TEXT("        }"),                                          // 13
		TEXT("    }"),                                              // 14
		TEXT("}"),                                                  // 15
	});
	if (!TestTrue(TEXT("the file builds"), Built.Tree.IsValid()))
	{
		Built.Dump(*this);
		return false;
	}
	if (!TestEqual(TEXT("one loop"), Built.EachBindings.Num(), 1))
	{
		return false;
	}
	const FDreamWidgetEachBinding& Loop = Built.EachBindings[0];
	if (TestEqual(TEXT("both item routes are on the loop"), Loop.EntryRoutes.Num(), 2))
	{
		const FDreamWidgetEntryRoute& Use = Loop.EntryRoutes[0];
		TestEqual(TEXT("the first on the button, by the name its copies keep"), Use.TargetWidgetDisplayName, FName(TEXT("Use")));
		TestTrue(TEXT("on the widget itself"), Use.Target == EDreamWidgetBindingTarget::Widget && Use.BehaviourIndex == INDEX_NONE);
		TestEqual(TEXT("its event"), Use.EventName, FName(TEXT("OnClicked")));
		TestEqual(TEXT("the item's function"), Use.ItemFunction, FName(TEXT("Use")));
		TestTrue(TEXT("called with nothing, as the parentheses said"), Use.bCallWithoutArguments);
#if WITH_EDITORONLY_DATA
		TestEqual(TEXT("remembering its line"), Use.SourceLine, 5);
		TestEqual(TEXT("and column"), Use.SourceColumn, 17);
#endif

		const FDreamWidgetEntryRoute& Amount = Loop.EntryRoutes[1];
		TestEqual(TEXT("the second on the slider"), Amount.TargetWidgetDisplayName, FName(TEXT("Amount")));
		TestEqual(TEXT("its event"), Amount.EventName, FName(TEXT("OnValueChanged")));
		TestEqual(TEXT("the item's function"), Amount.ItemFunction, FName(TEXT("UseWithValue")));
		TestFalse(TEXT("forwarding what the event sends, with no parentheses"), Amount.bCallWithoutArguments);
	}
	// A plain handler in a loop body keeps what it always did: one class route on the template.
	if (TestEqual(TEXT("the plain route is an ordinary route, as before"), Built.EventBindings.Num(), 1))
	{
		TestEqual(TEXT("to the class's handler"), Built.EventBindings[0].FunctionName, FName(TEXT("HandlePlain")));
	}
	TestEqual(TEXT("and no item route became a class route"), Built.EventBindings.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIViewModelBuildRefusedLoopRouteTest,
	"DreamGUI.Text.ViewModel.Build.ALoopBodyRoutesOnlyToAFunctionOfItsItemAndRefusesTheRestAsDUI5014",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIViewModelBuildRefusedLoopRouteTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIViewModelSyntaxTestLocal;

	FBuilt Built;
	BuildText(Built, {
		TEXT("Widget Root {"),
		TEXT("    for Item in Inventory.Items {"),
		TEXT("        Widget Row {"),
		TEXT("            Native.Button WithArgs { OnClicked -> Item.Use(1) }"),
		TEXT("            Native.Button TooDeep { OnClicked -> Item.Owner.Use() }"),
		TEXT("            Native.Button Elsewhere { OnClicked -> Settings.Apply() }"),
		TEXT("            Native.Button Fine { OnClicked -> Item.Use() }"),
		TEXT("        }"),
		TEXT("    }"),
		TEXT("}"),
	});
	TestEqual(TEXT("arguments, a deeper path and a path that is not the item's are each DUI5014"),
		Built.Count(EDreamUIDiagnosticCode::LoopBodyBindingUnsupported), 3);
	if (TestEqual(TEXT("the loop is still recorded"), Built.EachBindings.Num(), 1))
	{
		TestTrue(TEXT("with the one route it can hold"), Built.EachBindings[0].EntryRoutes.Num() == 1
			&& Built.EachBindings[0].EntryRoutes[0].TargetWidgetDisplayName == FName(TEXT("Fine")));
	}
	TestEqual(TEXT("and none became a class route"), Built.EventBindings.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIViewModelBuildMemberRouteTest,
	"DreamGUI.Text.ViewModel.Build.AMemberRouteOutsideALoopIsAnOrdinaryRouteOnceLoweredAndNothingBefore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIViewModelBuildMemberRouteTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIViewModelSyntaxTestLocal;

	{
		FBuilt Built;
		FDreamUINode Root = MakeNode(TEXT("Widget"), TEXT("Root"));

		// `OnClicked -> Settings.Apply()` after the thunk pass: the route target, and the handler it generated.
		FDreamUINode Apply = MakeNode(TEXT("Native.Button"), TEXT("Apply"), 2);
		FDreamUIProperty Lowered;
		Lowered.Name = TEXT("OnClicked");
		Lowered.RouteTarget = TEXT("Settings.Apply");
		Lowered.bRouteHasArgumentList = true;
		Lowered.EventHandler = TEXT("__DreamRoute_Apply_OnClicked");
		Lowered.Location = FDreamUISourceLocation(3, 9);
		Apply.Properties.Add(MoveTemp(Lowered));
		Root.Children.Add(MoveTemp(Apply));

		// The same line the pass refused (it said why): no handler, and nothing more to say here.
		FDreamUINode Refused = MakeNode(TEXT("Native.Button"), TEXT("Refused"), 4);
		FDreamUIProperty Unlowered;
		Unlowered.Name = TEXT("OnClicked");
		Unlowered.RouteTarget = TEXT("Settings.Missing");
		Unlowered.Location = FDreamUISourceLocation(5, 9);
		Refused.Properties.Add(MoveTemp(Unlowered));
		Root.Children.Add(MoveTemp(Refused));

		SetRoot(Built.Ast, MoveTemp(Root));
		BuildAst(Built);
		if (!TestTrue(TEXT("the tree built"), Built.Tree.IsValid()))
		{
			Built.Dump(*this);
			return false;
		}
		TestEqual(TEXT("the builder found nothing wrong with either"), Built.Diagnostics.NumErrors(), 0);
		if (TestEqual(TEXT("one route, the lowered one"), Built.EventBindings.Num(), 1))
		{
			const FDreamWidgetEventBinding& Route = Built.EventBindings[0];
			TestEqual(TEXT("on the button"), Route.WidgetName, FName(TEXT("Apply")));
			TestEqual(TEXT("its event"), Route.EventName, FName(TEXT("OnClicked")));
			TestEqual(TEXT("to the generated handler"), Route.FunctionName, FName(TEXT("__DreamRoute_Apply_OnClicked")));
#if WITH_EDITORONLY_DATA
			TestEqual(TEXT("from its line"), Route.SourceLine, 3);
#endif
		}
	}
	{
		// Straight from text, as the write-back's reference tree sees it: the pass never ran, and the route is skipped.
		FBuilt Built;
		BuildText(Built, {
			TEXT("Widget Root {"),
			TEXT("    Native.Button Apply {"),
			TEXT("        OnClicked -> Settings.Apply()"),
			TEXT("    }"),
			TEXT("}"),
		});
		TestTrue(TEXT("an unlowered member route builds clean"), Built.Tree.IsValid() && Built.Diagnostics.NumErrors() == 0);
		TestEqual(TEXT("and records nothing"), Built.EventBindings.Num(), 0);
	}
	{
		// The event checks still run first: a route onto something that is not an event says so, lowered or not.
		FBuilt Built;
		BuildText(Built, {
			TEXT("Widget Root {"),
			TEXT("    Native.Button Apply {"),
			TEXT("        RenderOpacity -> Settings.Apply()"),
			TEXT("    }"),
			TEXT("}"),
		});
		TestTrue(TEXT("a member route onto a non-event is DUI5010"), Built.Reported(EDreamUIDiagnosticCode::EventNotFound));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIViewModelBuildItemReadTest,
	"DreamGUI.Text.ViewModel.Build.AnItemReadInALoopBodyStaysOneHop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIViewModelBuildItemReadTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIViewModelSyntaxTestLocal;

	FBuilt Built;
	BuildText(Built, {
		TEXT("Widget Root {"),
		TEXT("    for Item in Inventory.Items {"),
		TEXT("        Widget Row {"),
		TEXT("            ToolTipText <- Item.Name"),
		TEXT("            Widget Deep { ToolTipText <- Item.Stats.Name }"),
		TEXT("            Widget Called { ToolTipText <- Item.Describe() }"),
		TEXT("        }"),
		TEXT("    }"),
		TEXT("}"),
	});
	if (TestEqual(TEXT("one loop"), Built.EachBindings.Num(), 1))
	{
		TestTrue(TEXT("the one-hop read is an item write, as before"), Built.EachBindings[0].EntryBindings.Num() == 1
			&& Built.EachBindings[0].EntryBindings[0].ItemMember == FName(TEXT("Name")));
	}
	TestTrue(TEXT("a two-hop item read is still refused"), Built.Reported(EDreamUIDiagnosticCode::BindingExpressionUnsupported));
	TestTrue(TEXT("and a call on the item is an expression a loop body cannot hold"),
		Built.Reported(EDreamUIDiagnosticCode::LoopBodyBindingUnsupported));
	TestEqual(TEXT("none of them became a class binding"), Built.Bindings.Num(), 0);
	return true;
}

#endif
