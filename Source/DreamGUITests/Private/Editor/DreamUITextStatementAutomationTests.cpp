// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "DreamUITextStructureTestFixtures.h"
#include "DreamWidgetBlueprintTestTypes.h"

#include "DetailCustomization/DreamWidgetEventCustomization.h"
#include "Designer/DreamWidgetPropertyBindingExtension.h"
#include "Designer/DreamUITextAuthoringGate.h"
#include "Text/DreamUIDiagnostics.h"
#include "Text/DreamUITextPatcher.h"

#include "Core/Components/DreamVisual.h"
#include "Core/DreamUIBehaviour.h"
#include "Interaction/UIButton.h"
#include "K2Node_CustomEvent.h"
#include "ScopedTransaction.h"
#include "UObject/UnrealType.h"

/*
 * Statements: a binding (`<-`), a route (`->`), a two-way binding (`<->`), and a value written in place of a binding.
 * The patcher writes, rewrites and takes out each as one line; the designer's Bind control, its Events rows and
 * "unbind and set" write that line into the `.dui`, and the compile reads it back.
 */

namespace DreamUITextStatementTestLocal
{
	using namespace DreamUITextStructureFixtures;

	FString Lines(const TArray<FString>& InLines)
	{
		return FString::Join(InLines, TEXT("\n"));
	}

	bool HasCode(const FDreamUIDiagnosticBag& InDiagnostics, EDreamUIDiagnosticCode InCode)
	{
		return InDiagnostics.Diagnostics.ContainsByPredicate([InCode](const FDreamUIDiagnostic& InDiagnostic) { return InDiagnostic.Code == InCode; });
	}

	/** One edit against InSource, as the flush would apply it. */
	bool Apply(const FString& InSource, const FDreamUIStructuralEdit& InEdit, FString& OutText, FDreamUIDiagnosticBag& OutDiagnostics)
	{
		FDreamUIAst Ast;
		OutText = InSource;
		return ParseText(InSource, Ast) && FDreamUITextPatcher::ApplyStructuralEdits(OutText, Ast, MakeArrayView(&InEdit, 1), OutDiagnostics);
	}

	FDreamUIStructuralEdit Statement(const TCHAR* InNodeId, const TCHAR* InProperty, const TCHAR* InStatement,
		EDreamUIPatchTarget InTarget = EDreamUIPatchTarget::Node, int32 InComponentIndex = INDEX_NONE, const TCHAR* InMatch = TEXT(""))
	{
		FDreamUIStructuralEdit Edit;
		Edit.Kind = EDreamUIStructuralEditKind::SetStatement;
		Edit.NodeId = InNodeId;
		Edit.PropertyName = InProperty;
		Edit.Statement = InStatement;
		Edit.PropertyTarget = InTarget;
		Edit.ComponentIndex = InComponentIndex;
		Edit.StatementMatch = InMatch;
		return Edit;
	}

	FDreamUIStructuralEdit Removal(const TCHAR* InNodeId, const TCHAR* InProperty,
		EDreamUIPatchTarget InTarget = EDreamUIPatchTarget::Node, int32 InComponentIndex = INDEX_NONE, const TCHAR* InMatch = TEXT(""))
	{
		FDreamUIStructuralEdit Edit = Statement(InNodeId, InProperty, TEXT(""), InTarget, InComponentIndex, InMatch);
		Edit.Kind = EDreamUIStructuralEditKind::RemoveProperty;
		return Edit;
	}

	const FDreamUIProperty* FindLine(const FDreamUIAst& InAst, const TCHAR* InNodeId, const TCHAR* InProperty, int32 InComponentIndex = INDEX_NONE)
	{
		const FDreamUINode* Node = FindNode(InAst, InNodeId);
		if (Node == nullptr)
		{
			return nullptr;
		}
		const TArray<FDreamUIProperty>& Properties = InComponentIndex == INDEX_NONE ? Node->Properties
			: Node->Components.IsValidIndex(InComponentIndex) ? Node->Components[InComponentIndex].Properties : Node->Properties;
		return Properties.FindByPredicate([InProperty](const FDreamUIProperty& P) { return P.Name == InProperty; });
	}

	/** The site the details panel would resolve for one of the template's objects. */
	DreamWidgetPropertyBindingExtension::FBindingSite SiteOf(UObject* InTemplateObject)
	{
		return DreamWidgetPropertyBindingExtension::ResolveBindingSite(InTemplateObject);
	}

	bool Compile(UDreamWidgetBlueprint* InBlueprint, FString& OutErrors)
	{
		FCompilerResultsLog Results;
		FKismetEditorUtilities::CompileBlueprint(InBlueprint, EBlueprintCompileOptions::SkipGarbageCollection, &Results);
		TArray<FString> Errors;
		for (const TSharedRef<FTokenizedMessage>& Message : Results.Messages)
		{
			if (Message->GetSeverity() == EMessageSeverity::Error)
			{
				Errors.Add(Message->ToText().ToString());
			}
		}
		OutErrors = FString::Join(Errors, TEXT(" | "));
		return Results.NumErrors == 0;
	}
}

// =================================================================================================
// The patcher
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIStatementPatcherTest,
	"DreamGUI.Text.Patcher.AStatementLineIsWrittenRewrittenAndTakenOut",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIStatementPatcherTest::RunTest(const FString&)
{
	using namespace DreamUITextStatementTestLocal;

	const FString Source = Lines({
		TEXT("Widget Root {"),
		TEXT("    + VerticalBox { }"),
		TEXT("    Text Title {"),
		TEXT("        Text = \"Hello\""),
		TEXT("        FontSize = 20"),
		TEXT("    }"),
		TEXT("    Widget Play {"),
		TEXT("        + UIButton { OnClick -> HandlePlay }"),
		TEXT("    }"),
		TEXT("    Native.Slider Volume {"),
		TEXT("        Value <- GetVolume()   // bound"),
		TEXT("    }"),
		TEXT("    if HasSave() {"),
		TEXT("        Text Continue { }"),
		TEXT("    }"),
		TEXT("}")
	});

	{
		FString Text;
		FDreamUIDiagnosticBag Diagnostics;
		TestTrue(TEXT("a value line becomes a binding"), Apply(Source, Statement(TEXT("Title"), TEXT("Text"), TEXT("<- GetTitle()")), Text, Diagnostics));
		TestTrue(TEXT("rewritten where it stood"), Text.Contains(TEXT("        Text <- GetTitle()\n        FontSize = 20")));
		TestFalse(TEXT("the literal is gone"), Text.Contains(TEXT("\"Hello\"")));
	}
	{
		FString Text;
		FDreamUIDiagnosticBag Diagnostics;
		TestTrue(TEXT("a binding becomes a value"), Apply(Source, Statement(TEXT("Volume"), TEXT("Value"), TEXT("= 0.5")), Text, Diagnostics));
		TestTrue(TEXT("and the comment after it stays"), Text.Contains(TEXT("        Value = 0.5   // bound")));
	}
	{
		FString Text;
		FDreamUIDiagnosticBag Diagnostics;
		TestTrue(TEXT("a two-way binding replaces a one-way one"), Apply(Source, Statement(TEXT("Volume"), TEXT("Value"), TEXT("<-> Settings.Volume")), Text, Diagnostics));
		FDreamUIAst Ast;
		if (TestTrue(TEXT("and reads back"), ParseText(Text, Ast)))
		{
			const FDreamUIProperty* Value = FindLine(Ast, TEXT("Volume"), TEXT("Value"));
			TestTrue(TEXT("as the mirror of that path"), Value != nullptr && Value->TwoWayProperty == TEXT("Settings.Volume"));
		}
	}
	{
		FString Text;
		FDreamUIDiagnosticBag Diagnostics;
		TestTrue(TEXT("a binding the node does not have is written as a new line"),
			Apply(Source, Statement(TEXT("Title"), TEXT("Shown"), TEXT("<- IsReady()")), Text, Diagnostics));
		TestTrue(TEXT("after the node's last line"), Text.Contains(TEXT("        FontSize = 20\n        Shown <- IsReady()\n    }")));
	}
	{
		FString Text;
		FDreamUIDiagnosticBag Diagnostics;
		TestTrue(TEXT("a route is written into a one-line '+' block"),
			Apply(Source, Statement(TEXT("Play"), TEXT("OnHoveredBP"), TEXT("-> HandleHover"), EDreamUIPatchTarget::Component, 0), Text, Diagnostics));
		FDreamUIAst Ast;
		if (TestTrue(TEXT("which still parses"), ParseText(Text, Ast)))
		{
			const FDreamUIProperty* Route = FindLine(Ast, TEXT("Play"), TEXT("OnHoveredBP"), 0);
			TestTrue(TEXT("with the new route in the block"), Route != nullptr && Route->EventHandler == TEXT("HandleHover"));
			TestNotNull(TEXT("beside the old one"), FindLine(Ast, TEXT("Play"), TEXT("OnClick"), 0));
		}
	}
	{
		FString Text;
		FDreamUIDiagnosticBag Diagnostics;
		TestTrue(TEXT("one handler is swapped for another"),
			Apply(Source, Statement(TEXT("Play"), TEXT("OnClick"), TEXT("-> HandlePlayAgain"), EDreamUIPatchTarget::Component, 0, TEXT("HandlePlay")), Text, Diagnostics));
		TestTrue(TEXT("inside the one-line block"), Text.Contains(TEXT("+ UIButton { OnClick -> HandlePlayAgain }")));
	}
	{
		FString Text;
		FDreamUIDiagnosticBag Diagnostics;
		TestTrue(TEXT("a route sharing its line is taken out"),
			Apply(Source, Removal(TEXT("Play"), TEXT("OnClick"), EDreamUIPatchTarget::Component, 0, TEXT("HandlePlay")), Text, Diagnostics));
		TestTrue(TEXT("and the block stays"), Text.Contains(TEXT("        + UIButton { }\n")));
	}
	{
		FString Text;
		FDreamUIDiagnosticBag Diagnostics;
		TestTrue(TEXT("a binding on its own line is taken out"), Apply(Source, Removal(TEXT("Volume"), TEXT("Value")), Text, Diagnostics));
		TestTrue(TEXT("line and comment"), Text.Contains(TEXT("    Native.Slider Volume {\n    }")));
	}
	{
		FString Text;
		FDreamUIDiagnosticBag Diagnostics;
		TestTrue(TEXT("a statement the file already says changes nothing"),
			Apply(Source, Statement(TEXT("Play"), TEXT("OnClick"), TEXT("-> HandlePlay"), EDreamUIPatchTarget::Component, 0), Text, Diagnostics));
		TestEqual(TEXT("byte for byte"), Text, Source);
	}

	// Refusals, each leaving the text as it was.
	{
		FString Text;
		FDreamUIDiagnosticBag Diagnostics;
		TestFalse(TEXT("a statement that does not parse is refused"), Apply(Source, Statement(TEXT("Title"), TEXT("Text"), TEXT("<- (")), Text, Diagnostics));
		TestTrue(TEXT("by the parser's reading"), HasCode(Diagnostics, EDreamUIDiagnosticCode::UnexpectedToken));
		TestEqual(TEXT("the text untouched"), Text, Source);
	}
	{
		FString Text;
		FDreamUIDiagnosticBag Diagnostics;
		TestFalse(TEXT("the Shown an 'if' makes is refused"), Apply(Source, Statement(TEXT("Continue"), TEXT("Shown"), TEXT("<- Other()")), Text, Diagnostics));
		TestTrue(TEXT("as a line nobody wrote"), HasCode(Diagnostics, EDreamUIDiagnosticCode::PatchSyntaxNotWritable));
	}
	{
		FString Text;
		FDreamUIDiagnosticBag Diagnostics;
		TestFalse(TEXT("a statement on a slot is refused"),
			Apply(Source, Statement(TEXT("Title"), TEXT("Padding"), TEXT("<- GetPadding()"), EDreamUIPatchTarget::Slot), Text, Diagnostics));
	}
	{
		FString Text;
		FDreamUIDiagnosticBag Diagnostics;
		TestFalse(TEXT("a route naming a handler no line names, among several, is refused"),
			Apply(Lines({ TEXT("Widget Root {"), TEXT("    OnA -> First"), TEXT("    OnA += Second"), TEXT("}") }),
				Removal(TEXT("Root"), TEXT("OnA"), EDreamUIPatchTarget::Node, INDEX_NONE, TEXT("Third")), Text, Diagnostics));
	}
	return true;
}

// =================================================================================================
// The designer: bindings, values, two-way bindings and routes, written into the file and read back
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIStatementDesignerTest,
	"DreamGUI.Designer.Text.BindingsAndRoutesAreWrittenIntoTheFileAndCompileBack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIStatementDesignerTest::RunTest(const FString&)
{
	using namespace DreamUITextStatementTestLocal;
	using namespace DreamWidgetPropertyBindingExtension;

	FScopedDuiFile Source(TEXT("StatementDesigner.dui"));
	if (!TestTrue(TEXT("the fixture wrote a .dui"), Source.Write({
		TEXT("props {"),
		TEXT("    Number Level = 0.5"),
		TEXT("}"),
		TEXT("viewmodels {"),
		TEXT("    DreamTestPlayerVM Player"),
		TEXT("}"),
		TEXT("Widget Root {"),
		TEXT("    + VerticalBox { }"),
		TEXT("    Text Title {"),
		TEXT("        Text = \"Hello\""),
		TEXT("    }"),
		TEXT("    Native.Slider Volume { }"),
		TEXT("    Native.Slider Amount { }"),
		TEXT("    Widget Play {"),
		TEXT("        + UIButton { }"),
		TEXT("    }"),
		TEXT("}")
	})))
	{
		return false;
	}
	// No designer: a script or a commandlet writes the same lines a click does.
	FScopedTextBlueprint Scoped(TEXT("BP_StatementDesigner"), Source.FilePath, /*bInOpenDesigner*/false,
		UDreamTextUserWidgetBindingBase::StaticClass());
	if (!TestNotNull(TEXT("the Blueprint was created"), Scoped.Blueprint)) return false;
	if (!TestEqual(TEXT("and its .dui compiled clean"), Scoped.CompileErrors, 0)) return false;
	UDreamWidgetBlueprint* Blueprint = Scoped.Blueprint;
	FString Errors;

	// ---- `<-`: bind, compile, read back
	UDreamWidget* Title = Scoped.FindTemplate(TEXT("Title"));
	if (!TestTrue(TEXT("the text has a visual"), Title != nullptr && Title->GetVisual() != nullptr)) return false;
	const FBindingSite TitleSite = SiteOf(Title->GetVisual());
	TestTrue(TEXT("a binding is written"), SetBinding(Blueprint, TitleSite, TEXT("Text"), TEXT("GetTitleText")));
	TestTrue(TEXT("as the line in the file, in place of the literal"), Source.Read().Contains(TEXT("        Text <- GetTitleText()")));
	TestFalse(TEXT("the literal is gone"), Source.Read().Contains(TEXT("\"Hello\"")));
	TestTrue(*FString::Printf(TEXT("the class compiles (%s)"), *Errors), Compile(Blueprint, Errors));
	const FDreamWidgetPropertyBinding* Bound = FindBinding(Blueprint, TitleSite, TEXT("Text"));
	TestTrue(TEXT("and the compile read the binding back from the file"), Bound != nullptr && Bound->FunctionName == FName(TEXT("GetTitleText")));

	// ---- unbind and set a value
	TestTrue(TEXT("the binding is turned into a value"), UnbindAndSetValue(Blueprint, TitleSite, TEXT("Text"), TEXT("\"Bye\"")));
	TestTrue(TEXT("as one line"), Source.Read().Contains(TEXT("        Text = \"Bye\"")));
	TestFalse(TEXT("the binding is gone from the file"), Source.Read().Contains(TEXT("<- GetTitleText")));
	TestTrue(*FString::Printf(TEXT("the class compiles (%s)"), *Errors), Compile(Blueprint, Errors));
	TestNull(TEXT("and from the class"), FindBinding(Blueprint, TitleSite, TEXT("Text")));

	// ---- unbind, keeping the value the object holds now
	TestTrue(TEXT("bound again"), SetBinding(Blueprint, TitleSite, TEXT("Text"), TEXT("GetTitleText")));
	Title = Scoped.FindTemplate(TEXT("Title"));
	UDreamVisual* Visual = Title != nullptr ? Title->GetVisual() : nullptr;
	FTextProperty* TextProperty = Visual != nullptr ? CastField<FTextProperty>(Visual->GetClass()->FindPropertyByName(TEXT("Text"))) : nullptr;
	if (TestNotNull(TEXT("the visual has its Text"), TextProperty))
	{
		TextProperty->SetPropertyValue_InContainer(Visual, FText::FromString(TEXT("Kept")));
		TestTrue(TEXT("unbound with the value it shows"), UnbindAndKeepValue(Blueprint, TitleSite, TextProperty, Visual));
		if (!TestTrue(TEXT("written as that value"), Source.Read().Contains(TEXT("        Text = \"Kept\""))))
		{
			AddInfo(TEXT("after unbind-and-keep:\n") + Source.Read());
		}
	}

	// ---- remove a binding outright
	TestTrue(TEXT("bound once more"), SetBinding(Blueprint, TitleSite, TEXT("Text"), TEXT("GetTitleText")));
	TestTrue(TEXT("and unbound"), RemoveBinding(Blueprint, TitleSite, TEXT("Text")));
	TestFalse(TEXT("the line is gone"), Source.Read().Contains(TEXT("GetTitleText")));
	TestTrue(*FString::Printf(TEXT("the class compiles (%s)"), *Errors), Compile(Blueprint, Errors));
	TestNull(TEXT("and the class has no binding"), FindBinding(Blueprint, TitleSite, TEXT("Text")));

	// ---- `<->`: offered variables, written, compiled, read back
	UDreamWidget* Volume = Scoped.FindTemplate(TEXT("Volume"));
	if (!TestNotNull(TEXT("the slider is in the tree"), Volume)) return false;
	const FProperty* ValueProperty = Volume->GetClass()->FindPropertyByName(TEXT("Value"));
	if (!TestNotNull(TEXT("with a Value"), ValueProperty)) return false;
	const TArray<FString> Variables = CollectTwoWayVariables(Blueprint, Volume->GetClass(), ValueProperty);
	const FString Offered = FString::Join(Variables, TEXT(", "));
	TestTrue(*FString::Printf(TEXT("the view model's float members are offered (%s)"), *Offered),
		Variables.Contains(TEXT("Player.Volume")) && Variables.Contains(TEXT("Player.Brightness")));
	// A `Number` is a double, and the slider's changed event hands its listener a float: the setter could not take it.
	TestFalse(*FString::Printf(TEXT("the file's own Number is not (%s)"), *Offered), Variables.Contains(TEXT("Level")));
	const FBindingSite VolumeSite = SiteOf(Volume);
	TestTrue(TEXT("a two-way binding is written"), SetTwoWayBinding(Blueprint, VolumeSite, TEXT("Value"), TEXT("Player.Volume")));
	TestTrue(*FString::Printf(TEXT("the class compiles with it (%s)"), *Errors), Compile(Blueprint, Errors));
	{
		FDreamUIAst Ast;
		const FDreamUIProperty* Value = ParseText(Source.Read(), Ast) ? FindLine(Ast, TEXT("Volume"), TEXT("Value")) : nullptr;
		TestTrue(TEXT("the file reads it back as that mirror"), Value != nullptr && Value->TwoWayProperty == TEXT("Player.Volume"));
	}
	const FDreamWidgetPropertyBinding* Mirror = FindBinding(Blueprint, VolumeSite, TEXT("Value"));
	TestTrue(TEXT("and the class holds it as a two-way binding"), Mirror != nullptr && Mirror->FunctionName.ToString().StartsWith(TEXT("__DreamTwoWayGet_")));
	TestTrue(TEXT("swapped for another member"), SetTwoWayBinding(Blueprint, VolumeSite, TEXT("Value"), TEXT("Player.Brightness")));
	TestTrue(TEXT("on the same line"), Source.Read().Contains(TEXT("Value <-> Player.Brightness")) && !Source.Read().Contains(TEXT("Player.Volume")));
	TestTrue(*FString::Printf(TEXT("the class compiles (%s)"), *Errors), Compile(Blueprint, Errors));

	// ---- routes: on a widget's own event, and on a behaviour the node writes as a `+` line
	UK2Node_CustomEvent* SliderHandler = DreamWidgetEventRoutes::AddRouteWithHandler(Blueprint, TEXT("Amount"),
		EDreamWidgetBindingTarget::Widget, INDEX_NONE, TEXT("OnValueChangedBP"));
	if (TestNotNull(TEXT("a route and its handler are made for the slider's event"), SliderHandler))
	{
		const FString Handler = SliderHandler->CustomFunctionName.ToString();
		TestTrue(TEXT("the route is the node's line"), Source.Read().Contains(FString::Printf(TEXT("OnValueChangedBP -> %s"), *Handler)));
		TestTrue(*FString::Printf(TEXT("the class compiles with it (%s)"), *Errors), Compile(Blueprint, Errors));
		const FDreamWidgetEventBinding* Route = DreamWidgetEventRoutes::FindRoute(Blueprint, TEXT("Amount"),
			EDreamWidgetBindingTarget::Widget, INDEX_NONE, TEXT("OnValueChangedBP"));
		TestTrue(TEXT("and reads it back"), Route != nullptr && Route->FunctionName == FName(*Handler));
		TestNull(TEXT("a second click on the same event makes nothing"), DreamWidgetEventRoutes::AddRouteWithHandler(Blueprint, TEXT("Amount"),
			EDreamWidgetBindingTarget::Widget, INDEX_NONE, TEXT("OnValueChangedBP")));
	}
	// The behaviour's index on the template, as the panel reads it: a behaviour may pull in required ones ahead of it.
	UDreamWidget* Play = Scoped.FindTemplate(TEXT("Play"));
	const int32 ButtonIndex = Play != nullptr ? Play->GetAllComponents().IndexOfByPredicate(
		[](const UDreamUIBehaviour* InBehaviour) { return InBehaviour != nullptr && InBehaviour->IsA<UUIButton>(); }) : INDEX_NONE;
	if (!TestTrue(TEXT("the button behaviour is on the template"), ButtonIndex != INDEX_NONE)) return false;
	UK2Node_CustomEvent* ButtonHandler = DreamWidgetEventRoutes::AddRouteWithHandler(Blueprint, TEXT("Play"),
		EDreamWidgetBindingTarget::Behaviour, ButtonIndex, TEXT("OnClick"));
	if (TestNotNull(TEXT("a route is made for the behaviour's event"), ButtonHandler))
	{
		const FString Handler = ButtonHandler->CustomFunctionName.ToString();
		FDreamUIAst Ast;
		const FDreamUIProperty* Route = ParseText(Source.Read(), Ast) ? FindLine(Ast, TEXT("Play"), TEXT("OnClick"), 0) : nullptr;
		TestTrue(TEXT("inside the node's '+ UIButton' block"), Route != nullptr && Route->EventHandler == Handler);
		TestTrue(*FString::Printf(TEXT("the class compiles with it (%s)"), *Errors), Compile(Blueprint, Errors));
		TestNotNull(TEXT("and reads it back"),
			DreamWidgetEventRoutes::FindRoute(Blueprint, TEXT("Play"), EDreamWidgetBindingTarget::Behaviour, ButtonIndex, TEXT("OnClick")));

		TestTrue(TEXT("the route is removed"),
			DreamWidgetEventRoutes::RemoveRoute(Blueprint, TEXT("Play"), EDreamWidgetBindingTarget::Behaviour, ButtonIndex, TEXT("OnClick")));
		TestFalse(TEXT("its line is gone"), Source.Read().Contains(FString::Printf(TEXT("-> %s"), *Handler)));
		TestTrue(*FString::Printf(TEXT("the class compiles (%s)"), *Errors), Compile(Blueprint, Errors));
		TestNull(TEXT("and has no route"),
			DreamWidgetEventRoutes::FindRoute(Blueprint, TEXT("Play"), EDreamWidgetBindingTarget::Behaviour, ButtonIndex, TEXT("OnClick")));
	}
	if (HasAnyErrors())
	{
		AddInfo(TEXT("file:\n") + Source.Read());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIStatementUndoTest,
	"DreamGUI.Designer.Text.ABindingWrittenFromThePanelUndoesAndRedoesWithTheFile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIStatementUndoTest::RunTest(const FString&)
{
	using namespace DreamUITextStatementTestLocal;
	using namespace DreamWidgetPropertyBindingExtension;

	FScopedDuiFile Library(TEXT("StatementUndoLibrary.dui"));
	FScopedDuiFile Source(TEXT("StatementUndo.dui"));
	if (!TestTrue(TEXT("the library was written"), Library.Write({ TEXT("style Caption { FontSize = 12 }") }))) return false;
	// A file that brings in another: the line is written into this one, the `use` and the style left as they are.
	if (!TestTrue(TEXT("the fixture wrote a .dui"), Source.Write({
		FString::Printf(TEXT("use \"%s\" as lib"), *Library.FilePath),
		TEXT("Widget Root {"),
		TEXT("    + VerticalBox { }"),
		TEXT("    Text Title : lib.Caption {"),
		TEXT("        Text = \"Hello\""),
		TEXT("    }"),
		TEXT("}")
	})))
	{
		return false;
	}
	const FString Before = Source.Read();
	FScopedTextBlueprint Scoped(TEXT("BP_StatementUndo"), Source.FilePath, /*bInOpenDesigner*/true,
		UDreamTextUserWidgetBindingBase::StaticClass());
	if (!TestNotNull(TEXT("the Blueprint was created"), Scoped.Blueprint)) return false;
	if (!TestEqual(TEXT("and its .dui compiled clean"), Scoped.CompileErrors, 0)) return false;
	if (!TestNotNull(TEXT("the designer opened"), Scoped.Designer)) return false;

	UDreamWidget* Title = Scoped.FindTemplate(TEXT("Title"));
	if (!TestTrue(TEXT("the text has a visual"), Title != nullptr && Title->GetVisual() != nullptr)) return false;
	UDreamWidget* PreviewTitle = Scoped.FindPreview(TEXT("Title"));
	if (TestTrue(TEXT("the text has a preview"), PreviewTitle != nullptr && PreviewTitle->GetVisual() != nullptr))
	{
		TestTrue(TEXT("the panel offers the Bind control on it"),
			IsBindable(PreviewTitle->GetVisual(), PreviewTitle->GetVisual()->GetClass()->FindPropertyByName(TEXT("Text"))));
	}
	const FBindingSite Site = SiteOf(Title->GetVisual());
	{
		const FScopedTransaction Transaction(NSLOCTEXT("DreamGUITests", "StatementUndoBind", "Bind from the panel"));
		TestTrue(TEXT("a binding is written with the designer open"), SetBinding(Scoped.Blueprint, Site, TEXT("Text"), TEXT("GetTitleText")));
	}
	const FString Bound = Source.Read();
	TestTrue(TEXT("into the file"), Bound.Contains(TEXT("        Text <- GetTitleText()")));
	TestTrue(TEXT("whose import is untouched"), Bound.StartsWith(FString::Printf(TEXT("use \"%s\" as lib"), *Library.FilePath)));
	TestTrue(TEXT("nor its style"), Bound.Contains(TEXT("Text Title : lib.Caption {")));

	if (!TestTrue(TEXT("there is something to undo"), GEditor->UndoTransaction())) return false;
	TestEqual(TEXT("the undo puts the file back"), Source.Read(), Before);
	if (!TestTrue(TEXT("there is something to redo"), GEditor->RedoTransaction())) return false;
	TestEqual(TEXT("the redo writes the line again"), Source.Read(), Bound);
	return true;
}

#endif
