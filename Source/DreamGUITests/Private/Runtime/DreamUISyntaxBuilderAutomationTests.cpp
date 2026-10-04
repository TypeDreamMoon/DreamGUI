// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Controls/DreamButton.h"
#include "Controls/DreamExpandableArea.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetPropertyBinding.h"
#include "Core/DreamWidgetTree.h"
#include "Interaction/DreamContentWidget.h"
#include "Misc/ScopeExit.h"
#include "Text/DreamUIAst.h"
#include "Text/DreamUIDiagnostics.h"
#include "Text/DreamUISourceFile.h"
#include "Text/DreamUITextBuilder.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

/*
 * What the builder makes of the newer .dui spellings: a layout container written as the node's type, a component
 * named by `use … as` (by path, by file, under a library's prefix), styles that carry components and slot lines, slot
 * declarations with a layout and a default, a host filling a component's slot by name, `Shown` and the `if` blocks it
 * stands for, routes onto a component's own events, and anonymous nodes.
 *
 * Parsed from text wherever the shape is one a file can have, so a test reads like the file it is about; built by
 * hand only for the shapes the parser refuses before the builder could see them, which are still the builder's to
 * refuse for the callers that hand it an AST directly. Assertions are on diagnostic codes, never on message text, and
 * the trees are held by TStrongObjectPtr for the length of a test -- nothing here collects garbage.
 */
namespace DreamUISyntaxBuilderTestLocal
{
	/** `use` imports served from a map: the spelling is the key, the resolved path is it under /virtual/. */
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

	/** One file's AST, its tree, and everything the parse and the build said and recorded. */
	struct FBuilt
	{
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		TStrongObjectPtr<UDreamWidgetTree> Tree;
		TArray<FDreamWidgetPropertyBinding> Bindings;
		TArray<FDreamWidgetEventBinding> EventBindings;

		UDreamWidget* Find(const FString& InDisplayName) const
		{
			UDreamWidget* Found = nullptr;
			if (Tree.IsValid())
			{
				Tree->ForEachWidget([&InDisplayName, &Found](UDreamWidget* InWidget)
				{
					if (Found == nullptr && IsValid(InWidget) && InWidget->GetDisplayName() == InDisplayName)
					{
						Found = InWidget;
					}
				});
			}
			return Found;
		}

		const FDreamUINode* FindNode(const FString& InId) const
		{
			const FDreamUINode* Found = nullptr;
			if (Ast.bHasRoot)
			{
				Ast.ForEachNode([&InId, &Found](const FDreamUINode& InNode)
				{
					if (Found == nullptr && InNode.Id == InId)
					{
						Found = &InNode;
					}
				});
			}
			return Found;
		}

		const FDreamWidgetPropertyBinding* FindBinding(const FString& InDisplayName, const TCHAR* InPropertyName) const
		{
			const UDreamWidget* Widget = Find(InDisplayName);
			if (Widget == nullptr)
			{
				return nullptr;
			}
			const FName WidgetName = UDreamWidgetTree::MakeWidgetVariableName(Widget);
			return Bindings.FindByPredicate([WidgetName, InPropertyName](const FDreamWidgetPropertyBinding& InBinding)
			{
				return InBinding.WidgetName == WidgetName && InBinding.PropertyName == FName(InPropertyName);
			});
		}

		bool Reported(EDreamUIDiagnosticCode InCode) const
		{
			return Diagnostics.Diagnostics.ContainsByPredicate([InCode](const FDreamUIDiagnostic& InDiagnostic)
			{
				return InDiagnostic.Code == InCode;
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

	/** Parse and, when the parse holds, build -- with somewhere to record events, as the compiler gives. */
	void Build(FBuilt& OutBuilt, const TArray<FString>& InLines, TMap<FString, FString> InImports = {})
	{
		OutBuilt.Diagnostics.SourceName = TEXT("Syntax.dui");
		if (FDreamUISourceFile::Parse(FString::Join(InLines, TEXT("\n")), OutBuilt.Diagnostics.SourceName, OutBuilt.Ast,
			OutBuilt.Diagnostics, MakeMapReader(MoveTemp(InImports))))
		{
			OutBuilt.Tree.Reset(FDreamUITextBuilder::Build(OutBuilt.Ast, GetTransientPackage(), OutBuilt.Diagnostics,
				OutBuilt.Bindings, &OutBuilt.EventBindings));
		}
	}

	/** Build OutBuilt.Ast as it was assembled by hand. */
	void BuildAst(FBuilt& OutBuilt)
	{
		OutBuilt.Diagnostics.SourceName = TEXT("SyntaxByHand.dui");
		OutBuilt.Tree.Reset(FDreamUITextBuilder::Build(OutBuilt.Ast, GetTransientPackage(), OutBuilt.Diagnostics,
			OutBuilt.Bindings, &OutBuilt.EventBindings));
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

	FDreamUINode MakeSlot(const FString& InName, int32 InLine = 1)
	{
		FDreamUINode Slot;
		Slot.Kind = EDreamUINodeKind::NamedSlot;
		Slot.Id = InName;
		Slot.Location = FDreamUISourceLocation(InLine, 1);
		return Slot;
	}

	void SetRoot(FDreamUIAst& OutAst, FDreamUINode InRoot)
	{
		OutAst.ClassPath = TEXT("/Game/UI/WBP_SyntaxByHand");
		OutAst.ClassPathLocation = FDreamUISourceLocation(1, 1);
		OutAst.Root = MoveTemp(InRoot);
		OutAst.bHasRoot = true;
	}

	/** The container a widget carries, as the class asked for, or null. */
	template <typename TContainer>
	TContainer* ContainerOf(const UDreamWidget* InWidget)
	{
		return InWidget != nullptr ? Cast<TContainer>(InWidget->GetLayoutContainer()) : nullptr;
	}

	const TCHAR* const ExpandableAreaUse = TEXT("use /Script/DreamGUIControls.DreamExpandableArea as Area");
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUISyntaxContainerTypeTest,
	"DreamGUI.Text.Syntax.ALayoutContainerAsTheTypeIsAPlainWidgetWhoseLinesReachTheContainer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUISyntaxContainerTypeTest::RunTest(const FString& Parameters)
{
	using namespace DreamUISyntaxBuilderTestLocal;
	{
		FBuilt Built;
		Build(Built, {
			TEXT("Widget Root {"),
			TEXT("    VerticalBox Categories {"),
			TEXT("        Spacing = 29"),
			TEXT("        RenderOpacity = 0.5"),
			TEXT("        Widget Cell {"),
			TEXT("            @slot ZOrder = 3"),
			TEXT("        }"),
			TEXT("    }"),
			TEXT("}"),
		});
		if (Built.Diagnostics.NumErrors() > 0)
		{
			Built.Dump(*this);
		}
		TestEqual(TEXT("A container-typed node builds without errors"), Built.Diagnostics.NumErrors(), 0);
		UDreamWidget* Categories = Built.Find(TEXT("Categories"));
		if (TestNotNull(TEXT("the node is in the tree"), Categories))
		{
			TestEqual(TEXT("as a plain widget"), Categories->GetClass(), UDreamWidget::StaticClass());
			TestNull(TEXT("with no visual"), Categories->GetVisual());
			UDreamLayoutContainerVerticalBox* Column = ContainerOf<UDreamLayoutContainerVerticalBox>(Categories);
			if (TestNotNull(TEXT("carrying the container its type names"), Column))
			{
				TestEqual(TEXT("and a line only the container declares landed on the container"), Column->Spacing, 29.0f);
			}
			TestEqual(TEXT("while a line the widget declares landed on the widget"), Categories->GetRenderOpacity(), 0.5f);
		}
		UDreamWidget* Cell = Built.Find(TEXT("Cell"));
		if (TestNotNull(TEXT("the child is in the tree"), Cell) && TestNotNull(TEXT("and the container gave it a panel slot"), Cell->GetPanelSlot()))
		{
			TestEqual(TEXT("which its @slot line was written on"), Cell->GetPanelSlot()->ZOrder, 3);
		}
	}
	{
		// A container is not something a binding can name -- the same rule `+ VerticalBox { Spacing <- F() }` meets.
		FBuilt Built;
		Build(Built, {
			TEXT("Widget Root {"),
			TEXT("    VerticalBox Column {"),
			TEXT("        Spacing <- GetSpacing()"),
			TEXT("    }"),
			TEXT("}"),
		});
		TestTrue(TEXT("a binding onto the container through the node is refused as a target no binding can name"),
			Built.Reported(EDreamUIDiagnosticCode::BindingTargetNotSupported));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUISyntaxOneContainerTest,
	"DreamGUI.Text.Syntax.ANodeIsLaidOutByOneContainerWhereverTheSecondComesFrom",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUISyntaxOneContainerTest::RunTest(const FString& Parameters)
{
	using namespace DreamUISyntaxBuilderTestLocal;
	{
		FBuilt Built;
		Build(Built, {
			TEXT("Widget Root {"),
			TEXT("    VerticalBox Column {"),
			TEXT("        + HorizontalBox { }"),
			TEXT("    }"),
			TEXT("}"),
		});
		TestTrue(TEXT("a '+' container on a container-typed node is a second container"), Built.Reported(EDreamUIDiagnosticCode::SecondLayoutContainer));
		TestNull(TEXT("and no tree comes back"), Built.Tree.Get());
	}
	{
		FBuilt Built;
		Build(Built, {
			TEXT("Widget Root {"),
			TEXT("    + VerticalBox { }"),
			TEXT("    + HorizontalBox { }"),
			TEXT("}"),
		});
		TestTrue(TEXT("two '+' containers on one node are refused rather than the second replacing the first"),
			Built.Reported(EDreamUIDiagnosticCode::SecondLayoutContainer));
	}
	{
		FBuilt Built;
		Build(Built, {
			TEXT("style Column {"),
			TEXT("    + VerticalBox { }"),
			TEXT("}"),
			TEXT("Widget Root : Column {"),
			TEXT("    + HorizontalBox { }"),
			TEXT("}"),
		});
		TestTrue(TEXT("a style's container and a different one of the node's are two containers"),
			Built.Reported(EDreamUIDiagnosticCode::SecondLayoutContainer));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUISyntaxAliasTest,
	"DreamGUI.Text.Syntax.AComponentAliasNamesItsClassByPathByFileAndUnderALibrarysPrefix",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUISyntaxAliasTest::RunTest(const FString& Parameters)
{
	using namespace DreamUISyntaxBuilderTestLocal;
	{
		FBuilt Built;
		Build(Built, {
			TEXT("use /Script/DreamGUIControls.DreamButton as Button"),
			TEXT("Widget Root {"),
			TEXT("    Button Ok {"),
			TEXT("    }"),
			TEXT("}"),
		});
		if (Built.Diagnostics.NumErrors() > 0)
		{
			Built.Dump(*this);
		}
		TestEqual(TEXT("A node typed by a path alias builds without errors"), Built.Diagnostics.NumErrors(), 0);
		UDreamWidget* Ok = Built.Find(TEXT("Ok"));
		TestTrue(TEXT("as the class the path names"), Ok != nullptr && Ok->GetClass() == UDreamButton::StaticClass());
	}
	{
		// A component file: it has a root, so `as` names its class, which its `class` line gives.
		FBuilt Built;
		Build(Built, {
			TEXT("use \"Row.dui\" as Row"),
			TEXT("Widget Root {"),
			TEXT("    Row Row1 {"),
			TEXT("    }"),
			TEXT("}"),
		}, {{TEXT("Row.dui"), TEXT("class /Script/DreamGUIControls.DreamButton\nWidget Root {\n}")}});
		if (Built.Diagnostics.NumErrors() > 0)
		{
			Built.Dump(*this);
		}
		TestEqual(TEXT("A node typed by a file alias builds without errors"), Built.Diagnostics.NumErrors(), 0);
		UDreamWidget* Row = Built.Find(TEXT("Row1"));
		TestTrue(TEXT("as the class the file's class line names"), Row != nullptr && Row->GetClass() == UDreamButton::StaticClass());
	}
	{
		// A library: no root, so `as` is a prefix, and the alias it declares comes along under it -- with its styles.
		FBuilt Built;
		Build(Built, {
			TEXT("use \"Lib.dui\" as nier"),
			TEXT("Widget Root {"),
			TEXT("    nier.Btn Ok : nier.Faint {"),
			TEXT("    }"),
			TEXT("}"),
		}, {{TEXT("Lib.dui"), TEXT("use /Script/DreamGUIControls.DreamButton as Btn\nstyle Faint {\n    RenderOpacity = 0.5\n}")}});
		if (Built.Diagnostics.NumErrors() > 0)
		{
			Built.Dump(*this);
		}
		TestEqual(TEXT("A node typed by a library's alias under its prefix builds without errors"), Built.Diagnostics.NumErrors(), 0);
		UDreamWidget* Ok = Built.Find(TEXT("Ok"));
		if (TestNotNull(TEXT("the node is in the tree"), Ok))
		{
			TestEqual(TEXT("as the class the library's alias names"), Ok->GetClass(), UDreamButton::StaticClass());
			TestEqual(TEXT("wearing the library's style"), Ok->GetRenderOpacity(), 0.5f);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUISyntaxAliasResolverTest,
	"DreamGUI.Text.Syntax.AFileAliasWithNoClassLineAsksTheEditorWhichBlueprintIsMadeFromIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUISyntaxAliasResolverTest::RunTest(const FString& Parameters)
{
	using namespace DreamUISyntaxBuilderTestLocal;

	// Whatever the editor installed is put back however this test ends: the hook is process-wide.
	TFunction<UClass*(const FString&)>& Resolver = FDreamUITextBuilder::SourceClassResolver();
	TFunction<UClass*(const FString&)> Installed = MoveTemp(Resolver);
	ON_SCOPE_EXIT
	{
		FDreamUITextBuilder::SourceClassResolver() = MoveTemp(Installed);
	};

	const TArray<FString> Host = {
		TEXT("use \"Row.dui\" as Row"),
		TEXT("Widget Root {"),
		TEXT("    Row Row1 {"),
		TEXT("    }"),
		TEXT("}"),
	};
	const TMap<FString, FString> Files = {{TEXT("Row.dui"), TEXT("Widget Root {\n}")}};
	{
		Resolver = TFunction<UClass*(const FString&)>();
		FBuilt Built;
		Build(Built, Host, Files);
		TestTrue(TEXT("with nobody to ask, the alias names no class"), Built.Reported(EDreamUIDiagnosticCode::ComponentAliasUnresolved));
	}
	{
		Resolver = [](const FString&) -> UClass* { return nullptr; };
		FBuilt Built;
		Build(Built, Host, Files);
		TestTrue(TEXT("and with no Blueprint made from the file, the same"), Built.Reported(EDreamUIDiagnosticCode::ComponentAliasUnresolved));
	}
	{
		TArray<FString> Asked;
		Resolver = [&Asked](const FString& InResolvedPath) -> UClass*
		{
			Asked.Add(InResolvedPath);
			return InResolvedPath == TEXT("/virtual/Row.dui") ? UDreamButton::StaticClass() : nullptr;
		};
		FBuilt Built;
		Build(Built, Host, Files);
		if (Built.Diagnostics.NumErrors() > 0)
		{
			Built.Dump(*this);
		}
		TestEqual(TEXT("The editor's answer builds the node without errors"), Built.Diagnostics.NumErrors(), 0);
		UDreamWidget* Row = Built.Find(TEXT("Row1"));
		TestTrue(TEXT("as the class it answered with"), Row != nullptr && Row->GetClass() == UDreamButton::StaticClass());
		TestTrue(TEXT("asked with the path the import resolved to"), Asked.Contains(TEXT("/virtual/Row.dui")));
		Resolver = TFunction<UClass*(const FString&)>();
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUISyntaxAliasErrorsTest,
	"DreamGUI.Text.Syntax.AnAliasThatIsABuiltInOrNamesNoUsableClassSaysWhich",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUISyntaxAliasErrorsTest::RunTest(const FString& Parameters)
{
	using namespace DreamUISyntaxBuilderTestLocal;
	{
		FBuilt Built;
		Build(Built, { TEXT("use /Script/DreamGUIControls.DreamButton as Text"), TEXT("Widget Root {"), TEXT("}") });
		TestTrue(TEXT("an alias spelt like a built-in tag is refused"), Built.Reported(EDreamUIDiagnosticCode::AliasShadowsBuiltIn));
	}
	{
		FBuilt Built;
		Build(Built, { TEXT("use /Script/DreamGUIControls.DreamButton as VerticalBox"), TEXT("Widget Root {"), TEXT("}") });
		TestTrue(TEXT("and so is one spelt like a layout container"), Built.Reported(EDreamUIDiagnosticCode::AliasShadowsBuiltIn));
	}
	{
		FBuilt Built;
		Build(Built, {
			TEXT("use /Game/DreamGUITests/Missing/WBP_Nowhere as Nowhere"),
			TEXT("Widget Root {"), TEXT("    Nowhere Here {"), TEXT("    }"), TEXT("}"),
		});
		TestTrue(TEXT("an alias whose path loads nothing names no class"), Built.Reported(EDreamUIDiagnosticCode::ComponentAliasUnresolved));
	}
	{
		FBuilt Built;
		Build(Built, {
			TEXT("use /Script/DreamGUI.DreamWidget as Plain"),
			TEXT("Widget Root {"), TEXT("    Plain Here {"), TEXT("    }"), TEXT("}"),
		});
		TestTrue(TEXT("an alias naming a class that is no user widget is refused as such"), Built.Reported(EDreamUIDiagnosticCode::NotAUserWidgetClass));
	}
	{
		FBuilt Built;
		Build(Built, {
			TEXT("use \"Lib.dui\" as nier"),
			TEXT("Widget Root {"), TEXT("    nier.Nope Here {"), TEXT("    }"), TEXT("}"),
		}, {{TEXT("Lib.dui"), TEXT("style Faint {\n    RenderOpacity = 0.5\n}")}});
		TestTrue(TEXT("a name the library does not declare is an unknown node type"), Built.Reported(EDreamUIDiagnosticCode::UnknownNodeType));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUISyntaxStyleComponentsTest,
	"DreamGUI.Text.Syntax.AStylesComponentsAndSlotLinesApplyFirstAndTheNodesOwnWin",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUISyntaxStyleComponentsTest::RunTest(const FString& Parameters)
{
	using namespace DreamUISyntaxBuilderTestLocal;

	FBuilt Built;
	Build(Built, {
		TEXT("style Column {"),
		TEXT("    + VerticalBox {"),
		TEXT("        Spacing = 15"),
		TEXT("        Padding = (1, 2, 3, 4)"),
		TEXT("    }"),
		TEXT("    @slot ZOrder = 7"),
		TEXT("    @slot Padding = (8, 8, 8, 8)"),
		TEXT("}"),
		TEXT("style WideColumn : Column {"),
		TEXT("    + VerticalBox {"),
		TEXT("        Spacing = 18"),
		TEXT("    }"),
		TEXT("}"),
		TEXT("Widget Root {"),
		TEXT("    + HorizontalBox { }"),
		TEXT("    Widget Col : WideColumn {"),
		TEXT("        + VerticalBox {"),
		TEXT("            Spacing = 20"),
		TEXT("        }"),
		TEXT("        @slot ZOrder = 9"),
		TEXT("    }"),
		TEXT("    VerticalBox Typed : Column {"),
		TEXT("        Spacing = 4"),
		TEXT("    }"),
		TEXT("}"),
	});
	if (Built.Diagnostics.NumErrors() > 0)
	{
		Built.Dump(*this);
	}
	TestEqual(TEXT("Styles carrying components and slot lines build without errors"), Built.Diagnostics.NumErrors(), 0);

	UDreamWidget* Col = Built.Find(TEXT("Col"));
	if (TestNotNull(TEXT("the styled node is in the tree"), Col))
	{
		UDreamLayoutContainerVerticalBox* Column = ContainerOf<UDreamLayoutContainerVerticalBox>(Col);
		if (TestNotNull(TEXT("the styles' container and the node's are one container"), Column))
		{
			TestEqual(TEXT("whose value the node wrote last wins, over the derived style's and the base's"), Column->Spacing, 20.0f);
			TestEqual(TEXT("and whose value only the base style wrote is kept"), Column->Padding, FMargin(1, 2, 3, 4));
		}
		if (TestNotNull(TEXT("the parent's container gave it a panel slot"), Col->GetPanelSlot()))
		{
			TestEqual(TEXT("the node's @slot line wins over the style's"), Col->GetPanelSlot()->ZOrder, 9);
			TestEqual(TEXT("and the style's line the node did not write is kept"), Col->GetPanelSlot()->Padding, FMargin(8, 8, 8, 8));
		}
	}
	UDreamWidget* Typed = Built.Find(TEXT("Typed"));
	if (TestNotNull(TEXT("the container-typed styled node is in the tree"), Typed))
	{
		UDreamLayoutContainerVerticalBox* Column = ContainerOf<UDreamLayoutContainerVerticalBox>(Typed);
		if (TestNotNull(TEXT("its type's container and its style's are one container"), Column))
		{
			TestEqual(TEXT("the node's bare line wins over the style's '+' line"), Column->Spacing, 4.0f);
			TestEqual(TEXT("and the style's other value is kept"), Column->Padding, FMargin(1, 2, 3, 4));
		}
		TestTrue(TEXT("and the style's @slot line reached its panel slot"), Typed->GetPanelSlot() != nullptr && Typed->GetPanelSlot()->ZOrder == 7);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUISyntaxSlotDeclarationTest,
	"DreamGUI.Text.Syntax.ASlotDeclarationCarriesALayoutAndMayBeTheDefault",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUISyntaxSlotDeclarationTest::RunTest(const FString& Parameters)
{
	using namespace DreamUISyntaxBuilderTestLocal;
	{
		FBuilt Built;
		Build(Built, {
			TEXT("style RowList {"),
			TEXT("    RenderOpacity = 0.5"),
			TEXT("}"),
			TEXT("Widget Root {"),
			TEXT("    + HorizontalBox { }"),
			TEXT("    slot Rows default : RowList {"),
			TEXT("        + VerticalBox {"),
			TEXT("            Spacing = 15"),
			TEXT("        }"),
			TEXT("        @slot ZOrder = 2"),
			TEXT("    }"),
			TEXT("    slot Detail"),
			TEXT("}"),
		});
		if (Built.Diagnostics.NumErrors() > 0)
		{
			Built.Dump(*this);
		}
		TestEqual(TEXT("Slot declarations with blocks build without errors"), Built.Diagnostics.NumErrors(), 0);

		UDreamWidget* Rows = Built.Find(TEXT("Rows"));
		UDreamNamedSlot* RowsSlot = Rows != nullptr ? Rows->GetComponent<UDreamNamedSlot>() : nullptr;
		if (TestNotNull(TEXT("the slot with a block is a named slot"), RowsSlot))
		{
			TestTrue(TEXT("marked as the default"), RowsSlot->bIsDefaultSlot);
			TestTrue(TEXT("and, laid out by a container, taking several"), RowsSlot->bAcceptsSeveral);
			UDreamLayoutContainerVerticalBox* Column = ContainerOf<UDreamLayoutContainerVerticalBox>(Rows);
			TestTrue(TEXT("whose container carries its line"), Column != nullptr && Column->Spacing == 15.0f);
			TestEqual(TEXT("whose style applied"), Rows->GetRenderOpacity(), 0.5f);
			TestTrue(TEXT("and whose @slot line reached its own panel slot"), Rows->GetPanelSlot() != nullptr && Rows->GetPanelSlot()->ZOrder == 2);
		}
		UDreamWidget* Detail = Built.Find(TEXT("Detail"));
		UDreamNamedSlot* DetailSlot = Detail != nullptr ? Detail->GetComponent<UDreamNamedSlot>() : nullptr;
		if (TestNotNull(TEXT("the plain slot is a named slot"), DetailSlot))
		{
			TestFalse(TEXT("not the default"), DetailSlot->bIsDefaultSlot);
			TestFalse(TEXT("and, with no layout, holding one"), DetailSlot->bAcceptsSeveral);
		}
		TArray<FName> Declared;
		UDreamUserWidget::CollectDeclaredSlotNames(Built.Tree.Get(), Declared);
		TestTrue(TEXT("the tree declares both"), Declared.Contains(FName(TEXT("Rows"))) && Declared.Contains(FName(TEXT("Detail"))));
	}
	{
		FBuilt Built;
		Build(Built, {
			TEXT("Widget Root {"),
			TEXT("    slot Rows default"),
			TEXT("    slot Detail default"),
			TEXT("}"),
		});
		TestTrue(TEXT("a second default in one file is refused"), Built.Reported(EDreamUIDiagnosticCode::MultipleDefaultSlots));
	}
	{
		// The same rule for a tree built by hand, which the parser never saw.
		FBuilt Built;
		FDreamUINode Root = MakeNode(TEXT("Widget"), TEXT("Root"));
		FDreamUINode First = MakeSlot(TEXT("Rows"), 2);
		First.bDefaultSlot = true;
		FDreamUINode Second = MakeSlot(TEXT("Detail"), 3);
		Second.bDefaultSlot = true;
		Root.Children.Add(First);
		Root.Children.Add(Second);
		SetRoot(Built.Ast, Root);
		BuildAst(Built);
		TestTrue(TEXT("the builder refuses a second default too"), Built.Reported(EDreamUIDiagnosticCode::MultipleDefaultSlots));
		TestNull(TEXT("and gives back no tree"), Built.Tree.Get());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUISyntaxSlotFillTest,
	"DreamGUI.Text.Syntax.AHostFillsAComponentsSlotByNameWithContentInitializeHangsThere",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUISyntaxSlotFillTest::RunTest(const FString& Parameters)
{
	using namespace DreamUISyntaxBuilderTestLocal;

	FBuilt Built;
	Build(Built, {
		ExpandableAreaUse,
		TEXT("Widget Root {"),
		TEXT("    Area Exp {"),
		TEXT("        slot Header {"),
		TEXT("            Text Title {"),
		TEXT("                @slot Padding = (1, 2, 3, 4)"),
		TEXT("            }"),
		TEXT("        }"),
		TEXT("        slot Content {"),
		TEXT("            Text First {"),
		TEXT("            }"),
		TEXT("            Text Second {"),
		TEXT("            }"),
		TEXT("        }"),
		TEXT("    }"),
		TEXT("}"),
	});
	if (Built.Diagnostics.NumErrors() > 0)
	{
		Built.Dump(*this);
	}
	TestEqual(TEXT("Filling a component's slots by name builds without errors"), Built.Diagnostics.NumErrors(), 0);

	UDreamUserWidget* Exp = Cast<UDreamUserWidget>(Built.Find(TEXT("Exp")));
	if (!TestNotNull(TEXT("the component instance is in the tree"), Exp))
	{
		return false;
	}
	UDreamWidget* Title = Built.Find(TEXT("Title"));
	if (TestNotNull(TEXT("the named slot's content is in the tree"), Title))
	{
		// Straight on the instance: a fill is an address, and makes no widget of its own in between.
		TestTrue(TEXT("hanging on the instance, as the designer puts a drop into a slot row"), Title->GetParent() == Exp);
		TestTrue(TEXT("and bound to the slot it fills, which is where Initialize moves it"),
			Exp->GetContentForNamedSlot(UDreamExpandableArea::HeaderSlotName) == Title);
		TestTrue(TEXT("its @slot line kept on a panel slot of its own for the hole that lays it out"),
			Title->GetPanelSlot() != nullptr && Title->GetPanelSlot()->Padding == FMargin(1, 2, 3, 4));
	}
	UDreamWidget* First = Built.Find(TEXT("First"));
	UDreamWidget* Second = Built.Find(TEXT("Second"));
	TestTrue(TEXT("the default slot's content is nested on the instance, all of it"),
		First != nullptr && Second != nullptr && First->GetParent() == Exp && Second->GetParent() == Exp);
	TestNull(TEXT("with no binding: nesting already means the default slot"),
		Exp->GetContentForNamedSlot(UDreamExpandableArea::ContentSlotName));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUISyntaxSlotFillErrorsTest,
	"DreamGUI.Text.Syntax.AFillOutsideAComponentForAnUndeclaredSlotOrOverfullIsRefused",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUISyntaxSlotFillErrorsTest::RunTest(const FString& Parameters)
{
	using namespace DreamUISyntaxBuilderTestLocal;
	{
		FBuilt Built;
		Build(Built, {
			TEXT("Widget Root {"),
			TEXT("    Widget Plain {"),
			TEXT("        slot Header {"),
			TEXT("            Text Title {"),
			TEXT("            }"),
			TEXT("        }"),
			TEXT("    }"),
			TEXT("}"),
		});
		TestTrue(TEXT("a fill under a node that is no component instance is refused"), Built.Reported(EDreamUIDiagnosticCode::SlotFillOutsideComponent));
	}
	{
		FBuilt Built;
		Build(Built, {
			ExpandableAreaUse,
			TEXT("Widget Root {"),
			TEXT("    Area Exp {"),
			TEXT("        slot Footer {"),
			TEXT("            Text Title {"),
			TEXT("            }"),
			TEXT("        }"),
			TEXT("    }"),
			TEXT("}"),
		});
		TestTrue(TEXT("a fill of a slot the component does not declare is refused"), Built.Reported(EDreamUIDiagnosticCode::UnknownSlotToFill));
	}
	{
		FBuilt Built;
		Build(Built, {
			ExpandableAreaUse,
			TEXT("Widget Root {"),
			TEXT("    Area Exp {"),
			TEXT("        slot Header {"),
			TEXT("            Text One {"),
			TEXT("            }"),
			TEXT("            Text Two {"),
			TEXT("            }"),
			TEXT("        }"),
			TEXT("    }"),
			TEXT("}"),
		});
		TestTrue(TEXT("two widgets for a named slot, which binds one, are refused"), Built.Reported(EDreamUIDiagnosticCode::ParentRefusedChild));
	}
	{
		// By hand: two fills of one slot share an id, which a parse would refuse before the builder saw them.
		FBuilt Built;
		FDreamUINode Root = MakeNode(TEXT("Widget"), TEXT("Root"));
		FDreamUINode Exp = MakeNode(TEXT("/Script/DreamGUIControls.DreamExpandableArea"), TEXT("Exp"), 2);
		FDreamUINode FirstFill = MakeSlot(TEXT("Header"), 3);
		FirstFill.bFillsSlot = true;
		FirstFill.Children.Add(MakeNode(TEXT("Text"), TEXT("One"), 4));
		FDreamUINode SecondFill = MakeSlot(TEXT("Header"), 5);
		SecondFill.bFillsSlot = true;
		SecondFill.Children.Add(MakeNode(TEXT("Text"), TEXT("Two"), 6));
		Exp.Children.Add(FirstFill);
		Exp.Children.Add(SecondFill);
		Root.Children.Add(Exp);
		SetRoot(Built.Ast, Root);
		BuildAst(Built);
		TestTrue(TEXT("a named slot filled twice is refused rather than the first content drifting into the default slot"),
			Built.Reported(EDreamUIDiagnosticCode::ParentRefusedChild));
	}
	{
		// By hand: a fill carrying lines of its own, which have nothing to land on -- the slot is the component's.
		FBuilt Built;
		FDreamUINode Root = MakeNode(TEXT("Widget"), TEXT("Root"));
		FDreamUINode Exp = MakeNode(TEXT("/Script/DreamGUIControls.DreamExpandableArea"), TEXT("Exp"), 2);
		FDreamUINode Fill = MakeSlot(TEXT("Header"), 3);
		Fill.bFillsSlot = true;
		Fill.StyleName = TEXT("Anything");
		Fill.Children.Add(MakeNode(TEXT("Text"), TEXT("One"), 4));
		Exp.Children.Add(Fill);
		Root.Children.Add(Exp);
		SetRoot(Built.Ast, Root);
		BuildAst(Built);
		TestTrue(TEXT("a fill with lines of its own is a malformed slot"), Built.Reported(EDreamUIDiagnosticCode::MalformedSlotDeclaration));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUISyntaxShownTest,
	"DreamGUI.Text.Syntax.ShownIsWrittenThroughVisibilityAndBoundThroughItsSetter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUISyntaxShownTest::RunTest(const FString& Parameters)
{
	using namespace DreamUISyntaxBuilderTestLocal;

	FBuilt Built;
	Build(Built, {
		TEXT("Widget Root {"),
		TEXT("    Widget Gone {"),
		TEXT("        Shown = false"),
		TEXT("    }"),
		TEXT("    Widget Kept {"),
		TEXT("        Shown <- HasSave()"),
		TEXT("    }"),
		TEXT("}"),
	});
	if (Built.Diagnostics.NumErrors() > 0)
	{
		Built.Dump(*this);
	}
	TestEqual(TEXT("Writing and binding Shown builds without errors"), Built.Diagnostics.NumErrors(), 0);
	UDreamWidget* Gone = Built.Find(TEXT("Gone"));
	if (TestNotNull(TEXT("the written node is in the tree"), Gone))
	{
		// The whole point: Shown keeps nothing of its own, so a write that landed in its field would be lost on save
		// and leave the widget visible.
		TestEqual(TEXT("'Shown = false' collapsed it, through the setter"), Gone->GetVisibility(), EDreamWidgetVisibility::Collapsed);
	}
	const FDreamWidgetPropertyBinding* Binding = Built.FindBinding(TEXT("Kept"), TEXT("Shown"));
	if (TestNotNull(TEXT("'Shown <- HasSave()' recorded a binding"), Binding))
	{
		TestEqual(TEXT("driven through SetShown"), Binding->SetterName, FName(TEXT("SetShown")));
		TestEqual(TEXT("from the function the line names"), Binding->FunctionName, FName(TEXT("HasSave")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUISyntaxConditionalTest,
	"DreamGUI.Text.Syntax.AnIfBlocksBranchesBuildAsNodesBoundThroughShown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUISyntaxConditionalTest::RunTest(const FString& Parameters)
{
	using namespace DreamUISyntaxBuilderTestLocal;

	FBuilt Built;
	Build(Built, {
		TEXT("Widget Root {"),
		TEXT("    if HasSave() {"),
		TEXT("        Widget Continue {"),
		TEXT("        }"),
		TEXT("    } else {"),
		TEXT("        Text NoSave {"),
		TEXT("            Text = \"No save data\""),
		TEXT("        }"),
		TEXT("    }"),
		TEXT("}"),
	});
	if (Built.Diagnostics.NumErrors() > 0)
	{
		Built.Dump(*this);
	}
	TestEqual(TEXT("An if block builds without errors"), Built.Diagnostics.NumErrors(), 0);
	TestNotNull(TEXT("the first branch's widget is in the tree"), Built.Find(TEXT("Continue")));
	TestNotNull(TEXT("and so is the other branch's, kept rather than rebuilt"), Built.Find(TEXT("NoSave")));

	// What the front end made of the block is a binding like a written one, and the builder treats it as one.
	const FDreamUINode* ContinueNode = Built.FindNode(TEXT("Continue"));
	const FDreamUIProperty* Synthesized = ContinueNode != nullptr
		? ContinueNode->Properties.FindByPredicate([](const FDreamUIProperty& InProperty) { return InProperty.Name == TEXT("Shown"); })
		: nullptr;
	if (TestNotNull(TEXT("the branch's widget carries a Shown line"), Synthesized))
	{
		TestTrue(TEXT("which the front end made"), Synthesized->bSynthesized);
	}
	const FDreamWidgetPropertyBinding* Binding = Built.FindBinding(TEXT("Continue"), TEXT("Shown"));
	if (TestNotNull(TEXT("and it was recorded as a binding"), Binding))
	{
		TestEqual(TEXT("driven through SetShown"), Binding->SetterName, FName(TEXT("SetShown")));
		TestEqual(TEXT("from the condition"), Binding->FunctionName, FName(TEXT("HasSave")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUISyntaxComponentEventTest,
	"DreamGUI.Text.Syntax.ARouteOntoAComponentsOwnEventIsRecordedAndAnUnloweredEmitIsSkipped",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUISyntaxComponentEventTest::RunTest(const FString& Parameters)
{
	using namespace DreamUISyntaxBuilderTestLocal;

	// A Blueprint event dispatcher is an inline multicast delegate marked assignable -- the shape the button's own
	// event has -- so a route onto one takes this same path.
	const FMulticastInlineDelegateProperty* Dispatcher =
		CastField<FMulticastInlineDelegateProperty>(UDreamButton::StaticClass()->FindPropertyByName(TEXT("OnClicked")));
	TestTrue(TEXT("the event routed below has the shape of a Blueprint dispatcher"),
		Dispatcher != nullptr && Dispatcher->HasAnyPropertyFlags(CPF_BlueprintAssignable));

	FBuilt Built;
	Build(Built, {
		TEXT("use /Script/DreamGUIControls.DreamButton as Button"),
		TEXT("events {"),
		TEXT("    Picked(Number Index)"),
		TEXT("}"),
		TEXT("Widget Root {"),
		TEXT("    Button Ok {"),
		TEXT("        OnClicked -> HandleOk"),
		TEXT("    }"),
		TEXT("    Button Emitter {"),
		TEXT("        OnClicked -> emit Picked(3)"),
		TEXT("    }"),
		TEXT("}"),
	});
	if (Built.Diagnostics.NumErrors() > 0)
	{
		Built.Dump(*this);
	}
	TestEqual(TEXT("Routes on component instances build without errors"), Built.Diagnostics.NumErrors(), 0);
	if (TestEqual(TEXT("one route was recorded: the emit has no handler until the compiler lowers it"), Built.EventBindings.Num(), 1))
	{
		const FDreamWidgetEventBinding& Route = Built.EventBindings[0];
		UDreamWidget* Ok = Built.Find(TEXT("Ok"));
		TestTrue(TEXT("on the instance that wrote it"), Ok != nullptr && Route.WidgetName == UDreamWidgetTree::MakeWidgetVariableName(Ok));
		TestEqual(TEXT("naming the component's event"), Route.EventName, FName(TEXT("OnClicked")));
		TestEqual(TEXT("and the host's handler"), Route.FunctionName, FName(TEXT("HandleOk")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUISyntaxNamespaceTest,
	"DreamGUI.Text.Syntax.ANamespacedStyleAndResourceResolveInTheirOwnLibrary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUISyntaxNamespaceTest::RunTest(const FString& Parameters)
{
	using namespace DreamUISyntaxBuilderTestLocal;

	// The host declares a Label and a Faint of its own, so a lookup that lost the prefix would find the wrong one
	// rather than nothing -- the failure that looks like it worked.
	FBuilt Built;
	Build(Built, {
		TEXT("use \"Lib.dui\" as nier"),
		TEXT("resources {"),
		TEXT("    Number Faint = 0.75"),
		TEXT("}"),
		TEXT("style Label {"),
		TEXT("    RenderOpacity = 1"),
		TEXT("}"),
		TEXT("Widget Root {"),
		TEXT("    Widget Danger : nier.Danger {"),
		TEXT("    }"),
		TEXT("    Widget Direct {"),
		TEXT("        RenderOpacity = @nier.Faint"),
		TEXT("    }"),
		TEXT("    Widget Local : Label {"),
		TEXT("    }"),
		TEXT("}"),
	}, {{TEXT("Lib.dui"), FString::Join(TArray<FString>{
		TEXT("resources {"),
		TEXT("    Number Faint = 0.25"),
		TEXT("}"),
		TEXT("style Label {"),
		TEXT("    RenderOpacity = @Faint"),
		TEXT("}"),
		TEXT("style Danger : Label {"),
		TEXT("    Visibility = Hidden"),
		TEXT("}"),
	}, TEXT("\n"))}});
	if (Built.Diagnostics.NumErrors() > 0)
	{
		Built.Dump(*this);
	}
	TestEqual(TEXT("Namespaced styles and resources build without errors"), Built.Diagnostics.NumErrors(), 0);
	UDreamWidget* Danger = Built.Find(TEXT("Danger"));
	if (TestNotNull(TEXT("the node wearing the library's style is in the tree"), Danger))
	{
		TestEqual(TEXT("its own line applied"), Danger->GetVisibility(), EDreamWidgetVisibility::Hidden);
		TestEqual(TEXT("and its base is the library's Label, reading the library's Faint"), Danger->GetRenderOpacity(), 0.25f);
	}
	UDreamWidget* Direct = Built.Find(TEXT("Direct"));
	TestTrue(TEXT("a prefixed resource names the library's entry"), Direct != nullptr && Direct->GetRenderOpacity() == 0.25f);
	UDreamWidget* Local = Built.Find(TEXT("Local"));
	TestTrue(TEXT("and the host's own style is still its own"), Local != nullptr && Local->GetRenderOpacity() == 1.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUISyntaxAnonymousTest,
	"DreamGUI.Text.Syntax.AnAnonymousNodeBuildsUnderTheIdTheParserGaveIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUISyntaxAnonymousTest::RunTest(const FString& Parameters)
{
	using namespace DreamUISyntaxBuilderTestLocal;

	FBuilt Built;
	Build(Built, {
		TEXT("Widget Root {"),
		TEXT("    HorizontalBox {"),
		TEXT("        Spacing = 14"),
		TEXT("        Text {"),
		TEXT("            Text = \"Status\""),
		TEXT("        }"),
		TEXT("    }"),
		TEXT("}"),
	});
	if (Built.Diagnostics.NumErrors() > 0)
	{
		Built.Dump(*this);
	}
	TestEqual(TEXT("Anonymous nodes build without errors"), Built.Diagnostics.NumErrors(), 0);
	if (!TestTrue(TEXT("the root holds one child"), Built.Ast.bHasRoot && Built.Ast.Root.Children.Num() == 1))
	{
		return false;
	}
	const FDreamUINode& BoxNode = Built.Ast.Root.Children[0];
	TestTrue(TEXT("which has no id of the author's"), BoxNode.bAnonymous && !BoxNode.Id.IsEmpty());
	UDreamWidget* Box = Built.Find(BoxNode.Id);
	if (TestNotNull(TEXT("and is built under the id the parser gave it"), Box))
	{
		UDreamLayoutContainerHorizontalBox* Row = ContainerOf<UDreamLayoutContainerHorizontalBox>(Box);
		TestTrue(TEXT("as the container its type names, with its line"), Row != nullptr && Row->Spacing == 14.0f);
	}
	if (TestEqual(TEXT("the box holds one child"), BoxNode.Children.Num(), 1))
	{
		UDreamWidget* Label = Built.Find(BoxNode.Children[0].Id);
		TestTrue(TEXT("an anonymous Text built with its visual"), Label != nullptr && Cast<UDreamText>(Label->GetVisual()) != nullptr);
		TestTrue(TEXT("under the anonymous box"), Label != nullptr && Label->GetParent() == Box);
	}
	return true;
}

#endif
