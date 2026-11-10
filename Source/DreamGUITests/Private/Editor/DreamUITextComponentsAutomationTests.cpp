// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "DreamTestCulture.h"
#include "DreamUITextStructureTestFixtures.h"

#include "Designer/DreamUITextAuthoringGate.h"
#include "Designer/DreamWidgetTreeEditing.h"
#include "Text/DreamUITextWriteBack.h"
#include "Core/DreamUIBehaviour.h"
#include "Core/Components/DreamImage.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Interaction/UIButton.h"
#include "Interaction/UIEventTrigger.h"

#include "UObject/StrongObjectPtr.h"

/*
 * What the designer changes about a node without adding or removing one -- its behaviours, its visual, its panel
 * -- on a `.dui`-backed hierarchy. The write-back reads each off the two trees: a `+` line inserted, removed or moved
 * to follow the objects the node holds, and the node's type rewritten when its visual or its panel (when the type is
 * the panel's name) is another, with the lines the new type has no property for taken out and said.
 */

namespace DreamUITextComponentsTestLocal
{
	using namespace DreamUITextStructureFixtures;

	struct FLiveTree
	{
		TStrongObjectPtr<UDreamWidgetTree> Tree;

		explicit FLiveTree(const FString& InText)
		{
			FDreamUIAst Ast;
			FDreamUIDiagnosticBag Diagnostics;
			Diagnostics.SourceName = TEXT("Components.dui");
			Tree.Reset(FDreamUITextWriteBack::BuildReferenceTree(InText, Ast, Diagnostics));
		}

		UDreamWidget* Find(const FString& InDisplayName) const
		{
			UDreamWidget* Found = nullptr;
			if (Tree.IsValid())
			{
				Tree->ForEachWidget([&Found, &InDisplayName](UDreamWidget* Widget)
				{
					if (Found == nullptr && Widget->GetDisplayName() == InDisplayName)
					{
						Found = Widget;
					}
				});
			}
			return Found;
		}
	};

	FString Lines(const TArray<FString>& InLines)
	{
		return FString::Join(InLines, TEXT("\n"));
	}

	/** A node's `+` lines as `A,B`, in the order the file writes them. */
	FString PlusLines(const FString& InText, const TCHAR* InNodeId)
	{
		FDreamUIAst Ast;
		if (!ParseText(InText, Ast))
		{
			return TEXT("<does not parse>");
		}
		TArray<FString> Names;
		if (const FDreamUINode* Node = FindNode(Ast, InNodeId))
		{
			for (const FDreamUIComponent& Component : Node->Components)
			{
				Names.Add(Component.ClassName);
			}
		}
		return FString::Join(Names, TEXT(","));
	}

	template<typename BehaviourType>
	BehaviourType* FindBehaviour(const UDreamWidget* InWidget)
	{
		if (InWidget != nullptr)
		{
			for (UDreamUIBehaviour* Behaviour : InWidget->GetAllComponents())
			{
				if (BehaviourType* Found = Cast<BehaviourType>(Behaviour))
				{
					return Found;
				}
			}
		}
		return nullptr;
	}
}

// =================================================================================================
// The write-back
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIWriteBackBehaviourLinesTest,
	"DreamGUI.Text.WriteBack.ABehaviourAddedRemovedOrReorderedOnALiveNodeReachesItsPlusLines",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIWriteBackBehaviourLinesTest::RunTest(const FString&)
{
	using namespace DreamUITextComponentsTestLocal;

	const FString One = Lines({
		TEXT("Widget Root {"),
		TEXT("    Widget Panel {"),
		TEXT("        + UIButton { }"),
		TEXT("    }"),
		TEXT("}")
	});
	{
		const FLiveTree Live(One);
		UDreamWidget* Panel = Live.Find(TEXT("Panel"));
		if (!TestNotNull(TEXT("the fixture builds"), Panel)) return false;
		TestNotNull(TEXT("a behaviour is added to the live node"), Panel->AddComponent(UUIEventTrigger::StaticClass()));
		FString Updated;
		FDreamUIDiagnosticBag Diagnostics;
		FDreamUIStructuralResidue Residue;
		TestTrue(TEXT("the write-back ran"), FDreamUITextWriteBack::ProduceText(One, Live.Tree.Get(), Updated, Diagnostics, nullptr, nullptr, &Residue));
		TestEqual(TEXT("its line is written after the one already there"), PlusLines(Updated, TEXT("Panel")), FString(TEXT("UIButton,UIEventTrigger")));
		TestTrue(TEXT("nothing left over"), Residue.IsEmpty());
	}
	{
		const FLiveTree Live(One);
		UDreamWidget* Panel = Live.Find(TEXT("Panel"));
		if (!TestNotNull(TEXT("the fixture builds"), Panel)) return false;
		Panel->RemoveComponent(FindBehaviour<UUIButton>(Panel));
		FString Updated;
		FDreamUIDiagnosticBag Diagnostics;
		FDreamUIStructuralResidue Residue;
		TestTrue(TEXT("the write-back ran"), FDreamUITextWriteBack::ProduceText(One, Live.Tree.Get(), Updated, Diagnostics, nullptr, nullptr, &Residue));
		TestEqual(TEXT("a behaviour taken off takes its line with it"), PlusLines(Updated, TEXT("Panel")), FString());
		TestTrue(TEXT("nothing left over"), Residue.IsEmpty());
	}

	const FString Two = Lines({
		TEXT("Widget Root {"),
		TEXT("    Widget Panel {"),
		TEXT("        + UIButton { }"),
		TEXT("        + UIEventTrigger { }"),
		TEXT("    }"),
		TEXT("}")
	});
	{
		const FLiveTree Live(Two);
		UDreamWidget* Panel = Live.Find(TEXT("Panel"));
		if (!TestNotNull(TEXT("the second fixture builds"), Panel)) return false;
		UUIEventTrigger* Trigger = FindBehaviour<UUIEventTrigger>(Panel);
		if (!TestNotNull(TEXT("with both behaviours"), Trigger)) return false;
		Panel->MoveComponentToIndex(Trigger, 0);
		FString Updated;
		FDreamUIDiagnosticBag Diagnostics;
		FDreamUIStructuralResidue Residue;
		TestTrue(TEXT("the write-back ran"), FDreamUITextWriteBack::ProduceText(Two, Live.Tree.Get(), Updated, Diagnostics, nullptr, nullptr, &Residue));
		TestEqual(TEXT("the lines follow the new order"), PlusLines(Updated, TEXT("Panel")), FString(TEXT("UIEventTrigger,UIButton")));
		TestTrue(TEXT("nothing left over"), Residue.IsEmpty());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIWriteBackRetypeTest,
	"DreamGUI.Text.WriteBack.AVisualOrPanelSwappedOnALiveNodeRetypesItAndDropsWhatItCannotHold",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIWriteBackRetypeTest::RunTest(const FString&)
{
	using namespace DreamUITextComponentsTestLocal;

	{
		const FString Source = Lines({
			TEXT("Widget Root {"),
			TEXT("    Text Label {"),
			TEXT("        Text = \"Hi\""),
			TEXT("        AnchorData.SizeDelta = (10, 10)"),
			TEXT("    }"),
			TEXT("}")
		});
		const FLiveTree Live(Source);
		UDreamWidget* Label = Live.Find(TEXT("Label"));
		if (!TestNotNull(TEXT("the fixture builds"), Label)) return false;
		TestNotNull(TEXT("the visual is swapped"), Label->CreateNewVisual(UDreamImage::StaticClass()));
		FString Updated;
		FDreamUIDiagnosticBag Diagnostics;
		FDreamUIStructuralResidue Residue;
		TestTrue(TEXT("the write-back ran"), FDreamUITextWriteBack::ProduceText(Source, Live.Tree.Get(), Updated, Diagnostics, nullptr, nullptr, &Residue));
		TestTrue(TEXT("the node is written with the new visual's type"), Updated.Contains(TEXT("Image Label")));
		TestFalse(TEXT("the text the image cannot show is taken out"), Updated.Contains(TEXT("\"Hi\"")));
		TestTrue(TEXT("what it can hold stays"), Updated.Contains(TEXT("AnchorData.SizeDelta = (10, 10)")));
		TestTrue(TEXT("and the loss is said"), Residue.Dropped.ContainsByPredicate([](const FString& InLine) { return InLine.StartsWith(TEXT("Label.Text")); }));
		TestTrue(TEXT("nothing failed"), Residue.IsEmpty());
		FDreamUIAst Ast;
		TestTrue(TEXT("the written file parses"), ParseText(Updated, Ast));
	}
	{
		const FString Source = Lines({
			TEXT("Widget Root {"),
			TEXT("    VerticalBox Column {"),
			TEXT("        Spacing = 7"),
			TEXT("        Widget Item {"),
			TEXT("        }"),
			TEXT("    }"),
			TEXT("}")
		});
		const FLiveTree Live(Source);
		UDreamWidget* Column = Live.Find(TEXT("Column"));
		if (!TestNotNull(TEXT("the panel fixture builds"), Column)) return false;
		TestNotNull(TEXT("the panel is replaced"), Column->CreateNewLayoutContainer(UDreamLayoutContainerCanvasPanel::StaticClass()));
		FString Updated;
		FDreamUIDiagnosticBag Diagnostics;
		FDreamUIStructuralResidue Residue;
		TestTrue(TEXT("the write-back ran"), FDreamUITextWriteBack::ProduceText(Source, Live.Tree.Get(), Updated, Diagnostics, nullptr, nullptr, &Residue));
		TestTrue(TEXT("the type that names the panel names the new one"), Updated.Contains(TEXT("CanvasPanel Column")));
		TestFalse(TEXT("the old panel's spacing is taken out"), Updated.Contains(TEXT("Spacing")));
		TestTrue(TEXT("and said"), Residue.Dropped.ContainsByPredicate([](const FString& InLine) { return InLine.StartsWith(TEXT("Column.Spacing")); }));
		TestTrue(TEXT("its child stays"), Updated.Contains(TEXT("Widget Item")));
		TestTrue(TEXT("nothing failed"), Residue.IsEmpty());
	}
	return true;
}

// =================================================================================================
// The designer
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUITextBehaviourGesturesTest,
	"DreamGUI.Designer.Text.ABehaviourAddedOrRemovedInTheDesignerIsWrittenAndAStylesIsNotRemoved",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUITextBehaviourGesturesTest::RunTest(const FString&)
{
	using namespace DreamUITextComponentsTestLocal;

	FScopedDuiFile Source(TEXT("Components.dui"));
	if (!TestTrue(TEXT("the fixture wrote a .dui"), Source.Write({
		TEXT("style Clicky {"),
		TEXT("    + UIButton { }"),
		TEXT("}"),
		TEXT("Widget Root {"),
		TEXT("    + CanvasPanel"),
		TEXT("    Widget Plain {"),
		TEXT("    }"),
		TEXT("    Widget Styled : Clicky {"),
		TEXT("    }"),
		TEXT("}")
	})))
	{
		return false;
	}
	AddExpectedError(TEXT("Refusing to remove the behaviour 'UIButton'.*Components\\.dui"), EAutomationExpectedErrorFlags::Contains, 0);

	FScopedTextBlueprint Scoped(TEXT("BP_Components"), Source.FilePath);
	if (!TestNotNull(TEXT("the Blueprint was created"), Scoped.Blueprint)) return false;
	if (!TestEqual(TEXT("and its .dui compiled clean"), Scoped.CompileErrors, 0)) return false;
	if (!TestNotNull(TEXT("the designer opened"), Scoped.Designer)) return false;

	const TArray<UClass*> Added{ UUIEventTrigger::StaticClass() };
	Scoped.Designer->DesignerAddComponents(Scoped.FindPreview(TEXT("Plain")), Added);
	TestEqual(TEXT("the behaviour is written as the node's line"), PlusLines(Source.Read(), TEXT("Plain")), FString(TEXT("UIEventTrigger")));
	TestNotNull(TEXT("and the compiled tree has it"), FindBehaviour<UUIEventTrigger>(Scoped.FindTemplate(TEXT("Plain"))));

	UDreamWidget* PlainPreview = Scoped.FindPreview(TEXT("Plain"));
	UUIEventTrigger* PreviewTrigger = FindBehaviour<UUIEventTrigger>(PlainPreview);
	if (TestNotNull(TEXT("the behaviour has a preview to remove"), PreviewTrigger))
	{
		TestTrue(TEXT("removing it goes through"), Scoped.Designer->DesignerRemoveComponent(PlainPreview, PreviewTrigger));
		TestEqual(TEXT("and takes its line out"), PlusLines(Source.Read(), TEXT("Plain")), FString());
	}

	// The style's behaviour is the style's: the node has no line to take out, and the next compile would build it again.
	const FString Before = Source.Read();
	UDreamWidget* StyledPreview = Scoped.FindPreview(TEXT("Styled"));
	UUIButton* StyleButton = FindBehaviour<UUIButton>(StyledPreview);
	if (TestNotNull(TEXT("the styled node has the style's behaviour"), StyleButton))
	{
		// The refusal is logged, and matched by the expected error above in the English it is written in.
		const DreamTests::FScopedEnglish English;
		TestFalse(TEXT("removing a behaviour its style gives it is refused"), Scoped.Designer->DesignerRemoveComponent(StyledPreview, StyleButton));
		TestEqual(TEXT("and the file is untouched"), Source.Read(), Before);
	}
	return true;
}

#endif
