// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Text/DreamUIAst.h"
#include "Text/DreamUISourceFile.h"
#include "Text/DreamUITextWriteBack.h"

#include "Core/DreamUIAnchorData.h"
#include "Core/DreamUIBehaviour.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamWidget.h"
#include "Interaction/UIButton.h"

#include "UObject/StrongObjectPtr.h"

/*
 * The shape pass of the write-back, straight at ProduceText with no designer: the three shapes a
 * structural edit in the designer depends on, and that were wrong underneath it -- a removal with
 * children (one removal, not one per node), a subtree inserted at once (in rounds, parents first,
 * with the `+` lines its nodes need), and a change of shape beside a value nobody edited (not written).
 */

namespace DreamUITextShapeWriteBackTestLocal
{
	/** The node with this id in a parse of InText, or null. */
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

	bool ParseText(const FString& InText, FDreamUIAst& OutAst)
	{
		FDreamUIDiagnosticBag Diagnostics;
		Diagnostics.SourceName = TEXT("Structure.dui");
		return FDreamUISourceFile::Parse(InText, Diagnostics.SourceName, OutAst, Diagnostics);
	}

	bool HoldsChild(const FDreamUINode* InParent, const FString& InChildId)
	{
		return InParent != nullptr && InParent->Children.ContainsByPredicate(
			[&InChildId](const FDreamUINode& InChild) { return InChild.Id == InChildId; });
	}

	/** A tree built from text, standing in for the designer's template: edited, then handed to ProduceText. */
	struct FLiveTree
	{
		TStrongObjectPtr<UDreamWidgetTree> Tree;

		explicit FLiveTree(const FString& InText)
		{
			FDreamUIAst Ast;
			FDreamUIDiagnosticBag Diagnostics;
			Diagnostics.SourceName = TEXT("Structure.dui");
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

		UDreamWidget* Add(const FString& InDisplayName, UDreamWidget* InParent) const
		{
			UDreamWidget* Added = Tree->ConstructWidget(UDreamWidget::StaticClass(), FName(*InDisplayName), FGuid::NewGuid());
			if (Added != nullptr)
			{
				Added->SetDisplayName(InDisplayName);
				Added->TrySetParent(InParent, /*bKeepWorldPosition*/false);
			}
			return Added;
		}
	};

	FString Lines(const TArray<FString>& InLines)
	{
		return FString::Join(InLines, TEXT("\n"));
	}
}

// =================================================================================================
// The write-back underneath
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIWriteBackNestedRemovalTest,
	"DreamGUI.Text.WriteBack.RemovingANodeWithChildrenIsOneRemoval",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIWriteBackNestedRemovalTest::RunTest(const FString&)
{
	using namespace DreamUITextShapeWriteBackTestLocal;

	const FString Source = Lines({
		TEXT("Widget Root {"),
		TEXT("    Widget Panel {"),
		TEXT("        Widget First {"),
		TEXT("            Widget Deep {"),
		TEXT("            }"),
		TEXT("        }"),
		TEXT("        Widget Second {"),
		TEXT("        }"),
		TEXT("    }"),
		TEXT("    Widget Keep {"),
		TEXT("    }"),
		TEXT("}")
	});
	const FLiveTree Live(Source);
	if (!TestTrue(TEXT("the fixture builds"), Live.Tree.IsValid())) return false;
	UDreamWidget* Panel = Live.Find(TEXT("Panel"));
	if (!TestNotNull(TEXT("Panel is there to remove"), Panel)) return false;
	Panel->DestroyWidget();

	FString Updated;
	FDreamUIDiagnosticBag Diagnostics;
	TestTrue(TEXT("the write-back ran"), FDreamUITextWriteBack::ProduceText(Source, Live.Tree.Get(), Updated, Diagnostics));
	TestFalse(TEXT("the node is gone from the file"), Updated.Contains(TEXT("Panel")));
	TestFalse(TEXT("its descendants with it"), Updated.Contains(TEXT("First")) || Updated.Contains(TEXT("Deep")));
	TestTrue(TEXT("its sibling is not"), Updated.Contains(TEXT("Widget Keep")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIWriteBackSubtreeRoundsTest,
	"DreamGUI.Text.WriteBack.ASubtreeAddedAtOnceIsWrittenWholeInRounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIWriteBackSubtreeRoundsTest::RunTest(const FString&)
{
	using namespace DreamUITextShapeWriteBackTestLocal;

	const FString Source = Lines({
		TEXT("Widget Root {"),
		TEXT("    Widget Existing {"),
		TEXT("    }"),
		TEXT("}")
	});
	const FLiveTree Live(Source);
	if (!TestTrue(TEXT("the fixture builds"), Live.Tree.IsValid())) return false;
	UDreamWidget* Root = Live.Find(TEXT("Root"));
	if (!TestNotNull(TEXT("the root is there"), Root)) return false;

	// Three levels at once, a value at the bottom and a behaviour on the middle: the child of a new node
	// has no parent in the text it is first planned against, and the behaviour has no line until its node does.
	UDreamWidget* Outer = Live.Add(TEXT("Outer"), Root);
	UDreamWidget* Middle = Live.Add(TEXT("Middle"), Outer);
	UDreamWidget* Leaf = Live.Add(TEXT("Leaf"), Middle);
	if (!TestTrue(TEXT("the subtree was made"), Outer != nullptr && Middle != nullptr && Leaf != nullptr)) return false;
	FDreamUIAnchorData Anchors = Leaf->GetAnchorData();
	Anchors.SizeDelta = FVector2D(33.0, 11.0);
	Leaf->SetAnchorData(Anchors);
	TestNotNull(TEXT("the middle node carries a behaviour"), Middle->AddComponent(UUIButton::StaticClass()));

	FString Updated;
	FDreamUIDiagnosticBag Diagnostics;
	TestTrue(TEXT("the write-back ran"), FDreamUITextWriteBack::ProduceText(Source, Live.Tree.Get(), Updated, Diagnostics));

	FDreamUIAst Ast;
	if (!TestTrue(TEXT("the written file parses"), ParseText(Updated, Ast))) return false;
	TestTrue(TEXT("the top of the subtree is under the root"), HoldsChild(&Ast.Root, TEXT("Outer")));
	TestTrue(TEXT("the middle under the top"), HoldsChild(FindNode(Ast, TEXT("Outer")), TEXT("Middle")));
	TestTrue(TEXT("the leaf under the middle"), HoldsChild(FindNode(Ast, TEXT("Middle")), TEXT("Leaf")));
	TestTrue(TEXT("the leaf's value came with it"), Updated.Contains(TEXT("(33, 11)")));
	const FDreamUINode* MiddleNode = FindNode(Ast, TEXT("Middle"));
	TestTrue(TEXT("and the middle node's behaviour has its line"), MiddleNode != nullptr
		&& MiddleNode->Components.ContainsByPredicate([](const FDreamUIComponent& InComponent)
		{
			return InComponent.ClassName == TEXT("UIButton");
		}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIWriteBackShapeKeepsUntouchedValuesTest,
	"DreamGUI.Text.WriteBack.AChangeOfShapeWritesNoValuesIntoNodesNobodyTouched",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIWriteBackShapeKeepsUntouchedValuesTest::RunTest(const FString&)
{
	using namespace DreamUITextShapeWriteBackTestLocal;

	const FString Source = Lines({
		TEXT("Widget Root {"),
		TEXT("    Widget Old {"),
		TEXT("        AnchorData.SizeDelta = (10, 10)"),
		TEXT("    }"),
		TEXT("}")
	});

	// The trap: a value that differs without anybody having edited it -- a panel's arithmetic, in the
	// designer -- standing beside a change of shape.
	auto DriftOld = [](const FLiveTree& InLive)
	{
		if (UDreamWidget* Old = InLive.Find(TEXT("Old")))
		{
			FDreamUIAnchorData Anchors = Old->GetAnchorData();
			Anchors.SizeDelta = FVector2D(99.0, 99.0);
			Old->SetAnchorData(Anchors);
		}
	};

	{
		const FLiveTree Live(Source);
		if (!TestTrue(TEXT("the fixture builds"), Live.Tree.IsValid())) return false;
		DriftOld(Live);
		Live.Add(TEXT("Fresh"), Live.Find(TEXT("Root")));

		FString Updated;
		FDreamUIDiagnosticBag Diagnostics;
		TestTrue(TEXT("the write-back ran"), FDreamUITextWriteBack::ProduceText(Source, Live.Tree.Get(), Updated, Diagnostics));
		TestTrue(TEXT("the new node is written"), Updated.Contains(TEXT("Widget Fresh")));
		TestTrue(TEXT("the untouched node keeps its line"), Updated.Contains(TEXT("AnchorData.SizeDelta = (10, 10)")));
		TestFalse(TEXT("and the drift is not written into it"), Updated.Contains(TEXT("(99, 99)")));
	}

	// The control: the same drift as an ordinary flush with nobody reporting is the sweep's, as before.
	{
		const FLiveTree Live(Source);
		if (!TestTrue(TEXT("the fixture builds again"), Live.Tree.IsValid())) return false;
		DriftOld(Live);

		FString Updated;
		FDreamUIDiagnosticBag Diagnostics;
		TestTrue(TEXT("the write-back ran"), FDreamUITextWriteBack::ProduceText(Source, Live.Tree.Get(), Updated, Diagnostics));
		TestTrue(TEXT("a value flush with no report still sweeps"), Updated.Contains(TEXT("(99, 99)")));
	}
	return true;
}

#endif
