// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Controls/DreamButton.h"
#include "Controls/DreamRingMenu.h"
#include "Core/Components/DreamLayerStyle.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetPropertyBinding.h"
#include "Core/DreamWidgetTree.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "Text/DreamUIAst.h"
#include "Text/DreamUIDiagnostics.h"
#include "Text/DreamUISourceFile.h"
#include "Text/DreamUITextBuilder.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

/*
 * Four spellings the demo screens asked for, read by the front end and built by the builder: `@tag` lines and the
 * lookup they feed (UDreamUserWidget::GetWidgetsByTag), `@slot Size`, a struct written as a map of its fields (a ring
 * menu's items), and a style clause naming several styles. Plus where a ring menu puts a template's content.
 *
 * As in the other syntax tests, a refusal is asserted by its code and its count, never by its words.
 */
namespace DreamUITagsAndShorthandsTestLocal
{
	FString Lines(const TArray<FString>& InLines)
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

	bool ParseText(const FString& InText, FDreamUIAst& OutAst, FDreamUIDiagnosticBag& OutDiagnostics)
	{
		OutDiagnostics.SourceName = TEXT("Tags.dui");
		return FDreamUISourceFile::Parse(InText, OutDiagnostics.SourceName, OutAst, OutDiagnostics);
	}

	const FDreamUINode* FindNode(const FDreamUIAst& InAst, const FString& InId)
	{
		const FDreamUINode* Found = nullptr;
		InAst.ForEachNode([&Found, &InId](const FDreamUINode& InNode)
		{
			if (Found == nullptr && InNode.Id == InId)
			{
				Found = &InNode;
			}
		});
		return Found;
	}

	TArray<FString> TagNames(const FDreamUINode& InNode)
	{
		TArray<FString> Names;
		for (const FDreamUITag& Tag : InNode.Tags)
		{
			Names.Add(Tag.Name);
		}
		return Names;
	}

	/** A file parsed and, when the parse holds, built. */
	struct FBuilt
	{
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		TStrongObjectPtr<UDreamWidgetTree> Tree;
		TArray<FDreamWidgetPropertyBinding> Bindings;

		explicit FBuilt(const FString& InText)
		{
			if (ParseText(InText, Ast, Diagnostics))
			{
				Tree.Reset(FDreamUITextBuilder::Build(Ast, GetTransientPackage(), Diagnostics, Bindings));
			}
		}

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
	};

	/** A game world for the length of a test, for what needs one to instance a user widget. */
	struct FScopedWorld
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);

		FScopedWorld()
		{
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		}

		~FScopedWorld()
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	};

	float ReadFloat(const UObject* InObject, const TCHAR* InName)
	{
		const FFloatProperty* Property = InObject != nullptr ? FindFProperty<FFloatProperty>(InObject->GetClass(), InName) : nullptr;
		return Property != nullptr ? Property->GetPropertyValue_InContainer(InObject) : -1.0f;
	}

	TArray<FString> DisplayNamesOf(const TArray<UDreamWidget*>& InWidgets)
	{
		TArray<FString> Names;
		for (const UDreamWidget* Widget : InWidgets)
		{
			Names.Add(Widget != nullptr ? Widget->GetDisplayName() : FString(TEXT("<null>")));
		}
		return Names;
	}
}

// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUITagLineParseTest,
	"DreamGUI.Text.Syntax.ATagLineNamesItsWidgetAndANodeTypedByATagResourceStillReads",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUITagLineParseTest::RunTest(const FString& Parameters)
{
	using namespace DreamUITagsAndShorthandsTestLocal;
	{
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		const bool bParsed = ParseText(Lines({
			TEXT("Widget Root {"),
			TEXT("    Widget TrimA {"),
			TEXT("        @tag Trim"),
			TEXT("        @tag Glow, UI.Accent, 金"),
			TEXT("    }"),
			TEXT("    @tag RootTag"),
			TEXT("    @tag Holder {"),
			TEXT("    }"),
			TEXT("    @tag {"),
			TEXT("    }"),
			TEXT("}") }), Ast, Diagnostics);
		if (!TestTrue(*FString::Printf(TEXT("tags read clean (%s)"), *Diagnostics.ToString()), bParsed && Diagnostics.Diagnostics.IsEmpty()))
		{
			return false;
		}
		TestEqual(TEXT("the root's own line"), TagNames(Ast.Root), TArray<FString>({ TEXT("RootTag") }));
		const FDreamUINode* TrimA = FindNode(Ast, TEXT("TrimA"));
		if (TestNotNull(TEXT("the tagged node"), TrimA))
		{
			TestEqual(TEXT("every name of both lines, dotted and Chinese ones too"), TagNames(*TrimA),
				TArray<FString>({ TEXT("Trim"), TEXT("Glow"), TEXT("UI.Accent"), TEXT("金") }));
			TestEqual(TEXT("a name is located at itself"), TrimA->Tags[1].Location.Line, 4);
			TestEqual(TEXT("and its line at the '@'"), TrimA->Tags[1].LineLocation.Column, 9);
		}
		// `@tag Holder { }` and `@tag { }` go on meaning a node typed by a resource called tag.
		TestEqual(TEXT("the root still has its three child nodes"), Ast.Root.Children.Num(), 3);
		const FDreamUINode* Holder = FindNode(Ast, TEXT("Holder"));
		TestTrue(TEXT("'@tag Holder { }' is a node of the resource type"), Holder != nullptr && Holder->TypeName == TEXT("@tag"));
		TestTrue(TEXT("'@tag { }' an anonymous one"), Ast.Root.Children.Num() == 3 && Ast.Root.Children[2].bAnonymous
			&& Ast.Root.Children[2].TypeName == TEXT("@tag"));
	}

	// The shapes a tag line is not, each alone so its count is its own.
	for (const TCHAR* Line : { TEXT("@tag"), TEXT("@tag 3"), TEXT("@tag \"Trim\""), TEXT("@tag Trim Glow"), TEXT("@tag Trim,") })
	{
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		ParseText(Lines({ TEXT("Widget Root {"), FString(TEXT("    ")) + Line, TEXT("    Widget After { }"), TEXT("}") }), Ast, Diagnostics);
		TestEqual(*FString::Printf(TEXT("'%s' is one DUI2024 and nothing else (%s)"), Line, *Diagnostics.ToString()),
			Diagnostics.Diagnostics.Num() == 1 ? CountOf(Diagnostics, EDreamUIDiagnosticCode::MalformedTag) : -1, 1);
		TestEqual(*FString::Printf(TEXT("'%s' leaves the next node read"), Line), Ast.Root.Children.Num(), 1);
	}

	// Where a tag has no widget to name.
	{
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		ParseText(Lines({
			TEXT("style Card {"),
			TEXT("    @tag Trim"),
			TEXT("}"),
			TEXT("Widget Root {"),
			TEXT("    for Item in Items {"),
			TEXT("        @tag Row, Cell"),
			TEXT("        Widget Row { }"),
			TEXT("    }"),
			TEXT("    if Ready() {"),
			TEXT("        @tag Shown"),
			TEXT("        Widget Ok { }"),
			TEXT("    }"),
			TEXT("}") }), Ast, Diagnostics);
		TestEqual(*FString::Printf(TEXT("a style's and a loop's are DUI2024, one per name of a loop's line (%s)"), *Diagnostics.ToString()),
			CountOf(Diagnostics, EDreamUIDiagnosticCode::MalformedTag), 3);
		TestEqual(TEXT("an 'if' block's is the branch's DUI2018"), CountOf(Diagnostics, EDreamUIDiagnosticCode::MalformedConditional), 1);
		TestEqual(TEXT("and nothing else"), Diagnostics.Diagnostics.Num(), 4);
	}
	return true;
}

// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUITagLookupTest,
	"DreamGUI.Text.Syntax.TaggedWidgetsAreFoundInTheirOwnTreeAndNotInsideAComponent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUITagLookupTest::RunTest(const FString& Parameters)
{
	using namespace DreamUITagsAndShorthandsTestLocal;
	FScopedWorld TestWorld;

	FBuilt Built(Lines({
		TEXT("Widget Root {"),
		TEXT("    Widget TrimA {"),
		TEXT("        @tag Trim"),
		TEXT("        @tag Trim"),
		TEXT("    }"),
		TEXT("    Widget Box {"),
		TEXT("        Widget TrimB {"),
		TEXT("            Tags = [Sigil]"),
		TEXT("            @tag Trim, UI.Glow"),
		TEXT("        }"),
		TEXT("        Widget Plain { }"),
		TEXT("    }"),
		TEXT("    Native.Button Ok {"),
		TEXT("        Widget Label {"),
		TEXT("            @tag Trim"),
		TEXT("        }"),
		TEXT("    }"),
		TEXT("}") }));
	if (!TestTrue(*FString::Printf(TEXT("the file builds (%s)"), *Built.Diagnostics.ToString()), Built.Tree.IsValid()))
	{
		return false;
	}
	const UDreamWidget* TrimA = Built.Find(TEXT("TrimA"));
	const UDreamWidget* TrimB = Built.Find(TEXT("TrimB"));
	if (!TestNotNull(TEXT("TrimA"), TrimA) || !TestNotNull(TEXT("TrimB"), TrimB))
	{
		return false;
	}
	TestEqual(TEXT("a name said twice is one tag"), TrimA->Tags, TArray<FName>({ FName(TEXT("Trim")) }));
	TestEqual(TEXT("the line's tags join the node's own Tags line"), TrimB->Tags,
		TArray<FName>({ FName(TEXT("Sigil")), FName(TEXT("Trim")), FName(TEXT("UI.Glow")) }));
	TestTrue(TEXT("HasTag answers by name"), TrimB->HasTag(TEXT("ui.glow")) && !TrimB->HasTag(NAME_None) && !TrimB->HasTag(TEXT("Glow")));

	TStrongObjectPtr<UDreamUserWidget> Host(NewObject<UDreamUserWidget>(TestWorld.World));
	Host->InitializeFromArchetype(Built.Tree.Get());
	ON_SCOPE_EXIT
	{
		if (IsValid(Host.Get()))
		{
			Host->DestroyWidget();
		}
	};
	UDreamButton* Button = nullptr;
	Host->GetWidgetTree()->ForEachWidget([&Button](UDreamWidget* InWidget)
	{
		if (Button == nullptr)
		{
			Button = Cast<UDreamButton>(InWidget);
		}
	});
	if (!TestNotNull(TEXT("the button was instanced"), Button) || !TestTrue(TEXT("and built"), Button->IsInitialized()))
	{
		return false;
	}
	// One of the button's OWN parts wears the tag too: the lookup must not see it from the host.
	UDreamWidget* ButtonPart = Button->GetChildren().Num() > 0 ? Button->GetChildren()[0] : nullptr;
	if (!TestNotNull(TEXT("the button has parts of its own"), ButtonPart) || !TestTrue(TEXT("other than the host's label"),
		ButtonPart->GetDisplayName() != TEXT("Label")))
	{
		return false;
	}
	ButtonPart->AddTag(TEXT("Trim"));

	TestEqual(TEXT("the host finds its own, the label it put in the button's slot included, in tree order"),
		DisplayNamesOf(Host->GetWidgetsByTag(TEXT("Trim"))), TArray<FString>({ TEXT("TrimA"), TEXT("TrimB"), TEXT("Label") }));
	TestEqual(TEXT("a tag only one widget wears"), DisplayNamesOf(Host->GetWidgetsByTag(TEXT("UI.Glow"))), TArray<FString>({ TEXT("TrimB") }));
	TestEqual(TEXT("the button finds its own part, and not the host's label in its slot"),
		DisplayNamesOf(Button->GetWidgetsByTag(TEXT("Trim"))), TArray<FString>({ ButtonPart->GetDisplayName() }));
	TestTrue(TEXT("None finds nothing"), Host->GetWidgetsByTag(NAME_None).IsEmpty());
	return true;
}

// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUISlotSizeShorthandTest,
	"DreamGUI.Text.Syntax.ASlotSizeIsTheSmallestAndTheLargestSizeAtOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUISlotSizeShorthandTest::RunTest(const FString& Parameters)
{
	using namespace DreamUITagsAndShorthandsTestLocal;
	{
		FBuilt Built(Lines({
			TEXT("style Fixed {"),
			TEXT("    @slot Size = (10, 20)"),
			TEXT("}"),
			TEXT("Widget Root {"),
			TEXT("    + VerticalBox { }"),
			TEXT("    Widget Line {"),
			TEXT("        @slot Size = (120, 48)"),
			TEXT("    }"),
			TEXT("    Widget Block {"),
			TEXT("        @slot { size = (64, 64)  Padding = (4, 0, 4, 0) }"),
			TEXT("    }"),
			TEXT("    Widget Styled : Fixed { }"),
			TEXT("    Widget Override : Fixed {"),
			TEXT("        @slot MaxDesiredSize = (30, 40)"),
			TEXT("    }"),
			TEXT("}") }));
		if (!TestTrue(*FString::Printf(TEXT("the file builds clean (%s)"), *Built.Diagnostics.ToString()),
			Built.Tree.IsValid() && Built.Diagnostics.NumErrors() == 0))
		{
			return false;
		}
		const FDreamUINode* LineNode = FindNode(Built.Ast, TEXT("Line"));
		if (TestNotNull(TEXT("the line node"), LineNode) && TestEqual(TEXT("one Size is two slot lines"), LineNode->SlotProperties.Num(), 2))
		{
			TestTrue(TEXT("the smallest size, made"), LineNode->SlotProperties[0].Name == TEXT("MinDesiredSize") && LineNode->SlotProperties[0].bSynthesized);
			TestTrue(TEXT("the largest, made"), LineNode->SlotProperties[1].Name == TEXT("MaxDesiredSize") && LineNode->SlotProperties[1].bSynthesized);
			TestEqual(TEXT("with the one value"), LineNode->SlotProperties[1].Value.Raw, FString(TEXT("(120, 48)")));
		}
		auto SlotOf = [&Built](const TCHAR* InId) -> const UDreamPanelSlot*
		{
			const UDreamWidget* Widget = Built.Find(InId);
			return Widget != nullptr ? Widget->GetPanelSlot() : nullptr;
		};
		const UDreamPanelSlot* Line = SlotOf(TEXT("Line"));
		const UDreamPanelSlot* Block = SlotOf(TEXT("Block"));
		const UDreamPanelSlot* Styled = SlotOf(TEXT("Styled"));
		const UDreamPanelSlot* Override = SlotOf(TEXT("Override"));
		if (!TestNotNull(TEXT("Line's slot"), Line) || !TestNotNull(TEXT("Block's slot"), Block)
			|| !TestNotNull(TEXT("Styled's slot"), Styled) || !TestNotNull(TEXT("Override's slot"), Override))
		{
			return false;
		}
		TestEqual(TEXT("a line's Size is its minimum"), Line->MinDesiredSize, FVector2D(120, 48));
		TestEqual(TEXT("and its maximum"), Line->MaxDesiredSize, FVector2D(120, 48));
		TestEqual(TEXT("a block's, whatever the case of the name"), Block->MaxDesiredSize, FVector2D(64, 64));
		TestEqual(TEXT("beside the block's other lines"), Block->Padding, FMargin(4, 0, 4, 0));
		TestEqual(TEXT("a style's"), Styled->MinDesiredSize, FVector2D(10, 20));
		TestEqual(TEXT("and a node's own line still has the last word"), Override->MaxDesiredSize, FVector2D(30, 40));
		TestEqual(TEXT("over half of it"), Override->MinDesiredSize, FVector2D(10, 20));
	}
	{
		// A Size bound means nothing the shorthand can do, and says so rather than "no property named Size".
		FBuilt Built(Lines({
			TEXT("Widget Root {"),
			TEXT("    + VerticalBox { }"),
			TEXT("    Widget Line {"),
			TEXT("        @slot Size <- GetSize()"),
			TEXT("    }"),
			TEXT("}") }));
		TestEqual(*FString::Printf(TEXT("a bound Size is one DUI4001 (%s)"), *Built.Diagnostics.ToString()),
			CountOf(Built.Diagnostics, EDreamUIDiagnosticCode::UnknownProperty), 1);
	}
	return true;
}

// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIStructMapTest,
	"DreamGUI.Text.Syntax.AStructIsWrittenAsAMapOfItsFieldsAndARingMenusItemsReadSo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIStructMapTest::RunTest(const FString& Parameters)
{
	using namespace DreamUITagsAndShorthandsTestLocal;
	{
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		ParseText(Lines({ TEXT("Widget Root {"), TEXT("    Items = [{ \"Lamp\", Tag: Lamp }]"), TEXT("}") }), Ast, Diagnostics);
		const FDreamUIProperty* Items = Ast.Root.Properties.Num() == 1 ? &Ast.Root.Properties[0] : nullptr;
		if (TestNotNull(TEXT("the line"), Items) && TestEqual(TEXT("one element"), Items->Value.Items.Num(), 1))
		{
			const FDreamUIValue& Map = Items->Value.Items[0];
			TestTrue(TEXT("a map of two entries"), Map.Kind == EDreamUIValueKind::Map && Map.Keys.Num() == 2 && Map.Items.Num() == 2);
			TestTrue(TEXT("the first without a key"), Map.IsPositionalKey(0) && Map.Items[0].Raw == TEXT("Lamp"));
			TestTrue(TEXT("the second with one"), !Map.IsPositionalKey(1) && Map.Keys[1].Raw == TEXT("Tag"));
		}
		FDreamUIAst Later;
		FDreamUIDiagnosticBag LaterDiagnostics;
		ParseText(Lines({ TEXT("Widget Root {"), TEXT("    Items = [{ Tag: Lamp, \"Lamp\" }]"), TEXT("}") }), Later, LaterDiagnostics);
		TestEqual(TEXT("only the first entry may go without a key"),
			CountOf(LaterDiagnostics, EDreamUIDiagnosticCode::MalformedContainer), 1);
	}
	{
		FBuilt Built(Lines({
			TEXT("Widget Root {"),
			TEXT("    Native.RingMenu Wheel {"),
			TEXT("        Items = ["),
			TEXT("            { \"灯\", Tag: Lamp },"),
			TEXT("            { \"门\", Tag: Door, bEnabled: false, Weight: 2 },"),
			TEXT("            { Label: \"窗\", Color: #FF0000 },"),
			TEXT("        ]"),
			TEXT("    }"),
			TEXT("    Widget Shadowed {"),
			TEXT("        + DreamLayerStyle { Style.DropShadows = [{ Distance: 12 }] }"),
			TEXT("    }"),
			TEXT("}") }));
		if (!TestTrue(*FString::Printf(TEXT("the file builds clean (%s)"), *Built.Diagnostics.ToString()),
			Built.Tree.IsValid() && Built.Diagnostics.NumErrors() == 0))
		{
			return false;
		}
		const UDreamRingMenu* Wheel = Cast<UDreamRingMenu>(Built.Find(TEXT("Wheel")));
		if (!TestNotNull(TEXT("the wheel"), Wheel) || !TestEqual(TEXT("three items"), Wheel->Items.Num(), 3))
		{
			return false;
		}
		TestEqual(TEXT("the keyless entry is the label"), Wheel->Items[0].Label.ToString(), FString(TEXT("灯")));
		TestEqual(TEXT("a name field"), Wheel->Items[1].Tag, FName(TEXT("Door")));
		TestFalse(TEXT("a bool field"), Wheel->Items[1].bEnabled);
		TestEqual(TEXT("a number field"), Wheel->Items[1].Weight, 2.0f);
		TestTrue(TEXT("a field left out keeps its default"), Wheel->Items[0].bEnabled && Wheel->Items[0].Weight == 1.0f);
		TestEqual(TEXT("the label by its key"), Wheel->Items[2].Label.ToString(), FString(TEXT("窗")));
		TestEqual(TEXT("a colour field"), Wheel->Items[2].Color, FColor(255, 0, 0, 255));
		const TOptional<FString> FirstKey = FTextInspector::GetKey(Wheel->Items[0].Label);
		const TOptional<FString> SecondKey = FTextInspector::GetKey(Wheel->Items[1].Label);
		TestTrue(TEXT("a label is a localizable text with a key of its own"), FirstKey.IsSet() && SecondKey.IsSet()
			&& FirstKey.GetValue() != SecondKey.GetValue() && FirstKey.GetValue().Contains(TEXT("Items.Label")));

		UDreamWidget* Shadowed = Built.Find(TEXT("Shadowed"));
		const UDreamLayerStyle* LayerStyle = Shadowed != nullptr ? Shadowed->GetComponent<UDreamLayerStyle>() : nullptr;
		if (TestNotNull(TEXT("a struct with no primary field reads a map too"), LayerStyle))
		{
			TestTrue(TEXT("one shadow, 12 away"), LayerStyle->GetStyle().DropShadows.Num() == 1
				&& LayerStyle->GetStyle().DropShadows[0].Distance == 12.0f);
		}
	}
	{
		FBuilt Built(Lines({
			TEXT("Widget Root {"),
			TEXT("    Native.RingMenu Wheel {"),
			TEXT("        Items = [{ Nope: 1 }]"),
			TEXT("    }"),
			TEXT("    Widget Shadowed {"),
			TEXT("        + DreamLayerStyle { Style.DropShadows = [{ 12 }] }"),
			TEXT("    }"),
			TEXT("}") }));
		TestEqual(*FString::Printf(TEXT("a field the struct does not have is DUI4002 (%s)"), *Built.Diagnostics.ToString()),
			CountOf(Built.Diagnostics, EDreamUIDiagnosticCode::UnknownPropertyPathSegment), 1);
		TestEqual(TEXT("a keyless entry on a struct with no primary field is DUI4009"),
			CountOf(Built.Diagnostics, EDreamUIDiagnosticCode::ContainerTypeMismatch), 1);
	}
	return true;
}

// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUISeveralStylesTest,
	"DreamGUI.Text.Syntax.SeveralStylesApplyLeftToRightAndEachOnlyOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUISeveralStylesTest::RunTest(const FString& Parameters)
{
	using namespace DreamUITagsAndShorthandsTestLocal;
	{
		FBuilt Built(Lines({
			TEXT("style Base {"),                            //  1
			TEXT("    RenderOpacity = 0.1"),                 //  2
			TEXT("    Visibility = Hidden"),                 //  3
			TEXT("}"),                                       //  4
			TEXT("style A : Base { RenderOpacity = 0.2 }"),  //  5
			TEXT("style B : Base { Visibility = Collapsed }"), // 6
			TEXT("style X { RenderOpacity = 0.3 }"),         //  7
			TEXT("style AB : A : B { }"),                    //  8
			TEXT("style Redundant : A : Base { }"),          //  9
			TEXT("Widget Root {"),                           // 10
			TEXT("    Widget BothAB : A : B { }"),           // 11
			TEXT("    Widget BothBA : B : A { }"),           // 12
			TEXT("    Widget Composed : AB { }"),            // 13
			TEXT("    Widget LaterWins : A : X { }"),        // 14
			TEXT("    Widget EarlierLoses : X : A { }"),     // 15
			TEXT("    Widget Again : A : Base { }"),         // 16
			TEXT("    Widget Twice : B : A : B { }"),        // 17
			TEXT("    Widget Own : A : X { RenderOpacity = 0.9 }"), // 18
			TEXT("}") }));
		if (!TestTrue(*FString::Printf(TEXT("the file builds (%s)"), *Built.Diagnostics.ToString()),
			Built.Tree.IsValid() && Built.Diagnostics.NumErrors() == 0))
		{
			return false;
		}
		const FDreamUINode* BothAB = FindNode(Built.Ast, TEXT("BothAB"));
		TestTrue(TEXT("a node's clause keeps every name"), BothAB != nullptr && BothAB->StyleRefs.Num() == 2
			&& BothAB->StyleRefs[1].Name == TEXT("B") && BothAB->StyleRefs[1].Location.Line == 11);
		const FDreamUIStyle* AB = Built.Ast.FindStyle(TEXT("AB"));
		TestTrue(TEXT("and a style's"), AB != nullptr && AB->BaseRefs.Num() == 2 && AB->BaseRefs[0].Name == TEXT("A"));

		TArray<const FDreamUIStyle*> Chain;
		TestTrue(TEXT("a shared base is applied once, first"), Built.Ast.CollectStyleChain(BothAB->StyleRefs, Chain)
			&& Chain.Num() == 3 && Chain[0]->Name == TEXT("Base") && Chain[1]->Name == TEXT("A") && Chain[2]->Name == TEXT("B"));

		auto Opacity = [&Built](const TCHAR* InId) { return ReadFloat(Built.Find(InId), TEXT("RenderOpacity")); };
		auto Collapsed = [&Built](const TCHAR* InId)
		{
			const UDreamWidget* Widget = Built.Find(InId);
			return Widget != nullptr && Widget->GetVisibility() == EDreamWidgetVisibility::Collapsed;
		};
		TestEqual(TEXT("A's value over the base's"), Opacity(TEXT("BothAB")), 0.2f);
		TestTrue(TEXT("and B's, which A does not touch"), Collapsed(TEXT("BothAB")));
		TestEqual(TEXT("the other order, the same here"), Opacity(TEXT("BothBA")), 0.2f);
		TestTrue(TEXT("both of them"), Collapsed(TEXT("BothBA")));
		TestEqual(TEXT("a style of two styles is the two"), Opacity(TEXT("Composed")), 0.2f);
		TestTrue(TEXT("both halves"), Collapsed(TEXT("Composed")));
		TestEqual(TEXT("the later name wins"), Opacity(TEXT("LaterWins")), 0.3f);
		TestEqual(TEXT("so the earlier loses"), Opacity(TEXT("EarlierLoses")), 0.2f);
		TestEqual(TEXT("a base named again changes nothing"), Opacity(TEXT("Again")), 0.2f);
		TestEqual(TEXT("and the node's own line beats them all"), Opacity(TEXT("Own")), 0.9f);

		TArray<int32> WarnedLines;
		for (const FDreamUIDiagnostic& Diagnostic : Built.Diagnostics.Diagnostics)
		{
			if (Diagnostic.Code == EDreamUIDiagnosticCode::StyleAlreadyApplied)
			{
				TestFalse(TEXT("DUI3025 is a warning"), Diagnostic.IsError());
				WarnedLines.Add(Diagnostic.Location.Line);
			}
		}
		WarnedLines.Sort();
		TestEqual(TEXT("DUI3025 once per redundant name: the style's, and the two nodes'"), WarnedLines, TArray<int32>({ 9, 16, 17 }));
	}
	{
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		ParseText(Lines({
			TEXT("style A { }"),
			TEXT("Widget Root {"),
			TEXT("    Widget Two : Nope : A : Gone { }"),
			TEXT("}") }), Ast, Diagnostics);
		TestEqual(TEXT("each unknown name of a clause is its own DUI3004"), CountOf(Diagnostics, EDreamUIDiagnosticCode::UnknownStyle), 2);
	}
	{
		FBuilt Built(Lines({
			TEXT("style Ring : Fine : Back { }"),
			TEXT("style Back : Ring { }"),
			TEXT("style Fine { RenderOpacity = 0.5 }"),
			TEXT("Widget Root {"),
			TEXT("    Widget Looped : Fine : Ring { }"),
			TEXT("}") }));
		TestEqual(*FString::Printf(TEXT("a cycle through any base is DUI3015, said once (%s)"), *Built.Diagnostics.ToString()),
			CountOf(Built.Diagnostics, EDreamUIDiagnosticCode::StyleCycle), 1);
	}
	return true;
}

// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRingMenuTemplatePlacementTest,
	"DreamGUI.Controls.RingMenu.ATemplatesContentStandsOnItsSlicesMiddleAndTheLabelStepsAside",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRingMenuTemplatePlacementTest::RunTest(const FString& Parameters)
{
	using namespace DreamUITagsAndShorthandsTestLocal;
	FScopedWorld TestWorld;

	TStrongObjectPtr<UDreamRingMenu> Menu(NewObject<UDreamRingMenu>(TestWorld.World));
	Menu->StyleSource = EDreamUIStyleSource::Inline;
	Menu->Style.StartAngle = 0.0f;
	Menu->Style.SweepAngle = 360.0f;
	Menu->Style.InnerRadius = 40.0f;
	Menu->Style.OuterRadius = 120.0f;
	Menu->Style.ContentRadius = 0.0f;
	Menu->Style.ContentWidth = 50.0f;
	for (const TCHAR* Label : { TEXT("A"), TEXT("B"), TEXT("C"), TEXT("D") })
	{
		FDreamRingMenuItem& Item = Menu->Items.AddDefaulted_GetRef();
		Item.Label = FText::AsCultureInvariant(Label);
	}
	Menu->Initialize();
	ON_SCOPE_EXIT
	{
		if (IsValid(Menu.Get()))
		{
			Menu->DestroyWidget();
		}
	};
	Menu->SetWedgeTemplateClass(UDreamUserWidget::StaticClass());

	UDreamWidget* Wedge = Menu->GetWedgeWidget(1);
	if (!TestNotNull(TEXT("the second wedge"), Wedge))
	{
		return false;
	}
	UDreamWidget* Content = Wedge->FindChildByDisplayName(TEXT("WedgeContent"));
	UDreamWidget* BuiltIn = Wedge->FindChildByDisplayName(TEXT("Content"));
	if (!TestNotNull(TEXT("a wedge made after the class was set has its instance"), Content))
	{
		return false;
	}
	// Four even slices from twelve o'clock: the second's middle is at 135 degrees, clockwise -- down and to the right.
	const float Mid = Menu->GetItemMidAngle(1);
	const float Radius = (40.0f + 120.0f) * 0.5f;
	const FVector2D Expected(Radius * FMath::Sin(FMath::DegreesToRadians(Mid)), Radius * FMath::Cos(FMath::DegreesToRadians(Mid)));
	const FDreamUIAnchorData& Anchors = Content->GetAnchorData();
	TestTrue(*FString::Printf(TEXT("centred on the slice's middle, midway between the edges: (%.1f, %.1f), expected (%.1f, %.1f)"),
		Anchors.AnchoredPosition.X, Anchors.AnchoredPosition.Y, Expected.X, Expected.Y),
		Anchors.AnchoredPosition.Equals(Expected, 0.01));
	TestEqual(TEXT("about its own centre"), Anchors.Pivot, FVector2D(0.5, 0.5));
	TestEqual(TEXT("a class whose root says no size is the content width, square"), Anchors.SizeDelta, FVector2D(50, 50));
	TestTrue(TEXT("and the built-in label steps aside"), BuiltIn == nullptr || !BuiltIn->GetWidgetActive());

	Menu->SetWedgeTemplateClass(nullptr);
	UDreamWidget* Rebuilt = Menu->GetWedgeWidget(1);
	TestTrue(TEXT("with the class taken away, the wedges have no instance"),
		Rebuilt != nullptr && Rebuilt->FindChildByDisplayName(TEXT("WedgeContent")) == nullptr);
	UDreamWidget* Back = Rebuilt != nullptr ? Rebuilt->FindChildByDisplayName(TEXT("Content")) : nullptr;
	TestTrue(TEXT("and the label is back"), Back != nullptr && Back->GetWidgetActive());
	return true;
}

#endif
