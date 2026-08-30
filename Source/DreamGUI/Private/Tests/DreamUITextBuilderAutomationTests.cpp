// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamBackgroundBlur.h"
#include "Core/Components/DreamBackgroundPixelate.h"
#include "Core/Components/DreamImage.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamPixelSort.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamSprite.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamTexture.h"
#include "Core/Components/DreamVisualEmpty.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetPropertyBinding.h"
#include "Core/DreamWidgetTree.h"
#include "Interaction/DreamContentWidget.h"
#include "Interaction/UISlider.h"
#include "Text/DreamUIAst.h"
#include "Text/DreamUIDiagnostics.h"
#include "Text/DreamUITextBuilder.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

/*
 * The builder, and only the builder.
 *
 * Every fixture here is a hand-built FDreamUIAst rather than a parsed string, which is not laziness
 * about writing .dui text: an AST built by hand cannot fail for a reason that belongs to the parser,
 * so a red test in this file always means the builder is wrong. It also means these tests were
 * written and could be reasoned about while the parser was still being implemented alongside them.
 *
 * Assertions are on diagnostic CODES and never on message text. The codes are the stable half of the
 * contract, deliberately (see DreamUIDiagnostics.h), and a suite that pins wording is one that has
 * to be edited every time a message is improved -- which is how messages stop being improved.
 *
 * Nothing here calls CollectGarbage. This code base has been bitten by that in tests before; a
 * TStrongObjectPtr on the tree is what keeps the built objects alive for the length of a test.
 */

namespace DreamUITextBuilderTestLocal
{
	FDreamUISourceLocation At(int32 InLine = 1)
	{
		// Non-zero, so FDreamUISourceLocation::IsValid holds and a diagnostic raised from a hand-built
		// node prints the same way one raised from a real file does.
		return FDreamUISourceLocation(InLine, 1);
	}

	FDreamUIValue Literal(EDreamUIValueKind InKind, const FString& InRaw)
	{
		FDreamUIValue Value;
		Value.Kind = InKind;
		Value.Raw = InRaw;
		Value.Location = At();
		return Value;
	}

	FDreamUIValue TupleOf(TArray<FString> InElements)
	{
		FDreamUIValue Value;
		Value.Kind = EDreamUIValueKind::Tuple;
		Value.Elements = MoveTemp(InElements);
		Value.Raw = FString::Printf(TEXT("(%s)"), *FString::Join(Value.Elements, TEXT(", ")));
		Value.Location = At();
		return Value;
	}

	FDreamUIProperty Assign(const FString& InName, FDreamUIValue InValue)
	{
		FDreamUIProperty Property;
		Property.Name = InName;
		Property.Value = MoveTemp(InValue);
		Property.Location = At();
		return Property;
	}

	FDreamUIProperty AssignNumber(const FString& InName, const FString& InRaw)
	{
		return Assign(InName, Literal(EDreamUIValueKind::Number, InRaw));
	}

	FDreamUIProperty AssignIdentifier(const FString& InName, const FString& InRaw)
	{
		return Assign(InName, Literal(EDreamUIValueKind::Identifier, InRaw));
	}

	FDreamUIProperty AssignString(const FString& InName, const FString& InRaw, const FString& InKeyOverride = FString())
	{
		FDreamUIValue Value = Literal(EDreamUIValueKind::String, InRaw);
		Value.LocalizationKeyOverride = InKeyOverride;
		return Assign(InName, MoveTemp(Value));
	}

	FDreamUIProperty BindTo(const FString& InName, const FString& InFunction)
	{
		FDreamUIProperty Property;
		Property.Name = InName;
		Property.BindingFunction = InFunction;
		Property.Location = At();
		return Property;
	}

	FDreamUINode MakeNode(const FString& InTypeName, const FString& InId)
	{
		FDreamUINode Node;
		Node.Kind = EDreamUINodeKind::Widget;
		Node.TypeName = InTypeName;
		Node.Id = InId;
		Node.Location = At();
		return Node;
	}

	FDreamUIComponent MakeComponent(const FString& InClassName)
	{
		FDreamUIComponent Component;
		Component.ClassName = InClassName;
		Component.Location = At();
		return Component;
	}

	FDreamUIAst AstWith(FDreamUINode InRoot)
	{
		FDreamUIAst Ast;
		Ast.ClassPath = TEXT("/Game/UI/WBP_BuilderTest");
		Ast.ClassPathLocation = At();
		Ast.Root = MoveTemp(InRoot);
		Ast.bHasRoot = true;
		return Ast;
	}

	/** The tree plus everything the build said about it, kept alive for the length of one test. */
	struct FBuildOutcome
	{
		TStrongObjectPtr<UDreamWidgetTree> Tree;
		FDreamUIDiagnosticBag Diagnostics;
		TArray<FDreamWidgetPropertyBinding> Bindings;

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

	FBuildOutcome BuildFrom(const FDreamUIAst& InAst)
	{
		FBuildOutcome Outcome;
		Outcome.Diagnostics.SourceName = TEXT("BuilderTest.dui");
		// The transient package rather than a world, on purpose: what the compiler builds is a class
		// template, and UDreamWidgetTree::GetWorld returning null is the defining property of one. A
		// test that handed the builder a world would not notice a dependency on having one.
		Outcome.Tree.Reset(FDreamUITextBuilder::Build(InAst, GetTransientPackage(), Outcome.Diagnostics, Outcome.Bindings));
		return Outcome;
	}

	const FDreamWidgetPropertyBinding* FindBinding(const FBuildOutcome& InOutcome, const TCHAR* InPropertyName)
	{
		return InOutcome.Bindings.FindByPredicate(
			[InPropertyName](const FDreamWidgetPropertyBinding& Binding) { return Binding.PropertyName == FName(InPropertyName); });
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUITextBuilderOuterTest,
	"DreamGUI.Text.EveryWidgetIsOuteredToTheTree",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUITextBuilderOuterTest::RunTest(const FString& Parameters)
{
	using namespace DreamUITextBuilderTestLocal;

	// Every shape the builder can create, in one tree: a plain widget, a widget with a visual, a
	// widget carrying a behaviour, a named slot, and a child two levels down. If any one of them
	// reached for NewObject instead of ConstructWidget, one of these outers would be wrong.
	FDreamUINode Root = MakeNode(TEXT("Widget"), TEXT("Root"));

	FDreamUINode Title = MakeNode(TEXT("Text"), TEXT("Title"));
	Root.Children.Add(Title);

	FDreamUINode Row = MakeNode(TEXT("Image"), TEXT("Row"));
	Row.Components.Add(MakeComponent(TEXT("Slider")));
	// Plain, not another visual: UDreamRectBlock::OnRegister check()s a settings asset and registers a
	// data texture, which is a project dependency this test has no business acquiring. The tag table
	// is covered on its own, without constructing anything.
	FDreamUINode Deep = MakeNode(TEXT("Widget"), TEXT("Deep"));
	Row.Children.Add(Deep);
	Root.Children.Add(Row);

	FDreamUINode Body;
	Body.Kind = EDreamUINodeKind::NamedSlot;
	Body.Id = TEXT("Body");
	Body.Location = At();
	Root.Children.Add(Body);

	const FBuildOutcome Outcome = BuildFrom(AstWith(Root));
	if (!TestNotNull(TEXT("the tree built"), Outcome.Tree.Get()))
	{
		return false;
	}
	TestFalse(TEXT("and built clean"), Outcome.Diagnostics.HasErrors());
	TestEqual(TEXT("with every node in it"), Outcome.Tree->CountWidgets(), 5);

	int32 Visited = 0;
	Outcome.Tree->ForEachWidget([this, &Outcome, &Visited](UDreamWidget* Widget)
	{
		Visited++;
		// THE assertion this file exists for. UIML outered widgets flat to the UWorld, which is why a
		// UIML hierarchy is owned by nothing and can never be a class template; see UDreamWidgetTree.
		TestEqual(*FString::Printf(TEXT("'%s' is outered to the tree"), *Widget->GetDisplayName()),
			Widget->GetOuter(), (UObject*)Outcome.Tree.Get());
	});
	TestEqual(TEXT("and the walk reached all of them"), Visited, 5);

	// A widget's sub-objects belong to the WIDGET, not to the tree -- that is what makes them come
	// across as fresh copies when the tree is instanced. Getting this backwards would still leave
	// every widget correctly outered above, so it is worth its own look.
	UDreamWidget* TitleWidget = Outcome.Find(TEXT("Title"));
	if (TestNotNull(TEXT("the Text node exists"), TitleWidget))
	{
		if (TestNotNull(TEXT("and has a visual"), TitleWidget->GetVisual()))
		{
			TestEqual(TEXT("whose outer is its widget"), TitleWidget->GetVisual()->GetOuter(), (UObject*)TitleWidget);
		}
	}
	UDreamWidget* RowWidget = Outcome.Find(TEXT("Row"));
	if (TestNotNull(TEXT("the behaviour host exists"), RowWidget) && RowWidget->GetAllComponents().Num() == 1)
	{
		TestEqual(TEXT("and its behaviour is outered to it"), RowWidget->GetAllComponents()[0]->GetOuter(), (UObject*)RowWidget);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUITextBuilderTagTableTest,
	"DreamGUI.Text.TheBuiltInTagTableNamesRealVisualClasses",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUITextBuilderTagTableTest::RunTest(const FString& Parameters)
{
	// Asked of the table directly, so every tag is covered without constructing the visual -- some of
	// them reach for project settings the moment they are registered, and the table is a claim about
	// names, not about whether a given project can draw them.
	struct FTagCase { const TCHAR* Tag; UClass* Expected; };
	const TArray<FTagCase> Cases =
	{
		{ TEXT("Widget"),             nullptr },
		{ TEXT("Image"),              UDreamImage::StaticClass() },
		{ TEXT("Text"),               UDreamText::StaticClass() },
		{ TEXT("Texture"),            UDreamTexture::StaticClass() },
		{ TEXT("Sprite"),             UDreamSprite::StaticClass() },
		{ TEXT("RectBlock"),          UDreamRectBlock::StaticClass() },
		{ TEXT("Empty"),              UDreamVisualEmpty::StaticClass() },
		{ TEXT("BackgroundBlur"),     UDreamBackgroundBlur::StaticClass() },
		{ TEXT("BackgroundPixelate"), UDreamBackgroundPixelate::StaticClass() },
		{ TEXT("PixelSort"),          UDreamPixelSort::StaticClass() },
	};

	for (const FTagCase& Case : Cases)
	{
		bool bIsKnown = false;
		UClass* Resolved = FDreamUITextBuilder::FindVisualClassForTag(Case.Tag, bIsKnown);
		TestTrue(*FString::Printf(TEXT("'%s' is a known tag"), Case.Tag), bIsKnown);
		TestEqual(*FString::Printf(TEXT("'%s' names its visual"), Case.Tag), Resolved, Case.Expected);
	}

	// 'Widget' returning null is a known tag with no visual, which is a different answer from an
	// unknown tag returning null -- collapsing the two is how a typo would silently build a blank.
	bool bIsKnown = true;
	TestNull(TEXT("an unknown tag resolves to nothing"), FDreamUITextBuilder::FindVisualClassForTag(TEXT("Blurble"), bIsKnown));
	TestFalse(TEXT("and reports itself unknown"), bIsKnown);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUITextBuilderWidgetPropertyTest,
	"DreamGUI.Text.ABareNameWritesTheWidgetsOwnProperty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUITextBuilderWidgetPropertyTest::RunTest(const FString& Parameters)
{
	using namespace DreamUITextBuilderTestLocal;

	FDreamUINode Root = MakeNode(TEXT("Widget"), TEXT("Root"));
	Root.Properties.Add(AssignNumber(TEXT("RenderOpacity"), TEXT("0.5")));
	Root.Properties.Add(AssignIdentifier(TEXT("Visibility"), TEXT("Collapsed")));

	const FBuildOutcome Outcome = BuildFrom(AstWith(Root));
	if (!TestNotNull(TEXT("the tree built"), Outcome.Root()))
	{
		return false;
	}
	TestEqual(TEXT("the float landed on the widget"), Outcome.Root()->GetRenderOpacity(), 0.5f);
	TestEqual(TEXT("and the enum was read by name, not by number"),
		Outcome.Root()->GetVisibility(), EDreamWidgetVisibility::Collapsed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUITextBuilderVisualPropertyTest,
	"DreamGUI.Text.ABareNameFallsThroughToTheVisual",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUITextBuilderVisualPropertyTest::RunTest(const FString& Parameters)
{
	using namespace DreamUITextBuilderTestLocal;

	// Both destinations on ONE node, which is the point: the author writes a flat list and does not
	// say which object each name belongs to, so the fallback order is the whole behaviour under test.
	FDreamUINode Root = MakeNode(TEXT("Text"), TEXT("Label"));
	Root.Properties.Add(AssignNumber(TEXT("FontSize"), TEXT("24")));
	Root.Properties.Add(AssignNumber(TEXT("RenderOpacity"), TEXT("0.25")));

	const FBuildOutcome Outcome = BuildFrom(AstWith(Root));
	if (!TestNotNull(TEXT("the tree built"), Outcome.Root()))
	{
		return false;
	}
	UDreamText* Visual = Cast<UDreamText>(Outcome.Root()->GetVisual());
	if (!TestNotNull(TEXT("the Text tag created a UDreamText"), Visual))
	{
		return false;
	}
	TestEqual(TEXT("FontSize, which only the visual has, went to the visual"), Visual->GetFontSize(), 24.0f);
	TestEqual(TEXT("RenderOpacity, which the widget has, went to the widget"), Outcome.Root()->GetRenderOpacity(), 0.25f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUITextBuilderBehaviourPropertyTest,
	"DreamGUI.Text.ComponentPropertiesAreWrittenOnTheBehaviour",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUITextBuilderBehaviourPropertyTest::RunTest(const FString& Parameters)
{
	using namespace DreamUITextBuilderTestLocal;

	FDreamUINode Root = MakeNode(TEXT("Widget"), TEXT("Root"));
	// Spelt without its U or its UI prefix, which is how ResolveComponentClass is meant to be used
	// and the reason there is no alias table to keep in step with the library.
	FDreamUIComponent Slider = MakeComponent(TEXT("Slider"));
	Slider.Properties.Add(AssignNumber(TEXT("MaxValue"), TEXT("8")));
	Root.Components.Add(Slider);

	const FBuildOutcome Outcome = BuildFrom(AstWith(Root));
	if (!TestNotNull(TEXT("the tree built"), Outcome.Root()))
	{
		return false;
	}
	UUISlider* Behaviour = Outcome.Root()->GetComponent<UUISlider>();
	if (!TestNotNull(TEXT("'+ Slider' attached a UUISlider"), Behaviour))
	{
		return false;
	}
	TestEqual(TEXT("and the property went onto it, not onto the widget"), Behaviour->GetMaxValue(), 8.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUITextBuilderPanelSlotPropertyTest,
	"DreamGUI.Text.SlotPropertiesAreWrittenOnThePanelSlot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUITextBuilderPanelSlotPropertyTest::RunTest(const FString& Parameters)
{
	using namespace DreamUITextBuilderTestLocal;

	// The parent has to lay out panel slots for the child to have one at all, and the only way a .dui
	// can say so is a layout container on the parent.
	FDreamUINode Root = MakeNode(TEXT("Widget"), TEXT("Root"));
	Root.Components.Add(MakeComponent(TEXT("VerticalBox")));

	FDreamUINode Cell = MakeNode(TEXT("Widget"), TEXT("Cell"));
	Cell.SlotProperties.Add(AssignNumber(TEXT("ZOrder"), TEXT("3")));
	Cell.SlotProperties.Add(Assign(TEXT("Padding"), TupleOf({ TEXT("8"), TEXT("4"), TEXT("8"), TEXT("4") })));
	// Written on the CHILD, and a bare name on the same node still means the child widget. Both
	// destinations in one node is the only way to see that @slot does not simply shadow everything.
	Cell.Properties.Add(AssignNumber(TEXT("RenderOpacity"), TEXT("0.75")));
	Root.Children.Add(Cell);

	const FBuildOutcome Outcome = BuildFrom(AstWith(Root));
	if (!TestNotNull(TEXT("the tree built"), Outcome.Root()))
	{
		return false;
	}
	UDreamWidget* CellWidget = Outcome.Find(TEXT("Cell"));
	if (!TestNotNull(TEXT("the child exists"), CellWidget))
	{
		return false;
	}
	UDreamPanelSlot* Slot = CellWidget->GetPanelSlot();
	if (!TestNotNull(TEXT("a panel layout on the parent gave the child a slot"), Slot))
	{
		return false;
	}
	TestEqual(TEXT("the int went onto the slot"), Slot->ZOrder, 3);
	TestEqual(TEXT("the margin short form went onto the slot"), Slot->Padding, FMargin(8, 4, 8, 4));
	TestEqual(TEXT("and the bare name still meant the widget"), CellWidget->GetRenderOpacity(), 0.75f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUITextBuilderDottedPathTest,
	"DreamGUI.Text.ADottedPathWritesTheLeafInsideTheStruct",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUITextBuilderDottedPathTest::RunTest(const FString& Parameters)
{
	using namespace DreamUITextBuilderTestLocal;

	FDreamUINode Root = MakeNode(TEXT("Widget"), TEXT("Root"));
	Root.Properties.Add(Assign(TEXT("AnchorData.SizeDelta"), TupleOf({ TEXT("400"), TEXT("240") })));
	// Two segments deep, and onto a leaf with no short form -- the drill and the write are separate
	// pieces of machinery and one working does not imply the other.
	Root.Properties.Add(AssignNumber(TEXT("AnchorData.AnchoredPosition.X"), TEXT("12")));

	const FBuildOutcome Outcome = BuildFrom(AstWith(Root));
	if (!TestNotNull(TEXT("the tree built"), Outcome.Root()))
	{
		return false;
	}
	const FDreamUIAnchorData& AnchorData = Outcome.Root()->GetAnchorData();
	TestEqual(TEXT("the tuple reached the struct field"), AnchorData.SizeDelta, FVector2D(400.0, 240.0));
	TestEqual(TEXT("and a two-segment path reached the field inside that"), AnchorData.AnchoredPosition.X, 12.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUITextBuilderStyleOrderTest,
	"DreamGUI.Text.AStyleIsAppliedBeforeTheNodesOwnProperties",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUITextBuilderStyleOrderTest::RunTest(const FString& Parameters)
{
	using namespace DreamUITextBuilderTestLocal;

	FDreamUINode Root = MakeNode(TEXT("Text"), TEXT("Label"));
	Root.StyleName = TEXT("Card");
	Root.Properties.Add(AssignNumber(TEXT("RenderOpacity"), TEXT("0.75")));

	FDreamUIAst Ast = AstWith(Root);
	FDreamUIStyle Style;
	Style.Name = TEXT("Card");
	Style.Location = At();
	// One property the node also sets, and one it does not. Only having the first would pass equally
	// well if the style were ignored entirely.
	Style.Properties.Add(AssignNumber(TEXT("RenderOpacity"), TEXT("0.25")));
	Style.Properties.Add(AssignNumber(TEXT("FontSize"), TEXT("30")));
	Ast.Styles.Add(Style);

	const FBuildOutcome Outcome = BuildFrom(Ast);
	if (!TestNotNull(TEXT("the tree built"), Outcome.Root()))
	{
		return false;
	}
	TestEqual(TEXT("the node's own value won over the style's"), Outcome.Root()->GetRenderOpacity(), 0.75f);
	UDreamText* Visual = Cast<UDreamText>(Outcome.Root()->GetVisual());
	if (TestNotNull(TEXT("the visual exists"), Visual))
	{
		TestEqual(TEXT("and the style's other property survived, on the visual"), Visual->GetFontSize(), 30.0f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUITextBuilderBindingTest,
	"DreamGUI.Text.ABindingRecordsTheWidgetNameAndTheSetter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUITextBuilderBindingTest::RunTest(const FString& Parameters)
{
	using namespace DreamUITextBuilderTestLocal;

	FDreamUINode Root = MakeNode(TEXT("Text"), TEXT("Label"));
	Root.Properties.Add(BindTo(TEXT("Text"), TEXT("GetTitleText")));
	Root.Properties.Add(BindTo(TEXT("RenderOpacity"), TEXT("GetFade")));
	FDreamUIComponent Slider = MakeComponent(TEXT("Slider"));
	Slider.Properties.Add(BindTo(TEXT("Value"), TEXT("GetAmount")));
	Root.Components.Add(Slider);

	const FBuildOutcome Outcome = BuildFrom(AstWith(Root));
	if (!TestNotNull(TEXT("the tree built"), Outcome.Root()))
	{
		return false;
	}
	TestFalse(TEXT("and raised nothing -- the bound functions are not checkable yet, by design"),
		Outcome.Diagnostics.HasErrors());
	if (!TestEqual(TEXT("three bindings came back"), Outcome.Bindings.Num(), 3))
	{
		return false;
	}

	const FDreamWidgetPropertyBinding* TextBinding = FindBinding(Outcome, TEXT("Text"));
	if (TestNotNull(TEXT("the visual's Text is bound"), TextBinding))
	{
		// Asserted against MakeWidgetVariableName rather than against the literal "Label", because
		// the contract is that the compiler and the runtime agree with THAT function -- a hard-coded
		// expectation here would keep passing while a private second copy drifted away from it.
		TestEqual(TEXT("named by the same function the runtime resolves with"),
			TextBinding->WidgetName, UDreamWidgetTree::MakeWidgetVariableName(Outcome.Root()));
		TestEqual(TEXT("and that is the node's id"), TextBinding->WidgetName, FName(TEXT("Label")));
		TestEqual(TEXT("pointed at the visual"), TextBinding->Target, EDreamWidgetBindingTarget::Visual);
		TestEqual(TEXT("with no behaviour index"), TextBinding->BehaviourIndex, (int32)INDEX_NONE);
		TestEqual(TEXT("through UDreamText::SetText"), TextBinding->SetterName, FName(TEXT("SetText")));
		TestEqual(TEXT("driven by the named function"), TextBinding->FunctionName, FName(TEXT("GetTitleText")));
	}

	const FDreamWidgetPropertyBinding* OpacityBinding = FindBinding(Outcome, TEXT("RenderOpacity"));
	if (TestNotNull(TEXT("the widget's RenderOpacity is bound"), OpacityBinding))
	{
		TestEqual(TEXT("pointed at the widget"), OpacityBinding->Target, EDreamWidgetBindingTarget::Widget);
		TestEqual(TEXT("through SetRenderOpacity"), OpacityBinding->SetterName, FName(TEXT("SetRenderOpacity")));
	}

	const FDreamWidgetPropertyBinding* SliderBinding = FindBinding(Outcome, TEXT("Value"));
	if (TestNotNull(TEXT("the behaviour's Value is bound"), SliderBinding))
	{
		TestEqual(TEXT("pointed at a behaviour"), SliderBinding->Target, EDreamWidgetBindingTarget::Behaviour);
		// By position, which is what FDreamWidgetPropertyBinding stores and what the instanced copy
		// will be found by -- names do not survive instancing.
		TestEqual(TEXT("by its position in the component array"), SliderBinding->BehaviourIndex, 0);
		TestEqual(TEXT("through SetValue"), SliderBinding->SetterName, FName(TEXT("SetValue")));
	}

	// The binding wrote nothing: a bound property keeps its default until the first evaluation, and a
	// builder that also assigned would produce a frame of the wrong value on every instance.
	if (UDreamText* Visual = Cast<UDreamText>(Outcome.Root()->GetVisual()))
	{
		TestEqual(TEXT("and binding left the property at its default"), Visual->GetText().ToString(), FString(TEXT("New Text")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUITextBuilderNamedSlotTest,
	"DreamGUI.Text.ANamedSlotNodeCarriesADreamNamedSlot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUITextBuilderNamedSlotTest::RunTest(const FString& Parameters)
{
	using namespace DreamUITextBuilderTestLocal;

	FDreamUINode Root = MakeNode(TEXT("Widget"), TEXT("Root"));
	FDreamUINode Slot;
	Slot.Kind = EDreamUINodeKind::NamedSlot;
	Slot.Id = TEXT("Body");
	Slot.Location = At();
	Root.Children.Add(Slot);

	const FBuildOutcome Outcome = BuildFrom(AstWith(Root));
	if (!TestNotNull(TEXT("the tree built"), Outcome.Root()))
	{
		return false;
	}
	UDreamWidget* SlotWidget = Outcome.Find(TEXT("Body"));
	if (!TestNotNull(TEXT("the slot node became a widget"), SlotWidget))
	{
		return false;
	}
	UDreamNamedSlot* NamedSlot = SlotWidget->GetComponent<UDreamNamedSlot>();
	if (!TestNotNull(TEXT("carrying a UDreamNamedSlot"), NamedSlot))
	{
		return false;
	}
	// The slot is named by the widget's display name; the language has no separate slot-name field,
	// and giving it one would be a second identity to keep in step with the id.
	TestEqual(TEXT("whose name is the node's id"), NamedSlot->GetSlotName(), FName(TEXT("Body")));

	TArray<FName> Declared;
	UDreamUserWidget::CollectDeclaredSlotNames(Outcome.Tree.Get(), Declared);
	TestTrue(TEXT("and the tree declares it, which is what a host fills"), Declared.Contains(FName(TEXT("Body"))));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUITextBuilderLocalizableTextTest,
	"DreamGUI.Text.AQuotedStringOnAnFTextIsLocalizable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUITextBuilderLocalizableTextTest::RunTest(const FString& Parameters)
{
	using namespace DreamUITextBuilderTestLocal;

	FDreamUINode Root = MakeNode(TEXT("Text"), TEXT("Title"));
	// One FText on the visual and one on the widget, both undiscriminated. That sharing is the
	// decision under test as much as the key format is: keys are read by humans in a spreadsheet, and
	// a node with one string does not need to be told which of its two objects holds it. Behaviours
	// and panel slots DO get a discriminator, but nothing in the library declares an FText on either,
	// so those branches have no property to point a test at.
	Root.Properties.Add(AssignString(TEXT("Text"), TEXT("Save")));
	Root.Properties.Add(AssignString(TEXT("AccessibleText"), TEXT("Save button")));
	Root.Properties.Add(AssignString(TEXT("ToolTipText"), TEXT("Write the file"), TEXT("tooltip.save")));

	const FBuildOutcome Outcome = BuildFrom(AstWith(Root));
	if (!TestNotNull(TEXT("the tree built"), Outcome.Root()))
	{
		return false;
	}
	UDreamText* Visual = Cast<UDreamText>(Outcome.Root()->GetVisual());
	if (!TestNotNull(TEXT("the visual exists"), Visual))
	{
		return false;
	}

	TestEqual(TEXT("the source string is what the author wrote"), Visual->GetText().ToString(), FString(TEXT("Save")));
	const TOptional<FString> Namespace = FTextInspector::GetNamespace(Visual->GetText());
	const TOptional<FString> Key = FTextInspector::GetKey(Visual->GetText());
	if (TestTrue(TEXT("and it carries a namespace, so the gatherer can see it"), Namespace.IsSet()))
	{
		// The class path, not the file name: a .dui renamed on disk must not orphan its translations.
		TestEqual(TEXT("which is the class the file compiles into"), Namespace.GetValue(), FString(TEXT("/Game/UI/WBP_BuilderTest")));
	}
	if (TestTrue(TEXT("and a key"), Key.IsSet()))
	{
		TestEqual(TEXT("built from the node id and the property"), Key.GetValue(), FString(TEXT("Title.Text")));
	}

	const TOptional<FString> WidgetKey = FTextInspector::GetKey(Outcome.Root()->GetAccessibleText());
	if (TestTrue(TEXT("the widget's own FText has a key"), WidgetKey.IsSet()))
	{
		// Short too, and that is the point: the widget and its visual share one key space on purpose.
		TestEqual(TEXT("in the same short form as the visual's"), WidgetKey.GetValue(), FString(TEXT("Title.AccessibleText")));
	}

	const TOptional<FString> OverriddenKey = FTextInspector::GetKey(Outcome.Root()->GetToolTipText());
	if (TestTrue(TEXT("the overridden entry has a key too"), OverriddenKey.IsSet()))
	{
		TestEqual(TEXT("and @key wins over the derived one"), OverriddenKey.GetValue(), FString(TEXT("tooltip.save")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUITextBuilderLoopSkippedTest,
	"DreamGUI.Text.LoopsAreSkippedWithAWarningAndTheRestStillBuilds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUITextBuilderLoopSkippedTest::RunTest(const FString& Parameters)
{
	using namespace DreamUITextBuilderTestLocal;

	FDreamUINode Root = MakeNode(TEXT("Widget"), TEXT("Root"));

	FDreamUINode Loop;
	Loop.Kind = EDreamUINodeKind::ForLoop;
	Loop.LoopVariable = TEXT("Item");
	Loop.LoopSourceFunction = TEXT("GetItems");
	Loop.Location = At();
	Loop.Children.Add(MakeNode(TEXT("Text"), TEXT("Entry")));
	Root.Children.Add(Loop);

	Root.Children.Add(MakeNode(TEXT("Image"), TEXT("Footer")));

	const FBuildOutcome Outcome = BuildFrom(AstWith(Root));
	if (!TestNotNull(TEXT("the tree still built"), Outcome.Root()))
	{
		return false;
	}
	// A warning, not an error: the author previewing a screen wants the parts that do work, and
	// failing the whole build over an unimplemented stage would hide them.
	TestFalse(TEXT("with no error"), Outcome.Diagnostics.HasErrors());
	TestEqual(TEXT("but one diagnostic"), Outcome.Diagnostics.Diagnostics.Num(), 1);
	if (Outcome.Diagnostics.Diagnostics.Num() == 1)
	{
		TestEqual(TEXT("which is a warning"), Outcome.Diagnostics.Diagnostics[0].Severity, EDreamUISeverity::Warning);
		// Its own code, so the docs page for it can say "temporary" and so retiring loops means
		// retiring one number rather than hunting for which UnknownNodeType sites meant this.
		TestEqual(TEXT("under the code that says why"), Outcome.Diagnostics.Diagnostics[0].Code,
			EDreamUIDiagnosticCode::LoopNotExpanded);
	}
	TestEqual(TEXT("the loop contributed nothing"), Outcome.Tree->CountWidgets(), 2);
	TestNull(TEXT("not even its body"), Outcome.Find(TEXT("Entry")));
	TestNotNull(TEXT("and its sibling built normally"), Outcome.Find(TEXT("Footer")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUITextBuilderDiagnosticCodesTest,
	"DreamGUI.Text.EachRefusalIsReportedUnderItsOwnCode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUITextBuilderDiagnosticCodesTest::RunTest(const FString& Parameters)
{
	using namespace DreamUITextBuilderTestLocal;

	// One negative per cause, arranged rather than asserted in place, because the interesting content
	// of each case is a single line and thirteen near-identical test bodies would bury it.
	struct FCase
	{
		const TCHAR* What;
		EDreamUIDiagnosticCode Code;
		TFunction<void(FDreamUIAst&)> Arrange;
	};

	const TArray<FCase> Cases =
	{
		{
			TEXT("a tag that is neither built in nor a path"), EDreamUIDiagnosticCode::UnknownNodeType,
			[](FDreamUIAst& Ast) { Ast.Root.TypeName = TEXT("Bogus"); }
		},
		{
			TEXT("an asset path that does not resolve"), EDreamUIDiagnosticCode::AssetNotFound,
			[](FDreamUIAst& Ast) { Ast.Root.TypeName = TEXT("/Game/DreamGUITests/NoSuchWidgetAsset"); }
		},
		{
			TEXT("a path to something that is not a user widget"), EDreamUIDiagnosticCode::NotAUserWidgetClass,
			[](FDreamUIAst& Ast) { Ast.Root.TypeName = TEXT("/Script/DreamGUI.DreamWidget"); }
		},
		{
			TEXT("a property no object on the node has"), EDreamUIDiagnosticCode::UnknownProperty,
			[](FDreamUIAst& Ast) { Ast.Root.Properties.Add(AssignNumber(TEXT("Wibble"), TEXT("1"))); }
		},
		{
			// The same shape of mistake, told apart by whether some OTHER tag would have had it. This
			// is the one an author actually makes: the right property name on the wrong node type.
			TEXT("a visual's property on a node with no visual"), EDreamUIDiagnosticCode::NoVisualForProperty,
			[](FDreamUIAst& Ast) { Ast.Root.Properties.Add(AssignNumber(TEXT("FontSize"), TEXT("24"))); }
		},
		{
			TEXT("a dotted path whose tail is not a field"), EDreamUIDiagnosticCode::UnknownPropertyPathSegment,
			[](FDreamUIAst& Ast) { Ast.Root.Properties.Add(AssignNumber(TEXT("AnchorData.Nope"), TEXT("1"))); }
		},
		{
			TEXT("a hex colour written on a float"), EDreamUIDiagnosticCode::ValueTypeMismatch,
			[](FDreamUIAst& Ast)
			{
				Ast.Root.Properties.Add(Assign(TEXT("RenderOpacity"), Literal(EDreamUIValueKind::HexColor, TEXT("FF0000"))));
			}
		},
		{
			TEXT("a three-element tuple on a two-element destination"), EDreamUIDiagnosticCode::TupleArityMismatch,
			[](FDreamUIAst& Ast)
			{
				Ast.Root.Properties.Add(Assign(TEXT("AnchorData.SizeDelta"),
					TupleOf({ TEXT("1"), TEXT("2"), TEXT("3") })));
			}
		},
		{
			TEXT("an identifier the enum does not declare"), EDreamUIDiagnosticCode::UnknownEnumValue,
			[](FDreamUIAst& Ast) { Ast.Root.Properties.Add(AssignIdentifier(TEXT("Visibility"), TEXT("Sideways"))); }
		},
		{
			// FlattenHierarchyIndex is transient with NO setter: a value written to it would read back
			// for the rest of the build and be gone the moment the class was saved -- the silent
			// failure the whole text pipeline exists to make impossible. AnimatableWidth held this
			// role until transients WITH a native setter became writable; its setter derives
			// AnchorData, so what is written to it does survive -- the same rule that lets
			// RelativeRotationEuler carry a rotation into the quaternion.
			TEXT("a transient property"), EDreamUIDiagnosticCode::PropertyNotWritable,
			[](FDreamUIAst& Ast) { Ast.Root.Properties.Add(AssignNumber(TEXT("FlattenHierarchyIndex"), TEXT("3"))); }
		},
		{
			TEXT("a style the file does not declare"), EDreamUIDiagnosticCode::UnknownStyle,
			[](FDreamUIAst& Ast) { Ast.Root.StyleName = TEXT("NoSuchStyle"); }
		},
		{
			TEXT("a '+' naming nothing attachable"), EDreamUIDiagnosticCode::UnknownBehaviourClass,
			[](FDreamUIAst& Ast) { Ast.Root.Components.Add(MakeComponent(TEXT("NotAThingAtAll"))); }
		},
		{
			TEXT("@slot under a parent that lays out no slots"), EDreamUIDiagnosticCode::NoPanelSlotForProperty,
			[](FDreamUIAst& Ast)
			{
				FDreamUINode Child = MakeNode(TEXT("Widget"), TEXT("Cell"));
				Child.SlotProperties.Add(AssignNumber(TEXT("ZOrder"), TEXT("1")));
				Ast.Root.Children.Add(Child);
			}
		},
		{
			// HAlign's setter is spelt SetParagraphHorizontalAlignment, so there is no SetHAlign for a
			// binding to go through -- exactly the case FindDreamWidgetSetterFor exists to answer, and
			// the only one of the three binding refusals that 5005 is still for.
			TEXT("a binding onto a property with no setter"), EDreamUIDiagnosticCode::BindingTargetHasNoSetter,
			[](FDreamUIAst& Ast)
			{
				Ast.Root.TypeName = TEXT("Text");
				Ast.Root.Properties.Add(BindTo(TEXT("HAlign"), TEXT("GetAlignment")));
			}
		},
		{
			// 5008, not 5005: writing a setter would not help, the struct field has nowhere to be
			// recorded in FDreamWidgetPropertyBinding at all.
			TEXT("a binding onto a field inside a struct"), EDreamUIDiagnosticCode::BindingTargetNotSupported,
			[](FDreamUIAst& Ast) { Ast.Root.Properties.Add(BindTo(TEXT("AnchorData.SizeDelta"), TEXT("GetSize"))); }
		},
		{
			// The other half of 5008: the destination OBJECT is unnameable. UDreamPanelSlot has a
			// perfectly good SetZOrder and it still cannot be bound.
			TEXT("a binding onto a panel slot"), EDreamUIDiagnosticCode::BindingTargetNotSupported,
			[](FDreamUIAst& Ast)
			{
				Ast.Root.Components.Add(MakeComponent(TEXT("VerticalBox")));
				FDreamUINode Child = MakeNode(TEXT("Widget"), TEXT("Cell"));
				Child.SlotProperties.Add(BindTo(TEXT("ZOrder"), TEXT("GetOrder")));
				Ast.Root.Children.Add(Child);
			}
		},
		{
			// UDreamNamedSlot caps its widget at one child, and the plain attach path drops the second
			// without a word -- a tree that is quietly missing a subtree the author can see in the file.
			TEXT("a second child under a one-child parent"), EDreamUIDiagnosticCode::ParentRefusedChild,
			[](FDreamUIAst& Ast)
			{
				FDreamUINode Slot;
				Slot.Kind = EDreamUINodeKind::NamedSlot;
				Slot.Id = TEXT("Body");
				Slot.Location = At();
				Slot.Children.Add(MakeNode(TEXT("Widget"), TEXT("First")));
				Slot.Children.Add(MakeNode(TEXT("Widget"), TEXT("Second")));
				Ast.Root.Children.Add(Slot);
			}
		},
		{
			// Still UnknownProperty even though the message is special-cased: 'Width' is a details
			// panel label, and the hint table changes the advice without inventing a second name.
			TEXT("a details-panel label written as a property"), EDreamUIDiagnosticCode::UnknownProperty,
			[](FDreamUIAst& Ast) { Ast.Root.Properties.Add(AssignNumber(TEXT("Width"), TEXT("400"))); }
		},
	};

	for (const FCase& Case : Cases)
	{
		FDreamUIAst Ast = AstWith(MakeNode(TEXT("Widget"), TEXT("Root")));
		Case.Arrange(Ast);

		const FBuildOutcome Outcome = BuildFrom(Ast);
		TestTrue(*FString::Printf(TEXT("%s is reported under DUI%d"), Case.What, (int32)Case.Code),
			Outcome.HasCode(Case.Code));
		// The tree is withheld as well as reported. Handing back a half-built tree next to an error
		// invites a caller to use it, and a class compiled from one is worse than no class.
		TestNull(*FString::Printf(TEXT("%s produces no tree"), Case.What), Outcome.Tree.Get());
	}

	// The one case that is not a node at all: nothing to build. NothingToBuild rather than EmptyTree,
	// which is 6xxx and belongs to the stage that attaches a tree to a blueprint, not to this one.
	FDreamUIAst Empty;
	Empty.ClassPathLocation = At();
	const FBuildOutcome EmptyOutcome = BuildFrom(Empty);
	TestNull(TEXT("an AST with no root produces no tree"), EmptyOutcome.Tree.Get());
	TestTrue(TEXT("and says so"), EmptyOutcome.HasCode(EDreamUIDiagnosticCode::NothingToBuild));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUITextBuilderDiagnosticBagIsAppendedTest,
	"DreamGUI.Text.ABuildJudgesOnlyTheErrorsItRaisedItself",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUITextBuilderDiagnosticBagIsAppendedTest::RunTest(const FString& Parameters)
{
	using namespace DreamUITextBuilderTestLocal;

	// A caller collecting several files into one bag is the normal case, and a builder that answered
	// "did this work" by looking at HasErrors would refuse to build the second file in every project
	// whose first file had a typo.
	FDreamUIDiagnosticBag Bag;
	Bag.SourceName = TEXT("Earlier.dui");
	Bag.AddError(EDreamUIDiagnosticCode::UnexpectedToken, At(3), TEXT("something the parser did not like"));

	TArray<FDreamWidgetPropertyBinding> Bindings;
	Bindings.Add(FDreamWidgetPropertyBinding());

	FDreamUINode Root = MakeNode(TEXT("Text"), TEXT("Label"));
	Root.Properties.Add(BindTo(TEXT("Text"), TEXT("GetTitleText")));

	TStrongObjectPtr<UDreamWidgetTree> Tree(
		FDreamUITextBuilder::Build(AstWith(Root), GetTransientPackage(), Bag, Bindings));

	TestNotNull(TEXT("a clean build still returns its tree"), Tree.Get());
	TestEqual(TEXT("the earlier diagnostic is untouched"), Bag.Diagnostics.Num(), 1);
	TestEqual(TEXT("and bindings are appended, not replaced"), Bindings.Num(), 2);
	return true;
}

#endif
