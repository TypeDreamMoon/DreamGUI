// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamWidget.h"
#include "Core/DreamWidgetEachBinding.h"
#include "Core/DreamWidgetPropertyBinding.h"
#include "Core/DreamWidgetTree.h"
#include "DreamRouteOperatorTestTypes.h"
#include "Text/DreamUIAst.h"
#include "Text/DreamUIDiagnostics.h"
#include "Text/DreamUISourceFile.h"
#include "Text/DreamUITextBuilder.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

/*
 * The three operators an event line is written with, and the one rule they carry: `+=` adds a listener, so it needs an
 * event that holds many; `=` is THE listener, so it needs one that holds one (a single-cast delegate); `->` takes either.
 *
 * The parser records the operator, and reads `=` as a route only by shape -- a dotted name or `emit` -- since one word
 * after `=` is exactly an enum value's spelling; the builder, which knows the destination, turns `OnPicked = Handler` into
 * the route it is and holds every route to its operator (RouteOperatorMismatch). None of the shipping widgets has a
 * single-cast delegate, so the builder cases put UDreamRouteOperatorTestBehaviour on a widget: one event of each kind.
 */
namespace DreamUIRouteOperatorTestLocal
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

	/** Lines inside `Widget Root { … }`; OutAst holds whatever they became. */
	bool ParseLines(const TArray<FString>& InBodyLines, FDreamUIAst& OutAst, FDreamUIDiagnosticBag& OutDiagnostics)
	{
		TArray<FString> Lines = { TEXT("Widget Root {") };
		for (const FString& Line : InBodyLines)
		{
			Lines.Add(TEXT("    ") + Line);
		}
		Lines.Add(TEXT("}"));
		return FDreamUISourceFile::Parse(Join(Lines), TEXT("RouteOperator.dui"), OutAst, OutDiagnostics);
	}

	const FDreamUIProperty* ParseLine(const FString& InLine, FDreamUIAst& OutAst, FDreamUIDiagnosticBag& OutDiagnostics)
	{
		ParseLines({ InLine }, OutAst, OutDiagnostics);
		return OutAst.bHasRoot && OutAst.Root.Properties.Num() > 0 ? &OutAst.Root.Properties[0] : nullptr;
	}

	/** The behaviour with one event of each kind, by the path a `+` line names it with. */
	const TCHAR* const TestBehaviour = TEXT("/Script/DreamGUITests.DreamRouteOperatorTestBehaviour");

	struct FBuilt
	{
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		TStrongObjectPtr<UDreamWidgetTree> Tree;
		TArray<FDreamWidgetPropertyBinding> Bindings;
		TArray<FDreamWidgetEventBinding> EventBindings;
		TArray<FDreamWidgetEachBinding> EachBindings;

		int32 Count(EDreamUIDiagnosticCode InCode) const { return CountOf(Diagnostics, InCode); }

		/** The route recorded on InWidgetName's InEventName, or null. */
		const FDreamWidgetEventBinding* FindRoute(const TCHAR* InWidgetName, const TCHAR* InEventName) const
		{
			return EventBindings.FindByPredicate([InWidgetName, InEventName](const FDreamWidgetEventBinding& InBinding)
			{
				return InBinding.WidgetName == FName(InWidgetName) && InBinding.EventName == FName(InEventName);
			});
		}

		void Dump(FAutomationTestBase& InTest) const
		{
			for (const FDreamUIDiagnostic& Diagnostic : Diagnostics.Diagnostics)
			{
				InTest.AddInfo(Diagnostic.ToString());
			}
		}
	};

	void BuildText(FBuilt& OutBuilt, const TArray<FString>& InLines)
	{
		OutBuilt.Diagnostics.SourceName = TEXT("RouteOperator.dui");
		if (FDreamUISourceFile::Parse(Join(InLines), OutBuilt.Diagnostics.SourceName, OutBuilt.Ast, OutBuilt.Diagnostics))
		{
			OutBuilt.Tree.Reset(FDreamUITextBuilder::Build(OutBuilt.Ast, GetTransientPackage(), OutBuilt.Diagnostics,
				OutBuilt.Bindings, &OutBuilt.EventBindings, &OutBuilt.EachBindings));
		}
	}

	/** `Widget <Id> { + <the test behaviour> { <InLine> } }`, two lines of a file. */
	TArray<FString> WidgetWithBehaviourLine(const TCHAR* InId, const TCHAR* InLine)
	{
		return {
			FString::Printf(TEXT("    Widget %s {"), InId),
			FString::Printf(TEXT("        + %s { %s }"), TestBehaviour, InLine),
			TEXT("    }"),
		};
	}
}

// =================================================================================================== parse

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIRouteOperatorParseTest,
	"DreamGUI.Text.RouteOperator.Parse.EveryRouteFormTakesEveryOperatorAndOneWordAfterEqualsStaysAValue",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIRouteOperatorParseTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIRouteOperatorTestLocal;

	struct FCase
	{
		const TCHAR* Line;
		EDreamUIRouteOperator Operator;
		const TCHAR* Handler;
		const TCHAR* Target;
		const TCHAR* Emit;
		bool bArgumentList;
	};
	const FCase Cases[] =
	{
		{ TEXT("OnChosen += HandleChosen"),          EDreamUIRouteOperator::Append, TEXT("HandleChosen"), TEXT(""),               TEXT(""),       false },
		{ TEXT("OnChosen += Settings.Apply()"),      EDreamUIRouteOperator::Append, TEXT(""),             TEXT("Settings.Apply"), TEXT(""),       true },
		{ TEXT("OnChosen += Settings.SetVolume"),    EDreamUIRouteOperator::Append, TEXT(""),             TEXT("Settings.SetVolume"), TEXT(""),   false },
		{ TEXT("OnChosen += emit Picked(1)"),        EDreamUIRouteOperator::Append, TEXT(""),             TEXT(""),               TEXT("Picked"), false },
		{ TEXT("OnPicked = Settings.Apply()"),       EDreamUIRouteOperator::Assign, TEXT(""),             TEXT("Settings.Apply"), TEXT(""),       true },
		{ TEXT("OnPicked = Settings.SetVolume"),     EDreamUIRouteOperator::Assign, TEXT(""),             TEXT("Settings.SetVolume"), TEXT(""),   false },
		{ TEXT("OnPicked = emit Picked(2)"),         EDreamUIRouteOperator::Assign, TEXT(""),             TEXT(""),               TEXT("Picked"), false },
		{ TEXT("OnPicked = emit Picked"),            EDreamUIRouteOperator::Assign, TEXT(""),             TEXT(""),               TEXT("Picked"), false },
		{ TEXT("OnClicked -> HandleClick"),          EDreamUIRouteOperator::Arrow,  TEXT("HandleClick"),  TEXT(""),               TEXT(""),       false },
		{ TEXT("OnClicked -> Settings.Apply()"),     EDreamUIRouteOperator::Arrow,  TEXT(""),             TEXT("Settings.Apply"), TEXT(""),       true },
	};
	for (const FCase& Case : Cases)
	{
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		const FDreamUIProperty* Property = ParseLine(Case.Line, Ast, Diagnostics);
		if (!TestTrue(*FString::Printf(TEXT("'%s' parses (%s)"), Case.Line, *Diagnostics.ToString()),
			Property != nullptr && !Diagnostics.HasErrors()))
		{
			continue;
		}
		TestTrue(*FString::Printf(TEXT("'%s' is an event line"), Case.Line), Property->IsEventBinding());
		TestTrue(*FString::Printf(TEXT("'%s' records its operator"), Case.Line), Property->RouteOperator == Case.Operator);
		TestEqual(*FString::Printf(TEXT("'%s' handler"), Case.Line), Property->EventHandler, FString(Case.Handler));
		TestEqual(*FString::Printf(TEXT("'%s' route target"), Case.Line), Property->RouteTarget, FString(Case.Target));
		TestEqual(*FString::Printf(TEXT("'%s' emitted event"), Case.Line), Property->EmitEvent, FString(Case.Emit));
		TestTrue(*FString::Printf(TEXT("'%s' argument list"), Case.Line), Property->bRouteHasArgumentList == Case.bArgumentList);
		TestTrue(*FString::Printf(TEXT("'%s' carries no value"), Case.Line), Property->Value.Raw.IsEmpty());
	}

	{
		// One word after `=` is a value to the parser, whatever it is going to be: the builder decides.
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		const FDreamUIProperty* Property = ParseLine(TEXT("OnPicked = HandlePick"), Ast, Diagnostics);
		TestTrue(TEXT("'= Handler' stays an identifier value"), Property != nullptr && !Property->IsEventBinding()
			&& Property->Value.Kind == EDreamUIValueKind::Identifier && Property->Value.Raw == TEXT("HandlePick")
			&& Property->RouteOperator == EDreamUIRouteOperator::Arrow);
	}
	{
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		const FDreamUIProperty* Property = ParseLine(TEXT("HorizontalAlignment = Fill"), Ast, Diagnostics);
		TestTrue(TEXT("an enum value is the value it always was"), Property != nullptr && !Property->IsEventBinding()
			&& Property->Value.Raw == TEXT("Fill"));
	}
	{
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		const FDreamUIProperty* Property = ParseLine(TEXT("Kind = emit"), Ast, Diagnostics);
		TestTrue(TEXT("'emit' alone after '=' is still a value"), Property != nullptr && !Property->IsEventBinding()
			&& Property->Value.Raw == TEXT("emit"));
	}
	{
		// `+=` is one token, and `+` still leads a component; a binding still ends where an event line plainly begins.
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		const bool bParsed = ParseLines({
			TEXT("+ VerticalBox { Spacing = 4 }"),
			TEXT("ToolTipText <- Title()  OnChosen += HandleChosen"),
		}, Ast, Diagnostics);
		TestTrue(*FString::Printf(TEXT("a component and two lines on one parse (%s)"), *Diagnostics.ToString()), bParsed);
		TestEqual(TEXT("the component is still one"), Ast.Root.Components.Num(), 1);
		if (TestEqual(TEXT("and both properties are read"), Ast.Root.Properties.Num(), 2))
		{
			TestTrue(TEXT("the binding"), Ast.Root.Properties[0].BindingFunction == TEXT("Title"));
			TestTrue(TEXT("and the appended route"), Ast.Root.Properties[1].EventHandler == TEXT("HandleChosen")
				&& Ast.Root.Properties[1].RouteOperator == EDreamUIRouteOperator::Append);
		}
	}
	{
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		ParseLine(TEXT("OnChosen += 3"), Ast, Diagnostics);
		TestTrue(TEXT("'+=' with no handler after it is refused"), Diagnostics.HasErrors());
	}
	return true;
}

// =================================================================================================== build

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIRouteOperatorBuildTest,
	"DreamGUI.Text.RouteOperator.Build.EachOperatorIsHeldToTheKindOfEventItNamesAsDUI5025",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIRouteOperatorBuildTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIRouteOperatorTestLocal;

	TArray<FString> Lines = { TEXT("Widget Root {") };
	Lines.Append(WidgetWithBehaviourLine(TEXT("AppendMulti"), TEXT("OnChosen += HandleChosen")));
	Lines.Append(WidgetWithBehaviourLine(TEXT("AppendSingle"), TEXT("OnPicked += HandlePick")));
	Lines.Append(WidgetWithBehaviourLine(TEXT("AssignSingle"), TEXT("OnPicked = HandlePick")));
	Lines.Append(WidgetWithBehaviourLine(TEXT("AssignMulti"), TEXT("OnChosen = HandleChosen")));
	Lines.Append(WidgetWithBehaviourLine(TEXT("ArrowSingle"), TEXT("OnPicked -> HandlePick")));
	Lines.Append(WidgetWithBehaviourLine(TEXT("ArrowMulti"), TEXT("OnChosen -> HandleChosen")));
	// Not an event: one word after `=` on anything else is the value it always was.
	Lines.Add(TEXT("    Widget Plain { Visibility = Hidden }"));
	Lines.Add(TEXT("}"));

	FBuilt Built;
	BuildText(Built, Lines);
	TestEqual(TEXT("'+=' on a single-cast delegate and '=' on a multicast event are each DUI5025"),
		Built.Count(EDreamUIDiagnosticCode::RouteOperatorMismatch), 2);
	if (Built.Diagnostics.NumErrors() != 2)
	{
		Built.Dump(*this);
	}
	TestEqual(TEXT("and nothing else is wrong with the file"), Built.Diagnostics.NumErrors(), 2);

	const FDreamWidgetEventBinding* AppendMulti = Built.FindRoute(TEXT("AppendMulti"), TEXT("OnChosen"));
	TestTrue(TEXT("'+=' onto a multicast event is recorded, on the behaviour"), AppendMulti != nullptr
		&& AppendMulti->FunctionName == FName(TEXT("HandleChosen")) && AppendMulti->Target == EDreamWidgetBindingTarget::Behaviour
		&& AppendMulti->BehaviourIndex != INDEX_NONE);
	TestNull(TEXT("'+=' onto a single-cast delegate is not"), Built.FindRoute(TEXT("AppendSingle"), TEXT("OnPicked")));

	const FDreamWidgetEventBinding* AssignSingle = Built.FindRoute(TEXT("AssignSingle"), TEXT("OnPicked"));
	TestTrue(TEXT("'= Handler' onto a single-cast delegate is read as the route it is, and recorded"), AssignSingle != nullptr
		&& AssignSingle->FunctionName == FName(TEXT("HandlePick")) && AssignSingle->Target == EDreamWidgetBindingTarget::Behaviour);
	TestNull(TEXT("'=' onto a multicast event is not"), Built.FindRoute(TEXT("AssignMulti"), TEXT("OnChosen")));

	TestNotNull(TEXT("'->' onto a single-cast delegate is recorded"), Built.FindRoute(TEXT("ArrowSingle"), TEXT("OnPicked")));
	TestNotNull(TEXT("'->' onto a multicast event is recorded"), Built.FindRoute(TEXT("ArrowMulti"), TEXT("OnChosen")));
	TestEqual(TEXT("four routes in all"), Built.EventBindings.Num(), 4);
	TestEqual(TEXT("and no line became a property binding"), Built.Bindings.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIRouteOperatorValueTest,
	"DreamGUI.Text.RouteOperator.Build.OneWordAfterEqualsOnAPropertyThatIsNoEventIsStillAValue",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIRouteOperatorValueTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIRouteOperatorTestLocal;

	FBuilt Built;
	BuildText(Built, {
		TEXT("Widget Root {"),
		TEXT("    Widget Plain {"),
		TEXT("        Visibility = Hidden"),
		TEXT("    }"),
		TEXT("}"),
	});
	if (!TestTrue(*FString::Printf(TEXT("the file builds (%s)"), *Built.Diagnostics.ToString()), Built.Tree.IsValid()))
	{
		return false;
	}
	UDreamWidget* Plain = nullptr;
	Built.Tree->ForEachWidget([&Plain](UDreamWidget* InWidget)
	{
		if (Plain == nullptr && IsValid(InWidget) && InWidget->GetDisplayName() == TEXT("Plain"))
		{
			Plain = InWidget;
		}
	});
	TestTrue(TEXT("the enum value was written"), Plain != nullptr && Plain->GetVisibility() == EDreamWidgetVisibility::Hidden);
	TestEqual(TEXT("and no route was made of it"), Built.EventBindings.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIRouteOperatorLoopTest,
	"DreamGUI.Text.RouteOperator.Build.ALoopBodyRouteToTheItemIsHeldToTheSameRule",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIRouteOperatorLoopTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIRouteOperatorTestLocal;

	FBuilt Built;
	BuildText(Built, {
		TEXT("Widget Root {"),
		TEXT("    for Item in Inventory.Items {"),
		TEXT("        Widget Row {"),
		FString::Printf(TEXT("            Widget Picks { + %s { OnPicked = Item.UseWithValue } }"), TestBehaviour),
		FString::Printf(TEXT("            Widget Chooses { + %s { OnChosen += Item.Use() } }"), TestBehaviour),
		FString::Printf(TEXT("            Widget Wrong { + %s { OnPicked += Item.Use() } }"), TestBehaviour),
		TEXT("        }"),
		TEXT("    }"),
		TEXT("}"),
	});
	TestEqual(TEXT("'+=' onto a single-cast delegate is DUI5025 in a loop body too"),
		Built.Count(EDreamUIDiagnosticCode::RouteOperatorMismatch), 1);
	if (!TestEqual(TEXT("one loop"), Built.EachBindings.Num(), 1))
	{
		Built.Dump(*this);
		return false;
	}
	const TArray<FDreamWidgetEntryRoute>& Routes = Built.EachBindings[0].EntryRoutes;
	if (TestEqual(TEXT("the two routes that fit their events are recorded per copy"), Routes.Num(), 2))
	{
		TestTrue(TEXT("'= Item.Func' onto a single-cast delegate"), Routes[0].TargetWidgetDisplayName == FName(TEXT("Picks"))
			&& Routes[0].EventName == FName(TEXT("OnPicked")) && Routes[0].ItemFunction == FName(TEXT("UseWithValue"))
			&& !Routes[0].bCallWithoutArguments && Routes[0].Target == EDreamWidgetBindingTarget::Behaviour);
		TestTrue(TEXT("'+= Item.Func()' onto a multicast one"), Routes[1].TargetWidgetDisplayName == FName(TEXT("Chooses"))
			&& Routes[1].EventName == FName(TEXT("OnChosen")) && Routes[1].ItemFunction == FName(TEXT("Use"))
			&& Routes[1].bCallWithoutArguments);
	}
	return true;
}

#endif
