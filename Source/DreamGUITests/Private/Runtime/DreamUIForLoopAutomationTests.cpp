// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIForAdapter.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetEachBinding.h"
#include "Core/DreamWidgetPropertyBinding.h"
#include "Core/DreamWidgetTree.h"
#include "DreamForLoopTestTypes.h"
#include "DreamScopedWorld.h"
#include "DreamWidgetBehaviourTestTypes.h"
#include "Interaction/DreamContentWidget.h"
#include "Text/DreamUIAst.h"
#include "Text/DreamUIDiagnostics.h"
#include "Text/DreamUITextBuilder.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

/*
 * `for Option in GetOptions() { Row { Label <- Option.Label } }`: one copy of a template per item, made at run time
 * inside an ordinary panel.
 *
 * Two halves, and each is pinned where it can be pinned without the other. The builder half (DreamGUI.Text.Syntax)
 * builds hand-made ASTs, so a red test there is the builder's and never the parser's: what a `for` turns into, and
 * every way of writing one in the wrong place. The run-time half (DreamGUI.Binding.For) drives UDreamUIForAdapter
 * over a tree assembled by hand in a game world -- no compiled class, so a red test there is the adapter's: copies,
 * their order, the item writes, the refresh, the template's state, and a copy that is a user widget of its own. The
 * route from a compiled class through UDreamUserWidget::ResolveEachBindings is the compile suite's
 * (DreamUIForLoopCompileAutomationTests.cpp), which is the only place a generated class can come from.
 *
 * Assertions are on diagnostic codes, never on message text, as in the rest of the builder's suite.
 */

namespace DreamUIForLoopTestLocal
{
	using DreamTests::FScopedGameWorld;

	// ---------------------------------------------------------------------------------------- builder fixtures

	FDreamUISourceLocation At(int32 InLine = 1)
	{
		return FDreamUISourceLocation(InLine, 1);
	}

	FDreamUINode MakeNode(const FString& InTypeName, const FString& InId, int32 InLine = 1)
	{
		FDreamUINode Node;
		Node.Kind = EDreamUINodeKind::Widget;
		Node.TypeName = InTypeName;
		Node.Id = InId;
		Node.Location = At(InLine);
		return Node;
	}

	/** `for <Variable> in <Source>()`, or `in <Source>` when bInFunction is false. */
	FDreamUINode MakeFor(const FString& InVariable, const FString& InSource, bool bInFunction = true, int32 InLine = 1)
	{
		FDreamUINode Loop;
		Loop.Kind = EDreamUINodeKind::ForLoop;
		Loop.LoopVariable = InVariable;
		Loop.LoopSourceFunction = InSource;
		Loop.bLoopSourceIsFunction = bInFunction;
		Loop.Location = At(InLine);
		return Loop;
	}

	/** `<Name> <- <Path>`, the way the parser hands a dotted read over: an un-lowered variable reference. */
	FDreamUIProperty BindRead(const FString& InName, const FString& InPath)
	{
		FDreamUIExpression Read;
		Read.Kind = FDreamUIExpression::EKind::VariableRef;
		Read.Symbol = InPath;
		Read.Location = At();

		FDreamUIProperty Property;
		Property.Name = InName;
		Property.BindingExpression = MoveTemp(Read);
		Property.Location = At();
		return Property;
	}

	FDreamUIAst AstWith(FDreamUINode InRoot)
	{
		FDreamUIAst Ast;
		Ast.ClassPath = TEXT("/Game/UI/WBP_ForLoopTest");
		Ast.ClassPathLocation = At();
		Ast.Root = MoveTemp(InRoot);
		Ast.bHasRoot = true;
		return Ast;
	}

	struct FBuildOutcome
	{
		TStrongObjectPtr<UDreamWidgetTree> Tree;
		FDreamUIDiagnosticBag Diagnostics;
		TArray<FDreamWidgetPropertyBinding> Bindings;
		TArray<FDreamWidgetEventBinding> EventBindings;
		TArray<FDreamWidgetEachBinding> EachBindings;

		UDreamWidget* Root() const { return Tree.IsValid() ? Tree->RootWidget.Get() : nullptr; }

		UDreamWidget* Find(const TCHAR* InDisplayName) const
		{
			UDreamWidget* Found = nullptr;
			if (Tree.IsValid())
			{
				Tree->ForEachWidget([&Found, InDisplayName](UDreamWidget* Widget)
				{
					if (Found == nullptr && Widget->GetDisplayName() == InDisplayName)
					{
						Found = Widget;
					}
				});
			}
			return Found;
		}

		bool HasCode(EDreamUIDiagnosticCode InCode) const
		{
			return Diagnostics.Diagnostics.ContainsByPredicate(
				[InCode](const FDreamUIDiagnostic& Diagnostic) { return Diagnostic.Code == InCode; });
		}
	};

	/** A build that offers somewhere to record loops, as a compile does. */
	FBuildOutcome BuildWithLoops(const FDreamUIAst& InAst)
	{
		FBuildOutcome Outcome;
		Outcome.Diagnostics.SourceName = TEXT("ForLoopTest.dui");
		Outcome.Tree.Reset(FDreamUITextBuilder::Build(InAst, GetTransientPackage(), Outcome.Diagnostics, Outcome.Bindings,
			&Outcome.EventBindings, &Outcome.EachBindings));
		return Outcome;
	}

	// ---------------------------------------------------------------------------------------- run-time fixtures

	/**
	 * Owner > Host > [Header, Entry (> Inner), Footer], registered in a game world: the shape a compiled class would
	 * have instanced, with Entry the `for` template. Header and Footer are there so "where the template is" means
	 * something -- copies that merely went to the end of the panel would land after Footer.
	 */
	struct FForTree
	{
		UDreamForLoopTestHost* Owner = nullptr;
		UDreamWidget* Host = nullptr;
		UDreamWidget* Header = nullptr;
		UDreamWidget* Template = nullptr;
		UDreamWidget* Footer = nullptr;

		static UDreamWidget* MakeWidget(UWorld* InWorld, UDreamWidget* InParent, const TCHAR* InName)
		{
			UDreamWidget* Widget = NewObject<UDreamWidget>(InWorld, NAME_None, RF_Public | RF_Transactional);
			Widget->SetDisplayName(InName);
			Widget->SetParentBeforeRegister(InParent);
			return Widget;
		}

		/** InTemplate null: a plain Entry with an Inner child. Otherwise that widget, unregistered, is the template. */
		explicit FForTree(UWorld* InWorld, UDreamWidget* InTemplate = nullptr)
		{
			Owner = CreateDreamWidget<UDreamForLoopTestHost>(InWorld);
			if (Owner == nullptr)
			{
				return;
			}
			Host = MakeWidget(InWorld, Owner, TEXT("Host"));
			Header = MakeWidget(InWorld, Host, TEXT("Header"));
			if (InTemplate != nullptr)
			{
				Template = InTemplate;
				Template->SetDisplayName(TEXT("Entry"));
				Template->SetParentBeforeRegister(Host);
			}
			else
			{
				Template = MakeWidget(InWorld, Host, TEXT("Entry"));
				MakeWidget(InWorld, Template, TEXT("Inner"));
				// Something of the template's own that no item writes, so a test can see it was left alone.
				Template->SetToolTipText(FText::FromString(TEXT("Authored")));
			}
			Footer = MakeWidget(InWorld, Host, TEXT("Footer"));
			// One registration for the whole subtree, after it is assembled -- the way an instanced class arrives.
			RegisterDreamWidgetHierarchy(Host);
		}

		bool IsValidFixture() const { return Owner != nullptr && Host != nullptr && Template != nullptr; }

		/** The host's children, which is what "in order, where the template is" is asserted against. */
		TArray<UDreamWidget*> Children() const { return Host->GetChildren(); }

		UDreamForLoopTestItem* AddItem(const TCHAR* InLabel, float InAmount) const
		{
			UDreamForLoopTestItem* Item = NewObject<UDreamForLoopTestItem>(Owner);
			Item->Label = FText::FromString(InLabel);
			Item->Amount = InAmount;
			Owner->Options.Add(Item);
			return Item;
		}
	};

	/**
	 * The binding the builder would have recorded for
	 *
	 *     for Option in GetOptions() {            (or `in Options`)
	 *         Widget Entry { ToolTipText <- Option.Label   Widget Inner { RenderOpacity <- Option.Amount } }
	 *     }
	 *
	 * -- two entries, on the template's root and one level down, both through setters.
	 */
	FDreamWidgetEachBinding MakeForBinding(bool bInFunction)
	{
		FDreamWidgetEachBinding Binding;
		Binding.bInPanel = true;
		Binding.HostWidgetName = TEXT("Host");
		Binding.TemplateWidgetName = TEXT("Entry");
		Binding.SourceName = bInFunction ? TEXT("GetOptions") : TEXT("Options");
		Binding.bSourceIsFunction = bInFunction;
		Binding.LoopVariable = TEXT("Option");

		FDreamWidgetEntryBinding& LabelEntry = Binding.EntryBindings.AddDefaulted_GetRef();
		LabelEntry.TargetWidgetDisplayName = TEXT("Entry");
		LabelEntry.Target = EDreamWidgetBindingTarget::Widget;
		LabelEntry.PropertyName = TEXT("ToolTipText");
		LabelEntry.SetterName = TEXT("SetToolTipText");
		LabelEntry.ItemMember = TEXT("Label");

		FDreamWidgetEntryBinding& AmountEntry = Binding.EntryBindings.AddDefaulted_GetRef();
		AmountEntry.TargetWidgetDisplayName = TEXT("Inner");
		AmountEntry.Target = EDreamWidgetBindingTarget::Widget;
		AmountEntry.PropertyName = UDreamWidget::GetPropertyName_RenderOpacity();
		AmountEntry.SetterName = TEXT("SetRenderOpacity");
		AmountEntry.ItemMember = TEXT("Amount");
		return Binding;
	}

	FString TooltipOf(const UDreamWidget* InWidget)
	{
		return IsValid(InWidget) ? InWidget->GetToolTipText().ToString() : FString(TEXT("<none>"));
	}

	/** The expected host children: Header, the template, the copies in order, Footer. */
	TArray<UDreamWidget*> ExpectedOrder(const FForTree& InTree, const TArray<UDreamWidget*>& InCopies)
	{
		TArray<UDreamWidget*> Expected;
		Expected.Add(InTree.Header);
		Expected.Add(InTree.Template);
		Expected.Append(InCopies);
		Expected.Add(InTree.Footer);
		return Expected;
	}
}

// ============================================================================================ builder

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIForBuildsInPlaceTest,
	"DreamGUI.Text.Syntax.AForBuildsItsTemplateWhereItIsWrittenAndRecordsAnInPanelBinding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIForBuildsInPlaceTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIForLoopTestLocal;

	FDreamUINode Root = MakeNode(TEXT("Widget"), TEXT("Root"));
	Root.Children.Add(MakeNode(TEXT("Widget"), TEXT("Header")));
	FDreamUINode Loop = MakeFor(TEXT("Option"), TEXT("GetOptions"), /*bInFunction*/true, /*InLine*/4);
	FDreamUINode Entry = MakeNode(TEXT("Widget"), TEXT("Entry"));
	Entry.Properties.Add(BindRead(TEXT("ToolTipText"), TEXT("Option.Label")));
	FDreamUINode Inner = MakeNode(TEXT("Widget"), TEXT("Inner"));
	Inner.Properties.Add(BindRead(TEXT("RenderOpacity"), TEXT("Option.Amount")));
	Entry.Children.Add(Inner);
	Loop.Children.Add(Entry);
	Root.Children.Add(Loop);
	Root.Children.Add(MakeNode(TEXT("Widget"), TEXT("Footer")));

	const FBuildOutcome Outcome = BuildWithLoops(AstWith(Root));
	if (!TestNotNull(TEXT("the tree built"), Outcome.Root()))
	{
		return false;
	}
	TestFalse(TEXT("and built clean"), Outcome.Diagnostics.HasErrors());

	// One template and nothing else: no content widget is synthesized, which is the whole of the difference from
	// an `each` at build time. The host's own container arranges the copies.
	TestEqual(TEXT("the loop contributed exactly its template and what is inside it"), Outcome.Tree->CountWidgets(), 5);
	UDreamWidget* Template = Outcome.Find(TEXT("Entry"));
	if (!TestNotNull(TEXT("the template was built"), Template))
	{
		return false;
	}
	const TArray<UDreamWidget*> RootChildren = Outcome.Root()->GetChildren();
	if (TestEqual(TEXT("the host has its three children"), RootChildren.Num(), 3))
	{
		TestEqual(TEXT("Header first"), RootChildren[0], Outcome.Find(TEXT("Header")));
		TestEqual(TEXT("the template where the 'for' was written"), RootChildren[1], Template);
		TestEqual(TEXT("Footer last"), RootChildren[2], Outcome.Find(TEXT("Footer")));
	}
	// Collapsing is the run time's: the class's authored copy is what the designer shows, as written.
	TestEqual(TEXT("the template is built visible"), Template->GetVisibility(), EDreamWidgetVisibility::Visible);
	// The item writes are per copy, never the class's: a class binding here would drive the template only.
	TestEqual(TEXT("no item write became a class binding"), Outcome.Bindings.Num(), 0);

	if (!TestEqual(TEXT("one loop was recorded"), Outcome.EachBindings.Num(), 1))
	{
		return false;
	}
	const FDreamWidgetEachBinding& Recorded = Outcome.EachBindings[0];
	TestTrue(TEXT("as a 'for', run in the panel"), Recorded.bInPanel);
	TestEqual(TEXT("hosted by the widget it is written in"), Recorded.HostWidgetName, FName(TEXT("Root")));
	TestEqual(TEXT("repeating the template"), Recorded.TemplateWidgetName, FName(TEXT("Entry")));
	TestTrue(TEXT("with no content widget"), Recorded.ContentWidgetName.IsNone());
	TestEqual(TEXT("from the source named"), Recorded.SourceName, FName(TEXT("GetOptions")));
	TestTrue(TEXT("which is a function"), Recorded.bSourceIsFunction);
	TestEqual(TEXT("under the loop variable written"), Recorded.LoopVariable, FName(TEXT("Option")));
#if WITH_EDITORONLY_DATA
	// The header's line, which is where DUI6006 and DUI6007 point.
	TestEqual(TEXT("remembering the header's line"), Recorded.SourceLine, 4);
#endif
	if (TestEqual(TEXT("with both item writes"), Recorded.EntryBindings.Num(), 2))
	{
		const FDreamWidgetEntryBinding& Label = Recorded.EntryBindings[0];
		TestEqual(TEXT("the first lands on the template's root"), Label.TargetWidgetDisplayName, FName(TEXT("Entry")));
		TestEqual(TEXT("on its tooltip"), Label.PropertyName, FName(TEXT("ToolTipText")));
		TestEqual(TEXT("through its setter"), Label.SetterName, FName(TEXT("SetToolTipText")));
		TestEqual(TEXT("from the item's Label"), Label.ItemMember, FName(TEXT("Label")));

		const FDreamWidgetEntryBinding& Amount = Recorded.EntryBindings[1];
		TestEqual(TEXT("the second one level down"), Amount.TargetWidgetDisplayName, FName(TEXT("Inner")));
		TestEqual(TEXT("through that widget's setter"), Amount.SetterName, FName(TEXT("SetRenderOpacity")));
		TestEqual(TEXT("from the item's Amount"), Amount.ItemMember, FName(TEXT("Amount")));
	}

	// The variable spelling, `for Option in Options`: the same record, with the source read rather than called.
	FDreamUINode VariableRoot = MakeNode(TEXT("Widget"), TEXT("Root"));
	FDreamUINode VariableLoop = MakeFor(TEXT("Option"), TEXT("Options"), /*bInFunction*/false);
	VariableLoop.Children.Add(MakeNode(TEXT("Widget"), TEXT("Entry")));
	VariableRoot.Children.Add(VariableLoop);
	const FBuildOutcome FromVariable = BuildWithLoops(AstWith(VariableRoot));
	if (TestEqual(TEXT("the variable form is recorded too"), FromVariable.EachBindings.Num(), 1))
	{
		TestFalse(TEXT("as a variable source"), FromVariable.EachBindings[0].bSourceIsFunction);
		TestEqual(TEXT("named as written"), FromVariable.EachBindings[0].SourceName, FName(TEXT("Options")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIForMisplacedTest,
	"DreamGUI.Text.Syntax.AForThatIsTheRootNestedFullOrNotOneWidgetIsRefusedAsMisplaced",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIForMisplacedTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIForLoopTestLocal;

	struct FCase
	{
		const TCHAR* What;
		FDreamUIAst Ast;
	};
	TArray<FCase> Cases;

	{
		// Nothing to repeat inside: the root is the class's own widget, and there is no panel above it.
		FDreamUINode Loop = MakeFor(TEXT("Option"), TEXT("GetOptions"));
		Loop.Children.Add(MakeNode(TEXT("Widget"), TEXT("Entry")));
		Cases.Add({ TEXT("a 'for' as the root"), AstWith(Loop) });
	}
	{
		FDreamUINode Inner = MakeFor(TEXT("Detail"), TEXT("GetDetails"));
		Inner.Children.Add(MakeNode(TEXT("Widget"), TEXT("Line")));
		FDreamUINode Entry = MakeNode(TEXT("Widget"), TEXT("Entry"));
		Entry.Children.Add(Inner);
		FDreamUINode Outer = MakeFor(TEXT("Option"), TEXT("GetOptions"));
		Outer.Children.Add(Entry);
		FDreamUINode Root = MakeNode(TEXT("Widget"), TEXT("Root"));
		Root.Children.Add(Outer);
		Cases.Add({ TEXT("a 'for' inside another 'for'"), AstWith(Root) });
	}
	{
		FDreamUINode Root = MakeNode(TEXT("Widget"), TEXT("Root"));
		Root.Children.Add(MakeFor(TEXT("Option"), TEXT("GetOptions")));
		Cases.Add({ TEXT("a 'for' with an empty body"), AstWith(Root) });
	}
	{
		FDreamUINode Loop = MakeFor(TEXT("Option"), TEXT("GetOptions"));
		Loop.Children.Add(MakeNode(TEXT("Widget"), TEXT("Label")));
		Loop.Children.Add(MakeNode(TEXT("Widget"), TEXT("Value")));
		FDreamUINode Root = MakeNode(TEXT("Widget"), TEXT("Root"));
		Root.Children.Add(Loop);
		Cases.Add({ TEXT("a 'for' with two widgets in its body"), AstWith(Root) });
	}
	{
		// A content widget holds one child, and a `for` adds one per item.
		FDreamUIComponent Single;
		Single.ClassName = UDreamContentWidget::StaticClass()->GetPathName();
		Single.Location = At();
		FDreamUINode Root = MakeNode(TEXT("Widget"), TEXT("Root"));
		Root.Components.Add(Single);
		FDreamUINode Loop = MakeFor(TEXT("Option"), TEXT("GetOptions"));
		Loop.Children.Add(MakeNode(TEXT("Widget"), TEXT("Entry")));
		Root.Children.Add(Loop);
		Cases.Add({ TEXT("a 'for' in a widget that holds one child"), AstWith(Root) });
	}

	for (const FCase& Case : Cases)
	{
		const FBuildOutcome Outcome = BuildWithLoops(Case.Ast);
		TestTrue(FString::Printf(TEXT("%s is refused under its own code"), Case.What),
			Outcome.HasCode(EDreamUIDiagnosticCode::ForMisplaced));
		TestNull(FString::Printf(TEXT("%s builds no tree"), Case.What), Outcome.Root());
		// Refused before anything was recorded, or taken back when the template failed: a half-made loop reaching
		// the class would be resolved at run time against a template that is not there.
		TestTrue(FString::Printf(TEXT("%s records no loop that could run"), Case.What),
			!Outcome.EachBindings.ContainsByPredicate([](const FDreamWidgetEachBinding& InBinding)
			{
				return InBinding.TemplateWidgetName.IsNone();
			}));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIForBodyBindingsTest,
	"DreamGUI.Text.Syntax.AForBodyTakesOneMemberOfTheItemAndRefusesAnythingRicher",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIForBodyBindingsTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIForLoopTestLocal;

	auto BuildBody = [](FDreamUIProperty InProperty)
	{
		FDreamUINode Entry = MakeNode(TEXT("Widget"), TEXT("Entry"));
		Entry.Properties.Add(MoveTemp(InProperty));
		FDreamUINode Loop = MakeFor(TEXT("Option"), TEXT("GetOptions"));
		Loop.Children.Add(Entry);
		FDreamUINode Root = MakeNode(TEXT("Widget"), TEXT("Root"));
		Root.Children.Add(Loop);
		return BuildWithLoops(AstWith(Root));
	};

	// An expression over the item: the thunk pass skips loop bodies (a generated function would ask the CLASS for a
	// name only the iteration has), so there is nothing it could compile to -- the same rule an `each` keeps.
	{
		FDreamUIExpression Item;
		Item.Kind = FDreamUIExpression::EKind::VariableRef;
		Item.Symbol = TEXT("Option.Amount");
		FDreamUIExpression Half;
		Half.Kind = FDreamUIExpression::EKind::Literal;
		Half.LiteralKind = EDreamUIValueKind::Number;
		Half.LiteralRaw = TEXT("0.5");
		FDreamUIExpression Product;
		Product.Kind = FDreamUIExpression::EKind::Binary;
		Product.Symbol = TEXT("*");
		Product.Operands = { Item, Half };

		FDreamUIProperty Property;
		Property.Name = TEXT("RenderOpacity");
		Property.BindingExpression = Product;
		Property.Location = At();
		const FBuildOutcome Outcome = BuildBody(Property);
		TestTrue(TEXT("an expression over the item is refused"), Outcome.HasCode(EDreamUIDiagnosticCode::LoopBodyBindingUnsupported));
	}

	// Two hops: the item's member's member. One hop is the whole contract.
	{
		const FBuildOutcome Outcome = BuildBody(BindRead(TEXT("ToolTipText"), TEXT("Option.Detail.Label")));
		TestTrue(TEXT("a read two members deep is refused"), Outcome.HasCode(EDreamUIDiagnosticCode::BindingExpressionUnsupported));
	}

	// And the one shape that is taken, so the two refusals above are not a builder refusing everything.
	{
		const FBuildOutcome Outcome = BuildBody(BindRead(TEXT("ToolTipText"), TEXT("Option.Label")));
		TestFalse(TEXT("one member of the item is taken"), Outcome.Diagnostics.HasErrors());
		TestTrue(TEXT("as an item write"), Outcome.EachBindings.Num() == 1 && Outcome.EachBindings[0].EntryBindings.Num() == 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIForSetterlessTest,
	"DreamGUI.Text.Syntax.AForMayWriteAComponentPropertyWithNoSetterButNotABehaviours",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIForSetterlessTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIForLoopTestLocal;

	// The design's own example: `Row { Label <- Option.Label }`, where Label is the component's `props` variable --
	// a Blueprint variable, with no SetLabel anywhere. Caption on the fixture row is the same shape.
	{
		FDreamUINode Row = MakeNode(UDreamForLoopTestRow::StaticClass()->GetPathName(), TEXT("Entry"));
		Row.Properties.Add(BindRead(TEXT("Caption"), TEXT("Option.Label")));
		FDreamUINode Loop = MakeFor(TEXT("Option"), TEXT("GetOptions"));
		Loop.Children.Add(Row);
		FDreamUINode Root = MakeNode(TEXT("Widget"), TEXT("Root"));
		Root.Children.Add(Loop);
		const FBuildOutcome Outcome = BuildWithLoops(AstWith(Root));
		TestFalse(TEXT("a component's setter-less property is taken"), Outcome.Diagnostics.HasErrors());
		if (TestTrue(TEXT("as an item write"), Outcome.EachBindings.Num() == 1 && Outcome.EachBindings[0].EntryBindings.Num() == 1))
		{
			const FDreamWidgetEntryBinding& Entry = Outcome.EachBindings[0].EntryBindings[0];
			TestEqual(TEXT("on the property"), Entry.PropertyName, FName(TEXT("Caption")));
			// None is how the adapter knows to write it directly.
			TestTrue(TEXT("with no setter recorded"), Entry.SetterName.IsNone());
		}
	}

	// A behaviour's property with no setter stays unbindable: written raw, a behaviour repaints nothing and
	// re-runs nothing, so the copy would hold the value and never show it.
	{
		FDreamUIComponent Sweep;
		Sweep.ClassName = UDreamUISweepTestBehaviour::StaticClass()->GetPathName();
		Sweep.Location = At();
		FDreamUIProperty Plain = BindRead(TEXT("Plain"), TEXT("Option.Amount"));
		Sweep.Properties.Add(Plain);
		FDreamUINode Entry = MakeNode(TEXT("Widget"), TEXT("Entry"));
		Entry.Components.Add(Sweep);
		FDreamUINode Loop = MakeFor(TEXT("Option"), TEXT("GetOptions"));
		Loop.Children.Add(Entry);
		FDreamUINode Root = MakeNode(TEXT("Widget"), TEXT("Root"));
		Root.Children.Add(Loop);
		const FBuildOutcome Outcome = BuildWithLoops(AstWith(Root));
		TestTrue(TEXT("a behaviour's setter-less property is still refused"),
			Outcome.HasCode(EDreamUIDiagnosticCode::BindingTargetHasNoSetter));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIForWithoutASinkTest,
	"DreamGUI.Text.Syntax.AForOnlyWarnsWhenTheCallerOfferedNowhereToRecordIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIForWithoutASinkTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIForLoopTestLocal;

	// What an `each` does in the same place, for the same reason: a caller building a raw AST still gets the rest of
	// the file. A compile always offers the array, so this is never what a class is built from.
	FDreamUINode Root = MakeNode(TEXT("Widget"), TEXT("Root"));
	FDreamUINode Loop = MakeFor(TEXT("Option"), TEXT("GetOptions"));
	Loop.Children.Add(MakeNode(TEXT("Widget"), TEXT("Entry")));
	Root.Children.Add(Loop);
	Root.Children.Add(MakeNode(TEXT("Widget"), TEXT("Footer")));

	FBuildOutcome Outcome;
	TArray<FDreamWidgetPropertyBinding> Bindings;
	Outcome.Tree.Reset(FDreamUITextBuilder::Build(AstWith(Root), GetTransientPackage(), Outcome.Diagnostics, Bindings));
	if (!TestNotNull(TEXT("the tree still built"), Outcome.Root()))
	{
		return false;
	}
	TestFalse(TEXT("with no error"), Outcome.Diagnostics.HasErrors());
	if (TestEqual(TEXT("and one diagnostic"), Outcome.Diagnostics.Diagnostics.Num(), 1))
	{
		TestEqual(TEXT("which is a warning"), Outcome.Diagnostics.Diagnostics[0].Severity, EDreamUISeverity::Warning);
		TestEqual(TEXT("under the code that says why"), Outcome.Diagnostics.Diagnostics[0].Code, EDreamUIDiagnosticCode::LoopNotExpanded);
	}
	TestNull(TEXT("the body was skipped"), Outcome.Find(TEXT("Entry")));
	TestNotNull(TEXT("and its sibling built"), Outcome.Find(TEXT("Footer")));
	return true;
}

// ============================================================================================ run time

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIForCopiesInOrderTest,
	"DreamGUI.Binding.For.OneCopyPerItemStandsInItemOrderWhereTheTemplateIsWithItsItemWritten",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIForCopiesInOrderTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIForLoopTestLocal;
	FScopedGameWorld TestWorld;
	FForTree Tree(TestWorld.World);
	if (!TestTrue(TEXT("the fixture tree was assembled"), Tree.IsValidFixture()))
	{
		return false;
	}
	Tree.AddItem(TEXT("Alpha"), 0.25f);
	Tree.AddItem(TEXT("Beta"), 0.5f);
	Tree.AddItem(TEXT("Gamma"), 0.75f);

	UDreamUIForAdapter* Adapter = NewObject<UDreamUIForAdapter>(Tree.Owner);
	Adapter->Initialize(Tree.Owner, MakeForBinding(/*bInFunction*/true), Tree.Host, Tree.Template);

	const TArray<UDreamWidget*> Copies = Adapter->GetCopies();
	if (!TestEqual(TEXT("one copy per item"), Copies.Num(), 3))
	{
		Tree.Owner->DestroyWidget();
		return false;
	}
	// Between Header and Footer, right after the template: where the `for` was written, not merely at the end.
	TestTrue(TEXT("the copies stand in item order right after the template"), Tree.Children() == ExpectedOrder(Tree, Copies));

	const TCHAR* Labels[] = { TEXT("Alpha"), TEXT("Beta"), TEXT("Gamma") };
	const float Amounts[] = { 0.25f, 0.5f, 0.75f };
	for (int32 Index = 0; Index < Copies.Num(); ++Index)
	{
		UDreamWidget* Copy = Copies[Index];
		TestNotEqual(FString::Printf(TEXT("copy %d is a widget of its own"), Index), Copy, Tree.Template);
		TestEqual(FString::Printf(TEXT("copy %d has its item's label on its root"), Index), TooltipOf(Copy), FString(Labels[Index]));
		const UDreamWidget* Inner = Copy->FindChildByDisplayName(TEXT("Inner"), false);
		if (TestNotNull(FString::Printf(TEXT("copy %d has its own Inner"), Index), Inner))
		{
			TestEqual(FString::Printf(TEXT("copy %d has its item's amount one level down"), Index), Inner->GetRenderOpacity(), Amounts[Index]);
		}
		// The template's own name, which is how the item writes find their widgets inside a copy.
		TestEqual(FString::Printf(TEXT("copy %d keeps the template's name"), Index), Copy->GetDisplayName(), FString(TEXT("Entry")));
		TestEqual(FString::Printf(TEXT("copy %d is shown"), Index), Copy->GetVisibility(), EDreamWidgetVisibility::Visible);
		TestTrue(FString::Printf(TEXT("copy %d is drawn"), Index), Copy->GetRenderVisibleInHierarchy());
		// Registered as it joined the tree, which is the moment a selectable inside it enters navigation.
		TestTrue(FString::Printf(TEXT("copy %d is registered"), Index), Copy->HasRegistered());
	}

	// The template: still there, the class's authored copy, and out of everything a player could reach.
	TestEqual(TEXT("the template is collapsed"), Tree.Template->GetVisibility(), EDreamWidgetVisibility::Collapsed);
	TestFalse(TEXT("so it takes no layout space"), Tree.Template->GetLayoutVisibleInHierarchy());
	// Both navigation searches -- the selectables' and UDreamWidgetNavigation's -- ask this before anything else.
	TestFalse(TEXT("and is neither drawn nor navigable"), Tree.Template->GetRenderVisibleInHierarchy());
	TestEqual(TEXT("and no item was written onto it"), TooltipOf(Tree.Template), FString(TEXT("Authored")));

	Tree.Owner->DestroyWidget();
	for (int32 Index = 0; Index < Copies.Num(); ++Index)
	{
		// In the owner's tree, so they go down with it: nothing is left standing for nobody to remove.
		TestFalse(FString::Printf(TEXT("copy %d went down with its owner"), Index), IsValid(Copies[Index]));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIForRefreshTest,
	"DreamGUI.Binding.For.ARefreshFollowsTheListAndKeepsTheCopiesOfItemsThatStayed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIForRefreshTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIForLoopTestLocal;
	FScopedGameWorld TestWorld;
	FForTree Tree(TestWorld.World);
	if (!TestTrue(TEXT("the fixture tree was assembled"), Tree.IsValidFixture()))
	{
		return false;
	}
	UDreamForLoopTestItem* A = Tree.AddItem(TEXT("A"), 0.1f);
	UDreamForLoopTestItem* B = Tree.AddItem(TEXT("B"), 0.2f);
	UDreamForLoopTestItem* C = Tree.AddItem(TEXT("C"), 0.3f);

	// The variable form this time: the same adapter, reading Options instead of calling GetOptions.
	UDreamUIForAdapter* Adapter = NewObject<UDreamUIForAdapter>(Tree.Owner);
	Adapter->Initialize(Tree.Owner, MakeForBinding(/*bInFunction*/false), Tree.Host, Tree.Template);
	const TArray<UDreamWidget*> First = Adapter->GetCopies();
	if (!TestEqual(TEXT("a copy per item from the variable"), First.Num(), 3))
	{
		Tree.Owner->DestroyWidget();
		return false;
	}
	UDreamWidget* CopyOfA = First[0];
	UDreamWidget* CopyOfB = First[1];
	UDreamWidget* CopyOfC = First[2];

	// Reordered: the same three copies, moved -- a reorder is not a rebuild.
	Tree.Owner->Options = { C, A, B };
	Adapter->Refresh();
	TArray<UDreamWidget*> Now = Adapter->GetCopies();
	TestTrue(TEXT("a reorder moves the copies the items already had"), Now == TArray<UDreamWidget*>({ CopyOfC, CopyOfA, CopyOfB }));
	TestTrue(TEXT("and stands them in the new order"), Tree.Children() == ExpectedOrder(Tree, Now));

	// Fewer: the copy of the item that left goes, the others stay where they are.
	Tree.Owner->Options = { C, B };
	Adapter->Refresh();
	Now = Adapter->GetCopies();
	TestTrue(TEXT("fewer items keep the copies of the ones that stayed"), Now == TArray<UDreamWidget*>({ CopyOfC, CopyOfB }));
	TestFalse(TEXT("and destroy the copy of the one that left"), IsValid(CopyOfA));
	TestTrue(TEXT("leaving no gap"), Tree.Children() == ExpectedOrder(Tree, Now));

	// More: a new copy for the new item, in its place, between two that were kept.
	UDreamForLoopTestItem* D = NewObject<UDreamForLoopTestItem>(Tree.Owner);
	D->Label = FText::FromString(TEXT("D"));
	D->Amount = 0.4f;
	Tree.Owner->Options = { C, D, B };
	Adapter->Refresh();
	Now = Adapter->GetCopies();
	if (TestEqual(TEXT("more items make more copies"), Now.Num(), 3))
	{
		TestEqual(TEXT("the first kept"), Now[0], CopyOfC);
		TestTrue(TEXT("a new one for the new item"), Now[1] != CopyOfA && Now[1] != CopyOfB && Now[1] != CopyOfC);
		TestEqual(TEXT("showing it"), TooltipOf(Now[1]), FString(TEXT("D")));
		TestEqual(TEXT("the last kept"), Now[2], CopyOfB);
		TestTrue(TEXT("all standing in order"), Tree.Children() == ExpectedOrder(Tree, Now));
	}

	// The same objects in the same order, one of them changed in place: nothing made or moved, the change shown.
	B->Label = FText::FromString(TEXT("B again"));
	const TArray<UDreamWidget*> BeforeSameRefresh = Adapter->GetCopies();
	Adapter->Refresh();
	TestTrue(TEXT("an unchanged list keeps every copy"), Adapter->GetCopies() == BeforeSameRefresh);
	TestEqual(TEXT("and writes the items again"), TooltipOf(CopyOfB), FString(TEXT("B again")));

	// Empty: every copy goes, the template stays collapsed.
	Tree.Owner->Options.Reset();
	Adapter->Refresh();
	TestEqual(TEXT("an empty list leaves no copies"), Adapter->GetCopies().Num(), 0);
	TestTrue(TEXT("and only the authored children"), Tree.Children() == ExpectedOrder(Tree, {}));
	TestEqual(TEXT("with the template still collapsed"), Tree.Template->GetVisibility(), EDreamWidgetVisibility::Collapsed);

	Tree.Owner->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIForNestedUserWidgetTest,
	"DreamGUI.Binding.For.ACopyOfAUserWidgetGetsItsOwnTreeAndTakesAPropertyWithNoSetter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIForNestedUserWidgetTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIForLoopTestLocal;
	FScopedGameWorld TestWorld;

	// A row component with contents of its own, the way an instanced class has them: a tree instanced from an
	// archetype, so a copy of it has a tree to be handed and a class's worth of initialization to go through.
	UDreamWidgetTree* RowArchetype = NewObject<UDreamWidgetTree>(GetTransientPackage());
	UDreamWidget* Face = RowArchetype->ConstructWidget<UDreamWidget>();
	Face->SetDisplayName(TEXT("Face"));
	RowArchetype->RootWidget = Face;
	UDreamForLoopTestRow* RowTemplate = NewObject<UDreamForLoopTestRow>(TestWorld.World, NAME_None, RF_Public | RF_Transactional);
	RowTemplate->InitializeFromArchetype(RowArchetype);
	RowTemplate->Caption = FText::FromString(TEXT("Authored"));

	FForTree Tree(TestWorld.World, RowTemplate);
	if (!TestTrue(TEXT("the fixture tree was assembled"), Tree.IsValidFixture())
		|| !TestTrue(TEXT("the template row has contents of its own"), RowTemplate->GetContentRoot() != nullptr))
	{
		return false;
	}
	Tree.AddItem(TEXT("First"), 1.0f);
	Tree.AddItem(TEXT("Second"), 1.0f);

	// What the builder records for `Row { Caption <- Option.Label }`: no setter, so the adapter writes the property.
	FDreamWidgetEachBinding Binding;
	Binding.bInPanel = true;
	Binding.HostWidgetName = TEXT("Host");
	Binding.TemplateWidgetName = TEXT("Entry");
	Binding.SourceName = TEXT("GetOptions");
	Binding.bSourceIsFunction = true;
	Binding.LoopVariable = TEXT("Option");
	FDreamWidgetEntryBinding& CaptionEntry = Binding.EntryBindings.AddDefaulted_GetRef();
	CaptionEntry.TargetWidgetDisplayName = TEXT("Entry");
	CaptionEntry.Target = EDreamWidgetBindingTarget::Widget;
	CaptionEntry.PropertyName = TEXT("Caption");
	CaptionEntry.ItemMember = TEXT("Label");

	UDreamUIForAdapter* Adapter = NewObject<UDreamUIForAdapter>(Tree.Owner);
	Adapter->Initialize(Tree.Owner, Binding, Tree.Host, Tree.Template);
	const TArray<UDreamWidget*> Copies = Adapter->GetCopies();
	if (!TestEqual(TEXT("a copy per item"), Copies.Num(), 2))
	{
		Tree.Owner->DestroyWidget();
		return false;
	}

	const TCHAR* Labels[] = { TEXT("First"), TEXT("Second") };
	for (int32 Index = 0; Index < Copies.Num(); ++Index)
	{
		const UDreamForLoopTestRow* Row = Cast<UDreamForLoopTestRow>(Copies[Index]);
		if (!TestNotNull(FString::Printf(TEXT("copy %d is a row"), Index), Row))
		{
			continue;
		}
		// Initialized as a copy is: its own tree, its own initialization, and so its own bindings resolved.
		TestTrue(FString::Printf(TEXT("copy %d was initialized"), Index), Row->IsInitialized());
		TestEqual(FString::Printf(TEXT("copy %d ran its own initialization once"), Index), Row->InitializedCount, 1);
		TestTrue(FString::Printf(TEXT("copy %d has a tree"), Index), Row->GetWidgetTree() != nullptr);
		TestTrue(FString::Printf(TEXT("copy %d has a tree of its own, not the template's"), Index),
			Row->GetWidgetTree() != RowTemplate->GetWidgetTree());
		const UDreamWidget* ContentRoot = Row->GetContentRoot();
		if (TestNotNull(FString::Printf(TEXT("copy %d has contents"), Index), ContentRoot))
		{
			TestTrue(FString::Printf(TEXT("copy %d's contents hang under it"), Index), ContentRoot->GetParent() == Row);
			TestTrue(FString::Printf(TEXT("copy %d's contents are not the template's"), Index), ContentRoot != RowTemplate->GetContentRoot());
		}
		// No SetCaption exists; the item reached the property anyway.
		TestEqual(FString::Printf(TEXT("copy %d holds its item's label"), Index), Row->Caption.ToString(), FString(Labels[Index]));
	}
	TestEqual(TEXT("the template row was not written"), RowTemplate->Caption.ToString(), FString(TEXT("Authored")));
	TestEqual(TEXT("and is collapsed"), RowTemplate->GetVisibility(), EDreamWidgetVisibility::Collapsed);

	Tree.Owner->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIForAdoptAndReleaseTest,
	"DreamGUI.Binding.For.CopiesBesideTheTemplateAreAdoptedAndNoneOutliveTheirTemplate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIForAdoptAndReleaseTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIForLoopTestLocal;
	FScopedGameWorld TestWorld;
	FForTree Tree(TestWorld.World);
	if (!TestTrue(TEXT("the fixture tree was assembled"), Tree.IsValidFixture()))
	{
		return false;
	}
	Tree.AddItem(TEXT("One"), 1.0f);
	Tree.AddItem(TEXT("Two"), 1.0f);

	UDreamUIForAdapter* First = NewObject<UDreamUIForAdapter>(Tree.Owner);
	First->Initialize(Tree.Owner, MakeForBinding(/*bInFunction*/true), Tree.Host, Tree.Template);
	const TArray<UDreamWidget*> Made = First->GetCopies();
	if (!TestEqual(TEXT("the first adapter made a copy per item"), Made.Num(), 2))
	{
		Tree.Owner->DestroyWidget();
		return false;
	}

	// What a duplicated owner's new adapter finds: the template with copies already beside it, which the source's
	// adapter made and duplication brought along. Taking them over, not adding to them.
	UDreamUIForAdapter* Second = NewObject<UDreamUIForAdapter>(Tree.Owner);
	Second->Initialize(Tree.Owner, MakeForBinding(/*bInFunction*/true), Tree.Host, Tree.Template);
	TestTrue(TEXT("copies already standing are adopted, not doubled"), Second->GetCopies() == Made);
	TestEqual(TEXT("so the panel holds one copy per item"), Tree.Children().Num(), 2 + 3);

	// Released: every copy goes, the authored children stay.
	Second->ReleaseCopies();
	TestFalse(TEXT("a released copy is destroyed"), Made.ContainsByPredicate([](const UDreamWidget* InCopy) { return IsValid(InCopy); }));
	TestTrue(TEXT("leaving the authored children"), Tree.Children() == ExpectedOrder(Tree, {}));

	// And a refresh brings them back.
	Second->Refresh();
	TestEqual(TEXT("a refresh after a release makes them again"), Second->GetCopies().Num(), 2);

	// A template taken out of the tree takes its copies with it: they are copies of nothing now, and no refresh
	// could reach them again.
	const TArray<UDreamWidget*> Remade = Second->GetCopies();
	Tree.Template->DestroyWidget();
	Second->Refresh();
	TestEqual(TEXT("the copies of a destroyed template are let go"), Second->GetCopies().Num(), 0);
	TestFalse(TEXT("and destroyed"), Remade.ContainsByPredicate([](const UDreamWidget* InCopy) { return IsValid(InCopy); }));
	TestEqual(TEXT("leaving Header and Footer"), Tree.Children().Num(), 2);

	Tree.Owner->DestroyWidget();
	return true;
}

#endif
